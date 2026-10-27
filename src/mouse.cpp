#include "mouse.h"
#include <windowsx.h>

namespace {
typedef BOOL(WINAPI*RegisterRawFn)(PCRAWINPUTDEVICE,UINT,UINT);
typedef UINT(WINAPI*GetRawFn)(HRAWINPUT,UINT,LPVOID,PUINT,UINT);
typedef BOOL(WINAPI*GetPointerFn)(UINT32,POINTER_INFO*);
RegisterRawFn register_raw;GetRawFn get_raw;GetPointerFn get_pointer;
HWND view;bool active=false,game=false,captured=false,raw=false;unsigned buttons=0;UINT32 touch_id=0;DWORD promoted_until=0;POINT center={};
bool promoted(){return ((ULONG_PTR)GetMessageExtraInfo()&0xffffff00u)==0xff515700u;}
void position(int x,int y){RECT r;GetClientRect(view,&r);if(r.right<2||r.bottom<2)return;InputEvent e;e.kind=INPUT_POSITION;e.width=r.right;e.height=r.bottom;e.x=x<0?0:x>=r.right?r.right-1:x;e.y=y<0?0:y>=r.bottom?r.bottom-1:y;input_enqueue(e);}
void button(int id,bool down){InputEvent e;e.kind=INPUT_BUTTON;e.x=id;e.action=down?BUTTON_ACTION_PRESS:BUTTON_ACTION_RELEASE;input_enqueue(e);}
void relative(int x,int y){if(!x&&!y)return;InputEvent e;e.kind=INPUT_RELATIVE;e.x=x;e.y=y;input_enqueue(e);}
void unclip(){
 if(captured){ClipCursor(nullptr);captured=false;}if(raw&&register_raw){RAWINPUTDEVICE device={1,2,RIDEV_REMOVE,nullptr};register_raw(&device,1,sizeof(device));raw=false;}SetCursor(LoadCursorW(nullptr,IDC_ARROW));
}
void release_buttons(){for(int i=1;i<=5;i++)if(buttons&(1u<<(i-1)))button(i,false);buttons=0;if(GetCapture()==view)ReleaseCapture();}
bool capture(){
 if(!active||!game||GetForegroundWindow()!=g_main)return false;
 RECT r;GetClientRect(view,&r);center.x=(r.left+r.right)/2;center.y=(r.top+r.bottom)/2;ClientToScreen(view,&center);
 if(register_raw&&get_raw){RAWINPUTDEVICE device={1,2,0,view};raw=register_raw(&device,1,sizeof(device))!=FALSE;}
 POINT a={r.left,r.top},b={r.right,r.bottom};ClientToScreen(view,&a);ClientToScreen(view,&b);RECT point=raw?RECT{center.x,center.y,center.x+1,center.y+1}:RECT{a.x,a.y,b.x,b.y};SetCursorPos(center.x,center.y);captured=ClipCursor(&point)!=FALSE;
 if(!captured){unclip();return false;}SetCursor(nullptr);return true;
}
bool pointer_info(WPARAM w,POINTER_INFO&info){return get_pointer&&get_pointer(GET_POINTERID_WPARAM(w),&info)&&(info.pointerType==PT_TOUCH||info.pointerType==PT_PEN);}
}
void mouse_init(HWND video){view=video;HMODULE user=GetModuleHandleW(L"user32.dll");register_raw=(RegisterRawFn)GetProcAddress(user,"RegisterRawInputDevices");get_raw=(GetRawFn)GetProcAddress(user,"GetRawInputData");get_pointer=(GetPointerFn)GetProcAddress(user,"GetPointerInfo");}
void mouse_begin(bool game_mode){unclip();active=true;game=game_mode;buttons=touch_id=0;promoted_until=0;if(game)capture();else if(GetForegroundWindow()==g_main){POINT pt;RECT r;GetCursorPos(&pt);ScreenToClient(view,&pt);GetClientRect(view,&r);if(pt.x>=0&&pt.y>=0&&pt.x<r.right&&pt.y<r.bottom)SetCursor(nullptr);}}
void mouse_end(){release_buttons();unclip();active=false;touch_id=0;}
void mouse_suspend(){release_buttons();unclip();if(touch_id){button(BUTTON_LEFT,false);touch_id=0;}}
bool mouse_toggle_game(){game=!game;release_buttons();if(game){SetFocus(view);capture();}else unclip();return game;}
bool mouse_is_game(){return game;}
void mouse_prepare_message(const MSG&m){if(!active)return;if(m.message==WM_POINTERDOWN||m.message==WM_NCPOINTERDOWN){release_buttons();unclip();}}
bool mouse_message(HWND window,UINT msg,WPARAM w,LPARAM l,LRESULT*result){
 if(!active)return false;
 if(msg==WM_SETCURSOR&&LOWORD(l)==HTCLIENT){if(!game||captured){SetCursor(nullptr);*result=TRUE;return true;}}
 if(msg==WM_KILLFOCUS||msg==WM_CANCELMODE){mouse_suspend();return false;}
 if(msg==WM_POINTERDOWN||msg==WM_POINTERUPDATE||msg==WM_POINTERUP){
  POINTER_INFO info={};if(!pointer_info(w,info))return false;promoted_until=GetTickCount()+250;UINT32 id=info.pointerId;
  if(msg==WM_POINTERDOWN){if(touch_id||!(info.pointerFlags&POINTER_FLAG_PRIMARY)){*result=0;return true;}release_buttons();unclip();SetFocus(view);touch_id=id;}
  if(touch_id==id){POINT pt=info.ptPixelLocation;ScreenToClient(window,&pt);position(pt.x,pt.y);if(msg==WM_POINTERDOWN)button(BUTTON_LEFT,true);if(msg==WM_POINTERUP||(info.pointerFlags&POINTER_FLAG_CANCELED)){button(BUTTON_LEFT,false);touch_id=0;}}
  *result=0;return true;
 }
 if(msg==WM_POINTERCAPTURECHANGED&&touch_id){button(BUTTON_LEFT,false);touch_id=0;*result=0;return true;}
 if(msg==WM_INPUT){
  if(captured&&raw){RAWINPUT input={};UINT size=sizeof(input);if(get_raw((HRAWINPUT)l,RID_INPUT,&input,&size,sizeof(RAWINPUTHEADER))!=(UINT)-1&&input.header.dwType==RIM_TYPEMOUSE&&!(input.data.mouse.usFlags&MOUSE_MOVE_ABSOLUTE))relative(input.data.mouse.lLastX,input.data.mouse.lLastY);}
  // Foreground raw input must still receive DefWindowProc cleanup.
  *result=DefWindowProcW(window,msg,w,l);return true;
 }
 bool touch=promoted();if(touch&&promoted_until&&(LONG)(promoted_until-GetTickCount())>0&&(msg>=WM_MOUSEFIRST&&msg<=WM_MOUSELAST)){*result=0;return true;}
 if(msg==WM_MOUSEMOVE){if(touch){unclip();position(GET_X_LPARAM(l),GET_Y_LPARAM(l));}else if(game){if(captured&&!raw){POINT p={GET_X_LPARAM(l),GET_Y_LPARAM(l)};ClientToScreen(window,&p);if(p.x!=center.x||p.y!=center.y){relative(p.x-center.x,p.y-center.y);SetCursorPos(center.x,center.y);}}}else position(GET_X_LPARAM(l),GET_Y_LPARAM(l));*result=0;return true;}
 int id=0;bool down=false;
 if(msg==WM_LBUTTONDOWN||msg==WM_LBUTTONUP){id=BUTTON_LEFT;down=msg==WM_LBUTTONDOWN;}
 else if(msg==WM_RBUTTONDOWN||msg==WM_RBUTTONUP){id=BUTTON_RIGHT;down=msg==WM_RBUTTONDOWN;}
 else if(msg==WM_MBUTTONDOWN||msg==WM_MBUTTONUP){id=BUTTON_MIDDLE;down=msg==WM_MBUTTONDOWN;}
 else if(msg==WM_XBUTTONDOWN||msg==WM_XBUTTONUP){id=GET_XBUTTON_WPARAM(w)==XBUTTON1?BUTTON_X1:BUTTON_X2;down=msg==WM_XBUTTONDOWN;}
 if(id){if(touch){unclip();position(GET_X_LPARAM(l),GET_Y_LPARAM(l));}else if(game){if(down&&!captured){SetFocus(view);capture();}}else position(GET_X_LPARAM(l),GET_Y_LPARAM(l));
  if(down){SetFocus(view);buttons|=1u<<(id-1);if(!captured)SetCapture(view);}else buttons&=~(1u<<(id-1));button(id,down);if(!down&&!buttons&&GetCapture()==view)ReleaseCapture();*result=TRUE;return true;}
 if(msg==WM_MOUSEWHEEL){InputEvent e;e.kind=INPUT_SCROLL;e.x=(short)GET_WHEEL_DELTA_WPARAM(w);input_enqueue(e);*result=0;return true;}
 if(msg==WM_CAPTURECHANGED){release_buttons();*result=0;return true;}
 return false;
}
