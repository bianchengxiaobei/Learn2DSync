cmake_minimum_required(VERSION 3.24)

project(nsync VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(SDL3 CONFIG REQUIRED)
find_package(bgfx CONFIG REQUIRED)
find_package(RmlUi CONFIG REQUIRED)
find_package(kcp CONFIG REQUIRED)
find_package(Stb REQUIRED)

if(NOT BGFX_SHADER_INCLUDE_PATH)
    message(FATAL_ERROR "BGFX_SHADER_INCLUDE_PATH is not set by the bgfx package")
endif()

set(SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(SHADER_OUT "${CMAKE_CURRENT_BINARY_DIR}/shaders")

add_custom_command(
    OUTPUT "${SHADER_OUT}/vs_dx11.bin"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${SHADER_OUT}"
    COMMAND $<TARGET_FILE:bgfx::shaderc>
        -f "${SHADER_DIR}/sprite.vert"
        -o "${SHADER_OUT}/vs_dx11.bin"
        --type vertex
        --platform windows
        -p s_5_0
        -i "${BGFX_SHADER_INCLUDE_PATH}"
        --varyingdef "${SHADER_DIR}/varying.def.sc"
    DEPENDS
        "${SHADER_DIR}/sprite.vert"
        "${SHADER_DIR}/varying.def.sc"
    VERBATIM
)

add_custom_command(
    OUTPUT "${SHADER_OUT}/fs_dx11.bin"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${SHADER_OUT}"
    COMMAND $<TARGET_FILE:bgfx::shaderc>
        -f "${SHADER_DIR}/sprite.frag"
        -o "${SHADER_OUT}/fs_dx11.bin"
        --type fragment
        --platform windows
        -p s_5_0
        -i "${BGFX_SHADER_INCLUDE_PATH}"
        --varyingdef "${SHADER_DIR}/varying.def.sc"
    DEPENDS
        "${SHADER_DIR}/sprite.frag"
        "${SHADER_DIR}/varying.def.sc"
    VERBATIM
)

add_custom_target(nsync_shaders DEPENDS
    "${SHADER_OUT}/vs_dx11.bin"
    "${SHADER_OUT}/fs_dx11.bin"
)

add_library(nsync_core STATIC
    src/core/aim.cpp
    src/core/animation.cpp
    src/core/atlas.cpp
    src/core/byte_io.cpp
    src/core/content.cpp
    src/core/entity.cpp
    src/core/fixed_step.cpp
    src/core/geometry.cpp
    src/core/hash.cpp
    src/core/hero_rig.cpp
    src/core/json.cpp
    src/core/map.cpp
    src/core/replay.cpp
    src/core/rng.cpp
    src/core/snapshot.cpp
    src/core/world.cpp
    src/shared/asset_watcher.cpp
    src/shared/config.cpp
    src/shared/crash_handler.cpp
    src/shared/log.cpp
    src/shared/pak.cpp
    src/shared/paths.cpp
    src/shared/resource.cpp
    src/shared/update_check.cpp
    src/shared/version.cpp
    src/shared/timer_resolution.cpp
    src/net/local_net.cpp
)

target_include_directories(nsync_core PUBLIC src)
target_compile_features(nsync_core PUBLIC cxx_std_20)
target_compile_definitions(nsync_core PUBLIC
    NOMINMAX
    NSYNC_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    NSYNC_VERSION="${PROJECT_VERSION}"
    NSYNC_BUILD_CONFIG="$<CONFIG>"
)

if(WIN32)
    target_link_libraries(nsync_core PUBLIC dbghelp winhttp winmm)
endif()

add_library(nsync_netlib STATIC
    src/net/protocol.cpp
    src/net/kcp_transport.cpp
    src/net/wan_trace.cpp
    src/net/real_client.cpp
    src/net/real_server.cpp
    src/net/server_clock.cpp
)

target_include_directories(nsync_netlib PUBLIC src)
target_compile_features(nsync_netlib PUBLIC cxx_std_20)
target_link_libraries(nsync_netlib PUBLIC
    nsync_core
    kcp::kcp
    ws2_32
)

add_executable(nsync_client
    src/client/audio.cpp
    src/client/client_settings.cpp
    src/client/localization.cpp
    src/client/main.cpp
    src/client/bgfx_app.cpp
    src/client/bgfx_ui.cpp
    src/client/game_ui.cpp
    src/client/image_loader.cpp
    src/client/rml_resource_file.cpp
    src/client/world_renderer.cpp
)

target_compile_definitions(nsync_client PRIVATE
    SDL_MAIN_HANDLED
    _CRT_SECURE_NO_WARNINGS
)

target_link_libraries(nsync_client PRIVATE
    nsync_core
    nsync_netlib
    SDL3::SDL3
    bgfx::bgfx
    bgfx::bx
    bgfx::bimg
    RmlUi::RmlUi
)

target_include_directories(nsync_client PRIVATE ${Stb_INCLUDE_DIR})

add_dependencies(nsync_client nsync_shaders)

add_executable(nsync_server
    src/server/main.cpp
    src/server/server_app.cpp
)

target_link_libraries(nsync_server PRIVATE nsync_core nsync_netlib)

add_executable(nsync_tests
    tests/test_main.cpp
    tests/test_determinism.cpp
    tests/test_net.cpp
    src/core/fixed_test.cpp
)

target_link_libraries(nsync_tests PRIVATE nsync_core nsync_netlib)

add_executable(nsync_pak src/tools/pak_pack.cpp)
target_link_libraries(nsync_pak PRIVATE nsync_core)

add_executable(nsync_mapc src/tools/map_compiler.cpp)
target_link_libraries(nsync_mapc PRIVATE nsync_core)

add_executable(nsync_datac src/tools/data_check.cpp)
target_link_libraries(nsync_datac PRIVATE nsync_core)

add_executable(nsync_atlas src/tools/atlas_packer.cpp)
target_include_directories(nsync_atlas PRIVATE ${Stb_INCLUDE_DIR})
target_link_libraries(nsync_atlas PRIVATE nsync_core)

add_executable(nsync_replay src/tools/replay_main.cpp)
target_link_libraries(nsync_replay PRIVATE nsync_core)
add_dependencies(nsync_tests nsync_replay)

add_executable(nsync_net src/tools/net_main.cpp)
target_link_libraries(nsync_net PRIVATE nsync_core)

add_executable(nsync_net_real src/tools/net_real_main.cpp)
target_link_libraries(nsync_net_real PRIVATE nsync_netlib)

add_executable(nsync_soak src/tools/soak_main.cpp)
target_link_libraries(nsync_soak PRIVATE nsync_netlib)

if(MSVC)
    target_compile_options(nsync_core PRIVATE /utf-8)
    target_compile_options(nsync_client PRIVATE /utf-8)
    target_compile_options(nsync_netlib PRIVATE /utf-8)
    target_compile_options(nsync_server PRIVATE /utf-8)
    target_compile_options(nsync_tests PRIVATE /utf-8)
    target_compile_options(nsync_pak PRIVATE /utf-8)
    target_compile_options(nsync_mapc PRIVATE /utf-8)
    target_compile_options(nsync_datac PRIVATE /utf-8)
    target_compile_options(nsync_atlas PRIVATE /utf-8)
    target_compile_options(nsync_replay PRIVATE /utf-8)
    target_compile_options(nsync_net PRIVATE /utf-8)
    target_compile_options(nsync_net_real PRIVATE /utf-8)
    target_compile_options(nsync_soak PRIVATE /utf-8)
endif()

add_custom_command(TARGET nsync_client POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:nsync_client>/assets"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/config"
        "$<TARGET_FILE_DIR:nsync_client>/config"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${SHADER_OUT}"
        "$<TARGET_FILE_DIR:nsync_client>/shaders"
    VERBATIM
)

add_custom_command(TARGET nsync_server POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/config"
        "$<TARGET_FILE_DIR:nsync_server>/config"
    VERBATIM
)

set_target_properties(nsync_client PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:nsync_client>"
)
set_target_properties(nsync_server PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:nsync_server>"
)
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT nsync_client)

enable_testing()

add_test(NAME nsync_core_tests COMMAND nsync_tests)
add_test(NAME nsync_replay_golden COMMAND nsync_replay --ticks 120 --hash-every 10 --verify)
add_test(NAME nsync_content_check COMMAND nsync_datac --in "${CMAKE_CURRENT_SOURCE_DIR}/assets/data/content.json" --check)
add_test(NAME nsync_atlas_check COMMAND nsync_atlas --in "${CMAKE_CURRENT_SOURCE_DIR}/assets/sprites/source" --check)
set_tests_properties(nsync_atlas_check PROPERTIES TIMEOUT 20)
add_test(NAME nsync_map_compile COMMAND nsync_mapc --in "${CMAKE_CURRENT_SOURCE_DIR}/assets/maps/arena_01.json" --check)
set_tests_properties(nsync_map_compile PROPERTIES TIMEOUT 20)
add_test(NAME nsync_map_compile_02 COMMAND nsync_mapc --in "${CMAKE_CURRENT_SOURCE_DIR}/assets/maps/arena_02.json" --check)
add_test(NAME nsync_map_compile_world1 COMMAND nsync_mapc --in "${CMAKE_CURRENT_SOURCE_DIR}/assets/maps/world1.json" --check)
add_test(NAME nsync_map_compile_crystal01 COMMAND nsync_mapc --in "${CMAKE_CURRENT_SOURCE_DIR}/assets/maps/crystal_01.json" --check)
add_test(NAME nsync_map_compile_crystal02 COMMAND nsync_mapc --in "${CMAKE_CURRENT_SOURCE_DIR}/assets/maps/crystal_02.json" --check)
add_test(NAME nsync_map_compile_world3 COMMAND nsync_mapc --in "${CMAKE_CURRENT_SOURCE_DIR}/assets/maps/world3.json" --check)
add_test(NAME nsync_map_compile_world5 COMMAND nsync_mapc --in "${CMAKE_CURRENT_SOURCE_DIR}/assets/maps/world5.json" --check)
add_test(NAME nsync_map_compile_world6 COMMAND nsync_mapc --in "${CMAKE_CURRENT_SOURCE_DIR}/assets/maps/world6.json" --check)
add_test(NAME nsync_map_compile_world8 COMMAND nsync_mapc --in "${CMAKE_CURRENT_SOURCE_DIR}/assets/maps/world8.json" --check)
add_test(NAME nsync_map_compile_world9 COMMAND nsync_mapc --in "${CMAKE_CURRENT_SOURCE_DIR}/assets/maps/world9.json" --check)
add_test(NAME nsync_map_compile_world10 COMMAND nsync_mapc --in "${CMAKE_CURRENT_SOURCE_DIR}/assets/maps/world10.json" --check)
add_test(NAME nsync_map_compile_world_flag_capture COMMAND nsync_mapc --in "${CMAKE_CURRENT_SOURCE_DIR}/assets/maps/world_flag_capture.json" --check)
set_tests_properties(nsync_map_compile_02 PROPERTIES TIMEOUT 20)
set_tests_properties(nsync_map_compile_crystal01 PROPERTIES TIMEOUT 20)
set_tests_properties(nsync_map_compile_crystal02 PROPERTIES TIMEOUT 20)
set_tests_properties(
    nsync_map_compile_world3
    nsync_map_compile_world5
    nsync_map_compile_world6
    nsync_map_compile_world8
    nsync_map_compile_world9
    nsync_map_compile_world10
    nsync_map_compile_world_flag_capture
    PROPERTIES TIMEOUT 20)
add_test(NAME nsync_client_help COMMAND nsync_client --help)
add_test(NAME nsync_server_smoke COMMAND nsync_server --ticks 5 --tick-rate 60 --log-level warn)
set_tests_properties(nsync_server_smoke PROPERTIES TIMEOUT 20)
add_test(NAME nsync_net_smoke COMMAND nsync_net --ticks 600 --latency-ms 150 --jitter-ms 30 --log-every 0)
set_tests_properties(nsync_net_smoke PROPERTIES TIMEOUT 60)
add_test(NAME nsync_net_loopback COMMAND nsync_net_real --loopback --ticks 300 --players 2 --input-delay 16 --log-level warn)
set_tests_properties(nsync_net_loopback PROPERTIES TIMEOUT 30)
add_test(NAME nsync_net_map_arena02 COMMAND nsync_net_real --loopback --ticks 180 --players 2 --input-delay 16 --map "${CMAKE_CURRENT_SOURCE_DIR}/assets/maps/arena_02.json" --log-level warn)
add_test(NAME nsync_net_map_world1 COMMAND nsync_net_real --loopback --ticks 300 --players 2 --humans 2 --input-delay 16 --map "${CMAKE_CURRENT_SOURCE_DIR}/assets/maps/world1.json" --log-level warn)
set_tests_properties(nsync_net_map_arena02 PROPERTIES TIMEOUT 30)
add_test(NAME nsync_net_latency_50 COMMAND nsync_net_real --loopback --ticks 300 --players 2 --input-delay 16 --rtt-ms 50 --log-level warn)
set_tests_properties(nsync_net_latency_50 PROPERTIES TIMEOUT 30)
add_test(NAME nsync_net_latency_150 COMMAND nsync_net_real --loopback --ticks 300 --players 2 --input-delay 16 --rtt-ms 150 --log-level warn)
set_tests_properties(nsync_net_latency_150 PROPERTIES TIMEOUT 30)
add_test(NAME nsync_net_loopback_legacy COMMAND nsync_net_real --loopback --ticks 300 --players 2 --input-delay 16 --input-sync-model legacy --log-level warn)
set_tests_properties(nsync_net_loopback_legacy PROPERTIES TIMEOUT 30)
add_test(NAME nsync_net_late_input_replay COMMAND nsync_net_real --loopback --ticks 180 --players 2 --input-delay 16 --rtt-ms 50 --client-lag --log-level warn)
set_tests_properties(nsync_net_late_input_replay PROPERTIES TIMEOUT 30)
add_test(NAME nsync_net_sync_time_to_step COMMAND nsync_net_real --loopback --ticks 180 --players 2 --input-delay 16 --client-lag --sync-time-to-step-ms 100 --log-level warn)
set_tests_properties(nsync_net_sync_time_to_step PROPERTIES TIMEOUT 30)
add_test(NAME nsync_control_loopback COMMAND nsync_net_real --loopback --mode control --ticks 360 --players 2 --humans 2 --input-delay 16 --log-level warn)
set_tests_properties(nsync_control_loopback PROPERTIES TIMEOUT 40)
add_test(NAME nsync_crystal_loopback COMMAND nsync_net_real --loopback --mode crystal --ticks 360 --players 2 --humans 2 --input-delay 16 --map "${CMAKE_CURRENT_SOURCE_DIR}/assets/maps/crystal_01.json" --log-level warn)
set_tests_properties(nsync_crystal_loopback PROPERTIES TIMEOUT 40)
add_test(NAME nsync_control_world5_loopback COMMAND nsync_net_real --loopback --mode control --ticks 360 --players 2 --humans 2 --input-delay 16 --map "${CMAKE_CURRENT_SOURCE_DIR}/assets/maps/world5.json" --log-level warn)
set_tests_properties(nsync_control_world5_loopback PROPERTIES TIMEOUT 40)
add_test(NAME nsync_lobby_loopback COMMAND nsync_net_real --loopback --lobby --players 10 --humans 2 --ticks 420 --match-ticks 300 --input-delay 16 --log-level warn)
set_tests_properties(nsync_lobby_loopback PROPERTIES TIMEOUT 40)
add_test(NAME nsync_lobby_reconnect COMMAND nsync_net_real --loopback --lobby --players 4 --humans 2 --ticks 360 --match-ticks 300 --drop-at 100 --resume-at 180 --input-delay 16 --log-level warn)
set_tests_properties(nsync_lobby_reconnect PROPERTIES TIMEOUT 40)
add_test(NAME nsync_lobby_rematch COMMAND nsync_net_real --loopback --lobby --players 4 --humans 2 --ticks 760 --match-ticks 300 --auto-rematch --input-delay 16 --log-level warn)
set_tests_properties(nsync_lobby_rematch PROPERTIES TIMEOUT 50)
add_test(NAME nsync_lobby_5v5 COMMAND nsync_net_real --loopback --lobby --players 10 --humans 10 --ticks 180 --match-ticks 120 --input-delay 16 --log-level warn)
set_tests_properties(nsync_lobby_5v5 PROPERTIES TIMEOUT 30)
add_test(NAME nsync_lobby_5v5_client_lag COMMAND nsync_net_real --loopback --lobby --players 10 --humans 10 --ticks 180 --match-ticks 180 --input-delay 16 --rtt-ms 50 --client-lag --log-level warn)
set_tests_properties(nsync_lobby_5v5_client_lag PROPERTIES TIMEOUT 40)
add_test(NAME nsync_server_net_smoke COMMAND nsync_server --net --port 40180 --players 2 --ticks 5 --log-level warn)
set_tests_properties(nsync_server_net_smoke PROPERTIES TIMEOUT 20)
add_test(
    NAME nsync_net_real_process
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_CURRENT_SOURCE_DIR}/scripts/real_process_smoke.ps1"
        -BinDir "$<TARGET_FILE_DIR:nsync_net_real>"
        -Port 40195
        -Ticks 240
        -MapPath "${CMAKE_CURRENT_SOURCE_DIR}/assets/maps/arena_01.json"
)
set_tests_properties(nsync_net_real_process PROPERTIES TIMEOUT 45)
add_test(
    NAME nsync_net_real_process_10p_150
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_CURRENT_SOURCE_DIR}/scripts/real_process_smoke.ps1"
        -BinDir "$<TARGET_FILE_DIR:nsync_net_real>"
        -Port 40200
        -Ticks 180
        -ClientCount 10
        -RttMs 150
        -InputDelay 21
        -ServerTicks 900
        -MapPath "${CMAKE_CURRENT_SOURCE_DIR}/assets/maps/arena_01.json"
)
set_tests_properties(nsync_net_real_process_10p_150 PROPERTIES TIMEOUT 60)
add_test(
    NAME nsync_net_real_process_multi_match
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_CURRENT_SOURCE_DIR}/scripts/real_process_smoke.ps1"
        -BinDir "$<TARGET_FILE_DIR:nsync_net_real>"
        -Port 40202
        -Ticks 500
        -ClientCount 2
        -RttMs 0
        -InputDelay 16
        -ServerTicks 900
        -MatchDurationTicks 120
        -MatchEndDelayMs 500
        -MinMatches 2
        -AutoRematch
        -MapPath "${CMAKE_CURRENT_SOURCE_DIR}/assets/maps/arena_01.json"
)
set_tests_properties(nsync_net_real_process_multi_match PROPERTIES TIMEOUT 60)
add_test(
    NAME nsync_net_real_process_step_scheduler
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_CURRENT_SOURCE_DIR}/scripts/real_process_smoke.ps1"
        -BinDir "$<TARGET_FILE_DIR:nsync_net_real>"
        -Port 40203
        -Ticks 300
        -ClientCount 2
        -RttMs 50
        -InputDelay 16
        -ServerTicks 600
        -SyncTimeToStepMs 100
        -ClientLag
        -FixedStepScheduler
        -MapPath "${CMAKE_CURRENT_SOURCE_DIR}/assets/maps/arena_01.json"
)
set_tests_properties(nsync_net_real_process_step_scheduler PROPERTIES TIMEOUT 60)
add_test(
    NAME nsync_net_real_process_wan_250
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_CURRENT_SOURCE_DIR}/scripts/real_process_smoke.ps1"
        -BinDir "$<TARGET_FILE_DIR:nsync_net_real>"
        -Port 40204
        -Ticks 300
        -ClientCount 2
        -RttMs 250
        -InputDelay 16
        -ServerTicks 600
        -ClientLag
        -FixedStepScheduler
        -MapPath "${CMAKE_CURRENT_SOURCE_DIR}/assets/maps/arena_01.json"
)
set_tests_properties(nsync_net_real_process_wan_250 PROPERTIES TIMEOUT 60)
add_test(
    NAME nsync_net_real_process_wan_400
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_CURRENT_SOURCE_DIR}/scripts/real_process_smoke.ps1"
        -BinDir "$<TARGET_FILE_DIR:nsync_net_real>"
        -Port 40205
        -Ticks 600
        -ClientCount 2
        -RttMs 400
        -InputDelay 16
        -ServerTicks 900
        -ClientLag
        -FixedStepScheduler
        -MapPath "${CMAKE_CURRENT_SOURCE_DIR}/assets/maps/arena_01.json"
)
set_tests_properties(nsync_net_real_process_wan_400 PROPERTIES TIMEOUT 60)
add_test(
    NAME nsync_net_real_process_wan_loss
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_CURRENT_SOURCE_DIR}/scripts/real_process_smoke.ps1"
        -BinDir "$<TARGET_FILE_DIR:nsync_net_real>"
        -Port 40206
        -Ticks 300
        -ClientCount 2
        -RttMs 250
        -JitterMs 30
        -LossPercent 5
        -DuplicatePercent 10
        -InputDelay 16
        -ServerTicks 600
        -ClientLag
        -FixedStepScheduler
        -RequireWanSimulation
        -MapPath "${CMAKE_CURRENT_SOURCE_DIR}/assets/maps/arena_01.json"
)
set_tests_properties(nsync_net_real_process_wan_loss PROPERTIES TIMEOUT 60)
add_test(
    NAME nsync_net_real_process_wan_burst
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_CURRENT_SOURCE_DIR}/scripts/real_process_smoke.ps1"
        -BinDir "$<TARGET_FILE_DIR:nsync_net_real>"
        -Port 40207
        -Ticks 300
        -ClientCount 2
        -RttMs 250
        -JitterMs 30
        -LossPercent 3
        -BurstLength 4
        -DuplicatePercent 5
        -InputDelay 16
        -ServerTicks 600
        -ClientLag
        -FixedStepScheduler
        -RequireWanSimulation
        -MapPath "${CMAKE_CURRENT_SOURCE_DIR}/assets/maps/arena_01.json"
)
set_tests_properties(nsync_net_real_process_wan_burst PROPERTIES TIMEOUT 60)
add_test(
    NAME nsync_net_real_process_wan_spike
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_CURRENT_SOURCE_DIR}/scripts/real_process_smoke.ps1"
        -BinDir "$<TARGET_FILE_DIR:nsync_net_real>"
        -Port 40208
        -Ticks 600
        -ClientCount 2
        -RttMs 250
        -SpikeMs 300
        -SpikeIntervalMs 1500
        -SpikeDurationMs 300
        -InputDelay 16
        -ServerTicks 900
        -ClientLag
        -FixedStepScheduler
        -RequireSpike
        -MapPath "${CMAKE_CURRENT_SOURCE_DIR}/assets/maps/arena_01.json"
)
set_tests_properties(nsync_net_real_process_wan_spike PROPERTIES TIMEOUT 60)
add_test(
    NAME nsync_net_real_process_wan_trace
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_CURRENT_SOURCE_DIR}/scripts/real_process_smoke.ps1"
        -BinDir "$<TARGET_FILE_DIR:nsync_net_real>"
        -Port 40209
        -Ticks 600
        -ClientCount 2
        -RttMs 0
        -InputDelay 16
        -ServerTicks 900
        -ClientLag
        -FixedStepScheduler
        -RequireTrace
        -TraceFile "${CMAKE_CURRENT_SOURCE_DIR}/assets/net/wan_trace_sample.csv"
        -MapPath "${CMAKE_CURRENT_SOURCE_DIR}/assets/maps/arena_01.json"
)
set_tests_properties(nsync_net_real_process_wan_trace PROPERTIES TIMEOUT 60)
add_test(
    NAME nsync_net_real_ack_rewrite
    COMMAND $<TARGET_FILE:nsync_net_real>
        --loopback
        --ticks 600
        --players 2
        --input-delay 16
        --rtt-ms 50
        --client-lag
        --log-level warn
)
set_tests_properties(
    nsync_net_real_ack_rewrite
    PROPERTIES
        TIMEOUT 30
        PASS_REGULAR_EXPRESSION "server acks mismatches=0 rewritten=[1-9][0-9]*")
add_test(
    NAME nsync_net_real_process_10_matches
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_CURRENT_SOURCE_DIR}/scripts/real_matches_soak.ps1"
        -BinDir "$<TARGET_FILE_DIR:nsync_net_real>"
        -MatchCount 10
        -BasePort 41200
        -ClientsPerMatch 2
        -Ticks 900
        -RttMs 150
        -TimeoutSeconds 180
        -ClientLag
        -FixedStepScheduler
        -MapPath "${CMAKE_CURRENT_SOURCE_DIR}/assets/maps/arena_01.json"
)
set_tests_properties(nsync_net_real_process_10_matches PROPERTIES TIMEOUT 240)
add_test(NAME nsync_soak_smoke COMMAND nsync_soak --minutes 1 --matches 1 --players 10 --humans 2)
set_tests_properties(nsync_soak_smoke PROPERTIES TIMEOUT 60)
add_test(NAME nsync_soak_10m COMMAND nsync_soak --minutes 10 --matches 1 --players 10 --humans 10)
set_tests_properties(nsync_soak_10m PROPERTIES TIMEOUT 600)
add_test(NAME nsync_soak_10_matches COMMAND nsync_soak --minutes 1 --matches 10 --players 10 --humans 2)
set_tests_properties(nsync_soak_10_matches PROPERTIES TIMEOUT 120)




set_tests_properties(
    nsync_core_tests
    nsync_replay_golden
    nsync_content_check
    nsync_atlas_check
    nsync_map_compile
    nsync_map_compile_02
    nsync_map_compile_world1
    nsync_map_compile_crystal01
    nsync_map_compile_crystal02
    nsync_map_compile_world3
    nsync_map_compile_world5
    nsync_map_compile_world6
    nsync_map_compile_world8
    nsync_map_compile_world9
    nsync_map_compile_world10
    nsync_map_compile_world_flag_capture
    nsync_client_help
    nsync_server_smoke
    PROPERTIES LABELS "FAST"
)
set_tests_properties(
    nsync_net_smoke
    nsync_server_net_smoke
    PROPERTIES LABELS "FAST;NETWORK"
)
set_tests_properties(
    nsync_net_loopback
    nsync_net_map_arena02
    nsync_net_map_world1
    nsync_net_latency_50
    nsync_net_latency_150
    nsync_net_loopback_legacy
    nsync_net_late_input_replay
    nsync_net_sync_time_to_step
    nsync_control_loopback
    nsync_crystal_loopback
    nsync_control_world5_loopback
    nsync_lobby_loopback
    nsync_lobby_reconnect
    nsync_lobby_rematch
    nsync_lobby_5v5
    nsync_lobby_5v5_client_lag
    PROPERTIES LABELS "NETWORK"
)
set_tests_properties(
    nsync_net_real_process
    nsync_net_real_process_10p_150
    nsync_net_real_process_multi_match
    nsync_net_real_process_step_scheduler
    nsync_net_real_process_wan_250
    nsync_net_real_process_wan_400
    nsync_net_real_process_wan_loss
    nsync_net_real_process_wan_burst
    nsync_net_real_process_wan_spike
    nsync_net_real_process_wan_trace
    nsync_net_real_ack_rewrite
    nsync_net_real_process_10_matches
    PROPERTIES LABELS "NETWORK;SLOW"
)
set_tests_properties(
    nsync_soak_smoke
    PROPERTIES LABELS "SOAK"
)
set_tests_properties(
    nsync_soak_10m
    nsync_soak_10_matches
    PROPERTIES LABELS "SOAK;SLOW"
)