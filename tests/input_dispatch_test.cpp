// Headless deterministic test. No windows, raw devices, real input or network.
#include "app.h"
#include <stdio.h>
#include <stdarg.h>

static HANDLE entered,gate;static volatile LONG block_next=0,calls=0,a_down=0,b_down=0;static volatile LONG64 rel_x=0,rel_y=0;
void app_log(const char*,...){}
extern "C" int LiSendMousePositionEvent(short,short,short,short){InterlockedIncrement(&calls);return 0;}
extern "C" int LiSendMouseMoveEvent(short x,short y){InterlockedExchangeAdd64(&rel_x,x);InterlockedExchangeAdd64(&rel_y,y);InterlockedIncrement(&calls);return 0;}
extern "C" int LiSendMouseButtonEvent(char,int){InterlockedIncrement(&calls);return 0;}
extern "C" int LiSendHighResScrollEvent(short){InterlockedIncrement(&calls);return 0;}
extern "C" int LiSendKeyboardEvent2(short key,char action,char,char){
 if(InterlockedExchange(&block_next,0)){SetEvent(entered);WaitForSingleObject(gate,INFINITE);}
 if(key=='A')InterlockedExchange(&a_down,action==KEY_ACTION_DOWN);if(key=='B')InterlockedExchange(&b_down,action==KEY_ACTION_DOWN);InterlockedIncrement(&calls);return 0;
}
static InputEvent key(int value,bool down){InputEvent e;e.kind=INPUT_KEY;e.x=value;e.action=down?KEY_ACTION_DOWN:KEY_ACTION_UP;return e;}
#define CHECK(expr) do{if(!(expr)){printf("FAIL line %d: %s\n",__LINE__,#expr);return 1;}}while(0)
static bool wait_value(volatile LONG*value,LONG expected){DWORD start=GetTickCount();while(InterlockedCompareExchange(value,0,0)!=expected){if(GetTickCount()-start>2000)return false;Sleep(1);}return true;}
int main(){
 InputQueue q;InputEvent e;e.kind=INPUT_POSITION;e.x=1;CHECK(q.push(e));e.x=2;CHECK(q.push(e));CHECK(q.size()==1);CHECK(q.pop(e)&&e.x==2);
 e.kind=INPUT_RELATIVE;e.x=100;e.y=-50;CHECK(q.push(e));CHECK(q.push(e));CHECK(q.pop(e)&&e.x==200&&e.y==-100);
 e.kind=INPUT_POSITION;e.x=7;CHECK(q.push(e));CHECK(q.push(key('A',true)));e.x=8;CHECK(q.push(e));CHECK(q.size()==3);CHECK(q.pop(e)&&e.kind==INPUT_POSITION&&e.x==7);CHECK(q.pop(e)&&e.kind==INPUT_KEY);CHECK(q.pop(e)&&e.x==8);
 for(int i=0;i<512;i++)CHECK(q.push(key('B',true)));CHECK(!q.push(key('B',false)));q.clear();CHECK(!q.pop(e));
 CHECK(input_enqueue(key('A',true))==-2);CHECK(calls==0);
 entered=CreateEventW(nullptr,TRUE,FALSE,nullptr);gate=CreateEventW(nullptr,TRUE,FALSE,nullptr);CHECK(entered&&gate);CHECK(input_session_begin());InterlockedExchange(&block_next,1);CHECK(input_enqueue(key('A',true))==0);CHECK(WaitForSingleObject(entered,2000)==WAIT_OBJECT_0);
 DWORD start=GetTickCount();for(int i=0;i<10000;i++){e.kind=INPUT_POSITION;e.x=i%1280;e.y=i%720;e.width=1280;e.height=720;CHECK(input_enqueue(e)==0);}DWORD enqueue_ms=GetTickCount()-start;CHECK(enqueue_ms<500);
 // Focus loss flushes the stale motion backlog and releases the old held key.
 input_release_all();CHECK(input_enqueue(key('B',true))==0);CHECK(input_enqueue(key('B',false))==0);SetEvent(gate);CHECK(wait_value(&calls,4));CHECK(!a_down&&!b_down);
 e.kind=INPUT_RELATIVE;e.x=100000;e.y=-50000;CHECK(input_enqueue(e)==0);start=GetTickCount();while(rel_x!=100000||rel_y!=-50000){CHECK(GetTickCount()-start<2000);Sleep(1);}input_session_end();CHECK(input_enqueue(key('A',true))==-2);
 for(int round=0;round<50;round++){CHECK(input_session_begin());CHECK(input_enqueue(key('B',true))==0);CHECK(wait_value(&b_down,1));input_session_end();CHECK(!a_down&&!b_down);}
 // Overflow must reset remote state, rather than strand a key-down event.
 ResetEvent(entered);ResetEvent(gate);CHECK(input_session_begin());InterlockedExchange(&block_next,1);CHECK(input_enqueue(key('A',true))==0);CHECK(WaitForSingleObject(entered,2000)==WAIT_OBJECT_0);
 for(int i=0;i<512;i++)CHECK(input_enqueue(key('B',true))==0);CHECK(input_enqueue(key('B',false))==-1);CHECK(input_enqueue(key('B',true))==0);CHECK(input_enqueue(key('B',false))==0);LONG before=calls;SetEvent(gate);start=GetTickCount();while(calls<before+4){CHECK(GetTickCount()-start<2000);Sleep(1);}CHECK(!a_down&&!b_down);input_session_end();
 CloseHandle(entered);CloseHandle(gate);printf("PASS: queue ordering/coalescing/overflow; 10000 enqueues during stalled transport in %u ms; 50 reconnects; stale-input reset; relative delta totals. No GUI/input/network APIs used.\n",(unsigned)enqueue_ms);return 0;
}
