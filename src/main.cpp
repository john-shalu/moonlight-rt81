#include "app.h"
#include "ui.h"
#include "mouse.h"
#include <windowsx.h>
#include <shellapi.h>
#include <mmsystem.h>

static HostState*host=nullptr;static VideoState*video=nullptr;static AudioState*audio=nullptr;
static HANDLE worker=nullptr,stop_event=nullptr,cancel_event=nullptr,job_done_event=nullptr;static volatile LONG core_entered=0;
static bool full_screen=false,automatic=false,auto_visible=false,closing=false,auto_pair_needed=false,host_online=false,preview_mode=false;
static RECT normal_rect;static DWORD normal_style;static bool held_keys[256];
static int selected_app=-1,job=0,exit_code=0;static DWORD test_duration=10000;static char target[256],pin_code[8];static unsigned target_port=47989;
static WCHAR test_folder[MAX_PATH],settings_path[MAX_PATH];
static bool input_test_mode=false;
static int window_dpi=96;
static volatile LONG ui_tick=0,ui_phase=0;static unsigned job_generation=0;static bool finish_pending=false,finish_ok=false;
static DWORD close_started=0;
static void input_key(short key,char action,char flags){InputEvent e;e.kind=INPUT_KEY;e.x=key;e.action=action;e.flags=flags;input_enqueue(e);}

enum{JOB_REFRESH=1,JOB_PAIR,JOB_START,JOB_TEST,JOB_SMOKE,JOB_PAIR_TEST,JOB_STREAM_TEST};

static void controls(bool busy){ui_busy(busy);}
static void layout(){ui_layout();}
static void release_input(){input_release_all();memset(held_keys,0,sizeof(held_keys));mouse_suspend();}
static void request_stop(){
 release_input();mouse_end();InterlockedExchange(&g_streaming,0);InterlockedExchange(&g_stop_requested,1);SetEvent(cancel_event);SetEvent(stop_event);app_status(L"正在停止并释放串流资源…");
}
static char modifiers(LPARAM l){char m=0;if(GetKeyState(VK_SHIFT)&0x8000)m|=MODIFIER_SHIFT;if(GetKeyState(VK_CONTROL)&0x8000)m|=MODIFIER_CTRL;if(GetKeyState(VK_MENU)&0x8000)m|=MODIFIER_ALT;if((GetKeyState(VK_LWIN)|GetKeyState(VK_RWIN))&0x8000)m|=MODIFIER_META;if(l&(1L<<24))m|=MODIFIER_EXTENDED;return m;}
static LRESULT CALLBACK video_proc(HWND h,UINT msg,WPARAM w,LPARAM l){
 if(msg==WM_GETDLGCODE)return DLGC_WANTALLKEYS;
 if(msg==WM_MOUSEACTIVATE)return MA_ACTIVATE;
 if(msg==WM_SETFOCUS&&g_streaming){app_log("Input focus acquired by video window");return 0;}
 if(g_streaming){
  LRESULT mouse_result=0;if(mouse_message(h,msg,w,l,&mouse_result))return mouse_result;
  if(msg==WM_KEYDOWN||msg==WM_KEYUP||msg==WM_SYSKEYDOWN||msg==WM_SYSKEYUP){
   bool pressed=msg==WM_KEYDOWN||msg==WM_SYSKEYDOWN;UINT key=(UINT)w&255;
   if(pressed&&(key=='Q'||key=='M')&&(GetKeyState(VK_CONTROL)&0x8000)&&(GetKeyState(VK_MENU)&0x8000)&&(GetKeyState(VK_SHIFT)&0x8000)){if(!(l&(1L<<30))){if(key=='Q')request_stop();else{release_input();ui_game_mouse(mouse_toggle_game());}}return 0;}
   if(key==VK_SHIFT)key=MapVirtualKeyW((UINT)((l>>16)&255),MAPVK_VSC_TO_VK_EX);
   else if(key==VK_CONTROL)key=(l&(1L<<24))?VK_RCONTROL:VK_LCONTROL;
   else if(key==VK_MENU)key=(l&(1L<<24))?VK_RMENU:VK_LMENU;
   if(key<256){if(!pressed&&!held_keys[key])return 0;char flags=modifiers(l);held_keys[key]=pressed;input_key((short)key,pressed?KEY_ACTION_DOWN:KEY_ACTION_UP,flags);}return 0;
  }
  if(msg==WM_KILLFOCUS){release_input();app_log("Input focus left video window; held keys/buttons released");return 0;}
 }
 return DefWindowProcW(h,msg,w,l);
}
// Exercises this application's own windows only; no connection or global input injection.
static bool input_routing_selftest(){
 POINT point={20,20};ClientToScreen(g_video,&point);LRESULT hit=SendMessageW(g_video,WM_NCHITTEST,0,MAKELPARAM(point.x,point.y));
 mouse_begin(false);InterlockedExchange(&g_streaming,1);SetFocus(g_video);bool focused=GetFocus()==g_video;
 SendMessageW(g_video,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(20,20));bool captured=GetCapture()==g_video;
 SendMessageW(g_video,WM_LBUTTONUP,0,MAKELPARAM(20,20));bool uncaptured=GetCapture()!=g_video;
 SendMessageW(g_video,WM_KEYDOWN,'A',1|(0x1e<<16));bool key_down=held_keys['A'];SendMessageW(g_video,WM_KEYUP,'A',1|(0x1e<<16));
 SendMessageW(g_video,WM_KEYDOWN,'B',1|(0x30<<16));SetFocus(ui_focus_control());bool released=!held_keys['B'];SetFocus(g_main);bool restored=GetFocus()==g_video;
 InputEvent e;e.kind=INPUT_KEY;e.x='A';e.action=KEY_ACTION_UP;bool offline=input_enqueue(e)==-2;
 release_input();mouse_end();InterlockedExchange(&g_streaming,0);bool ok=hit==HTCLIENT&&focused&&captured&&uncaptured&&key_down&&!held_keys['A']&&released&&restored&&offline;
 app_log("Input routing selftest=%s hit=%d focus=%d capture=%d release=%d keyboard=%d focus_release=%d focus_restore=%d offline_rejection=%d",ok?"PASS":"FAIL",(int)hit,focused,captured,uncaptured,key_down,released,restored,offline);return ok;
}
static void listener_log(const char*fmt,...){char text[3000];va_list ap;va_start(ap,fmt);vsnprintf(text,sizeof(text),fmt,ap);va_end(ap);app_log("%s",text);}
static void stage_starting(int stage){WCHAR name[120],message[180];utf8_to_wide(LiGetStageName(stage),name,120);wsprintfW(message,L"正在连接：%s",name);app_status(message);}
static void stage_complete(int stage){app_log("Connection stage complete: %s",LiGetStageName(stage));}
static void stage_failed(int stage,int error){app_log("Connection stage failed: %s error=%d",LiGetStageName(stage),error);}
static void connection_started(){app_status(L"已连接，正在准备输入。停止：Ctrl+Alt+Shift+Q。");}
static void connection_ended(int error){app_log("Connection terminated: %d",error);if(stop_event)SetEvent(stop_event);}
static void connection_status(int status){if(status==CONN_STATUS_POOR)app_status(L"网络不稳定：可先降低码率；点“停止”可退出。");}
static bool start_stream(){
 STREAM_CONFIGURATION config;SERVER_INFORMATION server;LiInitializeStreamConfiguration(&config);
 UiPreferences pref=ui_preferences();config.width=pref.width;config.height=pref.height;config.fps=pref.fps;config.bitrate=pref.bitrate;audio_muted(audio,!pref.sound);config.packetSize=1392;config.streamingRemotely=STREAM_CFG_LOCAL;
 config.audioConfiguration=AUDIO_CONFIGURATION_STEREO;config.supportedVideoFormats=VIDEO_FORMAT_H264;config.clientRefreshRateX100=6000;config.colorSpace=COLORSPACE_REC_709;config.colorRange=COLOR_RANGE_LIMITED;config.encryptionFlags=ENCFLG_ALL;
 if(!host_launch(host,selected_app,&config,&server))return false;
 CONNECTION_LISTENER_CALLBACKS listener;LiInitializeConnectionCallbacks(&listener);listener.stageStarting=stage_starting;listener.stageComplete=stage_complete;listener.stageFailed=stage_failed;listener.connectionStarted=connection_started;listener.connectionTerminated=connection_ended;listener.logMessage=listener_log;listener.connectionStatusUpdate=connection_status;
 DECODER_RENDERER_CALLBACKS v;AUDIO_RENDERER_CALLBACKS a;video_callbacks(video,&v);audio_callbacks(audio,&a);ResetEvent(stop_event);InterlockedExchange(&core_entered,1);
 if(g_stop_requested){InterlockedExchange(&core_entered,0);return false;}
 int rc=LiStartConnection(&server,&config,&listener,&v,&a,video,0,audio,0);SecureZeroMemory(config.remoteInputAesKey,sizeof(config.remoteInputAesKey));
 app_log("LiStartConnection returned %d",rc);
 bool sender_ok=false;
 if(rc==0){
  sender_ok=input_session_begin();if(sender_ok&&!g_stop_requested)PostMessageW(g_main,WM_APP_STREAM_ACTIVE,job_generation,0);
  bool stalled=false;DWORD started=GetTickCount();
  while(sender_ok&&!g_stop_requested){DWORD elapsed=GetTickCount()-started;if(job==JOB_STREAM_TEST&&elapsed>=test_duration)break;if(WaitForSingleObject(stop_event,250)==WAIT_OBJECT_0)break;
   DWORD age=GetTickCount()-(DWORD)InterlockedCompareExchange(&ui_tick,0,0),call_age=input_call_age();if(age>2500||call_age>1500){if(!stalled)app_log("Responsiveness watchdog: UI_age_ms=%u UI_phase=%d input_API_age_ms=%u",age,(int)InterlockedCompareExchange(&ui_phase,0,0),call_age);stalled=true;}else if(stalled){app_log("Responsiveness watchdog: recovered");stalled=false;}
  }
  PostMessageW(g_main,WM_APP_STREAM_INACTIVE,job_generation,0);input_session_end();LiStopConnection();
 }
 InterlockedExchange(&core_entered,0);return rc==0&&sender_ok&&(job!=JOB_STREAM_TEST||(video_has_frames(video)&&audio_has_packets(audio)));
}
static void load_artwork(){
 for(int i=0;i<host_app_count(host)&&i<12&&!g_stop_requested;i++){
  Buffer bytes;if(host_app_art(host,host_app_id(host,i),bytes)){HBITMAP bitmap=decode_artwork(bytes);if(bitmap&&!PostMessageW(g_main,WM_APP_ART,(WPARAM)i,(LPARAM)bitmap))DeleteObject(bitmap);}
 }
}
static DWORD WINAPI cancel_monitor(void*){
 HANDLE events[]={job_done_event,cancel_event};if(WaitForMultipleObjects(2,events,FALSE,INFINITE)!=WAIT_OBJECT_0+1)return 0;
 do{host_cancel(host);if(InterlockedCompareExchange(&core_entered,0,0))LiInterruptConnection();}while(WaitForSingleObject(job_done_event,50)==WAIT_TIMEOUT);return 0;
}
static DWORD WINAPI run_job(void*){
 HANDLE canceller=CreateThread(nullptr,0,cancel_monitor,nullptr,0,nullptr);bool ok=false;MMRESULT timer=timeBeginPeriod(1);HRESULT co=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
 auto_pair_needed=false;host_online=false;
 if(job==JOB_TEST){app_status(L"本地测试：三段 H.264 画面及 Opus 编解码，不连接主机。");ok=audio_selftest()&&host_crypto_selftest()&&video_local_test(video,test_folder);}
 else if(job==JOB_SMOKE)ok=host_crypto_selftest()&&audio_selftest();
 else if(host_set_address(host,target,target_port)){
  if(job==JOB_PAIR||job==JOB_PAIR_TEST){ok=host_pair(host,pin_code);host_online=ok;if(ok){ok=host_app_list(host);PostMessageW(g_main,WM_APP_APPS,0,0);if(ok&&!automatic)load_artwork();}}
  else if(job==JOB_REFRESH){host_online=host_query(host,false);ok=host_online;if(ok){if(!host_has_pin(host)){auto_pair_needed=true;}else{ok=host_app_list(host);if(ok){PostMessageW(g_main,WM_APP_APPS,0,0);if(!automatic)load_artwork();}}}}
  else if(job==JOB_START)ok=start_stream();
  else if(job==JOB_STREAM_TEST){
   if(host_app_list(host)){selected_app=-1;for(int i=0;i<host_app_count(host);i++)if(!_stricmp(host_app_name(host,i),"Desktop")||!strcmp(host_app_name(host,i),"桌面"))selected_app=host_app_id(host,i);
    if(selected_app>=0)ok=start_stream();else app_log("No Desktop app found; development test will not launch another app.");}
  }
 }
 SetEvent(job_done_event);if(canceller){WaitForSingleObject(canceller,INFINITE);CloseHandle(canceller);}
 if(SUCCEEDED(co))CoUninitialize();if(timer==TIMERR_NOERROR)timeEndPeriod(1);
 app_log("Operation complete: job=%d success=%d cancelled=%d",job,ok,g_stop_requested!=0);
 if(g_stop_requested)app_status(L"已停止。主机上的应用保持运行，可以稍后重新连接。");
 else if(!ok)app_status(L"操作未完成，可在“设置 → 帮助与诊断”查看原因。");
 else if(job==JOB_TEST||job==JOB_SMOKE)app_status(L"本地自检完成，结果已写入日志。");
 else if(job==JOB_PAIR||job==JOB_PAIR_TEST||(job==JOB_REFRESH&&host_is_paired(host)))app_status(L"已连接电脑，点击应用即可开始串流。");
 else if(job==JOB_REFRESH)app_status(L"已找到电脑，正在准备配对。");
 else app_status(L"串流已结束。可以重新选择应用并连接。");
 exit_code=ok?0:1;PostMessageW(g_main,WM_APP_FINISHED,ok?1:0,0);return 0;
}
static void toggle_fullscreen();
static void begin_job(int action){
 if(worker)return;bool needs_network=action!=JOB_TEST&&action!=JOB_SMOKE;
 if(needs_network&&!ui_target(target,sizeof(target),&target_port)){app_status(L"请先添加电脑，再开始连接。");return;}
 if(action==JOB_START){selected_app=ui_selected_app();if(selected_app<0){app_status(L"请先选择一个应用。");return;}}
 if(action==JOB_PAIR){unsigned random=0;if(!random_bytes(&random,sizeof(random)))return;snprintf(pin_code,sizeof(pin_code),"%04u",random%10000);ui_pairing(pin_code);}
 job=action;job_generation++;finish_pending=false;ResetEvent(cancel_event);ResetEvent(job_done_event);ResetEvent(stop_event);InterlockedExchange(&g_stop_requested,0);controls(true);
 if(action==JOB_START||action==JOB_STREAM_TEST||action==JOB_TEST){ui_stream(true);if(!automatic&&action==JOB_START&&ui_preferences().fullscreen&&!full_screen)toggle_fullscreen();}
 worker=CreateThread(nullptr,0,run_job,nullptr,0,nullptr);if(!worker){controls(false);ui_stream(false);app_status(L"无法开始连接，请重试。");}
}
static void toggle_fullscreen(){
 mouse_suspend();
 if(!full_screen){GetWindowRect(g_main,&normal_rect);normal_style=(DWORD)GetWindowLongPtrW(g_main,GWL_STYLE);MONITORINFO mi={};mi.cbSize=sizeof(mi);GetMonitorInfoW(MonitorFromWindow(g_main,MONITOR_DEFAULTTONEAREST),&mi);
  SetWindowLongPtrW(g_main,GWL_STYLE,WS_POPUP|WS_VISIBLE);SetWindowPos(g_main,HWND_TOP,mi.rcMonitor.left,mi.rcMonitor.top,mi.rcMonitor.right-mi.rcMonitor.left,mi.rcMonitor.bottom-mi.rcMonitor.top,SWP_FRAMECHANGED|SWP_SHOWWINDOW);full_screen=true;
 }else{SetWindowLongPtrW(g_main,GWL_STYLE,normal_style);SetWindowPos(g_main,nullptr,normal_rect.left,normal_rect.top,normal_rect.right-normal_rect.left,normal_rect.bottom-normal_rect.top,SWP_FRAMECHANGED|SWP_NOZORDER|SWP_SHOWWINDOW);full_screen=false;}
 ui_fullscreen(full_screen);layout();
}
static void ui_action(int action){switch(action){case UI_CONNECT:begin_job(JOB_REFRESH);break;case UI_PAIR:begin_job(JOB_PAIR);break;case UI_START:begin_job(JOB_START);break;case UI_STOP:request_stop();break;case UI_SELFTEST:begin_job(JOB_TEST);break;case UI_FULLSCREEN:toggle_fullscreen();break;case UI_GAME_MOUSE:release_input();ui_game_mouse(mouse_toggle_game());break;case UI_LOG_FOLDER:ShellExecuteW(g_main,L"open",g_profile,nullptr,nullptr,SW_SHOWNORMAL);break;}}
static LRESULT CALLBACK window_proc(HWND h,UINT message,WPARAM w,LPARAM l){
 if(message==WM_GETMINMAXINFO){MINMAXINFO*info=(MINMAXINFO*)l;info->ptMinTrackSize.x=MulDiv(850,window_dpi,96);info->ptMinTrackSize.y=MulDiv(640,window_dpi,96);return 0;}
 if(message==WM_SETFOCUS&&g_streaming){SetFocus(g_video);return 0;}
 if(message==WM_SIZE&&g_video){layout();return 0;}
 if(message==WM_APP_STATUS){ui_status((const WCHAR*)l);free((void*)l);return 0;}
 if(message==WM_APP_LOG)return 0;
 if(message==WM_APP_APPS){ui_apps(host);return 0;}
 if(message==WM_APP_ART){ui_art((int)w,(HBITMAP)l);return 0;}
 if(message==WM_APP_STREAM_ACTIVE){if((unsigned)w==job_generation&&!g_stop_requested&&input_session_active()){release_input();InterlockedExchange(&g_streaming,1);SetFocus(g_video);mouse_begin(ui_preferences().game_mouse);ui_game_mouse(mouse_is_game());}return 0;}
 if(message==WM_APP_STREAM_INACTIVE){if((unsigned)w==job_generation){release_input();mouse_end();InterlockedExchange(&g_streaming,0);}return 0;}
 if(message==WM_APP_FINISHED){finish_pending=true;finish_ok=w!=0;return 0;}
 if(message==WM_TIMER){
  InterlockedExchange(&ui_tick,(LONG)GetTickCount());
  if(closing&&worker&&GetTickCount()-close_started>5000){mouse_end();DestroyWindow(h);ExitProcess(ERROR_TIMEOUT);}
  if(ui_log_visible()){static WCHAR text[120000];if(log_snapshot(text,120000))ui_log(text);}
  if(finish_pending&&worker&&WaitForSingleObject(worker,0)==WAIT_OBJECT_0){
   CloseHandle(worker);worker=nullptr;finish_pending=false;release_input();mouse_end();InterlockedExchange(&g_streaming,0);bool cancelled=g_stop_requested!=0;
   if((job==JOB_START||job==JOB_TEST||job==JOB_STREAM_TEST)&&full_screen)toggle_fullscreen();
   if(job==JOB_REFRESH||job==JOB_PAIR)ui_host_result(host,host_online);ui_finished(job,finish_ok,host_is_paired(host),cancelled);
   if(automatic||closing)DestroyWindow(h);else if(auto_pair_needed&&finish_ok&&!cancelled)begin_job(JOB_PAIR);
  }return 0;
 }
 if(message==WM_ACTIVATEAPP&&!w){release_input();mouse_suspend();}
 if(message==WM_APP_STOP){request_stop();return 0;}
 if(message==WM_CLOSE){if(worker){if(!closing)close_started=GetTickCount();closing=true;request_stop();}else DestroyWindow(h);return 0;}
 if(message==WM_DESTROY){PostQuitMessage(0);return 0;}
 LRESULT result=0;if(ui_message(h,message,w,l,&result))return result;return DefWindowProcW(h,message,w,l);
}
static LONG WINAPI unhandled(EXCEPTION_POINTERS*e){app_log("FATAL exception=0x%08x",e->ExceptionRecord->ExceptionCode);return EXCEPTION_EXECUTE_HANDLER;}
extern "C" void entry(){
 int argc=0;WCHAR**argv=CommandLineToArgvW(GetCommandLineW(),&argc);const WCHAR*profile=nullptr;int auto_job=0;WCHAR auto_host[256]={},auto_port[20]={};
 for(int i=1;i<argc;i++){
  if(!lstrcmpiW(argv[i],L"--profile")&&i+1<argc)profile=argv[++i];
  else if(!lstrcmpiW(argv[i],L"--self-test")&&i+1<argc){lstrcpynW(test_folder,argv[++i],MAX_PATH);automatic=true;auto_job=JOB_TEST;}
  else if(!lstrcmpiW(argv[i],L"--smoke")){automatic=true;auto_job=JOB_SMOKE;}
  else if(!lstrcmpiW(argv[i],L"--input-test")){automatic=true;input_test_mode=true;}
  else if(!lstrcmpiW(argv[i],L"--pair-test")&&i+3<argc){lstrcpynW(auto_host,argv[++i],256);lstrcpynW(auto_port,argv[++i],20);wide_to_utf8(argv[++i],pin_code,sizeof(pin_code));automatic=true;auto_job=JOB_PAIR_TEST;}
  else if(!lstrcmpiW(argv[i],L"--stream-test")&&i+3<argc){lstrcpynW(auto_host,argv[++i],256);lstrcpynW(auto_port,argv[++i],20);unsigned seconds=(unsigned)_wtoi(argv[++i]);if(seconds>0&&seconds<=60)test_duration=seconds*1000;automatic=true;auto_job=JOB_STREAM_TEST;}
  else if(!lstrcmpiW(argv[i],L"--visible"))auto_visible=true;
  else if(!lstrcmpiW(argv[i],L"--ui-preview"))preview_mode=true;
 }
 runtime_init(profile);SetUnhandledExceptionFilter(unhandled);
 HANDLE instance_mutex=nullptr;
 if(!automatic&&!preview_mode){unsigned hash=2166136261u;for(int i=0;g_profile[i];i++)hash=(hash^(unsigned)g_profile[i])*16777619u;WCHAR mutex_name[80];wsprintfW(mutex_name,L"Local\\MoonlightRT81-%08x",hash);instance_mutex=CreateMutexW(nullptr,TRUE,mutex_name);if(instance_mutex&&GetLastError()==ERROR_ALREADY_EXISTS){HWND existing=FindWindowW(L"MoonlightRT81Native",nullptr);if(existing){ShowWindow(existing,SW_RESTORE);SetForegroundWindow(existing);}CloseHandle(instance_mutex);runtime_close();ExitProcess(0);}}
 CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
 if(!*test_folder){lstrcpynW(test_folder,g_exe_dir,MAX_PATH);if(lstrlenW(test_folder)<MAX_PATH-8)lstrcatW(test_folder,L"clips");}
 profile_path(L"settings.ini",settings_path);HINSTANCE instance=GetModuleHandleW(nullptr);WNDCLASSW c={};c.lpfnWndProc=window_proc;c.hInstance=instance;c.hCursor=LoadCursorW(nullptr,IDC_ARROW);c.hbrBackground=(HBRUSH)GetStockObject(BLACK_BRUSH);c.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(1));c.lpszClassName=L"MoonlightRT81Native";RegisterClassW(&c);
 WNDCLASSW video_class={};video_class.lpfnWndProc=video_proc;video_class.hInstance=instance;video_class.hCursor=LoadCursorW(nullptr,IDC_ARROW);video_class.hbrBackground=(HBRUSH)GetStockObject(BLACK_BRUSH);video_class.lpszClassName=L"MoonlightRT81VideoInput";RegisterClassW(&video_class);
 HDC display_dc=GetDC(nullptr);window_dpi=GetDeviceCaps(display_dc,LOGPIXELSX);ReleaseDC(nullptr,display_dc);if(window_dpi<96||window_dpi>288)window_dpi=96;
 RECT work_area={};if(!SystemParametersInfoW(SPI_GETWORKAREA,0,&work_area,0)){work_area.right=GetSystemMetrics(SM_CXSCREEN);work_area.bottom=GetSystemMetrics(SM_CYSCREEN);}int initial_width=work_area.right-work_area.left-48,initial_height=work_area.bottom-work_area.top-40;int fit_dpi=initial_height*96/640;if(window_dpi>fit_dpi&&fit_dpi>=96)window_dpi=fit_dpi;fit_dpi=initial_width*96/850;if(window_dpi>fit_dpi&&fit_dpi>=96)window_dpi=fit_dpi;int desired_w=MulDiv(1080,window_dpi,96),desired_h=MulDiv(760,window_dpi,96);if(initial_width>desired_w)initial_width=desired_w;if(initial_height>desired_h)initial_height=desired_h;
 g_main=CreateWindowExW(0,c.lpszClassName,L"Moonlight RT",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,work_area.left+(work_area.right-work_area.left-initial_width)/2,work_area.top+(work_area.bottom-work_area.top-initial_height)/2,initial_width,initial_height,nullptr,nullptr,instance,nullptr);
 g_video=CreateWindowExW(0,video_class.lpszClassName,L"",WS_CHILD|WS_TABSTOP|WS_CLIPCHILDREN|WS_CLIPSIBLINGS,12,150,640,360,g_main,nullptr,instance,nullptr);
 host=host_create();video=video_create(g_video);audio=audio_create();stop_event=CreateEventW(nullptr,TRUE,FALSE,nullptr);cancel_event=CreateEventW(nullptr,TRUE,FALSE,nullptr);job_done_event=CreateEventW(nullptr,TRUE,FALSE,nullptr);mouse_init(g_video);ui_init(g_main,instance,settings_path,ui_action,window_dpi);
 SetTimer(g_main,1,250,nullptr);InterlockedExchange(&ui_tick,(LONG)GetTickCount());
 if(*auto_host)ui_override_target(auto_host,auto_port);if(preview_mode)ui_preview();
 ShowWindow(g_main,automatic&&!auto_visible?SW_HIDE:SW_SHOWNORMAL);if(auto_visible)ShowWindow(g_main,SW_SHOWNORMAL);UpdateWindow(g_main);
 app_log("Log saved in per-user profile; version=0.2.0");
 if(auto_job)begin_job(auto_job);if(argv)LocalFree(argv);
 if(input_test_mode){ui_stream(true);exit_code=input_routing_selftest()?0:1;DestroyWindow(g_main);}
 MSG message;while(GetMessageW(&message,nullptr,0,0)>0){InterlockedExchange(&ui_tick,(LONG)GetTickCount());InterlockedExchange(&ui_phase,(LONG)message.message);mouse_prepare_message(message);if(!g_streaming&&message.message==WM_KEYDOWN&&message.wParam==VK_ESCAPE){SendMessageW(g_main,WM_KEYDOWN,VK_ESCAPE,0);continue;}if(!g_streaming&&IsDialogMessageW(g_main,&message))continue;TranslateMessage(&message);DispatchMessageW(&message);}
 // Do not destroy shared crypto/renderer objects while a cancelled worker is still unwinding.
 if(!worker){ui_destroy();video_destroy(video);audio_destroy(audio);host_destroy(host);if(stop_event)CloseHandle(stop_event);if(cancel_event)CloseHandle(cancel_event);if(job_done_event)CloseHandle(job_done_event);CoUninitialize();runtime_close();}
 if(instance_mutex){ReleaseMutex(instance_mutex);CloseHandle(instance_mutex);}ExitProcess(exit_code);
}
