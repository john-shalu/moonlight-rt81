/* Instrument only this probe's private COM interface instances. No DLL injection,
 * no driver patch, no system-wide hooks. A zero counter is inconclusive. */
#define CINTERFACE
#define COBJMACROS
#include "video_hooks.h"
#include <d3d11_4.h>
static ID3D11VideoDevice *device;
static ID3D11VideoContext *context;
static const ID3D11VideoDeviceVtbl *original_device;
static const ID3D11VideoContextVtbl *original_context;
static ID3D11VideoDeviceVtbl *copy_device;
static ID3D11VideoContextVtbl *copy_context;
static volatile LONG created, begun, submitted, ended;
static HRESULT STDMETHODCALLTYPE submit_buffers1(ID3D11VideoContext1 *c,ID3D11VideoDecoder *d,
 UINT count,const D3D11_VIDEO_DECODER_BUFFER_DESC1 *buffers) {
 HRESULT hr=((const ID3D11VideoContext1Vtbl*)original_context)->SubmitDecoderBuffers1(c,d,count,buffers);
 if(SUCCEEDED(hr))InterlockedIncrement(&submitted);return hr;
}
static BOOL same_interface(IUnknown*object,REFIID iid) {
 IUnknown *extended=0;BOOL same=FALSE;
 if(SUCCEEDED(IUnknown_QueryInterface(object,iid,(void**)&extended))){same=(void*)object==(void*)extended;IUnknown_Release(extended);}
 return same;
}
static HRESULT STDMETHODCALLTYPE create_decoder(ID3D11VideoDevice *d,
 const D3D11_VIDEO_DECODER_DESC *desc,const D3D11_VIDEO_DECODER_CONFIG *config,
 ID3D11VideoDecoder **out) {
 HRESULT hr=original_device->CreateVideoDecoder(d,desc,config,out);
 if(SUCCEEDED(hr) && out && *out) InterlockedIncrement(&created);
 return hr;
}
static HRESULT STDMETHODCALLTYPE begin_frame(ID3D11VideoContext *c,ID3D11VideoDecoder *d,
 ID3D11VideoDecoderOutputView *view,UINT bytes,const void *key) {
 HRESULT hr=original_context->DecoderBeginFrame(c,d,view,bytes,key);
 if(SUCCEEDED(hr))InterlockedIncrement(&begun);return hr;
}
static HRESULT STDMETHODCALLTYPE submit_buffers(ID3D11VideoContext *c,ID3D11VideoDecoder *d,
 UINT count,const D3D11_VIDEO_DECODER_BUFFER_DESC *buffers) {
 HRESULT hr=original_context->SubmitDecoderBuffers(c,d,count,buffers);
 if(SUCCEEDED(hr))InterlockedIncrement(&submitted);return hr;
}
static HRESULT STDMETHODCALLTYPE end_frame(ID3D11VideoContext *c,ID3D11VideoDecoder *d) {
 HRESULT hr=original_context->DecoderEndFrame(c,d);
 if(SUCCEEDED(hr))InterlockedIncrement(&ended);return hr;
}
static BOOL writable_object(void *p) {
 MEMORY_BASIC_INFORMATION info;
 if(!VirtualQuery(p,&info,sizeof(info)))return FALSE;
 return info.State==MEM_COMMIT && !(info.Protect&(PAGE_GUARD|PAGE_NOACCESS)) &&
 (info.Protect&(PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY));
}
BOOL install_video_hooks(ID3D11VideoDevice *d,ID3D11VideoContext *c) {
 if(device || !d || !c || !writable_object(d) || !writable_object(c))return FALSE;
 SIZE_T device_bytes=sizeof(*copy_device),context_bytes=sizeof(*copy_context);
 BOOL context1=same_interface((IUnknown*)c,&IID_ID3D11VideoContext1);
 if(same_interface((IUnknown*)d,&IID_ID3D11VideoDevice1))device_bytes=sizeof(ID3D11VideoDevice1Vtbl);
 if(context1)context_bytes=sizeof(ID3D11VideoContext1Vtbl);
 if(same_interface((IUnknown*)c,&IID_ID3D11VideoContext2))context_bytes=sizeof(ID3D11VideoContext2Vtbl);
 copy_device=VirtualAlloc(0,device_bytes,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
 copy_context=VirtualAlloc(0,context_bytes,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
 if(!copy_device || !copy_context){remove_video_hooks();return FALSE;}
 original_device=d->lpVtbl;original_context=c->lpVtbl;
 for(SIZE_T i=0;i<device_bytes;i++)((BYTE*)copy_device)[i]=((const BYTE*)original_device)[i];
 for(SIZE_T i=0;i<context_bytes;i++)((BYTE*)copy_context)[i]=((const BYTE*)original_context)[i];
 copy_device->CreateVideoDecoder=create_decoder;
 copy_context->DecoderBeginFrame=begin_frame;
 copy_context->SubmitDecoderBuffers=submit_buffers;
 copy_context->DecoderEndFrame=end_frame;
 if(context1)((ID3D11VideoContext1Vtbl*)copy_context)->SubmitDecoderBuffers1=submit_buffers1;
 ID3D11VideoDevice_AddRef(d);ID3D11VideoContext_AddRef(c);device=d;context=c;
 InterlockedExchangePointer((PVOID volatile*)&d->lpVtbl,copy_device);
 InterlockedExchangePointer((PVOID volatile*)&c->lpVtbl,copy_context);
 reset_video_calls();return TRUE;
}
void remove_video_hooks(void) {
 if(device)InterlockedExchangePointer((PVOID volatile*)&device->lpVtbl,(void*)original_device);
 if(context)InterlockedExchangePointer((PVOID volatile*)&context->lpVtbl,(void*)original_context);
 if(device)ID3D11VideoDevice_Release(device);
 if(context)ID3D11VideoContext_Release(context);
 device=0;context=0;
 if(copy_device)VirtualFree(copy_device,0,MEM_RELEASE);
 if(copy_context)VirtualFree(copy_context,0,MEM_RELEASE);
 copy_device=0;copy_context=0;
}
void reset_video_calls(void) {InterlockedExchange(&created,0);InterlockedExchange(&begun,0);InterlockedExchange(&submitted,0);InterlockedExchange(&ended,0);}
VideoCalls read_video_calls(void) {VideoCalls c={created,begun,submitted,ended};return c;}
