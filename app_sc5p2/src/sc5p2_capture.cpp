// Space Channel 5 Part 2: the test-run video recorder (see sc5p2_capture.h).

#include "sc5p2_capture.h"

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/ui/presenter.h>

REXCVAR_DEFINE_INT32(sc5p2_video_s, 0, "SC5P2",
                     "Test runs: record a video starting this many seconds after start (0 = off)");
REXCVAR_DEFINE_INT32(sc5p2_video_frames, 300, "SC5P2", "Frames in each recording");
REXCVAR_DEFINE_INT32(sc5p2_video_fps, 30, "SC5P2", "Recording rate (frames per second of wall time)");
REXCVAR_DEFINE_BOOL(sc5p2_video_half, true, "SC5P2", "Record at half the presented resolution");
REXCVAR_DEFINE_STRING(sc5p2_video_trigger_file, "", "SC5P2",
                      "Test runs: when this file appears, record the frames written in it, then delete it");

namespace sc5p2::capture {
namespace {

std::mutex g_mutex;
std::condition_variable g_cv;
bool g_stop = false;
std::thread g_thread;

// Waits up to `d`; true when Stop() was called.
template <class D>
bool WaitStop(D d) {
  std::unique_lock lock(g_mutex);
  return g_cv.wait_for(lock, d, [] { return g_stop; });
}

// Records `count` frames at sc5p2_video_fps into captures/<name>.rgba (+ .txt: width height fps frames).
void Record(const std::function<rex::ui::Presenter*()>& get_presenter, const std::string& name, int count) {
  const auto folder = rex::filesystem::GetExecutableFolder() / "captures";
  std::error_code ec;
  std::filesystem::create_directories(folder, ec);
  std::ofstream raw(folder / (name + ".rgba"), std::ios::binary | std::ios::trunc);
  if (!raw) {
    REXLOG_WARN("[capture] cannot write {}", rex::path_to_utf8(folder / (name + ".rgba")));
    return;
  }
  const int fps = std::max(1, REXCVAR_GET(sc5p2_video_fps));
  const uint32_t step = REXCVAR_GET(sc5p2_video_half) ? 2 : 1;
  const auto period = std::chrono::microseconds(1000000 / fps);
  uint32_t width = 0, height = 0;
  std::vector<uint8_t> frame;
  int written = 0, failed = 0;
  auto next = std::chrono::steady_clock::now();
  REXLOG_INFO("[capture] recording {} frames at {} fps into {}", count, fps, name);
  while (written < count) {
    if (WaitStop(next - std::chrono::steady_clock::now())) break;
    next += period;
    rex::ui::Presenter* presenter = get_presenter ? get_presenter() : nullptr;
    rex::ui::RawImage image;
    if (!presenter || !presenter->CaptureGuestOutput(image) || !image.width || !image.height) {
      if (++failed % 30 == 1) REXLOG_WARN("[capture] no guest output to capture ({} misses)", failed);
      continue;
    }
    if (!width) {
      width = image.width;
      height = image.height;
    }
    if (image.width != width || image.height != height) continue;  // the presented size changed: skip the frame
    const uint32_t w = width / step, h = height / step;
    frame.resize(size_t(w) * h * 4);
    for (uint32_t y = 0; y < h; ++y) {
      const uint8_t* row = image.data.data() + size_t(y) * step * image.stride;
      uint8_t* out = frame.data() + size_t(y) * w * 4;
      for (uint32_t x = 0; x < w; ++x) std::memcpy(out + size_t(x) * 4, row + size_t(x) * step * 4, 4);
    }
    raw.write(reinterpret_cast<const char*>(frame.data()), std::streamsize(frame.size()));
    ++written;
  }
  raw.close();
  std::ofstream(folder / (name + ".txt")) << width / step << ' ' << height / step << ' ' << fps << ' ' << written
                                          << '\n';
  REXLOG_INFO("[capture] {}: {} frames, {}x{} ({} misses)", name, written, width / step, height / step, failed);
}

void Run(std::function<rex::ui::Presenter*()> get_presenter) {
  const int start_s = REXCVAR_GET(sc5p2_video_s);
  if (start_s > 0) {
    if (WaitStop(std::chrono::seconds(start_s))) return;
    Record(get_presenter, "video_" + std::to_string(start_s) + "s", std::max(1, REXCVAR_GET(sc5p2_video_frames)));
  }
  const std::string trigger = REXCVAR_GET(sc5p2_video_trigger_file);
  if (trigger.empty()) return;
  for (int n = 1; !WaitStop(std::chrono::milliseconds(100));) {
    std::error_code ec;
    if (!std::filesystem::exists(trigger, ec)) continue;
    int count = 0;
    std::ifstream(trigger) >> count;
    std::filesystem::remove(trigger, ec);
    Record(get_presenter, "live_" + std::to_string(n++), count > 0 ? count : std::max(1, REXCVAR_GET(sc5p2_video_frames)));
  }
}

}  // namespace

void Start(std::function<rex::ui::Presenter*()> get_presenter) {
  if (REXCVAR_GET(sc5p2_video_s) <= 0 && REXCVAR_GET(sc5p2_video_trigger_file).empty()) return;
  {
    std::lock_guard lock(g_mutex);
    g_stop = false;
  }
  g_thread = std::thread(Run, std::move(get_presenter));
}

void Stop() {
  {
    std::lock_guard lock(g_mutex);
    g_stop = true;
  }
  g_cv.notify_all();
  if (g_thread.joinable()) g_thread.join();
}

}  // namespace sc5p2::capture
