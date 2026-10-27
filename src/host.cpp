#include "app.h"
#include <ws2tcpip.h>
#include <wincrypt.h>
#include <pugixml.hpp>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/x509.h>
#include <mbedtls/pk.h>
#include <mbedtls/rsa.h>
#include <mbedtls/sha256.h>
#include <mbedtls/aes.h>
#include <mbedtls/error.h>
#include <mbedtls/entropy.h>
#include <mbedtls/net_sockets.h>

static int rng(void*,unsigned char*p,size_t n){return random_bytes(p,n)?0:MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;}
static bool crypto_ok(int rc,const char*where){if(rc){char msg[200];mbedtls_strerror(rc,msg,sizeof(msg));app_log("%s failed: %d (%s)",where,rc,msg);return false;}return true;}
struct Identity {
 mbedtls_pk_context key;mbedtls_x509_crt cert;Buffer pem;char uid[33];bool ready=false;
 Identity(){mbedtls_pk_init(&key);mbedtls_x509_crt_init(&cert);uid[0]=0;}
 ~Identity(){mbedtls_x509_crt_free(&cert);mbedtls_pk_free(&key);}
 bool open(){
  if(ready)return true;WCHAR keypath[MAX_PATH],certpath[MAX_PATH],idpath[MAX_PATH];
  if(!profile_path(L"identity-key.dpapi",keypath)||!profile_path(L"identity-cert.pem",certpath)||!profile_path(L"device-id.txt",idpath))return false;
  Buffer encoded(true),plain(true),id;
  bool have_key=GetFileAttributesW(keypath)!=INVALID_FILE_ATTRIBUTES,have_cert=GetFileAttributesW(certpath)!=INVALID_FILE_ATTRIBUTES;
  if(have_key||have_cert){
   if(!read_file(keypath,encoded)||!read_file(certpath,pem)||!read_file(idpath,id)){app_log("Identity files incomplete; existing identity will not be replaced automatically.");return false;}
   DATA_BLOB input={(DWORD)encoded.size,encoded.data},output={};
   if(!CryptUnprotectData(&input,nullptr,nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&output)){app_log("Identity key cannot be decrypted for this Windows user: %u",GetLastError());return false;}
   bool copied=plain.assign(output.pbData,output.cbData);SecureZeroMemory(output.pbData,output.cbData);LocalFree(output.pbData);if(!copied)return false;
   if(!crypto_ok(mbedtls_pk_parse_key(&key,plain.data,plain.size+1,nullptr,0,rng,nullptr),"Load identity key")||
      !crypto_ok(mbedtls_x509_crt_parse(&cert,pem.data,pem.size+1),"Load identity certificate"))return false;
   if(id.size!=16){app_log("Invalid device ID");return false;}memcpy(uid,id.data,16);uid[16]=0;
   if(!crypto_ok(mbedtls_pk_check_pair(&cert.pk,&key,rng,nullptr),"Identity key/certificate match"))return false;
  }else{
   app_status(L"首次运行：正在生成配对身份，请稍候…");
   if(!crypto_ok(mbedtls_pk_setup(&key,mbedtls_pk_info_from_type(MBEDTLS_PK_RSA)),"RSA setup")||
      !crypto_ok(mbedtls_rsa_gen_key(mbedtls_pk_rsa(key),rng,nullptr,2048,65537),"Generate RSA identity"))return false;
   unsigned char keypem[8192]={},certpem[8192]={},serial_bytes[16],device_id[8];
   if(!random_bytes(serial_bytes,16)||!random_bytes(device_id,8))return false;serial_bytes[0]&=0x7f;serial_bytes[0]|=1;
   mbedtls_mpi serial;mbedtls_mpi_init(&serial);mbedtls_mpi_read_binary(&serial,serial_bytes,16);
   mbedtls_x509write_cert writer;mbedtls_x509write_crt_init(&writer);mbedtls_x509write_crt_set_version(&writer,MBEDTLS_X509_CRT_VERSION_3);
   mbedtls_x509write_crt_set_md_alg(&writer,MBEDTLS_MD_SHA256);mbedtls_x509write_crt_set_subject_key(&writer,&key);mbedtls_x509write_crt_set_issuer_key(&writer,&key);
   SYSTEMTIME t;GetSystemTime(&t);char before[20],after[20];snprintf(before,sizeof(before),"%04d0101000000",(int)t.wYear-1);snprintf(after,sizeof(after),"%04d1231235959",(int)t.wYear+10);
   int rc=mbedtls_x509write_crt_set_serial(&writer,&serial);
   if(!rc)rc=mbedtls_x509write_crt_set_subject_name(&writer,"CN=Moonlight RT81 Client");
   if(!rc)rc=mbedtls_x509write_crt_set_issuer_name(&writer,"CN=Moonlight RT81 Client");
   if(!rc)rc=mbedtls_x509write_crt_set_validity(&writer,before,after);
   if(!rc)rc=mbedtls_x509write_crt_set_basic_constraints(&writer,0,-1);
   if(!rc)rc=mbedtls_x509write_crt_set_key_usage(&writer,MBEDTLS_X509_KU_DIGITAL_SIGNATURE|MBEDTLS_X509_KU_KEY_ENCIPHERMENT);
   if(!rc)rc=mbedtls_x509write_crt_pem(&writer,certpem,sizeof(certpem),rng,nullptr);
   mbedtls_x509write_crt_free(&writer);mbedtls_mpi_free(&serial);if(!crypto_ok(rc,"Create client certificate"))return false;
   if(!crypto_ok(mbedtls_pk_write_key_pem(&key,keypem,sizeof(keypem)),"Encode private key"))return false;
   DATA_BLOB input={(DWORD)strlen((char*)keypem),keypem},output={};
   bool saved=CryptProtectData(&input,L"Moonlight RT81 identity",nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&output)!=FALSE;
   if(!saved)app_log("CryptProtectData failed: Win32=%u",GetLastError());
   SecureZeroMemory(keypem,sizeof(keypem));
   Buffer uidhex;if(!hex_encode(device_id,8,uidhex))saved=false;
   if(saved)saved=write_file(keypath,output.pbData,output.cbData)&&write_file(certpath,certpem,strlen((char*)certpem))&&write_file(idpath,uidhex.data,uidhex.size);
   if(output.pbData){SecureZeroMemory(output.pbData,output.cbData);LocalFree(output.pbData);}if(!saved){app_log("Saving identity failed: Win32=%u",GetLastError());return false;}
   pem.text((char*)certpem);memcpy(uid,uidhex.data,16);uid[16]=0;
   if(!crypto_ok(mbedtls_x509_crt_parse(&cert,pem.data,pem.size+1),"Parse new client certificate"))return false;
  }
  ready=true;app_log("Client identity ready (private key protected with per-user DPAPI)");return true;
 }
};
struct HostState {
 CRITICAL_SECTION network_lock;SOCKET active_socket=INVALID_SOCKET;
 Identity identity;char address[256]={},name[256]={},version[80]={},gfe[80]={};unsigned http_port=47989,https_port=47984;bool paired=false;int current_game=0,codec_modes=SCM_H264;
 char session_url[1024]={};Buffer pinned_pem;mbedtls_x509_crt pinned;WCHAR pin_path[MAX_PATH];
 struct App{char name[512];int id;}apps[128];int count=0;
 HostState(){InitializeCriticalSection(&network_lock);mbedtls_x509_crt_init(&pinned);pin_path[0]=0;}
 ~HostState(){mbedtls_x509_crt_free(&pinned);DeleteCriticalSection(&network_lock);}
 bool pin(const unsigned char*data,size_t n){mbedtls_x509_crt_free(&pinned);mbedtls_x509_crt_init(&pinned);return crypto_ok(mbedtls_x509_crt_parse(&pinned,data,n+1),"Parse pinned Sunshine certificate")&&pinned_pem.assign(data,n);}
};
static int verify_tls(void*context,mbedtls_x509_crt*cert,int depth,uint32_t*flags){
 HostState*h=(HostState*)context;
 if(depth==0){bool match=h->pinned.raw.len==cert->raw.len&&h->pinned.raw.len&&constant_equal(h->pinned.raw.p,cert->raw.p,cert->raw.len);*flags=match?0:MBEDTLS_X509_BADCERT_NOT_TRUSTED;return match?0:MBEDTLS_ERR_X509_CERT_VERIFY_FAILED;}
 *flags=0;return 0;
}
static int net_send(void*context,const unsigned char*data,size_t len){SOCKET s=*(SOCKET*)context;int n=send(s,(const char*)data,(int)len,0);return n>=0?n:MBEDTLS_ERR_NET_SEND_FAILED;}
static int net_recv(void*context,unsigned char*data,size_t len){SOCKET s=*(SOCKET*)context;int n=recv(s,(char*)data,(int)len,0);if(n>=0)return n;int e=WSAGetLastError();return e==WSAETIMEDOUT?MBEDTLS_ERR_SSL_TIMEOUT:MBEDTLS_ERR_NET_RECV_FAILED;}
static SOCKET connect_to(const char*host,unsigned port,DWORD timeout){
 char service[16];snprintf(service,sizeof(service),"%u",port);addrinfo hints={},*list=nullptr;hints.ai_socktype=SOCK_STREAM;hints.ai_family=AF_UNSPEC;
 if(getaddrinfo(host,service,&hints,&list)){app_log("Unable to resolve host");return INVALID_SOCKET;}SOCKET result=INVALID_SOCKET;
 for(addrinfo*a=list;a&&!g_stop_requested;a=a->ai_next){
  SOCKET s=socket(a->ai_family,a->ai_socktype,a->ai_protocol);if(s==INVALID_SOCKET)continue;u_long nonblocking=1;ioctlsocket(s,FIONBIO,&nonblocking);
  int rc=connect(s,a->ai_addr,(int)a->ai_addrlen);if(rc==SOCKET_ERROR&&WSAGetLastError()==WSAEWOULDBLOCK){fd_set write,error;FD_ZERO(&write);FD_ZERO(&error);FD_SET(s,&write);FD_SET(s,&error);timeval wait={3,0};rc=select(0,nullptr,&write,&error,&wait);if(rc>0){int code=0,n=sizeof(code);getsockopt(s,SOL_SOCKET,SO_ERROR,(char*)&code,&n);rc=code?SOCKET_ERROR:0;}else rc=SOCKET_ERROR;}
  if(rc==0){nonblocking=0;ioctlsocket(s,FIONBIO,&nonblocking);setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,(char*)&timeout,sizeof(timeout));setsockopt(s,SOL_SOCKET,SO_SNDTIMEO,(char*)&timeout,sizeof(timeout));result=s;break;}closesocket(s);
 }
 freeaddrinfo(list);if(result==INVALID_SOCKET)app_log("Cannot connect to Sunshine port %u",port);return result;
}
static bool dechunk(const unsigned char*p,size_t n,Buffer&out){
 out.size=0;size_t pos=0;while(pos<n){size_t end=pos;while(end+1<n&&!(p[end]=='\r'&&p[end+1]=='\n'))end++;if(end+1>=n||end-pos>40)return false;
  char line[48];memcpy(line,p+pos,end-pos);line[end-pos]=0;char*tail=nullptr;unsigned long bytes=strtoul(line,&tail,16);if(tail==line)return false;pos=end+2;
  if(!bytes)return true;if(bytes>4*1024*1024||bytes>n-pos||!out.append(p+pos,bytes))return false;pos+=bytes;if(pos+2>n||p[pos]!='\r'||p[pos+1]!='\n')return false;pos+=2;
 }return false;
}
static bool request(HostState*h,bool secure,const char*operation,const char*arguments,Buffer&body,DWORD timeout=10000){
 if(!h->identity.open())return false;if(secure&&!h->pinned.raw.len){app_log("HTTPS refused: no pinned host certificate");return false;}
 unsigned port=secure?h->https_port:h->http_port;SOCKET socket=connect_to(h->address,port,timeout);if(socket==INVALID_SOCKET)return false;
 EnterCriticalSection(&h->network_lock);h->active_socket=socket;LeaveCriticalSection(&h->network_lock);
 mbedtls_ssl_context ssl;mbedtls_ssl_config cfg;mbedtls_ssl_init(&ssl);mbedtls_ssl_config_init(&cfg);bool ok=false;Buffer header,raw;unsigned char uuid_bytes[16];Buffer uuid;
 if(!random_bytes(uuid_bytes,sizeof(uuid_bytes))||!hex_encode(uuid_bytes,sizeof(uuid_bytes),uuid))goto done;
 {
  char start[700];snprintf(start,sizeof(start),"GET /%s?uniqueid=%s&uuid=%s",operation,h->identity.uid,uuid.c_str());header.text(start);
  if(arguments&&*arguments){header.add("&");header.add(arguments);}header.add(" HTTP/1.1\r\nHost: ");header.add(h->address);
  char end[140];snprintf(end,sizeof(end),":%u\r\nUser-Agent: MoonlightRT81/0.1\r\nAccept: */*\r\nConnection: close\r\n\r\n",port);header.add(end);
 }
 if(secure){
  int rc=mbedtls_ssl_config_defaults(&cfg,MBEDTLS_SSL_IS_CLIENT,MBEDTLS_SSL_TRANSPORT_STREAM,MBEDTLS_SSL_PRESET_DEFAULT);
  if(!crypto_ok(rc,"TLS defaults"))goto done;
  mbedtls_ssl_conf_min_tls_version(&cfg,MBEDTLS_SSL_VERSION_TLS1_2);mbedtls_ssl_conf_authmode(&cfg,MBEDTLS_SSL_VERIFY_REQUIRED);
  mbedtls_ssl_conf_rng(&cfg,rng,nullptr);mbedtls_ssl_conf_verify(&cfg,verify_tls,h);mbedtls_ssl_conf_ca_chain(&cfg,&h->pinned,nullptr);
  if(!crypto_ok(mbedtls_ssl_conf_own_cert(&cfg,&h->identity.cert,&h->identity.key),"TLS client identity")||!crypto_ok(mbedtls_ssl_setup(&ssl,&cfg),"TLS setup"))goto done;
  mbedtls_ssl_set_hostname(&ssl,h->address);mbedtls_ssl_set_bio(&ssl,&socket,net_send,net_recv,nullptr);
  do{rc=mbedtls_ssl_handshake(&ssl);}while((rc==MBEDTLS_ERR_SSL_WANT_READ||rc==MBEDTLS_ERR_SSL_WANT_WRITE)&&!g_stop_requested);
  if(!crypto_ok(rc,"Pinned mutual TLS handshake"))goto done;
 }
 {
  size_t sent=0;while(sent<header.size&&!g_stop_requested){int rc=secure?mbedtls_ssl_write(&ssl,header.data+sent,header.size-sent):send(socket,(char*)header.data+sent,(int)(header.size-sent),0);if(rc<=0){app_log("HTTP send failed");goto done;}sent+=(size_t)rc;}
  unsigned char chunk[8192];for(;;){int n=secure?mbedtls_ssl_read(&ssl,chunk,sizeof(chunk)):recv(socket,(char*)chunk,sizeof(chunk),0);
   if(n==MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY||n==0)break;if(n<0){app_log("%s receive failed: %d",operation,n);goto done;}
   if(raw.size+(size_t)n>4*1024*1024||!raw.append(chunk,(size_t)n)){app_log("HTTP response too large");goto done;}
  }
  const char*split=strstr(raw.c_str(),"\r\n\r\n");if(!split||split-raw.c_str()>65536){app_log("Invalid HTTP response header");goto done;}
  if(strncmp(raw.c_str(),"HTTP/1.",7)||atoi(raw.c_str()+9)!=200){app_log("HTTP %s returned status %d",operation,raw.size>=12?atoi(raw.c_str()+9):0);goto done;}
  Buffer headers;headers.assign(raw.data,(size_t)(split-raw.c_str()));for(size_t i=0;i<headers.size;i++)if(headers.data[i]>='A'&&headers.data[i]<='Z')headers.data[i]+=32;
  const unsigned char*payload=(const unsigned char*)split+4;size_t length=raw.size-(size_t)(payload-raw.data);
  if(strstr(headers.c_str(),"transfer-encoding: chunked"))ok=dechunk(payload,length,body);
  else{const char*cl=strstr(headers.c_str(),"content-length:");if(cl&&strtoul(cl+15,nullptr,10)!=length){app_log("HTTP content-length mismatch");goto done;}ok=body.assign(payload,length);}
 }
 done:mbedtls_ssl_free(&ssl);mbedtls_ssl_config_free(&cfg);EnterCriticalSection(&h->network_lock);h->active_socket=INVALID_SOCKET;closesocket(socket);LeaveCriticalSection(&h->network_lock);return ok;
}
static bool xml(Buffer&text,pugi::xml_document&doc){
 pugi::xml_parse_result r=doc.load_buffer(text.data,text.size);if(!r){app_log("Invalid Sunshine XML response");return false;}
 auto root=doc.child("root");int code=root.attribute("status_code").as_int(-1);if(code!=200){app_log("Sunshine status=%d %s",code,root.attribute("status_message").as_string());return false;}return true;
}
HostState*host_create(){return new HostState;}void host_destroy(HostState*h){delete h;}
void host_cancel(HostState*h){if(!h)return;EnterCriticalSection(&h->network_lock);if(h->active_socket!=INVALID_SOCKET)shutdown(h->active_socket,SD_BOTH);LeaveCriticalSection(&h->network_lock);}
bool host_set_address(HostState*h,const char*address,unsigned port){
 size_t n=strlen(address);if(!n||n>=sizeof(h->address)||!port||port>65535)return false;
 for(size_t i=0;i<n;i++)if((unsigned char)address[i]<=32||strchr("/?#&\\",address[i]))return false;
 if(!strcmp(address,h->address)&&port==h->http_port)return true;
 strcpy(h->address,address);h->http_port=port;h->https_port=port-5;h->paired=false;h->count=0;
 mbedtls_x509_crt_free(&h->pinned);mbedtls_x509_crt_init(&h->pinned);h->pinned_pem.clear();
 char id[300];snprintf(id,sizeof(id),"%s:%u",address,port);unsigned char digest[32];mbedtls_sha256((unsigned char*)id,strlen(id),digest,0);Buffer hash;hex_encode(digest,12,hash);
 char name[80];snprintf(name,sizeof(name),"host-%s.pem",hash.c_str());WCHAR wide[80];utf8_to_wide(name,wide,80);profile_path(wide,h->pin_path);
 Buffer cert;if(read_file(h->pin_path,cert)&&!h->pin(cert.data,cert.size))return false;
 return true;
}
bool host_query(HostState*h,bool secure){
 Buffer body;if(!request(h,secure,"serverinfo",nullptr,body))return false;pugi::xml_document doc;if(!xml(body,doc))return false;auto root=doc.child("root");
 snprintf(h->name,sizeof(h->name),"%s",root.child_value("hostname"));snprintf(h->version,sizeof(h->version),"%s",root.child_value("appversion"));snprintf(h->gfe,sizeof(h->gfe),"%s",root.child_value("GfeVersion"));
 unsigned port=root.child("HttpsPort").text().as_uint();if(port&&port<=65535)h->https_port=port;
 h->paired=secure&&root.child("PairStatus").text().as_int()==1;
 const char*state=root.child_value("state");h->current_game=strstr(state,"SERVER_BUSY")?root.child("currentgame").text().as_int():0;
 h->codec_modes=root.child("ServerCodecModeSupport").text().as_int(SCM_H264);
 app_log("Sunshine server info received; paired=%d active_app=%d",h->paired,h->current_game);return true;
}
static bool pair_step(HostState*h,const char*args,pugi::xml_document&doc,DWORD timeout=10000,bool secure=false){Buffer body;if(!request(h,secure,"pair",args,body,timeout)||!xml(body,doc))return false;if(doc.child("root").child("paired").text().as_int()!=1){app_log("Pairing stage not accepted");return false;}return true;}
static bool aes_ecb(bool encrypt,const unsigned char*key,const Buffer&input,Buffer&output){
 if(!input.size||input.size%16||!output.reserve(input.size))return false;mbedtls_aes_context aes;mbedtls_aes_init(&aes);
 int rc=encrypt?mbedtls_aes_setkey_enc(&aes,key,128):mbedtls_aes_setkey_dec(&aes,key,128);
 for(size_t i=0;i<input.size&&!rc;i+=16)rc=mbedtls_aes_crypt_ecb(&aes,encrypt?MBEDTLS_AES_ENCRYPT:MBEDTLS_AES_DECRYPT,input.data+i,output.data+i);
 mbedtls_aes_free(&aes);if(!crypto_ok(rc,"Pair AES-ECB"))return false;output.size=input.size;output.data[output.size]=0;return true;
}
static bool cert_signature(mbedtls_x509_crt*c,Buffer&out){return out.assign(c->MBEDTLS_PRIVATE(sig).p,c->MBEDTLS_PRIVATE(sig).len);}
static bool sign_secret(Identity&id,const unsigned char*secret,Buffer&signature){
 unsigned char hash[32];mbedtls_sha256(secret,16,hash,0);if(!signature.reserve(1024))return false;size_t n=0;
 if(!crypto_ok(mbedtls_pk_sign(&id.key,MBEDTLS_MD_SHA256,hash,32,signature.data,signature.capacity,&n,rng,nullptr),"Sign pairing secret"))return false;
 signature.size=n;signature.data[n]=0;return true;
}
bool host_pair(HostState*h,const char*pin){
 if(strlen(pin)!=4||strspn(pin,"0123456789")!=4||!h->identity.open())return false;
 if(!host_query(h,false)||atoi(h->version)<7){app_log("Sunshine/GameStream generation 7+ required");return false;}
 unsigned char salt[16],challenge[16],secret[16],aeskey[32];if(!random_bytes(salt,16)||!random_bytes(challenge,16)||!random_bytes(secret,16))return false;
 Buffer salted(true);salted.assign(salt,16);salted.add(pin);mbedtls_sha256(salted.data,salted.size,aeskey,0);
 Buffer salt_hex,cert_hex,args;hex_encode(salt,16,salt_hex);hex_encode(h->identity.pem.data,h->identity.pem.size,cert_hex);
 args.text("devicename=roth&updateState=1&phrase=getservercert&salt=");args.add(salt_hex.c_str());args.add("&clientcert=");args.add(cert_hex.c_str());
 bool success=false;
 WCHAR pin_message[200];wsprintfW(pin_message,L"配对 PIN：%c%c%c%c。请在 Sunshine 主机的 PIN 页面输入；可点“停止”取消。",pin[0],pin[1],pin[2],pin[3]);app_status(pin_message);
 do{
  pugi::xml_document first;if(!pair_step(h,args.c_str(),first,120000))break;Buffer cert;
  if(!hex_decode(first.child("root").child_value("plaincert"),cert)||!cert.size||!h->pin(cert.data,cert.size))break;
  Buffer plain(true),encrypted(true),encoded;plain.assign(challenge,16);if(!aes_ecb(true,aeskey,plain,encrypted)||!hex_encode(encrypted.data,encrypted.size,encoded))break;
  args.text("devicename=roth&updateState=1&clientchallenge=");args.add(encoded.c_str());pugi::xml_document second;if(!pair_step(h,args.c_str(),second))break;
  Buffer response_bytes(true),response(true);if(!hex_decode(second.child("root").child_value("challengeresponse"),response_bytes)||!aes_ecb(false,aeskey,response_bytes,response)||response.size<48)break;
  Buffer material(true),signature,client_secret_hash(true);cert_signature(&h->identity.cert,signature);material.assign(response.data+32,16);material.append(signature.data,signature.size);material.append(secret,16);
  unsigned char digest[32];mbedtls_sha256(material.data,material.size,digest,0);client_secret_hash.assign(digest,32);
  if(!aes_ecb(true,aeskey,client_secret_hash,encrypted)||!hex_encode(encrypted.data,encrypted.size,encoded))break;
  args.text("devicename=roth&updateState=1&serverchallengeresp=");args.add(encoded.c_str());pugi::xml_document third;if(!pair_step(h,args.c_str(),third))break;
  Buffer pairing_secret(true);if(!hex_decode(third.child("root").child_value("pairingsecret"),pairing_secret)||pairing_secret.size<=16)break;
  mbedtls_sha256(pairing_secret.data,16,digest,0);
  if(!crypto_ok(mbedtls_pk_verify(&h->pinned.pk,MBEDTLS_MD_SHA256,digest,32,pairing_secret.data+16,pairing_secret.size-16),"Verify Sunshine pairing signature"))break;
  material.assign(challenge,16);cert_signature(&h->pinned,signature);material.append(signature.data,signature.size);material.append(pairing_secret.data,16);mbedtls_sha256(material.data,material.size,digest,0);
  if(!constant_equal(digest,response.data,32)){app_log("Pairing PIN challenge mismatch");break;}
  if(!sign_secret(h->identity,secret,signature))break;material.assign(secret,16);material.append(signature.data,signature.size);hex_encode(material.data,material.size,encoded);
  args.text("devicename=roth&updateState=1&clientpairingsecret=");args.add(encoded.c_str());pugi::xml_document fourth;if(!pair_step(h,args.c_str(),fourth))break;
  pugi::xml_document fifth;if(!pair_step(h,"devicename=roth&updateState=1&phrase=pairchallenge",fifth,10000,true))break;
  if(!write_file(h->pin_path,h->pinned_pem.data,h->pinned_pem.size)){app_log("Saving pinned host certificate failed");break;}
  success=host_query(h,true)&&h->paired;
 }while(false);
 SecureZeroMemory(aeskey,sizeof(aeskey));SecureZeroMemory(secret,sizeof(secret));
 if(!success){Buffer ignored;request(h,false,"unpair",nullptr,ignored,3000);h->paired=false;
  mbedtls_x509_crt_free(&h->pinned);mbedtls_x509_crt_init(&h->pinned);h->pinned_pem.clear();Buffer old;if(read_file(h->pin_path,old))h->pin(old.data,old.size);
  app_log("Pairing failed; unverified certificate discarded");}
 else app_log("Pairing completed; host certificate pinned");return success;
}
bool host_app_list(HostState*h){
 if(!host_query(h,true)||!h->paired)return false;Buffer body;if(!request(h,true,"applist",nullptr,body))return false;pugi::xml_document doc;if(!xml(body,doc))return false;h->count=0;
 for(auto node=doc.child("root").child("App");node&&h->count<128;node=node.next_sibling("App")){int id=node.child("ID").text().as_int(-1);if(id<0)continue;h->apps[h->count].id=id;snprintf(h->apps[h->count].name,sizeof(h->apps[0].name),"%s",node.child_value("AppTitle"));h->count++;}
 app_log("Application list received: %d items",h->count);return h->count>0;
}
int host_app_count(HostState*h){return h->count;}const char*host_app_name(HostState*h,int i){return i>=0&&i<h->count?h->apps[i].name:"";}int host_app_id(HostState*h,int i){return i>=0&&i<h->count?h->apps[i].id:-1;}
bool host_is_paired(HostState*h){return h->paired;}const char*host_name(HostState*h){return h->name;}
bool host_has_pin(HostState*h){return h->pinned_pem.size>0;}
bool host_app_art(HostState*h,int app_id,Buffer&out){char args[100];snprintf(args,sizeof(args),"appid=%d&AssetType=2&AssetIdx=0",app_id);return request(h,true,"appasset",args,out,1200);}
bool host_launch(HostState*h,int app_id,STREAM_CONFIGURATION*config,SERVER_INFORMATION*server){
 if(!host_query(h,true)||!h->paired)return false;
 if(h->current_game&&h->current_game!=app_id){app_log("A different application is already active on the host; not cancelling it.");return false;}
 if(!random_bytes(config->remoteInputAesKey,16)||!random_bytes(config->remoteInputAesIv,4))return false;
 Buffer key(true);hex_encode(config->remoteInputAesKey,16,key);unsigned char*v=(unsigned char*)config->remoteInputAesIv;int32_t keyid=(int32_t)(((uint32_t)v[0]<<24)|((uint32_t)v[1]<<16)|((uint32_t)v[2]<<8)|v[3]);
 char arguments[1500];snprintf(arguments,sizeof(arguments),"appid=%d&mode=%dx%dx%d&additionalStates=1&sops=0&rikey=%s&rikeyid=%d&localAudioPlayMode=0&surroundAudioInfo=%u&remoteControllersBitmap=0&gcmap=0&gcpersist=0%s",app_id,config->width,config->height,config->fps,key.c_str(),keyid,SURROUNDAUDIOINFO_FROM_AUDIO_CONFIGURATION(config->audioConfiguration),LiGetLaunchUrlQueryParameters());
 Buffer body;bool ok=request(h,true,h->current_game?"resume":"launch",arguments,body,120000);SecureZeroMemory(arguments,sizeof(arguments));if(!ok)return false;pugi::xml_document doc;if(!xml(body,doc))return false;
 const char*url=doc.child("root").child_value("sessionUrl0");if(!*url||strlen(url)>=sizeof(h->session_url)){app_log("Host did not provide a valid RTSP URL");return false;}strcpy(h->session_url,url);
 LiInitializeServerInformation(server);server->address=h->address;server->serverInfoAppVersion=h->version;server->serverInfoGfeVersion=*h->gfe?h->gfe:nullptr;server->rtspSessionUrl=h->session_url;server->serverCodecModeSupport=h->codec_modes;
 app_log("Host accepted launch/resume request; H264 %dx%d %dfps requested",config->width,config->height,config->fps);return true;
}
bool host_crypto_selftest(){
 Identity first;if(!first.open())return false;unsigned char secret[16]={};if(!random_bytes(secret,16))return false;Buffer signature;
 if(!sign_secret(first,secret,signature))return false;unsigned char hash[32];mbedtls_sha256(secret,16,hash,0);
 if(!crypto_ok(mbedtls_pk_verify(&first.cert.pk,MBEDTLS_MD_SHA256,hash,32,signature.data,signature.size),"Identity signature roundtrip"))return false;
 Identity second;if(!second.open())return false;bool same=first.cert.raw.len==second.cert.raw.len&&constant_equal(first.cert.raw.p,second.cert.raw.p,first.cert.raw.len);
 app_log("Identity/DPAPI/signature selftest=%s",same?"PASS":"FAIL");return same;
}
