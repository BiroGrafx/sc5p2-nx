// Space Channel 5 Part 2: the scripted test controller (see sc5p2_test_input.h).

#include "sc5p2_test_input.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include <rex/cvar.h>
#include <rex/input/device_assignment.h>
#include <rex/input/input_driver.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

REXCVAR_DEFINE_STRING(sc5p2_test_buttons, "", "SC5P2",
                      "Test runs: scripted presses \"T:btn,T1-T2:btn+btn\" (seconds after start)");
REXCVAR_DEFINE_STRING(sc5p2_test_buttons_file, "", "SC5P2",
                      "Test runs: file whose new lines \"btn[+btn][:hold]\" are pressed when read");

namespace sc5p2::test_input {
namespace {

// The SDK's Xbox types and status codes live in rex and rex::input.
using namespace rex;
using namespace rex::input;

using Clock = std::chrono::steady_clock;
constexpr rex::input::DeviceId kTestPad = static_cast<rex::input::DeviceId>(0x53433550);  // "SC5P"
constexpr double kDefaultHold = 0.15;

struct Press {
  double from, to;  // seconds since start
  uint16_t buttons;
  uint8_t left_trigger, right_trigger;
};

// "a+up" -> button bits; triggers separately. False for an unknown name.
bool ParseButtons(const std::string& text, Press& press) {
  std::stringstream names(text);
  std::string name;
  while (std::getline(names, name, '+')) {
    if (name == "a") press.buttons |= rex::input::X_INPUT_GAMEPAD_A;
    else if (name == "b") press.buttons |= rex::input::X_INPUT_GAMEPAD_B;
    else if (name == "x") press.buttons |= rex::input::X_INPUT_GAMEPAD_X;
    else if (name == "y") press.buttons |= rex::input::X_INPUT_GAMEPAD_Y;
    else if (name == "start") press.buttons |= rex::input::X_INPUT_GAMEPAD_START;
    else if (name == "back") press.buttons |= rex::input::X_INPUT_GAMEPAD_BACK;
    else if (name == "up") press.buttons |= rex::input::X_INPUT_GAMEPAD_DPAD_UP;
    else if (name == "down") press.buttons |= rex::input::X_INPUT_GAMEPAD_DPAD_DOWN;
    else if (name == "left") press.buttons |= rex::input::X_INPUT_GAMEPAD_DPAD_LEFT;
    else if (name == "right") press.buttons |= rex::input::X_INPUT_GAMEPAD_DPAD_RIGHT;
    else if (name == "lb") press.buttons |= rex::input::X_INPUT_GAMEPAD_LEFT_SHOULDER;
    else if (name == "rb") press.buttons |= rex::input::X_INPUT_GAMEPAD_RIGHT_SHOULDER;
    else if (name == "ls") press.buttons |= rex::input::X_INPUT_GAMEPAD_LEFT_THUMB;
    else if (name == "rs") press.buttons |= rex::input::X_INPUT_GAMEPAD_RIGHT_THUMB;
    else if (name == "lt") press.left_trigger = 255;
    else if (name == "rt") press.right_trigger = 255;
    else return false;
  }
  return true;
}

class TestPadDriver final : public rex::input::InputDriver {
 public:
  TestPadDriver() : InputDriver(nullptr, 0), start_(Clock::now()) {}

  X_STATUS Setup() override {
    // Timed script: "T:btn" or "T1-T2:btn", comma separated.
    std::stringstream entries(REXCVAR_GET(sc5p2_test_buttons));
    std::string entry;
    while (std::getline(entries, entry, ',')) {
      const auto colon = entry.find(':');
      if (colon == std::string::npos) continue;
      const std::string when = entry.substr(0, colon);
      Press press{};
      const auto dash = when.find('-');
      press.from = std::atof(when.substr(0, dash).c_str());
      press.to = dash == std::string::npos ? press.from + kDefaultHold : std::atof(when.substr(dash + 1).c_str());
      if (!ParseButtons(entry.substr(colon + 1), press)) {
        REXLOG_WARN("sc5p2_test_buttons: unknown button in \"{}\"", entry);
        continue;
      }
      presses_.push_back(press);
    }
    file_ = REXCVAR_GET(sc5p2_test_buttons_file);
    REXLOG_INFO("test input: scripted pad with {} timed presses{}", presses_.size(),
                file_.empty() ? "" : ", live file " + file_);
    return X_STATUS_SUCCESS;
  }

  void EnumerateDevices(std::vector<rex::input::DeviceInfo>& out) override {
    rex::input::DeviceInfo info;
    info.id = kTestPad;
    info.name = "SC5P2 test pad";
    info.synthetic = true;
    out.push_back(info);
  }

  X_RESULT GetDeviceState(rex::input::DeviceId id, X_INPUT_STATE* out_state) override {
    if (id != kTestPad) return X_ERROR_DEVICE_NOT_CONNECTED;
    std::lock_guard lock(mutex_);
    const double now = std::chrono::duration<double>(Clock::now() - start_).count();
    PollFile(now);
    uint16_t buttons = 0;
    uint8_t lt = 0, rt = 0;
    for (const auto& p : presses_) {
      if (now >= p.from && now < p.to) {
        buttons |= p.buttons;
        lt = std::max(lt, p.left_trigger);
        rt = std::max(rt, p.right_trigger);
      }
    }
    // Games compare packet numbers to see whether anything changed.
    if (buttons != last_buttons_ || lt != last_lt_ || rt != last_rt_) {
      packet_++;
      if (buttons) REXLOG_INFO("test input: {:.2f} s buttons {:04X} lt {} rt {}", now, buttons, lt, rt);
      last_buttons_ = buttons;
      last_lt_ = lt;
      last_rt_ = rt;
    }
    ApplyPins();
    std::memset(out_state, 0, sizeof(*out_state));
    out_state->packet_number = packet_;
    out_state->gamepad.buttons = buttons;
    out_state->gamepad.left_trigger = lt;
    out_state->gamepad.right_trigger = rt;
    return X_ERROR_SUCCESS;
  }

  X_RESULT GetDeviceCapabilities(rex::input::DeviceId id, uint32_t, X_INPUT_CAPABILITIES* out_caps) override {
    if (id != kTestPad) return X_ERROR_DEVICE_NOT_CONNECTED;
    if (out_caps) {
      std::memset(out_caps, 0, sizeof(*out_caps));
      out_caps->type = 0x01;      // XINPUT_DEVTYPE_GAMEPAD
      out_caps->sub_type = 0x01;  // XINPUT_DEVSUBTYPE_GAMEPAD
      out_caps->gamepad.buttons = 0xFFFF;
      out_caps->gamepad.left_trigger = 0xFF;
      out_caps->gamepad.right_trigger = 0xFF;
    }
    return X_ERROR_SUCCESS;
  }

  X_RESULT SetDeviceVibration(rex::input::DeviceId id, X_INPUT_VIBRATION*) override {
    return id == kTestPad ? X_ERROR_SUCCESS : X_ERROR_DEVICE_NOT_CONNECTED;
  }

  X_RESULT GetDeviceKeystroke(rex::input::DeviceId id, uint32_t, X_INPUT_KEYSTROKE*) override {
    return id == kTestPad ? X_ERROR_EMPTY : X_ERROR_DEVICE_NOT_CONNECTED;
  }

 private:
  // Live file: lines appended since the last read become presses starting now. Read at most every 100 ms.
  void PollFile(double now) {
    if (file_.empty() || now - last_poll_ < 0.1) return;
    last_poll_ = now;
    std::ifstream in(file_);
    std::string line;
    size_t index = 0;
    while (std::getline(in, line)) {
      if (index++ < lines_done_) continue;
      lines_done_ = index;
      if (line.empty() || line[0] == '#') continue;
      if (line[0] == '!') {
        Command(line);
        continue;
      }
      double hold = kDefaultHold;
      const auto colon = line.find(':');
      if (colon != std::string::npos) hold = std::atof(line.substr(colon + 1).c_str());
      Press press{now, now + hold, 0, 0, 0};
      if (ParseButtons(line.substr(0, colon), press)) {
        presses_.push_back(press);
        REXLOG_INFO("test input: live \"{}\" at {:.2f} s", line, now);
      } else {
        REXLOG_WARN("test input: unknown button in live line \"{}\"", line);
      }
    }
  }

  // Test aids in the live file, for reaching later scenes unattended (never in a player's build: the scripted pad
  // only exists when a test script is set):
  //   !dump <path>              every committed, readable guest region: records [u32 address][u32 size][bytes]
  //   !pin<8|16|32|f> <hex> <v> write the value (big-endian) at the guest address before every input poll
  //   !poke<8|16|32|f> <hex> <v> write it once
  //   !unpin                    drop every pin
  struct Pin {
    uint32_t address;
    int width;  // 1, 2, 4; 0 = float
    double value;
  };

  static void Write(const Pin& pin) {
    auto* memory = rex::system::kernel_state()->memory();
    uint8_t* p = memory->TranslateVirtual(pin.address);
    uint32_t bits = 0;
    if (pin.width == 0) {
      const float f = float(pin.value);
      std::memcpy(&bits, &f, 4);
    } else {
      bits = uint32_t(int64_t(pin.value));
    }
    switch (pin.width) {
      case 1: p[0] = uint8_t(bits); break;
      case 2: { const uint16_t v = __builtin_bswap16(uint16_t(bits)); std::memcpy(p, &v, 2); break; }
      default: { const uint32_t v = __builtin_bswap32(bits); std::memcpy(p, &v, 4); break; }
    }
  }

  void ApplyPins() {
    for (const auto& pin : pins_) Write(pin);
    // !watch8 / !watch32 / !watchf: log every change of the value (checked at each pad poll, i.e. every frame).
    auto* memory = rex::system::kernel_state()->memory();
    for (auto& w : watches_) {
      const uint8_t* p = memory->TranslateVirtual(w.address);
      uint32_t v = 0;
      if (w.width == 1) {
        v = p[0];
      } else {
        std::memcpy(&v, p, 4);
        v = __builtin_bswap32(v);
      }
      if (v != w.last || !w.seen) {
        const double t = std::chrono::duration<double>(Clock::now() - start_).count();
        if (w.width == 0) {
          float f, g;
          std::memcpy(&f, &v, 4);
          std::memcpy(&g, &w.last, 4);
          REXLOG_INFO("[watch] {:.3f} s {:08X}: {} -> {}", t, w.address, w.seen ? g : 0.0f, f);
        } else {
          REXLOG_INFO("[watch] {:.3f} s {:08X}: {} -> {}", t, w.address, w.seen ? w.last : 0u, v);
        }
        w.last = v;
        w.seen = true;
      }
    }
  }

  static void Dump(const std::string& path) {
    auto* memory = rex::system::kernel_state()->memory();
    std::ofstream out(path, std::ios::binary);
    uint64_t total = 0, regions = 0;
    // The heaps in order (0x7F000000 has none; the physical views each keep their own page table).
    for (uint64_t address = 0; address < 0xFFD00000ull;) {
      auto* heap = memory->LookupHeap(uint32_t(address));
      if (!heap) {
        address = address < 0x80000000ull ? 0x80000000ull : 0xFFD00000ull;
        continue;
      }
      rex::memory::HeapAllocationInfo info{};
      if (!heap->QueryRegionInfo(uint32_t(address), &info) || !info.region_size) break;
      const uint64_t size = info.region_size;
      if ((info.state & rex::memory::kMemoryAllocationCommit) && (info.protect & rex::memory::kMemoryProtectRead)) {
        const uint32_t header[2] = {uint32_t(address), uint32_t(size)};
        out.write(reinterpret_cast<const char*>(header), sizeof(header));
        out.write(reinterpret_cast<const char*>(memory->TranslateVirtual(uint32_t(address))), std::streamsize(size));
        total += size;
        ++regions;
      }
      address += size;
    }
    REXLOG_INFO("test input: dumped {} regions, {:.1f} MB, to {}", regions, total / 1048576.0, path);
  }

  void Command(const std::string& line) {
    std::stringstream words(line.substr(1));
    std::string verb, address_text;
    words >> verb;
    if (verb == "dump") {
      std::string path;
      std::getline(words >> std::ws, path);
      Dump(path);
      return;
    }
    if (verb.rfind("watch", 0) == 0) {
      Watch w{};
      const std::string type = verb.substr(5);
      w.width = type == "8" ? 1 : type == "f" ? 0 : 4;
      words >> address_text;
      w.address = uint32_t(std::strtoul(address_text.c_str(), nullptr, 16));
      if (w.address) {
        watches_.push_back(w);
        REXLOG_INFO("test input: watching {:08X} ({})", w.address, verb);
      }
      return;
    }
    if (verb == "unpin") {
      pins_.clear();
      REXLOG_INFO("test input: pins cleared");
      return;
    }
    const bool pin = verb.rfind("pin", 0) == 0, poke = verb.rfind("poke", 0) == 0;
    if (pin || poke) {
      const std::string type = verb.substr(pin ? 3 : 4);
      Pin p{};
      p.width = type == "8" ? 1 : type == "16" ? 2 : type == "32" ? 4 : type == "f" ? 0 : -1;
      words >> address_text >> p.value;
      p.address = uint32_t(std::strtoul(address_text.c_str(), nullptr, 16));
      if (p.width >= 0 && p.address) {
        Write(p);
        if (pin) pins_.push_back(p);
        REXLOG_INFO("test input: {} {:08X} = {} ({})", pin ? "pinned" : "poked", p.address, p.value, verb);
        return;
      }
    }
    REXLOG_WARN("test input: unknown command \"{}\"", line);
  }

  struct Watch {
    uint32_t address = 0;
    int width = 4;  // 1, 4; 0 = float
    uint32_t last = 0;
    bool seen = false;
  };
  const Clock::time_point start_;
  std::vector<Pin> pins_;
  std::vector<Watch> watches_;
  std::mutex mutex_;
  std::vector<Press> presses_;
  std::string file_;
  size_t lines_done_ = 0;
  double last_poll_ = -1;
  uint32_t packet_ = 0;
  uint16_t last_buttons_ = 0;
  uint8_t last_lt_ = 0, last_rt_ = 0;
};

}  // namespace

std::unique_ptr<rex::system::IInputSystem> Create(bool tool_mode) {
  auto input = rex::input::CreateDefaultInputSystem(tool_mode);
  if (!tool_mode && (!REXCVAR_GET(sc5p2_test_buttons).empty() || !REXCVAR_GET(sc5p2_test_buttons_file).empty())) {
    auto pad = std::make_unique<TestPadDriver>();
    if (pad->Setup() == X_STATUS_SUCCESS) {
      input->AddDriver(std::move(pad));
      // Every device feeds player 1, so the scripted pad and a real controller both drive the game.
      input->SetDeviceAssignment(std::make_unique<rex::input::SharedAssignment>());
    }
  }
  return input;
}

}  // namespace sc5p2::test_input
