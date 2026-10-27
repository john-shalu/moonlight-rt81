#include "ui.h"
#include <windowsx.h>
#include <shellapi.h>
#include <initializer_list>

namespace {
enum Page{HOME,APPS,SETTINGS,ADD_HOST,PAIRING,STREAM,DIAGNOSTICS};
enum{BACK=30,SETTINGS_BUTTON,REFRESH,ADD,EDIT_HOST,SAVE_SETTINGS,CANCEL_SETTINGS,TEST,DIAG,OPEN_LOG,SAVE_HOST,CANCEL_HOST,REMOVE_HOST,STREAM_MENU,DISCONNECT,WINDOW_MODE,CLOSE_DIAG,ADD_EMPTY,REPAIR_HOST,GAME_MOUSE,OPEN_LICENSE};
struct SavedHost{WCHAR address[256],name[128];unsigned port;int state;};
struct AppTile{WCHAR name[512];int id;HBITMAP art;};
HWND parent,back,settings_button,refresh,add,edit_host,empty_add,save_settings,cancel_settings,test,diag,open_log,save_host,cancel_host,remove_host,repair_host,menu_button,disconnect,window_mode,close_diag,open_license;
HWND address_edit,port_edit,name_edit,resolution_combo,fps_combo,bitrate_combo,fullscreen_check,sound_check,game_check,game_button,log_view,cards[128];
SavedHost hosts[16];AppTile apps[128];int host_count=0,current_host=-1,app_count=0,current_app=-1,scroll=0,content_height=0;
Page page=HOME,return_page=HOME,stream_return=HOME;bool busy=false,is_fullscreen=false,stream_menu=false,preview=false;
WCHAR settings_path[MAX_PATH],status[500],pin[8],override_address[256],override_port[20];
UiPreferences preferences;UiActionHandler dispatch;
HFONT font,small_font,title_font,card_font,brand_font,pin_font;
HBRUSH background,panel,field;int dpi=96;
const COLORREF BG=RGB(48,48,48),PANEL=RGB(34,34,34),CARD=RGB(61,61,61),FIELD=RGB(68,68,68),TEXT=RGB(245,245,245),MUTED=RGB(183,183,183),BLUE=RGB(77,172,255);
int px(int value){return MulDiv(value,dpi,96);}
void show(HWND h,bool visible){ShowWindow(h,visible?SW_SHOW:SW_HIDE);}
void place(HWND h,int x,int y,int width,int height){MoveWindow(h,px(x),px(y),px(width),px(height),TRUE);}
void fill(HDC dc,RECT r,COLORREF color){HBRUSH b=CreateSolidBrush(color);FillRect(dc,&r,b);DeleteObject(b);}
void line(HDC dc,int x1,int y1,int x2,int y2,COLORREF color,int width=1){HPEN pen=CreatePen(PS_SOLID,px(width),color);HGDIOBJ old=SelectObject(dc,pen);MoveToEx(dc,px(x1),px(y1),nullptr);LineTo(dc,px(x2),px(y2));SelectObject(dc,old);DeleteObject(pen);}
void text(HDC dc,const WCHAR*s,RECT r,HFONT f,COLORREF color,UINT flags=DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS){
 HGDIOBJ old=SelectObject(dc,f);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,color);DrawTextW(dc,s,-1,&r,flags|DT_NOPREFIX);SelectObject(dc,old);
}
RECT rect(int x,int y,int w,int h){RECT r={px(x),px(y),px(x+w),px(y+h)};return r;}
void label(HDC dc,const WCHAR*s,int x,int y,int w,int h,HFONT f=nullptr,COLORREF color=TEXT,UINT flags=DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS){text(dc,s,rect(x,y,w,h),f?f:font,color,flags);}
HWND button(const WCHAR*title,int id){HWND h=CreateWindowW(L"BUTTON",title,WS_CHILD|WS_TABSTOP|BS_OWNERDRAW,0,0,1,1,parent,(HMENU)(INT_PTR)id,GetModuleHandleW(nullptr),nullptr);SendMessageW(h,WM_SETFONT,(WPARAM)font,TRUE);return h;}
HWND combo(int id){HWND h=CreateWindowW(L"COMBOBOX",L"",WS_CHILD|WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,0,0,1,1,parent,(HMENU)(INT_PTR)id,GetModuleHandleW(nullptr),nullptr);SendMessageW(h,WM_SETFONT,(WPARAM)font,TRUE);return h;}
HWND edit(int id,bool numeric=false){HWND h=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_TABSTOP|ES_AUTOHSCROLL|(numeric?ES_NUMBER:0),0,0,1,1,parent,(HMENU)(INT_PTR)id,GetModuleHandleW(nullptr),nullptr);SendMessageW(h,WM_SETFONT,(WPARAM)font,TRUE);SendMessageW(h,EM_SETLIMITTEXT,numeric?5:255,0);return h;}
void add_item(HWND h,const WCHAR*value){SendMessageW(h,CB_ADDSTRING,0,(LPARAM)value);}
void save_hosts(){
 WCHAR value[30];wsprintfW(value,L"%d",host_count);WritePrivateProfileStringW(L"computers",L"count",value,settings_path);
 for(int i=0;i<host_count;i++){WCHAR section[30];wsprintfW(section,L"computer%d",i);WritePrivateProfileStringW(section,L"address",hosts[i].address,settings_path);WritePrivateProfileStringW(section,L"name",hosts[i].name,settings_path);wsprintfW(value,L"%u",hosts[i].port);WritePrivateProfileStringW(section,L"port",value,settings_path);}
 if(current_host>=0&&current_host<host_count){WritePrivateProfileStringW(L"client",L"host",hosts[current_host].address,settings_path);wsprintfW(value,L"%u",hosts[current_host].port);WritePrivateProfileStringW(L"client",L"port",value,settings_path);}
}
void load_preferences(){
 preferences.width=GetPrivateProfileIntW(L"stream",L"width",1280,settings_path);preferences.height=preferences.width==1920?1080:720;preferences.width=preferences.height==1080?1920:1280;
 preferences.fps=GetPrivateProfileIntW(L"stream",L"fps",30,settings_path)==60?60:30;preferences.bitrate=GetPrivateProfileIntW(L"stream",L"bitrate",5000,settings_path);
 if(preferences.bitrate<1000||preferences.bitrate>30000)preferences.bitrate=5000;
 preferences.fullscreen=GetPrivateProfileIntW(L"stream",L"fullscreen",1,settings_path)!=0;preferences.sound=GetPrivateProfileIntW(L"stream",L"sound",1,settings_path)!=0;preferences.game_mouse=GetPrivateProfileIntW(L"stream",L"game_mouse",0,settings_path)!=0;
}
void settings_values(){
 SendMessageW(resolution_combo,CB_SETCURSEL,preferences.width==1920?1:0,0);SendMessageW(fps_combo,CB_SETCURSEL,preferences.fps==60?1:0,0);
 const int rates[]={3000,5000,8000,10000,15000,20000,30000};int index=1;for(int i=0;i<7;i++)if(rates[i]==preferences.bitrate)index=i;SendMessageW(bitrate_combo,CB_SETCURSEL,index,0);
 SendMessageW(fullscreen_check,BM_SETCHECK,preferences.fullscreen?BST_CHECKED:BST_UNCHECKED,0);SendMessageW(sound_check,BM_SETCHECK,preferences.sound?BST_CHECKED:BST_UNCHECKED,0);SendMessageW(game_check,BM_SETCHECK,preferences.game_mouse?BST_CHECKED:BST_UNCHECKED,0);
}
void change_page(Page next){page=next;scroll=0;stream_menu=false;ui_layout();InvalidateRect(parent,nullptr,TRUE);}
void clear_apps(){for(int i=0;i<app_count;i++)if(apps[i].art)DeleteObject(apps[i].art);memset(apps,0,sizeof(apps));app_count=0;current_app=-1;}
void open_add(bool editing){
 if(!editing&&host_count>=16){ui_status(L"最多可以保存16台电脑。");return;}
 SetWindowTextW(address_edit,editing&&current_host>=0?hosts[current_host].address:L"");WCHAR port[20];wsprintfW(port,L"%u",editing&&current_host>=0?hosts[current_host].port:47989);SetWindowTextW(port_edit,port);SetWindowTextW(name_edit,editing&&current_host>=0?hosts[current_host].name:L"");
 SetWindowLongPtrW(save_host,GWLP_USERDATA,editing?1:0);change_page(ADD_HOST);SetFocus(address_edit);
}
void choose_host(int index){if(index<0||index>=host_count||busy)return;current_host=index;override_address[0]=0;if(!preview)save_hosts();clear_apps();if(preview){app_count=2;lstrcpyW(apps[0].name,L"Desktop");lstrcpyW(apps[1].name,L"Steam");apps[0].id=1;apps[1].id=2;change_page(APPS);}else dispatch(UI_CONNECT);}
void save_host_action(){
 WCHAR address[256],port_text[20],name[128];GetWindowTextW(address_edit,address,256);GetWindowTextW(port_edit,port_text,20);GetWindowTextW(name_edit,name,128);
 int port=_wtoi(port_text);bool valid=*address&&port>=1&&port<=65535;
 for(int i=0;address[i];i++)if(address[i]<=32||wcschr(L"/?#&\\",address[i]))valid=false;
 if(!valid){ui_status(L"请输入电脑的局域网 IP 或主机名，以及1到65535之间的端口。");return;}
 bool editing=GetWindowLongPtrW(save_host,GWLP_USERDATA)!=0;int index=editing?current_host:-1;
 for(int i=0;i<host_count;i++)if(!lstrcmpiW(address,hosts[i].address)&&port==(int)hosts[i].port)index=i;
 if(index<0){if(host_count>=16)return;index=host_count++;}lstrcpynW(hosts[index].address,address,256);lstrcpynW(hosts[index].name,*name?name:address,128);hosts[index].port=(unsigned)port;hosts[index].state=0;
 current_host=index;save_hosts();change_page(HOME);choose_host(index);
}
void monitor(HDC dc,RECT area,COLORREF color){
 int cx=(area.left+area.right)/2,cy=(area.top+area.bottom)/2;int ww=(area.right-area.left)*58/100,hh=ww*10/16;RECT screen={cx-ww/2,cy-hh/2-px(7),cx+ww/2,cy+hh/2-px(7)};
 HPEN pen=CreatePen(PS_SOLID,px(5),color);HGDIOBJ old=SelectObject(dc,pen),brush=SelectObject(dc,GetStockObject(NULL_BRUSH));RoundRect(dc,screen.left,screen.top,screen.right,screen.bottom,px(5),px(5));MoveToEx(dc,cx,screen.bottom,nullptr);LineTo(dc,cx,screen.bottom+px(17));MoveToEx(dc,cx-px(28),screen.bottom+px(18),nullptr);LineTo(dc,cx+px(28),screen.bottom+px(18));SelectObject(dc,brush);SelectObject(dc,old);DeleteObject(pen);
}
void draw_button(DRAWITEMSTRUCT*d){
 int id=(int)d->CtlID;bool pressed=(d->itemState&ODS_SELECTED)!=0,disabled=(d->itemState&ODS_DISABLED)!=0,focus=(d->itemState&ODS_FOCUS)!=0;
 COLORREF color=disabled?RGB(117,117,117):TEXT;RECT r=d->rcItem;
 if(id>=200&&id<328){
  int i=id-200;fill(d->hDC,r,pressed?RGB(79,79,79):CARD);
  if(page==HOME&&i<host_count){RECT icon=r;icon.bottom=icon.top+px(154);monitor(d->hDC,icon,disabled?MUTED:TEXT);RECT title=r;title.top+=px(158);title.bottom=title.top+px(34);title.left+=px(12);title.right-=px(12);text(d->hDC,hosts[i].name,title,card_font,color,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);RECT detail=title;detail.top+=px(34);detail.bottom+=px(31);text(d->hDC,hosts[i].state==1?L"在线":hosts[i].state==2?L"无法连接":L"点击连接",detail,small_font,hosts[i].state==1?RGB(129,211,139):MUTED,DT_CENTER|DT_VCENTER|DT_SINGLELINE);}
  else if(page==APPS&&i<app_count){
   RECT art=r;art.left+=px(10);art.right-=px(10);art.top+=px(10);art.bottom-=px(54);fill(d->hDC,art,RGB(45,45,45));
   if(apps[i].art){BITMAP info={};GetObjectW(apps[i].art,sizeof(info),&info);int aw=art.right-art.left,ah=art.bottom-art.top,w=aw,h=info.bmWidth?aw*info.bmHeight/info.bmWidth:ah;if(h>ah){h=ah;w=info.bmHeight?ah*info.bmWidth/info.bmHeight:aw;}HDC src=CreateCompatibleDC(d->hDC);HGDIOBJ old=SelectObject(src,apps[i].art);SetStretchBltMode(d->hDC,HALFTONE);StretchBlt(d->hDC,art.left+(aw-w)/2,art.top+(ah-h)/2,w,h,src,0,0,info.bmWidth,info.bmHeight,SRCCOPY);SelectObject(src,old);DeleteDC(src);}
   else{monitor(d->hDC,art,BLUE);RECT t=art;t.top=t.bottom-px(46);text(d->hDC,L"点击开始串流",t,small_font,MUTED,DT_CENTER|DT_VCENTER|DT_SINGLELINE);}
   RECT title=r;title.top=r.bottom-px(48);title.left+=px(10);title.right-=px(10);text(d->hDC,apps[i].name,title,font,color,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
  }
 }else{
  bool accent=id==SAVE_HOST||id==SAVE_SETTINGS||id==ADD_EMPTY;fill(d->hDC,r,pressed?RGB(77,91,104):accent?RGB(27,105,167):PANEL);
  WCHAR title[128];GetWindowTextW(d->hwndItem,title,128);text(d->hDC,title,r,font,color,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
 }
 if(focus){HPEN pen=CreatePen(PS_SOLID,px(2),BLUE);HGDIOBJ old=SelectObject(d->hDC,pen),br=SelectObject(d->hDC,GetStockObject(NULL_BRUSH));Rectangle(d->hDC,r.left+1,r.top+1,r.right-1,r.bottom-1);SelectObject(d->hDC,br);SelectObject(d->hDC,old);DeleteObject(pen);}
}
void paint(HDC dc){
 RECT client;GetClientRect(parent,&client);int w=MulDiv(client.right,96,dpi),h=MulDiv(client.bottom,96,dpi);FillRect(dc,&client,background);
 if(page==STREAM){FillRect(dc,&client,(HBRUSH)GetStockObject(BLACK_BRUSH));return;}
 fill(dc,rect(0,0,w,66),PANEL);
 // Moonlight's crescent mark, rendered with ordinary GDI primitives.
 HBRUSH moon=CreateSolidBrush(TEXT);HGDIOBJ br=SelectObject(dc,moon),pen=SelectObject(dc,GetStockObject(NULL_PEN));Ellipse(dc,px(24),px(18),px(52),px(46));SelectObject(dc,panel);Ellipse(dc,px(34),px(13),px(58),px(38));SelectObject(dc,br);SelectObject(dc,pen);DeleteObject(moon);
 label(dc,L"Moonlight",66,9,230,48,brand_font);
 if(page==HOME){label(dc,L"电脑",32,86,w-64,40,title_font);label(dc,host_count?L"选择电脑，开始串流。":L"添加运行 Sunshine 的电脑，即可开始。",32,129,w-64,28,font,MUTED);if(!host_count)monitor(dc,rect((w-240)/2,190,240,155),MUTED);}
 else if(page==APPS){WCHAR heading[160];lstrcpynW(heading,current_host>=0?hosts[current_host].name:L"应用",160);label(dc,heading,32,86,w-64,40,title_font);label(dc,L"选择一个应用，或继续上次的串流。",32,129,w-64,28,font,MUTED);}
 else if(page==ADD_HOST){label(dc,L"添加或编辑电脑",40,90,w-80,46,title_font);label(dc,L"电脑地址",48,155,250,28);label(dc,L"HTTP 端口",48,235,250,28);label(dc,L"显示名称（可选）",48,315,400,28);label(dc,L"在电脑上安装并启动 Sunshine，平板和电脑连接同一网络。",48,425,w-96,50,small_font,MUTED,DT_LEFT|DT_WORDBREAK);}
 else if(page==SETTINGS){label(dc,L"设置",40,86,w-80,42,title_font);label(dc,L"串流",48,142,300,32,card_font,BLUE);label(dc,L"分辨率",48,194,260,32);label(dc,L"帧率",48,248,260,32);label(dc,L"视频码率",48,302,260,32);label(dc,L"H.264 · 硬件解码 · 立体声",48,364,w-96,26,small_font,MUTED);label(dc,L"720p / 30 FPS 为已验证的默认值；更高画质需要更好的网络。",48,398,w-96,30,small_font,MUTED);}
 else if(page==PAIRING){label(dc,L"配对电脑",40,94,w-80,42,title_font);label(dc,L"在 Sunshine 的 PIN 页面输入下方数字",40,176,w-80,38,font,TEXT,DT_CENTER|DT_VCENTER|DT_SINGLELINE);label(dc,pin,40,230,w-80,116,pin_font,BLUE,DT_CENTER|DT_VCENTER|DT_SINGLELINE);label(dc,L"配对后，这台平板将自动记住电脑。",40,361,w-80,38,font,MUTED,DT_CENTER|DT_VCENTER|DT_SINGLELINE);}
 else if(page==DIAGNOSTICS){label(dc,L"帮助与诊断",40,86,w-260,42,title_font);label(dc,L"Moonlight RT 0.2.1  ·  Windows RT 8.1 社区适配版",40,134,w-80,32,font,MUTED);label(dc,L"© 2026 john-shalu 及上游作者 · GPLv3 · 无担保 · 可按许可修改、再分发",40,166,w-80,26,small_font,MUTED);}
 if(*status)label(dc,status,24,h-47,w-48,36,small_font,MUTED,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
}
void command(int id,int code){
 if(id>=200&&id<328&&!busy){int index=id-200;if(page==HOME)choose_host(index);else if(page==APPS&&index<app_count){current_app=index;if(preview)ui_status(L"界面预览：未连接主机。");else dispatch(UI_START);}return;}
 switch(id){
 case BACK:if(busy)return;if(page==SETTINGS){change_page(return_page);}else if(page==APPS)change_page(HOME);else change_page(HOME);break;
 case SETTINGS_BUTTON:if(!busy){return_page=page;settings_values();change_page(SETTINGS);}break;
 case REFRESH:if(!busy){if(current_host<0&&host_count)current_host=0;if(current_host>=0)dispatch(UI_CONNECT);else open_add(false);}break;
 case ADD:case ADD_EMPTY:if(!busy)open_add(false);break;
 case EDIT_HOST:if(!busy&&current_host>=0)open_add(true);break;
 case SAVE_HOST:save_host_action();break;
 case CANCEL_HOST:change_page(HOME);break;
 case REMOVE_HOST:if(current_host>=0&&!busy){int answer=MessageBoxW(parent,L"从本应用的电脑列表移除这台电脑？主机和配对凭据会保留。",L"移除电脑",MB_YESNO|MB_ICONQUESTION);if(answer==IDYES){for(int i=current_host;i+1<host_count;i++)hosts[i]=hosts[i+1];host_count--;current_host=host_count?0:-1;save_hosts();change_page(HOME);}}break;
 case REPAIR_HOST:if(!busy&&!preview)dispatch(UI_PAIR);break;
 case SAVE_SETTINGS:{
  preferences.width=SendMessageW(resolution_combo,CB_GETCURSEL,0,0)==1?1920:1280;preferences.height=preferences.width==1920?1080:720;preferences.fps=SendMessageW(fps_combo,CB_GETCURSEL,0,0)==1?60:30;int rate=(int)SendMessageW(bitrate_combo,CB_GETCURSEL,0,0);const int rates[]={3000,5000,8000,10000,15000,20000,30000};preferences.bitrate=rate>=0&&rate<7?rates[rate]:5000;
  preferences.fullscreen=SendMessageW(fullscreen_check,BM_GETCHECK,0,0)==BST_CHECKED;preferences.sound=SendMessageW(sound_check,BM_GETCHECK,0,0)==BST_CHECKED;preferences.game_mouse=SendMessageW(game_check,BM_GETCHECK,0,0)==BST_CHECKED;
  const WCHAR*names[]={L"width",L"fps",L"bitrate",L"fullscreen",L"sound",L"game_mouse"};int values[]={preferences.width,preferences.fps,preferences.bitrate,preferences.fullscreen?1:0,preferences.sound?1:0,preferences.game_mouse?1:0};for(int i=0;i<6;i++){WCHAR v[30];wsprintfW(v,L"%d",values[i]);WritePrivateProfileStringW(L"stream",names[i],v,settings_path);}change_page(return_page);ui_status(L"设置已保存，下次串流生效。");break;}
 case CANCEL_SETTINGS:change_page(return_page);break;
 case TEST:if(!busy)dispatch(UI_SELFTEST);break;
 case DIAG:change_page(DIAGNOSTICS);break;
 case CLOSE_DIAG:change_page(SETTINGS);break;
 case OPEN_LOG:dispatch(UI_LOG_FOLDER);break;
 case OPEN_LICENSE:{WCHAR path[MAX_PATH],quoted[MAX_PATH+4];GetModuleFileNameW(nullptr,path,MAX_PATH);WCHAR*slash=nullptr;for(WCHAR*cur=path;*cur;cur++)if(*cur==L'\\')slash=cur;if(slash&&slash-path<MAX_PATH-10){lstrcpyW(slash+1,L"LICENSE");wsprintfW(quoted,L"\"%s\"",path);ShellExecuteW(parent,L"open",L"notepad.exe",quoted,nullptr,SW_SHOWNORMAL);}break;}
 case DISCONNECT:dispatch(UI_STOP);break;
 case WINDOW_MODE:dispatch(UI_FULLSCREEN);break;
 case GAME_MOUSE:dispatch(UI_GAME_MOUSE);break;
 case STREAM_MENU:stream_menu=!stream_menu;ui_layout();break;
 }
}
}

bool ui_init(HWND window,HINSTANCE instance,const WCHAR*settings,UiActionHandler action,int display_dpi){
 parent=window;dispatch=action;lstrcpynW(settings_path,settings,MAX_PATH);dpi=display_dpi;if(dpi<96||dpi>288)dpi=96;
 auto make_font=[](int height,int weight){return CreateFontW(-px(height),0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");};
 font=make_font(16,FW_NORMAL);small_font=make_font(13,FW_NORMAL);title_font=make_font(26,FW_NORMAL);card_font=make_font(20,FW_NORMAL);brand_font=make_font(24,FW_NORMAL);pin_font=make_font(74,FW_LIGHT);background=CreateSolidBrush(BG);panel=CreateSolidBrush(PANEL);field=CreateSolidBrush(FIELD);
 back=button(L"‹  返回",BACK);settings_button=button(L"设置",SETTINGS_BUTTON);refresh=button(L"刷新",REFRESH);add=button(L"＋ 添加电脑",ADD);edit_host=button(L"编辑电脑",EDIT_HOST);empty_add=button(L"＋ 添加电脑",ADD_EMPTY);
 save_settings=button(L"保存",SAVE_SETTINGS);cancel_settings=button(L"取消",CANCEL_SETTINGS);test=button(L"本地自检",TEST);diag=button(L"帮助与诊断",DIAG);open_log=button(L"打开日志文件夹",OPEN_LOG);save_host=button(L"保存并连接",SAVE_HOST);cancel_host=button(L"取消",CANCEL_HOST);remove_host=button(L"移除电脑",REMOVE_HOST);repair_host=button(L"重新配对",REPAIR_HOST);menu_button=button(L"☰",STREAM_MENU);disconnect=button(L"断开串流",DISCONNECT);window_mode=button(L"窗口 / 全屏",WINDOW_MODE);close_diag=button(L"返回设置",CLOSE_DIAG);
 open_license=button(L"开源许可",OPEN_LICENSE);
 address_edit=edit(60);port_edit=edit(61,true);name_edit=edit(62);resolution_combo=combo(70);fps_combo=combo(71);bitrate_combo=combo(72);
 add_item(resolution_combo,L"1280 × 720（720p）");add_item(resolution_combo,L"1920 × 1080（1080p）");add_item(fps_combo,L"30 FPS");add_item(fps_combo,L"60 FPS");for(const WCHAR*v:{L"3 Mbps",L"5 Mbps",L"8 Mbps",L"10 Mbps",L"15 Mbps",L"20 Mbps",L"30 Mbps"})add_item(bitrate_combo,v);
 fullscreen_check=CreateWindowW(L"BUTTON",L"串流时全屏",WS_CHILD|WS_TABSTOP|BS_AUTOCHECKBOX,0,0,1,1,parent,(HMENU)73,instance,nullptr);sound_check=CreateWindowW(L"BUTTON",L"播放声音",WS_CHILD|WS_TABSTOP|BS_AUTOCHECKBOX,0,0,1,1,parent,(HMENU)74,instance,nullptr);SendMessageW(fullscreen_check,WM_SETFONT,(WPARAM)font,TRUE);SendMessageW(sound_check,WM_SETFONT,(WPARAM)font,TRUE);
 game_check=CreateWindowW(L"BUTTON",L"默认游戏鼠标",WS_CHILD|WS_TABSTOP|BS_AUTOCHECKBOX,0,0,1,1,parent,(HMENU)75,instance,nullptr);SendMessageW(game_check,WM_SETFONT,(WPARAM)font,TRUE);game_button=button(L"游戏鼠标：关",GAME_MOUSE);
 log_view=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VSCROLL|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL,0,0,1,1,parent,nullptr,instance,nullptr);SendMessageW(log_view,WM_SETFONT,(WPARAM)small_font,TRUE);SendMessageW(log_view,EM_SETLIMITTEXT,119000,0);
 for(int i=0;i<128;i++)cards[i]=button(L"",200+i);
 host_count=GetPrivateProfileIntW(L"computers",L"count",0,settings_path);if(host_count<0||host_count>16)host_count=0;
 for(int i=0;i<host_count;i++){WCHAR section[30];wsprintfW(section,L"computer%d",i);GetPrivateProfileStringW(section,L"address",L"",hosts[i].address,256,settings_path);GetPrivateProfileStringW(section,L"name",hosts[i].address,hosts[i].name,128,settings_path);hosts[i].port=GetPrivateProfileIntW(section,L"port",47989,settings_path);if(!hosts[i].port||hosts[i].port>65535)hosts[i].port=47989;hosts[i].state=0;}
 if(!host_count){GetPrivateProfileStringW(L"client",L"host",L"",hosts[0].address,256,settings_path);if(*hosts[0].address){host_count=1;lstrcpynW(hosts[0].name,hosts[0].address,128);hosts[0].port=GetPrivateProfileIntW(L"client",L"port",47989,settings_path);if(!hosts[0].port||hosts[0].port>65535)hosts[0].port=47989;}}
 current_host=host_count?0:-1;load_preferences();settings_values();ui_status(L"Moonlight RT  ·  Windows RT 8.1");ui_layout();return true;
}
void ui_destroy(){clear_apps();for(HFONT f:{font,small_font,title_font,card_font,brand_font,pin_font})if(f)DeleteObject(f);if(background)DeleteObject(background);if(panel)DeleteObject(panel);if(field)DeleteObject(field);}
void ui_layout(){
 if(!parent)return;RECT client;GetClientRect(parent,&client);int w=MulDiv(client.right,96,dpi),h=MulDiv(client.bottom,96,dpi);SetWindowTextW(disconnect,page==PAIRING?L"取消配对":page==STREAM?L"断开串流":L"取消连接");
 HWND all[]={back,settings_button,refresh,add,edit_host,empty_add,save_settings,cancel_settings,test,diag,open_log,save_host,cancel_host,remove_host,repair_host,menu_button,disconnect,window_mode,game_button,close_diag,address_edit,port_edit,name_edit,resolution_combo,fps_combo,bitrate_combo,fullscreen_check,sound_check,game_check,log_view,open_license};for(HWND control:all)show(control,false);for(HWND card:cards)show(card,false);
 show(g_video,page==STREAM);if(page==STREAM){int top=is_fullscreen?0:58;int vw=w,vh=vw*9/16;if(vh>h-top){vh=h-top;vw=vh*16/9;}place(g_video,(w-vw)/2,top+(h-top-vh)/2,vw,vh);show(menu_button,true);place(menu_button,w-58,8,48,42);SetWindowPos(menu_button,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE);if(!is_fullscreen||stream_menu){show(disconnect,true);show(window_mode,true);show(game_button,true);place(disconnect,16,8,132,42);place(window_mode,158,8,142,42);place(game_button,310,8,176,42);SetWindowPos(disconnect,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE);SetWindowPos(window_mode,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE);SetWindowPos(game_button,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE);}ShowScrollBar(parent,SB_VERT,FALSE);return;}
 show(settings_button,page==HOME||page==APPS);place(settings_button,w-104,10,84,46);show(back,page!=HOME&&page!=PAIRING);place(back,300,10,104,46);
 if(page==HOME||page==APPS){show(refresh,true);place(refresh,w-202,10,88,46);show(add,page==HOME&&!busy);place(add,w-354,10,142,46);show(edit_host,page==APPS&&!busy);place(edit_host,w-354,10,142,46);if(busy){show(disconnect,true);place(disconnect,w-354,10,142,46);}}
 if(page==HOME||page==APPS){int count=page==HOME?host_count:app_count;int cw=page==HOME?280:200,ch=page==HOME?236:270,gap=20,cols=(w-64+gap)/(cw+gap);if(cols<1)cols=1;if(count>0&&cols>count)cols=count;int gridw=cols*cw+(cols-1)*gap,x=(w-gridw)/2;int rows=(count+cols-1)/cols;content_height=rows*(ch+gap);int viewh=h-230;int max_scroll=content_height-viewh;if(max_scroll<0)max_scroll=0;if(scroll>max_scroll)scroll=max_scroll;
  SCROLLINFO si={sizeof(si),SIF_RANGE|SIF_PAGE|SIF_POS,0,content_height,((UINT)(viewh>0?viewh:1)),scroll,0};SetScrollInfo(parent,SB_VERT,&si,TRUE);ShowScrollBar(parent,SB_VERT,max_scroll>0);
  for(int i=0;i<count;i++){int y=178+(i/cols)*(ch+gap)-scroll;place(cards[i],x+(i%cols)*(cw+gap),y,cw,ch);show(cards[i],y>=166&&y+ch<=h-52);EnableWindow(cards[i],!busy);SetWindowTextW(cards[i],page==HOME?hosts[i].name:apps[i].name);}
  if(!host_count&&page==HOME){show(empty_add,true);place(empty_add,(w-220)/2,360,220,54);}return;
 }
 ShowScrollBar(parent,SB_VERT,FALSE);
 if(page==ADD_HOST){for(HWND c:{address_edit,port_edit,name_edit,save_host,cancel_host})show(c,true);place(address_edit,48,188,w>750?600:w-96,38);place(port_edit,48,268,170,38);place(name_edit,48,348,w>750?600:w-96,38);place(save_host,48,h-112,178,48);place(cancel_host,238,h-112,110,48);if(GetWindowLongPtrW(save_host,GWLP_USERDATA)&&current_host>=0){show(remove_host,true);show(repair_host,true);place(repair_host,365,h-112,140,48);place(remove_host,w-192,h-112,144,48);}}
 if(page==SETTINGS){for(HWND c:{resolution_combo,fps_combo,bitrate_combo,fullscreen_check,sound_check,game_check,save_settings,cancel_settings,diag})show(c,true);place(resolution_combo,320,190,340,250);place(fps_combo,320,244,340,180);place(bitrate_combo,320,298,340,330);place(fullscreen_check,48,442,250,34);place(sound_check,320,442,220,34);place(game_check,560,442,240,34);place(save_settings,48,h-112,140,48);place(cancel_settings,200,h-112,112,48);place(diag,w-228,h-112,180,48);}
 if(page==PAIRING){show(disconnect,true);SetWindowTextW(disconnect,L"取消配对");place(disconnect,(w-160)/2,434,160,48);}
 else SetWindowTextW(disconnect,L"断开串流");
 if(page==DIAGNOSTICS){for(HWND c:{log_view,test,open_log,close_diag,open_license})show(c,true);place(open_license,w-198,90,158,38);place(log_view,40,210,w-80,h-346);place(test,40,h-112,148,48);place(open_log,200,h-112,190,48);place(close_diag,w-210,h-112,170,48);EnableWindow(test,!busy);}
}
bool ui_message(HWND,UINT message,WPARAM w,LPARAM l,LRESULT*result){
 if(message==WM_ERASEBKGND){*result=1;return true;}
 if(message==WM_PAINT){PAINTSTRUCT ps;HDC dc=BeginPaint(parent,&ps);paint(dc);EndPaint(parent,&ps);*result=0;return true;}
 if(message==WM_DRAWITEM){draw_button((DRAWITEMSTRUCT*)l);*result=TRUE;return true;}
 if(message==WM_CTLCOLORSTATIC||message==WM_CTLCOLOREDIT||message==WM_CTLCOLORBTN||message==WM_CTLCOLORLISTBOX){HDC dc=(HDC)w;SetTextColor(dc,TEXT);SetBkColor(dc,(message==WM_CTLCOLOREDIT||message==WM_CTLCOLORLISTBOX)?FIELD:BG);*result=(LRESULT)((message==WM_CTLCOLOREDIT||message==WM_CTLCOLORLISTBOX)?field:background);return true;}
 if(message==WM_COMMAND){command(LOWORD(w),HIWORD(w));*result=0;return true;}
 if(message==WM_CONTEXTMENU&&!busy&&page==HOME){for(int i=0;i<host_count;i++)if((HWND)w==cards[i]){current_host=i;open_add(true);*result=0;return true;}}
 if((message==WM_VSCROLL||message==WM_MOUSEWHEEL)&&(page==HOME||page==APPS)){int delta=0;if(message==WM_MOUSEWHEEL)delta=-GET_WHEEL_DELTA_WPARAM(w)/WHEEL_DELTA*80;else{switch(LOWORD(w)){case SB_LINEUP:delta=-40;break;case SB_LINEDOWN:delta=40;break;case SB_PAGEUP:delta=-200;break;case SB_PAGEDOWN:delta=200;break;case SB_THUMBTRACK:{SCROLLINFO si={sizeof(si),SIF_TRACKPOS};GetScrollInfo(parent,SB_VERT,&si);scroll=si.nTrackPos;break;}}}scroll+=delta;if(scroll<0)scroll=0;ui_layout();InvalidateRect(parent,nullptr,TRUE);*result=0;return true;}
 if(message==WM_KEYDOWN&&w==VK_ESCAPE&&!busy&&page!=STREAM){command(BACK,0);*result=0;return true;}
 return false;
}
void ui_busy(bool value){busy=value;for(HWND h:{back,settings_button,refresh,add,edit_host,empty_add,save_settings,cancel_settings,save_host,cancel_host,remove_host})EnableWindow(h,!busy);ui_layout();InvalidateRect(parent,nullptr,FALSE);}
void ui_status(const WCHAR*value){lstrcpynW(status,value,500);if(parent)InvalidateRect(parent,nullptr,FALSE);}
void ui_pairing(const char*value){utf8_to_wide(value,pin,8);change_page(PAIRING);}
void ui_apps(HostState*host){clear_apps();app_count=host_app_count(host);if(app_count>128)app_count=128;for(int i=0;i<app_count;i++){apps[i].id=host_app_id(host,i);utf8_to_wide(host_app_name(host,i),apps[i].name,512);}change_page(APPS);}
void ui_host_result(HostState*host,bool online){if(current_host>=0){hosts[current_host].state=online?1:2;if(online&&*host_name(host)&&(!*hosts[current_host].name||!lstrcmpiW(hosts[current_host].name,hosts[current_host].address))){utf8_to_wide(host_name(host),hosts[current_host].name,128);save_hosts();}}InvalidateRect(parent,nullptr,FALSE);}
void ui_finished(int action,bool ok,bool paired,bool cancelled){ui_busy(false);if(page==PAIRING&&(!ok||cancelled))change_page(HOME);if(page==STREAM){change_page(stream_return);}if(!ok&&!cancelled)ui_status(L"连接未完成。请检查电脑和网络，可在“设置 → 帮助与诊断”查看详情。");}
void ui_stream(bool active){if(active){if(page!=STREAM)stream_return=page;change_page(STREAM);stream_menu=false;}else if(page==STREAM)change_page(stream_return);}
void ui_fullscreen(bool active){is_fullscreen=active;ui_layout();InvalidateRect(parent,nullptr,TRUE);}
void ui_log(const WCHAR*value){SetWindowTextW(log_view,value);SendMessageW(log_view,EM_SETSEL,-1,-1);SendMessageW(log_view,EM_SCROLLCARET,0,0);}
bool ui_log_visible(){return page==DIAGNOSTICS;}
void ui_game_mouse(bool enabled){SetWindowTextW(game_button,enabled?L"游戏鼠标：开":L"游戏鼠标：关");}
void ui_override_target(const WCHAR*address,const WCHAR*port){lstrcpynW(override_address,address,256);lstrcpynW(override_port,port,20);}
bool ui_target(char*address,int capacity,unsigned*port){if(*override_address){wide_to_utf8(override_address,address,capacity);*port=(unsigned)_wtoi(override_port);}else{if(current_host<0||current_host>=host_count)return false;wide_to_utf8(hosts[current_host].address,address,capacity);*port=hosts[current_host].port;}return *address&&*port&&*port<=65535;}
int ui_selected_app(){return current_app>=0&&current_app<app_count?apps[current_app].id:-1;}
UiPreferences ui_preferences(){return preferences;}
HWND ui_focus_control(){return menu_button;}
void ui_art(int index,HBITMAP bitmap){if(index<0||index>=app_count){if(bitmap)DeleteObject(bitmap);return;}if(apps[index].art)DeleteObject(apps[index].art);apps[index].art=bitmap;InvalidateRect(cards[index],nullptr,TRUE);}
void ui_preview(){preview=true;if(!host_count){host_count=2;lstrcpyW(hosts[0].address,L"192.0.2.10");lstrcpyW(hosts[0].name,L"Gaming PC");hosts[0].port=47989;hosts[0].state=1;lstrcpyW(hosts[1].address,L"192.0.2.11");lstrcpyW(hosts[1].name,L"Living room PC");hosts[1].port=47989;}ui_layout();}
