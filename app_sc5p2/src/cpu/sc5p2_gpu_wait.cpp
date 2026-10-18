// Space Channel 5 Part 2: the game's Direct3D GPU wait sleeps instead of spinning (CPU enhancement
// sa2_cpu_gpu_wait_sleep). sa2-nx's app/src/cpu/sa2_gpu_wait.cpp with Space Channel 5 Part 2's address: the iteration
// function is sub_821CEB30 (tools/d3d_match.py against SA2's sub_8254F400, 0.93; tools/listing_diff.py: the same
// device fields, read pointer word at +11024 and state byte at +11069, and the hang limit as an immediate 5000 where
// SA2 loads it from a global). The wait record comes from sub_821CEA38 (SA2's sub_8254F308, 1.00). SA2's notes below
// (its call sites and measurements are SA2's).
//
// WHAT THE GAME DOES
//   Direct3D's waits for the GPU (ring space: sub_821D1050; fences and the others, 7 call sites) all loop on one
//   iteration function, sub_821CEB30(r3 = the wait record sub_821CEA38 filled: [0] device, [8] the read pointer seen
//   last time, [12] the tick of its last change, [20] the time base at the start). One iteration is 32 db16cyc delays,
//   then: GPU already declared hung (bit 0x02 of [device+11069]) -> 0 (stop); the read pointer ([[device+11024]])
//   moved -> remember it and its tick; no movement for longer than the game's limit -> the hang handler; else 1 (keep
//   waiting). The caller re-checks its condition after each iteration.
//   On the PC profile the game thread spent about 70 % of its time in this loop; on the console the game thread showed
//   ~100 % of a core whatever the frame rate, on a machine with 3 cores for the game.
//
// WHAT THIS DOES
//   Before an iteration in which nothing can have changed - the same wait record as the previous iteration on this
//   thread, the read pointer still the one the record saw, and no ring progress since the previous iteration returned
//   (the progress counter of me_ring_wait.cpp, bumped by the ring thread after each ring segment, each fence write and
//   each interrupt) - it sleeps until the ring thread reports progress, at most sa2_gpu_wait_max_us. Then the original
//   iteration runs as always. The counter is read AFTER the original returns, i.e. before the caller re-checks its
//   condition, so a change the caller could have missed always shows as progress and the next iteration does not sleep.
//
// WHY IT IS EXACT: the game reads the same memory and takes the same decisions; it only reaches them later, by at most
//   one wake-up latency (or max_us when the wake-up comes from something the ring does not report).
// RISK: a wait whose condition changes without ring progress or an interrupt gains up to max_us per iteration.
// KEY: sa2_cpu_gpu_wait_sleep (with sa2_cpu_enhancements); sa2_gpu_wait_max_us.
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>

#include <fmt/format.h>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/platform.h>
#include <rex/ppc.h>

#include "cpu/sa2_enhancements.h"
#include "me/me_native_system.h"

SA2_ENHANCEMENT(g_gpu_wait_sleep, sa2_cpu_gpu_wait_sleep, sa2::enh::kCpu, true,
                "The game's Direct3D GPU wait (sub_821CEB30) sleeps until the ring thread makes progress instead of "
                "spinning");
REXCVAR_DEFINE_INT32(sa2_gpu_wait_max_us, 1000, "SA2 enhancements",
                     "sa2_cpu_gpu_wait_sleep: longest sleep per iteration of the game's GPU wait, in microseconds")
    .range(50, 100000)
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(sc5p2_gpu_wait_census_space, false, "SC5P2",
                    "Diagnostic (with sc5p2_gpu_wait_census): also census Direct3D's ring-space wait sub_821D1050 "
                    "itself (caller, ring thread state, distances)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(sc5p2_gpu_wait_census, false, "SC5P2",
                    "Diagnostic: every 10 s, log which of Direct3D's GPU waits the game slept in (by the caller of the "
                    "iteration) and how long")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace {

// sc5p2_gpu_wait_census: per caller of the iteration (its return address, ctx.lr: the generator stores it before
// every call in this build), iterations and time asleep, logged and cleared every 10 s.
struct CensusSlot {
  uint32_t caller = 0;
  uint64_t iterations = 0;
  uint64_t asleep_us = 0;
};
std::mutex g_census_mutex;
std::array<CensusSlot, 12> g_census{};
uint64_t g_census_other = 0;
std::chrono::steady_clock::time_point g_census_next{};

void NoteCensus(uint32_t caller, uint64_t asleep_us) {
  std::lock_guard lock(g_census_mutex);
  const auto now = std::chrono::steady_clock::now();
  if (g_census_next == std::chrono::steady_clock::time_point{}) g_census_next = now + std::chrono::seconds(10);
  CensusSlot* slot = nullptr;
  for (auto& s : g_census) {
    if (s.caller == caller || s.caller == 0) {
      slot = &s;
      break;
    }
  }
  if (slot) {
    slot->caller = caller;
    ++slot->iterations;
    slot->asleep_us += asleep_us;
  } else {
    ++g_census_other;
  }
  if (now < g_census_next) return;
  g_census_next = now + std::chrono::seconds(10);
  std::string text;
  for (auto& s : g_census) {
    if (!s.caller) break;
    text += fmt::format(" {:08X}: {} iterations, {:.1f} ms asleep;", s.caller, s.iterations, s.asleep_us / 1000.0);
    s = CensusSlot{};
  }
  REXLOG_INFO("[gpuwait] last 10 s, by caller of the iteration:{}{}", text,
              g_census_other ? fmt::format(" {} in other callers", g_census_other) : std::string());
  g_census_other = 0;
}

constexpr uint32_t kIteration = 0x821CEB30;
constexpr uint32_t kIterationBytes = 216;
constexpr uint64_t kIterationHash = 0x2F33DB135F1FB0BFull;

constexpr uint32_t kDeviceReadPointerWord = 11024;  // address of the word the GPU writes the read pointer back to
constexpr uint32_t kDeviceState = 11069;            // bit 0x02: GPU declared hung

inline uint32_t PhysOff(uint32_t a) {
#if REX_PLATFORM_WIN32 || (REX_PLATFORM_MAC && REX_ARCH_ARM64)
  return a >= 0xE0000000u ? 0x1000u : 0u;
#else
  (void)a;
  return 0u;
#endif
}
inline uint32_t Ld32(const uint8_t* base, uint32_t a) {
  uint32_t v;
  std::memcpy(&v, base + a + PhysOff(a), 4);
  return __builtin_bswap32(v);
}
inline uint8_t Ld8(const uint8_t* base, uint32_t a) { return base[a + PhysOff(a)]; }

// The previous iteration on this thread: its record, the record's start time base, and the progress counter when it
// returned.
thread_local uint32_t t_record = 0;
thread_local uint32_t t_record_start = 0;
thread_local uint32_t t_progress = 0;

std::atomic<int> g_state{0};  // 0 unknown, 1 on, -1 off (code check failed or key off); set by the first call

}  // namespace

REX_EXTERN(__imp__sub_821CEB30);
REX_HOOK_RAW(sub_821CEB30) {
  int state = g_state.load(std::memory_order_relaxed);
  if (state == 0) {
    state = (g_gpu_wait_sleep.active &&
             sa2::enh::GuestCodeMatches(base, kIteration, kIterationBytes, kIterationHash, "sa2_cpu_gpu_wait_sleep"))
                ? 1
                : -1;
    g_state.store(state, std::memory_order_relaxed);
  }
  if (state < 0) {
    __imp__sub_821CEB30(ctx, base);
    return;
  }
  const uint32_t record = ctx.r3.u32;
  const uint32_t start = Ld32(base, record + 20);
  static const bool census = REXCVAR_GET(sc5p2_gpu_wait_census);
  const uint32_t caller = uint32_t(ctx.lr);
  uint64_t asleep_us = 0;
  if (record == t_record && start == t_record_start) {
    const uint32_t device = Ld32(base, record);
    if (device && !(Ld8(base, device + kDeviceState) & 0x02)) {
      const uint32_t word = Ld32(base, device + kDeviceReadPointerWord);
      if (word && Ld32(base, word) == Ld32(base, record + 8) && me::native::RingProgress() == t_progress) {
        static const uint32_t max_us = uint32_t(REXCVAR_GET(sa2_gpu_wait_max_us));
        const auto t0 = std::chrono::steady_clock::now();
        me::native::WaitRingProgress(t_progress, max_us);
        g_gpu_wait_sleep.Use();
        asleep_us = uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(
                                 std::chrono::steady_clock::now() - t0)
                                 .count());
        g_gpu_wait_sleep.Add(asleep_us);
      }
    }
  }
  if (census) NoteCensus(caller, asleep_us);
  __imp__sub_821CEB30(ctx, base);
  t_record = record;
  t_record_start = start;
  t_progress = me::native::RingProgress();
}

namespace {
const bool g_unit = [] {
  g_gpu_wait_sleep.amount_unit = "us asleep";
  return true;
}();
}  // namespace

// sc5p2_gpu_wait_census, the ring-space wait itself: sub_821D1050(r3 = device, r4 = target) waits while
// (W - target) < (W - RPTR), W = [device+11036] (the game's write position in the primary ring) and RPTR = the read
// pointer the ring thread writes back: until the ring thread has consumed the primary ring up to `target`. Logged every
// 10 s: the waits, their time, what the ring thread was doing when each began (me::native::RingDiagState), how far the
// game was ahead of the ring thread and how far past the ring thread's position the target was; plus the first 24
// waits one by one.
namespace {
struct SpaceCensus {
  uint64_t calls = 0, waits = 0, wait_us = 0, by_state[4] = {}, ahead = 0, ahead_max = 0, beyond = 0, beyond_max = 0;
  std::array<std::pair<uint32_t, uint64_t>, 6> callers{};  // caller of sub_821D1050 -> microseconds waited
  int logged = 0;
  std::chrono::steady_clock::time_point next{};
};
SpaceCensus g_space;
std::mutex g_space_mutex;
}  // namespace

REX_EXTERN(__imp__sub_821D1050);
REX_HOOK_RAW(sub_821D1050) {
  static const bool census = REXCVAR_GET(sc5p2_gpu_wait_census) && REXCVAR_GET(sc5p2_gpu_wait_census_space);
  const uint32_t device = ctx.r3.u32, target = ctx.r4.u32, caller = uint32_t(ctx.lr);
  if (!census || !device || !target) {
    __imp__sub_821D1050(ctx, base);
    return;
  }
  const uint32_t w = Ld32(base, device + 11036), word = Ld32(base, device + 11024);
  const uint32_t rptr = word ? Ld32(base, word) : 0;
  const bool waits = (w - target) < (w - rptr);
  const uint32_t state = me::native::RingDiagState(), pos = me::native::RingDiagPos();
  const auto t0 = std::chrono::steady_clock::now();
  if (waits) me::native::TimelineNote(me::native::kTlGpuWait, caller, target - rptr);
  __imp__sub_821D1050(ctx, base);
  if (waits) me::native::TimelineNote(me::native::kTlGpuWaitEnd, caller);
  const auto now = std::chrono::steady_clock::now();
  const uint64_t us = uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(now - t0).count());
  std::lock_guard lock(g_space_mutex);
  auto& c = g_space;
  if (c.next == std::chrono::steady_clock::time_point{}) c.next = now + std::chrono::seconds(10);
  ++c.calls;
  if (waits) {
    ++c.waits;
    c.wait_us += us;
    ++c.by_state[state & 3];
    const uint64_t ahead = w - rptr, beyond = target - rptr;
    c.ahead += ahead;
    c.ahead_max = std::max(c.ahead_max, ahead);
    c.beyond += beyond;
    c.beyond_max = std::max(c.beyond_max, beyond);
    for (auto& slot : c.callers) {
      if (slot.first == caller || slot.first == 0) {
        slot.first = caller;
        slot.second += us;
        break;
      }
    }
    if (c.logged < 24) {
      ++c.logged;
      REXLOG_INFO("[gpuwait] ring space from {:08X}: W {:08X} target {:08X} RPTR {:08X} (ring thread {} at {:08X}); "
                  "waited {} us",
                  caller, w, target, rptr,
                  state == 0 ? "idle" : state == 1 ? "parsing" : state == 2 ? "in the flip wait" : "in another wait", pos,
                  us);
    }
  }
  if (now < c.next) return;
  c.next = now + std::chrono::seconds(10);
  REXLOG_INFO("[gpuwait] ring space, last 10 s: {} calls, {} waited, {:.1f} ms; ring thread at the start: idle {}, "
              "parsing {}, flip wait {}, other wait {}; the game ahead by {} words on average (max {}), the target {} "
              "words past the read pointer on average (max {})",
              c.calls, c.waits, c.wait_us / 1000.0, c.by_state[0], c.by_state[1], c.by_state[2], c.by_state[3],
              c.waits ? c.ahead / c.waits : 0, c.ahead_max, c.waits ? c.beyond / c.waits : 0, c.beyond_max);
  std::string by_caller;
  for (const auto& slot : c.callers)
    if (slot.first) by_caller += fmt::format(" {:08X}: {:.1f} ms;", slot.first, slot.second / 1000.0);
  if (!by_caller.empty()) REXLOG_INFO("[gpuwait] ring space by caller:{}", by_caller);
  const int logged = c.logged;
  c = SpaceCensus{};
  c.logged = logged;
  c.next = now + std::chrono::seconds(10);
}
