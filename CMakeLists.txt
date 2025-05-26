# # Initial comment, e.g., build command for macOS:
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
    set(HOMEBREW_PREFIX "/opt/homebrew") # Or /usr/local for Intel Macs, if Homebrew is installed there
    set(ENV{PKG_CONFIG_PATH} "${HOMEBREW_PREFIX}/lib/pkgconfig")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED sdl2)
    pkg_check_modules(SDL2_TTF REQUIRED sdl2_ttf) #
elseif(WIN32)
    # Windows specific configuration
    # # CMAKE_PREFIX_PATH should be set in CLion CMake options:
    # -DCMAKE_PREFIX_PATH="C:/dev_libs/SDL2-2.32.6/x86_64-w64-mingw32;C:/dev_libs/SDL2_ttf-2.24.0/x86_64-w64-mingw32"
    message(STATUS "WIN32: Initial CMAKE_PREFIX_PATH: ${CMAKE_PREFIX_PATH}")

    set(SDL2_BASE_DIR "")
    set(SDL2_TTF_BASE_DIR "")

    if(CMAKE_PREFIX_PATH)
        foreach(prefix_path_item ${CMAKE_PREFIX_PATH})
            if(EXISTS "${prefix_path_item}/include/SDL2/SDL.h" AND (NOT SDL2_BASE_DIR OR "${prefix_path_item}" MATCHES ".*SDL2[^_]*"))
                set(SDL2_BASE_DIR "${prefix_path_item}")
                message(STATUS "WIN32: Found potential SDL2 base directory: ${SDL2_BASE_DIR}") # [cite: 5]
            endif()
            if(EXISTS "${prefix_path_item}/include/SDL2/SDL_ttf.h" AND (NOT SDL2_TTF_BASE_DIR OR "${prefix_path_item}" MATCHES ".*SDL2_ttf.*"))
                set(SDL2_TTF_BASE_DIR "${prefix_path_item}")
                message(STATUS "WIN32: Found potential SDL2_ttf base directory: ${SDL2_TTF_BASE_DIR}") # [cite: 5]
            endif()
        endforeach()
    endif() # [cite: 6]

    if(NOT SDL2_BASE_DIR)
        message(FATAL_ERROR "WIN32: SDL2 base directory not found. Please ensure CMAKE_PREFIX_PATH contains the path to your SDL2 x86_64-w64-mingw32 directory (e.g., C:/dev_libs/SDL2-2.32.6/x86_64-w64-mingw32).") # [cite: 7]
    endif()
    if(NOT SDL2_TTF_BASE_DIR)
        message(FATAL_ERROR "WIN32: SDL2_ttf base directory not found. Please ensure CMAKE_PREFIX_PATH contains the path to your SDL2_ttf x86_64-w64-mingw32 directory (e.g., C:/dev_libs/SDL2_ttf-2.24.0/x86_64-w64-mingw32).") # [cite: 7]
    endif()

    set(SDL2_INCLUDE_DIRS "${SDL2_BASE_DIR}/include")
    set(SDL2_LIBRARIES    "${SDL2_BASE_DIR}/lib/libSDL2main.a;${SDL2_BASE_DIR}/lib/libSDL2.dll.a")
    set(SDL2_TTF_INCLUDE_DIRS "${SDL2_TTF_BASE_DIR}/include")
    set(SDL2_TTF_LIBRARIES    "${SDL2_TTF_BASE_DIR}/lib/libSDL2_ttf.dll.a")

    set(SDL2_FOUND TRUE)
    set(SDL2_TTF_FOUND TRUE)

    message(STATUS "WIN32: Manually set SDL2_INCLUDE_DIRS: ${SDL2_INCLUDE_DIRS}") # [cite: 8]
    message(STATUS "WIN32: Manually set SDL2_LIBRARIES: ${SDL2_LIBRARIES}") # [cite: 8]
    message(STATUS "WIN32: Manually set SDL2_TTF_INCLUDE_DIRS: ${SDL2_TTF_INCLUDE_DIRS}") # [cite: 8]
    message(STATUS "WIN32: Manually set SDL2_TTF_LIBRARIES: ${SDL2_TTF_LIBRARIES}") # [cite: 8]

    include_directories(SYSTEM "${SDL2_INCLUDE_DIRS}")
    include_directories(SYSTEM "${SDL2_TTF_INCLUDE_DIRS}")
    if(IS_DIRECTORY "${SDL2_INCLUDE_DIRS}/SDL2")
        include_directories(SYSTEM "${SDL2_INCLUDE_DIRS}/SDL2") # SDL2 headers are often located here [cite: 1]
        message(STATUS "WIN32: Globally added SYSTEM include path: ${SDL2_INCLUDE_DIRS}/SDL2") # [cite: 8]
    endif()
    message(STATUS "WIN32: Globally added SYSTEM include path: ${SDL2_INCLUDE_DIRS}") # [cite: 8]
    message(STATUS "WIN32: Globally added SYSTEM include path: ${SDL2_TTF_INCLUDE_DIRS}") # [cite: 9]

else()
    # Linux and other PkgConfig-based systems
    find_package(PkgConfig REQUIRED) # [cite: 9]
    pkg_check_modules(SDL2 REQUIRED sdl2)
    pkg_check_modules(SDL2_TTF REQUIRED sdl2_ttf)
endif()

# --- Executable Definition ---
# Add all .c files from the src/ directory [cite: 1]
add_executable(${PROJECT_NAME}
        src/main.c
        src/app_context.c
        src/event_handler.c
        src/file_paths.c
        src/layout_logic.c
        src/rendering.c
        src/stats_handler.c
        src/text_processing.c # [cite: 10]
        src/utf8_utils.c
)

# --- Include Directories ---
target_include_directories(${PROJECT_NAME} PRIVATE
        ${SDL2_INCLUDE_DIRS}
        ${SDL2_TTF_INCLUDE_DIRS}
        "${CMAKE_CURRENT_SOURCE_DIR}/src"  # Add src/ for local header files [cite: 1]
)

# --- Linking ---
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE
            ${SDL2_LIBRARIES}
            ${SDL2_TTF_LIBRARIES}
    )
    # If you have a resource.rc file, it should be in the project root or the path updated [cite: 1, 11]
    set(WINDOWS_RESOURCE_FILE "${CMAKE_CURRENT_SOURCE_DIR}/resource.rc") # [cite: 11]
    if(EXISTS "${WINDOWS_RESOURCE_FILE}")
        target_sources(${PROJECT_NAME} PRIVATE "${WINDOWS_RESOURCE_FILE}") # [cite: 11]
    else() # [cite: 11]
        message(STATUS "Windows resource file (resource.rc) not found. Executable will not have a custom icon via .rc file.") # [cite: 11, 12]
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

# ==========================================================================================
# --- macOS Specific Bundling and Packaging ---
# ==========================================================================================
if(APPLE)
    # ... (macOS specific bundling code remains unchanged) ...
    set(SDL2_LIBRARY_FILES_TO_BUNDLE "")
    if(SDL2_FOUND AND SDL2_LIBRARIES AND SDL2_LIBRARY_DIRS)
        foreach(LIB_NAME_STEM ${SDL2_LIBRARIES})
            if ("${LIB_NAME_STEM}" MATCHES "^-l(.+)")
                set(CLEAN_LIB_NAME_STEM "${CMAKE_MATCH_1}") # [cite: 16]
            else()
                set(CLEAN_LIB_NAME_STEM "${LIB_NAME_STEM}") # [cite: 17]
            endif()
            find_library(SDL2_SINGLE_LIB_FILE NAMES ${CLEAN_LIB_NAME_STEM} PATHS ${SDL2_LIBRARY_DIRS} NO_DEFAULT_PATH)
            if(SDL2_SINGLE_LIB_FILE)
                list(APPEND SDL2_LIBRARY_FILES_TO_BUNDLE "${SDL2_SINGLE_LIB_FILE}") # [cite: 17]
                unset(SDL2_SINGLE_LIB_FILE CACHE)
            else()
                message(WARNING "Could not find library file for stem '${CLEAN_LIB_NAME_STEM}' in '${SDL2_LIBRARY_DIRS}' for bundling.") # [cite: 18]
            endif()
        endforeach()
    endif()
    list(REMOVE_DUPLICATES SDL2_LIBRARY_FILES_TO_BUNDLE)

    set(SDL2_TTF_LIBRARY_FILES_TO_BUNDLE "")
    if(SDL2_TTF_FOUND AND SDL2_TTF_LIBRARIES AND SDL2_TTF_LIBRARY_DIRS)
        foreach(LIB_NAME_STEM ${SDL2_TTF_LIBRARIES}) # [cite: 18]
            if ("${LIB_NAME_STEM}" MATCHES "^-l(.+)")
                set(CLEAN_LIB_NAME_STEM "${CMAKE_MATCH_1}") # [cite: 19]
            else()
                set(CLEAN_LIB_NAME_STEM "${LIB_NAME_STEM}")
            endif()
            find_library(SDL2_TTF_SINGLE_LIB_FILE NAMES ${CLEAN_LIB_NAME_STEM} PATHS ${SDL2_TTF_LIBRARY_DIRS} NO_DEFAULT_PATH)
            if(SDL2_TTF_SINGLE_LIB_FILE) # [cite: 19]
                list(APPEND SDL2_TTF_LIBRARY_FILES_TO_BUNDLE "${SDL2_TTF_SINGLE_LIB_FILE}") # [cite: 20]
                unset(SDL2_TTF_SINGLE_LIB_FILE CACHE)
            else()
                message(WARNING "Could not find library file for stem '${CLEAN_LIB_NAME_STEM}' in '${SDL2_TTF_LIBRARY_DIRS}' for bundling.")
            endif()
        endforeach() # [cite: 20]
    endif()
    list(REMOVE_DUPLICATES SDL2_TTF_LIBRARY_FILES_TO_BUNDLE)
    # Update the path to appicon.icns [cite: 1, 21]
    set(MACOSX_BUNDLE_ICON_FILE "${CMAKE_CURRENT_SOURCE_DIR}/assets/appicon.icns") # [cite: 21]
    set(MACOSX_BUNDLE_INFO_STRING "${PROJECT_NAME_STR} Typing Application")
    set(MACOSX_BUNDLE_GUI_IDENTIFIER "${COMPANY_NAME_STR}.${PROJECT_NAME_STR}")
    set(INFO_PLIST_PATH "${CMAKE_CURRENT_BINARY_DIR}/Info.plist") # Generated Info.plist

    file(WRITE "${INFO_PLIST_PATH}"
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
            "<plist version=\"1.0\">\n"
            "<dict>\n"
            "  <key>CFBundleName</key>\n" # [cite: 22]
            "  <string>${PROJECT_NAME_STR}</string>\n"
            "  <key>CFBundleExecutable</key>\n"
            "  <string>${PROJECT_NAME}</string>\n"
            "  <key>CFBundleIdentifier</key>\n"
            "  <string>${MACOSX_BUNDLE_GUI_IDENTIFIER}</string>\n"
            "  <key>CFBundlePackageType</key>\n"
            "  <string>APPL</string>\n" # [cite: 23]
            "  <key>NSPrincipalClass</key>\n"
            "  <string>NSApplication</string>\n"
            "  <key>NSHighResolutionCapable</key>\n"
            "  <true/>\n"
            "  <key>CFBundleIconFile</key>\n"
            "  <string>appicon.icns</string>\n" # Name of icon file in bundle
            "  <key>CFBundleInfoDictionaryVersion</key>\n" # [cite: 24]
            "  <string>6.0</string>\n"
            "  <key>CFBundleShortVersionString</key>\n"
            "  <string>1.0</string>\n"
            "  <key>CFBundleVersion</key>\n"
            "  <string>1</string>\n"
            "  <key>LSMinimumSystemVersion</key>\n"
            "  <string>${CMAKE_OSX_DEPLOYMENT_TARGET}</string>\n" # [cite: 25]
            "  <key>CFBundleDevelopmentRegion</key>\n"
            "  <string>en</string>\n"
            "</dict>\n"
            "</plist>\n"
    )

    set_target_properties(${PROJECT_NAME} PROPERTIES MACOSX_BUNDLE TRUE MACOSX_BUNDLE_INFO_PLIST "${INFO_PLIST_PATH}")

    # Copying text.txt to the bundle (using the updated TEXT_FILE_SOURCE) [cite: 1]
    if(EXISTS "${TEXT_FILE_SOURCE}") # [cite: 26]
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${TEXT_FILE_SOURCE}" # Source: assets/text.txt [cite: 1]
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/../Resources/text.txt" # Destination in the bundle [cite: 1]
                COMMENT "Copying text.txt to Bundle Resources"
        )
    endif() # [cite: 27]

    # Copying appicon.icns to the bundle (using the updated MACOSX_BUNDLE_ICON_FILE) [cite: 1]
    if(EXISTS "${MACOSX_BUNDLE_ICON_FILE}")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MACOSX_BUNDLE_ICON_FILE}" # Source: assets/appicon.icns [cite: 1]
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/../Resources/appicon.icns" # Destination in the bundle [cite: 1]
                COMMENT "Copying appicon.icns to Bundle Resources" # [cite: 28]
        )
    endif()

    set(FRAMEWORKS_DIR "$<TARGET_FILE_DIR:${PROJECT_NAME}>/../Frameworks")

    set(OTHER_DEPENDENCIES_TO_BUNDLE
            "/opt/homebrew/opt/freetype/lib/libfreetype.6.dylib"
            "/opt/homebrew/opt/harfbuzz/lib/libharfbuzz.0.dylib"
            "/opt/homebrew/opt/glib/lib/libglib-2.0.0.dylib"
            "/opt/homebrew/opt/graphite2/lib/libgraphite2.3.dylib"
            "/opt/homebrew/opt/libpng/lib/libpng16.16.dylib" # [cite: 28]
            "/opt/homebrew/opt/gettext/lib/libintl.8.dylib" # [cite: 29]
            "/opt/homebrew/opt/pcre2/lib/libpcre2-8.0.dylib"
    )
    set(LIBS_TO_BUNDLE ${SDL2_LIBRARY_FILES_TO_BUNDLE} ${SDL2_TTF_LIBRARY_FILES_TO_BUNDLE} ${OTHER_DEPENDENCIES_TO_BUNDLE})
    list(REMOVE_DUPLICATES LIBS_TO_BUNDLE)
    list(FILTER LIBS_TO_BUNDLE EXCLUDE REGEX "^$") # Remove empty elements, if any [cite: 1]

    set(ALL_FINAL_BUNDLED_NAMES_LIST "") # List of final names for the script [cite: 1]
    foreach(temp_lib_path_for_list ${LIBS_TO_BUNDLE})
        get_filename_component(temp_orig_name_for_list ${temp_lib_path_for_list} NAME)
        set(temp_final_name_for_list ${temp_orig_name_for_list})
        # Logic for renaming SDL libraries for consistency [cite: 1]
        if(temp_lib_path_for_list MATCHES ".*/libSDL2-[0-9.]+\\.dylib$" OR temp_lib_path_for_list MATCHES ".*/libSDL2\\.dylib$")
            if (NOT temp_lib_path_for_list MATCHES ".*SDL2_image.*" AND NOT temp_lib_path_for_list MATCHES ".*SDL2_mixer.*" AND NOT temp_lib_path_for_list MATCHES ".*SDL2_net.*" AND NOT temp_lib_path_for_list MATCHES ".*SDL2_ttf.*") # [cite: 30]
                set(temp_final_name_for_list "libSDL2-2.0.0.dylib")
            endif()
        endif()
        if(temp_lib_path_for_list MATCHES ".*/libSDL2_ttf-[0-9.]+\\.dylib$" OR temp_lib_path_for_list MATCHES ".*/libSDL2_ttf\\.dylib$")
            set(temp_final_name_for_list "libSDL2_ttf-2.0.0.dylib") # [cite: 31]
        endif()
        list(APPEND ALL_FINAL_BUNDLED_NAMES_LIST "${temp_final_name_for_list}") # [cite: 31]
    endforeach()
    list(REMOVE_DUPLICATES ALL_FINAL_BUNDLED_NAMES_LIST)


    foreach(LIB_PATH ${LIBS_TO_BUNDLE})
        if(EXISTS "${LIB_PATH}")
            get_filename_component(ORIGINAL_LIB_FILENAME ${LIB_PATH} NAME)
            set(FINAL_BUNDLED_LIB_NAME ${ORIGINAL_LIB_FILENAME})
            # Logic for renaming SDL libraries (repeated for consistency during copying and path changes) [cite: 1, 32]
            if(LIB_PATH MATCHES ".*/libSDL2-[0-9.]+\\.dylib$" OR LIB_PATH MATCHES ".*/libSDL2\\.dylib$")
                if (NOT LIB_PATH MATCHES ".*SDL2_image.*" AND NOT LIB_PATH MATCHES ".*SDL2_mixer.*" AND NOT LIB_PATH MATCHES ".*SDL2_net.*" AND NOT LIB_PATH MATCHES ".*SDL2_ttf.*") # [cite: 32]
                    set(FINAL_BUNDLED_LIB_NAME "libSDL2-2.0.0.dylib")
                endif()
            endif() # [cite: 33]
            if(LIB_PATH MATCHES ".*/libSDL2_ttf-[0-9.]+\\.dylib$" OR LIB_PATH MATCHES ".*/libSDL2_ttf\\.dylib$")
                set(FINAL_BUNDLED_LIB_NAME "libSDL2_ttf-2.0.0.dylib") # [cite: 33]
            endif()

            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${LIB_PATH}" "${FRAMEWORKS_DIR}/${FINAL_BUNDLED_LIB_NAME}" # [cite: 34]
                    COMMENT "Copying ${FINAL_BUNDLED_LIB_NAME} to Bundle Frameworks"
            )
            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                    COMMAND /usr/bin/install_name_tool -id "@rpath/${FINAL_BUNDLED_LIB_NAME}"
                    "${FRAMEWORKS_DIR}/${FINAL_BUNDLED_LIB_NAME}" # [cite: 35]
                    COMMENT "Fixing id for ${FINAL_BUNDLED_LIB_NAME}" VERBATIM
            )
            # This command changes paths in the .app file itself [cite: 1]
            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                    COMMAND /usr/bin/install_name_tool -change "${LIB_PATH}" "@rpath/${FINAL_BUNDLED_LIB_NAME}" # [cite: 36]
                    "$<TARGET_FILE:${PROJECT_NAME}>"
                    COMMENT "Fixing library path for ${FINAL_BUNDLED_LIB_NAME} in executable" VERBATIM
            )

            find_program(OTOOL_EXECUTABLE otool) # [cite: 36]
            find_program(BASH_EXECUTABLE bash)

            if(OTOOL_EXECUTABLE AND BASH_EXECUTABLE) # [cite: 37]
                # Update the path to the script template [cite: 1]
                set(FIX_DEPS_SCRIPT_TEMPLATE "${CMAKE_CURRENT_SOURCE_DIR}/scripts/fix_inner_deps.sh.in") # [cite: 37]
                if(EXISTS "${FIX_DEPS_SCRIPT_TEMPLATE}")
                    set(CONFIGURED_FIX_DEPS_SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/fix_inner_deps_for_${FINAL_BUNDLED_LIB_NAME}.sh")
                    configure_file("${FIX_DEPS_SCRIPT_TEMPLATE}" "${CONFIGURED_FIX_DEPS_SCRIPT}" @ONLY) # [cite: 38]
                    find_program(CHMOD_EXECUTABLE chmod)
                    if(CHMOD_EXECUTABLE)
                        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                                COMMAND "${CHMOD_EXECUTABLE}" +x "${CONFIGURED_FIX_DEPS_SCRIPT}" # [cite: 39]
                                COMMENT "Make ${CONFIGURED_FIX_DEPS_SCRIPT} executable" VERBATIM
                        )
                    else() # [cite: 40]
                        message(WARNING "chmod executable not found. Cannot make ${CONFIGURED_FIX_DEPS_SCRIPT} executable.") # [cite: 40, 41]
                    endif()
                    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                            COMMAND "${BASH_EXECUTABLE}" "${CONFIGURED_FIX_DEPS_SCRIPT}"
                            "${FRAMEWORKS_DIR}" # [cite: 42]
                            "${FINAL_BUNDLED_LIB_NAME}"
                            "${OTOOL_EXECUTABLE}"
                            "${HOMEBREW_PREFIX}"
                            ${ALL_FINAL_BUNDLED_NAMES_LIST} # Pass the list of all final names [cite: 1, 43]
                            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
                            COMMENT "Fixing internal dependencies for ${FINAL_BUNDLED_LIB_NAME} using script" VERBATIM
                    ) # [cite: 44]
                else()
                    message(STATUS "fix_inner_deps.sh.in not found at ${FIX_DEPS_SCRIPT_TEMPLATE}. Skipping internal dependency fixing for ${FINAL_BUNDLED_LIB_NAME}.") # [cite: 44]
                endif()
            else()
                message(WARNING "otool or bash not found. Cannot fix internal library dependencies automatically for ${FINAL_BUNDLED_LIB_NAME}. Bundle may not be portable.") # [cite: 45, 46]
            endif()
        else()
            message(WARNING "Library to bundle not found (during foreach loop): ${LIB_PATH}") # [cite: 46]
        endif()
    endforeach()

    set_target_properties(${PROJECT_NAME} PROPERTIES
            BUILD_WITH_INSTALL_RPATH TRUE
            INSTALL_RPATH "@executable_path/../Frameworks"
    )

    # Explicit fix for main SDL libraries if not already fixed by the loop above [cite: 1, 47]
    # This might be redundant if the loop has already processed them correctly [cite: 1]
    set(SDL2_ACTUAL_LINK_PATH "/opt/homebrew/opt/sdl2/lib/libSDL2-2.0.0.dylib") # Or your actual path [cite: 1]
    set(SDL2_BUNDLED_NAME "libSDL2-2.0.0.dylib")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND /usr/bin/install_name_tool -change "${SDL2_ACTUAL_LINK_PATH}" "@rpath/${SDL2_BUNDLED_NAME}" "$<TARGET_FILE:${PROJECT_NAME}>"
            COMMENT "Explicitly fixing executable link for SDL2 (if not covered by loop)" VERBATIM
    )
    set(SDL2_TTF_ACTUAL_LINK_PATH "/opt/homebrew/opt/sdl2_ttf/lib/libSDL2_ttf-2.0.0.dylib") # Or your actual path [cite: 1, 48]
    set(SDL2_TTF_BUNDLED_NAME "libSDL2_ttf-2.0.0.dylib")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND /usr/bin/install_name_tool -change "${SDL2_TTF_ACTUAL_LINK_PATH}" "@rpath/${SDL2_TTF_BUNDLED_NAME}" "$<TARGET_FILE:${PROJECT_NAME}>"
            COMMENT "Explicitly fixing executable link for SDL2_ttf (if not covered by loop)" VERBATIM
    )

    # --- DMG Creation ---
    set(DMG_VOLUME_NAME "${PROJECT_NAME_STR}") # [cite: 48]
    set(FINAL_DMG_FILENAME "${CMAKE_BINARY_DIR}/${PROJECT_NAME_STR}.dmg")
    set(TEMP_DMG_FILENAME "${CMAKE_BINARY_DIR}/temp_${PROJECT_NAME_STR}.dmg")
    set(DMG_BACKGROUND_IMAGE_NAME "dmg_background.png") # [cite: 48]
    # Update the path to the DMG background image [cite: 1, 49]
    set(DMG_BACKGROUND_IMAGE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/assets/${DMG_BACKGROUND_IMAGE_NAME}") # [cite: 49]
    set(MOUNT_POINT "/tmp/dmg_mount_point_for_${PROJECT_NAME_STR}") # Use a temporary directory [cite: 1]
    set(APP_BUNDLE_SOURCE_PATH "$<TARGET_BUNDLE_DIR:${PROJECT_NAME}>")
    set(APP_BUNDLE_DESTINATION_NAME "${PROJECT_NAME_STR}.app")

    set(DMG_COMMANDS "")
    list(APPEND DMG_COMMANDS COMMAND ${CMAKE_COMMAND} -E remove -f "${FINAL_DMG_FILENAME}")
    list(APPEND DMG_COMMANDS COMMAND ${CMAKE_COMMAND} -E remove -f "${TEMP_DMG_FILENAME}")
    list(APPEND DMG_COMMANDS COMMAND hdiutil create -size 120m -fs HFS+ -volname "${DMG_VOLUME_NAME}" "${TEMP_DMG_FILENAME}"
            COMMENT "Creating temporary DMG (120MB, HFS+, Vol: ${DMG_VOLUME_NAME})")
    list(APPEND DMG_COMMANDS COMMAND hdiutil detach "${MOUNT_POINT}" -force -quiet || echo "Detach of ${MOUNT_POINT} failed or not mounted (expected on clean run)." # [cite: 50]
            COMMENT "Attempting to detach any pre-existing mount at ${MOUNT_POINT}") # [cite: 51]
    list(APPEND DMG_COMMANDS COMMAND ${CMAKE_COMMAND} -E rm -rf "${MOUNT_POINT}")
    list(APPEND DMG_COMMANDS COMMAND ${CMAKE_COMMAND} -E make_directory "${MOUNT_POINT}")
    list(APPEND DMG_COMMANDS COMMAND hdiutil attach "${TEMP_DMG_FILENAME}" -mountpoint "${MOUNT_POINT}" -nobrowse -quiet
            COMMENT "Mounting temporary DMG to ${MOUNT_POINT}")
    list(APPEND DMG_COMMANDS COMMAND sleep 2 COMMENT "Pausing for 2 seconds for volume to register before copying")
    list(APPEND DMG_COMMANDS COMMAND ditto --noqtn "${APP_BUNDLE_SOURCE_PATH}" "${MOUNT_POINT}/${APP_BUNDLE_DESTINATION_NAME}" # [cite: 52]
            COMMENT "Copying ${APP_BUNDLE_DESTINATION_NAME} to DMG volume")
    list(APPEND DMG_COMMANDS COMMAND ln -sf /Applications "${MOUNT_POINT}/Applications"
            COMMENT "Creating Applications symlink in DMG")
    list(APPEND DMG_COMMANDS COMMAND ls -la "${MOUNT_POINT}" COMMENT "Listing contents of DMG root after copying app and symlink") # [cite: 52]


    set(DMG_LAYOUT_COMMANDS "")
    if(EXISTS "${DMG_BACKGROUND_IMAGE_PATH}")
        list(APPEND DMG_LAYOUT_COMMANDS COMMAND ${CMAKE_COMMAND} -E make_directory "${MOUNT_POINT}/.background")
        list(APPEND DMG_LAYOUT_COMMANDS COMMAND ${CMAKE_COMMAND} -E copy "${DMG_BACKGROUND_IMAGE_PATH}" "${MOUNT_POINT}/.background/${DMG_BACKGROUND_IMAGE_NAME}") # [cite: 53]
        # AppleScript for customizing DMG appearance [cite: 1]
        list(APPEND DMG_LAYOUT_COMMANDS COMMAND osascript
                -e "tell application \"Finder\""
                -e "  delay 2" # Increased for reliability [cite: 1]
                -e "  try"
                -e "    set mountedVolumePath to POSIX file \"${MOUNT_POINT}\"" # [cite: 54]
                -e "    set mountedVolumeAlias to mountedVolumePath as alias" # [cite: 54]
                -e "    open mountedVolumeAlias"
                -e "    tell container window of mountedVolumeAlias" # [cite: 55]
                -e "      set current view to icon view"
                -e "      set toolbar visible to false"
                -e "      set statusbar visible to false"
                -e "      set the bounds to {100, 100, 780, 540}" # DMG window size [cite: 1, 56]
                -e "      set viewOptions to the icon view options"
                -e "      set arrangement of viewOptions to not arranged" # [cite: 56]
                -e "      set icon size of viewOptions to 100" # Icon size [cite: 1, 57]
                -e "      set background picture of viewOptions to file \".background:${DMG_BACKGROUND_IMAGE_NAME}\" of mountedVolumeAlias"
                -e "      set position of item \"${APP_BUNDLE_DESTINATION_NAME}\" to {180, 190}" # Position of .app [cite: 1]
                -e "      set position of item \"Applications\" to {550, 190}" # Position of Applications [cite: 1, 58]
                -e "    end tell"
                -e "    update mountedVolumeAlias without registering applications"
                -e "    delay 1"
                -e "    close mountedVolumeAlias"
                -e "  on error errMsg number errorNumber" # [cite: 59]
                -e "    log \"AppleScript Full Layout Error: \" & errMsg & \" (Number: \" & errorNumber & \")\"" # [cite: 60]
                -e "    return \"AppleScript Full Layout Error: \" & errMsg & \" (Number: \" & errorNumber & \")\""
                -e "  end try" # [cite: 60]
                -e "end tell"
                COMMENT "Setting DMG window layout and icon positions"
        )
        list(APPEND DMG_LAYOUT_COMMANDS COMMAND SetFile -a V "${MOUNT_POINT}/.background" || echo ".background folder not hidden (SetFile may have failed or not found)") # [cite: 61]
    else()
        message(STATUS "DMG background image not found at ${DMG_BACKGROUND_IMAGE_PATH}. Creating DMG without custom background/layout.")
        list(APPEND DMG_LAYOUT_COMMANDS COMMAND osascript
                -e "tell application \"Finder\""
                -e "  delay 2"
                -e "  try" # [cite: 62]
                -e "    set mountedVolumePath to POSIX file \"${MOUNT_POINT}\""
                -e "    set mountedVolumeAlias to mountedVolumePath as alias"
                -e "    open mountedVolumeAlias"
                -e "    tell container window of mountedVolumeAlias" # [cite: 63]
                -e "      set the bounds to {100, 100, 780, 540}" # [cite: 63]
                -e "    end tell"
                -e "    close mountedVolumeAlias"
                -e "  on error errMsg number errorNumber" # [cite: 64]
                -e "    log \"AppleScript Simple Layout Error: \" & errMsg & \" (Number: \" & errorNumber & \")\""
                -e "    return \"AppleScript Simple Layout Error: \" & errMsg & \" (Number: \" & errorNumber & \")\""
                -e "  end try"
                -e "end tell" # [cite: 65]
                COMMENT "Basic open/close of DMG window"
        )
    endif()
    list(APPEND DMG_COMMANDS ${DMG_LAYOUT_COMMANDS}) # [cite: 65]
    list(APPEND DMG_COMMANDS COMMAND sync COMMENT "Syncing filesystem")
    list(APPEND DMG_COMMANDS COMMAND sync COMMENT "Syncing filesystem again") # Additional sync for reliability [cite: 1]
    list(APPEND DMG_COMMANDS COMMAND hdiutil detach "${MOUNT_POINT}" -force -quiet
            COMMENT "Unmounting temporary DMG") # [cite: 66]
    list(APPEND DMG_COMMANDS COMMAND hdiutil convert "${TEMP_DMG_FILENAME}" -format UDZO -imagekey zlib-level=9 -o "${FINAL_DMG_FILENAME}"
            COMMENT "Converting to final compressed DMG (UDZO, zlib-9)")
    list(APPEND DMG_COMMANDS COMMAND ${CMAKE_COMMAND} -E remove -f "${TEMP_DMG_FILENAME}"
            COMMENT "Removing temporary DMG")

    add_custom_target(CreateDMGPackage ALL
            ${DMG_COMMANDS}
            DEPENDS ${PROJECT_NAME} # [cite: 67]
            VERBATIM
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
            COMMENT "Creating DMG package ${FINAL_DMG_FILENAME}"
    )

    install(TARGETS ${PROJECT_NAME} BUNDLE DESTINATION "/Applications" COMPONENT Runtime)

endif() # APPLE


# ==========================================================================================
# --- Windows Specific Installation and Packaging ---
# ==========================================================================================
if(WIN32)
    install(TARGETS ${PROJECT_NAME}
            RUNTIME DESTINATION bin # Install .exe to the bin directory [cite: 1]
            COMPONENT Runtime # [cite: 68]
    )

    set(RUNTIME_DEPENDENCY_DLLS
            "SDL2.dll"
            "SDL2_ttf.dll"
            "libfreetype-6.dll" # SDL2_ttf dependency [cite: 1]
            "libpng16-16.dll"   # freetype dependency [cite: 1]
            "zlib1.dll"         # libpng dependency [cite: 1, 68]
            "libbrotlidec.dll"  # freetype dependency (via harfbuzz, possibly) [cite: 1, 69]
            "libbrotlicommon.dll" # freetype dependency
            # MinGW dependencies (may vary depending on compiler version) [cite: 1]
            "libgcc_s_seh-1.dll" # Or another version, e.g., libgcc_s_dw2-1.dll for 32-bit [cite: 1]
            "libwinpthread-1.dll"
            # "libstdc++-6.dll" # If it were C++ [cite: 1, 70]
    )
    set(DLL_SEARCH_PATHS "")
    if(SDL2_BASE_DIR AND IS_DIRECTORY "${SDL2_BASE_DIR}/bin")
        list(APPEND DLL_SEARCH_PATHS "${SDL2_BASE_DIR}/bin")
    endif() # [cite: 70]
    if(SDL2_TTF_BASE_DIR AND IS_DIRECTORY "${SDL2_TTF_BASE_DIR}/bin")
        list(APPEND DLL_SEARCH_PATHS "${SDL2_TTF_BASE_DIR}/bin")
    endif() # [cite: 70]

    # Add path to MinGW compiler bin for DLL search (libgcc, libwinpthread) [cite: 1]
    if(DEFINED CMAKE_C_COMPILER_ID AND CMAKE_C_COMPILER_ID MATCHES "GNU")
        get_filename_component(MINGW_BIN_DIR "${CMAKE_C_COMPILER}" DIRECTORY)
        if(IS_DIRECTORY "${MINGW_BIN_DIR}")
            list(APPEND DLL_SEARCH_PATHS "${MINGW_BIN_DIR}") # [cite: 71]
            message(STATUS "WIN32: Added MinGW compiler bin directory to DLL search paths: ${MINGW_BIN_DIR}")
        endif()
    endif()
    list(REMOVE_DUPLICATES DLL_SEARCH_PATHS)
    message(STATUS "WIN32: DLL_SEARCH_PATHS: ${DLL_SEARCH_PATHS}") # [cite: 71]
    # [cite: 71] # REMOVED: set(WINDOWS_FONT_FILE_BASENAME ...) та set(WINDOWS_FONT_FILE_SOURCE_PATH ...)

    foreach(DLL_NAME ${RUNTIME_DEPENDENCY_DLLS})
        find_file(DLL_FILE_PATH NAMES ${DLL_NAME} HINTS ${DLL_SEARCH_PATHS} NO_DEFAULT_PATH)
        if(DLL_FILE_PATH)
            message(STATUS "Found DLL for Windows (for CPack install & POST_BUILD copy): ${DLL_FILE_PATH}") # [cite: 72]
            install(FILES "${DLL_FILE_PATH}" DESTINATION bin COMPONENT Runtime) # Copy DLL to bin
            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD # [cite: 72]
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${DLL_FILE_PATH}"
                    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/" # Copy next to .exe in the build directory [cite: 1, 73]
                    COMMENT "Copying ${DLL_NAME} to build directory for Windows" # [cite: 73]
            )
            unset(DLL_FILE_PATH CACHE) # Important for the next find_file search [cite: 1]
        else()
            message(WARNING "Could not find DLL: ${DLL_NAME}. It will not be included in ZIP/Installer. The application may not run.") # [cite: 74, 75]
        endif()
    endforeach()

    # Install text.txt (using the updated TEXT_FILE_SOURCE) [cite: 1]
    if(EXISTS "${TEXT_FILE_SOURCE}") # [cite: 75]
        install(FILES "${TEXT_FILE_SOURCE}" DESTINATION "." COMPONENT Runtime) # Install text.txt to the installation root [cite: 1]
    endif()

    # Install appicon.ico (from assets directory) [cite: 1]
    set(WINDOWS_APP_ICON_SOURCE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/assets/appicon.ico")
    if(EXISTS "${WINDOWS_APP_ICON_SOURCE_PATH}")
        install(FILES "${WINDOWS_APP_ICON_SOURCE_PATH}" DESTINATION "." COMPONENT Runtime) # Install appicon.ico to the installation root [cite: 1]
    endif() # [cite: 76]
    # [cite: 76] # REMOVED: Block if(EXISTS "${WINDOWS_FONT_FILE_SOURCE_PATH}") ... install(FILES ...) for font [cite: 1]

    # CPack settings for NSIS
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

    set(WINDOWS_APP_ICON_ICO "${CMAKE_CURRENT_SOURCE_DIR}/assets/appicon.ico")
    if(EXISTS "${WINDOWS_APP_ICON_ICO}")
        set(CPACK_NSIS_MUI_ICON "${WINDOWS_APP_ICON_ICO}")
        set(CPACK_NSIS_MUI_UNIICON "${WINDOWS_APP_ICON_ICO}")
    else()
        message(STATUS "appicon.ico not found at ${WINDOWS_APP_ICON_ICO}. Installer will use default icon.")
    endif()

    set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES64")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_REQUEST_EXECUTION_LEVEL "admin") # Keep administrator rights request [cite: 1]

    # Define the main executable target for CPack
    set(CPACK_PACKAGE_EXECUTABLES "${PROJECT_NAME}" "${PROJECT_NAME_STR}")

    # Path where executables are installed, relative to install root
    set(CPACK_NSIS_EXECUTABLES_DIRECTORY "bin")

    # Start Menu Links - Be explicit with the path to the executable
    set(CPACK_NSIS_MENU_LINKS
            "bin\\${PROJECT_NAME}.exe" # Path to .exe relative to $INSTDIR [cite: 1]
            "${PROJECT_NAME_STR}"      # Shortcut name in the Start Menu folder [cite: 1]
            # (The Start Menu folder will be named ${PROJECT_NAME_STR}) [cite: 1]
    )

    # Desktop Shortcut variable - CPack uses this with CPACK_PACKAGE_EXECUTABLES
    # We will also add an explicit command below for more robustness
    set(CPACK_NSIS_DESKTOP_SHORTCUTS "${PROJECT_NAME_STR}")

    # This variable should encourage NSIS to create icons if UI options are present
    set(CPACK_NSIS_CREATE_ICONS_EVERYWHERE TRUE)

    # Explicitly add NSIS command to create the desktop shortcut.
    # This command will be inserted into the generated NSIS script.
    # $DESKTOP is an NSIS constant for the desktop folder.
    # $INSTDIR is an NSIS constant for the installation directory.
    # appicon.ico is installed to $INSTDIR by: install(FILES ... DESTINATION "." ...)
    set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS
            "CreateShortCut '$DESKTOP\\${PROJECT_NAME_STR}.lnk' '$INSTDIR\\bin\\${PROJECT_NAME}.exe' '' '$INSTDIR\\appicon.ico' 0"
    )

    # Add command to remove the desktop shortcut during uninstallation
    set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS
            "Delete '$DESKTOP\\${PROJECT_NAME_STR}.lnk'"
    )

    # Component Description
    set(CPACK_COMPONENT_RUNTIME_DISPLAY_NAME "Application Files")
    set(CPACK_COMPONENT_RUNTIME_DESCRIPTION "The main application executable and required libraries.")
    set(CPACK_COMPONENT_RUNTIME_REQUIRED TRUE)
    set(CPACK_COMPONENTS_ALL Runtime)

    # --- Automatic ZIP archive creation after build ---
    set(WIN_ZIP_PACKAGE_BASENAME "${PROJECT_NAME_STR}-${CPACK_PACKAGE_VERSION}-win64_direct")
    set(WIN_FINAL_ZIP_FILENAME "${CMAKE_BINARY_DIR}/${WIN_ZIP_PACKAGE_BASENAME}.zip")
    set(WIN_ZIP_STAGING_DIR_PARENT "${CMAKE_CURRENT_BINARY_DIR}/staging_for_zip")
    set(WIN_ZIP_STAGING_DIR_NAME "${WIN_ZIP_PACKAGE_BASENAME}") # The archive root will have this name [cite: 1, 79]
    set(WIN_ZIP_STAGING_DIR_FULLPATH "${WIN_ZIP_STAGING_DIR_PARENT}/${WIN_ZIP_STAGING_DIR_NAME}")

    # Target to prepare the staging directory
    set(STAGING_PREP_COMMAND_LIST "")
    list(APPEND STAGING_PREP_COMMAND_LIST COMMAND ${CMAKE_COMMAND} -E remove -f "${WIN_FINAL_ZIP_FILENAME}") # Typo STAGING_PREP_C-OMMAND_LIST fixed
    list(APPEND STAGING_PREP_COMMAND_LIST COMMAND ${CMAKE_COMMAND} -E remove_directory "${WIN_ZIP_STAGING_DIR_FULLPATH}")
    list(APPEND STAGING_PREP_COMMAND_LIST COMMAND ${CMAKE_COMMAND} -E make_directory "${WIN_ZIP_STAGING_DIR_FULLPATH}/bin") # Create bin in staging [cite: 1]
    # Copy .exe to staging/bin [cite: 1, 80]
    list(APPEND STAGING_PREP_COMMAND_LIST COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE_DIR:${PROJECT_NAME}>/${PROJECT_NAME}.exe" "${WIN_ZIP_STAGING_DIR_FULLPATH}/bin/")

    # Copy DLLs to staging/bin
    foreach(dll_to_zip_name ${RUNTIME_DEPENDENCY_DLLS})
        list(APPEND STAGING_PREP_COMMAND_LIST COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE_DIR:${PROJECT_NAME}>/${dll_to_zip_name}" "${WIN_ZIP_STAGING_DIR_FULLPATH}/bin/")
    endforeach()

    # Copy appicon.ico (from assets) to staging root
    if(EXISTS "${WINDOWS_APP_ICON_ICO}") # Use the already defined variable [cite: 81]
        list(APPEND STAGING_PREP_COMMAND_LIST COMMAND ${CMAKE_COMMAND} -E copy_if_different "${WINDOWS_APP_ICON_ICO}" "${WIN_ZIP_STAGING_DIR_FULLPATH}/appicon.ico")
    endif()
    # [cite: 81] # REMOVED: Block if(EXISTS "${WINDOWS_FONT_FILE_SOURCE_PATH}") ... list(APPEND STAGING_PREP_COMMAND_LIST ...) for font [cite: 1]


    add_custom_target(PrepareWinZipStaging ALL
            ${STAGING_PREP_COMMAND_LIST}
            DEPENDS ${PROJECT_NAME}
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
            COMMENT "Preparing files for Windows ZIP package" # [cite: 82]
            VERBATIM
    )

    # Command to create the ZIP file from the staging directory
    add_custom_command(
            OUTPUT "${WIN_FINAL_ZIP_FILENAME}"
            COMMAND ${CMAKE_COMMAND} -E tar cvf "${WIN_FINAL_ZIP_FILENAME}" --format=zip . # '.' means the contents of the current directory [cite: 1, 83]
            WORKING_DIRECTORY "${WIN_ZIP_STAGING_DIR_FULLPATH}" # Important! Work from inside WIN_ZIP_STAGING_DIR_FULLPATH [cite: 1, 83, 84]
            DEPENDS PrepareWinZipStaging
            COMMENT "Creating ZIP: ${WIN_FINAL_ZIP_FILENAME} from ${WIN_ZIP_STAGING_DIR_FULLPATH}"
            VERBATIM
    )

    # Final target that depends on the ZIP file and cleans up the staging directory (optional)
    add_custom_target(CreateWinZipPackage_Direct ALL
            # The cleanup command can be removed if you want to keep staging for verification [cite: 1]
            # COMMAND ${CMAKE_COMMAND} -E remove_directory "${WIN_ZIP_STAGING_DIR_FULLPATH}"
            COMMAND ${CMAKE_COMMAND} -E echo "Windows ZIP package (direct method) should be available at ${WIN_FINAL_ZIP_FILENAME}" # [cite: 85]
            DEPENDS "${WIN_FINAL_ZIP_FILENAME}" # Depends on ZIP creation
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
            COMMENT "Finalizing Windows ZIP package: ${WIN_FINAL_ZIP_FILENAME}"
            VERBATIM # [cite: 87]
    )

endif() # WIN32

# General installation for non-APPLE systems (e.g., Linux, if not cross-compiling)
# This is already handled for Windows above
if(NOT APPLE AND NOT WIN32 AND NOT CMAKE_CROSSCOMPILING)
    install(TARGETS ${PROJECT_NAME}
            RUNTIME DESTINATION bin # Install to <prefix>/bin [cite: 1]
            COMPONENT Runtime #
    )
    # Possibly also need to copy text.txt for Linux installation here
    if(EXISTS "${TEXT_FILE_SOURCE}")
        install(FILES "${TEXT_FILE_SOURCE}" DESTINATION "share/${PROJECT_NAME_STR}" COMPONENT Runtime) # Example: share/TypingApp/text.txt [cite: 1]
    endif()
endif()

# Include CPack only if it's Windows or macOS, where we configured it [cite: 1]
if(APPLE OR WIN32)
    include(CPack)
endif()