# vim: set sts=2 ts=2 sw=2 et:

function(ow_add_package target)
    set(_package_dir "${CMAKE_BINARY_DIR}/package")

    if (WIN32)
        set(_platform_folder "win64")
    else ()
        set(_platform_folder "linuxsteamrt64")
    endif ()

    add_custom_target("${target}-package" ALL
            COMMAND "${CMAKE_COMMAND}"
            "-DOW_PACKAGE_DIR=${_package_dir}"
            "-DOW_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
            "-DOW_PLUGIN_NAME=${OW_META_NAME}"
            "-DOW_PLUGIN_ALIAS=${OW_META_ALIAS}"
            "-DOW_PLATFORM_FOLDER=${_platform_folder}"
            "-DOW_PLUGIN_FILE=$<TARGET_FILE:${target}>"
            "-DOW_PLUGIN_SUFFIX=$<TARGET_FILE_SUFFIX:${target}>"
            -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/StagePackage.cmake"
            DEPENDS "${target}"
            COMMENT "${OW_META_NAME}: staging ready-to-copy package"
            VERBATIM
    )

    message(STATUS
            "${OW_META_NAME}: package directory is ${_package_dir}"
    )
endfunction()
