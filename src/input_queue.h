#pragma once
#include <stdint.h>
enum InputKind{INPUT_POSITION=1,INPUT_RELATIVE,INPUT_BUTTON,INPUT_KEY,INPUT_SCROLL};
struct InputEvent{int kind=0,x=0,y=0,width=0,height=0;char action=0,flags=0;};
// Passive bounded queue. Synchronization belongs to the dispatcher, never the transport.
class InputQueue {
 static constexpr unsigned CAPACITY=512;
 InputEvent entries[CAPACITY];unsigned first=0,count=0;
 static int bounded_sum(int a,int b){int64_t n=(int64_t)a+b;return n>1048576?1048576:n< -1048576?-1048576:(int)n;}
public:
 void clear(){first=count=0;}
 bool push(const InputEvent&e){
  if(count&&(e.kind==INPUT_POSITION||e.kind==INPUT_RELATIVE)){
   InputEvent&last=entries[(first+count-1)%CAPACITY];
   if(last.kind==e.kind){if(e.kind==INPUT_POSITION)last=e;else{last.x=bounded_sum(last.x,e.x);last.y=bounded_sum(last.y,e.y);}return true;}
  }
  if(count==CAPACITY)return false;entries[(first+count)%CAPACITY]=e;count++;return true;
 }
 bool pop(InputEvent&e){if(!count)return false;e=entries[first];first=(first+1)%CAPACITY;count--;return true;}
 unsigned size()const{return count;}
};
