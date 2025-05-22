#sudo cmake --build /Users/kirilltokarev/Programming/CLion/Me/TypingApp/cmake-build-debug --target install
set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum macOS version to run on")
cmake_minimum_required(VERSION 3.20) # [cite: 1]
project(TypingApp C) # [cite: 1]

set(CMAKE_C_STANDARD 23) # [cite: 1]
set(CMAKE_C_STANDARD_REQUIRED ON) # [cite: 1]

# Path to Homebrew for arm64 [cite: 1]
set(HOMEBREW_PREFIX "/opt/homebrew") # [cite: 1]
set(ENV{PKG_CONFIG_PATH} "${HOMEBREW_PREFIX}/lib/pkgconfig") # [cite: 1]

find_package(PkgConfig REQUIRED) # [cite: 1]
pkg_check_modules(SDL2 REQUIRED sdl2) # [cite: 1]
pkg_check_modules(SDL2_TTF REQUIRED sdl2_ttf) # [cite: 1]

# Find actual library files for SDL2
set(SDL2_LIBRARY_FILES_TO_BUNDLE "") # [cite: 1]
if(SDL2_FOUND AND SDL2_LIBRARIES AND SDL2_LIBRARY_DIRS) # [cite: 1]
    foreach(LIB_NAME_STEM ${SDL2_LIBRARIES}) # [cite: 1]
        if ("${LIB_NAME_STEM}" MATCHES "^-l(.+)") # [cite: 1]
            set(CLEAN_LIB_NAME_STEM "${CMAKE_MATCH_1}") # [cite: 1]
        else()
            set(CLEAN_LIB_NAME_STEM "${LIB_NAME_STEM}") # [cite: 1]
        endif()

        find_library(SDL2_SINGLE_LIB_FILE # [cite: 2]
                NAMES ${CLEAN_LIB_NAME_STEM} # [cite: 2]
                PATHS ${SDL2_LIBRARY_DIRS} # [cite: 2]
                NO_DEFAULT_PATH # [cite: 2]
        )
        if(SDL2_SINGLE_LIB_FILE) # [cite: 2]
            list(APPEND SDL2_LIBRARY_FILES_TO_BUNDLE "${SDL2_SINGLE_LIB_FILE}") # [cite: 2]
            unset(SDL2_SINGLE_LIB_FILE CACHE) # [cite: 2]
        else()
            message(WARNING "Could not find library file for stem '${CLEAN_LIB_NAME_STEM}' in '${SDL2_LIBRARY_DIRS}' for bundling.") # [cite: 3]
        endif()
    endforeach()
endif()
list(REMOVE_DUPLICATES SDL2_LIBRARY_FILES_TO_BUNDLE) # [cite: 3]

# Find actual library files for SDL2_ttf
set(SDL2_TTF_LIBRARY_FILES_TO_BUNDLE "") # [cite: 3]
if(SDL2_TTF_FOUND AND SDL2_TTF_LIBRARIES AND SDL2_TTF_LIBRARY_DIRS) # [cite: 3]
    foreach(LIB_NAME_STEM ${SDL2_TTF_LIBRARIES}) # [cite: 3]
        if ("${LIB_NAME_STEM}" MATCHES "^-l(.+)") # [cite: 3]
            set(CLEAN_LIB_NAME_STEM "${CMAKE_MATCH_1}") # [cite: 3]
        else()
            set(CLEAN_LIB_NAME_STEM "${LIB_NAME_STEM}") # [cite: 4]
        endif()

        find_library(SDL2_TTF_SINGLE_LIB_FILE # [cite: 4]
                NAMES ${CLEAN_LIB_NAME_STEM} # [cite: 4]
                PATHS ${SDL2_TTF_LIBRARY_DIRS} # [cite: 4]
                NO_DEFAULT_PATH # [cite: 4]
        )
        if(SDL2_TTF_SINGLE_LIB_FILE) # [cite: 4]
            list(APPEND SDL2_TTF_LIBRARY_FILES_TO_BUNDLE "${SDL2_TTF_SINGLE_LIB_FILE}") # [cite: 5]
            unset(SDL2_TTF_SINGLE_LIB_FILE CACHE) # [cite: 5]
        else()
            message(WARNING "Could not find library file for stem '${CLEAN_LIB_NAME_STEM}' in '${SDL2_TTF_LIBRARY_DIRS}' for bundling.") # [cite: 5]
        endif()
    endforeach()
endif()
list(REMOVE_DUPLICATES SDL2_TTF_LIBRARY_FILES_TO_BUNDLE) # [cite: 5]

add_executable(${PROJECT_NAME} main.c) # [cite: 5]

target_include_directories(${PROJECT_NAME} PRIVATE # [cite: 5]
        ${SDL2_INCLUDE_DIRS} # [cite: 5]
        ${SDL2_TTF_INCLUDE_DIRS} # [cite: 5]
)

target_link_directories(${PROJECT_NAME} PRIVATE # [cite: 5]
        ${SDL2_LIBRARY_DIRS} # [cite: 5]
        ${SDL2_TTF_LIBRARY_DIRS} # [cite: 5]
)

target_compile_options(${PROJECT_NAME} PRIVATE # [cite: 5]
        ${SDL2_CFLAGS_OTHER} # [cite: 6]
        ${SDL2_TTF_CFLAGS_OTHER} # [cite: 6]
)

target_link_libraries(${PROJECT_NAME} PRIVATE SDL2 SDL2_ttf) # [cite: 6]


if(APPLE) # [cite: 6]
    set(MACOSX_BUNDLE_ICON_FILE "${CMAKE_CURRENT_SOURCE_DIR}appicon.icns") # [cite: 6]
    set(MACOSX_BUNDLE_INFO_STRING "${PROJECT_NAME} Typing Application") # [cite: 6, 31]
    set(MACOSX_BUNDLE_GUI_IDENTIFIER "com.yourcompany.${PROJECT_NAME}") # [cite: 6, 31]

    set(INFO_PLIST_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist") # [cite: 6]
    if(NOT EXISTS "${INFO_PLIST_PATH}") # [cite: 6, 7]
        file(WRITE "${CMAKE_BINARY_DIR}/Info.plist.in" # [cite: 7]
                "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n" # [cite: 7]
                "<!DOCTYPE plist PUBLIC \"-//Apple Computer//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n" # [cite: 7, 32]
                "<plist version=\"1.0\">\n" # [cite: 7, 32]
                "<dict>\n" # [cite: 7, 32]
                "  <key>CFBundleDevelopmentRegion</key>\n" # [cite: 8, 32]
                "  <string>English</string>\n" # [cite: 8, 32]
                "  <key>CFBundleExecutable</key>\n" # [cite: 8, 32]
                "  <string>\${PROJECT_NAME}</string>\n" # [cite: 8, 33]
                "  <key>CFBundleIconFile</key>\n" # [cite: 8, 33]
                "  <string>appicon.icns</string>\n" # [cite: 8, 33]
                "  <key>CFBundleIdentifier</key>\n" # [cite: 9, 33]
                "  <string>\${MACOSX_BUNDLE_GUI_IDENTIFIER}</string>\n" # [cite: 9, 33]
                "  <key>CFBundleInfoDictionaryVersion</key>\n" # [cite: 9, 34]
                "  <string>6.0</string>\n" # [cite: 9, 34]
                "  <key>CFBundleName</key>\n" # [cite: 9, 34]
                "  <string>\${PROJECT_NAME}</string>\n" # [cite: 10, 34]
                "  <key>CFBundlePackageType</key>\n" # [cite: 10, 34]
                "  <string>APPL</string>\n" # [cite: 10, 34]
                "  <key>CFBundleShortVersionString</key>\n" # [cite: 10, 35]
                "  <string>1.0</string>\n" # [cite: 10, 35]
                "  <key>CFBundleVersion</key>\n" # [cite: 11, 35]
                "  <string>1</string>\n" # [cite: 11, 35]
                "  <key>NSPrincipalClass</key>\n" # [cite: 11, 36]
                "  <string>NSApplication</string>\n" # [cite: 11, 36]
                "  <key>NSMainNibFile</key>\n" # [cite: 11, 36]
                "  <string></string>\n" # [cite: 12, 36]
                "  <key>LSMinimumSystemVersion</key>\n" # [cite: 12, 36]
                "  <string>10.9</string>" # [cite: 12, 37]
                "</dict>\n" # [cite: 12, 37]
                "</plist>\n" # [cite: 12, 37]
        )
        configure_file("${CMAKE_BINARY_DIR}/Info.plist.in" "${INFO_PLIST_PATH}" @ONLY) # [cite: 13, 37]
    endif()

    set_target_properties(${PROJECT_NAME} PROPERTIES # [cite: 13]
            MACOSX_BUNDLE TRUE # [cite: 13]
            MACOSX_BUNDLE_INFO_PLIST "${INFO_PLIST_PATH}" # [cite: 13]
    )

    # Copy resources to Bundle Resources [cite: 13, 38]
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/text.txt") # [cite: 13, 38]
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD # [cite: 13, 38]
                COMMAND ${CMAKE_COMMAND} -E copy_if_different # [cite: 13, 38]
                "${CMAKE_CURRENT_SOURCE_DIR}/text.txt" # [cite: 14, 38]
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/../Resources/text.txt" # [cite: 14, 38]
                COMMENT "Copying text.txt to Bundle Resources" # [cite: 14, 38]
        )
    endif()
    if(EXISTS "${MACOSX_BUNDLE_ICON_FILE}") # [cite: 14, 39]
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD # [cite: 14, 39]
                COMMAND ${CMAKE_COMMAND} -E copy_if_different # [cite: 14, 39]
                "${MACOSX_BUNDLE_ICON_FILE}" # [cite: 15, 39]
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/../Resources/appicon.icns" # [cite: 15, 39]
                COMMENT "Copying appicon.icns to Bundle Resources" # [cite: 15, 40]
        )
    endif()

    # Path to Frameworks directory inside the bundle [cite: 15, 40]
    set(FRAMEWORKS_DIR "$<TARGET_FILE_DIR:${PROJECT_NAME}>/../Frameworks") # [cite: 15, 40]

    # --- Define list of libraries to bundle ---
    set(OTHER_DEPENDENCIES_TO_BUNDLE
            # Based on otool -L output for SDL2_ttf, HarfBuzz, FreeType
            "/opt/homebrew/opt/freetype/lib/libfreetype.6.dylib"
            "/opt/homebrew/opt/harfbuzz/lib/libharfbuzz.0.dylib"
            "/opt/homebrew/opt/glib/lib/libglib-2.0.0.dylib"
            "/opt/homebrew/opt/graphite2/lib/libgraphite2.3.dylib"
            "/opt/homebrew/opt/libpng/lib/libpng16.16.dylib"

            # USER ACTION REQUIRED: Check dependencies of libglib-2.0.0.dylib
            # Run this command in your terminal:
            # otool -L /opt/homebrew/opt/glib/lib/libglib-2.0.0.dylib
            # Add any Homebrew-provided libraries (e.g., pcre2, gettext/libintl) it lists.
            # Ensure they are from /opt/homebrew/... and not system paths.
            # Example (uncomment and adjust if needed after running otool):
            # "/opt/homebrew/lib/libpcre2-8.0.dylib"       #<-- Adjust actual path/name
            # "/opt/homebrew/opt/gettext/lib/libintl.8.dylib" #<-- Adjust actual path/name
    )

    set(LIBS_TO_BUNDLE
            ${SDL2_LIBRARY_FILES_TO_BUNDLE}
            ${SDL2_TTF_LIBRARY_FILES_TO_BUNDLE}
            ${OTHER_DEPENDENCIES_TO_BUNDLE}
    )
    list(REMOVE_DUPLICATES LIBS_TO_BUNDLE) # [cite: 19]
    list(FILTER LIBS_TO_BUNDLE EXCLUDE REGEX "^$") # Remove empty elements if any [cite: 19]


    foreach(LIB_PATH ${LIBS_TO_BUNDLE}) # [cite: 19]
        if(EXISTS "${LIB_PATH}") # [cite: 19]
            get_filename_component(LIB_NAME ${LIB_PATH} NAME) # [cite: 19]
            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD # [cite: 19]
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different # [cite: 20]
                    "${LIB_PATH}" # [cite: 20]
                    "${FRAMEWORKS_DIR}/${LIB_NAME}" # [cite: 20]
                    COMMENT "Copying ${LIB_NAME} to Bundle Frameworks" # [cite: 20]
            )
            # Fix id for the copied library [cite: 20]
            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD # [cite: 21]
                    COMMAND install_name_tool -id "@rpath/${LIB_NAME}" # [cite: 21]
                    "${FRAMEWORKS_DIR}/${LIB_NAME}" # [cite: 21]
                    COMMENT "Fixing id for ${LIB_NAME}" # [cite: 21]
            )
            # Fix library path in the executable [cite: 22]
            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD # [cite: 22]
                    COMMAND install_name_tool -change "${LIB_PATH}" # [cite: 22]
                    "@rpath/${LIB_NAME}" # [cite: 22]
                    "$<TARGET_FILE:${PROJECT_NAME}>" # [cite: 23]
                    COMMENT "Fixing library path for ${LIB_NAME} in executable" # [cite: 23]
            )

            # Fix internal dependencies within the copied library (EXPERIMENTAL)
            # This makes the bundled libraries look for their *own* dependencies (that are also bundled)
            # in @rpath (i.e., within the Frameworks directory).
            find_program(OTOOL_EXECUTABLE otool)
            find_program(BASH_EXECUTABLE bash)

            if(OTOOL_EXECUTABLE AND BASH_EXECUTABLE)
                # Prepare list of basenames of all bundled libraries for the script
                set(ALL_BUNDLED_LIB_BASENAMES "")
                foreach(b_lib_path_for_script ${LIBS_TO_BUNDLE}) # Use the full LIBS_TO_BUNDLE list
                    get_filename_component(b_lib_name_for_script ${b_lib_path_for_script} NAME)
                    list(APPEND ALL_BUNDLED_LIB_BASENAMES "${b_lib_name_for_script}")
                endforeach()
                # No need to convert to space-separated string here; list will be passed as separate args.

                # Path to the script template
                set(FIX_DEPS_SCRIPT_TEMPLATE "${CMAKE_CURRENT_SOURCE_DIR}/fix_inner_deps.sh.in")
                # Path for the configured script (specific to the library being processed if needed, or a general one)
                # For simplicity, we can use one configured script if its content doesn't change per LIB_NAME,
                # but the arguments to it will change.
                # Here, the script itself is generic, so one configure_file is enough.
                set(CONFIGURED_FIX_DEPS_SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/fix_inner_deps.sh")
                configure_file("${FIX_DEPS_SCRIPT_TEMPLATE}" "${CONFIGURED_FIX_DEPS_SCRIPT}" COPYONLY) # COPYONLY if no @VAR@ subs needed in script
                # OR remove COPYONLY if you add @VAR@ subs

                find_program(CHMOD_EXECUTABLE chmod)
                if(CHMOD_EXECUTABLE)
                    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                            COMMAND "${CHMOD_EXECUTABLE}" +x "${CONFIGURED_FIX_DEPS_SCRIPT}"
                            COMMENT "Make fix_inner_deps.sh executable"
                    )
                else()
                    message(WARNING "chmod executable not found. Cannot make ${CONFIGURED_FIX_DEPS_SCRIPT} executable.")
                endif()
                add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                        COMMAND "${BASH_EXECUTABLE}"
                        "${CONFIGURED_FIX_DEPS_SCRIPT}"
                        "${FRAMEWORKS_DIR}"  # Arg 1: $<TARGET_FILE_DIR:${PROJECT_NAME}>/../Frameworks (GenEx)
                        "${LIB_NAME}"        # Arg 2: Current library being processed
                        "${OTOOL_EXECUTABLE}" # Arg 3: Path to otool
                        "${HOMEBREW_PREFIX}"  # Arg 4: /opt/homebrew
                        ${ALL_BUNDLED_LIB_BASENAMES} # Args 5...: List of all bundled lib basenames
                        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
                        COMMENT "Fixing internal dependencies for ${LIB_NAME} using script"
                        VERBATIM # Ensures arguments are passed correctly, especially the list
                )
            else()
                message(WARNING "otool or bash not found. Cannot fix internal library dependencies automatically for ${LIB_NAME}. Bundle may not be portable.")
            endif()

        else()
            message(WARNING "Library to bundle not found: ${LIB_PATH}") # [cite: 28]
        endif()
    endforeach()

    # Set rpath for the executable to search in Frameworks directory [cite: 28]
    set_target_properties(${PROJECT_NAME} PROPERTIES # [cite: 28]
            BUILD_WITH_INSTALL_RPATH TRUE # [cite: 28]
            INSTALL_RPATH "@executable_path/../Frameworks" # [cite: 28]
    )
endif()

install(TARGETS ${PROJECT_NAME} # [cite: 29]
        BUNDLE DESTINATION "/Applications" # [cite: 29]
)