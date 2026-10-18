# Building sc5p2-nx

How to build the Switch NRO of Space Channel 5 Part 2 from your own copy of the game. The steps below are the ones the
released build was made with, on macOS (Intel); Linux should work the same way with its own paths (untested). sa2-nx's
[building.md](building.md) explains the same machinery for Windows and Sonic Adventure 2 in more depth.

Nothing derived from the game is ever committed: the extracted files (`assets/`), the code generated from `default.xex`
(`app_sc5p2/generated/`), the shaders and the game pack are all made on your computer from your copy.

## What you need

- CMake 3.25 or later, Ninja, clang (Xcode's on macOS), Python 3 with the `xxhash` module (`pip3 install xxhash`).
- [devkitPro](https://devkitpro.org) with devkitA64, libnx and its tools (`nacptool`, `elf2nro`), in `/opt/devkitpro`.
- The NVK driver for the Switch, built as described in [mesa/README.md](../mesa/README.md) (the build of
  [nfsmw-nx](https://github.com/StevensND/nfsmw-nx)): you need the folder `<sdk>/opt/devkitpro/portlibs/switch` it
  installs into.
- [Node.js](https://nodejs.org) 20 or later and a clone of the
  [installer page](https://github.com/BiroGrafx/sc5p2-nx-installer), to make the shader package and the game pack.
- Your game package: `Content/0000000000000000/58410A39/000D0000/B1A42250167C9606EE7B7C6491AFD506B839176858`.

## 1. Extract the game

```sh
python3 tools/stfs_extract.py "<...>/58410A39/000D0000/B1A42250167C9606EE7B7C6491AFD506B839176858" assets/game_root
```

The package is only read. `assets/game_root` then holds `default.xex` and the game's folders (`ch52_g/`...).

## 2. Build the code generator

The code generator is the SDK's `rexglue` tool, built for your computer:

```sh
cmake -S sdk -B out/host -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build out/host --target rexglue
```

It is written to `sdk/out/<host>/rexglue` (`mac-amd64`, `mac-arm64`, `linux-amd64`...), where
`tools/codegen_sc5p2.sh` looks for it; `REXGLUE=<path>` points the script at another one.

## 3. Translate the game's code to C++

```sh
tools/codegen_sc5p2.sh         # app_sc5p2/generated/ from assets/game_root/default.xex (log: out/sc5p2_codegen.log)
tools/post_codegen_sc5p2.sh    # sa2-nx's optimised code generator steps, in order
```

`app_sc5p2/sc5p2_manifest.toml` names the game's files; `overrides.toml` and `codegen_flavor.toml` hold this game's
corrections and options, each with its evidence. Run both scripts again after any change to them. The build makes the
rhythm offset's copy of the dance judge from the generated code by itself (`tools/make_rhythm_offset.py`).

## 4. The Switch NRO

```sh
cmake -S app_sc5p2 -B out/nx-sc5p2 -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=tools/switch/cmake/switch-devkitA64.cmake \
  -DREXGLUE_SWITCH_NVK_SDK=<sdk>/opt/devkitpro/portlibs/switch
cmake --build out/nx-sc5p2 --target sc5p2
```

The result is `out/nx-sc5p2/sc5p2.nro`. The build links the game code and the app with LTO (`SC5P2_SWITCH_LTO`, on by
default; `SC5P2_SWITCH_LTO_JOBS` sets its parallel jobs): the first build takes a long time. The fixed settings
(`app_sc5p2/sc5p2_switch_defaults.toml`) are compiled in, and the player's settings file (`sc5p2_switch.toml`) is
written next to the NRO as `sc5p2.toml` when it is missing. The homebrew menu's icon is `app_sc5p2/icon.jpg` (made from
the game's own `MarketPlace.png`).

## 5. The shader package and the game pack

The installer page makes both from your game with its own code, and the same code runs in Node. In a clone of the
installer, with your NRO and your game package:

```sh
node test/release_node.mjs <game package> <your sc5p2.nro> --write   # its build entry for your NRO (release/manifest.json)
node test/package_node.mjs sc5p2-nx.zip <game package>                # sc5p2.nro, the shaders and the game pack
```

The zip holds `sc5p2-nx/` exactly as it goes on the SD card. The shader package is the one the released NRO was tested
with when its fingerprint in `release/manifest.json` is unchanged by `release_node.mjs`.

(The shaders can also be made with this repository's tools: `tools/collect_shader_files.py` takes the containers of
`ch52_g/ShaderXbox360`, and `tools/build_shader_library_mac.sh` translates and packs them with a DXC of your own. The
13 Direct3D shaders the game builds at run time are made by the installer from `default.xex`: the build entry of its
`release/manifest.json` lists where.)

## 6. The SD card

Copy the `sc5p2-nx` folder of the zip to `sdmc:/switch/`, and launch `sc5p2.nro` with a game override (hold **R**
while starting an installed title) or a forwarder with a **39-bit address space**. The game writes its log to
`logs/` next to the NRO, its pipeline cache to `cache/` and its settings file `sc5p2.toml` at the first start.

## Releasing a new version

1. Build and test the NRO on the console with the shader package the installer makes.
2. In the installer, update its build entry: `node test/release_node.mjs <game package> sc5p2.nro --version <x.y.z>
   --write`, and push it.
3. Publish a release of this repository with `sc5p2.nro` as an asset.
4. Run the installer's "Deploy to GitHub Pages" workflow (Actions tab): it downloads the NRO of the latest release,
   checks it against the build entry and publishes the page.

## A test build on macOS

The same CMake project builds a macOS test build (not a release target; it was used to find the dance judge and to
measure the CPU work):

```sh
cmake -S app_sc5p2 -B out/mac-sc5p2 -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build out/mac-sc5p2 --target sc5p2
```

It reads the extracted game from `assets/game_root`. Test-only settings (in `sc5p2.toml` or as `--key=value` on the
command line): `sc5p2_test_buttons_file` (scripted presses), `sc5p2_test_autoplay_ms` (the notes press themselves
that long after their beat), `sc5p2_diag_rhythm`, `sc5p2_diag_memcpy` and `sc5p2_diag_ring_draw_delay_ns` (a slower
ring thread, to see the dances keep the music's time).
