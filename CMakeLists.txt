# vim: set sts=2 ts=2 sw=2 et:

cmake_minimum_required(VERSION 3.19)
project(CS2OpenWorld LANGUAGES CXX C)

include(CTest)

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


set(TEMPLATE_SDK_ROOT "${CMAKE_SOURCE_DIR}/hl2sdk-cs2")
set(TEMPLATE_MMS_ROOT "${CMAKE_SOURCE_DIR}/metamod-source")
set(TEMPLATE_MANIFESTS_ROOT "${TEMPLATE_MMS_ROOT}/hl2sdk-manifests/manifests")

foreach (_pair "TEMPLATE_SDK_ROOT|CS2 SDK" "TEMPLATE_MMS_ROOT|Metamod:Source" "TEMPLATE_MANIFESTS_ROOT|HL2SDK manifests")
    string(REPLACE "|" ";" _pair_list "${_pair}")
    list(GET _pair_list 0 _var)
    list(GET _pair_list 1 _desc)
    if (NOT IS_DIRECTORY "${${_var}}")
        message(FATAL_ERROR "${_desc} is missing at ${${_var}}.")
    endif ()
endforeach ()


if (NOT EXISTS "${CMAKE_SOURCE_DIR}/plugin-metadata.json")
    message(FATAL_ERROR "plugin-metadata.json is missing. Run setup-template.ps1 first.")
endif ()
file(READ "${CMAKE_SOURCE_DIR}/plugin-metadata.json" _template_metadata_json)
foreach (_key name alias display_name description author url log_tag license version)
    template_json_string("${_template_metadata_json}" _value ${_key})
    if (NOT DEFINED _value)
        message(FATAL_ERROR "plugin-metadata.json is missing: ${_key}.")
    endif ()
    string(TOUPPER "${_key}" _key_upper)
    set(TEMPLATE_META_${_key_upper} "${_value}")
    unset(_value)
endforeach ()

if (NOT TEMPLATE_META_NAME MATCHES "^[a-z0-9_-]+$")
    message(FATAL_ERROR "The plugin name may contain only lowercase letters, numbers, underscores, and hyphens.")
endif ()
if (NOT TEMPLATE_META_ALIAS MATCHES "^[A-Za-z0-9_-]+$")
    message(FATAL_ERROR "The plugin alias may contain only letters, numbers, underscores, and hyphens.")
endif ()
if (NOT TEMPLATE_META_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "The plugin version must use major.minor.patch format.")
endif ()

foreach (_field DISPLAY_NAME DESCRIPTION AUTHOR URL LOG_TAG LICENSE)
    set(_value "${TEMPLATE_META_${_field}}")
    string(REPLACE "\\" "\\\\" _value "${_value}")
    string(REPLACE "\"" "\\\"" _value "${_value}")
    string(REPLACE "\r\n" "\\n" _value "${_value}")
    string(REPLACE "\n" "\\n" _value "${_value}")
    string(REPLACE "\r" "\\r" _value "${_value}")
    set(TEMPLATE_META_${_field} "${_value}")
endforeach ()

configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/version_gen.h.in"
        "${CMAKE_BINARY_DIR}/versioning/version_gen.h"
        @ONLY
)


file(READ "${TEMPLATE_MANIFESTS_ROOT}/cs2.json" _template_cs2_manifest_json)

template_json_string("${_template_cs2_manifest_json}" TEMPLATE_SDK_CODE code)
template_json_string_array("${_template_cs2_manifest_json}" _template_sdk_include_paths include_paths)
set(TEMPLATE_SDK_INCLUDE_DIRS "")
foreach (_p ${_template_sdk_include_paths})
    list(APPEND TEMPLATE_SDK_INCLUDE_DIRS "${TEMPLATE_SDK_ROOT}/${_p}")
endforeach ()

template_json_string_array("${_template_cs2_manifest_json}" TEMPLATE_SDK_LINUX_DEFINES linux defines)
template_json_string_array("${_template_cs2_manifest_json}" _template_sdk_linux_postlink_libs linux x86_64 postlink_libs)
template_json_string_array("${_template_cs2_manifest_json}" _template_sdk_linux_dynamic_libs linux x86_64 dynamic_libs)
template_json_string("${_template_cs2_manifest_json}" TEMPLATE_SDK_LINUX_PROTOC_PATH linux protoc_path)

template_json_string_array("${_template_cs2_manifest_json}" _template_sdk_windows_libs windows x86_64 libs)
template_json_string("${_template_cs2_manifest_json}" TEMPLATE_SDK_WINDOWS_PROTOC_PATH windows protoc_path)


set(TEMPLATE_SDK_LINUX_POSTLINK_LIBS "")
foreach (_lib ${_template_sdk_linux_postlink_libs})
    list(APPEND TEMPLATE_SDK_LINUX_POSTLINK_LIBS "${TEMPLATE_SDK_ROOT}/${_lib}")
endforeach ()
set(TEMPLATE_SDK_LINUX_DYNAMIC_LIBS "")
foreach (_lib ${_template_sdk_linux_dynamic_libs})
    list(APPEND TEMPLATE_SDK_LINUX_DYNAMIC_LIBS "${TEMPLATE_SDK_ROOT}/${_lib}")
endforeach ()
set(TEMPLATE_SDK_WINDOWS_LIBS "")
foreach (_lib ${_template_sdk_windows_libs})
    list(APPEND TEMPLATE_SDK_WINDOWS_LIBS "${TEMPLATE_SDK_ROOT}/${_lib}")
endforeach ()

file(GLOB _template_all_manifests "${TEMPLATE_MANIFESTS_ROOT}/*.json")
set(TEMPLATE_SE_DEFINES "")
foreach (_manifest ${_template_all_manifests})
    file(READ "${_manifest}" _m_json)
    template_json_string("${_m_json}" _m_define define)
    template_json_string("${_m_json}" _m_code code)
    list(APPEND TEMPLATE_SE_DEFINES "SE_${_m_define}=${_m_code}")
    unset(_m_define)
    unset(_m_code)
endforeach ()


file(GLOB_RECURSE TEMPLATE_PLUGIN_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/src/core/*.cpp"
        "${CMAKE_SOURCE_DIR}/src/adapters/*.cpp"
        "${CMAKE_SOURCE_DIR}/src/modules/*.cpp"
        "${CMAKE_SOURCE_DIR}/src/services/*.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/*.cpp"
        "${CMAKE_SOURCE_DIR}/src/features/*.cpp"
        "${CMAKE_SOURCE_DIR}/src/features/*.c"
)
if (WIN32)
    list(FILTER TEMPLATE_PLUGIN_SOURCES EXCLUDE REGEX "_linux\\.(c|cpp)$")
else ()
    list(FILTER TEMPLATE_PLUGIN_SOURCES EXCLUDE REGEX "_(win|windows)\\.(c|cpp)$")
endif ()

add_library("${TEMPLATE_META_NAME}" MODULE
        ${TEMPLATE_PLUGIN_SOURCES}
)
template_configure_target("${TEMPLATE_META_NAME}")

set_target_properties("${TEMPLATE_META_NAME}" PROPERTIES
        PREFIX ""
        OUTPUT_NAME "${TEMPLATE_META_NAME}"
)

target_include_directories("${TEMPLATE_META_NAME}" PRIVATE
        "${CMAKE_SOURCE_DIR}/src"
        "${TEMPLATE_SDK_ROOT}"
        "${TEMPLATE_SDK_ROOT}/public/entity2"
        "${TEMPLATE_SDK_ROOT}/game/server"
        "${TEMPLATE_MMS_ROOT}/third_party/khook/include"
)

if (WIN32)
    target_link_libraries("${TEMPLATE_META_NAME}" PRIVATE
            ${TEMPLATE_SDK_WINDOWS_LIBS}
            "psapi.lib"
            "${TEMPLATE_SDK_ROOT}/lib/public/win64/mathlib.lib"
    )
else ()
    target_link_libraries("${TEMPLATE_META_NAME}" PRIVATE
            ${TEMPLATE_SDK_LINUX_POSTLINK_LIBS}
            ${TEMPLATE_SDK_LINUX_DYNAMIC_LIBS}
            "${TEMPLATE_SDK_ROOT}/lib/linux64/mathlib.a"
    )
endif ()

include(Protobuf)
template_generate_protobuf(TEMPLATE_PROTOBUF_SOURCES TEMPLATE_PROTOBUF_GEN_DIR)

add_library(template-protobuf STATIC ${TEMPLATE_PROTOBUF_SOURCES})
add_dependencies(template-protobuf template-protobuf-gen)
template_configure_target(template-protobuf)
target_include_directories(template-protobuf PRIVATE "${TEMPLATE_PROTOBUF_GEN_DIR}")
target_include_directories("${TEMPLATE_META_NAME}" PRIVATE "${TEMPLATE_PROTOBUF_GEN_DIR}")
target_link_libraries("${TEMPLATE_META_NAME}" PRIVATE template-protobuf)
add_dependencies("${TEMPLATE_META_NAME}" template-protobuf-gen)

template_add_package("${TEMPLATE_META_NAME}")

if (BUILD_TESTING)
    add_executable(particle-menu-tests
            tests/unit/particle_menu_tests.cpp
            src/ui/particle/particle_menu.cpp
    )
    target_compile_features(particle-menu-tests PRIVATE cxx_std_20)
    target_include_directories(particle-menu-tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
    add_test(NAME particle-menu-tests COMMAND particle-menu-tests)
endif ()
