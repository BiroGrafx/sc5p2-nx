// Space Channel 5 Part 2 diagnostic (sc5p2_diag_input): who reads the pad and when, relative to the vblanks and flips.
// sub_822B8540 is the game's only call of XamInputGetState (r3 = user, r4 = XINPUT_STATE). Logged: the first 64 reads
// one by one (guest thread, caller, Direct3D's vblank and flip counts, microseconds since the last vblank), every
// change of the buttons, and every 10 s the reads per guest thread.
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>

#include <fmt/format.h>

#include <rex/chrono/clock.h>
#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc.h>
#include <rex/system/xthread.h>

#include "me/me_native_system.h"

REXCVAR_DEFINE_BOOL(sc5p2_diag_input, false, "SC5P2",
                    "Diagnostic: log the game's pad reads (thread, caller, vblank/flip counts, button changes)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace {

constexpr uint32_t kDevice = 0x40002600;  // Direct3D's device (the interrupt callback's data on every run so far)
// The game's judging, found with memory dumps on the Mac (2026-10-17): the View Rating as percent x 10 (u16, 100 =
// 10.0 %) and the hearts (u8), both in the dance state at 0x82F235xx. Logged at every change ("[judge]"), so a test run
// says objectively how the moves were judged.
constexpr uint32_t kViewRating = 0x82F23520;
constexpr uint32_t kHearts = 0x82F235BD;

inline uint32_t Ld32(const uint8_t* base, uint32_t a) {
  uint32_t v;
  std::memcpy(&v, base + a, 4);
  return __builtin_bswap32(v);
}
inline uint16_t Ld16(const uint8_t* base, uint32_t a) {
  uint16_t v;
  std::memcpy(&v, base + a, 2);
  return __builtin_bswap16(v);
}

std::mutex g_mutex;
int g_logged = 0;
uint16_t g_last_buttons = 0;
uint32_t g_last_vblank_count = 0;
std::chrono::steady_clock::time_point g_last_vblank_seen{}, g_next_report{};
std::array<std::pair<uint32_t, uint64_t>, 8> g_by_thread{};
double g_after_flip_sum = 0, g_after_flip_max = 0, g_after_vblank_sum = 0;
uint64_t g_reads = 0;
uint16_t g_last_rating = 0xFFFF;
uint8_t g_last_hearts = 0xFF;
uint64_t g_rating_up = 0, g_rating_down = 0;

}  // namespace

REX_EXTERN(__imp__sub_822B8540);
REX_HOOK_RAW(sub_822B8540) {
  static const bool on = REXCVAR_GET(sc5p2_diag_input);
  const uint32_t state = ctx.r4.u32, caller = uint32_t(ctx.lr);
  __imp__sub_822B8540(ctx, base);
  me::native::TimelineNote(me::native::kTlPadRead, Ld32(base, kDevice + 16848),
                           state ? Ld16(base, state + 4) : 0);
  if (!on) return;
  const auto now = std::chrono::steady_clock::now();
  const uint32_t vblanks = Ld32(base, kDevice + 16824), flips = Ld32(base, kDevice + 16848);
  // Guest time base now vs Direct3D's stamps at the last vblank (+16828) and the last flip (+16836), in microseconds.
  const uint32_t tb = uint32_t(rex::chrono::Clock::QueryGuestTickCount());
  const double us_per_tick = 1e6 / double(rex::chrono::Clock::guest_tick_frequency());
  const double after_vblank = double(uint32_t(tb - Ld32(base, kDevice + 16828))) * us_per_tick;
  const double after_flip = double(uint32_t(tb - Ld32(base, kDevice + 16836))) * us_per_tick;
  const uint16_t buttons = state ? Ld16(base, state + 4) : 0;
  auto* thread = rex::system::XThread::GetCurrentThread();
  const uint32_t entry = thread ? thread->creation_params()->start_address : 0;
  std::lock_guard lock(g_mutex);
  // The vblank count moves on the vblank thread; the first read that sees a new count approximates its time.
  if (vblanks != g_last_vblank_count) {
    g_last_vblank_count = vblanks;
    g_last_vblank_seen = now;
  }
  const auto since = std::chrono::duration_cast<std::chrono::microseconds>(now - g_last_vblank_seen).count();
  for (auto& slot : g_by_thread) {
    if (slot.first == entry || slot.first == 0) {
      slot.first = entry;
      ++slot.second;
      break;
    }
  }
  (void)since;
  g_after_flip_sum += after_flip;
  g_after_flip_max = std::max(g_after_flip_max, after_flip);
  g_after_vblank_sum += after_vblank;
  ++g_reads;
  if (g_logged < 64 || (g_reads % 300) == 0) {
    ++g_logged;
    REXLOG_INFO("[input] read by thread {:08X} from {:08X}: vblank {} flip {}, {:.0f} us after the vblank, {:.0f} us "
                "after the flip, buttons {:04X}",
                entry, caller, vblanks, flips, after_vblank, after_flip, buttons);
  }
  {
    const uint16_t rating = Ld16(base, kViewRating);
    const uint8_t hearts = base[kHearts];
    if (rating != g_last_rating || hearts != g_last_hearts) {
      if (g_last_rating != 0xFFFF && rating != g_last_rating) (rating > g_last_rating ? g_rating_up : g_rating_down)++;
      REXLOG_INFO("[judge] rating {}.{} % -> {}.{} %, hearts {} -> {} (flip {}, buttons {:04X})", g_last_rating / 10,
                  g_last_rating % 10, rating / 10, rating % 10, g_last_hearts, hearts, flips, buttons);
      g_last_rating = rating;
      g_last_hearts = hearts;
    }
  }
  if (buttons != g_last_buttons) {
    REXLOG_INFO("[input] buttons {:04X} -> {:04X} (thread {:08X}, vblank {} flip {})", g_last_buttons, buttons, entry,
                vblanks, flips);
    g_last_buttons = buttons;
  }
  if (g_next_report == std::chrono::steady_clock::time_point{}) g_next_report = now + std::chrono::seconds(10);
  if (now >= g_next_report) {
    g_next_report = now + std::chrono::seconds(10);
    std::string text;
    for (auto& slot : g_by_thread) {
      if (slot.first) text += fmt::format(" thread {:08X}: {} reads;", slot.first, slot.second);
      slot = {};
    }
    REXLOG_INFO("[input] last 10 s:{}; read on average {:.0f} us after the flip (max {:.0f}), {:.0f} us after the "
                "vblank; rating rises {} falls {}",
                text, g_reads ? g_after_flip_sum / g_reads : 0.0, g_after_flip_max,
                g_reads ? g_after_vblank_sum / g_reads : 0.0, g_rating_up, g_rating_down);
    g_rating_up = g_rating_down = 0;
    g_after_flip_sum = g_after_flip_max = g_after_vblank_sum = 0;
    g_reads = 0;
  }
}
