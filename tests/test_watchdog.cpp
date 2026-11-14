#include "SDL.h"
#include "common/watchdog.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <thread>
#include <iostream>
struct SDL_Thread {std::thread thread;int result=0;};
static std::atomic<uint32_t> ticks{1000},clock_reads{0};
static std::atomic<int> exits{0},live{0};
uint32_t SDL_GetTicks(){clock_reads.fetch_add(1);return ticks.load();}
void SDL_Delay(uint32_t){std::this_thread::sleep_for(std::chrono::milliseconds(1));}
SDL_Thread* SDL_CreateThread(int (*fn)(void*),const char*,void* arg){
 auto* t=new SDL_Thread;t->thread=std::thread([=]{++live;t->result=fn(arg);--live;});return t;}
void SDL_WaitThread(SDL_Thread* t,int* out){t->thread.join();if(out)*out=t->result;delete t;}
void sysProcessExit(int){++exits;}
static void wait_exit(){for(int i=0;i<500&&exits.load()==0;++i)std::this_thread::sleep_for(std::chrono::milliseconds(1));assert(exits.load()>0);}
int main(){
 for(int run=0;run<3;++run){
  ticks.store(1000);exits.store(0);a4_watchdog_start("test");a4_watchdog_phase(A4_PHASE_IDLE);
  for(int i=0;i<200;++i){ticks.fetch_add(16);a4_watchdog_tick();}
  a4_watchdog_stop();assert(live.load()==0&&exits.load()==0);
  const auto reads=clock_reads.load();std::this_thread::sleep_for(std::chrono::milliseconds(5));assert(clock_reads.load()==reads);
 }
 ticks.store(1000);exits.store(0);a4_watchdog_start("present");a4_watchdog_phase(A4_PHASE_PRESENT);ticks.store(9001);wait_exit();a4_watchdog_stop();assert(live.load()==0);
 ticks.store(1000);exits.store(0);a4_watchdog_start("load");a4_watchdog_phase(A4_PHASE_LOAD);ticks.store(10000);
 std::this_thread::sleep_for(std::chrono::milliseconds(10));assert(exits.load()==0);
 ticks.store(31001);wait_exit();a4_watchdog_stop();assert(live.load()==0);
 std::cout<<"watchdog: heartbeat/restart/shutdown join, PRESENT 8s and other-phase 30s budgets passed\n";
}
