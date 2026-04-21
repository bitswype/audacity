# CMake script to notarize macOS build using notarytool
# (xcrun altool was deprecated by Apple in November 2023)
#
# Arguments:
#   APP_LOCATION             - path to .app bundle (optional)
#   DMG_LOCATION             - path to .dmg (optional)
#   APPLE_NOTARY_PROFILE     - keychain profile name created via
#                              `xcrun notarytool store-credentials`
#
# At least one of APP_LOCATION or DMG_LOCATION must be set.

cmake_policy(SET CMP0054 NEW)
cmake_policy(SET CMP0011 NEW)

if(NOT DEFINED APPLE_NOTARY_PROFILE OR APPLE_NOTARY_PROFILE STREQUAL "")
    message(FATAL_ERROR
        "APPLE_NOTARY_PROFILE not set. Create one with:\n"
        "  xcrun notarytool store-credentials <PROFILE_NAME> --apple-id <email> --team-id <TEAMID>")
endif()

function(notarize_path path)
    message(STATUS "Notarizing ${path} (this can take several minutes)")

    execute_process(
        COMMAND xcrun notarytool submit
            "${path}"
            --keychain-profile "${APPLE_NOTARY_PROFILE}"
            --wait
            --output-format plist
        OUTPUT_VARIABLE submit_output
        RESULT_VARIABLE submit_result
    )

    if(NOT submit_result EQUAL 0)
        message(FATAL_ERROR "notarytool submit failed:\n${submit_output}")
    endif()

    # Pull status and id out of the plist
    string(REGEX MATCH "<key>status</key>[ \t\r\n]*<string>([^<]+)</string>" _ "${submit_output}")
    set(status "${CMAKE_MATCH_1}")
    string(REGEX MATCH "<key>id</key>[ \t\r\n]*<string>([^<]+)</string>" _ "${submit_output}")
    set(submission_id "${CMAKE_MATCH_1}")

    message(STATUS "  Submission id: ${submission_id}")
    message(STATUS "  Status:        ${status}")

    if(NOT status STREQUAL "Accepted")
        # Pull the log so the failure reason is right in the build output
        execute_process(
            COMMAND xcrun notarytool log
                "${submission_id}"
                --keychain-profile "${APPLE_NOTARY_PROFILE}"
            OUTPUT_VARIABLE log_output
        )
        message(FATAL_ERROR
            "Notarization failed for ${path}\n"
            "Status: ${status}\n"
            "Log:\n${log_output}")
    endif()

    message(STATUS "  Stapling ticket")
    execute_process(
        COMMAND xcrun stapler staple "${path}"
        RESULT_VARIABLE staple_result
    )
    if(NOT staple_result EQUAL 0)
        message(FATAL_ERROR "stapler staple failed for ${path}")
    endif()
endfunction()

if(DEFINED APP_LOCATION AND NOT APP_LOCATION STREQUAL "")
    # notarytool wants .zip / .dmg / .pkg — zip the .app first
    get_filename_component(parent "${APP_LOCATION}/.." ABSOLUTE)
    set(archive "${parent}/notarization.zip")

    execute_process(
        COMMAND xcrun ditto -c -k --keepParent "${APP_LOCATION}" "${archive}"
        RESULT_VARIABLE ditto_result
    )
    if(NOT ditto_result EQUAL 0)
        message(FATAL_ERROR "ditto failed to create ${archive}")
    endif()

    notarize_path("${archive}")
    # Staple the .app, not the zip
    execute_process(COMMAND xcrun stapler staple "${APP_LOCATION}")

    file(REMOVE "${archive}")
endif()

if(DEFINED DMG_LOCATION AND NOT DMG_LOCATION STREQUAL "")
    notarize_path("${DMG_LOCATION}")
endif()
