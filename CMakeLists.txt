# vim: set sts=2 ts=2 sw=2 et:

cmake_minimum_required(VERSION 3.19)
project(CS2OpenWorld LANGUAGES CXX C)

include(CTest)
find_package(Threads REQUIRED)

option(OW_ENABLE_CACHE_FALLBACK "Build optional cache fallback components (disabled by default)." OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(JsonUtil)
include(CompilerSettings)
include(Package)

if (NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "CS2 Open World only builds for x86_64.")
endif ()

if (WIN32)
    if (NOT MSVC)
        message(FATAL_ERROR "The Windows build needs the Microsoft C++ compiler.")
    endif ()
elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    if (NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "The Linux build needs Clang.")
    endif ()
else ()
    message(FATAL_ERROR "CS2 Open World only builds for Windows or Linux.")
endif ()

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif ()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)


set(OW_SDK_ROOT "${CMAKE_SOURCE_DIR}/hl2sdk-cs2")
set(OW_MMS_ROOT "${CMAKE_SOURCE_DIR}/metamod-source")
set(OW_MANIFESTS_ROOT "${OW_MMS_ROOT}/hl2sdk-manifests/manifests")

foreach (_pair "OW_SDK_ROOT|CS2 SDK" "OW_MMS_ROOT|Metamod:Source" "OW_MANIFESTS_ROOT|HL2SDK manifests")
    string(REPLACE "|" ";" _pair_list "${_pair}")
    list(GET _pair_list 0 _var)
    list(GET _pair_list 1 _desc)
    if (NOT IS_DIRECTORY "${${_var}}")
        message(FATAL_ERROR "${_desc} is missing at ${${_var}}.")
    endif ()
endforeach ()


if (NOT EXISTS "${CMAKE_SOURCE_DIR}/plugin-metadata.json")
    message(FATAL_ERROR "plugin-metadata.json is missing. Run setup-openworld.ps1 first.")
endif ()
file(READ "${CMAKE_SOURCE_DIR}/plugin-metadata.json" _ow_metadata_json)
foreach (_key name alias display_name description author url log_tag license version)
    ow_json_string("${_ow_metadata_json}" _value ${_key})
    if (NOT DEFINED _value)
        message(FATAL_ERROR "plugin-metadata.json is missing: ${_key}.")
    endif ()
    string(TOUPPER "${_key}" _key_upper)
    set(OW_META_${_key_upper} "${_value}")
    unset(_value)
endforeach ()

if (NOT OW_META_NAME MATCHES "^[a-z0-9_-]+$")
    message(FATAL_ERROR "The plugin name may contain only lowercase letters, numbers, underscores, and hyphens.")
endif ()
if (NOT OW_META_ALIAS MATCHES "^[A-Za-z0-9_-]+$")
    message(FATAL_ERROR "The plugin alias may contain only letters, numbers, underscores, and hyphens.")
endif ()
if (NOT OW_META_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "The plugin version must use major.minor.patch format.")
endif ()

foreach (_field DISPLAY_NAME DESCRIPTION AUTHOR URL LOG_TAG LICENSE)
    set(_value "${OW_META_${_field}}")
    string(REPLACE "\\" "\\\\" _value "${_value}")
    string(REPLACE "\"" "\\\"" _value "${_value}")
    string(REPLACE "\r\n" "\\n" _value "${_value}")
    string(REPLACE "\n" "\\n" _value "${_value}")
    string(REPLACE "\r" "\\r" _value "${_value}")
    set(OW_META_${_field} "${_value}")
endforeach ()

configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/version_gen.h.in"
        "${CMAKE_BINARY_DIR}/versioning/version_gen.h"
        @ONLY
)


file(READ "${OW_MANIFESTS_ROOT}/cs2.json" _ow_cs2_manifest_json)

ow_json_string("${_ow_cs2_manifest_json}" OW_SDK_CODE code)
ow_json_string_array("${_ow_cs2_manifest_json}" _ow_sdk_include_paths include_paths)
set(OW_SDK_INCLUDE_DIRS "")
foreach (_p ${_ow_sdk_include_paths})
    list(APPEND OW_SDK_INCLUDE_DIRS "${OW_SDK_ROOT}/${_p}")
endforeach ()

ow_json_string_array("${_ow_cs2_manifest_json}" OW_SDK_LINUX_DEFINES linux defines)
ow_json_string_array("${_ow_cs2_manifest_json}" _ow_sdk_linux_postlink_libs linux x86_64 postlink_libs)
ow_json_string_array("${_ow_cs2_manifest_json}" _ow_sdk_linux_dynamic_libs linux x86_64 dynamic_libs)
ow_json_string("${_ow_cs2_manifest_json}" OW_SDK_LINUX_PROTOC_PATH linux protoc_path)

ow_json_string_array("${_ow_cs2_manifest_json}" _ow_sdk_windows_libs windows x86_64 libs)
ow_json_string("${_ow_cs2_manifest_json}" OW_SDK_WINDOWS_PROTOC_PATH windows protoc_path)


set(OW_SDK_LINUX_POSTLINK_LIBS "")
foreach (_lib ${_ow_sdk_linux_postlink_libs})
    list(APPEND OW_SDK_LINUX_POSTLINK_LIBS "${OW_SDK_ROOT}/${_lib}")
endforeach ()
set(OW_SDK_LINUX_DYNAMIC_LIBS "")
foreach (_lib ${_ow_sdk_linux_dynamic_libs})
    list(APPEND OW_SDK_LINUX_DYNAMIC_LIBS "${OW_SDK_ROOT}/${_lib}")
endforeach ()
set(OW_SDK_WINDOWS_LIBS "")
foreach (_lib ${_ow_sdk_windows_libs})
    list(APPEND OW_SDK_WINDOWS_LIBS "${OW_SDK_ROOT}/${_lib}")
endforeach ()

file(GLOB _ow_all_manifests "${OW_MANIFESTS_ROOT}/*.json")
set(OW_SE_DEFINES "")
foreach (_manifest ${_ow_all_manifests})
    file(READ "${_manifest}" _m_json)
    ow_json_string("${_m_json}" _m_define define)
    ow_json_string("${_m_json}" _m_code code)
    list(APPEND OW_SE_DEFINES "SE_${_m_define}=${_m_code}")
    unset(_m_define)
    unset(_m_code)
endforeach ()


file(GLOB_RECURSE OW_PLUGIN_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/src/core/*.cpp"
        "${CMAKE_SOURCE_DIR}/src/adapters/*.cpp"
        "${CMAKE_SOURCE_DIR}/src/modules/*.cpp"
        "${CMAKE_SOURCE_DIR}/src/services/*.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/*.cpp"
        "${CMAKE_SOURCE_DIR}/src/features/*.cpp"
        "${CMAKE_SOURCE_DIR}/src/features/*.c"
        "${CMAKE_SOURCE_DIR}/src/player/player.cpp"
        "${CMAKE_SOURCE_DIR}/src/player/player_manager.cpp"
        "${CMAKE_SOURCE_DIR}/src/utils/gameconfig.cpp"
        "${CMAKE_SOURCE_DIR}/src/utils/schema.cpp"
        "${CMAKE_SOURCE_DIR}/src/utils/utils.cpp"
)
if (WIN32)
    list(APPEND OW_PLUGIN_SOURCES "${CMAKE_SOURCE_DIR}/src/utils/plat_win.cpp")
else ()
    list(APPEND OW_PLUGIN_SOURCES "${CMAKE_SOURCE_DIR}/src/utils/plat_linux.cpp")
endif ()
if (WIN32)
    list(FILTER OW_PLUGIN_SOURCES EXCLUDE REGEX "_linux\\.(c|cpp)$")
else ()
    list(FILTER OW_PLUGIN_SOURCES EXCLUDE REGEX "_(win|windows)\\.(c|cpp)$")
endif ()
if (NOT OW_ENABLE_CACHE_FALLBACK)
    list(FILTER OW_PLUGIN_SOURCES EXCLUDE REGEX "/src/modules/persistence/(cache_manager|player_warmup|redis_cache)\\.cpp$")
endif ()

add_library("${OW_META_NAME}" MODULE
        ${OW_PLUGIN_SOURCES}
        "${OW_SDK_ROOT}/entity2/entityidentity.cpp"
        "${OW_SDK_ROOT}/entity2/entitysystem.cpp"
)
ow_configure_target("${OW_META_NAME}")
if (OW_ENABLE_CACHE_FALLBACK)
    target_compile_definitions("${OW_META_NAME}" PRIVATE OW_ENABLE_CACHE_FALLBACK=1)
else ()
    message(STATUS "OW cache fallback is disabled and excluded from the plugin build.")
endif ()

set_target_properties("${OW_META_NAME}" PROPERTIES
        PREFIX ""
        OUTPUT_NAME "${OW_META_NAME}"
)

target_include_directories("${OW_META_NAME}" PRIVATE
        "${CMAKE_SOURCE_DIR}/src"
        "${CMAKE_SOURCE_DIR}/src/player"
        "${OW_SDK_ROOT}"
        "${OW_SDK_ROOT}/public/entity2"
        "${OW_SDK_ROOT}/game/server"
        "${OW_MMS_ROOT}/third_party/khook/include"
)

if (WIN32)
    target_link_libraries("${OW_META_NAME}" PRIVATE
            ${OW_SDK_WINDOWS_LIBS}
            "ws2_32.lib"
            "psapi.lib"
            "${OW_SDK_ROOT}/lib/public/win64/mathlib.lib"
    )
else ()
    target_link_libraries("${OW_META_NAME}" PRIVATE
            ${OW_SDK_LINUX_POSTLINK_LIBS}
            ${OW_SDK_LINUX_DYNAMIC_LIBS}
            ${CMAKE_DL_LIBS}
            "${OW_SDK_ROOT}/lib/linux64/mathlib.a"
    )
endif ()

include(Protobuf)
ow_generate_protobuf(OW_PROTOBUF_SOURCES OW_PROTOBUF_GEN_DIR)

add_library(ow-protobuf STATIC ${OW_PROTOBUF_SOURCES})
add_dependencies(ow-protobuf ow-protobuf-gen)
ow_configure_target(ow-protobuf)
target_include_directories(ow-protobuf PRIVATE "${OW_PROTOBUF_GEN_DIR}")
target_include_directories("${OW_META_NAME}" PRIVATE "${OW_PROTOBUF_GEN_DIR}")
target_link_libraries("${OW_META_NAME}" PRIVATE ow-protobuf)
target_link_libraries("${OW_META_NAME}" PRIVATE Threads::Threads)
add_dependencies("${OW_META_NAME}" ow-protobuf-gen)

ow_add_package("${OW_META_NAME}")

if (BUILD_TESTING)
    add_executable(particle-menu-tests
            tests/unit/particle_menu_tests.cpp
            src/ui/particle/map_overview_cache.cpp
            src/ui/particle/particle_menu.cpp
            src/ui/particle/particle_menu_sessions.cpp
    )
    target_compile_features(particle-menu-tests PRIVATE cxx_std_20)
    target_include_directories(particle-menu-tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
    add_test(NAME particle-menu-tests COMMAND particle-menu-tests)

    add_executable(ui-tests
            tests/unit/ui_tests.cpp
            src/ui/ui_manager.cpp
            src/ui/hud/hud_system.cpp
            src/ui/input/input_manager.cpp
    )
    target_compile_features(ui-tests PRIVATE cxx_std_20)
    target_include_directories(ui-tests PRIVATE
            "${CMAKE_SOURCE_DIR}/src"
            "${OW_SDK_ROOT}/public"
            "${OW_SDK_ROOT}/public/tier0"
    )
    if (MSVC)
        target_compile_definitions(ui-tests PRIVATE
                COMPILER_MSVC COMPILER_MSVC64 WIN32 WIN64 X64BITS PLATFORM_64BITS
        )
    else ()
        target_compile_definitions(ui-tests PRIVATE
                COMPILER_GCC LINUX _LINUX POSIX X64BITS PLATFORM_64BITS
        )
    endif ()
    add_test(NAME ui-tests COMMAND ui-tests)

    add_executable(core-tests
            tests/unit/core_tests.cpp
            src/adapters/persistence/postgres_adapter.cpp
            src/adapters/persistence/player_settings_cache.cpp
            src/modules/persistence/player_settings_worker.cpp
            src/modules/persistence/player_character_worker.cpp
            src/modules/persistence/player_route_worker.cpp
            src/core/cluster_config.cpp
            src/core/module_manager.cpp
            src/modules/transfer/transfer_manager.cpp
            src/modules/transfer/transfer_module.cpp
            src/modules/inventory/inventory_system.cpp
            src/modules/economy/economy_system.cpp
            src/adapters/cs2/client_redirector.cpp
    )
    if (OW_ENABLE_CACHE_FALLBACK)
        target_sources(core-tests PRIVATE
                src/modules/persistence/cache_manager.cpp
                src/modules/persistence/player_warmup.cpp
                src/modules/persistence/redis_cache.cpp
        )
        target_compile_definitions(core-tests PRIVATE OW_ENABLE_CACHE_FALLBACK=1)
    endif ()
    	target_compile_features(core-tests PRIVATE cxx_std_20)
    	target_compile_definitions(core-tests PRIVATE OW_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    	target_link_libraries(core-tests PRIVATE Threads::Threads)
    if (CMAKE_DL_LIBS)
        target_link_libraries(core-tests PRIVATE ${CMAKE_DL_LIBS})
    endif ()
    target_include_directories(core-tests PRIVATE
            "${CMAKE_SOURCE_DIR}/src"
            "${OW_SDK_ROOT}"
            ${OW_SDK_INCLUDE_DIRS}
            "${OW_SDK_ROOT}/public/engine"
            "${OW_SDK_ROOT}/public/tier1"
            "${OW_SDK_ROOT}/public/entity2"
            "${OW_PROTOBUF_GEN_DIR}"
    )
    if (MSVC)
        target_compile_definitions(core-tests PRIVATE
                COMPILER_MSVC COMPILER_MSVC64 WIN32 WIN64 X64BITS PLATFORM_64BITS
                GAME_DLL META_IS_SOURCE2 SOURCE_ENGINE=${OW_SDK_CODE}
        )
        target_link_libraries(core-tests PRIVATE "ws2_32.lib")
    else ()
        target_compile_definitions(core-tests PRIVATE
                COMPILER_GCC LINUX _LINUX POSIX X64BITS PLATFORM_64BITS
                GAME_DLL META_IS_SOURCE2 SOURCE_ENGINE=${OW_SDK_CODE}
                ${OW_SDK_LINUX_DEFINES}
        )
    endif ()
    add_test(NAME core-tests COMMAND core-tests)
endif ()
