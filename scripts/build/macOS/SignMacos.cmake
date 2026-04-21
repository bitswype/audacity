# CMake script to sign a macOS .app bundle for Developer ID distribution.
#
# Arguments:
#   APP_IDENTIFIER             - app identifier (e.g. com.bitswype.audacity-mc)
#   APP_LOCATION               - path to .app bundle (optional)
#   DMG_LOCATION               - path to .dmg (optional)
#   APPLE_CODESIGN_IDENTITY    - identity, e.g. "Developer ID Application: ..."
#   APPLE_CODESIGN_ENTITLEMENTS - path to entitlements plist
#
# We sign inner binaries first, then the bundle, because `codesign --deep`
# is unreliable (Apple has been deprecating --deep for years) and the inner
# binaries get ad-hoc signed earlier in the build via `codesign --sign -`.
# Without --force, our real Developer ID signing would skip those because
# they already have a (worthless) ad-hoc signature.

function( codesign_one path is_dmg )
    message(STATUS "Signing ${path}")

    set( args
        --force
        --verbose
        --timestamp
        --identifier "${APP_IDENTIFIER}"
        --sign "${APPLE_CODESIGN_IDENTITY}"
    )

    if( NOT is_dmg )
        list( APPEND args
            --options runtime
            --entitlements "${APPLE_CODESIGN_ENTITLEMENTS}"
        )
    endif()

    execute_process(
        COMMAND xcrun codesign ${args} ${path}
        RESULT_VARIABLE rc
    )
    if( NOT rc EQUAL 0 )
        message( FATAL_ERROR "codesign failed for ${path} (exit ${rc})" )
    endif()
endfunction()

function( sign_glob root pattern )
    file( GLOB_RECURSE files
          LIST_DIRECTORIES Off
          "${root}/${pattern}"
        )
    foreach( f ${files} )
        codesign_one( "${f}" Off )
    endforeach()
endfunction()

if( DEFINED APP_LOCATION )
    # Sign in dependency order: inner-most first.
    # 1. All .dylibs (Conan deps + our internal libs)
    sign_glob( "${APP_LOCATION}/Contents/Frameworks" "*.dylib" )
    # 2. Modules and plug-ins
    sign_glob( "${APP_LOCATION}/Contents/modules"   "*.so"    )
    sign_glob( "${APP_LOCATION}/Contents/modules"   "*.dylib" )
    sign_glob( "${APP_LOCATION}/Contents/plug-ins"  "*.so"    )
    sign_glob( "${APP_LOCATION}/Contents/plug-ins"  "*.dylib" )
    # 3. Binaries in MacOS/ (Wrapper + Audacity-MC + image-compiler if present)
    file( GLOB exes LIST_DIRECTORIES Off "${APP_LOCATION}/Contents/MacOS/*" )
    foreach( exe ${exes} )
        if( NOT IS_DIRECTORY "${exe}" )
            codesign_one( "${exe}" Off )
        endif()
    endforeach()
    # 4. The bundle itself last
    codesign_one( "${APP_LOCATION}" Off )
endif()

if( DEFINED DMG_LOCATION )
    codesign_one( "${DMG_LOCATION}" On )
endif()
