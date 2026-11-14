#ifndef A4_AUDIO_RING_H
#define A4_AUDIO_RING_H
#include <atomic>
#include <cstdint>
#include <cstddef>
// One producer (retro_run), one consumer (SDL audio callback). Release
// publishes complete stereo samples; acquire prevents reuse before consumption.
// No locks or allocation occur in either audio path.
template<std::uint32_t Frames, std::uint32_t MaxQueued = Frames-1>
class A4AudioRing {
  static_assert(Frames>1 && (Frames&(Frames-1))==0,"power-of-two ring required");
  static_assert(MaxQueued>0 && MaxQueued<Frames,"one slot must remain empty");
  static_assert(std::atomic<std::uint32_t>::is_always_lock_free,"audio indices must be lock-free");
public:
  bool push(std::int16_t left,std::int16_t right) {
    const auto write=write_.load(std::memory_order_relaxed);
    const auto read=read_.load(std::memory_order_acquire);
    if(((write-read)&mask)>=MaxQueued) return false;
    samples_[write*2]=left;samples_[write*2+1]=right;
    write_.store((write+1)&mask,std::memory_order_release);return true;
  }
  bool pop(std::int16_t& left,std::int16_t& right) {
    const auto read=read_.load(std::memory_order_relaxed);
    if(read==write_.load(std::memory_order_acquire)) return false;
    left=samples_[read*2];right=samples_[read*2+1];
    read_.store((read+1)&mask,std::memory_order_release);return true;
  }
private:
  static constexpr std::uint32_t mask=Frames-1;
  std::int16_t samples_[Frames*2]{};
  std::atomic<std::uint32_t> write_{0},read_{0};
};
#endif
