# Code signing

if( CMAKE_SYSTEM_NAME MATCHES "Windows" )
   install( CODE "set( PFX_SIGN_PS_LOCATION \"${CMAKE_SOURCE_DIR}/scripts/build/windows/PfxSign.ps1\") " )
   install( SCRIPT "scripts/build/windows/PfxSign.cmake" )
elseif( CMAKE_SYSTEM_NAME MATCHES "Darwin")
   set_from_env( APPLE_CODESIGN_IDENTITY )
   # Notarization uses a keychain profile created with notarytool
   # store-credentials. Legacy username/password vars are kept but unused
   # by the new NotarizeMacos.cmake.
   set_from_env( APPLE_NOTARY_PROFILE )

   # Pass arguments to cmake install script
   install( CODE "set( APPLE_CODESIGN_IDENTITY \"${APPLE_CODESIGN_IDENTITY}\" )" )
   install( CODE "set( APPLE_NOTARY_PROFILE \"${APPLE_NOTARY_PROFILE}\" )" )

   # Unique bundle identifier so this fork is treated as a separate app by
   # macOS (Launch Services, Gatekeeper, preferences scoping, etc).
   install( CODE "set( APP_IDENTIFIER \"com.bitswype.audacity-mc\" )" )
   install( CODE "get_filename_component( APP_LOCATION \${CMAKE_INSTALL_PREFIX}/Audacity-MC.app ABSOLUTE )" )
   install( CODE "set( APPLE_CODESIGN_ENTITLEMENTS ${CMAKE_SOURCE_DIR}/mac/Audacity.entitlements )")

   install( SCRIPT "scripts/build/macOS/SignMacos.cmake" )

   if( ${_OPT}perform_notarization )
      install( SCRIPT "scripts/build/macOS/NotarizeMacos.cmake" )
   endif()
endif()
