# vim: set sts=2 ts=2 sw=2 et:

function(template_add_package target)
    set(_package_dir "${CMAKE_BINARY_DIR}/package")
    set(_addons_root "${_package_dir}/game/csgo/addons")
    set(_plugin_root "${_addons_root}/${TEMPLATE_META_NAME}")
    set(_metamod_dir "${_addons_root}/metamod")
    set(_cfg_dir "${_package_dir}/game/csgo/cfg")

    if (WIN32)
        set(_platform_folder "win64")
    else ()
        set(_platform_folder "linuxsteamrt64")
    endif ()

    set(_bin_dir "${_plugin_root}/bin/${_platform_folder}")
    set(_gamedata_dir "${_plugin_root}/gamedata")
    set(_licenses_dir "${_plugin_root}/licenses")
    set(_translations_dir "${_plugin_root}/translations")

    add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${_bin_dir}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:${target}>"
            "${_bin_dir}/${TEMPLATE_META_NAME}$<TARGET_FILE_SUFFIX:${target}>"
            COMMENT "${TEMPLATE_META_NAME}: copying plugin binary"
            VERBATIM
    )

    foreach (_asset_pair
            "${CMAKE_SOURCE_DIR}/gamedata/${TEMPLATE_META_NAME}.games.txt|${_gamedata_dir}/${TEMPLATE_META_NAME}.games.txt"
            "${CMAKE_SOURCE_DIR}/cfg/plugin.cfg|${_cfg_dir}/${TEMPLATE_META_NAME}.cfg"
            "${CMAKE_SOURCE_DIR}/configs/openworld.json|${_plugin_root}/configs/openworld.json"
    )
        string(REPLACE "|" ";" _asset_parts "${_asset_pair}")
        list(GET _asset_parts 0 _asset_source)
        list(GET _asset_parts 1 _asset_destination)
        if (EXISTS "${_asset_source}")
            get_filename_component(_asset_destination_dir "${_asset_destination}" DIRECTORY)
            add_custom_command(TARGET ${target} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E make_directory "${_asset_destination_dir}"
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_asset_source}"
                    "${_asset_destination}"
                    VERBATIM
            )
        endif ()
    endforeach ()

    file(GLOB _translation_files CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/translations/*.txt")
    if (_translation_files)
        add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory "${_translations_dir}"
                VERBATIM
        )
        foreach (_src ${_translation_files})
            get_filename_component(_name "${_src}" NAME)
            add_custom_command(TARGET ${target} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_src}" "${_translations_dir}/${_name}"
                    VERBATIM
            )
        endforeach ()
    endif ()

    set(_notice_file "${CMAKE_SOURCE_DIR}/THIRD_PARTY_NOTICES.md")
    if (EXISTS "${_notice_file}")
        add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_notice_file}"
                "${_plugin_root}/THIRD_PARTY_NOTICES.md"
                VERBATIM
        )
    endif ()

    set(_license_files "")
    if (EXISTS "${CMAKE_SOURCE_DIR}/LICENSE")
        list(APPEND _license_files "${CMAKE_SOURCE_DIR}/LICENSE")
    endif ()
    file(GLOB _additional_license_files CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/licenses/*.txt")
    list(APPEND _license_files ${_additional_license_files})
    foreach (_license_file ${_license_files})
        get_filename_component(_license_name "${_license_file}" NAME)
        add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory "${_licenses_dir}"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_license_file}"
                "${_licenses_dir}/${_license_name}"
                VERBATIM
        )
    endforeach ()

    file(MAKE_DIRECTORY "${_metamod_dir}")
    file(WRITE "${_metamod_dir}/${TEMPLATE_META_NAME}.vdf"
            "\"Metamod Plugin\"\n{\n\t\"alias\"\t\"${TEMPLATE_META_ALIAS}\"\n\t\"file\"\t\"addons/${TEMPLATE_META_NAME}/bin/${_platform_folder}/${TEMPLATE_META_NAME}\"\n}\n"
    )

    message(STATUS "${TEMPLATE_META_NAME}: package directory is ${_package_dir}")
endfunction()
