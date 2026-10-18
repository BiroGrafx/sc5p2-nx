// Space Channel 5 Part 2: native C runtime memcpy and memset (CPU enhancements sa2_cpu_native_memcpy and
// sa2_cpu_native_memset).
//
// The game's CRT memcpy (sub_82280AE0, 1160 bytes) and memset (sub_82281010, 160 bytes) are byte for byte SA2's
// sub_827F05C0 and sub_827EFEA0 (the same FNV-1a hashes), so sa2-nx's natives replace them unchanged
// (src/cpu/hot/n_827F05C0.h and n_827EFEA0.h, copied from sa2-nx's app: they say why they are exact). Profile (Mac,
// the Report 4 boss arena, 2026-10-18): the recompiled memcpy was the hottest function, 24 % of the game's busy
// samples; the console ran that fight at 22-28 fps, CPU bound.
//
// WHY EXACT: the natives reproduce the routines' memory effect and leave every register as the original's callers see
// it (both are assembly routines: callers treat r4-r12, ctr and the CR fields as clobbered); a call they cannot copy
// exactly (memcpy onto an overlapping higher destination, a range that wraps around 4 GB, memset with a 64-bit count)
// is declined and the original runs. The guest code is checked against the hash before the first use.
// RISK: none known: sa2-nx runs the same natives with its guard on the console (no difference ever logged).
// KEY: sa2_cpu_native_memcpy, sa2_cpu_native_memset (with sa2_cpu_enhancements).
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>

#include <fmt/format.h>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc.h>

#include "cpu/sa2_enhancements.h"
#include "cpu/hot/n_827EFEA0.h"
#include "cpu/hot/n_827F05C0.h"

SA2_ENHANCEMENT(g_native_memcpy, sa2_cpu_native_memcpy, sa2::enh::kCpu, true,
                "Native CRT memcpy (sub_82280AE0, SA2's sub_827F05C0): host memcpy/memmove");
SA2_ENHANCEMENT(g_native_memset, sa2_cpu_native_memset, sa2::enh::kCpu, true,
                "Native CRT memset (sub_82281010, SA2's sub_827EFEA0): host memset");

namespace {

constexpr uint32_t kMemcpy = 0x82280AE0;
constexpr uint32_t kMemcpyBytes = 1160;
constexpr uint64_t kMemcpyHash = 0xEE0EAEB1E0584297ull;
constexpr uint32_t kMemset = 0x82281010;
constexpr uint32_t kMemsetBytes = 160;
constexpr uint64_t kMemsetHash = 0x54D201DC8A21DABAull;

// 0 unknown, 1 on, -1 off (key off or the code check failed); set by the first call (every racing first caller stores
// the same value).
std::atomic<int> g_memcpy_state{0}, g_memset_state{0};

inline bool On(std::atomic<int>& state, const sa2::enh::Enhancement& e, const uint8_t* base, uint32_t address,
               uint32_t bytes, uint64_t hash) {
  int s = state.load(std::memory_order_relaxed);
  if (s == 0) {
    s = (e.active && sa2::enh::GuestCodeMatches(base, address, bytes, hash, e.key)) ? 1 : -1;
    state.store(s, std::memory_order_relaxed);
  }
  return s > 0;
}

}  // namespace

// sc5p2_diag_memcpy: every 10 s, the game's memcpy calls by size and by caller (bytes copied), on the calling threads'
// shared counters (a diagnostic: racing threads may lose a count).
REXCVAR_DEFINE_BOOL(sc5p2_diag_memcpy, false, "SC5P2", "Diagnostic: log the game's memcpy calls by size and caller")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
namespace {
struct MemcpyDiag {
  std::mutex mutex;
  uint64_t calls[8] = {}, bytes[8] = {};  // sizes <=16, <=64, <=256, <=1K, <=4K, <=16K, <=64K, more
  std::array<std::pair<uint32_t, uint64_t>, 24> callers{};
  std::chrono::steady_clock::time_point next{};
};
MemcpyDiag g_memcpy_diag;
void NoteMemcpy(uint32_t caller, uint32_t n) {
  auto& d = g_memcpy_diag;
  std::lock_guard lock(d.mutex);
  int bucket = 0;
  for (uint32_t limit = 16; bucket < 7 && n > limit; limit <<= 2) ++bucket;
  ++d.calls[bucket];
  d.bytes[bucket] += n;
  for (auto& c : d.callers) {
    if (c.first == caller || c.first == 0) {
      c.first = caller;
      c.second += n;
      break;
    }
  }
  const auto now = std::chrono::steady_clock::now();
  if (d.next == std::chrono::steady_clock::time_point{}) d.next = now + std::chrono::seconds(10);
  if (now < d.next) return;
  d.next = now + std::chrono::seconds(10);
  static const char* const kNames[8] = {"<=16", "<=64", "<=256", "<=1K", "<=4K", "<=16K", "<=64K", ">64K"};
  std::string text;
  for (int i = 0; i < 8; ++i) text += fmt::format(" {}: {} calls {:.1f} MB;", kNames[i], d.calls[i], d.bytes[i] / 1e6);
  std::sort(d.callers.begin(), d.callers.end(), [](auto& a, auto& b) { return a.second > b.second; });
  std::string top;
  for (int i = 0; i < 10 && d.callers[i].first; ++i)
    top += fmt::format(" {:08X} {:.1f} MB;", d.callers[i].first, d.callers[i].second / 1e6);
  REXLOG_INFO("[memcpy] last 10 s by size:{} top callers by bytes:{}", text, top);
  for (auto& c : d.calls) c = 0;
  for (auto& b : d.bytes) b = 0;
  d.callers = {};
}
}  // namespace

REX_EXTERN(__imp__sub_82280AE0);
REX_HOOK_RAW(sub_82280AE0) {
  static const bool diag = REXCVAR_GET(sc5p2_diag_memcpy);
  if (diag) NoteMemcpy(uint32_t(ctx.lr), ctx.r5.u32);
  if (On(g_memcpy_state, g_native_memcpy, base, kMemcpy, kMemcpyBytes, kMemcpyHash) &&
      sa2::hot::n_827F05C0::Native(ctx, base)) {
    g_native_memcpy.UseSampled();
    return;
  }
  __imp__sub_82280AE0(ctx, base);
}

REX_EXTERN(__imp__sub_82281010);
REX_HOOK_RAW(sub_82281010) {
  if (On(g_memset_state, g_native_memset, base, kMemset, kMemsetBytes, kMemsetHash) &&
      sa2::hot::n_827EFEA0::Native(ctx, base)) {
    g_native_memset.UseSampled();
    return;
  }
  __imp__sub_82281010(ctx, base);
}
