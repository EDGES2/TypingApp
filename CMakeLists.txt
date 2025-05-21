cmake_minimum_required(VERSION 3.20)
project(TypingApp C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Шлях до Homebrew для arm64
set(HOMEBREW_PREFIX "/opt/homebrew")
set(ENV{PKG_CONFIG_PATH} "${HOMEBREW_PREFIX}/lib/pkgconfig")

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)
pkg_check_modules(SDL2_TTF REQUIRED sdl2_ttf)

add_executable(${PROJECT_NAME} main.c)

target_include_directories(${PROJECT_NAME} PRIVATE
        ${SDL2_INCLUDE_DIRS}
        ${SDL2_TTF_INCLUDE_DIRS}
)

target_link_directories(${PROJECT_NAME} PRIVATE
        ${SDL2_LIBRARY_DIRS}
        ${SDL2_TTF_LIBRARY_DIRS}
)

target_compile_options(${PROJECT_NAME} PRIVATE
        ${SDL2_CFLAGS_OTHER}
        ${SDL2_TTF_CFLAGS_OTHER}
)

# Важливо: тепер ми просто вказуємо назви бібліотек,
# а CMake знайде їх завдяки pkg-config
target_link_libraries(${PROJECT_NAME} PRIVATE SDL2 SDL2_ttf)


if(APPLE)
    set(MACOSX_BUNDLE_ICON_FILE "${CMAKE_CURRENT_SOURCE_DIR}/appicon.icns") # Переконайтесь, що файл існує
    set(MACOSX_BUNDLE_INFO_STRING "${PROJECT_NAME} Typing Application")
    set(MACOSX_BUNDLE_GUI_IDENTIFIER "com.yourcompany.${PROJECT_NAME}") # Замініть на ваш
    # set(MACOSX_BUNDLE_LONG_VERSION_STRING "${PROJECT_VERSION_STRING}") # Якщо визначено версію проекту
    # set(MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")   # Якщо визначено версію проекту

    # Створення базового Info.plist, якщо він не існує
    set(INFO_PLIST_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist")
    if(NOT EXISTS "${INFO_PLIST_PATH}")
        file(WRITE "${CMAKE_BINARY_DIR}/Info.plist.in"
                "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                "<!DOCTYPE plist PUBLIC \"-//Apple Computer//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
                "<plist version=\"1.0\">\n"
                "<dict>\n"
                "  <key>CFBundleDevelopmentRegion</key>\n"
                "  <string>English</string>\n"
                "  <key>CFBundleExecutable</key>\n"
                "  <string>${PROJECT_NAME}</string>\n"
                "  <key>CFBundleIconFile</key>\n"
                "  <string>appicon.icns</string>\n" # Назва файлу іконки всередині Resources
                "  <key>CFBundleIdentifier</key>\n"
                "  <string>${MACOSX_BUNDLE_GUI_IDENTIFIER}</string>\n"
                "  <key>CFBundleInfoDictionaryVersion</key>\n"
                "  <string>6.0</string>\n"
                "  <key>CFBundleName</key>\n"
                "  <string>${PROJECT_NAME}</string>\n"
                "  <key>CFBundlePackageType</key>\n"
                "  <string>APPL</string>\n"
                "  <key>CFBundleShortVersionString</key>\n"
                "  <string>1.0</string>\n" # Замініть на вашу версію
                "  <key>CFBundleVersion</key>\n"
                "  <string>1</string>\n"   # Замініть на вашу версію
                "  <key>NSPrincipalClass</key>\n" # Потрібно для SDL
                "  <string>NSApplication</string>\n"
                "  <key>NSMainNibFile</key>\n"    # Потрібно для SDL, якщо не використовуєте Storyboard
                "  <string></string>\n"
                "  <key>LSMinimumSystemVersion</key>\n"
                "  <string>10.9</string>" # Приклад мінімальної версії macOS
                "</dict>\n"
                "</plist>\n"
        )
        configure_file("${CMAKE_BINARY_DIR}/Info.plist.in" "${INFO_PLIST_PATH}" @ONLY)
    endif()

    set_target_properties(${PROJECT_NAME} PROPERTIES
            MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_INFO_PLIST "${INFO_PLIST_PATH}"
    )

    # Копіювання ресурсів (text.txt та іконки) всередину бандла
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
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/../Resources/appicon.icns" # Назва файлу іконки всередині бандла
                COMMENT "Copying appicon.icns to Bundle Resources"
        )
    endif()

    # Шлях, куди будуть копіюватися бібліотеки всередині бандла
    set(FRAMEWORKS_DIR "$<TARGET_FILE_DIR:${PROJECT_NAME}>/../Frameworks")

    # Список бібліотек для копіювання (приклад, потрібно адаптувати)
    # Ви можете знайти точні шляхи, виконавши `otool -L TypingApp.app/Contents/MacOS/TypingApp`
    # після звичайної збірки, щоб побачити, на які бібліотеки з Homebrew він посилається.
    set(LIBS_TO_BUNDLE
            "${SDL2_LIBRARIES}" # Це зазвичай повний шлях до libSDL2.dylib
            "${SDL2_TTF_LIBRARIES}" # Це зазвичай повний шлях до libSDL2_ttf.dylib
            # Додайте сюди шляхи до інших залежностей SDL2_ttf, якщо потрібно:
            # Наприклад, для arm64 Mac з Homebrew:
            "${HOMEBREW_PREFIX}/lib/libfreetype.dylib"
            "${HOMEBREW_PREFIX}/lib/libpng16.dylib" # Або libpng.dylib
            "${HOMEBREW_PREFIX}/lib/libbrotlidec.dylib"
            "${HOMEBREW_PREFIX}/lib/libbrotlicommon.dylib"
            "${HOMEBREW_PREFIX}/lib/libharfbuzz.dylib"
            # Залежності harfbuzz:
            "${HOMEBREW_PREFIX}/lib/libglib-2.0.0.dylib"
            "${HOMEBREW_PREFIX}/lib/libpcre2-8.0.dylib" # Або схожий
            "${HOMEBREW_PREFIX}/opt/gettext/lib/libintl.8.dylib" # Або схожий
            "${HOMEBREW_PREFIX}/lib/libgraphite2.3.dylib"
            # Перевірте `otool -L /opt/homebrew/lib/libSDL2_ttf.dylib` та `otool -L /opt/homebrew/lib/libharfbuzz.dylib`
            # для повного списку залежностей, які не є системними.
    )

    # Видаляємо дублікати та порожні елементи зі списку
    list(REMOVE_DUPLICATES LIBS_TO_BUNDLE)
    list(FILTER LIBS_TO_BUNDLE EXCLUDE REGEX "^$")


    foreach(LIB_PATH ${LIBS_TO_BUNDLE})
        if(EXISTS "${LIB_PATH}")
            get_filename_component(LIB_NAME ${LIB_PATH} NAME)
            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${LIB_PATH}"
                    "${FRAMEWORKS_DIR}/${LIB_NAME}"
                    COMMENT "Copying ${LIB_NAME} to Bundle Frameworks"
            )
            # Змінюємо install_name для скопійованої бібліотеки, щоб вона посилалася на себе через @rpath
            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                    COMMAND install_name_tool -id "@rpath/${LIB_NAME}"
                    "${FRAMEWORKS_DIR}/${LIB_NAME}"
                    COMMENT "Fixing id for ${LIB_NAME}"
            )
            # Змінюємо шлях завантаження для цієї бібліотеки у виконуваному файлі
            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                    COMMAND install_name_tool -change "${LIB_PATH}" # Старий абсолютний шлях
                    "@rpath/${LIB_NAME}" # Новий шлях відносно @rpath
                    "$<TARGET_FILE:${PROJECT_NAME}>" # Сам виконуваний файл
                    COMMENT "Fixing library path for ${LIB_NAME} in executable"
            )

            # Потрібно також виправити залежності всередині самих бібліотек
            # (наприклад, libSDL2_ttf.dylib залежить від libfreetype.dylib)
            # Цей крок складніший і може вимагати рекурсивного обходу залежностей
            # або використання інструменту типу macdeployqt (але він для Qt) або ручного скрипту.
            # Для простоти, почнемо з цього. Якщо будуть проблеми, цей крок потрібно буде доопрацювати.
            # Наприклад, для кожної скопійованої бібліотеки пройтися по її залежностям (otool -L)
            # і виправити їх на @rpath/...
            #
            # Приклад для однієї залежності (якщо libSDL2_ttf.dylib залежить від libfreetype.6.dylib):
            # find_program(OTOOL_EXECUTABLE otool)
            # if(OTOOL_EXECUTABLE)
            #     add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            #         COMMAND bash -c "for dep in $(${OTOOL_EXECUTABLE} -L ${FRAMEWORKS_DIR}/${LIB_NAME} | grep '${HOMEBREW_PREFIX}' | awk '{print $1}'); do install_name_tool -change \"$${dep}\" \"@rpath/$$(basename \"$${dep}\")\" ${FRAMEWORKS_DIR}/${LIB_NAME}; done"
            #         COMMENT "Fixing internal dependencies for ${LIB_NAME}"
            #     )
            # endif()

        else()
            message(WARNING "Library to bundle not found: ${LIB_PATH}")
        endif()
    endforeach()

    # Встановлюємо rpath для виконуваного файлу, щоб він шукав бібліотеки у Frameworks
    set_target_properties(${PROJECT_NAME} PROPERTIES
            BUILD_WITH_INSTALL_RPATH TRUE
            INSTALL_RPATH "@executable_path/../Frameworks"
    )
endif()

# Команда встановлення (залишається, але тепер встановлює бандл)
install(TARGETS ${PROJECT_NAME}
        BUNDLE DESTINATION "/Applications" # Встановлює TypingApp.app у /Applications
        # Для не-Apple систем можна додати звичайне встановлення:
        # RUNTIME DESTINATION bin COMPONENT Runtime # Якщо це не APPLE
)