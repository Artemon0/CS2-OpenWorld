# vim: set sts=2 ts=2 sw=2 et:

# Creates the `ow_build_flags` INTERFACE target that carries every define and compiler flag used by
# the shipped plugin. Test executables link the same interface, so a unit test can no longer be
# compiled with settings that differ from the released binary -- that divergence is exactly what hid
# the -fno-exceptions build break of the persistence workers.
function(ow_define_build_flags)
    if (TARGET ow_build_flags)
        return()
    endif ()

    add_library(ow_build_flags INTERFACE)
    target_compile_features(ow_build_flags INTERFACE cxx_std_20)

    target_compile_definitions(ow_build_flags INTERFACE
            ${OW_SE_DEFINES}
            SOURCE_ENGINE=${OW_SDK_CODE}
            GAME_DLL
            RAD_TELEMETRY_DISABLED
            X64BITS
            PLATFORM_64BITS
            META_IS_SOURCE2
    )

    if (MSVC)
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" PARENT_SCOPE)
        set(CMAKE_CXX_FLAGS_RELEASE "" PARENT_SCOPE)
        set(CMAKE_C_FLAGS_RELEASE "" PARENT_SCOPE)

        target_compile_definitions(ow_build_flags INTERFACE
                _CRT_SECURE_NO_DEPRECATE
                _CRT_SECURE_NO_WARNINGS
                _CRT_NONSTDC_NO_DEPRECATE
                _HAS_EXCEPTIONS=0
                _ITERATOR_DEBUG_LEVEL=0
                WIN32
                _WINDOWS
                COMPILER_MSVC
                COMPILER_MSVC64
                WIN64
        )

        target_compile_options(ow_build_flags INTERFACE
                /MT /W3 /WX
                /wd4005 /wd4018 /wd4099 /wd4146 /wd4244 /wd4267 /wd5033 /wd5048
                /std:c++20 /Oy-
                /experimental:deterministic
                /pathmap:${CMAKE_SOURCE_DIR}=CS2OpenWorld
        )

        target_compile_options(ow_build_flags INTERFACE $<$<COMPILE_LANGUAGE:CXX>:/TP>)

        target_link_options(ow_build_flags INTERFACE
                /WX /IGNORE:4099 /NODEFAULTLIB:MSVCRT legacy_stdio_definitions.lib
        )

        if (CMAKE_BUILD_TYPE STREQUAL "Debug")
            target_compile_definitions(ow_build_flags INTERFACE DEBUG _DEBUG)
            target_compile_options(ow_build_flags INTERFACE /Od /RTC1 /Zi)
        else ()
            target_link_options(ow_build_flags INTERFACE /DEBUG:NONE)
        endif ()

        if (CMAKE_BUILD_TYPE STREQUAL "Release")
            target_compile_definitions(ow_build_flags INTERFACE NDEBUG)
            target_compile_options(ow_build_flags INTERFACE /Ox)
            target_link_options(ow_build_flags INTERFACE /OPT:ICF /OPT:REF)
        endif ()
    else ()
        target_compile_definitions(ow_build_flags INTERFACE
                stricmp=strcasecmp
                _stricmp=strcasecmp
                _snprintf=snprintf
                _vsnprintf=vsnprintf
                HAVE_STDINT_H
                GNUC
                LINUX
                _LINUX
                POSIX
                _FILE_OFFSET_BITS=64
                COMPILER_GCC
                ${OW_SDK_LINUX_DEFINES}
                _GLIBCXX_USE_CXX11_ABI=0
        )

        target_compile_options(ow_build_flags INTERFACE
                -pipe -fno-strict-aliasing -Wall -Werror
                -Wno-sign-compare -Wno-uninitialized -Wno-unused -Wno-switch
                -fPIC -fvisibility=hidden -fno-omit-frame-pointer
        )

        target_compile_options(ow_build_flags INTERFACE
                $<$<COMPILE_LANGUAGE:CXX>:-std=c++20>
                $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
                $<$<COMPILE_LANGUAGE:CXX>:-frtti>
                $<$<COMPILE_LANGUAGE:CXX>:-fno-threadsafe-statics>
                $<$<COMPILE_LANGUAGE:CXX>:-fvisibility-inlines-hidden>
                $<$<COMPILE_LANGUAGE:CXX>:-Wno-delete-non-virtual-dtor>
                $<$<COMPILE_LANGUAGE:CXX>:-Wno-non-virtual-dtor>
                $<$<COMPILE_LANGUAGE:CXX>:-Wno-overloaded-virtual>
                $<$<COMPILE_LANGUAGE:CXX>:-Wno-register>
                $<$<COMPILE_LANGUAGE:CXX>:-Wno-invalid-offsetof>
                $<$<COMPILE_LANGUAGE:CXX>:-Wno-parentheses>
        )

        target_link_options(ow_build_flags INTERFACE
                -lm -fuse-ld=lld -Wl,--no-gnu-unique -static-libstdc++ -lgcc_eh
        )

        if (CMAKE_BUILD_TYPE STREQUAL "Debug")
            target_compile_definitions(ow_build_flags INTERFACE DEBUG _DEBUG)
            target_compile_options(ow_build_flags INTERFACE -g3)
        endif ()

        if (CMAKE_BUILD_TYPE STREQUAL "Release")
            target_compile_definitions(ow_build_flags INTERFACE NDEBUG)
            target_compile_options(ow_build_flags INTERFACE -O3)
            target_link_options(ow_build_flags INTERFACE -Wl,-s)
        endif ()
    endif ()
endfunction()

function(ow_configure_target target)
    ow_define_build_flags()

    target_link_libraries(${target} PRIVATE ow_build_flags)

    target_include_directories(${target} PRIVATE
            "${CMAKE_BINARY_DIR}/versioning"
            "${CMAKE_SOURCE_DIR}"
            ${OW_SDK_INCLUDE_DIRS}
            "${OW_SDK_ROOT}/public/engine"
            "${OW_SDK_ROOT}/public/tier1"
            "${OW_MMS_ROOT}/core"
            "${OW_MMS_ROOT}/third_party/khook/include"
    )

    target_sources(${target} PRIVATE
            "${OW_SDK_ROOT}/tier1/convar.cpp"
            "${OW_SDK_ROOT}/public/tier0/memoverride.cpp"
    )
endfunction()
