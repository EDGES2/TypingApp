# [cite: 59] # Initial comment, e.g., build command for macOS:
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
    set(HOMEBREW_PREFIX "/opt/homebrew") # Або /usr/local для Intel Macs, якщо Homebrew встановлено там
    set(ENV{PKG_CONFIG_PATH} "${HOMEBREW_PREFIX}/lib/pkgconfig")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED sdl2)
    pkg_check_modules(SDL2_TTF REQUIRED sdl2_ttf)
elseif(WIN32)
    # Windows specific configuration
    # [cite: 60] # CMAKE_PREFIX_PATH should be set in CLion CMake options:
    # -DCMAKE_PREFIX_PATH="C:/dev_libs/SDL2-2.32.6/x86_64-w64-mingw32;C:/dev_libs/SDL2_ttf-2.24.0/x86_64-w64-mingw32"
    message(STATUS "WIN32: Initial CMAKE_PREFIX_PATH: ${CMAKE_PREFIX_PATH}")

    set(SDL2_BASE_DIR "")
    set(SDL2_TTF_BASE_DIR "")

    if(CMAKE_PREFIX_PATH)
        foreach(prefix_path_item ${CMAKE_PREFIX_PATH})
            if(EXISTS "${prefix_path_item}/include/SDL2/SDL.h" AND (NOT SDL2_BASE_DIR OR "${prefix_path_item}" MATCHES ".*SDL2[^_]*"))
                set(SDL2_BASE_DIR "${prefix_path_item}")
                message(STATUS "WIN32: Found potential SDL2 base directory: ${SDL2_BASE_DIR}") # [cite: 61]
            endif()
            if(EXISTS "${prefix_path_item}/include/SDL2/SDL_ttf.h" AND (NOT SDL2_TTF_BASE_DIR OR "${prefix_path_item}" MATCHES ".*SDL2_ttf.*"))
                set(SDL2_TTF_BASE_DIR "${prefix_path_item}")
                message(STATUS "WIN32: Found potential SDL2_ttf base directory: ${SDL2_TTF_BASE_DIR}")
            endif()
        endforeach()
    endif() # [cite: 62]

    if(NOT SDL2_BASE_DIR)
        message(FATAL_ERROR "WIN32: SDL2 base directory not found. Please ensure CMAKE_PREFIX_PATH contains the path to your SDL2 x86_64-w64-mingw32 directory (e.g., C:/dev_libs/SDL2-2.32.6/x86_64-w64-mingw32).") # [cite: 63]
    endif()
    if(NOT SDL2_TTF_BASE_DIR)
        message(FATAL_ERROR "WIN32: SDL2_ttf base directory not found. Please ensure CMAKE_PREFIX_PATH contains the path to your SDL2_ttf x86_64-w64-mingw32 directory (e.g., C:/dev_libs/SDL2_ttf-2.24.0/x86_64-w64-mingw32).")
    endif()

    set(SDL2_INCLUDE_DIRS "${SDL2_BASE_DIR}/include")
    set(SDL2_LIBRARIES    "${SDL2_BASE_DIR}/lib/libSDL2main.a;${SDL2_BASE_DIR}/lib/libSDL2.dll.a")
    set(SDL2_TTF_INCLUDE_DIRS "${SDL2_TTF_BASE_DIR}/include")
    set(SDL2_TTF_LIBRARIES    "${SDL2_TTF_BASE_DIR}/lib/libSDL2_ttf.dll.a")

    set(SDL2_FOUND TRUE)
    set(SDL2_TTF_FOUND TRUE)

    message(STATUS "WIN32: Manually set SDL2_INCLUDE_DIRS: ${SDL2_INCLUDE_DIRS}")
    message(STATUS "WIN32: Manually set SDL2_LIBRARIES: ${SDL2_LIBRARIES}") # [cite: 64]
    message(STATUS "WIN32: Manually set SDL2_TTF_INCLUDE_DIRS: ${SDL2_TTF_INCLUDE_DIRS}")
    message(STATUS "WIN32: Manually set SDL2_TTF_LIBRARIES: ${SDL2_TTF_LIBRARIES}")

    include_directories(SYSTEM "${SDL2_INCLUDE_DIRS}")
    include_directories(SYSTEM "${SDL2_TTF_INCLUDE_DIRS}")
    if(IS_DIRECTORY "${SDL2_INCLUDE_DIRS}/SDL2")
        include_directories(SYSTEM "${SDL2_INCLUDE_DIRS}/SDL2") # Часто заголовки SDL2 знаходяться тут
        message(STATUS "WIN32: Globally added SYSTEM include path: ${SDL2_INCLUDE_DIRS}/SDL2")
    endif()
    message(STATUS "WIN32: Globally added SYSTEM include path: ${SDL2_INCLUDE_DIRS}")
    message(STATUS "WIN32: Globally added SYSTEM include path: ${SDL2_TTF_INCLUDE_DIRS}")

else()
    # Linux and other PkgConfig-based systems
    find_package(PkgConfig REQUIRED) # [cite: 65]
    pkg_check_modules(SDL2 REQUIRED sdl2)
    pkg_check_modules(SDL2_TTF REQUIRED sdl2_ttf)
endif()

# --- Executable Definition ---
# Додаємо всі .c файли з каталогу src/
add_executable(${PROJECT_NAME}
        src/main.c
        src/app_context.c
        src/event_handler.c
        src/file_paths.c
        src/layout_logic.c
        src/rendering.c
        src/stats_handler.c
        src/text_processing.c
        src/utf8_utils.c
)

# --- Include Directories ---
target_include_directories(${PROJECT_NAME} PRIVATE
        ${SDL2_INCLUDE_DIRS}
        ${SDL2_TTF_INCLUDE_DIRS}
        "${CMAKE_CURRENT_SOURCE_DIR}/src"  # Додаємо src/ для локальних заголовкових файлів
)

# --- Linking ---
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE
            ${SDL2_LIBRARIES}
            ${SDL2_TTF_LIBRARIES}
    )
    # Якщо у вас є файл resource.rc, він має бути в корені проєкту або шлях оновлено
    set(WINDOWS_RESOURCE_FILE "${CMAKE_CURRENT_SOURCE_DIR}/resource.rc")
    if(EXISTS "${WINDOWS_RESOURCE_FILE}")
        target_sources(${PROJECT_NAME} PRIVATE "${WINDOWS_RESOURCE_FILE}")
    else() # [cite: 66]
        message(STATUS "Windows resource file (resource.rc) not found. Executable will not have a custom icon via .rc file.") # [cite: 67]
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
# Оновлюємо шлях до text.txt
set(TEXT_FILE_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/assets/text.txt") # [cite: 68]

if(EXISTS "${TEXT_FILE_SOURCE}")
    if(APPLE)
        # macOS bundle resource handling (part of the larger macOS section below)
    elseif(WIN32)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${TEXT_FILE_SOURCE}"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/text.txt"
                COMMENT "Copying text.txt to build directory for Windows" # [cite: 69]
        )
        # Для інсталятора CPack
        install(FILES "${TEXT_FILE_SOURCE}" DESTINATION "." COMPONENT Runtime)
    else() # Linux, etc.
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${TEXT_FILE_SOURCE}"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/text.txt" # [cite: 70]
                COMMENT "Copying text.txt to build directory"
        )
        # Можливо, також потрібно install для Linux, якщо ви плануєте створювати пакети
        # install(FILES "${TEXT_FILE_SOURCE}" DESTINATION "bin" COMPONENT Runtime) # Приклад
    endif()
else()
    message(WARNING "Default text file not found at ${TEXT_FILE_SOURCE}. Application might start with placeholder text.")
endif()


# ==========================================================================================
# --- macOS Specific Bundling and Packaging ---
# ==========================================================================================
if(APPLE)
    set(SDL2_LIBRARY_FILES_TO_BUNDLE "")
    if(SDL2_FOUND AND SDL2_LIBRARIES AND SDL2_LIBRARY_DIRS)
        foreach(LIB_NAME_STEM ${SDL2_LIBRARIES})
            if ("${LIB_NAME_STEM}" MATCHES "^-l(.+)")
                set(CLEAN_LIB_NAME_STEM "${CMAKE_MATCH_1}") # [cite: 71]
            else()
                set(CLEAN_LIB_NAME_STEM "${LIB_NAME_STEM}")
            endif()
            find_library(SDL2_SINGLE_LIB_FILE NAMES ${CLEAN_LIB_NAME_STEM} PATHS ${SDL2_LIBRARY_DIRS} NO_DEFAULT_PATH)
            if(SDL2_SINGLE_LIB_FILE)
                list(APPEND SDL2_LIBRARY_FILES_TO_BUNDLE "${SDL2_SINGLE_LIB_FILE}") # [cite: 72]
                unset(SDL2_SINGLE_LIB_FILE CACHE)
            else()
                message(WARNING "Could not find library file for stem '${CLEAN_LIB_NAME_STEM}' in '${SDL2_LIBRARY_DIRS}' for bundling.")
            endif()
        endforeach()
    endif()
    list(REMOVE_DUPLICATES SDL2_LIBRARY_FILES_TO_BUNDLE)

    set(SDL2_TTF_LIBRARY_FILES_TO_BUNDLE "")
    if(SDL2_TTF_FOUND AND SDL2_TTF_LIBRARIES AND SDL2_TTF_LIBRARY_DIRS)
        foreach(LIB_NAME_STEM ${SDL2_TTF_LIBRARIES}) # [cite: 73]
            if ("${LIB_NAME_STEM}" MATCHES "^-l(.+)")
                set(CLEAN_LIB_NAME_STEM "${CMAKE_MATCH_1}")
            else()
                set(CLEAN_LIB_NAME_STEM "${LIB_NAME_STEM}")
            endif()
            find_library(SDL2_TTF_SINGLE_LIB_FILE NAMES ${CLEAN_LIB_NAME_STEM} PATHS ${SDL2_TTF_LIBRARY_DIRS} NO_DEFAULT_PATH)
            if(SDL2_TTF_SINGLE_LIB_FILE) # [cite: 74]
                list(APPEND SDL2_TTF_LIBRARY_FILES_TO_BUNDLE "${SDL2_TTF_SINGLE_LIB_FILE}")
                unset(SDL2_TTF_SINGLE_LIB_FILE CACHE)
            else()
                message(WARNING "Could not find library file for stem '${CLEAN_LIB_NAME_STEM}' in '${SDL2_TTF_LIBRARY_DIRS}' for bundling.")
            endif()
        endforeach() # [cite: 75]
    endif()
    list(REMOVE_DUPLICATES SDL2_TTF_LIBRARY_FILES_TO_BUNDLE)
    # Оновлюємо шлях до appicon.icns
    set(MACOSX_BUNDLE_ICON_FILE "${CMAKE_CURRENT_SOURCE_DIR}/assets/appicon.icns") # [cite: 75]
    set(MACOSX_BUNDLE_INFO_STRING "${PROJECT_NAME_STR} Typing Application")
    set(MACOSX_BUNDLE_GUI_IDENTIFIER "${COMPANY_NAME_STR}.${PROJECT_NAME_STR}")
    set(INFO_PLIST_PATH "${CMAKE_CURRENT_BINARY_DIR}/Info.plist") # Generated Info.plist

    file(WRITE "${INFO_PLIST_PATH}"
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
            "<plist version=\"1.0\">\n"
            "<dict>\n"
            "  <key>CFBundleName</key>\n" # [cite: 76]
            "  <string>${PROJECT_NAME_STR}</string>\n"
            "  <key>CFBundleExecutable</key>\n"
            "  <string>${PROJECT_NAME}</string>\n"
            "  <key>CFBundleIdentifier</key>\n"
            "  <string>${MACOSX_BUNDLE_GUI_IDENTIFIER}</string>\n"
            "  <key>CFBundlePackageType</key>\n"
            "  <string>APPL</string>\n" # [cite: 77]
            "  <key>NSPrincipalClass</key>\n"
            "  <string>NSApplication</string>\n"
            "  <key>NSHighResolutionCapable</key>\n"
            "  <true/>\n"
            "  <key>CFBundleIconFile</key>\n"
            "  <string>appicon.icns</string>\n" # Назва файлу іконки в бандлі
            "  <key>CFBundleInfoDictionaryVersion</key>\n" # [cite: 78]
            "  <string>6.0</string>\n"
            "  <key>CFBundleShortVersionString</key>\n"
            "  <string>1.0</string>\n"
            "  <key>CFBundleVersion</key>\n"
            "  <string>1</string>\n"
            "  <key>LSMinimumSystemVersion</key>\n"
            "  <string>${CMAKE_OSX_DEPLOYMENT_TARGET}</string>\n" # [cite: 79]
            "  <key>CFBundleDevelopmentRegion</key>\n"
            "  <string>en</string>\n"
            "</dict>\n"
            "</plist>\n"
    )

    set_target_properties(${PROJECT_NAME} PROPERTIES MACOSX_BUNDLE TRUE MACOSX_BUNDLE_INFO_PLIST "${INFO_PLIST_PATH}")

    # Копіювання text.txt до бандлу (використовуємо оновлений TEXT_FILE_SOURCE)
    if(EXISTS "${TEXT_FILE_SOURCE}") # [cite: 80]
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${TEXT_FILE_SOURCE}" # Джерело: assets/text.txt
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/../Resources/text.txt" # Призначення в бандлі
                COMMENT "Copying text.txt to Bundle Resources"
        )
    endif() # [cite: 80]

    # Копіювання appicon.icns до бандлу (використовуємо оновлений MACOSX_BUNDLE_ICON_FILE)
    if(EXISTS "${MACOSX_BUNDLE_ICON_FILE}")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MACOSX_BUNDLE_ICON_FILE}" # Джерело: assets/appicon.icns
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/../Resources/appicon.icns" # Призначення в бандлі
                COMMENT "Copying appicon.icns to Bundle Resources"
        )
    endif()

    set(FRAMEWORKS_DIR "$<TARGET_FILE_DIR:${PROJECT_NAME}>/../Frameworks")

    set(OTHER_DEPENDENCIES_TO_BUNDLE
            "/opt/homebrew/opt/freetype/lib/libfreetype.6.dylib"
            "/opt/homebrew/opt/harfbuzz/lib/libharfbuzz.0.dylib"
            "/opt/homebrew/opt/glib/lib/libglib-2.0.0.dylib"
            "/opt/homebrew/opt/graphite2/lib/libgraphite2.3.dylib"
            "/opt/homebrew/opt/libpng/lib/libpng16.16.dylib" # [cite: 81]
            "/opt/homebrew/opt/gettext/lib/libintl.8.dylib"
            "/opt/homebrew/opt/pcre2/lib/libpcre2-8.0.dylib"
    )
    set(LIBS_TO_BUNDLE ${SDL2_LIBRARY_FILES_TO_BUNDLE} ${SDL2_TTF_LIBRARY_FILES_TO_BUNDLE} ${OTHER_DEPENDENCIES_TO_BUNDLE})
    list(REMOVE_DUPLICATES LIBS_TO_BUNDLE)
    list(FILTER LIBS_TO_BUNDLE EXCLUDE REGEX "^$") # Видаляємо порожні елементи, якщо такі є

    set(ALL_FINAL_BUNDLED_NAMES_LIST "") # Список фінальних імен для скрипта
    foreach(temp_lib_path_for_list ${LIBS_TO_BUNDLE})
        get_filename_component(temp_orig_name_for_list ${temp_lib_path_for_list} NAME)
        set(temp_final_name_for_list ${temp_orig_name_for_list})
        # Логіка перейменування SDL бібліотек для консистентності
        if(temp_lib_path_for_list MATCHES ".*/libSDL2-[0-9.]+\\.dylib$" OR temp_lib_path_for_list MATCHES ".*/libSDL2\\.dylib$")
            if (NOT temp_lib_path_for_list MATCHES ".*SDL2_image.*" AND NOT temp_lib_path_for_list MATCHES ".*SDL2_mixer.*" AND NOT temp_lib_path_for_list MATCHES ".*SDL2_net.*" AND NOT temp_lib_path_for_list MATCHES ".*SDL2_ttf.*") # [cite: 82]
                set(temp_final_name_for_list "libSDL2-2.0.0.dylib")
            endif()
        endif()
        if(temp_lib_path_for_list MATCHES ".*/libSDL2_ttf-[0-9.]+\\.dylib$" OR temp_lib_path_for_list MATCHES ".*/libSDL2_ttf\\.dylib$")
            set(temp_final_name_for_list "libSDL2_ttf-2.0.0.dylib")
        endif()
        list(APPEND ALL_FINAL_BUNDLED_NAMES_LIST "${temp_final_name_for_list}") # [cite: 83]
    endforeach()
    list(REMOVE_DUPLICATES ALL_FINAL_BUNDLED_NAMES_LIST)


    foreach(LIB_PATH ${LIBS_TO_BUNDLE})
        if(EXISTS "${LIB_PATH}")
            get_filename_component(ORIGINAL_LIB_FILENAME ${LIB_PATH} NAME)
            set(FINAL_BUNDLED_LIB_NAME ${ORIGINAL_LIB_FILENAME})
            # Логіка перейменування SDL бібліотек (повторюється для консистентності при копіюванні та зміні шляхів)
            if(LIB_PATH MATCHES ".*/libSDL2-[0-9.]+\\.dylib$" OR LIB_PATH MATCHES ".*/libSDL2\\.dylib$")
                if (NOT LIB_PATH MATCHES ".*SDL2_image.*" AND NOT LIB_PATH MATCHES ".*SDL2_mixer.*" AND NOT LIB_PATH MATCHES ".*SDL2_net.*" AND NOT LIB_PATH MATCHES ".*SDL2_ttf.*") # [cite: 84]
                    set(FINAL_BUNDLED_LIB_NAME "libSDL2-2.0.0.dylib")
                endif()
            endif()
            if(LIB_PATH MATCHES ".*/libSDL2_ttf-[0-9.]+\\.dylib$" OR LIB_PATH MATCHES ".*/libSDL2_ttf\\.dylib$")
                set(FINAL_BUNDLED_LIB_NAME "libSDL2_ttf-2.0.0.dylib") # [cite: 85]
            endif()

            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${LIB_PATH}" "${FRAMEWORKS_DIR}/${FINAL_BUNDLED_LIB_NAME}"
                    COMMENT "Copying ${FINAL_BUNDLED_LIB_NAME} to Bundle Frameworks"
            )
            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                    COMMAND /usr/bin/install_name_tool -id "@rpath/${FINAL_BUNDLED_LIB_NAME}"
                    "${FRAMEWORKS_DIR}/${FINAL_BUNDLED_LIB_NAME}"
                    COMMENT "Fixing id for ${FINAL_BUNDLED_LIB_NAME}" VERBATIM
            )
            # Ця команда змінює шляхи в самому .app файлі
            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                    COMMAND /usr/bin/install_name_tool -change "${LIB_PATH}" "@rpath/${FINAL_BUNDLED_LIB_NAME}"
                    "$<TARGET_FILE:${PROJECT_NAME}>"
                    COMMENT "Fixing library path for ${FINAL_BUNDLED_LIB_NAME} in executable" VERBATIM
            )

            find_program(OTOOL_EXECUTABLE otool) # [cite: 86]
            find_program(BASH_EXECUTABLE bash)

            if(OTOOL_EXECUTABLE AND BASH_EXECUTABLE)
                # Оновлюємо шлях до шаблону скрипта
                set(FIX_DEPS_SCRIPT_TEMPLATE "${CMAKE_CURRENT_SOURCE_DIR}/scripts/fix_inner_deps.sh.in") # [cite: 86]
                if(EXISTS "${FIX_DEPS_SCRIPT_TEMPLATE}")
                    set(CONFIGURED_FIX_DEPS_SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/fix_inner_deps_for_${FINAL_BUNDLED_LIB_NAME}.sh")
                    configure_file("${FIX_DEPS_SCRIPT_TEMPLATE}" "${CONFIGURED_FIX_DEPS_SCRIPT}" @ONLY) # [cite: 87]
                    find_program(CHMOD_EXECUTABLE chmod)
                    if(CHMOD_EXECUTABLE)
                        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                                COMMAND "${CHMOD_EXECUTABLE}" +x "${CONFIGURED_FIX_DEPS_SCRIPT}"
                                COMMENT "Make ${CONFIGURED_FIX_DEPS_SCRIPT} executable" VERBATIM
                        )
                    else() # [cite: 88]
                        message(WARNING "chmod executable not found. Cannot make ${CONFIGURED_FIX_DEPS_SCRIPT} executable.") # [cite: 89]
                    endif()
                    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                            COMMAND "${BASH_EXECUTABLE}" "${CONFIGURED_FIX_DEPS_SCRIPT}"
                            "${FRAMEWORKS_DIR}"
                            "${FINAL_BUNDLED_LIB_NAME}"
                            "${OTOOL_EXECUTABLE}"
                            "${HOMEBREW_PREFIX}"
                            ${ALL_FINAL_BUNDLED_NAMES_LIST} # Передаємо список усіх фінальних імен
                            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
                            COMMENT "Fixing internal dependencies for ${FINAL_BUNDLED_LIB_NAME} using script" VERBATIM
                    )
                else()
                    message(STATUS "fix_inner_deps.sh.in not found at ${FIX_DEPS_SCRIPT_TEMPLATE}. Skipping internal dependency fixing for ${FINAL_BUNDLED_LIB_NAME}.") # [cite: 90]
                endif()
            else()
                message(WARNING "otool or bash not found. Cannot fix internal library dependencies automatically for ${FINAL_BUNDLED_LIB_NAME}. Bundle may not be portable.")
            endif()
        else()
            message(WARNING "Library to bundle not found (during foreach loop): ${LIB_PATH}") # [cite: 91]
        endif()
    endforeach()

    set_target_properties(${PROJECT_NAME} PROPERTIES
            BUILD_WITH_INSTALL_RPATH TRUE
            INSTALL_RPATH "@executable_path/../Frameworks"
    )

    # Явне виправлення для головних бібліотек SDL, якщо вони ще не виправлені циклом вище
    # Це може бути надлишковим, якщо цикл вже обробив їх правильно
    set(SDL2_ACTUAL_LINK_PATH "/opt/homebrew/opt/sdl2/lib/libSDL2-2.0.0.dylib") # Або ваш актуальний шлях
    set(SDL2_BUNDLED_NAME "libSDL2-2.0.0.dylib")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND /usr/bin/install_name_tool -change "${SDL2_ACTUAL_LINK_PATH}" "@rpath/${SDL2_BUNDLED_NAME}" "$<TARGET_FILE:${PROJECT_NAME}>"
            COMMENT "Explicitly fixing executable link for SDL2 (if not covered by loop)" VERBATIM
    )
    set(SDL2_TTF_ACTUAL_LINK_PATH "/opt/homebrew/opt/sdl2_ttf/lib/libSDL2_ttf-2.0.0.dylib") # Або ваш актуальний шлях
    set(SDL2_TTF_BUNDLED_NAME "libSDL2_ttf-2.0.0.dylib")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND /usr/bin/install_name_tool -change "${SDL2_TTF_ACTUAL_LINK_PATH}" "@rpath/${SDL2_TTF_BUNDLED_NAME}" "$<TARGET_FILE:${PROJECT_NAME}>"
            COMMENT "Explicitly fixing executable link for SDL2_ttf (if not covered by loop)" VERBATIM
    )

    # --- DMG Creation ---
    set(DMG_VOLUME_NAME "${PROJECT_NAME_STR}") # [cite: 92]
    set(FINAL_DMG_FILENAME "${CMAKE_BINARY_DIR}/${PROJECT_NAME_STR}.dmg")
    set(TEMP_DMG_FILENAME "${CMAKE_BINARY_DIR}/temp_${PROJECT_NAME_STR}.dmg")
    set(DMG_BACKGROUND_IMAGE_NAME "dmg_background.png") # [cite: 92]
    # Оновлюємо шлях до фонового зображення DMG
    set(DMG_BACKGROUND_IMAGE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/assets/${DMG_BACKGROUND_IMAGE_NAME}") # [cite: 92]
    set(MOUNT_POINT "/tmp/dmg_mount_point_for_${PROJECT_NAME_STR}") # Використовуємо тимчасовий каталог
    set(APP_BUNDLE_SOURCE_PATH "$<TARGET_BUNDLE_DIR:${PROJECT_NAME}>")
    set(APP_BUNDLE_DESTINATION_NAME "${PROJECT_NAME_STR}.app")

    set(DMG_COMMANDS "")
    list(APPEND DMG_COMMANDS COMMAND ${CMAKE_COMMAND} -E remove -f "${FINAL_DMG_FILENAME}")
    list(APPEND DMG_COMMANDS COMMAND ${CMAKE_COMMAND} -E remove -f "${TEMP_DMG_FILENAME}")
    list(APPEND DMG_COMMANDS COMMAND hdiutil create -size 120m -fs HFS+ -volname "${DMG_VOLUME_NAME}" "${TEMP_DMG_FILENAME}"
            COMMENT "Creating temporary DMG (120MB, HFS+, Vol: ${DMG_VOLUME_NAME})")
    list(APPEND DMG_COMMANDS COMMAND hdiutil detach "${MOUNT_POINT}" -force -quiet || echo "Detach of ${MOUNT_POINT} failed or not mounted (expected on clean run)." # [cite: 93]
            COMMENT "Attempting to detach any pre-existing mount at ${MOUNT_POINT}") # [cite: 94]
    list(APPEND DMG_COMMANDS COMMAND ${CMAKE_COMMAND} -E rm -rf "${MOUNT_POINT}")
    list(APPEND DMG_COMMANDS COMMAND ${CMAKE_COMMAND} -E make_directory "${MOUNT_POINT}")
    list(APPEND DMG_COMMANDS COMMAND hdiutil attach "${TEMP_DMG_FILENAME}" -mountpoint "${MOUNT_POINT}" -nobrowse -quiet
            COMMENT "Mounting temporary DMG to ${MOUNT_POINT}")
    list(APPEND DMG_COMMANDS COMMAND sleep 2 COMMENT "Pausing for 2 seconds for volume to register before copying")
    list(APPEND DMG_COMMANDS COMMAND ditto --noqtn "${APP_BUNDLE_SOURCE_PATH}" "${MOUNT_POINT}/${APP_BUNDLE_DESTINATION_NAME}"
            COMMENT "Copying ${APP_BUNDLE_DESTINATION_NAME} to DMG volume")
    list(APPEND DMG_COMMANDS COMMAND ln -sf /Applications "${MOUNT_POINT}/Applications"
            COMMENT "Creating Applications symlink in DMG")
    list(APPEND DMG_COMMANDS COMMAND ls -la "${MOUNT_POINT}" COMMENT "Listing contents of DMG root after copying app and symlink") # [cite: 95]


    set(DMG_LAYOUT_COMMANDS "")
    if(EXISTS "${DMG_BACKGROUND_IMAGE_PATH}")
        list(APPEND DMG_LAYOUT_COMMANDS COMMAND ${CMAKE_COMMAND} -E make_directory "${MOUNT_POINT}/.background")
        list(APPEND DMG_LAYOUT_COMMANDS COMMAND ${CMAKE_COMMAND} -E copy "${DMG_BACKGROUND_IMAGE_PATH}" "${MOUNT_POINT}/.background/${DMG_BACKGROUND_IMAGE_NAME}")
        # AppleScript для налаштування вигляду DMG
        list(APPEND DMG_LAYOUT_COMMANDS COMMAND osascript
                -e "tell application \"Finder\""
                -e "  delay 2" # Збільшено для надійності
                -e "  try"
                -e "    set mountedVolumePath to POSIX file \"${MOUNT_POINT}\""
                -e "    set mountedVolumeAlias to mountedVolumePath as alias" # [cite: 96]
                -e "    open mountedVolumeAlias"
                -e "    tell container window of mountedVolumeAlias"
                -e "      set current view to icon view"
                -e "      set toolbar visible to false"
                -e "      set statusbar visible to false"
                -e "      set the bounds to {100, 100, 780, 540}" # Розмір вікна DMG
                -e "      set viewOptions to the icon view options"
                -e "      set arrangement of viewOptions to not arranged" # [cite: 97]
                -e "      set icon size of viewOptions to 100" # Розмір іконок
                -e "      set background picture of viewOptions to file \".background:${DMG_BACKGROUND_IMAGE_NAME}\" of mountedVolumeAlias"
                -e "      set position of item \"${APP_BUNDLE_DESTINATION_NAME}\" to {180, 190}" # Позиція .app
                -e "      set position of item \"Applications\" to {550, 190}" # Позиція Applications
                -e "    end tell"
                -e "    update mountedVolumeAlias without registering applications"
                -e "    delay 1"
                -e "    close mountedVolumeAlias"
                -e "  on error errMsg number errorNumber" # [cite: 98]
                -e "    log \"AppleScript Full Layout Error: \" & errMsg & \" (Number: \" & errorNumber & \")\""
                -e "    return \"AppleScript Full Layout Error: \" & errMsg & \" (Number: \" & errorNumber & \")\""
                -e "  end try"
                -e "end tell"
                COMMENT "Setting DMG window layout and icon positions"
        )
        list(APPEND DMG_LAYOUT_COMMANDS COMMAND SetFile -a V "${MOUNT_POINT}/.background" || echo ".background folder not hidden (SetFile may have failed or not found)") # [cite: 99]
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
                -e "      set the bounds to {100, 100, 780, 540}" # [cite: 100]
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
    list(APPEND DMG_COMMANDS ${DMG_LAYOUT_COMMANDS}) # [cite: 101]
    list(APPEND DMG_COMMANDS COMMAND sync COMMENT "Syncing filesystem")
    list(APPEND DMG_COMMANDS COMMAND sync COMMENT "Syncing filesystem again") # Додатковий sync для надійності
    list(APPEND DMG_COMMANDS COMMAND hdiutil detach "${MOUNT_POINT}" -force -quiet
            COMMENT "Unmounting temporary DMG")
    list(APPEND DMG_COMMANDS COMMAND hdiutil convert "${TEMP_DMG_FILENAME}" -format UDZO -imagekey zlib-level=9 -o "${FINAL_DMG_FILENAME}"
            COMMENT "Converting to final compressed DMG (UDZO, zlib-9)")
    list(APPEND DMG_COMMANDS COMMAND ${CMAKE_COMMAND} -E remove -f "${TEMP_DMG_FILENAME}"
            COMMENT "Removing temporary DMG")

    add_custom_target(CreateDMGPackage ALL
            ${DMG_COMMANDS}
            DEPENDS ${PROJECT_NAME}
            VERBATIM
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
            COMMENT "Creating DMG package ${FINAL_DMG_FILENAME}"
    )

    install(TARGETS ${PROJECT_NAME} BUNDLE DESTINATION "/Applications" COMPONENT Runtime)

endif() # [cite: 102] # APPLE


# ==========================================================================================
# --- Windows Specific Installation and Packaging ---
# ==========================================================================================
if(WIN32)
    install(TARGETS ${PROJECT_NAME}
            RUNTIME DESTINATION bin # Встановлюємо .exe в каталог bin
            COMPONENT Runtime
    )

    set(RUNTIME_DEPENDENCY_DLLS
            "SDL2.dll"
            "SDL2_ttf.dll"
            "libfreetype-6.dll" # Залежність SDL2_ttf
            "libpng16-16.dll"   # Залежність freetype
            "zlib1.dll"         # Залежність libpng # [cite: 103]
            "libbrotlidec.dll"  # Залежність freetype (через harfbuzz, можливо)
            "libbrotlicommon.dll" # Залежність freetype
            # Залежності MinGW (можуть відрізнятися залежно від версії компілятора)
            "libgcc_s_seh-1.dll" # Або інша версія, наприклад, libgcc_s_dw2-1.dll для 32-біт
            "libwinpthread-1.dll"
            # "libstdc++-6.dll" # Якщо б це був C++
    )
    set(DLL_SEARCH_PATHS "")
    if(SDL2_BASE_DIR AND IS_DIRECTORY "${SDL2_BASE_DIR}/bin")
        list(APPEND DLL_SEARCH_PATHS "${SDL2_BASE_DIR}/bin")
endif()
    if(SDL2_TTF_BASE_DIR AND IS_DIRECTORY "${SDL2_TTF_BASE_DIR}/bin")
        list(APPEND DLL_SEARCH_PATHS "${SDL2_TTF_BASE_DIR}/bin")
endif() # [cite: 104]

    # Додаємо шлях до bin компілятора MinGW для пошуку DLL (libgcc, libwinpthread)
    if(DEFINED CMAKE_C_COMPILER_ID AND CMAKE_C_COMPILER_ID MATCHES "GNU")
        get_filename_component(MINGW_BIN_DIR "${CMAKE_C_COMPILER}" DIRECTORY)
        if(IS_DIRECTORY "${MINGW_BIN_DIR}")
            list(APPEND DLL_SEARCH_PATHS "${MINGW_BIN_DIR}")
            message(STATUS "WIN32: Added MinGW compiler bin directory to DLL search paths: ${MINGW_BIN_DIR}")
        endif()
    endif()
    list(REMOVE_DUPLICATES DLL_SEARCH_PATHS)
    message(STATUS "WIN32: DLL_SEARCH_PATHS: ${DLL_SEARCH_PATHS}")
    # [cite: 105] # ВИДАЛЕНО: set(WINDOWS_FONT_FILE_BASENAME ...) та set(WINDOWS_FONT_FILE_SOURCE_PATH ...)

    foreach(DLL_NAME ${RUNTIME_DEPENDENCY_DLLS})
        find_file(DLL_FILE_PATH NAMES ${DLL_NAME} HINTS ${DLL_SEARCH_PATHS} NO_DEFAULT_PATH)
        if(DLL_FILE_PATH)
            message(STATUS "Found DLL for Windows (for CPack install & POST_BUILD copy): ${DLL_FILE_PATH}")
            install(FILES "${DLL_FILE_PATH}" DESTINATION bin COMPONENT Runtime) # Копіюємо DLL в bin
            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD # [cite: 106]
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${DLL_FILE_PATH}"
                    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/" # Копіюємо поруч з .exe в каталозі збірки
                    COMMENT "Copying ${DLL_NAME} to build directory for Windows" # [cite: 107]
            )
            unset(DLL_FILE_PATH CACHE) # Важливо для наступного пошуку find_file
        else()
            message(WARNING "Could not find DLL: ${DLL_NAME}. It will not be included in ZIP/Installer. The application may not run.") # [cite: 108]
        endif()
    endforeach()

    # Встановлення text.txt (використовуємо оновлений TEXT_FILE_SOURCE)
    if(EXISTS "${TEXT_FILE_SOURCE}") # [cite: 108]
        install(FILES "${TEXT_FILE_SOURCE}" DESTINATION "." COMPONENT Runtime) # Встановлюємо text.txt в корінь інсталяції
    endif()

    # Встановлення appicon.ico (з каталогу assets)
    set(WINDOWS_APP_ICON_SOURCE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/assets/appicon.ico")
    if(EXISTS "${WINDOWS_APP_ICON_SOURCE_PATH}")
        install(FILES "${WINDOWS_APP_ICON_SOURCE_PATH}" DESTINATION "." COMPONENT Runtime) # Встановлюємо appicon.ico в корінь інсталяції
    endif()
    # [cite: 108] # ВИДАЛЕНО: Блок if(EXISTS "${WINDOWS_FONT_FILE_SOURCE_PATH}") ... install(FILES ...) для шрифту

    # CPack settings for NSIS
    set(CPACK_PACKAGE_NAME "${PROJECT_NAME_STR}") # [cite: 109]
    set(CPACK_PACKAGE_VERSION "1.0.0") # Можна зробити динамічним
    set(CPACK_PACKAGE_VENDOR "${COMPANY_NAME_STR}")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_NAME_STR} - Typing Application")
    set(CPACK_PACKAGE_HOMEPAGE_URL "https://example.com/${PROJECT_NAME_STR}") # Замініть на реальний URL
    set(CPACK_PACKAGE_CONTACT "${COMPANY_NAME_STR}")

    set(CPACK_PACKAGE_INSTALL_DIRECTORY "${PROJECT_NAME_STR} ${CPACK_PACKAGE_VERSION}")
    set(CPACK_NSIS_PACKAGE_NAME "${PROJECT_NAME_STR} ${CPACK_PACKAGE_VERSION}")
    set(CPACK_NSIS_DISPLAY_NAME "${PROJECT_NAME_STR}") # Назва в Add/Remove Programs
    set(CPACK_NSIS_PUBLISHER "${COMPANY_NAME_STR}")
    set(CPACK_NSIS_URL_INFO_ABOUT "https://example.com/${PROJECT_NAME_STR}") # Посилання "Про програму"

    # Оновлюємо шлях до іконки для інсталятора (з assets)
    set(WINDOWS_APP_ICON_ICO "${CMAKE_CURRENT_SOURCE_DIR}/assets/appicon.ico") # [cite: 109]
    if(EXISTS "${WINDOWS_APP_ICON_ICO}")
        set(CPACK_NSIS_MUI_ICON "${WINDOWS_APP_ICON_ICO}")
        set(CPACK_NSIS_MUI_UNIICON "${WINDOWS_APP_ICON_ICO}") # Іконка для деінсталятора
    else()
        message(STATUS "appicon.ico not found at ${WINDOWS_APP_ICON_ICO}. Installer will use default icon.") # [cite: 110]
    endif()

    set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES64") # Або $PROGRAMFILES для 32-біт
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_EXECUTABLES_DIRECTORY "bin") # Ярлик буде вказувати на bin/TypingApp.exe
    set(CPACK_NSIS_MENU_LINKS
            "bin/${PROJECT_NAME}.exe" "${PROJECT_NAME_STR}" # Ярлик у меню "Пуск"
    )
    # Опис компонента
    set(CPACK_COMPONENT_RUNTIME_DISPLAY_NAME "Application Files")
    set(CPACK_COMPONENT_RUNTIME_DESCRIPTION "The main application executable and required libraries.")
    set(CPACK_COMPONENT_RUNTIME_REQUIRED TRUE)
    set(CPACK_COMPONENTS_ALL Runtime) # Включаємо наш єдиний компонент Runtime

    # --- Автоматичне створення ZIP-архіву після збірки ---
    set(WIN_ZIP_PACKAGE_BASENAME "${PROJECT_NAME_STR}-${CPACK_PACKAGE_VERSION}-win64_direct")
    set(WIN_FINAL_ZIP_FILENAME "${CMAKE_BINARY_DIR}/${WIN_ZIP_PACKAGE_BASENAME}.zip")
    set(WIN_ZIP_STAGING_DIR_PARENT "${CMAKE_CURRENT_BINARY_DIR}/staging_for_zip")
    set(WIN_ZIP_STAGING_DIR_NAME "${WIN_ZIP_PACKAGE_BASENAME}") # Корінь архіву буде мати цю назву # [cite: 111]
    set(WIN_ZIP_STAGING_DIR_FULLPATH "${WIN_ZIP_STAGING_DIR_PARENT}/${WIN_ZIP_STAGING_DIR_NAME}")

    # Target to prepare the staging directory
    set(STAGING_PREP_COMMAND_LIST "")
    list(APPEND STAGING_PREP_COMMAND_LIST COMMAND ${CMAKE_COMMAND} -E remove -f "${WIN_FINAL_ZIP_FILENAME}")
    list(APPEND STAGING_PREP_COMMAND_LIST COMMAND ${CMAKE_COMMAND} -E remove_directory "${WIN_ZIP_STAGING_DIR_FULLPATH}")
    list(APPEND STAGING_PREP_COMMAND_LIST COMMAND ${CMAKE_COMMAND} -E make_directory "${WIN_ZIP_STAGING_DIR_FULLPATH}/bin") # Створюємо bin в staging
    # Копіюємо .exe в staging/bin
    list(APPEND STAGING_PREP_COMMAND_LIST COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE_DIR:${PROJECT_NAME}>/${PROJECT_NAME}.exe" "${WIN_ZIP_STAGING_DIR_FULLPATH}/bin/")

    # Копіюємо DLL в staging/bin
    foreach(dll_to_zip_name ${RUNTIME_DEPENDENCY_DLLS})
        list(APPEND STAGING_PREP_COMMAND_LIST COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE_DIR:${PROJECT_NAME}>/${dll_to_zip_name}" "${WIN_ZIP_STAGING_DIR_FULLPATH}/bin/")
    endforeach()

    # Копіюємо text.txt (з assets) в корінь staging
    if(EXISTS "${TEXT_FILE_SOURCE}") # [cite: 112]
        list(APPEND STAGING_PREP_COMMAND_LIST COMMAND ${CMAKE_COMMAND} -E copy_if_different "${TEXT_FILE_SOURCE}" "${WIN_ZIP_STAGING_DIR_FULLPATH}/text.txt")
    endif()
    # Копіюємо appicon.ico (з assets) в корінь staging
    if(EXISTS "${WINDOWS_APP_ICON_ICO}") # Використовуємо вже визначену змінну # [cite: 112]
        list(APPEND STAGING_PREP_COMMAND_LIST COMMAND ${CMAKE_COMMAND} -E copy_if_different "${WINDOWS_APP_ICON_ICO}" "${WIN_ZIP_STAGING_DIR_FULLPATH}/appicon.ico")
    endif()
    # [cite: 112] # ВИДАЛЕНО: Блок if(EXISTS "${WINDOWS_FONT_FILE_SOURCE_PATH}") ... list(APPEND STAGING_PREP_COMMAND_LIST ...) для шрифту


    add_custom_target(PrepareWinZipStaging ALL
            ${STAGING_PREP_COMMAND_LIST}
            DEPENDS ${PROJECT_NAME}
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
            COMMENT "Preparing files for Windows ZIP package" # [cite: 113]
            VERBATIM
    )

    # Command to create the ZIP file from the staging directory
    add_custom_command(
            OUTPUT "${WIN_FINAL_ZIP_FILENAME}"
            COMMAND ${CMAKE_COMMAND} -E tar cvf "${WIN_FINAL_ZIP_FILENAME}" --format=zip . # '.' означає вміст поточного каталогу
            WORKING_DIRECTORY "${WIN_ZIP_STAGING_DIR_FULLPATH}" # Важливо! Працюємо зсередини WIN_ZIP_STAGING_DIR_FULLPATH # [cite: 114]
            DEPENDS PrepareWinZipStaging
            COMMENT "Creating ZIP: ${WIN_FINAL_ZIP_FILENAME} from ${WIN_ZIP_STAGING_DIR_FULLPATH}"
            VERBATIM
    )

    # Final target that depends on the ZIP file and cleans up the staging directory (опціонально)
    add_custom_target(CreateWinZipPackage_Direct ALL
            # Можна видалити команду очищення, якщо хочете залишити staging для перевірки
            # COMMAND ${CMAKE_COMMAND} -E remove_directory "${WIN_ZIP_STAGING_DIR_FULLPATH}"
            COMMAND ${CMAKE_COMMAND} -E echo "Windows ZIP package (direct method) should be available at ${WIN_FINAL_ZIP_FILENAME}" # [cite: 115]
            DEPENDS "${WIN_FINAL_ZIP_FILENAME}" # Залежить від створення ZIP
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
            COMMENT "Finalizing Windows ZIP package: ${WIN_FINAL_ZIP_FILENAME}"
            VERBATIM
    )

endif() # WIN32

# Загальна інсталяція для систем, що не є APPLE (наприклад, Linux, якщо не крос-компіляція)
# Для Windows це вже оброблено вище
if(NOT APPLE AND NOT WIN32 AND NOT CMAKE_CROSSCOMPILING)
    install(TARGETS ${PROJECT_NAME}
            RUNTIME DESTINATION bin # Встановлюємо в <prefix>/bin
            COMPONENT Runtime # [cite: 116]
    )
    # Можливо, тут також потрібно копіювати text.txt для Linux інсталяції
    if(EXISTS "${TEXT_FILE_SOURCE}")
        install(FILES "${TEXT_FILE_SOURCE}" DESTINATION "share/${PROJECT_NAME_STR}" COMPONENT Runtime) # Приклад: share/TypingApp/text.txt
    endif()
endif()

# Включаємо CPack тільки якщо це Windows або macOS, де ми його налаштували
if(APPLE OR WIN32)
    include(CPack)
endif()