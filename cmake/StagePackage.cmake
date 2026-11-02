cmake_minimum_required(VERSION 3.19)

foreach(_required_var
        OW_PACKAGE_DIR
        OW_SOURCE_DIR
        OW_PLUGIN_NAME
        OW_PLUGIN_ALIAS
        OW_PLATFORM_FOLDER
        OW_PLUGIN_FILE
        OW_PLUGIN_SUFFIX
)
    if (NOT DEFINED ${_required_var} OR "${${_required_var}}" STREQUAL "")
        message(FATAL_ERROR "Package staging requires ${_required_var}.")
    endif ()
endforeach ()

if (NOT EXISTS "${OW_PLUGIN_FILE}")
    message(FATAL_ERROR "Built plugin binary was not found: ${OW_PLUGIN_FILE}")
endif ()

set(_config_source "${OW_SOURCE_DIR}/configs/openworld.ini")
if (NOT EXISTS "${_config_source}")
    set(_config_source "${OW_SOURCE_DIR}/configs/openworld.ini.example")
endif ()
if (NOT EXISTS "${_config_source}")
    message(FATAL_ERROR
            "OpenWorld config and example are both missing."
    )
endif ()
set(_single_map_config_source "${OW_SOURCE_DIR}/configs/openworld.single-map.ini.example")

set(_plugin_cfg_source "${OW_SOURCE_DIR}/cfg/plugin.cfg")
if (NOT EXISTS "${_plugin_cfg_source}")
    message(FATAL_ERROR
            "Required server config is missing: ${_plugin_cfg_source}"
    )
endif ()

set(_install_notes_source "${OW_SOURCE_DIR}/PACKAGE_INSTALL.txt")
if (NOT EXISTS "${_install_notes_source}")
    message(FATAL_ERROR
            "Package installation notes are missing: ${_install_notes_source}"
    )
endif ()

set(_client_bindings_source "${OW_SOURCE_DIR}/client/ow_keyboard_bindings.cfg")
if (NOT EXISTS "${_client_bindings_source}")
    message(FATAL_ERROR
            "Optional client bindings are missing: ${_client_bindings_source}"
    )
endif ()

# Recreate the output so obsolete files from an earlier package layout cannot
# accidentally ship alongside the current build.
file(REMOVE_RECURSE "${OW_PACKAGE_DIR}")

set(_addons_dir "${OW_PACKAGE_DIR}/addons")
set(_plugin_root "${_addons_dir}/${OW_PLUGIN_NAME}")
set(_metamod_dir "${_addons_dir}/metamod")
set(_cfg_dir "${OW_PACKAGE_DIR}/cfg")
set(_bin_dir "${_plugin_root}/bin/${OW_PLATFORM_FOLDER}")
set(_gamedata_dir "${_plugin_root}/gamedata")
set(_translations_dir "${_plugin_root}/translations")
set(_licenses_dir "${_plugin_root}/licenses")
set(_client_cfg_dir "${OW_PACKAGE_DIR}/client/cfg")

file(MAKE_DIRECTORY "${_bin_dir}" "${_metamod_dir}" "${_cfg_dir}" "${_client_cfg_dir}")

function(_stage_file source destination)
    get_filename_component(_destination_dir "${destination}" DIRECTORY)
    file(MAKE_DIRECTORY "${_destination_dir}")
    execute_process(
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${source}" "${destination}"
            RESULT_VARIABLE _copy_result
    )
    if (NOT "${_copy_result}" STREQUAL "0")
        message(FATAL_ERROR
                "Failed to stage ${source} to ${destination}: "
                "${_copy_result}"
        )
    endif ()
endfunction()

_stage_file(
        "${OW_PLUGIN_FILE}"
        "${_bin_dir}/${OW_PLUGIN_NAME}${OW_PLUGIN_SUFFIX}"
)
_stage_file(
        "${_config_source}"
        "${_plugin_root}/configs/openworld.ini"
)
if (EXISTS "${_single_map_config_source}")
    _stage_file(
            "${_single_map_config_source}"
            "${_plugin_root}/configs/openworld.single-map.ini.example"
    )
endif ()
_stage_file(
        "${_plugin_cfg_source}"
        "${_cfg_dir}/${OW_PLUGIN_NAME}.cfg"
)
_stage_file(
        "${_install_notes_source}"
        "${OW_PACKAGE_DIR}/INSTALL.txt"
)
_stage_file(
        "${_client_bindings_source}"
        "${_client_cfg_dir}/ow_keyboard_bindings.cfg"
)

set(_map_assets_dir "${OW_SOURCE_DIR}/assets/maps")
if (IS_DIRECTORY "${_map_assets_dir}")
    file(GLOB _map_overview_images "${_map_assets_dir}/*_overview.png")
    file(GLOB _map_overview_metadata "${_map_assets_dir}/*.overview.ini")
    foreach (_map_asset IN LISTS _map_overview_images _map_overview_metadata)
        get_filename_component(_map_asset_name "${_map_asset}" NAME)
        _stage_file("${_map_asset}" "${_plugin_root}/maps/${_map_asset_name}")
    endforeach ()
endif ()

set(_gamedata_source "${OW_SOURCE_DIR}/gamedata/${OW_PLUGIN_NAME}.games.txt")
if (EXISTS "${_gamedata_source}")
    _stage_file(
            "${_gamedata_source}"
            "${_gamedata_dir}/${OW_PLUGIN_NAME}.games.txt"
    )
endif ()

file(GLOB _translation_files "${OW_SOURCE_DIR}/translations/*.txt")
foreach(_translation_file IN LISTS _translation_files)
    get_filename_component(_translation_name "${_translation_file}" NAME)
    _stage_file(
            "${_translation_file}"
            "${_translations_dir}/${_translation_name}"
    )
endforeach ()

set(_notice_file "${OW_SOURCE_DIR}/THIRD_PARTY_NOTICES.md")
if (EXISTS "${_notice_file}")
    _stage_file("${_notice_file}" "${_plugin_root}/THIRD_PARTY_NOTICES.md")
endif ()

if (EXISTS "${OW_SOURCE_DIR}/LICENSE")
    _stage_file("${OW_SOURCE_DIR}/LICENSE" "${_licenses_dir}/LICENSE")
endif ()
file(GLOB _additional_license_files "${OW_SOURCE_DIR}/licenses/*.txt")
foreach(_license_file IN LISTS _additional_license_files)
    get_filename_component(_license_name "${_license_file}" NAME)
    _stage_file(
            "${_license_file}"
            "${_licenses_dir}/${_license_name}"
    )
endforeach ()

string(CONCAT _vdf_contents
        "\"Metamod Plugin\"\n"
        "{\n"
        "\t\"alias\"\t\"${OW_PLUGIN_ALIAS}\"\n"
        "\t\"file\"\t\"addons/${OW_PLUGIN_NAME}/bin/"
        "${OW_PLATFORM_FOLDER}/${OW_PLUGIN_NAME}\"\n"
        "}\n"
)
file(WRITE "${_metamod_dir}/${OW_PLUGIN_NAME}.vdf" "${_vdf_contents}")
