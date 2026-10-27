#include "app.h"
#include <mbedtls/threading.h>
#include <psa/crypto.h>
#include <psa/crypto_extra.h>
#include <bcrypt.h>
#include <new>

HWND g_main=0,g_video=0;
WCHAR g_profile[MAX_PATH],g_exe_dir[MAX_PATH],g_log_path[MAX_PATH];
volatile LONG g_stop_requested=0,g_streaming=0;
static CRITICAL_SECTION logger_lock;
static HANDLE logger_file=INVALID_HANDLE_VALUE;
static char log_text[120000];static size_t log_size;
static char disk_pending[262144];static size_t disk_size;
static HANDLE log_wake=nullptr,log_thread=nullptr;static volatile LONG log_stopping=0;
static LARGE_INTEGER frequency;
void*operator new(size_t n){void*p=malloc(n?n:1);if(!p)ExitProcess(ERROR_NOT_ENOUGH_MEMORY);return p;}
void*operator new[](size_t n){return operator new(n);}
void operator delete(void*p)noexcept{free(p);}void operator delete[](void*p)noexcept{free(p);}
void operator delete(void*p,size_t)noexcept{free(p);}void operator delete[](void*p,size_t)noexcept{free(p);}
extern "C" void __cxa_pure_virtual(){TerminateProcess(GetCurrentProcess(),0xc0000005);for(;;){}}
extern "C" void __stack_chk_fail(){TerminateProcess(GetCurrentProcess(),0xc0000409);for(;;){}}
static void mutex_init(mbedtls_threading_mutex_t*m){InitializeCriticalSection(&m->lock);m->valid=1;}
static void mutex_free(mbedtls_threading_mutex_t*m){if(m->valid){DeleteCriticalSection(&m->lock);m->valid=0;}}
static int mutex_lock(mbedtls_threading_mutex_t*m){if(!m||!m->valid)return MBEDTLS_ERR_THREADING_BAD_INPUT_DATA;EnterCriticalSection(&m->lock);return 0;}
static int mutex_unlock(mbedtls_threading_mutex_t*m){if(!m||!m->valid)return MBEDTLS_ERR_THREADING_BAD_INPUT_DATA;LeaveCriticalSection(&m->lock);return 0;}
void log_lock(){EnterCriticalSection(&logger_lock);}void log_unlock(){LeaveCriticalSection(&logger_lock);}
const char*get_log_text(){return log_text;}
static DWORD WINAPI log_writer(void*){
 char batch[65536];DWORD last_flush=GetTickCount();bool dirty=false;
 for(;;){WaitForSingleObject(log_wake,250);
  for(;;){log_lock();size_t n=disk_size<sizeof(batch)?disk_size:sizeof(batch);if(n){memcpy(batch,disk_pending,n);memmove(disk_pending,disk_pending+n,disk_size-n);disk_size-=n;}log_unlock();
   if(!n)break;if(logger_file!=INVALID_HANDLE_VALUE){DWORD done;WriteFile(logger_file,batch,(DWORD)n,&done,nullptr);dirty=true;}
  }
  if(dirty&&logger_file!=INVALID_HANDLE_VALUE&&(log_stopping||GetTickCount()-last_flush>=1000)){FlushFileBuffers(logger_file);last_flush=GetTickCount();dirty=false;}
  if(log_stopping)return 0;
 }
}
bool log_snapshot(WCHAR*out,int capacity){if(!TryEnterCriticalSection(&logger_lock))return false;utf8_to_wide(log_text,out,capacity);log_unlock();return true;}
void app_log(const char*fmt,...){
 char line[4000];va_list ap;va_start(ap,fmt);int count=vsnprintf(line,sizeof(line)-3,fmt,ap);va_end(ap);
 if(count<0)return;size_t n=(size_t)count;if(n>sizeof(line)-3)n=sizeof(line)-3;
 if(strstr(line,"rikey=")||strstr(line,"clientcert=")||strstr(line,"clientpairingsecret=")||strstr(line,"PRIVATE KEY")){
  strcpy(line,"[sensitive protocol data redacted]");n=strlen(line);
 }
 line[n++]='\r';line[n++]='\n';line[n]=0;
 log_lock();if(disk_size+n<=sizeof(disk_pending)){memcpy(disk_pending+disk_size,line,n);disk_size+=n;}
 if(log_size+n>=sizeof(log_text)){size_t keep=sizeof(log_text)/2;memmove(log_text,log_text+log_size-keep,keep);log_size=keep;}
 memcpy(log_text+log_size,line,n+1);log_size+=n;log_unlock();if(log_wake)SetEvent(log_wake);
}
void app_status(const WCHAR*message){
 if(!g_main)return;size_t bytes=((size_t)lstrlenW(message)+1)*sizeof(WCHAR);WCHAR*p=(WCHAR*)malloc(bytes);
 if(p){memcpy(p,message,bytes);if(!PostMessageW(g_main,WM_APP_STATUS,0,(LPARAM)p))free(p);}
}
bool app_check(HRESULT hr,const char*what){if(FAILED(hr)){app_log("FAIL %s: 0x%08x",what,(unsigned)hr);return false;}return true;}
uint64_t clock_us(){LARGE_INTEGER now;QueryPerformanceCounter(&now);return(uint64_t)((double)now.QuadPart*1000000.0/(double)frequency.QuadPart);}
bool random_bytes(void*data,size_t n){return n<=0xffffffff&&BCryptGenRandom(nullptr,(PUCHAR)data,(ULONG)n,BCRYPT_USE_SYSTEM_PREFERRED_RNG)==0;}
bool profile_path(const WCHAR*name,WCHAR*out){if(lstrlenW(g_profile)+lstrlenW(name)+2>=MAX_PATH)return false;lstrcpyW(out,g_profile);lstrcatW(out,L"\\");lstrcatW(out,name);return true;}
bool read_file(const WCHAR*path,Buffer&out){
 HANDLE f=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);if(f==INVALID_HANDLE_VALUE)return false;
 DWORD high=0,size=GetFileSize(f,&high),got=0;bool ok=!high&&size<=16*1024*1024&&out.reserve(size)&&ReadFile(f,out.data,size,&got,nullptr)&&got==size;
 CloseHandle(f);if(ok){out.size=size;out.data[size]=0;}return ok;
}
bool write_file(const WCHAR*path,const void*data,size_t length){
 if(length>16*1024*1024)return false;
 WCHAR temp[MAX_PATH];if(lstrlenW(path)+5>=MAX_PATH)return false;lstrcpyW(temp,path);lstrcatW(temp,L".tmp");
 HANDLE f=CreateFileW(temp,GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);if(f==INVALID_HANDLE_VALUE)return false;
 DWORD done=0;bool ok=WriteFile(f,data,(DWORD)length,&done,nullptr)&&done==length&&FlushFileBuffers(f);CloseHandle(f);
 if(ok)ok=MoveFileExW(temp,path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
 if(!ok)DeleteFileW(temp);return ok;
}
void utf8_to_wide(const char*i,WCHAR*out,int n){if(!MultiByteToWideChar(CP_UTF8,0,i,-1,out,n)&&n)out[0]=0;}
void wide_to_utf8(const WCHAR*i,char*out,int n){if(!WideCharToMultiByte(CP_UTF8,0,i,-1,out,n,nullptr,nullptr)&&n)out[0]=0;}
bool hex_encode(const void*p,size_t n,Buffer&out){if(!out.reserve(n*2))return false;const unsigned char*s=(const unsigned char*)p;const char*h="0123456789abcdef";for(size_t i=0;i<n;i++){out.data[2*i]=h[s[i]>>4];out.data[2*i+1]=h[s[i]&15];}out.size=2*n;out.data[out.size]=0;return true;}
bool hex_decode(const char*s,Buffer&out){size_t n=strlen(s);if(n%2||!out.reserve(n/2))return false;for(size_t i=0;i<n;i+=2){int b=0;for(int j=0;j<2;j++){char c=s[i+j];int v=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;if(v<0)return false;b=b*16+v;}out.data[i/2]=(unsigned char)b;}out.size=n/2;out.data[out.size]=0;return true;}
bool constant_equal(const void*a,const void*b,size_t n){const unsigned char*x=(const unsigned char*)a,*y=(const unsigned char*)b;unsigned v=0;for(size_t i=0;i<n;i++)v|=x[i]^y[i];return v==0;}
void runtime_init(const WCHAR*override_path){
 InitializeCriticalSection(&logger_lock);QueryPerformanceFrequency(&frequency);
 GetModuleFileNameW(nullptr,g_exe_dir,MAX_PATH);for(int i=lstrlenW(g_exe_dir)-1;i>=0;i--)if(g_exe_dir[i]=='\\'){g_exe_dir[i+1]=0;break;}
 if(override_path&&*override_path)lstrcpynW(g_profile,override_path,MAX_PATH);
 else{GetEnvironmentVariableW(L"LOCALAPPDATA",g_profile,MAX_PATH);if(lstrlenW(g_profile)<MAX_PATH-20)lstrcatW(g_profile,L"\\MoonlightRT81");}
 CreateDirectoryW(g_profile,nullptr);
 SYSTEMTIME t;GetLocalTime(&t);WCHAR file[80];wsprintfW(file,L"moonlight-%04u%02u%02u-%02u%02u%02u.log",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond);
 if(profile_path(file,g_log_path))logger_file=CreateFileW(g_log_path,GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
 log_wake=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(log_wake)log_thread=CreateThread(nullptr,0,log_writer,nullptr,0,nullptr);
 mbedtls_threading_set_alt(mutex_init,mutex_free,mutex_lock,mutex_unlock);
 psa_status_t rc=psa_crypto_init();app_log("Moonlight RT81 native client 0.2.1; PSA init=%d",(int)rc);
#if defined(__arm__)
 app_log("Architecture=ARM32; target Windows RT 8.1; native Media Foundation/D3D11 backend");
#else
 app_log("Architecture=x64 development validation build; not for Windows RT");
#endif
 WSADATA ws;int wsa=WSAStartup(MAKEWORD(2,2),&ws);app_log("WSAStartup=%d",wsa);
}
void runtime_close(){WSACleanup();mbedtls_psa_crypto_free();mbedtls_threading_free_alt();InterlockedExchange(&log_stopping,1);if(log_thread){SetEvent(log_wake);if(WaitForSingleObject(log_thread,2000)!=WAIT_OBJECT_0)return;CloseHandle(log_thread);}if(log_wake)CloseHandle(log_wake);if(logger_file!=INVALID_HANDLE_VALUE)CloseHandle(logger_file);DeleteCriticalSection(&logger_lock);}
