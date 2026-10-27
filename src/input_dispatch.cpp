#include "app.h"
#include "input_queue.h"

namespace {
SRWLOCK queue_lock=SRWLOCK_INIT;InputQueue queue;
HANDLE thread=nullptr,wake=nullptr;bool accepting=false,stopping=false,reset_pending=false;
volatile LONG reset_after_contention=0;
volatile LONG session_ready=0;
bool keys[256];char key_flags[256];unsigned buttons=0;
unsigned sent=0,errors=0,resets=0;volatile LONG dropped=0,in_call=0;DWORD call_started=0,max_call_ms=0;
int timed_send(const InputEvent&e){
 call_started=GetTickCount();InterlockedExchange(&in_call,1);int rc=-1;
 if(e.kind==INPUT_POSITION)rc=LiSendMousePositionEvent((short)e.x,(short)e.y,(short)e.width,(short)e.height);
 else if(e.kind==INPUT_RELATIVE){int x=e.x,y=e.y;rc=0;while(x||y){int dx=x< -32768?-32768:x>32767?32767:x,dy=y< -32768?-32768:y>32767?32767:y;rc=LiSendMouseMoveEvent((short)dx,(short)dy);if(rc)break;x-=dx;y-=dy;}}
 else if(e.kind==INPUT_BUTTON){rc=LiSendMouseButtonEvent(e.action,e.x);if(!rc){unsigned mask=1u<<(e.x-1);if(e.action==BUTTON_ACTION_PRESS)buttons|=mask;else buttons&=~mask;}}
 else if(e.kind==INPUT_KEY){rc=LiSendKeyboardEvent2((short)e.x,e.action,e.flags,0);if(!rc&&e.x>=0&&e.x<256){keys[e.x]=e.action==KEY_ACTION_DOWN;key_flags[e.x]=e.flags;}}
 else if(e.kind==INPUT_SCROLL)rc=LiSendHighResScrollEvent((short)e.x);
 DWORD elapsed=GetTickCount()-call_started;if(elapsed>max_call_ms)max_call_ms=elapsed;InterlockedExchange(&in_call,0);sent++;if(rc)errors++;return rc;
}
void release_remote(){
 for(int i=0;i<256;i++)if(keys[i]){InputEvent e;e.kind=INPUT_KEY;e.x=i;e.action=KEY_ACTION_UP;e.flags=key_flags[i]&MODIFIER_EXTENDED;timed_send(e);keys[i]=false;}
 for(int i=1;i<=5;i++)if(buttons&(1u<<(i-1))){InputEvent e;e.kind=INPUT_BUTTON;e.x=i;e.action=BUTTON_ACTION_RELEASE;timed_send(e);}buttons=0;resets++;
}
DWORD WINAPI sender(void*){
 for(;;){WaitForSingleObject(wake,10);
  for(;;){InputEvent e;AcquireSRWLockExclusive(&queue_lock);
   if(InterlockedExchange(&reset_after_contention,0)){queue.clear();reset_pending=true;}
   bool reset=reset_pending,stop=stopping;reset_pending=false;bool have=!reset&&!stop&&queue.pop(e);ReleaseSRWLockExclusive(&queue_lock);
   if(reset)release_remote();if(stop)return 0;if(reset)continue;if(!have)break;timed_send(e);
  }
 }
}
}
bool input_session_begin(){
 if(thread)return false;wake=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!wake)return false;
 AcquireSRWLockExclusive(&queue_lock);queue.clear();memset(keys,0,sizeof(keys));memset(key_flags,0,sizeof(key_flags));buttons=0;sent=errors=resets=0;dropped=in_call=reset_after_contention=0;max_call_ms=0;reset_pending=stopping=false;accepting=true;ReleaseSRWLockExclusive(&queue_lock);
 thread=CreateThread(nullptr,0,sender,nullptr,0,nullptr);if(!thread){AcquireSRWLockExclusive(&queue_lock);accepting=false;ReleaseSRWLockExclusive(&queue_lock);CloseHandle(wake);wake=nullptr;return false;}InterlockedExchange(&session_ready,1);return true;
}
int input_enqueue(const InputEvent&e){
 // This lock protects bounded memory operations only. The consumer always
 // releases it before calling the protocol, logging or doing any I/O. Do not
 // drop key transitions just because a mouse event overlaps this short lock.
 AcquireSRWLockExclusive(&queue_lock);
 if(!accepting){ReleaseSRWLockExclusive(&queue_lock);return -2;}
 bool ok=queue.push(e);if(!ok){queue.clear();reset_pending=true;InterlockedIncrement(&dropped);}SetEvent(wake);ReleaseSRWLockExclusive(&queue_lock);return ok?0:-1;
}
void input_release_all(){
 if(!TryAcquireSRWLockExclusive(&queue_lock)){InterlockedExchange(&reset_after_contention,1);return;}
 if(accepting){queue.clear();reset_pending=true;SetEvent(wake);}ReleaseSRWLockExclusive(&queue_lock);
}
void input_session_end(){
 InterlockedExchange(&session_ready,0);if(!thread)return;AcquireSRWLockExclusive(&queue_lock);accepting=false;stopping=reset_pending=true;queue.clear();SetEvent(wake);ReleaseSRWLockExclusive(&queue_lock);
 // Called only by the stream worker, while the library input stream still exists.
 app_log("Input dispatcher stopping; current API age_ms=%u",input_call_age());
 WaitForSingleObject(thread,INFINITE);CloseHandle(thread);thread=nullptr;CloseHandle(wake);wake=nullptr;
 app_log("Input dispatcher: sent=%u errors=%u resets=%u dropped=%d max_api_ms=%u",sent,errors,resets,(int)dropped,max_call_ms);
}
DWORD input_call_age(){return InterlockedCompareExchange(&in_call,0,0)?GetTickCount()-call_started:0;}
bool input_session_active(){return InterlockedCompareExchange(&session_ready,0,0)!=0;}
