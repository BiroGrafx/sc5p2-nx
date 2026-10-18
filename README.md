<div align=center>

<img src="app_sc5p2/icon.jpg" alt="Space Channel 5 Part 2" width="30%">

</div>
<h1 align=center>Space Channel 5 Part 2 — Nintendo Switch port</h1>

A native Nintendo Switch port of the Xbox 360 (Xbox Live Arcade) version of **Space Channel 5 Part 2** (2011).

It's not an emulator. The PowerPC code of the game's `default.xex` is statically recompiled to C++ with
[ReXGlue](https://github.com/rexglue/rexglue-sdk) and compiled for the Switch, and the game is drawn by a native Vulkan
renderer running on NVK (Mesa). It runs at the game's **30 FPS**, with the dances in time with the music.

> [!IMPORTANT]
> **sc5p2-nx is a sister port of [sa2-nx](https://github.com/ChanseyIsTheBest/sa2-nx)**, ChanseyIsTheBest's Switch
> port of Sonic Adventure 2. It is built on sa2-nx's SDK, native renderer, code generator steps, tools and installer
> page, set up for this game. None of it would exist without that work: **thank you, ChanseyIsTheBest**, and thank
> you to everyone sa2-nx itself builds on (see [Credits](#credits)).

> [!NOTE]
> You need your own copy of the Xbox Live Arcade game: its content package from an Xbox 360
> (`Content/0000000000000000/58410A39/000D0000/B1A42250167C9606EE7B7C6491AFD506B839176858`).

## How to install

1. Open the installer page: **https://BiroGrafx.github.io/sc5p2-nx-installer/**
2. Choose your game: its **package file** (`B1A42250167C9606EE7B7C6491AFD506B839176858`) or the whole `58410A39`
   folder copied from your Xbox 360. Then press **Create sc5p2-nx.zip**. Everything runs in your browser and the game
   files never leave your computer: the page checks your copy, downloads the build and makes the shaders and the game
   pack from your files.
3. Extract the downloaded `sc5p2-nx.zip` into `sdmc:/switch/`.

Your `sc5p2-nx` folder should look like this:

```text
/switch/sc5p2-nx/
  sc5p2.nro
  LICENSES.txt
  sc5p2.toml                (your settings; written at the first start when it is missing)
  sa2_shaders.mesp          (the game's shaders, made from your copy)
  sa2_shaders.mesp.idx
  game_root.nxpack          (the files of your game, in one file)
  game_root.nxidx
```

Launch it with a game override (hold **R** while starting an installed title) or a forwarder. Album applet mode does
not give the game enough memory. If you use a forwarder, set it to a **39-bit address space**.

To build everything yourself instead, see [docs/building-sc5p2.md](docs/building-sc5p2.md).

> [!NOTE]
> **Updating:** open the installer page again and press **Create sc5p2-nx-update.zip**. It only holds the new
> `sc5p2.nro` and the shaders, so there is no need to copy the game again: extract it over your folder. Your
> `sc5p2.toml`, the game files and the `cache` folder stay.

> [!CAUTION]
> **The first time you play** after installing or updating, the game compiles the graphics pipelines it has not seen
> yet, so **IT CAN STUTTER for a moment** in new places. They are saved in `cache/` on the SD card and later sessions
> start smoothly.

## Controls

By default the face buttons work **by position, as on the Xbox 360 pad** the game was made for: the bottom button is
the Xbox's A (**Chu!**), the right one is the Xbox's B (**Hey!**).

<div align="center">

| Switch | Xbox 360 |
| --- | --- |
| B (bottom) | A |
| A (right) | B |
| Y (left) | X |
| X (top) | Y |
| ZR / ZL | RT / LT |
| R / L | RB / LB |
| + | Start |
| − | Back |
| Sticks / D-pad | Sticks / D-pad |

</div>

To use the buttons by letter instead (the Switch's A acts as A), set `input_xbox_layout = false` in `sc5p2.toml`.
Each player has a button map in `sc5p2.toml` (`[input_p1]` and `[input_p2]`).

## Settings

`sc5p2.toml`, next to the NRO, holds only your options. Everything else is built into the NRO.

<div align="center">

| Setting | What it does |
| --- | --- |
| `sa2_msaa` | Anti-aliasing: `0` off (default), `2` or `4` (smoother edges, more GPU work) |
| `sa2_vsync_relaxed` | `true` (default, keep it): the vblanks follow the frames |
| `sc5p2_rhythm_offset_ms` | Rhythm offset for the speaker, wired headphones and the TV: `0` (default) is the game's own timing |
| `sc5p2_rhythm_offset_bluetooth_ms` | Rhythm offset used while Bluetooth headphones play the sound: `300` (default) |
| `input_xbox_layout` | Face buttons by position (`true`, default) or by letter (`false`) |
| `input_joycon_sideways` | Single Joy-Cons held sideways, one per player (`true`, default) |
| `[input_p1]`, `[input_p2]` | Each player's buttons: for every Xbox button, the Switch buttons that press it, e.g. `a = "B, ZR"` |

</div>

If you delete `sc5p2.toml`, the game writes the default one again at the next start.

### Bluetooth headphones

Bluetooth headphones play the sound a few hundred milliseconds late, so every press made on the beat you hear reaches
the game late. The port notices when the console's audio goes to Bluetooth and judges the dances' presses 300 ms
later while it does, so the moves count again; the speaker, wired headphones and the TV keep the game's own timing.
Ulala's answers to your presses still sound late through the headphones (the sound itself cannot arrive sooner), so
wired audio feels best. If your headphones' presses still come out late or early, raise or lower
`sc5p2_rhythm_offset_bluetooth_ms`; every 30 s of dancing the log (`logs/` next to the NRO) says, in a `[rhythm]` line,
how far from the beat your presses landed.

## Resolution

The game draws **1280x720** in handheld mode and **1920x1080** docked, and switches when you dock or undock.

## Performance

The game runs at 30 FPS. It was tested on an original (Erista) Switch with the CPU at **1785 MHz** and the GPU at
**460 MHz** in handheld mode (a [sys-clk](https://github.com/retronx-team/sys-clk) profile for the title you launch it
from), through the whole game including the Report 4 boss. At the stock 1020 MHz the busiest scenes (the crowded
dances, the final boss) drop frames.

The dance stays in time with the music even when a frame is late: the game counts its time in vblanks, and the port
gives a late frame every vblank that really passed. Frames are handed to the screen on its own refresh (the display's
vsync event), so a steady 30 FPS looks steady.

> [!CAUTION]
> Overclocking pushes the console beyond what it was designed for and you do it at your own risk.

## How it works

- **The game's code is translated, not emulated.** [ReXGlue](https://github.com/rexglue/rexglue-sdk) translates every
  function of the game's program to C++, which is then compiled for the Switch, with sa2-nx's code generator steps
  (registers kept in C++ locals, direct calls, LTO).
- **sa2-nx's renderer.** The game's frames are drawn with Vulkan by the native renderer of sa2-nx (which grew from
  masseffect-nx's), fed by the game's command ring.
- **Shaders translated beforehand.** The game's GPU programs, and Direct3D's built-in ones from the executable, are
  translated with [XenosRecomp](https://github.com/hedge-dev/XenosRecomp) and DXC into one package, on the installer
  page or with the build tools.
- **Graphics driver.** Vulkan runs on NVK, from the Mesa project, through
  [mesa-switch](https://github.com/danfromtico/mesa-switch), in the build of
  [nfsmw-nx](https://github.com/StevensND/nfsmw-nx) (see [mesa/README.md](mesa/README.md)).
- **What this port adds** (all in [app_sc5p2/](app_sc5p2/)):
  - vblanks that follow the frames, kept on the screen's own refresh, and a late frame counting every vblank that
    passed (`src/me/me_native_system.cpp`): the dances keep the music's time and 30 FPS look steady;
  - the rhythm offset for Bluetooth headphones: the dance judge (the game's task `ShInput<Judge>`) runs as a copy made
    from the generated code at build time, with its clock moved (`src/cpu/sc5p2_rhythm_offset.cpp.in`,
    `tools/make_rhythm_offset.py`), and the console's audio output read with audctl (`src/cpu/sc5p2_audio_output.cpp`);
  - the game's C runtime `memcpy` and `memset` as sa2-nx's native versions (`src/cpu/sc5p2_hot_crt.cpp`);
  - Direct3D's GPU waits sleeping instead of spinning (`src/cpu/sc5p2_gpu_wait.cpp`), and the game's analysis
    corrections for the code generator (`overrides.toml`, `codegen_flavor.toml`).

## Build

macOS or Linux, CMake, Ninja, Python and clang for the code generator; devkitA64 and libnx from
[devkitPro](https://devkitpro.org) and NVK built as described in [mesa/README.md](mesa/README.md) for the Switch; and
your own copy of the game for the code generator. The whole process is in
[docs/building-sc5p2.md](docs/building-sc5p2.md).

<div align="center">

| Folder | Contents |
| --- | --- |
| `app_sc5p2/` | This game: hooks, settings, the rhythm offset, the CMake project (with sa2-nx's native renderer) |
| `app/` | sa2-nx's Sonic Adventure 2 app, kept as the reference `app_sc5p2/` was adapted from |
| `sdk/` | ReXGlue SDK with sa2-nx's Horizon layer and code generator changes |
| `shaders/` | XenosRecomp with sa2-nx's changes, and the shader package tools |
| `tools/` | Code generation steps, build scripts, analysis tools (the `*_sc5p2*` ones are this port's) |
| `patches/`, `pgo/`, `mesa/` | sa2-nx's third-party diffs, its PGO profile, and the NVK driver notes |
| `docs/` | sa2-nx's documentation (start with [docs/porting-guide.md](docs/porting-guide.md)) and this port's build guide |

</div>

## Credits

- **SEGA and United Game Artists** — creators of Space Channel 5 Part 2. This is an unofficial, fan-made port with no
  affiliation.
- **[ChanseyIsTheBest](https://github.com/ChanseyIsTheBest/sa2-nx)** — **sa2-nx** and
  **[sa2-nx-installer](https://github.com/ChanseyIsTheBest/sa2-nx-installer)**, which this port is built on: the
  SDK's Switch work, the native renderer, the code generator steps, the tools, the documentation and the installer
  page. Thank you!
- **[Tom Clay](https://github.com/rexglue/rexglue-sdk)** — the ReXGlue SDK, built on the work of the
  **[Xenia](https://xenia.jp)** team.
- **[StevensND](https://github.com/StevensND/nfsmw-nx)** — nfsmw-nx: the Switch layer of the SDK, the NVK build and
  much of the method; and [nfsmw-nx-installer](https://github.com/StevensND/nfsmw-nx-installer), which the installer
  page grew from.
- **[nebadasSwifty](https://github.com/nebadasSwifty/masseffect-nx)** — masseffect-nx: the native renderer sa2-nx's
  grew from.
- **[hedge-dev](https://github.com/hedge-dev/XenosRecomp)** — XenosRecomp, the Xenos shader translator.
- **[danfromtico](https://github.com/danfromtico/mesa-switch)** and **[NaGaa95](https://github.com/NaGaa95/mesa-switch)**
  — mesa-switch: Mesa, NVK and NAK on Horizon.
- **[devkitPro](https://devkitpro.org) and [switchbrew](https://github.com/switchbrew/libnx)** — devkitA64 and libnx.
- **Claude** (Anthropic) — an AI assistant, used as a helper tool for this port.
- sa2-nx's sibling ports, which share their fixes with it, and the other libraries listed in
  [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Legal

No affiliation with SEGA, Microsoft or Nintendo. "Space Channel 5" and "Ulala" are trademarks of SEGA; the icon
artwork is © SEGA. This repository contains no assets or program code from the original game. Users must provide
their own legally obtained copy, and everything made from it (`assets/`, `app_sc5p2/generated/`, the shader package,
the game pack) is made on their own computer. Running homebrew requires custom firmware, which violates Nintendo's ToS
and can get a console banned — your call.

Source code is provided under the GPL-3.0 License (see [LICENSE](LICENSE)), like sa2-nx and the ports it builds on.
The SDK keeps its BSD-3-Clause license (see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)).
