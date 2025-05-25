# Initial comment, e.g., build command for macOS:
# sudo cmake --build /Users/kirilltokarev/Programming/CLion/Me/TypingApp/cmake-build-debug --target install

cmake_minimum_required(VERSION 3.20)
project(TypingApp C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(PROJECT_NAME_STR "${PROJECT_NAME}") # This will set PROJECT_NAME_STR to "TypingApp"
set(COMPANY_NAME_STR "com.typingapp")   # Consistent with main.c and Info.plist

# --- Platform-Specific SDL2/SDL2_ttf Configuration ---
if(APPLE)
    # macOS specific: Use Homebrew's PkgConfig by default
    set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum macOS version to run on")
    set(HOMEBREW_PREFIX "/opt/homebrew")
    set(ENV{PKG_CONFIG_PATH} "${HOMEBREW_PREFIX}/lib/pkgconfig")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED sdl2)
    pkg_check_modules(SDL2_TTF REQUIRED sdl2_ttf)
elseif(WIN32)
    # Windows specific: Use CMake's find_package for SDL2.
    # User might need to set SDL2_DIR or add SDL2 dev libraries to CMAKE_PREFIX_PATH.
    set(SDL2_PATH_HINT "" CACHE PATH "Path to SDL2 development library root (e.g., C:/SDL2-2.x.x/x86_64-w64-mingw32)")
    if(SDL2_PATH_HINT)
        list(APPEND CMAKE_PREFIX_PATH ${SDL2_PATH_HINT})
    endif()
    find_package(SDL2 REQUIRED)
    find_package(SDL2_ttf REQUIRED)
else()
    # Linux and other PkgConfig-based systems
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED sdl2)
    pkg_check_modules(SDL2_TTF REQUIRED sdl2_ttf)
endif()

# --- Executable Definition ---
add_executable(${PROJECT_NAME} main.c)

# --- Include Directories ---
target_include_directories(${PROJECT_NAME} PRIVATE
        ${SDL2_INCLUDE_DIRS}
        ${SDL2_TTF_INCLUDE_DIRS}
)

# --- Linking ---
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE
            ${SDL2_LIBRARIES}
            ${SDL2_TTF_LIBRARIES}
    )
    set(WINDOWS_RESOURCE_FILE "${CMAKE_CURRENT_SOURCE_DIR}/resource.rc")
    if(EXISTS "${WINDOWS_RESOURCE_FILE}")
        target_sources(${PROJECT_NAME} PRIVATE "${WINDOWS_RESOURCE_FILE}")
    else()
        message(STATUS "Windows resource file (resource.rc) not found. Executable will not have a custom icon.")
    endif()
else() # macOS, Linux, and other PkgConfig-based systems
    target_link_directories(${PROJECT_NAME} PRIVATE
            ${SDL2_LIBRARY_DIRS}
            ${SDL2_TTF_LIBRARY_DIRS}
    )
    target_compile_options(${PROJECT_NAME} PRIVATE
            ${SDL2_CFLAGS_OTHER}
            ${SDL2_TTF_CFLAGS_OTHER}
    )
    target_link_libraries(${PROJECT_NAME} PRIVATE ${SDL2_LIBRARIES} ${SDL2_TTF_LIBRARIES})
endif()

# --- Asset/Resource Handling (text.txt) ---
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/text.txt")
    set(TEXT_FILE_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/text.txt")
    if(APPLE)
        # macOS bundle resource handling (part of the larger macOS section below)
    elseif(WIN32)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${TEXT_FILE_SOURCE}"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/text.txt"
                COMMENT "Copying text.txt to build directory for Windows"
        )
        install(FILES "${TEXT_FILE_SOURCE}" DESTINATION "." COMPONENT Runtime)
    else() # Linux, etc.
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${TEXT_FILE_SOURCE}"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/text.txt"
                COMMENT "Copying text.txt to build directory"
        )
    endif()
endif()


# ==========================================================================================
# --- macOS Specific Bundling and Packaging ---
# ==========================================================================================
if(APPLE)
    set(SDL2_LIBRARY_FILES_TO_BUNDLE "")
    if(SDL2_FOUND AND SDL2_LIBRARIES AND SDL2_LIBRARY_DIRS)
        foreach(LIB_NAME_STEM ${SDL2_LIBRARIES})
            if ("${LIB_NAME_STEM}" MATCHES "^-l(.+)")
                set(CLEAN_LIB_NAME_STEM "${CMAKE_MATCH_1}")
            else()
                set(CLEAN_LIB_NAME_STEM "${LIB_NAME_STEM}")
            endif()
            find_library(SDL2_SINGLE_LIB_FILE NAMES ${CLEAN_LIB_NAME_STEM} PATHS ${SDL2_LIBRARY_DIRS} NO_DEFAULT_PATH)
            if(SDL2_SINGLE_LIB_FILE)
                list(APPEND SDL2_LIBRARY_FILES_TO_BUNDLE "${SDL2_SINGLE_LIB_FILE}")
                unset(SDL2_SINGLE_LIB_FILE CACHE)
            else()
                message(WARNING "Could not find library file for stem '${CLEAN_LIB_NAME_STEM}' in '${SDL2_LIBRARY_DIRS}' for bundling.")
            endif()
        endforeach()
    endif()
    list(REMOVE_DUPLICATES SDL2_LIBRARY_FILES_TO_BUNDLE)

    set(SDL2_TTF_LIBRARY_FILES_TO_BUNDLE "")
    if(SDL2_TTF_FOUND AND SDL2_TTF_LIBRARIES AND SDL2_TTF_LIBRARY_DIRS)
        foreach(LIB_NAME_STEM ${SDL2_TTF_LIBRARIES})
            if ("${LIB_NAME_STEM}" MATCHES "^-l(.+)")
                set(CLEAN_LIB_NAME_STEM "${CMAKE_MATCH_1}")
            else()
                set(CLEAN_LIB_NAME_STEM "${LIB_NAME_STEM}")
            endif()
            find_library(SDL2_TTF_SINGLE_LIB_FILE NAMES ${CLEAN_LIB_NAME_STEM} PATHS ${SDL2_TTF_LIBRARY_DIRS} NO_DEFAULT_PATH)
            if(SDL2_TTF_SINGLE_LIB_FILE)
                list(APPEND SDL2_TTF_LIBRARY_FILES_TO_BUNDLE "${SDL2_TTF_SINGLE_LIB_FILE}")
                unset(SDL2_TTF_SINGLE_LIB_FILE CACHE)
            else()
                message(WARNING "Could not find library file for stem '${CLEAN_LIB_NAME_STEM}' in '${SDL2_TTF_LIBRARY_DIRS}' for bundling.")
            endif()
        endforeach()
    endif()
    list(REMOVE_DUPLICATES SDL2_TTF_LIBRARY_FILES_TO_BUNDLE)

    set(MACOSX_BUNDLE_ICON_FILE "${CMAKE_CURRENT_SOURCE_DIR}/appicon.icns")
    set(MACOSX_BUNDLE_INFO_STRING "${PROJECT_NAME_STR} Typing Application")
    set(MACOSX_BUNDLE_GUI_IDENTIFIER "${COMPANY_NAME_STR}.${PROJECT_NAME_STR}")

    set(INFO_PLIST_PATH "${CMAKE_CURRENT_BINARY_DIR}/Info.plist")
    # Use the Info.plist you provided
    file(WRITE "${INFO_PLIST_PATH}"
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
            "<plist version=\"1.0\">\n"
            "<dict>\n"
            "  <key>CFBundleName</key>\n"
            "  <string>${PROJECT_NAME_STR}</string>\n" # Use CMake variable for consistency
            "  <key>CFBundleExecutable</key>\n"
            "  <string>${PROJECT_NAME}</string>\n" # Target name
            "  <key>CFBundleIdentifier</key>\n"
            "  <string>${MACOSX_BUNDLE_GUI_IDENTIFIER}</string>\n" # Use CMake variable
            "  <key>CFBundlePackageType</key>\n"
            "  <string>APPL</string>\n"
            "  <key>NSPrincipalClass</key>\n"
            "  <string>NSApplication</string>\n"
            "  <key>NSHighResolutionCapable</key>\n"
            "  <true/>\n"
            "  <key>CFBundleIconFile</key>\n"
            "  <string>appicon.icns</string>\n" # Ensure this is copied to Resources
            "  <key>CFBundleInfoDictionaryVersion</key>\n"
            "  <string>6.0</string>\n"
            "  <key>CFBundleShortVersionString</key>\n"
            "  <string>1.0</string>\n"
            "  <key>CFBundleVersion</key>\n"
            "  <string>1</string>\n"
            "  <key>LSMinimumSystemVersion</key>\n"
            "  <string>${CMAKE_OSX_DEPLOYMENT_TARGET}</string>\n"
            "  <key>CFBundleDevelopmentRegion</key>\n"
            "  <string>en</string>\n" # Or English
            "</dict>\n"
            "</plist>\n"
    )

    set_target_properties(${PROJECT_NAME} PROPERTIES
            MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_INFO_PLIST "${INFO_PLIST_PATH}"
    )

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/text.txt")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/text.txt"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/../Resources/text.txt"
                COMMENT "Copying text.txt to Bundle Resources"
        )
    endif()
    if(EXISTS "${MACOSX_BUNDLE_ICON_FILE}")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MACOSX_BUNDLE_ICON_FILE}"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/../Resources/appicon.icns"
                COMMENT "Copying appicon.icns to Bundle Resources"
        )
    endif()

    set(FRAMEWORKS_DIR "$<TARGET_FILE_DIR:${PROJECT_NAME}>/../Frameworks")

    set(OTHER_DEPENDENCIES_TO_BUNDLE
            "/opt/homebrew/opt/freetype/lib/libfreetype.6.dylib"
            "/opt/homebrew/opt/harfbuzz/lib/libharfbuzz.0.dylib"
            "/opt/homebrew/opt/glib/lib/libglib-2.0.0.dylib"
            "/opt/homebrew/opt/graphite2/lib/libgraphite2.3.dylib"
            "/opt/homebrew/opt/libpng/lib/libpng16.16.dylib"
            "/opt/homebrew/opt/gettext/lib/libintl.8.dylib"
            "/opt/homebrew/opt/pcre2/lib/libpcre2-8.0.dylib"
    )
    set(LIBS_TO_BUNDLE
            ${SDL2_LIBRARY_FILES_TO_BUNDLE}
            ${SDL2_TTF_LIBRARY_FILES_TO_BUNDLE}
            ${OTHER_DEPENDENCIES_TO_BUNDLE}
    )
    list(REMOVE_DUPLICATES LIBS_TO_BUNDLE)
    list(FILTER LIBS_TO_BUNDLE EXCLUDE REGEX "^$")

    set(ALL_FINAL_BUNDLED_NAMES_LIST "")
    foreach(temp_lib_path_for_list ${LIBS_TO_BUNDLE})
        get_filename_component(temp_orig_name_for_list ${temp_lib_path_for_list} NAME)
        set(temp_final_name_for_list ${temp_orig_name_for_list})
        if(temp_lib_path_for_list MATCHES ".*/libSDL2-[0-9.]+\\.dylib$" OR temp_lib_path_for_list MATCHES ".*/libSDL2\\.dylib$")
            if (NOT temp_lib_path_for_list MATCHES ".*SDL2_image.*" AND NOT temp_lib_path_for_list MATCHES ".*SDL2_mixer.*" AND NOT temp_lib_path_for_list MATCHES ".*SDL2_net.*" AND NOT temp_lib_path_for_list MATCHES ".*SDL2_ttf.*")
                set(temp_final_name_for_list "libSDL2-2.0.0.dylib")
            endif()
        endif()
        if(temp_lib_path_for_list MATCHES ".*/libSDL2_ttf-[0-9.]+\\.dylib$" OR temp_lib_path_for_list MATCHES ".*/libSDL2_ttf\\.dylib$")
            set(temp_final_name_for_list "libSDL2_ttf-2.0.0.dylib")
        endif()
        list(APPEND ALL_FINAL_BUNDLED_NAMES_LIST "${temp_final_name_for_list}")
    endforeach()
    list(REMOVE_DUPLICATES ALL_FINAL_BUNDLED_NAMES_LIST)

    foreach(LIB_PATH ${LIBS_TO_BUNDLE})
        if(EXISTS "${LIB_PATH}")
            get_filename_component(ORIGINAL_LIB_FILENAME ${LIB_PATH} NAME)
            set(FINAL_BUNDLED_LIB_NAME ${ORIGINAL_LIB_FILENAME})
            if(LIB_PATH MATCHES ".*/libSDL2-[0-9.]+\\.dylib$" OR LIB_PATH MATCHES ".*/libSDL2\\.dylib$")
                if (NOT LIB_PATH MATCHES ".*SDL2_image.*" AND NOT LIB_PATH MATCHES ".*SDL2_mixer.*" AND NOT LIB_PATH MATCHES ".*SDL2_net.*" AND NOT LIB_PATH MATCHES ".*SDL2_ttf.*")
                    set(FINAL_BUNDLED_LIB_NAME "libSDL2-2.0.0.dylib")
                endif()
            endif()
            if(LIB_PATH MATCHES ".*/libSDL2_ttf-[0-9.]+\\.dylib$" OR LIB_PATH MATCHES ".*/libSDL2_ttf\\.dylib$")
                set(FINAL_BUNDLED_LIB_NAME "libSDL2_ttf-2.0.0.dylib")
            endif()
            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LIB_PATH}" "${FRAMEWORKS_DIR}/${FINAL_BUNDLED_LIB_NAME}"
                    COMMENT "Copying ${FINAL_BUNDLED_LIB_NAME} to Bundle Frameworks"
            )
            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                    COMMAND /usr/bin/install_name_tool -id "@rpath/${FINAL_BUNDLED_LIB_NAME}" "${FRAMEWORKS_DIR}/${FINAL_BUNDLED_LIB_NAME}"
                    COMMENT "Fixing id for ${FINAL_BUNDLED_LIB_NAME}" VERBATIM
            )
            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                    COMMAND /usr/bin/install_name_tool -change "${LIB_PATH}" "@rpath/${FINAL_BUNDLED_LIB_NAME}" "$<TARGET_FILE:${PROJECT_NAME}>"
                    COMMENT "Fixing library path for ${FINAL_BUNDLED_LIB_NAME} in executable (via loop)" VERBATIM
            )
            find_program(OTOOL_EXECUTABLE otool)
            find_program(BASH_EXECUTABLE bash)
            if(OTOOL_EXECUTABLE AND BASH_EXECUTABLE)
                set(FIX_DEPS_SCRIPT_TEMPLATE "${CMAKE_CURRENT_SOURCE_DIR}/fix_inner_deps.sh.in") #
                if(EXISTS "${FIX_DEPS_SCRIPT_TEMPLATE}")
                    set(CONFIGURED_FIX_DEPS_SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/fix_inner_deps_for_${FINAL_BUNDLED_LIB_NAME}.sh")
                    configure_file("${FIX_DEPS_SCRIPT_TEMPLATE}" "${CONFIGURED_FIX_DEPS_SCRIPT}" @ONLY)
                    find_program(CHMOD_EXECUTABLE chmod)
                    if(CHMOD_EXECUTABLE)
                        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                                COMMAND "${CHMOD_EXECUTABLE}" +x "${CONFIGURED_FIX_DEPS_SCRIPT}"
                                COMMENT "Make ${CONFIGURED_FIX_DEPS_SCRIPT} executable" VERBATIM
                        )
                    else()
                        message(WARNING "chmod executable not found. Cannot make ${CONFIGURED_FIX_DEPS_SCRIPT} executable.")
                    endif()
                    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                            COMMAND "${BASH_EXECUTABLE}" "${CONFIGURED_FIX_DEPS_SCRIPT}"
                            "${FRAMEWORKS_DIR}" "${FINAL_BUNDLED_LIB_NAME}" "${OTOOL_EXECUTABLE}" "${HOMEBREW_PREFIX}" ${ALL_FINAL_BUNDLED_NAMES_LIST}
                            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
                            COMMENT "Fixing internal dependencies for ${FINAL_BUNDLED_LIB_NAME} using script" VERBATIM
                    )
                else()
                    message(STATUS "fix_inner_deps.sh.in not found at ${FIX_DEPS_SCRIPT_TEMPLATE}. Skipping internal dependency fixing for ${FINAL_BUNDLED_LIB_NAME}.")
                endif()
            else()
                message(WARNING "otool or bash not found. Cannot fix internal library dependencies automatically for ${FINAL_BUNDLED_LIB_NAME}. Bundle may not be portable.")
            endif()
        else()
            message(WARNING "Library to bundle not found (during foreach loop): ${LIB_PATH}")
        endif()
    endforeach()

    set_target_properties(${PROJECT_NAME} PROPERTIES
            BUILD_WITH_INSTALL_RPATH TRUE
            INSTALL_RPATH "@executable_path/../Frameworks"
    )
    set(SDL2_ACTUAL_LINK_PATH "/opt/homebrew/opt/sdl2/lib/libSDL2-2.0.0.dylib")
    set(SDL2_BUNDLED_NAME "libSDL2-2.0.0.dylib")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND /usr/bin/install_name_tool -change "${SDL2_ACTUAL_LINK_PATH}" "@rpath/${SDL2_BUNDLED_NAME}" "$<TARGET_FILE:${PROJECT_NAME}>"
            COMMENT "Explicitly fixing executable link for SDL2" VERBATIM
    )
    set(SDL2_TTF_ACTUAL_LINK_PATH "/opt/homebrew/opt/sdl2_ttf/lib/libSDL2_ttf-2.0.0.dylib")
    set(SDL2_TTF_BUNDLED_NAME "libSDL2_ttf-2.0.0.dylib")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND /usr/bin/install_name_tool -change "${SDL2_TTF_ACTUAL_LINK_PATH}" "@rpath/${SDL2_TTF_BUNDLED_NAME}" "$<TARGET_FILE:${PROJECT_NAME}>"
            COMMENT "Explicitly fixing executable link for SDL2_ttf" VERBATIM
    )

    set(DMG_VOLUME_NAME "${PROJECT_NAME_STR}")
    set(FINAL_DMG_FILENAME "${CMAKE_BINARY_DIR}/${PROJECT_NAME_STR}.dmg")
    set(TEMP_DMG_FILENAME "${CMAKE_BINARY_DIR}/temp_${PROJECT_NAME_STR}.dmg")
    set(DMG_BACKGROUND_IMAGE_NAME "dmg_background.png")
    set(DMG_BACKGROUND_IMAGE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/${DMG_BACKGROUND_IMAGE_NAME}")
    set(MOUNT_POINT "/tmp/dmg_mount_point_for_${PROJECT_NAME_STR}")
    set(APP_BUNDLE_SOURCE_PATH "$<TARGET_BUNDLE_DIR:${PROJECT_NAME}>")
    set(APP_BUNDLE_DESTINATION_NAME "${PROJECT_NAME_STR}.app")
    set(DMG_COMMANDS "")
    list(APPEND DMG_COMMANDS COMMAND ${CMAKE_COMMAND} -E remove -f "${FINAL_DMG_FILENAME}")
    list(APPEND DMG_COMMANDS COMMAND ${CMAKE_COMMAND} -E remove -f "${TEMP_DMG_FILENAME}")
    list(APPEND DMG_COMMANDS
            COMMAND hdiutil create -size 120m -fs HFS+ -volname "${DMG_VOLUME_NAME}" "${TEMP_DMG_FILENAME}"
            COMMENT "Creating temporary DMG (120MB, HFS+, Vol: ${DMG_VOLUME_NAME})"
    )
    list(APPEND DMG_COMMANDS
            COMMAND hdiutil detach "${MOUNT_POINT}" -force -quiet || echo "Detach of ${MOUNT_POINT} failed or not mounted (expected on clean run)."
            COMMENT "Attempting to detach any pre-existing mount at ${MOUNT_POINT}"
    )
    list(APPEND DMG_COMMANDS COMMAND ${CMAKE_COMMAND} -E rm -rf "${MOUNT_POINT}")
    list(APPEND DMG_COMMANDS COMMAND ${CMAKE_COMMAND} -E make_directory "${MOUNT_POINT}")
    list(APPEND DMG_COMMANDS
            COMMAND hdiutil attach "${TEMP_DMG_FILENAME}" -mountpoint "${MOUNT_POINT}" -nobrowse -quiet
            COMMENT "Mounting temporary DMG to ${MOUNT_POINT}"
    )
    list(APPEND DMG_COMMANDS COMMAND sleep 2 COMMENT "Pausing for 2 seconds for volume to register before copying")
    list(APPEND DMG_COMMANDS
            COMMAND ditto --noqtn "${APP_BUNDLE_SOURCE_PATH}" "${MOUNT_POINT}/${APP_BUNDLE_DESTINATION_NAME}"
            COMMENT "Copying ${APP_BUNDLE_DESTINATION_NAME} to DMG volume"
    )
    list(APPEND DMG_COMMANDS
            COMMAND ln -sf /Applications "${MOUNT_POINT}/Applications"
            COMMENT "Creating Applications symlink in DMG"
    )
    list(APPEND DMG_COMMANDS
            COMMAND ls -la "${MOUNT_POINT}"
            COMMENT "Listing contents of DMG root after copying app and symlink"
    )
    set(DMG_LAYOUT_COMMANDS "")
    if(EXISTS "${DMG_BACKGROUND_IMAGE_PATH}")
        list(APPEND DMG_LAYOUT_COMMANDS COMMAND ${CMAKE_COMMAND} -E make_directory "${MOUNT_POINT}/.background")
        list(APPEND DMG_LAYOUT_COMMANDS COMMAND ${CMAKE_COMMAND} -E copy "${DMG_BACKGROUND_IMAGE_PATH}" "${MOUNT_POINT}/.background/${DMG_BACKGROUND_IMAGE_NAME}")
        list(APPEND DMG_LAYOUT_COMMANDS COMMAND osascript
                -e "tell application \"Finder\""
                -e "  delay 3"
                -e "  try"
                -e "    set mountedVolumePath to POSIX file \"${MOUNT_POINT}\""
                -e "    set mountedVolumeAlias to mountedVolumePath as alias"
                -e "    open mountedVolumeAlias"
                -e "    tell container window of mountedVolumeAlias"
                -e "      set current view to icon view"
                -e "      set toolbar visible to false"
                -e "      set statusbar visible to false"
                -e "      set the bounds to {100, 100, 780, 540}"
                -e "      set viewOptions to the icon view options"
                -e "      set arrangement of viewOptions to not arranged"
                -e "      set icon size of viewOptions to 100"
                -e "      set background picture of viewOptions to file \".background:${DMG_BACKGROUND_IMAGE_NAME}\" of mountedVolumeAlias"
                -e "      set position of item \"${APP_BUNDLE_DESTINATION_NAME}\" to {180, 190}"
                -e "      set position of item \"Applications\" to {550, 190}"
                -e "    end tell"
                -e "    update mountedVolumeAlias without registering applications"
                -e "    delay 1"
                -e "    close mountedVolumeAlias"
                -e "  on error errMsg number errorNumber"
                -e "    log \"AppleScript Full Layout Error: \" & errMsg & \" (Number: \" & errorNumber & \")\""
                -e "    return \"AppleScript Full Layout Error: \" & errMsg & \" (Number: \" & errorNumber & \")\""
                -e "  end try"
                -e "end tell"
                COMMENT "Setting DMG window layout and icon positions"
        )
        list(APPEND DMG_LAYOUT_COMMANDS COMMAND SetFile -a V "${MOUNT_POINT}/.background" || echo ".background folder not hidden (SetFile may have failed or not found)")
    else()
        message(STATUS "DMG background image not found at ${DMG_BACKGROUND_IMAGE_PATH}. Creating DMG without custom background/layout.")
        list(APPEND DMG_LAYOUT_COMMANDS COMMAND osascript
                -e "tell application \"Finder\""
                -e "  delay 2"
                -e "  try"
                -e "    set mountedVolumePath to POSIX file \"${MOUNT_POINT}\""
                -e "    set mountedVolumeAlias to mountedVolumePath as alias"
                -e "    open mountedVolumeAlias"
                -e "    tell container window of mountedVolumeAlias"
                -e "      set the bounds to {100, 100, 780, 540}"
                -e "    end tell"
                -e "    close mountedVolumeAlias"
                -e "  on error errMsg number errorNumber"
                -e "    log \"AppleScript Simple Layout Error: \" & errMsg & \" (Number: \" & errorNumber & \")\""
                -e "    return \"AppleScript Simple Layout Error: \" & errMsg & \" (Number: \" & errorNumber & \")\""
                -e "  end try"
                -e "end tell"
                COMMENT "Basic open/close of DMG window"
        )
    endif()
    list(APPEND DMG_COMMANDS ${DMG_LAYOUT_COMMANDS})
    list(APPEND DMG_COMMANDS COMMAND sync COMMENT "Syncing filesystem")
    list(APPEND DMG_COMMANDS COMMAND sync COMMENT "Syncing filesystem again")
    list(APPEND DMG_COMMANDS
            COMMAND hdiutil detach "${MOUNT_POINT}" -force -quiet
            COMMENT "Unmounting temporary DMG"
    )
    list(APPEND DMG_COMMANDS
            COMMAND hdiutil convert "${TEMP_DMG_FILENAME}" -format UDZO -imagekey zlib-level=9 -o "${FINAL_DMG_FILENAME}"
            COMMENT "Converting to final compressed DMG"
    )
    list(APPEND DMG_COMMANDS
            COMMAND ${CMAKE_COMMAND} -E remove -f "${TEMP_DMG_FILENAME}"
            COMMENT "Removing temporary DMG"
    )
    add_custom_target(CreateDMGPackage ALL
            ${DMG_COMMANDS}
            DEPENDS ${PROJECT_NAME}
            VERBATIM
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
            COMMENT "Creating DMG package ${FINAL_DMG_FILENAME}"
    )
    install(TARGETS ${PROJECT_NAME}
            BUNDLE DESTINATION "/Applications"
            COMPONENT Runtime
    )
endif() # APPLE


# ==========================================================================================
# --- Windows Specific Installation and Packaging ---
# ==========================================================================================
if(WIN32)
    install(TARGETS ${PROJECT_NAME}
            RUNTIME DESTINATION bin
            COMPONENT Runtime
    )
    set(SDL_DLL_FILES
            "SDL2.dll"
            "SDL2_ttf.dll"
            "libfreetype-6.dll"
            "libpng16-16.dll"
            "zlib1.dll"
            "libbrotlidec.dll"
            "libbrotlicommon.dll"
    )
    set(DLL_SEARCH_PATHS "")
    if(SDL2_PATH_HINT AND IS_DIRECTORY "${SDL2_PATH_HINT}/bin")
        list(APPEND DLL_SEARCH_PATHS "${SDL2_PATH_HINT}/bin")
    endif()
    if(SDL2_LIBRARIES AND NOT TARGET ${SDL2_LIBRARIES})
        get_filename_component(SDL2_LIB_DIR "${SDL2_LIBRARIES}" DIRECTORY)
        if(IS_DIRECTORY "${SDL2_LIB_DIR}")
            list(APPEND DLL_SEARCH_PATHS "${SDL2_LIB_DIR}")
            if(IS_DIRECTORY "${SDL2_LIB_DIR}/../bin")
                list(APPEND DLL_SEARCH_PATHS "${SDL2_LIB_DIR}/../bin")
            endif()
        endif()
    endif()
    if(SDL2_TTF_LIBRARIES AND NOT TARGET ${SDL2_TTF_LIBRARIES})
        get_filename_component(SDL2_TTF_LIB_DIR "${SDL2_TTF_LIBRARIES}" DIRECTORY)
        if(IS_DIRECTORY "${SDL2_TTF_LIB_DIR}")
            list(APPEND DLL_SEARCH_PATHS "${SDL2_TTF_LIB_DIR}")
            if(IS_DIRECTORY "${SDL2_TTF_LIB_DIR}/../bin")
                list(APPEND DLL_SEARCH_PATHS "${SDL2_TTF_LIB_DIR}/../bin")
            endif()
        endif()
    endif()
    if(CMAKE_PREFIX_PATH)
        foreach(prefix_p ${CMAKE_PREFIX_PATH})
            if(IS_DIRECTORY "${prefix_p}/bin")
                list(APPEND DLL_SEARCH_PATHS "${prefix_p}/bin")
            endif()
        endforeach()
    endif()
    list(REMOVE_DUPLICATES DLL_SEARCH_PATHS)

    foreach(DLL_NAME ${SDL_DLL_FILES})
        find_file(DLL_FILE_PATH NAMES ${DLL_NAME} HINTS ${DLL_SEARCH_PATHS} NO_DEFAULT_PATH)
        if(DLL_FILE_PATH)
            message(STATUS "Found DLL for Windows: ${DLL_FILE_PATH}")
            install(FILES "${DLL_FILE_PATH}" DESTINATION bin COMPONENT Runtime)
            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${DLL_FILE_PATH}"
                    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/"
                    COMMENT "Copying ${DLL_NAME} to build directory for Windows"
            )
            unset(DLL_FILE_PATH CACHE)
        else()
            message(WARNING "Could not find DLL: ${DLL_NAME}. It will not be bundled for Windows. The application may not run.")
        endif()
    endforeach()

    include(CPack)

    set(CPACK_PACKAGE_NAME "${PROJECT_NAME_STR}")
    set(CPACK_PACKAGE_VERSION "1.0.0")
    set(CPACK_PACKAGE_VENDOR "${COMPANY_NAME_STR}")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_NAME_STR} - Typing Application")
    set(CPACK_PACKAGE_HOMEPAGE_URL "https://example.com/${PROJECT_NAME_STR}")
    set(CPACK_PACKAGE_CONTACT "${COMPANY_NAME_STR}")
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "${PROJECT_NAME_STR} ${CPACK_PACKAGE_VERSION}")
    set(CPACK_NSIS_PACKAGE_NAME "${PROJECT_NAME_STR} ${CPACK_PACKAGE_VERSION}")
    set(CPACK_NSIS_DISPLAY_NAME "${PROJECT_NAME_STR}")
    set(CPACK_NSIS_PUBLISHER "${COMPANY_NAME_STR}")
    set(CPACK_NSIS_URL_INFO_ABOUT "https://example.com/${PROJECT_NAME_STR}")
    set(WINDOWS_APP_ICON_ICO "${CMAKE_CURRENT_SOURCE_DIR}/appicon.ico")
    if(EXISTS "${WINDOWS_APP_ICON_ICO}")
        set(CPACK_NSIS_MUI_ICON "${WINDOWS_APP_ICON_ICO}")
        set(CPACK_NSIS_MUI_UNIICON "${WINDOWS_APP_ICON_ICO}")
        install(FILES "${WINDOWS_APP_ICON_ICO}" DESTINATION "." COMPONENT Runtime)
    else()
        message(STATUS "appicon.ico not found at ${WINDOWS_APP_ICON_ICO}. Installer will use default icon.")
    endif()
    set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES64")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_EXECUTABLES_DIRECTORY "bin")
    set(CPACK_NSIS_MENU_LINKS
            "bin/${PROJECT_NAME}.exe" "${PROJECT_NAME_STR}"
    )
    set(CPACK_COMPONENT_RUNTIME_DISPLAY_NAME "Application Files")
    set(CPACK_COMPONENT_RUNTIME_DESCRIPTION "The main application executable and required libraries.")
    set(CPACK_COMPONENT_RUNTIME_REQUIRED TRUE)
    set(CPACK_COMPONENTS_ALL Runtime)
endif() # WIN32

if(NOT APPLE AND NOT CMAKE_CROSSCOMPILING)
    install(TARGETS ${PROJECT_NAME}
            RUNTIME DESTINATION bin
            COMPONENT Runtime
    )
endif()