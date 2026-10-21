cmake_minimum_required(VERSION 3.20)
project(mm3_recomp C)
if(WIN32)
    enable_language(RC)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
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
    enable_language(CXX)
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

# Programs captured from the user's disc remain local derived data. An empty
# directory still builds the interpreter, which can capture them on first run.
set(XBOXRECOMP_DSP_PROGRAM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/game_files/dsp" CACHE PATH
    "Local GP/EP program captures for native DSP compilation")
add_subdirectory(${XBOXRECOMP_DIR} ${CMAKE_BINARY_DIR}/xboxrecomp)
if(MSVC AND MM3_DSP_AUTO_INLINE)
    # RelWithDebInfo defaults to /Ob1, which leaves small non-inline interpreter
    # helpers as calls. Override only this measured hot source, retaining its
    # existing arithmetic and Debug build behavior.
    set_property(SOURCE "${XBOXRECOMP_DIR}/src/apu/dsp/interp/dsp_cpu.c"
        TARGET_DIRECTORY xbox_apu APPEND PROPERTY
        COMPILE_OPTIONS "$<$<NOT:$<CONFIG:Debug>>:/Ob2>")
endif()
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

# Generated functions move between split chunks whenever the generator's
# function list changes, so find each project hook's owner instead of naming
# a chunk. Hooks rename the generated definition to <name>_original and the
# project supplies <name>.
set(MM3_RENAMED_FUNCTIONS
    sub_00170E5D sub_000145DC sub_001F02AD
    sub_0025968E)                # widescreen: wraps the projection-matrix builder
if(MM3_MOVIE_INPUT_TRACE)
    list(APPEND MM3_RENAMED_FUNCTIONS sub_001DB6E8 sub_001F2A71 sub_001F443D sub_0035BDC0)
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
    sub_00162EBA sub_00162EF3)       # facade detail/far stream use (mm3_facade_streams.c)
set(MM3_DEFINITION_ONLY_FUNCTIONS ${MM3_DISTANCE_FUNCTIONS})
# Repair newly decoded collision cells once, before the native queries use them.
list(APPEND MM3_DEFINITION_ONLY_FUNCTIONS sub_0015A929)
# Ordered host depth fog and its optional horizon replacement.
list(APPEND MM3_DEFINITION_ONLY_FUNCTIONS sub_000F8DCE sub_000F8750)
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
# Keep same-chunk calls directed to the recovered x87 helper definitions.
list(APPEND MM3_DEFINITION_ONLY_FUNCTIONS sub_0009BC04 sub_0009BC35 sub_0009BC52)
# Preserve same-chunk locale calls to the custom vehicle-name extension.
list(APPEND MM3_DEFINITION_ONLY_FUNCTIONS sub_0011C194)
# Native graphics pages retain the retail Meme parser and controller UI.
list(APPEND MM3_DEFINITION_ONLY_FUNCTIONS
    sub_00119AEE sub_00102168 sub_00102191 sub_001021B5 sub_0006EA22
    sub_001C5E11 sub_0006B3EF sub_0011C650)
# Preserve same-chunk catalog calls to the retired-car selection fallback.
list(APPEND MM3_DEFINITION_ONLY_FUNCTIONS sub_001DDFC5)
# Keep retail vehicles first and sort custom/DLC menu entries by top speed.
list(APPEND MM3_DEFINITION_ONLY_FUNCTIONS sub_001DE752)
# The CRT MMX probe must preserve the stack effects of PUSHFD/POPFD.
list(APPEND MM3_DEFINITION_ONLY_FUNCTIONS sub_00093770)
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
if(MM3_NATIVE_FENCE_WAIT)
    list(APPEND MM3_SEAM_FUNCTIONS sub_00344640)
    include("${CMAKE_CURRENT_SOURCE_DIR}/scripts/mm3_fence_wait_seam.cmake")
endif()
list(JOIN MM3_SEAM_FUNCTIONS "|" seam_names)
# recomp_runtime_entries.c holds the project's replacement x87 helper bodies.
set(hook_scan_sources ${RECOMP_GEN_SOURCES})
list(FILTER hook_scan_sources EXCLUDE REGEX "/recomp_runtime_entries[.]c$")
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
foreach(hooked IN LISTS MM3_RENAMED_FUNCTIONS)
    set_property(SOURCE "${owner_${hooked}}" APPEND PROPERTY
        COMPILE_DEFINITIONS "${hooked}=${hooked}_original")
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

# Definition and loop hooks share one transformed copy per owning chunk,
# carrying over its other compile properties even after functions move chunks.
set(distance_owners "")
foreach(hooked IN LISTS MM3_DEFINITION_ONLY_FUNCTIONS)
    list(APPEND distance_owners "${owner_${hooked}}")
endforeach()
foreach(hooked IN LISTS MM3_SEAM_FUNCTIONS)
    list(APPEND distance_owners "${owner_${hooked}}")
endforeach()
list(REMOVE_DUPLICATES distance_owners)
# Regeneration must refresh every transformed copy, including definition-only
# hooks, even when the generated filenames stay the same or owners move chunks.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${distance_owners})
foreach(distance_input IN LISTS distance_owners)
    get_filename_component(distance_name "${distance_input}" NAME)
    set(distance_output "${CMAKE_CURRENT_BINARY_DIR}/render_distance/${distance_name}")
    file(READ "${distance_input}" distance_content)
    foreach(hooked IN LISTS MM3_DEFINITION_ONLY_FUNCTIONS)
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
    foreach(tree_geometry_owner sub_0015CD35 sub_0015B9AB)
        if(owner_${tree_geometry_owner} STREQUAL distance_input)
            mm3_add_tree_geometry(distance_content ${tree_geometry_owner})
        endif()
    endforeach()
    file(CONFIGURE OUTPUT "${distance_output}" CONTENT "${distance_content}" @ONLY)
    foreach(distance_property COMPILE_DEFINITIONS COMPILE_OPTIONS)
        get_source_file_property(distance_value "${distance_input}" ${distance_property})
        if(distance_value)
            set_property(SOURCE "${distance_output}" PROPERTY ${distance_property} "${distance_value}")
        endif()
    endforeach()
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

add_executable(${PROJECT_NAME}
    src/main.c
    src/mm3_collision_cells.c
    src/mm3_native_fog.c
    src/mm3_launcher.c
    src/mm3_launcher_config.c
    src/mm3_pedestrian_compat.c
    src/mm3_display_config.c
    src/mm3_display_settings.c
    src/mm3_menu_resources.c
    src/mm3_graphics_menu.c
    "${MM3_MENU_HEADER}"
    src/mm3_asset_cache.c
    src/mm3_debug_features.c
 src/mm3_memory_compat.c
 src/mm3_memory_tails.c
 src/mm3_runtime_entries.c
 src/mm3_asset_trace.c
 src/mm3_stream_compat.c
 src/recomp_manual.c
 src/mm3_widescreen.c
 src/mm3_name_markers.c
 src/mm3_map_zoom.c
 src/mm3_hud_options.c
 src/mm3_race_guidance.c
 src/mm3_render_distance.c
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
 src/mm3_debug_teleport.c
 src/mm3_stress_debug.c
 src/mm3_custom_car_names.c
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
 src/mm3_darkness_monitor.c
 src/recomp_trace_probe.c
 ${RECOMP_GEN_SOURCES})

if(WIN32)
    target_sources(${PROJECT_NAME} PRIVATE src/mm3.rc)
endif()

set_property(SOURCE src/recomp_manual.c APPEND PROPERTY COMPILE_DEFINITIONS
    "recomp_lookup_manual=recomp_lookup_manual_base" "sub_00083A6C=sub_00083A6C_original")
# The game's per-frame light level is watched from the light-queue consumer
# (src/mm3_darkness_monitor.c); unit tests that include the queue source skip it.
set_property(SOURCE src/mm3_light_queue.c APPEND PROPERTY COMPILE_DEFINITIONS MM3_DARKNESS_MONITOR=1)
if(MM3_MOVIE_INPUT_TRACE)
    target_sources(${PROJECT_NAME} PRIVATE src/mm3_movie_input_trace.c)
endif()
if(MM3_OFFLINE_XNET)
    target_sources(${PROJECT_NAME} PRIVATE src/mm3_offline_compat.c)
endif()
if(MM3_UNLOCK_ALL_CARS)
    target_sources(${PROJECT_NAME} PRIVATE src/mm3_car_unlocks.c)
endif()
if(MM3_FRONTEND_WATCH)
    target_sources(${PROJECT_NAME} PRIVATE src/mm3_frontend_watch.c)
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
    src
    "${CMAKE_CURRENT_BINARY_DIR}/native_menu"
    src/recomp
    ${RECOMP_GEN_DIR}
    ${XBOXRECOMP_DIR}/src/video
    ${XBOXRECOMP_DIR}/include)

target_link_libraries(${PROJECT_NAME} PRIVATE xboxrecomp)
set_property(SOURCE src/mm3_launcher.c APPEND PROPERTY COMPILE_DEFINITIONS MM3_LAUNCHER_AUDIO=1)
set_property(SOURCE src/mm3_launcher.c APPEND PROPERTY COMPILE_DEFINITIONS MM3_LAUNCHER_NETWORK=1)
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE comctl32 xinput)
endif()

# MM3's CRT uses NtAllocateVirtualMemory reserve requests as heap-backed
# buffers.  Keep the vendor runtime untouched: redirect its public reserve
# seam to the existing Xbox heap on the ELF build used by this checkout.
if(NOT MSVC)
 target_link_options(${PROJECT_NAME} PRIVATE
  "-Wl,--wrap=xbox_ReserveAlloc"
  "-Wl,--wrap=recomp_trace_esp")
endif()

if(MM3_RECOMP_ABI_CHECK)
    target_compile_definitions(${PROJECT_NAME} PRIVATE RECOMP_ABI_CHECK)
    target_compile_definitions(xbox_kernel PRIVATE RECOMP_ABI_CHECK)
endif()

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /bigobj)
endif()
if(MM3_PGO)
    target_sources(${PROJECT_NAME} PRIVATE src/mm3_pgo_link_stubs.c)
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
