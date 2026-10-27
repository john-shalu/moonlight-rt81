#include "app.h"
#include <wincodec.h>

HBITMAP decode_artwork(const Buffer&data){
 if(!data.size||data.size>4*1024*1024)return nullptr;
 IWICImagingFactory*factory=nullptr;IWICStream*stream=nullptr;IWICBitmapDecoder*decoder=nullptr;IWICBitmapFrameDecode*frame=nullptr;IWICBitmapScaler*scaler=nullptr;IWICFormatConverter*converter=nullptr;HBITMAP bitmap=nullptr;
 HRESULT hr=CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,__uuidof(IWICImagingFactory),(void**)&factory);
 if(SUCCEEDED(hr))hr=factory->CreateStream(&stream);
 if(SUCCEEDED(hr))hr=stream->InitializeFromMemory(data.data,(DWORD)data.size);
 if(SUCCEEDED(hr))hr=factory->CreateDecoderFromStream(stream,nullptr,WICDecodeMetadataCacheOnLoad,&decoder);
 if(SUCCEEDED(hr))hr=decoder->GetFrame(0,&frame);UINT width=0,height=0;
 if(SUCCEEDED(hr))hr=frame->GetSize(&width,&height);
 if(!width||!height||width>8192||height>8192)hr=E_INVALIDARG;
 UINT scaled_w=width,scaled_h=height;if(SUCCEEDED(hr)){if(scaled_w>360){scaled_h=(UINT)((uint64_t)scaled_h*360/scaled_w);scaled_w=360;}if(scaled_h>480){scaled_w=(UINT)((uint64_t)scaled_w*480/scaled_h);scaled_h=480;}if(!scaled_w)scaled_w=1;if(!scaled_h)scaled_h=1;hr=factory->CreateBitmapScaler(&scaler);}
 if(SUCCEEDED(hr))hr=scaler->Initialize(frame,scaled_w,scaled_h,WICBitmapInterpolationModeFant);
 if(SUCCEEDED(hr))hr=factory->CreateFormatConverter(&converter);
 if(SUCCEEDED(hr))hr=converter->Initialize(scaler,GUID_WICPixelFormat32bppBGR,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom);
 if(SUCCEEDED(hr)){BITMAPINFO info={};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=(LONG)scaled_w;info.bmiHeader.biHeight=-(LONG)scaled_h;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;void*bits=nullptr;bitmap=CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&bits,nullptr,0);if(!bitmap||FAILED(converter->CopyPixels(nullptr,scaled_w*4,scaled_w*scaled_h*4,(BYTE*)bits))){if(bitmap)DeleteObject(bitmap);bitmap=nullptr;}}
 release(converter);release(scaler);release(frame);release(decoder);release(stream);release(factory);return bitmap;
}
