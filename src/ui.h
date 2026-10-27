#pragma once
#include "app.h"

enum UiAction { UI_CONNECT=1,UI_PAIR,UI_START,UI_STOP,UI_SELFTEST,UI_FULLSCREEN,UI_LOG_FOLDER,UI_GAME_MOUSE };
struct UiPreferences { int width=1280,height=720,fps=30,bitrate=5000;bool fullscreen=true,sound=true,game_mouse=false; };
typedef void (*UiActionHandler)(int);
bool ui_init(HWND window,HINSTANCE instance,const WCHAR*settings,UiActionHandler action,int display_dpi);
void ui_destroy();
bool ui_message(HWND window,UINT message,WPARAM w,LPARAM l,LRESULT*result);
void ui_layout();
void ui_busy(bool busy);
void ui_status(const WCHAR*text);
void ui_pairing(const char*pin);
void ui_apps(HostState*host);
void ui_host_result(HostState*host,bool online);
void ui_finished(int action,bool ok,bool paired,bool cancelled);
void ui_stream(bool active);
void ui_fullscreen(bool active);
void ui_log(const WCHAR*text);
bool ui_log_visible();void ui_game_mouse(bool enabled);
void ui_override_target(const WCHAR*address,const WCHAR*port);
bool ui_target(char*address,int capacity,unsigned*port);
int ui_selected_app();
UiPreferences ui_preferences();
HWND ui_focus_control();
void ui_art(int index,HBITMAP bitmap);
void ui_preview();
