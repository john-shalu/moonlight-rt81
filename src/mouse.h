#pragma once
#include "app.h"
void mouse_init(HWND video);
void mouse_begin(bool game_mode);
void mouse_end();
void mouse_suspend();
bool mouse_toggle_game();
bool mouse_is_game();
void mouse_prepare_message(const MSG&message);
bool mouse_message(HWND window,UINT message,WPARAM w,LPARAM l,LRESULT*result);
