cmake_minimum_required(VERSION 3.25)
project(Dominion2026 VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)

if(WIN32)
    add_compile_definitions(
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _CRT_SECURE_NO_WARNINGS
        UNICODE
        _UNICODE
    )
    if(MINGW)
        add_compile_options(-mstackrealign)
    endif()
endif()

# C library lz4_static
add_library(lz4_static STATIC external/lz4/lz4.c)
target_include_directories(lz4_static PUBLIC external/lz4)
target_compile_options(lz4_static PRIVATE -O3 -w)

# C++ target ron_core (STATIC library containing all src/core/*.cpp)
set(RON_CORE_SOURCES
    src/core/assert_log.cpp
    src/core/clock.cpp
    src/core/arena.cpp
    src/core/hash.cpp
    src/core/compression.cpp
    src/core/fixed_math.cpp
    src/core/fixed_geom.cpp
    src/core/job_system.cpp
    src/core/task_graph.cpp
    src/core/profiler.cpp
)

add_library(ron_core STATIC ${RON_CORE_SOURCES})

target_include_directories(ron_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/external/lz4
)

target_compile_options(ron_core PRIVATE
    -Wall -Wextra -Wpedantic -Wshadow -Werror=return-type
    -march=x86-64-v2 -fno-exceptions -fno-rtti -fno-strict-aliasing
    $<$<CONFIG:Debug>:-O0 -g3>
    $<$<NOT:$<CONFIG:Debug>>:-O3 -fomit-frame-pointer>
)

target_compile_definitions(ron_core PUBLIC
    $<$<CONFIG:Debug>:RON_DEBUG=1>
    $<$<NOT:$<CONFIG:Debug>>:NDEBUG RON_DEBUG=0>
)

if(WIN32)
    target_link_libraries(ron_core PUBLIC lz4_static kernel32)
else()
    target_link_libraries(ron_core PUBLIC lz4_static)
endif()

# C++ target ron_sim (STATIC library containing src/sim/*.cpp)
set(RON_SIM_SOURCES
    src/sim/entity_soa.cpp
    src/sim/command_bus.cpp
    src/sim/sim_world.cpp
    src/sim/game_db.cpp
)

add_library(ron_sim STATIC ${RON_SIM_SOURCES})

target_include_directories(ron_sim PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/external/lz4
)

target_compile_options(ron_sim PRIVATE
    -Wall -Wextra -Wpedantic -Wshadow -Werror=return-type
    -march=x86-64-v2 -fno-exceptions -fno-rtti -fno-strict-aliasing
    -fwrapv -Werror=double-promotion -Werror=float-conversion -Werror=switch
    $<$<CONFIG:Debug>:-O0 -g3>
    $<$<NOT:$<CONFIG:Debug>>:-O3 -fomit-frame-pointer>
)

target_compile_definitions(ron_sim PUBLIC
    $<$<CONFIG:Debug>:RON_DEBUG=1>
    $<$<NOT:$<CONFIG:Debug>>:NDEBUG RON_DEBUG=0>
)

target_compile_definitions(ron_sim PRIVATE RON_SIM_NO_FLOAT=1)

target_link_libraries(ron_sim PUBLIC ron_core)

# C++ target ron_assets (STATIC library containing src/assets/*.cpp)
set(RON_ASSETS_SOURCES
    src/assets/model_data.cpp
    src/assets/texture_data.cpp
    src/assets/theatre_data.cpp
)

add_library(ron_assets STATIC ${RON_ASSETS_SOURCES})

target_include_directories(ron_assets PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_options(ron_assets PRIVATE
    -Wall -Wextra -Wpedantic -Wshadow -Werror=return-type
    -march=x86-64-v2 -fno-exceptions -fno-rtti -fno-strict-aliasing
    $<$<CONFIG:Debug>:-O0 -g3>
    $<$<NOT:$<CONFIG:Debug>>:-O3 -fomit-frame-pointer>
)

target_compile_definitions(ron_assets PUBLIC
    $<$<CONFIG:Debug>:RON_DEBUG=1>
    $<$<NOT:$<CONFIG:Debug>>:NDEBUG RON_DEBUG=0>
)

target_link_libraries(ron_assets PUBLIC ron_core)

# C++ executable ron_tests
set(RON_TEST_SOURCES
    tests/test_main.cpp
    tests/test_gate01_core.cpp
    tests/test_gate02_fixed_math.cpp
    tests/test_gate03_jobs.cpp
    tests/test_gate04_sim_kernel.cpp
    tests/test_gate05_db.cpp
    tests/test_theatre.cpp
    tests/test_models.cpp
    tests/test_textures.cpp
)

add_executable(ron_tests ${RON_TEST_SOURCES})

target_compile_options(ron_tests PRIVATE
    -Wall -Wextra -Wpedantic -Wshadow -Werror=return-type
    -march=x86-64-v2 -fno-exceptions -fno-rtti -fno-strict-aliasing
    $<$<CONFIG:Debug>:-O0 -g3>
    $<$<NOT:$<CONFIG:Debug>>:-O3 -fomit-frame-pointer>
)

target_compile_definitions(ron_tests PRIVATE
    $<$<CONFIG:Debug>:RON_DEBUG=1>
    $<$<NOT:$<CONFIG:Debug>>:NDEBUG RON_DEBUG=0>
    RON_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(ron_tests PRIVATE ron_sim ron_assets ron_core)

if(MINGW)
    target_link_options(ron_tests PRIVATE -Wl,--stack,16777216 -static -static-libgcc -static-libstdc++)
endif()

# --- SDL3 & glad dependencies ---
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS ON)

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL_GPU OFF CACHE BOOL "" FORCE)
set(SDL_CAMERA OFF CACHE BOOL "" FORCE)

add_subdirectory(third_party/SDL3 EXCLUDE_FROM_ALL)

# Target glad (STATIC from third_party/glad/src/gl.c)
add_library(glad STATIC third_party/glad/src/gl.c)
target_include_directories(glad SYSTEM PUBLIC third_party/glad/include)
target_compile_options(glad PRIVATE -O3 -w)

set(CMAKE_C_EXTENSIONS OFF)

# Target ron_sdl_smoke
add_executable(ron_sdl_smoke src/render/sdl_smoke.cpp)
target_include_directories(ron_sdl_smoke PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(ron_sdl_smoke PRIVATE SDL3::SDL3-static)
if(WIN32)
    target_link_libraries(ron_sdl_smoke PRIVATE winmm imm32 setupapi version ole32 oleaut32 gdi32 user32 shell32 advapi32)
endif()
if(MINGW)
    target_link_options(ron_sdl_smoke PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# C++ target ron_render (STATIC library containing src/render/*.cpp)
set(RON_RENDER_SOURCES
    src/render/gl_context.cpp
    src/render/vram_ledger.cpp
    src/render/gl_objects.cpp
    src/render/gl_bindings.cpp
    src/render/shader_preproc.cpp
    src/render/shader_program.cpp
    src/render/shader_library.cpp
    src/render/persistent_ring.cpp
    src/render/gl_state.cpp
    src/render/render_target.cpp
    src/render/gpu_timer.cpp
    src/render/rmath.cpp
    src/render/height_sampler.cpp
    src/render/camera_rts.cpp
    src/render/frame_fill.cpp
    src/render/terrain_quadtree.cpp
    src/render/terrain_source.cpp
    src/render/terrain_renderer.cpp
    src/render/territory_map.cpp
    src/render/fog_map.cpp
    src/render/crater_math.cpp
    src/render/minimap_cpu.cpp
    src/render/jfa_reference.cpp
    src/render/territory_sdf.cpp
    src/render/crater_splatter.cpp
    src/render/fog_gpu.cpp
    src/render/world_overlays.cpp
    src/render/minimap_renderer.cpp
    src/render/entity_feed.cpp
    src/render/entity_math.cpp
    src/render/entity_source.cpp
    src/render/entity_assets.cpp
    src/render/entity_renderer.cpp
    src/render/entity_crowd.cpp
    src/render/cascade_math.cpp
    src/render/shadow_ref.cpp
    src/render/pbr_ref.cpp
    src/render/marks_math.cpp
    src/render/icon_map.cpp
    src/render/ui_source.cpp
    src/render/ui_atlas.cpp
    src/render/lighting_system.cpp
    src/render/marks_renderer.cpp
)

set_source_files_properties(src/render/territory_sdf.cpp tests/render/test_jfa.cpp PROPERTIES COMPILE_OPTIONS "-O3")

add_library(ron_render STATIC ${RON_RENDER_SOURCES})

target_include_directories(ron_render PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/glad/include
)

target_compile_options(ron_render PRIVATE
    -Wall -Wextra -Wpedantic -Wshadow -Werror=return-type
    -march=x86-64-v2 -fno-strict-aliasing
    $<$<CONFIG:Debug>:-O0 -g3>
    $<$<NOT:$<CONFIG:Debug>>:-O3 -fomit-frame-pointer>
)

target_compile_definitions(ron_render PUBLIC
    $<$<CONFIG:Debug>:RON_DEBUG=1>
    $<$<NOT:$<CONFIG:Debug>>:NDEBUG RON_DEBUG=0>
)

target_link_libraries(ron_render PUBLIC glad SDL3::SDL3-static ron_core ron_assets)
if(WIN32)
    target_link_libraries(ron_render PUBLIC winmm imm32 setupapi version ole32 oleaut32 gdi32 user32 shell32 advapi32)
endif()

# C++ executable ron_render_tests
set(RON_RENDER_TEST_SOURCES
    tests/render/test_main.cpp
    tests/render/gl_fixture.cpp
    tests/render/scene_fixture.cpp
    tests/render/test_context.cpp
    tests/render/test_vram.cpp
    tests/render/test_shader.cpp
    tests/render/test_frame.cpp
    tests/render/test_targets.cpp
    tests/render/test_camera.cpp
    tests/render/test_terrain_cpu.cpp
    tests/render/test_cdlod.cpp
    tests/render/test_terrain_gpu.cpp
    tests/render/test_ocean.cpp
    tests/render/test_overlay_cpu.cpp
    tests/render/test_jfa.cpp
    tests/render/test_crater_gpu.cpp
    tests/render/test_fog_gpu.cpp
    tests/render/test_overlay_scene.cpp
    tests/render/test_minimap.cpp
    tests/render/test_entity_cpu.cpp
    tests/render/test_entity_assets.cpp
    tests/render/test_entity_cull.cpp
    tests/render/test_entity_draw.cpp
    tests/render/test_entity_perf.cpp
    tests/render/test_light_cpu.cpp
    tests/render/test_light_gpu.cpp
    tests/render/test_shadow_gpu.cpp
    tests/render/test_lit_scene.cpp
)

add_executable(ron_render_tests ${RON_RENDER_TEST_SOURCES})

target_include_directories(ron_render_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/glad/include
)

target_compile_options(ron_render_tests PRIVATE
    -Wall -Wextra -Wpedantic -Wshadow -Werror=return-type
    -march=x86-64-v2 -fno-strict-aliasing
    $<$<CONFIG:Debug>:-O0 -g3>
    $<$<NOT:$<CONFIG:Debug>>:-O3 -fomit-frame-pointer>
)

target_compile_definitions(ron_render_tests PRIVATE
    $<$<CONFIG:Debug>:RON_DEBUG=1>
    $<$<NOT:$<CONFIG:Debug>>:NDEBUG RON_DEBUG=0>
    RON_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(ron_render_tests PRIVATE ron_render glad SDL3::SDL3-static ron_core ron_assets)
if(WIN32)
    target_link_libraries(ron_render_tests PRIVATE winmm imm32 setupapi version ole32 oleaut32 gdi32 user32 shell32 advapi32)
endif()
if(MINGW)
    target_link_options(ron_render_tests PRIVATE -Wl,--stack,16777216 -static -static-libgcc -static-libstdc++)
endif()

# C++ executable ron_view
add_executable(ron_view src/render/ron_view_main.cpp)

target_include_directories(ron_view PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/glad/include
)

target_compile_options(ron_view PRIVATE
    -Wall -Wextra -Wpedantic -Wshadow -Werror=return-type
    -march=x86-64-v2 -fno-strict-aliasing
    $<$<CONFIG:Debug>:-O0 -g3>
    $<$<NOT:$<CONFIG:Debug>>:-O3 -fomit-frame-pointer>
)

target_compile_definitions(ron_view PRIVATE
    $<$<CONFIG:Debug>:RON_DEBUG=1>
    $<$<NOT:$<CONFIG:Debug>>:NDEBUG RON_DEBUG=0>
    RON_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(ron_view PRIVATE ron_render glad SDL3::SDL3-static ron_core ron_assets)
if(WIN32)
    target_link_libraries(ron_view PRIVATE winmm imm32 setupapi version ole32 oleaut32 gdi32 user32 shell32 advapi32)
endif()
if(MINGW)
    target_link_options(ron_view PRIVATE -Wl,--stack,16777216 -static -static-libgcc -static-libstdc++)
endif()


