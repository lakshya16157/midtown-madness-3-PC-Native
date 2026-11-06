cmake_minimum_required(VERSION 3.20)
project(mm3_recomp C CXX)
if(WIN32)
    enable_language(RC)
endif()

# The recompiled game code uses x86 SIMD intrinsics for the guest's MMX/SSE,
# and the runtime traps hardware registers by decoding x86-64 instructions,
# so the host must be x86-64 (Windows, Linux or an Intel Mac).
if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
    message(FATAL_ERROR "MM3 needs an x86-64 host; this is ${CMAKE_SYSTEM_PROCESSOR}")
endif()

# Recompiled x86 code relies on what MSVC does anyway: signed arithmetic wraps
# like the CPU's, and guest memory is read through whatever pointer type an
# instruction implies. GCC and Clang optimise both away as undefined
# behaviour unless told not to.
if(NOT MSVC)
    add_compile_options(-fwrapv -fno-strict-aliasing)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(XBOXRECOMP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tools/xboxrecomp" CACHE PATH
    "Path to the local xboxrecomp toolkit")
set(RECOMP_GEN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/recomp/gen" CACHE PATH
    "Directory containing generated recompilation C sources")
option(MM3_RECOMP_ABI_CHECK "Check guest callee-saved register contracts" OFF)
option(MM3_MOVIE_INPUT_TRACE "Observe movie completion and skip input" OFF)
option(MM3_OFFLINE_XNET "Bypass MM3 network readiness for offline startup" ON)
option(MM3_UNLOCK_ALL_CARS "Include the optional all-car unlock override" ON)
option(MM3_FRONTEND_WATCH "Trace writes to the RootLevel child pointer" OFF)
option(MM3_TRACY "Profile the host rendering pipeline with Tracy" OFF)
option(MM3_NATIVE_FENCE_WAIT "Wait on native GPU notifications in the direct guest fence loop" ON)
option(MM3_DSP_AUTO_INLINE "Allow automatic inlining in the optimized DSP interpreter" ON)
if(MM3_TRACY)
    set(MM3_TRACY_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tools/bin/tracy-0.14.1" CACHE PATH "Tracy 0.14.1 sources")
    set(TRACY_ENABLE ON CACHE BOOL "Enable Tracy client" FORCE)
    set(TRACY_ON_DEMAND ON CACHE BOOL "Record only while a profiler is connected" FORCE)
    add_subdirectory("${MM3_TRACY_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/tracy" EXCLUDE_FROM_ALL)
endif()

# Profile-guided optimisation (MSVC Release/RelWithDebInfo), in a build
# folder of its own; the code it produces computes exactly the same results.
#   cmake -S . -B build-pgo -G "Ninja Multi-Config" -DMM3_PGO=GEN
#     instrumented build: play it, exit normally (writes mm3_recomp!N.pgc)
#   cmake -B build-pgo -DMM3_PGO=USE    then build: optimised with that profile
# Every C source of the game and the toolkit is compiled with /GL, so both the
# generated game code and the renderer are laid out for the measured run.
set(MM3_PGO "" CACHE STRING "MSVC profile-guided optimisation: empty (off), GEN or USE")
set(MM3_PGO_PGD "${CMAKE_BINARY_DIR}/pgo/mm3_recomp.pgd" CACHE FILEPATH
    "Profile database for MM3_PGO")
if(MM3_PGO)
    if(NOT MSVC OR NOT MM3_PGO MATCHES "^(GEN|USE)$")
        message(FATAL_ERROR "MM3_PGO must be GEN or USE, with MSVC")
    endif()
    add_compile_options("$<$<NOT:$<CONFIG:Debug>>:/GL>")
    foreach(pgo_config RELEASE RELWITHDEBINFO)
        string(APPEND CMAKE_STATIC_LINKER_FLAGS_${pgo_config} " /LTCG")
        string(REGEX REPLACE "/INCREMENTAL(:NO)?" "" CMAKE_EXE_LINKER_FLAGS_${pgo_config}
            "${CMAKE_EXE_LINKER_FLAGS_${pgo_config}}")
        string(APPEND CMAKE_EXE_LINKER_FLAGS_${pgo_config} " /INCREMENTAL:NO /LTCG /CGTHREADS:8")
    endforeach()
    get_filename_component(pgo_folder "${MM3_PGO_PGD}" DIRECTORY)
    file(MAKE_DIRECTORY "${pgo_folder}")
    if(MM3_PGO STREQUAL "USE" AND NOT EXISTS "${MM3_PGO_PGD}")
        message(FATAL_ERROR "MM3_PGO=USE needs the trained profile ${MM3_PGO_PGD}")
    endif()
endif()

# The audio DSP programs MM3 runs, saved there by the game (src/main.c; setup.py
# captures them from a short run) and recompiled to C by each build; derived
# from the user's game like src/recomp/gen. They remain local derived data: an
# empty directory still builds the interpreter, which can capture them on first run.
set(XBOXRECOMP_DSP_PROGRAM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/game_files/dsp" CACHE PATH
    "Local GP/EP program captures for native DSP compilation")
# MM3 owns its depth-view compatibility shader; both host renderers use it.
set(XBOXRECOMP_NV2A_SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/mods/shaders/nv2a")
# Combiner configurations whose specialised shaders are built with the game:
# Wine/Proton (the Steam Deck) cannot compile them (tools/shader_states).
set(XBOXRECOMP_PS_STATES "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/mm3_ps_states.txt")
if(NOT WIN32)
    # The MM3 runtime patch includes <windows.h> in a few runtime files too: give them the
    # runtime's own POSIX Win32 layer (src/compat/posix/windows.h).
    include_directories(BEFORE ${CMAKE_CURRENT_SOURCE_DIR}/src/compat/posix)
endif()
add_subdirectory(${XBOXRECOMP_DIR} ${CMAKE_BINARY_DIR}/xboxrecomp)
# MM3 VP-only playback was measured about 10 dB above GP output (-10.7 vs
# -20.7 dBFS in a race). Keep its existing calibration outside the runtime.
target_compile_definitions(xbox_apu PRIVATE XBOXRECOMP_APU_VP_MONITOR_GAIN=0.316f)
if(MSVC AND MM3_DSP_AUTO_INLINE)
    # RelWithDebInfo defaults to /Ob1, which leaves small non-inline interpreter
    # helpers as calls. Override only this measured hot source, retaining its
    # existing arithmetic and Debug build behavior.
    set_property(SOURCE "${XBOXRECOMP_DIR}/src/apu/dsp/interp/dsp_cpu.c"
        TARGET_DIRECTORY xbox_apu APPEND PROPERTY
        COMPILE_OPTIONS "$<$<NOT:$<CONFIG:Debug>>:/Ob2>")
endif()


# Settings launcher (src/mm3_launcher.cpp): RmlUi on its Win32/D3D11 backend,
# or with the SDL3 GPU renderer its SDL3 platform and SDL GPU renderer;
# FreeType for its fonts. Static, fetched once at configure time.
include(FetchContent)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)   # archive timestamps: extraction time (CMake 3.24+)
endif()
FetchContent_Declare(freetype
    URL https://github.com/freetype/freetype/archive/refs/tags/VER-2-13-3.tar.gz
    URL_HASH SHA256=bc5c898e4756d373e0d991bab053036c5eb2aa7c0d5c67e8662ddc6da40c4103)
FetchContent_Declare(rmlui
    URL https://github.com/mikke89/RmlUi/archive/refs/tags/6.3.tar.gz
    URL_HASH SHA256=d977298bb6147610e5984d5db85ddf284020d655a8713913f6982074f1dbdede)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
set(BUILD_SHARED_LIBS OFF)
set(SKIP_INSTALL_ALL ON)
foreach(ft_dep ZLIB BZIP2 PNG HARFBUZZ BROTLI)
    set(FT_DISABLE_${ft_dep} ON)
endforeach()
FetchContent_MakeAvailable(freetype)
add_library(Freetype::Freetype ALIAS freetype)
FetchContent_MakeAvailable(rmlui)
FetchContent_Declare(imgui
    URL https://github.com/ocornut/imgui/archive/refs/tags/v1.91.9b.tar.gz
    URL_HASH SHA256=8e1bbc76c71d74fef2fb85db7e7ca8eba13d6a86623c54992b60162db554ffdb)
FetchContent_MakeAvailable(imgui)
if(XBOXRECOMP_RENDERER STREQUAL "SDL3_GPU")
    # RmlUi's SDL GPU renderer loads textures through SDL3_image; the
    # launcher's images need no external codecs.
    set(SDLIMAGE_VENDORED OFF CACHE BOOL "" FORCE)
    set(SDLIMAGE_DEPS_SHARED OFF CACHE BOOL "" FORCE)
    set(SDLIMAGE_SAMPLES OFF CACHE BOOL "" FORCE)
    set(SDLIMAGE_TESTS OFF CACHE BOOL "" FORCE)
    set(SDLIMAGE_INSTALL OFF CACHE BOOL "" FORCE)
    set(SDLIMAGE_PNG_LIBPNG OFF CACHE BOOL "" FORCE)
    foreach(format AVIF JXL TIF WEBP)
        set(SDLIMAGE_${format} OFF CACHE BOOL "" FORCE)
    endforeach()
    FetchContent_Declare(SDL3_image
        URL https://github.com/libsdl-org/SDL_image/releases/download/release-3.4.6/SDL3_image-3.4.6.tar.gz
        URL_HASH SHA256=d2e4637ae700f72e5196b8fbd749850ed2e5e1e09c5a5be8d06ff55aaccf3b01)
    FetchContent_MakeAvailable(SDL3_image)
    set(MM3_RMLUI_BACKEND
        ${rmlui_SOURCE_DIR}/Backends/RmlUi_Platform_SDL.cpp
        ${rmlui_SOURCE_DIR}/Backends/RmlUi_Renderer_SDL_GPU.cpp
        ${rmlui_SOURCE_DIR}/Backends/RmlUi_Backend_SDL_GPU.cpp)
    set(MM3_RMLUI_BACKEND_LIBS SDL3::SDL3 SDL3_image::SDL3_image)
    set(MM3_IMGUI_BACKEND
        ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlgpu3.cpp)
    set(MM3_IMGUI_BACKEND_LIBS SDL3::SDL3)
    set(MM3_D3D11_LIBS)
else()
    set(MM3_RMLUI_BACKEND
        ${rmlui_SOURCE_DIR}/Backends/RmlUi_Platform_Win32.cpp
        ${rmlui_SOURCE_DIR}/Backends/RmlUi_Renderer_DX11.cpp
        ${rmlui_SOURCE_DIR}/Backends/RmlUi_Backend_Win32_DX11.cpp)
    set(MM3_RMLUI_BACKEND_LIBS d3d11 dxgi d3dcompiler dwmapi)
    set(MM3_IMGUI_BACKEND
        ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp)
    set(MM3_IMGUI_BACKEND_LIBS d3d11 d3dcompiler dwmapi)
    set(MM3_D3D11_LIBS d3d11 dxgi d3dcompiler dwmapi)
endif()
# Windows' pad API stays XInput with either renderer; elsewhere pads come
# through SDL3 (tools/xboxrecomp/src/input).
if(WIN32)
    set(MM3_WIN32_LIBS xinput user32)
    set(MM3_LIBM)
else()
    set(MM3_WIN32_LIBS)
    # The platform layer's fenv calls need libm (the xboxrecomp umbrella adds it
    # too, but the focused checks link only the layers they use).
    set(MM3_LIBM m)
endif()
add_library(mm3_imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${MM3_IMGUI_BACKEND})
target_include_directories(mm3_imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(mm3_imgui PUBLIC ${MM3_IMGUI_BACKEND_LIBS})
if(MM3_TRACY)
    foreach(profile_target xbox_kernel xbox_video)
        target_link_libraries(${profile_target} PRIVATE Tracy::TracyClient)
        target_compile_definitions(${profile_target} PRIVATE RECOMP_TRACY=1)
    endforeach()
endif()

file(GLOB RECOMP_GEN_SOURCES CONFIGURE_DEPENDS
    "${RECOMP_GEN_DIR}/*.c")
# Setup reuses generated functions on an existing checkout. Refresh the
# project's compatibility header so pulling fixes does not require regeneration.
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/src/mm3_recomp_types.h"
    "${RECOMP_GEN_DIR}/recomp_types.h" COPYONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/tools/xboxrecomp/templates/runtime/recomp_types.h"
    "${RECOMP_GEN_DIR}/recomp_base.h" COPYONLY)

# Generated functions move between split chunks whenever the generator's
# function list changes, so find each project hook's owner instead of naming
# a chunk. Hooks rename the generated definition to <name>_original and the
# project supplies <name>.
set(MM3_RENAMED_FUNCTIONS
    sub_00170E5D sub_000145DC sub_001F02AD
    sub_0025968E                 # widescreen: wraps the projection-matrix builder
    sub_001DB6E8 sub_001F2A71 sub_001F443D  # skip intro: movie play, frame, skip check
)
if(MM3_MOVIE_INPUT_TRACE)
    list(APPEND MM3_RENAMED_FUNCTIONS sub_0035BDC0)
endif()
if(MM3_OFFLINE_XNET)
    list(APPEND MM3_RENAMED_FUNCTIONS sub_0019531D)
endif()
if(MM3_FRONTEND_WATCH)
    list(APPEND MM3_RENAMED_FUNCTIONS sub_000FA37E sub_0011935B sub_001253F4 sub_0008E420 sub_0008E3C0)
    set_property(SOURCE "src/recomp_manual.c" APPEND PROPERTY
        COMPILE_DEFINITIONS "MM3_HEAP_WATCH_WRAPPER=1")
endif()
# Render distance renames definitions only: renaming every reference with a
# compile definition would bypass the hooks for callers in the same chunk.
set(MM3_DISTANCE_FUNCTIONS sub_000D9E2E sub_000E2D5B sub_000F4AC3 sub_000FE71E
    sub_001661D0 sub_001633B7 sub_002256C7 sub_002203E5 sub_0022F298
    sub_00248518                     # flare capacity (src/mods/draw_distance.c)
    sub_001A6CE3                     # occlusion clip distance: the small-prop size cull
    sub_00162EBA sub_00162EF3)       # facade detail/far stream use (mm3_facade_streams.c)
set(MM3_DEFINITION_ONLY_FUNCTIONS ${MM3_DISTANCE_FUNCTIONS})
# Repair newly decoded collision cells once, before the native queries use them.
list(APPEND MM3_DEFINITION_ONLY_FUNCTIONS sub_0015A929)
# Ordered host depth fog and its optional horizon replacement.
list(APPEND MM3_DEFINITION_ONLY_FUNCTIONS sub_000F8DCE sub_000F8750)
# Car damage: src/mm3_damage_diagnostics.c logs each scaled hit and keeps every
# car's impulse over an Xbox frame's four physics steps for the damage step.
list(APPEND MM3_DEFINITION_ONLY_FUNCTIONS sub_00021DCF sub_001CDB6C)
# Launcher time limits scale the scripts' race_timer and its bonuses
# (src/mm3_time_limits.c).
list(APPEND MM3_DEFINITION_ONLY_FUNCTIONS sub_001F5F24 sub_001F5A5A)
# One-frame light volume jumps are watched at the native volume builder
# (src/mm3_light_watch.c, [MM3_LIGHT_JUMP]).
list(APPEND MM3_DEFINITION_ONLY_FUNCTIONS sub_00013068)
# The language chosen in Game Setup survives a restart (src/mm3_language.c).
list(APPEND MM3_DEFINITION_ONLY_FUNCTIONS sub_001193A6 sub_00124822)
# The downloadable races' own route pictures (src/mm3_dlc_races.c).
list(APPEND MM3_DEFINITION_ONLY_FUNCTIONS sub_002430FD)
# Slipstream drafting and the roll guard on every car's integrate step
# (src/mm3_vehicle_dynamics.c).
list(APPEND MM3_DEFINITION_ONLY_FUNCTIONS sub_001D7441)
# F8 free camera: the race view's world-to-camera matrix (src/mm3_free_camera.c).
list(APPEND MM3_DEFINITION_ONLY_FUNCTIONS sub_001EA16C)
# F2 holds the race still: the engine timer's counter read (src/mm3_pause_clock.c).
list(APPEND MM3_DEFINITION_ONLY_FUNCTIONS sub_0016FC89)
# Opponents' difficulty: the race opponents' Normal driving state (src/mm3_ai_difficulty.c).
list(APPEND MM3_DEFINITION_ONLY_FUNCTIONS sub_0014194C)
# Lua natives this port adds to sub_0020646F's list (src/mm3_lua_natives.c).
list(APPEND MM3_DEFINITION_ONLY_FUNCTIONS sub_0020646F)
# Screen-widget draws skip links to destroyed widgets, and a destroyed menu
# screen is let go by the menu still holding it (src/mm3_ui_guard.c).
list(APPEND MM3_DEFINITION_ONLY_FUNCTIONS sub_000C2BFB sub_001C3C2D sub_0011965D)
# Native menu layout, focus, activation and composite control geometry.
list(APPEND MM3_DEFINITION_ONLY_FUNCTIONS sub_001C5935 sub_000C2FA7 sub_00246894
    sub_001118C7 sub_00111CBF sub_001C1585 sub_00103E41 sub_00103FBD sub_001038D0
    sub_0010DDC0 sub_001C44F8 sub_00117B03 sub_00117C99 sub_00117D11
    sub_00104F7B sub_0010514E sub_0010553A)
# The main loop and world update can share a generated chunk. Rename only
# these definitions so direct and same-chunk calls enter the clocked wrappers.
set(MM3_SIMULATION_FUNCTIONS sub_00025422 sub_0002545A sub_001E7590 sub_001EC8E6
    sub_0022DBBF)                # and the race AI's update, kept at 30 Hz
list(APPEND MM3_DEFINITION_ONLY_FUNCTIONS ${MM3_SIMULATION_FUNCTIONS})
# Building diagnostics keep same-chunk calls routed through opt-in wrappers.
set(MM3_BUILDING_FUNCTIONS sub_00168A77 sub_001C7B15 sub_001A67BA sub_00166E01
    sub_001681FB sub_00167DA5 sub_00166B3E sub_00162F75)
list(APPEND MM3_DEFINITION_ONLY_FUNCTIONS ${MM3_BUILDING_FUNCTIONS})
# Detach struck trees into the native rigid-body street-prop controller.
list(APPEND MM3_DEFINITION_ONLY_FUNCTIONS sub_0016AEDB sub_000E1F49 sub_001D137B)
list(APPEND MM3_DEFINITION_ONLY_FUNCTIONS sub_00198592 sub_001986B1)
# Preserve same-chunk near-camera calls to the chase-distance adjustment.
list(APPEND MM3_DEFINITION_ONLY_FUNCTIONS sub_001EEC53)
# Refresh each active camera after a live display-aspect change.
list(APPEND MM3_DEFINITION_ONLY_FUNCTIONS sub_000F4BB1)
# Cache map texture ownership at native load/reconfiguration/destruction boundaries.
list(APPEND MM3_DEFINITION_ONLY_FUNCTIONS sub_0011C742 sub_0011CAAC sub_0007CEC3)
# Preserve same-chunk locale calls to the custom vehicle-name extension.
list(APPEND MM3_DEFINITION_ONLY_FUNCTIONS sub_0011C194)
# Circuit mode lists only the circuit races, with their own picture strip;
# Checkpoint lists the rest.
list(APPEND MM3_DEFINITION_ONLY_FUNCTIONS sub_001238B7 sub_00103BE1 sub_001038D0)
# The profile box's Checkpoint line and the post-race "Next race" leave
# circuits out outside the Circuit mode.
list(APPEND MM3_DEFINITION_ONLY_FUNCTIONS sub_001E0F69 sub_001E4F97)
# [MM3_POLICE] names why a city's police pool stays empty: the AI setup and
# the police fill (src/mm3_police_log.c; car creation steps by seam).
list(APPEND MM3_DEFINITION_ONLY_FUNCTIONS sub_00158291 sub_001507DA)
# Back from a circuit race the menu returns to the Circuit screens.
list(APPEND MM3_DEFINITION_ONLY_FUNCTIONS sub_00120A9A)
# Race script errors are printed to the log as well.
list(APPEND MM3_DEFINITION_ONLY_FUNCTIONS sub_0020F684)
# Native graphics pages retain the retail Meme parser and controller UI.
list(APPEND MM3_DEFINITION_ONLY_FUNCTIONS
    sub_00119AEE sub_00102168 sub_00102191 sub_001021B5 sub_0006EA22
    sub_001C5E11 sub_0006B3EF sub_0011C650)
# Preserve same-chunk catalog calls to the retired-car selection fallback.
list(APPEND MM3_DEFINITION_ONLY_FUNCTIONS sub_001DDFC5)
# Keep retail vehicles first and sort custom/DLC menu entries by top speed;
# race opponents get the Xbox cars, never the installer's extra cars.
# The profile box counts owned cars from the same list as its total.
list(APPEND MM3_DEFINITION_ONLY_FUNCTIONS sub_001DE752 sub_001DE012 sub_001DE995)
# The CRT MMX probe (sub_00093770) is lifted by the fork's generator now, with
# CPUID answered by the runtime, so it is no longer replaced here.
# The CRT remainder loop tests FPREM's C2, independently of a previous FCMP.
list(APPEND MM3_DEFINITION_ONLY_FUNCTIONS sub_00253748)
# Preserve the merged zero-flag tests in the native pedestrian arrival query.
list(APPEND MM3_DEFINITION_ONLY_FUNCTIONS sub_001466D9)
# Carry projected name-marker identity through the native UI vertex packer.
list(APPEND MM3_DEFINITION_ONLY_FUNCTIONS sub_001093BB sub_00069807)
# The hot visibility leaf keeps its balanced x87 scratch stack in native locals.
list(APPEND MM3_DEFINITION_ONLY_FUNCTIONS sub_001C793A)
# Keep the matrix leaf's x87 scratch in scalars, with retail exceptional paths.
list(APPEND MM3_DEFINITION_ONLY_FUNCTIONS sub_00259982)
# Scope even world lighting to the native full-scene composite and its colors.
list(APPEND MM3_DEFINITION_ONLY_FUNCTIONS sub_000FF19A)
list(APPEND MM3_DEFINITION_ONLY_FUNCTIONS sub_000FF324 sub_000FF15C sub_00230DEE)
# Local light sources retain native activation, with forward car beams and
# source-sized illumination around vehicle and city lamps.
list(APPEND MM3_DEFINITION_ONLY_FUNCTIONS sub_000CF2DA sub_000489A6 sub_00021E18)
# Lamp glows clear their own lens/housing: lift appended glow points toward the eye.
list(APPEND MM3_DEFINITION_ONLY_FUNCTIONS sub_001A831B)
# Pair each driven vehicle with its lamp graph so its own headlight switch governs.
list(APPEND MM3_DEFINITION_ONLY_FUNCTIONS sub_000DFE0E)
# Expire car-light caches at the native queue reset rather than rescanning
# every queued light at each lamp callback; retain same-chunk wrapper calls.
list(APPEND MM3_DEFINITION_ONLY_FUNCTIONS sub_0022FDFD)
# Factory profiles bind the real constructed controller and leave retail cars
# on their original torque and gearbox paths, including same-chunk calls.
list(APPEND MM3_DEFINITION_ONLY_FUNCTIONS sub_0021ED64 sub_001D0E2D sub_001CB7E8 sub_001CB881 sub_001CB9A5 sub_001CF7B4)
if(MM3_UNLOCK_ALL_CARS)
    list(APPEND MM3_DEFINITION_ONLY_FUNCTIONS sub_001DEA30)
endif()

list(REMOVE_DUPLICATES MM3_DEFINITION_ONLY_FUNCTIONS)
list(JOIN MM3_RENAMED_FUNCTIONS "|" hook_names)
list(JOIN MM3_DEFINITION_ONLY_FUNCTIONS "|" distance_names)
set(MM3_SEAM_FUNCTIONS sub_00344900 sub_003449B0 sub_0013FE96 sub_0014E808)
include("${CMAKE_CURRENT_SOURCE_DIR}/scripts/mm3_dma_publish_seam.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/scripts/mm3_collision_call_seam.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/scripts/mm3_ai_query_stack_seam.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/scripts/mm3_masked_visibility_seam.cmake")
list(APPEND MM3_SEAM_FUNCTIONS sub_0013AB27)
include("${CMAKE_CURRENT_SOURCE_DIR}/scripts/mm3_route_diagnostics_seam.cmake")
list(APPEND MM3_SEAM_FUNCTIONS sub_0024FA01 sub_0024F917 sub_0024F964)
include("${CMAKE_CURRENT_SOURCE_DIR}/scripts/mm3_route_cancel_seam.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/scripts/mm3_apu_mmio_seam.cmake")
list(APPEND MM3_SEAM_FUNCTIONS sub_0027DCDA sub_0027DD72)
include("${CMAKE_CURRENT_SOURCE_DIR}/scripts/mm3_audio_resource_pool_seam.cmake")
list(APPEND MM3_SEAM_FUNCTIONS sub_0017BB4B)
list(APPEND MM3_SEAM_FUNCTIONS sub_00224A07)
include("${CMAKE_CURRENT_SOURCE_DIR}/scripts/mm3_city_streaming_seam.cmake")
list(APPEND MM3_SEAM_FUNCTIONS sub_0023F09B)
include("${CMAKE_CURRENT_SOURCE_DIR}/scripts/mm3_speedometer_seam.cmake")
list(APPEND MM3_SEAM_FUNCTIONS sub_0011CA25 sub_0011D1E8 sub_0011D808 sub_0011CAE8 sub_0011DCBF)
include("${CMAKE_CURRENT_SOURCE_DIR}/scripts/mm3_map_zoom_seam.cmake")
list(APPEND MM3_SEAM_FUNCTIONS sub_001EC352)
include("${CMAKE_CURRENT_SOURCE_DIR}/scripts/mm3_hud_cleanup_seam.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/scripts/mm3_name_marker_seam.cmake")
list(APPEND MM3_SEAM_FUNCTIONS sub_000E10A5 sub_000E1F49 sub_000E2385 sub_000E247C)
include("${CMAKE_CURRENT_SOURCE_DIR}/scripts/mm3_lamp_lighting_seam.cmake")
list(APPEND MM3_SEAM_FUNCTIONS sub_00254FDD sub_0022FF8B sub_0022FFD1 sub_0023006E sub_00230970)
include("${CMAKE_CURRENT_SOURCE_DIR}/scripts/mm3_light_queue_seam.cmake")
list(APPEND MM3_SEAM_FUNCTIONS sub_000E01C2 sub_000E0277)
include("${CMAKE_CURRENT_SOURCE_DIR}/scripts/mm3_car_light_culling_seam.cmake")
list(APPEND MM3_SEAM_FUNCTIONS sub_00201BCA sub_0011CAE8 sub_00120091 sub_00116535 sub_0014BAE9 sub_0014C121)
list(REMOVE_DUPLICATES MM3_SEAM_FUNCTIONS)
include("${CMAKE_CURRENT_SOURCE_DIR}/scripts/mm3_race_guidance_seam.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/scripts/mm3_zone_diagnostics_seam.cmake")
list(APPEND MM3_SEAM_FUNCTIONS sub_0020F1AB)
include("${CMAKE_CURRENT_SOURCE_DIR}/scripts/mm3_glow_capacity_seam.cmake")
list(APPEND MM3_SEAM_FUNCTIONS sub_0022C86A)
include("${CMAKE_CURRENT_SOURCE_DIR}/scripts/mm3_tree_contact_seam.cmake")
list(APPEND MM3_SEAM_FUNCTIONS sub_0015CD35 sub_0015B9AB)
include("${CMAKE_CURRENT_SOURCE_DIR}/scripts/mm3_score_tables_seam.cmake")
list(APPEND MM3_SEAM_FUNCTIONS sub_001E8A7A sub_001E9060)
include("${CMAKE_CURRENT_SOURCE_DIR}/scripts/mm3_police_create_seam.cmake")
list(APPEND MM3_SEAM_FUNCTIONS sub_0015070A ${MM3_POLICE_FACTORY_OWNERS})
include("${CMAKE_CURRENT_SOURCE_DIR}/scripts/mm3_dsound_isr_seam.cmake")
list(APPEND MM3_SEAM_FUNCTIONS ${MM3_DSOUND_GUARD_FUNCTIONS})
if(MM3_NATIVE_FENCE_WAIT)
    list(APPEND MM3_SEAM_FUNCTIONS sub_00344640)
    include("${CMAKE_CURRENT_SOURCE_DIR}/scripts/mm3_fence_wait_seam.cmake")
endif()
list(JOIN MM3_SEAM_FUNCTIONS "|" seam_names)
# recomp_runtime_entries.c also owns hooked bodies: the pointer-only entries
# (mm3_pointer_function_entries.json, e.g. the lamp owners 0xE2385/0xE247C).
set(hook_scan_sources ${RECOMP_GEN_SOURCES})
foreach(generated_source IN LISTS hook_scan_sources)
    file(STRINGS "${generated_source}" hooked_definitions
        REGEX "^void (${hook_names}|${distance_names}|${seam_names})[(]void[)]$")
    foreach(definition IN LISTS hooked_definitions)
        string(REGEX REPLACE "^void (sub_[0-9A-F]+)[(]void[)]$" "\\1" hooked "${definition}")
        if(DEFINED owner_${hooked})
            message(FATAL_ERROR "${hooked} is defined in ${owner_${hooked}} and ${generated_source}")
        endif()
        set(owner_${hooked} "${generated_source}")
    endforeach()
endforeach()
foreach(hooked IN LISTS MM3_RENAMED_FUNCTIONS MM3_DEFINITION_ONLY_FUNCTIONS MM3_SEAM_FUNCTIONS)
    if(NOT DEFINED owner_${hooked})
        message(FATAL_ERROR "No generated definition of ${hooked} in ${RECOMP_GEN_DIR}")
    endif()
endforeach()
if(MM3_FRONTEND_WATCH)
    if(MSVC)
        set(watch_include "/FI${CMAKE_CURRENT_SOURCE_DIR}/src/mm3_frontend_watch_hook.h")
    else()
        set(watch_include "-include" "${CMAKE_CURRENT_SOURCE_DIR}/src/mm3_frontend_watch_hook.h")
    endif()
    set_property(SOURCE ${RECOMP_GEN_SOURCES} APPEND PROPERTY COMPILE_OPTIONS ${watch_include})
endif()

# 0x252AF and the runtime-proven entries are supplied by the project. The
# generator also lists them in the aggregate unresolved-stub TU; rename those
# fallbacks so the real definitions own the dispatch symbols.
set(RECOMP_UNRESOLVED_STUB "${RECOMP_GEN_DIR}/recomp_stubs_unresolved.c")
set_property(SOURCE "${RECOMP_UNRESOLVED_STUB}" APPEND PROPERTY
    COMPILE_DEFINITIONS "sub_000252AF=sub_000252AF_unresolved")
# Each stub reports the first time it runs (src/mm3_unresolved_observe.c);
# the frontend watch build brings its own observer. MM3_UNRESOLVED_STUB_STOP
# turns on the fork's strict diagnostic instead: stop at the first stub that
# runs (recomp_unresolved_stub_stop in xboxrecomp).
option(MM3_UNRESOLVED_STUB_STOP "Stop at the first unresolved stub that runs instead of logging it" OFF)
if(MM3_UNRESOLVED_STUB_STOP)
    set_property(SOURCE "${RECOMP_UNRESOLVED_STUB}" APPEND PROPERTY
        COMPILE_DEFINITIONS "RECOMP_UNRESOLVED_STUB_OBSERVE=recomp_unresolved_stub_stop")
elseif(NOT MM3_FRONTEND_WATCH)
    if(MSVC)
        set(unresolved_observe "/FI${CMAKE_CURRENT_SOURCE_DIR}/src/mm3_unresolved_observe.h")
    else()
        set(unresolved_observe "-include" "${CMAKE_CURRENT_SOURCE_DIR}/src/mm3_unresolved_observe.h")
    endif()
    set_property(SOURCE "${RECOMP_UNRESOLVED_STUB}" APPEND PROPERTY COMPILE_OPTIONS ${unresolved_observe})
endif()
# Prototypes those two generated files use without declaring (see the header).
if(MSVC)
    set(mm3_gen_prototypes "/FI${CMAKE_CURRENT_SOURCE_DIR}/src/mm3_gen_prototypes.h")
else()
    set(mm3_gen_prototypes "-include" "${CMAKE_CURRENT_SOURCE_DIR}/src/mm3_gen_prototypes.h")
endif()
set_property(SOURCE "${RECOMP_UNRESOLVED_STUB}" "${RECOMP_GEN_DIR}/recomp_runtime_entries.c"
    APPEND PROPERTY COMPILE_OPTIONS ${mm3_gen_prototypes})
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/mm3_runtime_entry_repairs.json")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/mm3_runtime_entry_repairs.json" runtime_entries_json)
string(JSON runtime_entry_count LENGTH "${runtime_entries_json}")
math(EXPR runtime_entry_last "${runtime_entry_count} - 1")
foreach(runtime_entry_index RANGE 0 ${runtime_entry_last})
    string(JSON runtime_entry_start GET "${runtime_entries_json}" ${runtime_entry_index} start)
    string(SUBSTRING "${runtime_entry_start}" 2 8 runtime_entry_suffix)
    set_property(SOURCE "${RECOMP_UNRESOLVED_STUB}" APPEND PROPERTY
        COMPILE_DEFINITIONS "sub_${runtime_entry_suffix}=sub_${runtime_entry_suffix}_unresolved")
endforeach()

# All wrappers rename only their generated definitions. A compile definition
# also renames same-chunk calls, bypassing the wrapper (including the world
# scheduler). Preserve the fork's loop seams and other source properties in
# one transformed copy per owning chunk.
set(chunk_hooks ${MM3_RENAMED_FUNCTIONS} ${MM3_DEFINITION_ONLY_FUNCTIONS})
set(distance_owners "")
foreach(hooked IN LISTS chunk_hooks MM3_SEAM_FUNCTIONS)
    list(APPEND distance_owners "${owner_${hooked}}")
endforeach()
# Every generated chunk caches its register addresses and the guest base at
# function entry (RECOMP_GEN_PROLOGUE in src/mm3_recomp_types.h); it goes
# through the same transformed copy.
option(MM3_GEN_REGISTER_CACHE "Generated functions keep register addresses and the guest base in locals" ON)
# Functions doing a lot of x87 work also keep the x87 stack, top, control word
# and compare state in locals, written back at every call and return
# (scripts/mm3_gen_x87_locals.py; bit-identical, tests/mm3_gen_x87_locals_regression.py).
option(MM3_GEN_X87_LOCALS "x87-heavy generated functions keep their x87 state in locals" ON)
set(MM3_GEN_CACHE_ARGS "")
if(MM3_GEN_X87_LOCALS)
    set(MM3_GEN_CACHE_ARGS "--x87-locals")
endif()
if(MM3_GEN_REGISTER_CACHE)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/mm3_gen_register_cache.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/mm3_gen_x87_locals.py")
    foreach(gen_source IN LISTS RECOMP_GEN_SOURCES)
        get_filename_component(gen_dir "${gen_source}" DIRECTORY)
        # Not the dispatch table or the one-line unresolved-call stubs.
        if(gen_dir STREQUAL RECOMP_GEN_DIR AND NOT gen_source MATCHES "/recomp_(dispatch|stubs_unresolved)\\.c$")
            list(APPEND distance_owners "${gen_source}")
        endif()
    endforeach()
endif()
list(REMOVE_DUPLICATES distance_owners)
# Regeneration must refresh every transformed copy, including definition-only
# hooks, even when the generated filenames stay the same or owners move chunks.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${distance_owners})
foreach(distance_input IN LISTS distance_owners)
    get_filename_component(distance_name "${distance_input}" NAME)
    set(distance_output "${CMAKE_CURRENT_BINARY_DIR}/hooked_chunks/${distance_name}")
    file(READ "${distance_input}" distance_content)
    foreach(hooked IN LISTS chunk_hooks)
        if(owner_${hooked} STREQUAL distance_input)
            string(REPLACE "void ${hooked}(void)" "void ${hooked}_original(void)"
                distance_content "${distance_content}")
        endif()
    endforeach()
    if(owner_sub_001C7B15 STREQUAL distance_input)
        mm3_keep_masked_visibility_fallback(distance_content)
    endif()
    if(MM3_NATIVE_FENCE_WAIT AND owner_sub_00344640 STREQUAL distance_input)
        mm3_add_fence_wait(distance_content)
    endif()
    foreach(dma_owner sub_00344900 sub_003449B0)
        if(owner_${dma_owner} STREQUAL distance_input)
            mm3_add_dma_publish(distance_content ${dma_owner})
        endif()
    endforeach()
    foreach(collision_owner sub_0013FE96 sub_0014E808)
        if(owner_${collision_owner} STREQUAL distance_input)
            mm3_fix_collision_calls(distance_content ${collision_owner})
        endif()
    endforeach()
    if(owner_sub_0013AB27 STREQUAL distance_input)
        mm3_fix_ai_query_stack(distance_content)
    endif()
    foreach(route_owner sub_0013AB27 sub_0024FA01 sub_0024F917 sub_0024F964)
        if(owner_${route_owner} STREQUAL distance_input)
            mm3_add_route_diagnostics(distance_content ${route_owner})
        endif()
    endforeach()
    if(owner_sub_0024F964 STREQUAL distance_input)
        mm3_fix_route_cancellation(distance_content)
    endif()
    foreach(apu_owner sub_0027DCDA sub_0027DD72)
        if(owner_${apu_owner} STREQUAL distance_input)
            mm3_add_apu_mmio(distance_content ${apu_owner})
        endif()
    endforeach()
    if(owner_sub_0017BB4B STREQUAL distance_input)
        mm3_guard_audio_resource_pool(distance_content)
    endif()
    if(owner_sub_0023F09B STREQUAL distance_input)
        mm3_add_speedometer_readout(distance_content)
    endif()
    if(owner_sub_00224A07 STREQUAL distance_input)
        mm3_add_city_streaming_limit(distance_content)
    endif()
    # sub_00116535 first: it declares the carry its tail-called halves read.
    foreach(map_owner sub_0011CA25 sub_0011D1E8 sub_0011D808 sub_0011CAE8 sub_0011DCBF sub_00116535)
        if(owner_${map_owner} STREQUAL distance_input)
            mm3_add_map_zoom(distance_content ${map_owner})
        endif()
    endforeach()
    if(owner_sub_001EC352 STREQUAL distance_input)
        mm3_hide_navigation_arrows(distance_content)
    endif()
    if(owner_sub_001093BB STREQUAL distance_input)
        mm3_add_name_marker_seam(distance_content sub_001093BB_original)
    endif()
    foreach(lamp_owner sub_000E10A5 sub_000E1F49 sub_000E2385 sub_000E247C)
        if(owner_${lamp_owner} STREQUAL distance_input)
            if(lamp_owner STREQUAL "sub_000E1F49")
                mm3_add_lamp_lighting_seam(distance_content ${lamp_owner}_original)
            else()
                mm3_add_lamp_lighting_seam(distance_content ${lamp_owner})
            endif()
        endif()
    endforeach()
    foreach(light_queue_owner sub_00254FDD sub_0022FF8B sub_0022FFD1 sub_0023006E sub_00230970)
        if(owner_${light_queue_owner} STREQUAL distance_input)
            mm3_add_light_queue_seam(distance_content ${light_queue_owner})
        endif()
    endforeach()
    foreach(car_light_owner sub_000E01C2 sub_000E0277)
        if(owner_${car_light_owner} STREQUAL distance_input)
            mm3_add_car_light_culling_seam(distance_content ${car_light_owner})
        endif()
    endforeach()
    # sub_00116535 before its tail-called halves: it declares their carry.
    foreach(race_owner sub_00201BCA sub_0011CAE8 sub_00120091 sub_00116535 sub_0014BAE9 sub_0014C121)
        if(owner_${race_owner} STREQUAL distance_input)
            mm3_add_race_guidance(distance_content ${race_owner})
        endif()
    endforeach()
    if(owner_sub_0020F1AB STREQUAL distance_input)
        mm3_add_zone_diagnostics(distance_content)
    endif()
    if(owner_sub_0022C86A STREQUAL distance_input)
        mm3_add_glow_capacity(distance_content)
    endif()
    if(owner_sub_001D137B STREQUAL distance_input)
        mm3_add_tree_contacts(distance_content)
    endif()
    if(owner_sub_0016AEDB STREQUAL distance_input)
        mm3_add_tree_mesh(distance_content)
    endif()
    if(owner_sub_0027AF28 STREQUAL distance_input)
        foreach(dsound_owner IN LISTS MM3_DSOUND_GUARD_FUNCTIONS)
            if(NOT owner_${dsound_owner} STREQUAL distance_input)
                message(FATAL_ERROR "MM3 DirectSound ISR sync: ${dsound_owner} moved to another chunk")
            endif()
        endforeach()
        mm3_add_dsound_isr_sync(distance_content)
    endif()
    foreach(tree_geometry_owner sub_0015CD35 sub_0015B9AB)
        if(owner_${tree_geometry_owner} STREQUAL distance_input)
            mm3_add_tree_geometry(distance_content ${tree_geometry_owner})
        endif()
    endforeach()
    foreach(score_owner sub_001E8A7A sub_001E9060)
        if(owner_${score_owner} STREQUAL distance_input)
            mm3_fix_score_tables(distance_content ${score_owner})
        endif()
    endforeach()
    if(owner_sub_0015070A STREQUAL distance_input)
        mm3_add_police_create_steps(distance_content)
    endif()
    foreach(police_owner IN LISTS MM3_POLICE_FACTORY_OWNERS)
        if(owner_${police_owner} STREQUAL distance_input)
            mm3_add_police_factory_notes(distance_content ${police_owner})
        endif()
    endforeach()
    set(distance_cache OFF)
    if(MM3_GEN_REGISTER_CACHE)
        string(FIND "${distance_content}" "#define RECOMP_GENERATED_CODE" distance_generated)
        if(NOT distance_generated EQUAL -1)
            # Last, after the seams: they anchor on the untouched function text.
            set(distance_plain "${CMAKE_CURRENT_BINARY_DIR}/render_distance/${distance_name}.plain")
            file(WRITE "${distance_plain}" "${distance_content}")
            execute_process(COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/scripts/mm3_gen_register_cache.py"
                    "${distance_plain}" "${distance_plain}.cached" ${MM3_GEN_CACHE_ARGS}
                RESULT_VARIABLE distance_result OUTPUT_QUIET)
            if(NOT distance_result EQUAL 0)
                message(FATAL_ERROR "mm3_gen_register_cache.py failed on ${distance_name}")
            endif()
            file(READ "${distance_plain}.cached" distance_content)
            file(REMOVE "${distance_plain}" "${distance_plain}.cached")
            set(distance_cache ON)
        endif()
    endif()
    file(CONFIGURE OUTPUT "${distance_output}" CONTENT "${distance_content}" @ONLY)
    foreach(distance_property COMPILE_DEFINITIONS COMPILE_OPTIONS)
        get_source_file_property(distance_value "${distance_input}" ${distance_property})
        if(distance_value)
            set_property(SOURCE "${distance_output}" PROPERTY ${distance_property} "${distance_value}")
        endif()
    endforeach()
    if(distance_cache)
        set_property(SOURCE "${distance_output}" APPEND PROPERTY COMPILE_DEFINITIONS MM3_GEN_REGISTER_CACHE=1)
    endif()
    list(REMOVE_ITEM RECOMP_GEN_SOURCES "${distance_input}")
    list(APPEND RECOMP_GEN_SOURCES "${distance_output}")
endforeach()

find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(MM3_MENU_HEADER "${CMAKE_CURRENT_BINARY_DIR}/native_menu/mm3_menu_resources.h")
add_custom_command(OUTPUT "${MM3_MENU_HEADER}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/native-menu/generate.py"
        --archive "${CMAKE_CURRENT_SOURCE_DIR}/game_files/Data/Data_hd.zip"
        --header "${MM3_MENU_HEADER}"
    DEPENDS tools/native-menu/generate.py tools/native-menu/native_menu.py
        "${CMAKE_CURRENT_SOURCE_DIR}/game_files/Data/Data_hd.zip"
    COMMENT "Building native graphics menu resources from the installed game"
    VERBATIM)

# The launcher's header picture: the game's own logo, read from the installed game.
set(MM3_LOGO_HEADER "${CMAKE_CURRENT_BINARY_DIR}/branding/mm3_logo.h")
add_custom_command(OUTPUT "${MM3_LOGO_HEADER}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/branding/generate_logo.py"
        --archive "${CMAKE_CURRENT_SOURCE_DIR}/game_files/Data/Data_dvd.zip"
        --header "${MM3_LOGO_HEADER}"
    DEPENDS tools/branding/generate_logo.py tools/car-models/mm3_textures.py
        "${CMAKE_CURRENT_SOURCE_DIR}/game_files/Data/Data_dvd.zip"
    COMMENT "Reading the launcher logo from the installed game"
    VERBATIM)

add_executable(${PROJECT_NAME}
    src/derived_cache.c
    src/fixed_step.c
    src/feature_switch.c
    src/env_range.c
    src/string_ci.c
    src/slot_publish.c
    src/geometry/camera_math.c
    src/geometry/piece_cluster.c
    src/geometry/road_graph.c
    src/geometry/kb_spline.c
    src/geometry/polyline.c
    src/geometry/thick_line.c
    src/geometry/vec3_rotate.c
    src/main.c
    src/mm3_launcher.c
    src/mm3_launcher_config.c
    src/mm3_update.c
    src/mm3_pedestrian_compat.c
    src/mm3_display_config.c
    src/mm3_display_settings.c
    src/mm3_menu_resources.c
    src/mm3_graphics_menu.c
    "${MM3_MENU_HEADER}"
    "${MM3_LOGO_HEADER}"
    src/mm3_asset_cache.c
       src/mm3_host_video.c
    src/mm3_launcher.cpp
    src/ui/rmlui_host.cpp
    ${MM3_RMLUI_BACKEND}
    src/mods/mods.c
    src/mods/render_commands.c
    src/mods/render_policy.c
    src/mods/debug_overlay.cpp
    src/mods/dev_tools.c
    src/mods/menu_mouse.c
    src/mods/collision_fix.c
    src/mods/widescreen_hud.c
    src/mods/skip_intro.c
    src/mods/dvd_media.c
    src/mods/framerate.c
    src/mods/keyboard_driving.c
 src/mm3_memory_tails.c
 src/mm3_runtime_entries.c
 src/mm3_asset_trace.c
 src/recomp_manual.c
 src/mm3_name_markers.c
 src/mm3_map_zoom.c
 src/mm3_hud_options.c
 src/mm3_race_guidance.c
 src/mm3_facade_capacity.c
 src/mm3_facade_streams.c
 src/mm3_building_diagnostics.c
 src/mm3_producer_diagnostics.c
 src/mm3_route_diagnostics.c
 src/mm3_zone_diagnostics.c
 src/mm3_apu_mmio.c
 src/mm3_direct_log.c
    src/mm3_collision_profile.c
 src/mm3_tree_diagnostics.c
 src/mm3_tree_body.c
 src/mm3_tree_audio.c
 src/mm3_visibility.c
 src/mm3_masked_visibility.c
 src/mm3_matrix.c
 src/mm3_stress_debug.c
 src/mm3_custom_car_names.c
 src/mm3_circuit_races.c
 src/mm3_dlc_races.c
 src/mm3_exhaust.c
 src/mm3_lua_errors.c
 src/mm3_damage_diagnostics.c
 src/mm3_time_limits.c
 src/mm3_map_pins.c
 src/mm3_language.c
 src/mm3_vehicle_dynamics.c
 src/mm3_cheat_keys.c
 src/mm3_free_camera.c
 src/mm3_pause_clock.c
 src/mm3_photo.c
 src/mm3_ai_difficulty.c
 src/mm3_police_log.c
 src/mm3_score_tables.c
 src/mm3_lua_natives.c
 src/mm3_ui_guard.c
 src/mm3_heap_guard.c
 src/mm3_heap_trace.c
 src/mm3_unresolved_observe.c
 src/mm3_car_selection_compat.c
 src/mm3_car_order.c
 src/mm3_speedometer.c
 src/mm3_factory_physics.c
 src/mm3_cpu_compat.c
 src/mm3_world_lighting.c
 src/mm3_car_lighting.c
 src/mm3_car_light_visibility.c
 src/mm3_lamp_lighting.c
 src/mm3_lamp_glow.c
 src/mm3_light_queue.c
 src/mm3_light_watch.c
 src/mm3_darkness_monitor.c
 src/mods/widescreen.c
 src/mods/draw_distance.c
 src/mods/teleport.c
 src/recomp_trace_probe.c
 ${RECOMP_GEN_SOURCES})

if(WIN32)
    target_sources(${PROJECT_NAME} PRIVATE src/mm3.rc)
endif()
if(XBOXRECOMP_RENDERER STREQUAL "SDL3_GPU")
    # Renderer entry points the MM3 runtime patch adds to D3D11 only (DESIGN.md).
    target_sources(${PROJECT_NAME} PRIVATE src/compat/sdlgpu_d3d11_only.c)
    target_sources(${PROJECT_NAME} PRIVATE src/mods/world_tris_sdlgpu.c)
endif()
if(XBOXRECOMP_RENDERER STREQUAL "SDL3_GPU")
    set_property(SOURCE ${MM3_RMLUI_BACKEND} src/mm3_launcher.cpp src/ui/rmlui_host.cpp APPEND PROPERTY
        COMPILE_DEFINITIONS RMLUI_SDL_VERSION_MAJOR=3)
endif()

set_property(SOURCE src/recomp_manual.c APPEND PROPERTY COMPILE_DEFINITIONS
    "recomp_lookup_manual=recomp_lookup_manual_base" "sub_00083A6C=sub_00083A6C_original")
# The game's per-frame light level is watched from the light-queue consumer
# (src/mm3_darkness_monitor.c); unit tests that include the queue source skip it.
set_property(SOURCE src/mm3_light_queue.c APPEND PROPERTY COMPILE_DEFINITIONS MM3_DARKNESS_MONITOR=1)
if(MM3_MOVIE_INPUT_TRACE)
    target_sources(${PROJECT_NAME} PRIVATE src/mm3_movie_input_trace.c)
    target_compile_definitions(${PROJECT_NAME} PRIVATE MM3_MOVIE_INPUT_TRACE)
endif()
if(MM3_OFFLINE_XNET)
    target_sources(${PROJECT_NAME} PRIVATE src/mm3_offline_compat.c)
endif()
if(MM3_UNLOCK_ALL_CARS)
    target_sources(${PROJECT_NAME} PRIVATE src/mm3_car_unlocks.c)
endif()
if(MM3_FRONTEND_WATCH)
    # A hardware write watchpoint: debug registers through the Win32 exception
    # CONTEXT, or a perf_event breakpoint on Linux. Other hosts have neither.
    if(NOT WIN32 AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "MM3_FRONTEND_WATCH needs Windows or Linux hardware watchpoints")
    endif()
    target_sources(${PROJECT_NAME} PRIVATE src/mm3_frontend_watch.c)
endif()

if(NOT WIN32)
    # <windows.h> for the port's own sources: the runtime's POSIX Win32 layer.
    # Force-included too, since some files use Win32 names without including it.
    target_include_directories(${PROJECT_NAME} BEFORE PRIVATE src/compat/posix)
    get_target_property(_mm3_sources ${PROJECT_NAME} SOURCES)
    foreach(_src IN LISTS _mm3_sources)
        # C only: mm3_launcher.cpp and the overlay bring their own Win32 stand-ins off Windows.
        if(_src MATCHES "^src/.*\\.c$" AND NOT _src MATCHES "^src/recomp/")
            set_property(SOURCE ${_src} APPEND PROPERTY COMPILE_OPTIONS
                -include "${CMAKE_CURRENT_SOURCE_DIR}/src/compat/posix/windows.h")
        endif()
    endforeach()
endif()
target_include_directories(${PROJECT_NAME} PRIVATE
    src
    "${CMAKE_CURRENT_BINARY_DIR}/native_menu"
    "${CMAKE_CURRENT_BINARY_DIR}/branding"
    src/geometry
    src/recomp
    ${RECOMP_GEN_DIR}
    ${XBOXRECOMP_DIR}/src/video
    ${XBOXRECOMP_DIR}/include)

target_link_libraries(${PROJECT_NAME} PRIVATE xboxrecomp mm3_imgui RmlUi::RmlUi
    ${MM3_RMLUI_BACKEND_LIBS} ${MM3_WIN32_LIBS})
target_include_directories(${PROJECT_NAME} PRIVATE ${rmlui_SOURCE_DIR}/Backends)
set_property(SOURCE src/mm3_launcher.c APPEND PROPERTY COMPILE_DEFINITIONS MM3_LAUNCHER_AUDIO=1)
set_property(SOURCE src/mm3_launcher.c APPEND PROPERTY COMPILE_DEFINITIONS MM3_LAUNCHER_NETWORK=1)
# The release this build belongs to: the launcher's title and the first log
# line show it, so a report says which version it came from.
set(MM3_VERSION "2.6 beta 2")
set_property(SOURCE src/mm3_launcher.c src/main.c APPEND PROPERTY COMPILE_DEFINITIONS
    "MM3_VERSION=\"${MM3_VERSION}\"")
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE comctl32 winhttp bcrypt shell32 windowscodecs ole32 uuid)
endif()

# Focused offline checks; explicitly built, never part of the game build.
# Each one links the platform layer, which supplies the <platform/xbox_winnt.h>
# vocabulary off Windows (on Windows it only adds the include path).
foreach(check teleport dev_tools draw_distance)
    add_executable(mm3_${check}_contract EXCLUDE_FROM_ALL tests/mm3_${check}_contract.c)
    target_include_directories(mm3_${check}_contract PRIVATE src src/recomp ${RECOMP_GEN_DIR}
        ${XBOXRECOMP_DIR}/include ${XBOXRECOMP_DIR}/src/video ${XBOXRECOMP_DIR}/src/kernel)
    target_link_libraries(mm3_${check}_contract PRIVATE platform ${MM3_WIN32_LIBS} ${MM3_LIBM})
    if(MSVC)
        target_compile_options(mm3_${check}_contract PRIVATE /UNDEBUG)
    endif()
endforeach()

# Win32-only checks, so not registered elsewhere. The overlay check drives the
# WM_* message path of debug_overlay.cpp with the Win32/DX11 ImGui backend
# (the SDL3 build reads SDL events instead, and the SDL3 build's ImGui backend
# is not linked here), and the input check drives the XInput port ABI, which
# xinput_device.c uses only on Windows (SDL gamepads elsewhere).
if(WIN32 AND XBOXRECOMP_RENDERER STREQUAL "D3D11")
    add_executable(mm3_overlay_contract EXCLUDE_FROM_ALL tests/mm3_overlay_contract.cpp)
    target_include_directories(mm3_overlay_contract PRIVATE src src/recomp ${RECOMP_GEN_DIR}
        ${XBOXRECOMP_DIR}/include ${XBOXRECOMP_DIR}/src/video)
    target_link_libraries(mm3_overlay_contract PRIVATE mm3_imgui ${MM3_WIN32_LIBS})
    if(MSVC)
        target_compile_options(mm3_overlay_contract PRIVATE /UNDEBUG)
    endif()
endif()

# GNU linkers only: route recomp_trace_esp through src/recomp_trace_probe.c
# (an opt-in RECOMP_TRACE_EFD85 diagnostic).
if(NOT MSVC)
    target_link_options(${PROJECT_NAME} PRIVATE "-Wl,--wrap=recomp_trace_esp")
endif()

if(MM3_RECOMP_ABI_CHECK)
    target_compile_definitions(${PROJECT_NAME} PRIVATE RECOMP_ABI_CHECK)
    target_compile_definitions(xbox_kernel PRIVATE RECOMP_ABI_CHECK)
endif()

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /bigobj)
endif()
if(MM3_PGO)
    file(TO_NATIVE_PATH "${MM3_PGO_PGD}" pgo_database)
    if(MM3_PGO STREQUAL "GEN")
        # Fast counters keep the instrumented build playable for training.
        target_link_options(${PROJECT_NAME} PRIVATE
            "$<$<NOT:$<CONFIG:Debug>>:/FASTGENPROFILE:PGD=${pgo_database}>")
    else()
        target_link_options(${PROJECT_NAME} PRIVATE
            "$<$<NOT:$<CONFIG:Debug>>:/USEPROFILE:PGD=${pgo_database}>")
    endif()
endif()

add_executable(mm3_launcher_settings_contract EXCLUDE_FROM_ALL
    tests/mm3_launcher_settings_contract.cpp src/mods/mods.c src/feature_switch.c
    ${XBOXRECOMP_DIR}/src/input/keymap_spec.c ${MM3_RMLUI_BACKEND})
target_include_directories(mm3_launcher_settings_contract PRIVATE src
    ${rmlui_SOURCE_DIR}/Backends ${XBOXRECOMP_DIR}/src/video ${XBOXRECOMP_DIR}/src/input)
target_link_libraries(mm3_launcher_settings_contract PRIVATE platform RmlUi::RmlUi
    ${MM3_RMLUI_BACKEND_LIBS} ${MM3_WIN32_LIBS} ${MM3_LIBM})
if(XBOXRECOMP_RENDERER STREQUAL "SDL3_GPU")
    # The launcher source selects its SDL3 path and RmlUi's SDL3 renderer on
    # these; the contract includes the launcher source, so set them here.
    target_compile_definitions(mm3_launcher_settings_contract PRIVATE
        XBOXRECOMP_SDL3=1 RMLUI_SDL_VERSION_MAJOR=3)
endif()
if(MSVC)
    target_compile_options(mm3_launcher_settings_contract PRIVATE /UNDEBUG)
endif()

# The distance enhancement belongs to this port, including host shaders.
target_sources(mm3_recomp PRIVATE src/mods/render_fog.c)
include(cmake/MM3RenderFog.cmake)
mm3_render_fog_shaders(mm3_recomp "${XBOXRECOMP_RENDERER}")
