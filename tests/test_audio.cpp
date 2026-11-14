#include "audio_ring.h"
#include "runner_policy.h"
#include <cassert>
#include <thread>
#include <atomic>
#include <iostream>
int main() {
  A4AudioRing<8,5> small;
  int16_t l=99,r=99;assert(!small.pop(l,r)&&l==99&&r==99);
  for(int round=0;round<1000;++round) {
    for(int i=0;i<5;++i)assert(small.push(static_cast<int16_t>(i),static_cast<int16_t>(-i)));
    assert(!small.push(999,999));
    for(int i=0;i<5;++i){assert(small.pop(l,r));assert(l==i&&r==-i);}
    assert(!small.pop(l,r));
  }
  A4AudioRing<32768,24000> ring;
  constexpr uint32_t count=1000000;
  std::atomic<bool> done{false};
  std::thread producer([&]{
    for(uint32_t i=0;i<count;++i) {
      while(!ring.push(static_cast<int16_t>(i&0xffff),static_cast<int16_t>(i>>16)))std::this_thread::yield();
    }
    done.store(true,std::memory_order_release);
  });
  uint32_t consumed=0;
  while(consumed<count) {
    if(ring.pop(l,r)) {
      const uint32_t got=uint16_t(l)|(uint32_t(uint16_t(r))<<16);
      assert(got==consumed);++consumed;
    } else std::this_thread::yield();
  }
  producer.join();assert(done.load(std::memory_order_acquire)&&!ring.pop(l,r));
  assert(strcmp(a4_runner_save_dir("atari800","800XL (64K)",A4_SAVES_5200),A4_SAVES_8BIT)==0);
  assert(strcmp(a4_runner_save_dir("atari800","5200",A4_SAVES_5200),A4_SAVES_5200)==0);
  assert(strcmp(a4_runner_save_dir("stella","800XL (64K)",A4_SAVES_2600),A4_SAVES_2600)==0);
  assert(strcmp(a4_runner_save_dir("atari800","",A4_SAVES_5200),A4_SAVES_5200)==0);
  std::cout<<"1,000,000 concurrent stereo frames in exact order; 1,000 full/wrap/empty cycles; save-folder isolation passed\n";
}
