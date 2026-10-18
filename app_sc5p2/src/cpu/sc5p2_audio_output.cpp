// Space Channel 5 Part 2: which output the console's audio goes to (see sc5p2_rhythm.h). On the Switch, audctl's
// GetActiveOutputTarget (13.0.0+; the play report's output target as a fallback) says Speaker, Headphone, Tv,
// UsbOutputDevice or Bluetooth. Read once when the watch starts, then every second by a thread that is never joined
// and never destroyed (libnx cannot detach a thread); every change is logged.
#include "cpu/sc5p2_rhythm.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

#include <rex/logging.h>
#include <rex/platform.h>

#if REX_PLATFORM_SWITCH
#include <switch.h>
#endif

namespace sc5p2::rhythm {
namespace {

constexpr int kUnknown = -1;
constexpr int kBluetooth = 5;  // AudioTarget_Bluetooth
std::atomic<int> g_target{kUnknown};

[[maybe_unused]] const char* TargetName(int target) {
  switch (target) {
    case 1: return "the speaker";
    case 2: return "wired headphones";
    case 3: return "the TV";
    case 4: return "a USB audio device";
    case kBluetooth: return "Bluetooth headphones";
    default: return "an unknown output";
  }
}

#if REX_PLATFORM_SWITCH
int Query() {
  AudioTarget target = AudioTarget_Invalid;
  if (R_SUCCEEDED(audctlGetActiveOutputTarget(&target))) return int(target);
  if (R_SUCCEEDED(audctlGetAudioOutputTargetForPlayReport(&target))) return int(target);
  return kUnknown;
}
#endif

}  // namespace

void StartAudioOutputWatch() {
  static std::once_flag once;
  std::call_once(once, [] {
#if REX_PLATFORM_SWITCH
    const Result rc = audctlInitialize();
    if (R_FAILED(rc)) {
      REXLOG_WARN("[rhythm] the audio output cannot be read (audctl {:#x}): the Bluetooth offset stays off", rc);
      return;
    }
    const int first = Query();
    g_target.store(first, std::memory_order_relaxed);
    REXLOG_WARN("[rhythm] audio output: {}", first == kUnknown ? "unreadable" : TargetName(first));
    new std::thread([] {
      for (;;) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        const int now = Query();
        const int before = g_target.exchange(now, std::memory_order_relaxed);
        if (now != before) {
          REXLOG_WARN("[rhythm] audio output: {}", now == kUnknown ? "unreadable" : TargetName(now));
        }
      }
    });
#endif
  });
}

bool BluetoothActive() { return g_target.load(std::memory_order_relaxed) == kBluetooth; }

}  // namespace sc5p2::rhythm
