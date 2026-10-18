// Space Channel 5 Part 2 hooks for the native renderer (src/me, sa2-nx's renderer from masseffect-nx through
// banjo-nx). Same role as sa2-nx's app/src/me/sa2_me_hooks.cpp, with Space Channel 5 Part 2's addresses: found by
// tools/d3d_match.py against SA2's named Direct3D functions (out/sa2ref, 2026-10-16; score 1.00 = identical shape
// unless noted):
//
//   - Draw-time shader load sub_821E1530 (SA2's sub_82556A60, 0.91; r3 = device): reads the bound VS at device+12944
//     and the PS at device+12940 (SA2: +13072 / +13068, the same code 128 bytes earlier), then the VS object's +896,
//     +872 and +904 as SA2. The objects are noted before the original patches the VS for the vertex declaration.
//   - Draw records (NoteDrawCall: primitive type, then r5..r7 as Direct3D received them):
//       D3DDevice_DrawVertices        sub_821CF910 (dev, type, start, count)
//       D3DDevice_DrawIndexedVertices sub_821CFD28 (dev, type, base, start, count)
//       D3DDevice_DrawVerticesUP      sub_821CF8C8 (dev, type, count, data, stride)
//   - Shader containers for the library (sc5p2_dump_shaders = true): D3DDevice_CreateVertexShader sub_821CE8F8 and
//     D3DDevice_CreatePixelShader sub_821CE710 receive the blob in r3: [0] flags, [1] header size (the microcode starts
//     there), [2] microcode size. Written whole to shaders/vs_<hash>.bin / ps_<hash>.bin, once each;
//     tools/build_shader_library.sh turns the folder into the shader package.
//
// The ring renderer only for now: sa2-nx's direct-state records and native Direct3D layer (sa2_api_*) are not ported
// (sc5p2_api_stub.cpp). Every hook calls the recompiled original unchanged; no PPC register is touched.

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <system_error>
#include <unordered_set>

#include <fmt/format.h>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc.h>

#define XXH_INLINE_ALL
#include <xxhash.h>

#include "me/me_native_system.h"

REXCVAR_DEFINE_BOOL(sc5p2_dump_shaders, false, "SC5P2",
                    "Write every shader container the game creates to shaders/ next to the executable "
                    "(vs_<hash>.bin / ps_<hash>.bin), for tools/build_shader_library.sh");
REXCVAR_DEFINE_STRING(sc5p2_dump_shaders_dir, "", "SC5P2",
                      "sc5p2_dump_shaders: folder to write to instead of shaders/ next to the executable");

namespace {

inline uint32_t Load32(const uint8_t* base, uint32_t address) {
  uint32_t v;
  std::memcpy(&v, base + address, 4);
  return __builtin_bswap32(v);
}

constexpr uint32_t kDeviceVertexShader = 12944;  // 0x3290
constexpr uint32_t kDevicePixelShader = 12940;   // 0x328C

const bool g_device_layout_set = [] {
  me::native::SetDeviceShaderOffsets(kDeviceVertexShader, kDevicePixelShader);
  return true;
}();

std::mutex g_dump_mutex;
std::unordered_set<uint64_t> g_dumped;

void DumpShaderBlob(const uint8_t* base, uint32_t blob_address, bool vertex) {
  if (!REXCVAR_GET(sc5p2_dump_shaders) || !blob_address) {
    return;
  }
  const uint32_t header = Load32(base, blob_address + 4);
  const uint32_t ucode = Load32(base, blob_address + 8);
  if (header < 12 || header > (1u << 20) || !ucode || ucode > (1u << 20)) {
    REXLOG_WARN("[shaders] implausible {} blob at {:08X} (header {}, microcode {}), skipped", vertex ? "VS" : "PS",
                blob_address, header, ucode);
    return;
  }
  const uint8_t* blob = base + blob_address;
  const size_t size = size_t(header) + ucode;
  const uint64_t hash = XXH3_64bits(blob, size);
  {
    std::lock_guard lock(g_dump_mutex);
    if (!g_dumped.insert(hash).second) {
      return;
    }
  }
  const std::string dir = REXCVAR_GET(sc5p2_dump_shaders_dir);
  const auto folder = dir.empty() ? rex::filesystem::GetExecutableFolder() / "shaders" : std::filesystem::path(dir);
  std::error_code ec;
  std::filesystem::create_directories(folder, ec);
  const auto path = folder / fmt::format("{}_{:016x}.bin", vertex ? "vs" : "ps", hash);
  std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(blob), std::streamsize(size));
  REXLOG_INFO("[shaders] {} {} bytes -> {}", vertex ? "VS" : "PS", size, rex::path_to_utf8(path.filename()));
}

}  // namespace

// Draw-time shader load (FlushState's shader part, called from every Draw*).
REX_EXTERN(__imp__sub_821E1530);
REX_HOOK_RAW(sub_821E1530) {
  const uint32_t device = ctx.r3.u32;
  if (device >= 0x40000000 && device < 0x7F000000) {
    const uint32_t vs = Load32(base, device + kDeviceVertexShader), ps = Load32(base, device + kDevicePixelShader);
    if (!me::native::ShaderObjectsFinished(vs, ps)) {
      me::native::NoteShaderObject(base, vs, true);
      me::native::NoteShaderObject(base, ps, false);
    }
  }
  __imp__sub_821E1530(ctx, base);
}

REX_EXTERN(__imp__sub_821CF910);
REX_HOOK_RAW(sub_821CF910) {  // D3DDevice_DrawVertices
  me::native::NoteDrawCall(base, ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, ctx.r7.u32);
  __imp__sub_821CF910(ctx, base);
}

REX_EXTERN(__imp__sub_821CFD28);
REX_HOOK_RAW(sub_821CFD28) {  // D3DDevice_DrawIndexedVertices
  me::native::NoteDrawCall(base, ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, ctx.r7.u32);
  __imp__sub_821CFD28(ctx, base);
}

REX_EXTERN(__imp__sub_821CF8C8);
REX_HOOK_RAW(sub_821CF8C8) {  // D3DDevice_DrawVerticesUP
  me::native::NoteDrawCall(base, ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, ctx.r7.u32);
  __imp__sub_821CF8C8(ctx, base);
}

REX_EXTERN(__imp__sub_821CE8F8);
REX_HOOK_RAW(sub_821CE8F8) {  // D3DDevice_CreateVertexShader (r3 = the container blob)
  DumpShaderBlob(base, ctx.r3.u32, true);
  __imp__sub_821CE8F8(ctx, base);
}

REX_EXTERN(__imp__sub_821CE710);
REX_HOOK_RAW(sub_821CE710) {  // D3DDevice_CreatePixelShader (r3 = the container blob)
  DumpShaderBlob(base, ctx.r3.u32, false);
  __imp__sub_821CE710(ctx, base);
}
