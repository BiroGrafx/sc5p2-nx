# Space Channel 5 Part 2 (Xbox Live Arcade): static recompilation host application.
#
# Built like sa2-nx's app/ (same SDK, same code generator, same native renderer). The game code is not in this
# repository: run the code generator first, which writes generated/ from your own default.xex together with the CMake
# glue (generated/rexglue.cmake):
#   tools/codegen_sc5p2.sh
# macOS (CPU-side tests; the emulated Xenos GPU through MoltenVK, or the native renderer with sa2_renderer_native):
#   cmake -S app_sc5p2 -B out/mac-sc5p2 -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
#     -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DREXGLUE_ENABLE_TRACY=OFF
# Nintendo Switch (devkitA64 + libnx, the native renderer on NVK; the shader package comes from
# tools/build_shader_library_mac.sh):
#   cmake -S app_sc5p2 -B out/nx-sc5p2 -G Ninja -DCMAKE_BUILD_TYPE=Release \
#     -DCMAKE_TOOLCHAIN_FILE=tools/switch/cmake/switch-devkitA64.cmake -DDEVKITPRO=/opt/devkitpro \
#     -DREXGLUE_SWITCH_NVK_SDK=<mesa horizon sdk>/opt/devkitpro/portlibs/switch -DREXSDK_DIR=$PWD/sdk \
#     -DSA2_GLSLANG_VALIDATOR=<host glslangValidator>

cmake_minimum_required(VERSION 3.25)
project(sc5p2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(REXSDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../sdk" CACHE PATH "Path to the ReXGlue SDK source tree")

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/generated/rexglue.cmake")
    message(FATAL_ERROR "app_sc5p2/generated/rexglue.cmake is missing: run the code generator first")
endif()
include(generated/rexglue.cmake)

set(SC5P2_SOURCES
    src/main.cpp
    # Scripted controller for test runs (sc5p2_test_buttons / sc5p2_test_buttons_file; off by default).
    src/sc5p2_test_input.cpp
    # Test-run video recordings (sc5p2_video_s / sc5p2_video_trigger_file; off by default).
    src/sc5p2_capture.cpp
    # sa2-nx's native renderer (src/me, masseffect-nx's PM4-ring Vulkan renderer as adapted by banjo-nx and sa2-nx),
    # with Space Channel 5 Part 2's hooks: sa2_renderer_native = true replaces the emulated Xenos GPU.
    src/me/me_native_system.cpp
    src/me/me_ring_wait.cpp
    src/me/me_renderdoc.cpp
    src/me/native/sa2_native_draws.cpp
    src/me/native/sa2_deferred_recording.cpp
    src/me/native/sa2_native_targets.cpp
    src/me/native/sa2_native_shaders.cpp
    src/me/native/sa2_native_textures_pool.cpp
    src/me/native/sa2_shader_library.cpp
    src/me/native/sa2_native_glue.cpp
    src/me/sa2_render_mode.cpp
    src/me/sc5p2_me_hooks.cpp
    src/me/sc5p2_api_stub.cpp
    # sa2-nx's switchable enhancements (keys the renderer reads) and the start-up self-check of the AArch64-only forms.
    src/cpu/sa2_enhancements.cpp
    src/cpu/sa2_forms_selfcheck.cpp
    src/cpu/sa2_gpu_boost.cpp
    # The game's Direct3D GPU wait sleeps until the ring moves instead of spinning (sa2_cpu_gpu_wait_sleep).
    src/cpu/sc5p2_gpu_wait.cpp
    src/cpu/sc5p2_input_diag.cpp
    # The game's C runtime memcpy and memset run as host code (sa2_cpu_native_memcpy / sa2_cpu_native_memset).
    src/cpu/sc5p2_hot_crt.cpp
    # The dances' rhythm offset (Bluetooth headphones) and the [rhythm] timing report.
    src/cpu/sc5p2_audio_output.cpp
    # The PC side of sa2-nx's post-codegen self-checks (empty on the Switch).
    src/cpu/sa2_codegen_checks.cpp
)
if(NOT REXGLUE_PLATFORM_SWITCH)
    # Switch profiler counters the native renderer calls (no-ops elsewhere) and the PC sampling profiler.
    list(APPEND SC5P2_SOURCES src/me/me_pc_stubs.cpp src/profiler/sa2_cpu_profile_pc.cpp
                              src/profiler/sa2_thread_cycles_pc.cpp)
endif()
# The codegen flavour (tools/post_codegen_sc5p2.sh writes generated/codegen_flavor.txt) names the build in the first
# "[enh]" log line, as in sa2-nx's app/.
set(SC5P2_FLAVOR_FILE "${CMAKE_CURRENT_SOURCE_DIR}/generated/codegen_flavor.txt")
set(SC5P2_BUILD_FLAVOR "classic codegen")
if(EXISTS "${SC5P2_FLAVOR_FILE}")
    file(READ "${SC5P2_FLAVOR_FILE}" SC5P2_BUILD_FLAVOR)
    string(STRIP "${SC5P2_BUILD_FLAVOR}" SC5P2_BUILD_FLAVOR)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${SC5P2_FLAVOR_FILE}")
endif()
set_source_files_properties(src/cpu/sa2_enhancements.cpp PROPERTIES
    COMPILE_DEFINITIONS "SA2_BUILD_FLAVOR=\"${SC5P2_BUILD_FLAVOR}\"")

add_executable(sc5p2 ${SC5P2_SOURCES})

# The dances' rhythm offset runs a copy of the game's judge update (sub_823EE390) with three insertions. The copy is
# made from the generated code at build time (tools/make_rhythm_offset.py, src/cpu/sc5p2_rhythm_offset.cpp.in), so the
# repository holds no recompiled game code.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(SC5P2_RHYTHM_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/rhythm/sc5p2_rhythm_offset.cpp")
file(GLOB SC5P2_RECOMP_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/generated/default/sc5p2_recomp.*.cpp")
add_custom_command(OUTPUT "${SC5P2_RHYTHM_SOURCE}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/../tools/make_rhythm_offset.py"
            --generated "${CMAKE_CURRENT_SOURCE_DIR}/generated/default"
            --template "${CMAKE_CURRENT_SOURCE_DIR}/src/cpu/sc5p2_rhythm_offset.cpp.in" --out "${SC5P2_RHYTHM_SOURCE}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/cpu/sc5p2_rhythm_offset.cpp.in"
            "${CMAKE_CURRENT_SOURCE_DIR}/../tools/make_rhythm_offset.py" ${SC5P2_RECOMP_SOURCES}
    COMMENT "Making the rhythm offset's copy of the judge (sub_823EE390)"
    VERBATIM)
target_sources(sc5p2 PRIVATE "${SC5P2_RHYTHM_SOURCE}")
set_source_files_properties("${SC5P2_RHYTHM_SOURCE}" PROPERTIES
    INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/generated/default")

# The emulated Xenos GPU stays available as the reference (sa2_renderer_native = false).
rexglue_setup_target(sc5p2 GPU_PLUGINS xenos)

# The renderer's fast draw path calls into the patched NVK through weak references (nvk_switch_*, src/me/native/
# sa2_native_draws.cpp) that are null without it; Apple's linker needs to be told they may stay undefined.
if(APPLE)
    target_link_options(sc5p2 PRIVATE "LINKER:-U,_nvk_switch_draw" "LINKER:-U,_nvk_switch_dibujo"
                                      "LINKER:-U,_nvk_switch_set4" "LINKER:-U,_vk_switch_preload_pipeline")
endif()

if(NOT REXGLUE_PLATFORM_SWITCH AND TARGET imgui)
    target_include_directories(sc5p2 PRIVATE $<TARGET_PROPERTY:imgui,INTERFACE_INCLUDE_DIRECTORIES>)
endif()

# SPIR-V initializers of the native renderer's EDRAM transfer / utility shaders (src/me/native/shaders), compiled on
# the host with glslangValidator (SPIR-V is platform independent: the Switch build uses the same host tool).
find_program(_sc5p2_host_glslang glslangValidator)
if(NOT _sc5p2_host_glslang)
    set(_sc5p2_host_glslang "${CMAKE_CURRENT_SOURCE_DIR}/../out/glslang/StandAlone/glslangValidator${CMAKE_EXECUTABLE_SUFFIX}")
endif()
set(SA2_GLSLANG_VALIDATOR "${_sc5p2_host_glslang}" CACHE FILEPATH "Host glslangValidator")
set(SA2_GENERATED_SHADER_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated-shaders")
set(SA2_EDRAM_SHADER_INCLUDES)
foreach(_shader sa2_edram_7e3_to_rgba8 sa2_edram_rgba8_to_7e3 sa2_edram_16f_to_16f me_resolve_exp_bias me_edram_depth_to_rgba8 me_edram_depth_to_16f me_edram_import me_edram_color_to_depth me_edram_depth_to_depth me_edram_depth_to_stencil me_edram_color_to_stencil me_edram_depth_to_depth_msaa2 me_edram_depth_msaa2_to_depth_1x me_edram_depth_to_stencil_msaa2 me_edram_depth_msaa2_to_stencil_1x me_edram_raw64_to_rgba8 me_edram_rgba8_to_raw64 me_edram_raw64_to_16f me_edram_16f_to_raw64 me_edram_raw64_to_raw64 me_edram_depth_to_raw64 me_edram_raw64_to_depth me_depth_resolve_guestspace me_depth_resolve_guestspace_msaa2 me_edram_stencil_to_buffer me_edram_stencil_msaa2_to_buffer me_edram_color_stencil_to_buffer me_edram_color_to_color me_edram_r64_to_r64 sa2_resolve_copy sa2_depth_resolve_ms)
    set(_extension comp)
    if(_shader STREQUAL "me_edram_import")
        set(_extension vert)
    elseif(_shader MATCHES "^me_edram_(color_to_depth|depth_to_depth|depth_to_stencil|color_to_stencil|depth_to_depth_msaa2|depth_msaa2_to_depth_1x|depth_to_stencil_msaa2|depth_msaa2_to_stencil_1x|raw64_to_depth|color_to_color|r64_to_r64)$" OR _shader STREQUAL "sa2_resolve_copy" OR
           _shader STREQUAL "sa2_depth_resolve_ms")
        set(_extension frag)
    endif()
    set(_source "${CMAKE_CURRENT_SOURCE_DIR}/src/me/native/shaders/${_shader}.${_extension}")
    set(_output "${SA2_GENERATED_SHADER_DIR}/${_shader}.inc")
    set(_target_flags)
    if(_shader MATCHES "^me_edram_(stencil_to_buffer|color_stencil_to_buffer|stencil_msaa2_to_buffer|depth_to_depth_msaa2|depth_msaa2_to_depth_1x|depth_to_stencil_msaa2|depth_msaa2_to_stencil_1x)$" OR _shader STREQUAL "me_depth_resolve_guestspace_msaa2")
        set(_target_flags --target-env vulkan1.2)
        set(_output "${SA2_GENERATED_SHADER_DIR}/${_shader}_generated.inc")
    endif()
    add_custom_command(OUTPUT "${_output}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${SA2_GENERATED_SHADER_DIR}"
        COMMAND "${SA2_GLSLANG_VALIDATOR}" -V ${_target_flags} -x -o "${_output}" "${_source}"
        DEPENDS "${_source}" VERBATIM)
    list(APPEND SA2_EDRAM_SHADER_INCLUDES "${_output}")
endforeach()
add_custom_target(sc5p2_edram_shaders DEPENDS ${SA2_EDRAM_SHADER_INCLUDES})
add_dependencies(sc5p2 sc5p2_edram_shaders)
target_include_directories(sc5p2 PRIVATE
    "${SA2_GENERATED_SHADER_DIR}"
    src
    src/me
    src/me/native
    src/me/native/shaders
    "${REXSDK_DIR}/src/core"
    # Precompiled SPIR-V of the SDK presenter (vulkan_spirv/guest_output_*): the renderer draws into its output.
    "${REXSDK_DIR}/src/ui/shaders")

# CPU-only SDK sources the native renderer reuses (draw-extent estimation, shader microcode interpretation).
target_sources(sc5p2 PRIVATE
    "${REXSDK_DIR}/src/graphics/register_file.cpp"
    "${REXSDK_DIR}/src/graphics/format/ucode.cpp"
    "${REXSDK_DIR}/src/graphics/pipeline/shader/interpreter.cpp")

# The renderer includes xxhash.h and renderdoc_app.h directly; with the SDK built from source they are private to it.
foreach(_dep xxhash renderdoc)
    if(TARGET ${_dep})
        target_include_directories(sc5p2 PRIVATE $<TARGET_PROPERTY:${_dep},INTERFACE_INCLUDE_DIRECTORIES>)
    endif()
endforeach()

# Same as sa2-nx: no FMA contraction (results would drift from the Xbox 360) and no strict aliasing (unsafe for
# recompiled code), on every target that holds generated code.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    foreach(target sc5p2 sc5p2_recomp)
        if(TARGET ${target})
            target_compile_options(${target} PRIVATE -ffp-contract=off -fno-strict-aliasing)
        endif()
    endforeach()
endif()

# Same as sa2-nx: `sqrt` without the errno check in the game code (a compare and a branch to the library after every
# fsqrt). Values are unchanged: nothing reads errno.
option(SC5P2_GAME_NO_MATH_ERRNO "-fno-math-errno on the recompiled game code" ON)
if(SC5P2_GAME_NO_MATH_ERRNO AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND TARGET sc5p2_recomp)
    target_compile_options(sc5p2_recomp PRIVATE -fno-math-errno)
endif()

# Settings template next to the executable, only when there is none yet (the copy there is the user's to edit).
if(NOT REXGLUE_PLATFORM_SWITCH)
    add_custom_command(TARGET sc5p2 POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/sc5p2.toml -DDST=$<TARGET_FILE_DIR:sc5p2>/sc5p2.toml
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/copy_if_missing.cmake"
        VERBATIM)
endif()

# Nintendo Switch (devkitA64 + libnx, static NVK): sa2-nx's Switch build without the parts made from SA2's own
# console sessions (its PGO profile and its hot-function order):
#   - sc5p2_register.cpp is one generated function of SetFunction calls that runs once at startup: -O0 compiles it in
#     seconds (GCC -O3 spent 13-15 minutes on SA2's) and changes nothing but a few microseconds of startup.
#   - LTO of the game code and the app as one program, with -fipa-pta; the link repeats -ffp-contract=off and
#     -fno-strict-aliasing, and xxHash is built with XXH_FORCE_MEMORY_ACCESS=0 (nfsmw-nx's LTO builds got garbage
#     cache keys from its default once it was inlined).
#   - No x29 frame chain in the game code (one more callee-saved register for it); line tables (-g1, DWARF 4) for the
#     app's own code, so console crash addresses map to source lines.
#   - The fixed settings (sc5p2_switch_defaults.toml) compiled in, and the player's sc5p2.toml (sc5p2_switch.toml)
#     written next to the NRO when it is missing.
if(REXGLUE_PLATFORM_SWITCH)
    set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/generated/default/sc5p2_register.cpp" APPEND PROPERTY
        COMPILE_OPTIONS -O0)
    option(SC5P2_SWITCH_LTO "Link-time optimisation of the game code and the app (Switch)" ON)
    set(SC5P2_SWITCH_LTO_JOBS "8" CACHE STRING "Parallel LTRANS jobs of the Switch LTO link")
    target_compile_definitions(sc5p2 PRIVATE XXH_FORCE_MEMORY_ACCESS=0)
    if(SC5P2_SWITCH_LTO)
        foreach(target sc5p2_recomp sc5p2)
            target_compile_options(${target} PRIVATE -flto -fno-fat-lto-objects -fipa-pta)
        endforeach()
        target_link_options(sc5p2 PRIVATE -flto=${SC5P2_SWITCH_LTO_JOBS} -flto-partition=balanced -fipa-pta
                                          -ffp-contract=off -fno-strict-aliasing)
    endif()
    target_compile_options(sc5p2_recomp PRIVATE -fomit-frame-pointer)
    target_compile_options(sc5p2 PRIVATE -g1 -gdwarf-4)
    target_link_options(sc5p2 PRIVATE -g1 -gdwarf-4)
    message(STATUS "sc5p2: Switch LTO ${SC5P2_SWITCH_LTO} (${SC5P2_SWITCH_LTO_JOBS} LTRANS jobs)")

    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/sc5p2_switch_defaults.toml" SC5P2_BUILTIN_CONFIG)
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/sc5p2_switch.toml" SC5P2_PLAYER_CONFIG)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/sc5p2_switch_defaults.toml" "${CMAKE_CURRENT_SOURCE_DIR}/sc5p2_switch.toml")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/src/sc5p2_builtin_config.h.in"
                   "${CMAKE_CURRENT_BINARY_DIR}/builtin_config/sc5p2_builtin_config.h" @ONLY)
    target_include_directories(sc5p2 PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/builtin_config")
    target_compile_definitions(sc5p2 PRIVATE SC5P2_BUILTIN_CONFIG=1)
    # The homebrew menu's entry: name, author, version and icon (app_sc5p2/icon.jpg, 256x256 JPEG, made from the
    # game's own MarketPlace.png banner).
    rexglue_switch_add_nro(sc5p2
        NAME "Space Channel 5 Part 2"
        AUTHOR "SEGA"
        VERSION "1.1.0"
        ICON "${CMAKE_CURRENT_SOURCE_DIR}/icon.jpg")
endif()
