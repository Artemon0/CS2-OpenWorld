function(template_configure_target target)
    target_compile_features(${target} PRIVATE cxx_std_20)

    target_compile_definitions(${target} PRIVATE
            ${TEMPLATE_SE_DEFINES}
            SOURCE_ENGINE=${TEMPLATE_SDK_CODE}
            GAME_DLL
            RAD_TELEMETRY_DISABLED
            X64BITS
            PLATFORM_64BITS
            META_IS_SOURCE2
    )

    target_include_directories(${target} PRIVATE
            "${CMAKE_BINARY_DIR}/versioning"
            "${CMAKE_SOURCE_DIR}"
            ${TEMPLATE_SDK_INCLUDE_DIRS}
            "${TEMPLATE_SDK_ROOT}/public/engine"
            "${TEMPLATE_SDK_ROOT}/public/tier1"
            "${TEMPLATE_MMS_ROOT}/core"
            "${TEMPLATE_MMS_ROOT}/third_party/khook/include"
    )

    target_sources(${target} PRIVATE
            "${TEMPLATE_SDK_ROOT}/tier1/convar.cpp"
            "${TEMPLATE_SDK_ROOT}/public/tier0/memoverride.cpp"
    )

    if (MSVC)
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" PARENT_SCOPE)
        set(CMAKE_CXX_FLAGS_RELEASE "" PARENT_SCOPE)
        set(CMAKE_C_FLAGS_RELEASE "" PARENT_SCOPE)

        target_compile_definitions(${target} PRIVATE
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

        target_compile_options(${target} PRIVATE
                /MT /W3 /WX
                /wd4005 /wd4018 /wd4099 /wd4146 /wd4244 /wd4267 /wd5033 /wd5048
                /std:c++20 /Oy-
                /experimental:deterministic
                /pathmap:${CMAKE_SOURCE_DIR}=CS2OpenWorld
        )

        target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/TP>)

        target_link_options(${target} PRIVATE
                /WX /IGNORE:4099 /NODEFAULTLIB:MSVCRT legacy_stdio_definitions.lib
        )

        if (CMAKE_BUILD_TYPE STREQUAL "Debug")
            target_compile_definitions(${target} PRIVATE DEBUG _DEBUG)
            target_compile_options(${target} PRIVATE /Od /RTC1 /Zi)
        else ()
            target_link_options(${target} PRIVATE /DEBUG:NONE)
        endif ()

        if (CMAKE_BUILD_TYPE STREQUAL "Release")
            target_compile_definitions(${target} PRIVATE NDEBUG)
            target_compile_options(${target} PRIVATE /Ox)
            target_link_options(${target} PRIVATE /OPT:ICF /OPT:REF)
        endif ()

    else ()
        target_compile_definitions(${target} PRIVATE
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
                ${TEMPLATE_SDK_LINUX_DEFINES}
                _GLIBCXX_USE_CXX11_ABI=0
        )

        target_compile_options(${target} PRIVATE
                -pipe -fno-strict-aliasing -Wall -Werror
                -Wno-sign-compare -Wno-uninitialized -Wno-unused -Wno-switch
                -fPIC -fvisibility=hidden -fno-omit-frame-pointer
        )

        target_compile_options(${target} PRIVATE
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

        target_link_options(${target} PRIVATE
                -lm -fuse-ld=lld -Wl,--no-gnu-unique -static-libstdc++ -lgcc_eh
        )

        if (CMAKE_BUILD_TYPE STREQUAL "Debug")
            target_compile_definitions(${target} PRIVATE DEBUG _DEBUG)
            target_compile_options(${target} PRIVATE -g3)
        endif ()

        if (CMAKE_BUILD_TYPE STREQUAL "Release")
            target_compile_definitions(${target} PRIVATE NDEBUG)
            target_compile_options(${target} PRIVATE -O3)
            target_link_options(${target} PRIVATE -Wl,-s)
        endif ()
    endif ()
endfunction()