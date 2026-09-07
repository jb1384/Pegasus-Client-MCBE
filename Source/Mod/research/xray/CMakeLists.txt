# Historical probes are opt-in and are not part of the working module.
add_library(BedrockUtilityXrayProbe SHARED
    probes/xray_probe_dll.cpp
)

add_library(BedrockUtilityXrayStageProbe SHARED
    probes/xray_stage_probe_dll.cpp
)

add_library(BedrockUtilityXrayRunProbe SHARED
    probes/xray_run_probe_dll.cpp
)

target_compile_features(BedrockUtilityXrayProbe PRIVATE cxx_std_20)
target_compile_definitions(BedrockUtilityXrayProbe PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
)

if(MSVC)
    target_compile_options(BedrockUtilityXrayProbe PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(BedrockUtilityXrayProbe PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(BedrockUtilityXrayProbe PROPERTIES
    OUTPUT_NAME "BedrockUtilityFramework.XrayProbe"
)

target_compile_features(BedrockUtilityXrayStageProbe PRIVATE cxx_std_20)
target_compile_definitions(BedrockUtilityXrayStageProbe PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
)

if(MSVC)
    target_compile_options(BedrockUtilityXrayStageProbe PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(BedrockUtilityXrayStageProbe PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(BedrockUtilityXrayStageProbe PROPERTIES
    OUTPUT_NAME "BedrockUtilityFramework.XrayStageProbe3"
)

target_compile_features(BedrockUtilityXrayRunProbe PRIVATE cxx_std_20)
target_compile_definitions(BedrockUtilityXrayRunProbe PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
)

if(MSVC)
    target_compile_options(BedrockUtilityXrayRunProbe PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(BedrockUtilityXrayRunProbe PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(BedrockUtilityXrayRunProbe PROPERTIES
    OUTPUT_NAME "BedrockUtilityFramework.XrayIdentityProbe8"
)

