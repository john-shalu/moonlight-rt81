#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define NOMINMAX
#include <winsock2.h>
#include <windows.h>
#include <objbase.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <Limelight.h>
#include "input_queue.h"

template<class T> inline void release(T*&p){if(p){p->Release();p=0;}}
struct Buffer {
 unsigned char*data=nullptr;size_t size=0,capacity=0;bool secret=false;
 Buffer(bool sensitive=false):secret(sensitive){}
 Buffer(const Buffer&)=delete;Buffer&operator=(const Buffer&)=delete;
 ~Buffer(){clear();}
 void clear(){if(data){if(secret)SecureZeroMemory(data,capacity);free(data);}data=nullptr;size=capacity=0;}
 bool reserve(size_t n){if(n>16*1024*1024)return false;if(n+1<=capacity)return true;size_t c=n+1;void*p=realloc(data,c);if(!p)return false;data=(unsigned char*)p;capacity=c;return true;}
 bool assign(const void*p,size_t n){if(!reserve(n))return false;if(n)memcpy(data,p,n);size=n;data[size]=0;return true;}
 bool append(const void*p,size_t n){if(n>16*1024*1024-size||!reserve(size+n))return false;if(n)memcpy(data+size,p,n);size+=n;data[size]=0;return true;}
 bool text(const char*s){return assign(s,strlen(s));}
 bool add(const char*s){return append(s,strlen(s));}
 const char*c_str()const{return data?(const char*)data:"";}
};
extern HWND g_main,g_video;
extern WCHAR g_profile[MAX_PATH],g_exe_dir[MAX_PATH],g_log_path[MAX_PATH];
extern volatile LONG g_stop_requested,g_streaming;
enum{WM_APP_LOG=WM_APP+1,WM_APP_STATUS,WM_APP_APPS,WM_APP_FINISHED,WM_APP_STOP,WM_APP_ART,WM_APP_STREAM_ACTIVE,WM_APP_STREAM_INACTIVE};
void app_log(const char*fmt,...);
void app_status(const WCHAR*message);
bool app_check(HRESULT hr,const char*operation);
void runtime_init(const WCHAR*profile_override);
void runtime_close();
uint64_t clock_us();
bool random_bytes(void*data,size_t length);
bool read_file(const WCHAR*path,Buffer&out);
bool write_file(const WCHAR*path,const void*data,size_t length);
bool profile_path(const WCHAR*name,WCHAR*out);
void utf8_to_wide(const char*input,WCHAR*out,int capacity);
void wide_to_utf8(const WCHAR*input,char*out,int capacity);
bool hex_encode(const void*data,size_t size,Buffer&out);
bool hex_decode(const char*text,Buffer&out);
bool constant_equal(const void*a,const void*b,size_t size);
const char*get_log_text();
void log_lock();void log_unlock();
bool log_snapshot(WCHAR*out,int capacity);
bool input_session_begin();void input_session_end();bool input_session_active();
int input_enqueue(const InputEvent&);void input_release_all();DWORD input_call_age();

struct HostState;
struct VideoState;
struct AudioState;
HostState*host_create();void host_destroy(HostState*);
void host_cancel(HostState*);
bool host_set_address(HostState*,const char*address,unsigned port);
bool host_query(HostState*,bool secure);
bool host_pair(HostState*,const char*pin);
bool host_app_list(HostState*);
int host_app_count(HostState*);const char*host_app_name(HostState*,int);int host_app_id(HostState*,int);
bool host_is_paired(HostState*);
bool host_has_pin(HostState*);
bool host_app_art(HostState*,int app_id,Buffer&);
bool host_crypto_selftest();
bool host_launch(HostState*,int app_id,STREAM_CONFIGURATION*,SERVER_INFORMATION*);
const char*host_name(HostState*);
VideoState*video_create(HWND);
void video_destroy(VideoState*);
void video_callbacks(VideoState*,DECODER_RENDERER_CALLBACKS*);
void video_log_stats(VideoState*);
bool video_has_frames(VideoState*);
bool video_local_test(VideoState*,const WCHAR*folder);
AudioState*audio_create();void audio_destroy(AudioState*);
void audio_callbacks(AudioState*,AUDIO_RENDERER_CALLBACKS*);
bool audio_selftest();
bool audio_has_packets(AudioState*);
void audio_muted(AudioState*,bool muted);
HBITMAP decode_artwork(const Buffer&);
