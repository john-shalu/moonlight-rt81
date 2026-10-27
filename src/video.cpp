#include "app.h"
#include <initguid.h>
#include <d3d11_4.h>
#include <d3d10.h>
#include <dxgi1_2.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <mftransform.h>
#include <codecapi.h>
#include <wmcodecdsp.h>
#include "video_hooks.h"
#define logf app_log
#define check app_check
static volatile LONG video_cancel=0;
static void logguid(const char*n,const GUID&g){app_log("%s={%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",n,g.Data1,g.Data2,g.Data3,g.Data4[0],g.Data4[1],g.Data4[2],g.Data4[3],g.Data4[4],g.Data4[5],g.Data4[6],g.Data4[7]);}
static void logwide(const char*n,const WCHAR*v){char s[700];wide_to_utf8(v,s,sizeof(s));app_log("%s=%s",n,s);}
struct Graphics {
 ID3D11Device *device=0;ID3D11DeviceContext *context=0;
 ID3D11VideoDevice *video=0;ID3D11VideoContext *vcontext=0;
 IMFDXGIDeviceManager *manager=0;IDXGISwapChain *swap=0;
 ID3D11VideoProcessorEnumerator *enumerator=0;ID3D11VideoProcessor *processor=0;
 ID3D11VideoProcessorOutputView *output=0;ID3D11Texture2D *back=0;
 ID3D11Query *completion=0;
 HRESULT last_present=S_OK;
 bool hooked=false;
 void close(){remove_video_hooks();release(completion);release(output);release(processor);release(enumerator);release(back);release(swap);release(manager);release(vcontext);release(video);release(context);release(device);}
 bool init(){
  D3D_FEATURE_LEVEL requested[]={D3D_FEATURE_LEVEL_11_1,D3D_FEATURE_LEVEL_11_0,D3D_FEATURE_LEVEL_10_1,D3D_FEATURE_LEVEL_10_0,D3D_FEATURE_LEVEL_9_3,D3D_FEATURE_LEVEL_9_2,D3D_FEATURE_LEVEL_9_1},actual;
  HRESULT hr=D3D11CreateDevice(0,D3D_DRIVER_TYPE_HARDWARE,0,D3D11_CREATE_DEVICE_BGRA_SUPPORT|D3D11_CREATE_DEVICE_VIDEO_SUPPORT,requested,7,D3D11_SDK_VERSION,&device,&actual,&context);
  if(FAILED(hr)){
   logf("D3D11 VIDEO_SUPPORT device failed 0x%08x; retry hardware without the flag",(UINT)hr);
   hr=D3D11CreateDevice(0,D3D_DRIVER_TYPE_HARDWARE,0,D3D11_CREATE_DEVICE_BGRA_SUPPORT,requested,7,D3D11_SDK_VERSION,&device,&actual,&context);
  }
  if(!check(hr,"D3D11CreateDevice(HARDWARE; no WARP fallback)"))return false;
  logf("D3D.feature_level=0x%x",(UINT)actual);
  IDXGIDevice*dxgi=0;IDXGIAdapter*adapter=0;
  if(SUCCEEDED(device->QueryInterface(__uuidof(IDXGIDevice),(void**)&dxgi)) && SUCCEEDED(dxgi->GetAdapter(&adapter))){
   IDXGIAdapter2* modern=0; if(SUCCEEDED(adapter->QueryInterface(__uuidof(IDXGIAdapter2),(void**)&modern))){DXGI_ADAPTER_DESC2 d={}; if(SUCCEEDED(modern->GetDesc2(&d))){logwide("Hardware adapter",d.Description);logf("Adapter vendor=0x%04x device=0x%04x flags=%u",d.VendorId,d.DeviceId,d.Flags);}release(modern);} else {DXGI_ADAPTER_DESC d={};adapter->GetDesc(&d);logwide("Legacy adapter label",d.Description);}
  }IDXGIDevice1*latency_device=nullptr;if(SUCCEEDED(device->QueryInterface(__uuidof(IDXGIDevice1),(void**)&latency_device))){latency_device->SetMaximumFrameLatency(1);release(latency_device);}release(adapter);release(dxgi);
  ID3D10Multithread*multithread=0;
  if(SUCCEEDED(context->QueryInterface(__uuidof(ID3D10Multithread),(void**)&multithread))){multithread->SetMultithreadProtected(TRUE);release(multithread);}
  if(!check(device->QueryInterface(__uuidof(ID3D11VideoDevice),(void**)&video),"ID3D11VideoDevice"))return false;
  if(!check(context->QueryInterface(__uuidof(ID3D11VideoContext),(void**)&vcontext),"ID3D11VideoContext"))return false;
  UINT profiles=video->GetVideoDecoderProfileCount();logf("D3D11 decoder profile count=%u",profiles);
  for(UINT i=0;i<profiles;i++){GUID g={};if(SUCCEEDED(video->GetVideoDecoderProfile(i,&g)))logguid("D3D11.decoder_profile",g);}
  hooked=install_video_hooks(video,vcontext)!=FALSE;logf("Private COM instrumentation=%s",hooked?"installed":"unavailable (zero counts will be inconclusive)");
  D3D11_QUERY_DESC query={D3D11_QUERY_EVENT,0};hr=device->CreateQuery(&query,&completion);
  if(!check(hr,"Create GPU completion query"))return false;
  UINT token=0;if(!check(MFCreateDXGIDeviceManager(&token,&manager),"MFCreateDXGIDeviceManager"))return false;
  return check(manager->ResetDevice(device,token),"IMFDXGIDeviceManager.ResetDevice");
 }
 bool wait_for_decode(){
  context->End(completion);context->Flush();DWORD start=GetTickCount();BOOL done=FALSE;HRESULT hr;
  while(!video_cancel){hr=context->GetData(completion,&done,sizeof(done),0);if(hr==S_OK&&done)return true;
   if(FAILED(hr)){check(hr,"GPU completion GetData");return false;}
   if(GetTickCount()-start>2000){logf("FAIL GPU completion timed out");return false;}Sleep(1);
  }return false;
 }
 bool prepare_display(UINT width,UINT height){
  if(swap)return true;
  RECT client;GetClientRect(g_video,&client);UINT outw=(UINT)(client.right-client.left),outh=(UINT)(client.bottom-client.top);
  IDXGIDevice*dxgi=0;IDXGIAdapter*adapter=0;IDXGIFactory*factory=0;HRESULT hr=device->QueryInterface(__uuidof(IDXGIDevice),(void**)&dxgi);
  if(SUCCEEDED(hr))hr=dxgi->GetAdapter(&adapter);
  if(SUCCEEDED(hr))hr=adapter->GetParent(__uuidof(IDXGIFactory),(void**)&factory);
  if(SUCCEEDED(hr)){HRESULT association=factory->MakeWindowAssociation(g_main,DXGI_MWA_NO_ALT_ENTER|DXGI_MWA_NO_WINDOW_CHANGES);if(FAILED(association))app_log("DXGI window association hint unavailable: 0x%08x",(unsigned)association);}
  DXGI_SWAP_CHAIN_DESC desc={};desc.BufferDesc.Width=outw;desc.BufferDesc.Height=outh;desc.BufferDesc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;
  desc.SampleDesc.Count=1;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.BufferCount=2;desc.OutputWindow=g_video;desc.Windowed=TRUE;desc.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
  if(SUCCEEDED(hr))hr=factory->CreateSwapChain(device,&desc,&swap);
  release(factory);release(adapter);release(dxgi);if(!check(hr,"CreateSwapChain"))return false;
  if(!check(swap->GetBuffer(0,__uuidof(ID3D11Texture2D),(void**)&back),"GetBackBuffer"))return false;
  D3D11_VIDEO_PROCESSOR_CONTENT_DESC content={};content.InputFrameFormat=D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
  content.InputFrameRate.Numerator=30;content.InputFrameRate.Denominator=1;content.InputWidth=width;content.InputHeight=height;
  content.OutputFrameRate=content.InputFrameRate;content.OutputWidth=outw;content.OutputHeight=outh;content.Usage=D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
  if(!check(video->CreateVideoProcessorEnumerator(&content,&enumerator),"CreateVideoProcessorEnumerator"))return false;
  if(!check(video->CreateVideoProcessor(enumerator,0,&processor),"CreateVideoProcessor"))return false;
  D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC view={};view.ViewDimension=D3D11_VPOV_DIMENSION_TEXTURE2D;
  if(!check(video->CreateVideoProcessorOutputView(back,enumerator,&view,&output),"CreateVideoProcessorOutputView"))return false;
  RECT source={0,0,(LONG)width,(LONG)height},destination={0,0,(LONG)outw,(LONG)outh};
  vcontext->VideoProcessorSetStreamFrameFormat(processor,0,D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);
  vcontext->VideoProcessorSetStreamAutoProcessingMode(processor,0,FALSE);
  vcontext->VideoProcessorSetStreamSourceRect(processor,0,TRUE,&source);
  vcontext->VideoProcessorSetStreamDestRect(processor,0,TRUE,&destination);
  vcontext->VideoProcessorSetOutputTargetRect(processor,TRUE,&destination);
  D3D11_VIDEO_PROCESSOR_COLOR_SPACE input={};input.YCbCr_Matrix=1;input.Nominal_Range=1;
  D3D11_VIDEO_PROCESSOR_COLOR_SPACE rgb={};rgb.RGB_Range=0;
  vcontext->VideoProcessorSetStreamColorSpace(processor,0,&input);vcontext->VideoProcessorSetOutputColorSpace(processor,&rgb);
  return true;
 }
 bool present(ID3D11Texture2D*texture,UINT subresource,UINT frame){
  if(!processor||!output)return false;
  D3D11_TEXTURE2D_DESC td={};texture->GetDesc(&td);
  D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC view={};view.ViewDimension=D3D11_VPIV_DIMENSION_TEXTURE2D;
  view.Texture2D.MipSlice=subresource%td.MipLevels;view.Texture2D.ArraySlice=subresource/td.MipLevels;
  ID3D11VideoProcessorInputView*input=0;HRESULT hr=video->CreateVideoProcessorInputView(texture,enumerator,&view,&input);
  if(SUCCEEDED(hr)){D3D11_VIDEO_PROCESSOR_STREAM stream={};stream.Enable=TRUE;stream.OutputIndex=0;stream.InputFrameOrField=frame;stream.pInputSurface=input;
   hr=vcontext->VideoProcessorBlt(processor,output,frame,1,&stream);}
  release(input);if(SUCCEEDED(hr))hr=swap->Present(0,0);last_present=hr;return hr==S_OK;
 }
};
struct VideoState {
 HWND window;Graphics graphics;IMFTransform*decoder=nullptr;HANDLE thread=nullptr,ready=nullptr,start=nullptr;
 int width=1280,height=720,fps=30,result=-1;volatile LONG stopping=0;bool selftest=false,test_ok=false,need_idr=true;
 WCHAR test_folder[MAX_PATH]={};Buffer au;UINT64 decoded=0,presented=0,errors=0,decode_us=0;DWORD last_stats=0;int view_w=0,view_h=0;
 explicit VideoState(HWND w):window(w){ready=CreateEventW(nullptr,TRUE,FALSE,nullptr);start=CreateEventW(nullptr,TRUE,FALSE,nullptr);}
 ~VideoState(){if(ready)CloseHandle(ready);if(start)CloseHandle(start);}
 bool output_type(){
  for(DWORD i=0;i<32;i++){IMFMediaType*t=nullptr;if(FAILED(decoder->GetOutputAvailableType(0,i,&t)))break;GUID sub={};t->GetGUID(MF_MT_SUBTYPE,&sub);
   if(IsEqualGUID(sub,MFVideoFormat_NV12)&&SUCCEEDED(decoder->SetOutputType(0,t,0))){UINT32 w=0,h=0;if(SUCCEEDED(MFGetAttributeSize(t,MF_MT_FRAME_SIZE,&w,&h))&&w&&h&&w<=4096&&h<=4096){width=(int)w;height=(int)h;}release(t);return true;}release(t);
  }app_log("Decoder has no accepted NV12 output format");return false;
 }
 bool create_decoder(){
  release(decoder);HRESULT hr=CoCreateInstance(CLSID_CMSH264DecoderMFT,nullptr,CLSCTX_INPROC_SERVER,__uuidof(IMFTransform),(void**)&decoder);
  if(!app_check(hr,"Create H264 MFT"))return false;IMFAttributes*a=nullptr;
  if(SUCCEEDED(decoder->GetAttributes(&a))){a->SetUINT32(MF_LOW_LATENCY,TRUE);release(a);}ICodecAPI*api=nullptr;
  if(SUCCEEDED(decoder->QueryInterface(IID_ICodecAPI,(void**)&api))){VARIANT v={};v.vt=VT_UI4;v.ulVal=1;hr=api->SetValue(&CODECAPI_AVLowLatencyMode,&v);app_log("Decoder low latency property=0x%08x",(unsigned)hr);release(api);}
  if(!app_check(decoder->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER,(ULONG_PTR)graphics.manager),"Set hardware decoder device"))return false;
  IMFMediaType*t=nullptr;hr=MFCreateMediaType(&t);if(SUCCEEDED(hr))hr=t->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video);
  if(SUCCEEDED(hr))hr=t->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_H264);if(SUCCEEDED(hr))hr=MFSetAttributeSize(t,MF_MT_FRAME_SIZE,width,height);
  if(SUCCEEDED(hr))hr=MFSetAttributeRatio(t,MF_MT_FRAME_RATE,fps,1);if(SUCCEEDED(hr))hr=MFSetAttributeRatio(t,MF_MT_PIXEL_ASPECT_RATIO,1,1);
  if(SUCCEEDED(hr))hr=t->SetUINT32(MF_MT_INTERLACE_MODE,MFVideoInterlace_Progressive);if(SUCCEEDED(hr))hr=decoder->SetInputType(0,t,0);release(t);
  if(!app_check(hr,"Configure H264 input")||!output_type())return false;
  if(!app_check(decoder->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING,0),"Begin video stream"))return false;
  return app_check(decoder->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM,0),"Start video stream");
 }
 void discard_display(){
  graphics.wait_for_decode();release(graphics.output);release(graphics.processor);release(graphics.enumerator);release(graphics.back);release(graphics.swap);
 }
 bool draw(ID3D11Texture2D*texture,UINT sub){
  RECT r;GetClientRect(window,&r);int w=r.right-r.left,h=r.bottom-r.top;if(w<=0||h<=0)return true;
  if(view_w!=w||view_h!=h){discard_display();view_w=w;view_h=h;}
  if(!graphics.prepare_display(width,height))return false;
  if(graphics.present(texture,sub,(UINT)decoded)){presented++;return true;}
  if(graphics.last_present==DXGI_STATUS_OCCLUDED)return true;
  app_log("Video present failed: 0x%08x",(unsigned)graphics.last_present);return false;
 }
 bool drain(){
  for(int attempt=0;attempt<64&&!stopping;attempt++){
   MFT_OUTPUT_STREAM_INFO info={};HRESULT hr=decoder->GetOutputStreamInfo(0,&info);if(!app_check(hr,"Video output info"))return false;
   MFT_OUTPUT_DATA_BUFFER out={};IMFSample*provided=nullptr;
   if(!(info.dwFlags&(MFT_OUTPUT_STREAM_PROVIDES_SAMPLES|MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES))){
    IMFMediaBuffer*b=nullptr;hr=MFCreateSample(&provided);if(SUCCEEDED(hr))hr=MFCreateAlignedMemoryBuffer(info.cbSize,info.cbAlignment?info.cbAlignment-1:0,&b);if(SUCCEEDED(hr))hr=provided->AddBuffer(b);release(b);if(FAILED(hr)){release(provided);return false;}out.pSample=provided;
   }
   DWORD flags=0;hr=decoder->ProcessOutput(0,1,&out,&flags);release(out.pEvents);
   if(hr==MF_E_TRANSFORM_NEED_MORE_INPUT){release(provided);return true;}
   if(hr==MF_E_TRANSFORM_STREAM_CHANGE){release(provided);discard_display();if(!output_type())return false;continue;}
   if(FAILED(hr)){if(out.pSample!=provided)release(out.pSample);release(provided);app_check(hr,"Decode frame");return false;}
   IMFSample*sample=out.pSample;IMFMediaBuffer*buffer=nullptr;IMFDXGIBuffer*dxgi=nullptr;ID3D11Texture2D*texture=nullptr;UINT sub=0;
   bool ok=sample&&SUCCEEDED(sample->GetBufferByIndex(0,&buffer))&&SUCCEEDED(buffer->QueryInterface(__uuidof(IMFDXGIBuffer),(void**)&dxgi))&&SUCCEEDED(dxgi->GetResource(__uuidof(ID3D11Texture2D),(void**)&texture))&&SUCCEEDED(dxgi->GetSubresourceIndex(&sub));
   if(ok){decoded++;ok=draw(texture,sub);if(decoded==1)app_status(L"串流画面已收到：H.264 原厂硬解。点画面可发送鼠标和键盘。");}
   else app_log("Hardware decoder did not return a DXGI texture; software fallback is disabled");
   release(texture);release(dxgi);release(buffer);if(sample!=provided)release(sample);release(provided);if(!ok)return false;
  }return stopping!=0;
 }
 int feed(const void*data,size_t length,bool keyframe,uint64_t timestamp){
  if(stopping)return DR_OK;if(!length||length>8*1024*1024)return DR_NEED_IDR;
  if(need_idr&&!keyframe)return DR_NEED_IDR;
  if(need_idr){decoder->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH,0);decoder->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM,0);need_idr=false;}
  IMFSample*sample=nullptr;IMFMediaBuffer*buffer=nullptr;BYTE*p=nullptr;HRESULT hr=MFCreateSample(&sample);
  if(SUCCEEDED(hr))hr=MFCreateMemoryBuffer((DWORD)length,&buffer);if(SUCCEEDED(hr))hr=buffer->Lock(&p,nullptr,nullptr);
  if(SUCCEEDED(hr)){memcpy(p,data,length);buffer->Unlock();hr=buffer->SetCurrentLength((DWORD)length);}if(SUCCEEDED(hr))hr=sample->AddBuffer(buffer);release(buffer);
  if(SUCCEEDED(hr))hr=sample->SetSampleTime((LONGLONG)timestamp*10);if(SUCCEEDED(hr))hr=sample->SetSampleDuration(10000000/fps);
  if(keyframe&&SUCCEEDED(hr))hr=sample->SetUINT32(MFSampleExtension_CleanPoint,TRUE);
  uint64_t begin=clock_us();
  if(SUCCEEDED(hr)){for(int retry=0;retry<4;retry++){hr=decoder->ProcessInput(0,sample,0);if(hr!=MF_E_NOTACCEPTING)break;if(!drain()){hr=E_FAIL;break;}}}
  release(sample);bool ok=SUCCEEDED(hr)&&drain();decode_us+=clock_us()-begin;
  if(!ok){errors++;need_idr=true;app_log("Video decoder requested IDR; hr=0x%08x",(unsigned)hr);return DR_NEED_IDR;}return DR_OK;
 }
 bool run_local_file(const WCHAR*filename){
  WCHAR path[MAX_PATH];if(lstrlenW(test_folder)+lstrlenW(filename)+2>=MAX_PATH)return false;lstrcpyW(path,test_folder);lstrcatW(path,L"\\");lstrcatW(path,filename);
  Buffer data;if(!read_file(path,data)){app_log("Cannot open local H264 test clip");return false;}DWORD offset[260],count=0;
  for(DWORD p=0;p+4<data.size;p++){DWORD prefix=0;if(!data.data[p]&&!data.data[p+1]){if(data.data[p+2]==1)prefix=3;else if(!data.data[p+2]&&data.data[p+3]==1)prefix=4;}if(prefix){if((data.data[p+prefix]&31)==9){if(count>=250)return false;offset[count++]=p;}p+=prefix;}}
  if(!count||offset[0])return false;offset[count]=(DWORD)data.size;need_idr=true;uint64_t begin=clock_us();UINT64 before=decoded;
  for(DWORD i=0;i<count&&!stopping;i++){while(!stopping&&clock_us()-begin<(uint64_t)i*1000000/fps)Sleep(1);if(feed(data.data+offset[i],offset[i+1]-offset[i],i%30==0,(uint64_t)i*1000000/fps)!=DR_OK)return false;}
  decoder->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN,0);bool ok=drain();app_log("Local clip frames=%u decoded=%u",count,(unsigned)(decoded-before));return ok&&decoded-before==count;
 }
 void stats(){VideoCalls c=read_video_calls();app_log("Video totals: decoded=%u presented=%u errors=%u mean_submit_decode_us=%u DXVA_begin=%d submit=%d end=%d",(unsigned)decoded,(unsigned)presented,(unsigned)errors,decoded?(unsigned)(decode_us/decoded):0,c.begun,c.submitted,c.ended);}
};
static VideoState*active_video=nullptr;
static DWORD WINAPI video_thread(void*context){
 VideoState*s=(VideoState*)context;HRESULT co=CoInitializeEx(nullptr,COINIT_MULTITHREADED);HRESULT mf=FAILED(co)?co:MFStartup(MF_VERSION,MFSTARTUP_FULL);
 s->result=SUCCEEDED(mf)&&s->graphics.init()&&s->create_decoder()?0:-1;SetEvent(s->ready);
 if(s->result==0){WaitForSingleObject(s->start,INFINITE);
  if(s->selftest){const WCHAR*names[]={L"baseline-cavlc.h264",L"main-cabac.h264",L"high-cabac.h264"};s->test_ok=true;
   for(int i=0;i<3&&!s->stopping;i++){if(i&&!s->create_decoder()){s->test_ok=false;break;}if(!s->run_local_file(names[i])){s->test_ok=false;break;}}s->test_ok=s->test_ok&&s->decoded==360;
  }else{
   while(!s->stopping){VIDEO_FRAME_HANDLE handle;PDECODE_UNIT du;if(!LiWaitForNextVideoFrame(&handle,&du)){if(s->stopping)break;continue;}
    int result=DR_NEED_IDR;s->au.size=0;
    if(du->fullLength>0&&du->fullLength<=8*1024*1024&&s->au.reserve((size_t)du->fullLength)){
     bool valid=true;for(PLENTRY p=du->bufferList;p;p=p->next)if(p->length<=0||(size_t)p->length>(size_t)du->fullLength-s->au.size||!s->au.append(p->data,(size_t)p->length)){valid=false;break;}
     if(valid&&s->au.size==(size_t)du->fullLength)result=s->feed(s->au.data,s->au.size,du->frameType==FRAME_TYPE_IDR,du->presentationTimeUs);
    }LiCompleteVideoFrame(handle,result);
    DWORD now=GetTickCount();if(now-s->last_stats>=5000){s->stats();s->last_stats=now;}
   }
  }
 }
 s->stats();release(s->decoder);s->graphics.close();if(SUCCEEDED(mf))MFShutdown();if(SUCCEEDED(co))CoUninitialize();return 0;
}
static int video_setup(int format,int width,int height,int fps,void*context,int){
 VideoState*s=(VideoState*)context;if(!s||format!=VIDEO_FORMAT_H264||width<=0||height<=0||width>1920||height>1088||fps<=0||fps>60)return -1;
 active_video=s;s->width=width;s->height=height;s->fps=fps;s->stopping=0;video_cancel=0;s->decoded=s->presented=s->errors=s->decode_us=0;s->need_idr=true;s->result=-1;s->view_w=s->view_h=0;
 ResetEvent(s->ready);ResetEvent(s->start);s->thread=CreateThread(nullptr,0,video_thread,s,0,nullptr);if(!s->thread)return -1;
 if(WaitForSingleObject(s->ready,15000)!=WAIT_OBJECT_0){app_log("Hardware video initialization timed out");return -1;}return s->result;
}
static void video_start(){if(active_video)SetEvent(active_video->start);}
static void video_stop(){if(!active_video)return;InterlockedExchange(&active_video->stopping,1);InterlockedExchange(&video_cancel,1);SetEvent(active_video->start);if(!active_video->selftest)LiWakeWaitForVideoFrame();}
static void video_cleanup(){if(!active_video)return;VideoState*s=active_video;video_stop();if(s->thread){WaitForSingleObject(s->thread,INFINITE);CloseHandle(s->thread);s->thread=nullptr;}s->selftest=false;}
VideoState*video_create(HWND window){return new VideoState(window);}
void video_destroy(VideoState*s){if(!s)return;if(active_video==s){video_cleanup();active_video=nullptr;}delete s;}
void video_callbacks(VideoState*,DECODER_RENDERER_CALLBACKS*cb){LiInitializeVideoCallbacks(cb);cb->setup=video_setup;cb->start=video_start;cb->stop=video_stop;cb->cleanup=video_cleanup;cb->capabilities=CAPABILITY_PULL_RENDERER;}
void video_log_stats(VideoState*s){if(s)s->stats();}
bool video_has_frames(VideoState*s){VideoCalls c=read_video_calls();return s&&s->decoded>0&&c.begun>0&&c.submitted>0;}
bool video_local_test(VideoState*s,const WCHAR*folder){if(s->thread)return false;s->selftest=true;lstrcpynW(s->test_folder,folder,MAX_PATH);bool ok=video_setup(VIDEO_FORMAT_H264,1280,720,30,s,0)==0;if(ok){video_start();WaitForSingleObject(s->thread,INFINITE);ok=s->test_ok;}video_cleanup();return ok;}
