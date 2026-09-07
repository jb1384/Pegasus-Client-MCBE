cmake_minimum_required(VERSION 3.24)

project(BedrockUtilityFramework VERSION 0.1.0 LANGUAGES CXX)

add_library(BedrockNavigation STATIC
    src/navigation/Planner.cpp
    src/navigation/Controller.cpp
    src/navigation/NavigationCommands.cpp
)
target_compile_features(BedrockNavigation PUBLIC cxx_std_20)
target_include_directories(BedrockNavigation PUBLIC src)
if(MSVC)
    target_compile_options(BedrockNavigation PRIVATE /W4 /permissive- /EHsc)
endif()

if(NOT WIN32)
    message(FATAL_ERROR "BedrockUtilityFramework currently targets 64-bit Windows only.")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Configure this project with a 64-bit generator/toolchain.")
endif()

add_library(BedrockUtilityFramework SHARED
    src/dllmain.cpp
    src/framework/Framework.cpp
    src/framework/Logger.cpp
    src/framework/ModuleManager.cpp
    src/framework/Commands.cpp
    src/integration/ChatCommands.cpp
    src/framework/Config.cpp
    src/integration/SplashTextHook.cpp
    src/integration/Renderer.cpp
    src/modules/AntiKnockbackModule.cpp
    src/modules/CriticalsModule.cpp
    src/modules/ReachModule.cpp
    src/modules/GhostHandModule.cpp
    src/modules/GameplayModules.cpp
    src/modules/SpeedModule.cpp
    src/modules/XrayModule.cpp
    src/modules/FullbrightModule.cpp
    src/modules/BaritoneModule.cpp
    src/integration/NativeNavigationAdapter.cpp
)
target_link_libraries(BedrockUtilityFramework PRIVATE BedrockNavigation)

target_include_directories(BedrockUtilityFramework PRIVATE src)
target_compile_features(BedrockUtilityFramework PRIVATE cxx_std_20)
target_compile_definitions(BedrockUtilityFramework PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
)

if(MSVC)
    target_compile_options(BedrockUtilityFramework PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(BedrockUtilityFramework PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(BedrockUtilityFramework PROPERTIES
    OUTPUT_NAME "BedrockUtilityFramework.Xray"
)

option(BUF_BUILD_RESEARCH_PROBES "Build historical native research probes" OFF)
option(BUF_BUILD_NAVIGATION_PROBE "Build passive navigation input diagnostics" OFF)
if(BUF_BUILD_NAVIGATION_PROBE)
    add_library(BedrockNavigationProbe SHARED research/navigation/NavigationProbe.cpp)
    target_include_directories(BedrockNavigationProbe PRIVATE src)
    target_compile_features(BedrockNavigationProbe PRIVATE cxx_std_20)
    target_compile_definitions(BedrockNavigationProbe PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
    add_library(BedrockNavigationActionProbe SHARED research/navigation/NavigationProbe.cpp)
    target_include_directories(BedrockNavigationActionProbe PRIVATE src)
    target_compile_features(BedrockNavigationActionProbe PRIVATE cxx_std_20)
    target_compile_definitions(BedrockNavigationActionProbe PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE BUF_NAVIGATION_ACTION_PROBE)
    add_library(BedrockNavigationMiningProbe SHARED research/navigation/NavigationProbe.cpp)
    target_include_directories(BedrockNavigationMiningProbe PRIVATE src)
    target_compile_features(BedrockNavigationMiningProbe PRIVATE cxx_std_20)
    target_compile_definitions(BedrockNavigationMiningProbe PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE BUF_NAVIGATION_MINING_PROBE)
    add_library(BedrockNavigationMiningProbeV2 SHARED research/navigation/NavigationProbe.cpp)
    target_include_directories(BedrockNavigationMiningProbeV2 PRIVATE src)
    target_compile_features(BedrockNavigationMiningProbeV2 PRIVATE cxx_std_20)
    target_compile_definitions(BedrockNavigationMiningProbeV2 PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE BUF_NAVIGATION_MINING_PROBE)
    add_library(BedrockNavigationWorldProbe SHARED research/navigation/WorldProbe.cpp)
    target_include_directories(BedrockNavigationWorldProbe PRIVATE src)
    target_compile_features(BedrockNavigationWorldProbe PRIVATE cxx_std_20)
    target_compile_definitions(BedrockNavigationWorldProbe PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
    add_library(BedrockNavigationSnapshotProbe SHARED research/navigation/SnapshotProbe.cpp src/integration/NativeNavigationAdapter.cpp)
    target_link_libraries(BedrockNavigationSnapshotProbe PRIVATE BedrockNavigation user32)
    target_compile_definitions(BedrockNavigationSnapshotProbe PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
    add_library(BedrockNavigationSnapshotProbeRelease SHARED research/navigation/SnapshotProbe.cpp src/integration/NativeNavigationAdapter.cpp)
    target_link_libraries(BedrockNavigationSnapshotProbeRelease PRIVATE BedrockNavigation user32)
    target_compile_definitions(BedrockNavigationSnapshotProbeRelease PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE BUF_SNAPSHOT_RELEASE_PROBE)
    add_library(BedrockNavigationRouteProbe SHARED research/navigation/RouteProbe.cpp src/integration/NativeNavigationAdapter.cpp)
    target_link_libraries(BedrockNavigationRouteProbe PRIVATE BedrockNavigation user32)
    target_compile_definitions(BedrockNavigationRouteProbe PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
    add_library(BedrockNavigationRouteProbeV2 SHARED research/navigation/RouteProbe.cpp src/integration/NativeNavigationAdapter.cpp)
    target_link_libraries(BedrockNavigationRouteProbeV2 PRIVATE BedrockNavigation user32)
    target_compile_definitions(BedrockNavigationRouteProbeV2 PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE BUF_ROUTE_V2)
    add_library(BedrockNavigationRouteProbeV3 SHARED research/navigation/RouteProbe.cpp src/integration/NativeNavigationAdapter.cpp)
    target_link_libraries(BedrockNavigationRouteProbeV3 PRIVATE BedrockNavigation user32)
    target_compile_definitions(BedrockNavigationRouteProbeV3 PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE BUF_ROUTE_V3)
    add_library(BedrockNavigationRouteProbeV4 SHARED research/navigation/RouteProbe.cpp src/integration/NativeNavigationAdapter.cpp)
    target_link_libraries(BedrockNavigationRouteProbeV4 PRIVATE BedrockNavigation user32)
    target_compile_definitions(BedrockNavigationRouteProbeV4 PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE BUF_ROUTE_V4)
    add_library(BedrockNavigationRouteProbeV5 SHARED research/navigation/RouteProbe.cpp src/integration/NativeNavigationAdapter.cpp)
    target_link_libraries(BedrockNavigationRouteProbeV5 PRIVATE BedrockNavigation user32)
    target_compile_definitions(BedrockNavigationRouteProbeV5 PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE BUF_ROUTE_V5)
    add_library(BedrockNavigationRouteLogProbe SHARED research/navigation/RouteLogProbe.cpp)
    target_include_directories(BedrockNavigationRouteLogProbe PRIVATE src)
    target_compile_features(BedrockNavigationRouteLogProbe PRIVATE cxx_std_20)
    target_compile_definitions(BedrockNavigationRouteLogProbe PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
    add_library(BedrockNavigationRouteProbeV6 SHARED research/navigation/RouteProbe.cpp src/integration/NativeNavigationAdapter.cpp)
    target_link_libraries(BedrockNavigationRouteProbeV6 PRIVATE BedrockNavigation user32)
    target_compile_definitions(BedrockNavigationRouteProbeV6 PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE BUF_ROUTE_V5 BUF_ROUTE_V6)
endif()
if(BUF_BUILD_RESEARCH_PROBES)
    add_subdirectory(research/xray)
endif()

option(BUF_BUILD_SMOKE_HOST "Build the authorized self-loading smoke-test host" ON)

if(BUF_BUILD_SMOKE_HOST)
    add_executable(BedrockUtilitySmokeHost tests/smoke_host.cpp)
    target_compile_features(BedrockUtilitySmokeHost PRIVATE cxx_std_20)
    target_compile_definitions(BedrockUtilitySmokeHost PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        UNICODE
        _UNICODE
    )

    if(MSVC)
        target_compile_options(BedrockUtilitySmokeHost PRIVATE /W4 /permissive- /EHsc)
    endif()

    add_executable(BedrockUtilityOverlayHost tests/overlay_host.cpp)
    target_compile_features(BedrockUtilityOverlayHost PRIVATE cxx_std_20)
    target_compile_definitions(BedrockUtilityOverlayHost PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        UNICODE
        _UNICODE
    )

    if(MSVC)
        target_compile_options(BedrockUtilityOverlayHost PRIVATE /W4 /permissive- /EHsc)
    endif()
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(PegasusSplashTextTest tests/splash_text_test.cpp)
    target_compile_features(PegasusSplashTextTest PRIVATE cxx_std_20)
    target_compile_definitions(PegasusSplashTextTest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
    add_test(NAME splash_text COMMAND PegasusSplashTextTest)
    add_executable(BedrockNavigationLayoutTest tests/navigation_layout_test.cpp)
    target_compile_features(BedrockNavigationLayoutTest PRIVATE cxx_std_20)
    target_compile_definitions(BedrockNavigationLayoutTest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
    target_include_directories(BedrockNavigationLayoutTest PRIVATE src)
    add_test(NAME navigation_layout COMMAND BedrockNavigationLayoutTest)
    add_executable(BedrockUtilityNavigationTest tests/navigation_test.cpp)
    target_link_libraries(BedrockUtilityNavigationTest PRIVATE BedrockNavigation)
    add_test(NAME navigation COMMAND BedrockUtilityNavigationTest)
    add_executable(BedrockUtilityBaritoneTest tests/baritone_module_test.cpp
        src/modules/BaritoneModule.cpp src/integration/NativeNavigationAdapter.cpp
        src/framework/ModuleManager.cpp src/framework/Commands.cpp src/framework/Logger.cpp)
    target_compile_definitions(BedrockUtilityBaritoneTest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
    target_link_libraries(BedrockUtilityBaritoneTest PRIVATE BedrockNavigation gdi32 user32)
    add_test(NAME baritone_module COMMAND BedrockUtilityBaritoneTest)
    add_executable(BedrockUtilityJetpackTest tests/jetpack_policy_test.cpp src/framework/Logger.cpp)
    target_compile_definitions(BedrockUtilityJetpackTest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
    target_compile_features(BedrockUtilityJetpackTest PRIVATE cxx_std_20)
    add_test(NAME jetpack_policy COMMAND BedrockUtilityJetpackTest)
    add_executable(BedrockUtilityCommandsTest tests/commands_test.cpp src/framework/ModuleManager.cpp src/framework/Commands.cpp src/framework/Logger.cpp)
    target_compile_definitions(BedrockUtilityCommandsTest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
    target_include_directories(BedrockUtilityCommandsTest PRIVATE src)
    target_compile_features(BedrockUtilityCommandsTest PRIVATE cxx_std_20)
    add_test(NAME commands COMMAND BedrockUtilityCommandsTest)
    add_executable(BedrockUtilityEspProjectionTest tests/esp_projection_test.cpp)
    target_compile_features(BedrockUtilityEspProjectionTest PRIVATE cxx_std_20)
    add_test(NAME esp_projection COMMAND BedrockUtilityEspProjectionTest)
    add_executable(BedrockUtilityXrayPolicyTest tests/xray_policy_test.cpp src/modules/XrayModule.cpp src/framework/Logger.cpp)
    target_compile_definitions(BedrockUtilityXrayPolicyTest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
    target_include_directories(BedrockUtilityXrayPolicyTest PRIVATE src)
    target_compile_features(BedrockUtilityXrayPolicyTest PRIVATE cxx_std_20)
    add_test(NAME xray_policy COMMAND BedrockUtilityXrayPolicyTest)
    add_executable(BedrockUtilityTriggerbotTest tests/triggerbot_policy_test.cpp src/framework/Logger.cpp)
    target_compile_definitions(BedrockUtilityTriggerbotTest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
    target_compile_features(BedrockUtilityTriggerbotTest PRIVATE cxx_std_20)
    add_test(NAME triggerbot_policy COMMAND BedrockUtilityTriggerbotTest)
    add_executable(BedrockUtilityFullbrightTest tests/fullbright_policy_test.cpp src/modules/FullbrightModule.cpp src/framework/Logger.cpp)
    target_compile_definitions(BedrockUtilityFullbrightTest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
    target_compile_features(BedrockUtilityFullbrightTest PRIVATE cxx_std_20)
    add_test(NAME fullbright_policy COMMAND BedrockUtilityFullbrightTest)
    add_executable(BedrockUtilityAirjumpTest tests/airjump_policy_test.cpp src/framework/Logger.cpp)
    target_compile_definitions(BedrockUtilityAirjumpTest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
    target_compile_features(BedrockUtilityAirjumpTest PRIVATE cxx_std_20)
    add_test(NAME airjump_policy COMMAND BedrockUtilityAirjumpTest)
    add_executable(BedrockUtilityGameplayTest tests/gameplay_policy_test.cpp src/framework/Logger.cpp)
    target_compile_definitions(BedrockUtilityGameplayTest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
    target_compile_features(BedrockUtilityGameplayTest PRIVATE cxx_std_20)
    add_test(NAME gameplay_policy COMMAND BedrockUtilityGameplayTest)
    add_executable(BedrockUtilityEntityReachTest tests/entity_reach_test.cpp src/framework/Logger.cpp)
    target_compile_definitions(BedrockUtilityEntityReachTest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
    target_compile_features(BedrockUtilityEntityReachTest PRIVATE cxx_std_20)
    add_test(NAME entity_reach COMMAND BedrockUtilityEntityReachTest)
    add_executable(BedrockUtilityGhostHandTest tests/ghost_hand_test.cpp src/framework/Logger.cpp)
    target_compile_definitions(BedrockUtilityGhostHandTest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
    target_compile_features(BedrockUtilityGhostHandTest PRIVATE cxx_std_20)
    add_test(NAME ghost_hand COMMAND BedrockUtilityGhostHandTest)
    add_executable(BedrockUtilityMenuTest tests/menu_interaction_test.cpp src/integration/Renderer.cpp src/framework/ModuleManager.cpp src/framework/Commands.cpp)
    target_compile_definitions(BedrockUtilityMenuTest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
    target_include_directories(BedrockUtilityMenuTest PRIVATE src)
    target_compile_features(BedrockUtilityMenuTest PRIVATE cxx_std_20)
    add_test(NAME menu_interaction COMMAND BedrockUtilityMenuTest)
    set_tests_properties(menu_interaction PROPERTIES SKIP_RETURN_CODE 77)
endif()

