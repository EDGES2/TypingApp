cmake_minimum_required(VERSION 3.20)
project(TypingApp C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Ensure pkg-config sees Homebrew’s .pc files
set(ENV{PKG_CONFIG_PATH} "/opt/homebrew/lib/pkgconfig")

# Find SDL2 and SDL2_ttf via pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)
pkg_check_modules(SDL2_TTF REQUIRED sdl2_ttf)

add_executable(${PROJECT_NAME} main.c)

# Include directories from pkg-config
target_include_directories(${PROJECT_NAME} PRIVATE
        ${SDL2_INCLUDE_DIRS}
        ${SDL2_TTF_INCLUDE_DIRS}
)

# Library search paths from pkg-config
target_link_directories(${PROJECT_NAME} PRIVATE
        ${SDL2_LIBRARY_DIRS}
        ${SDL2_TTF_LIBRARY_DIRS}
)

# Compiler flags from pkg-config
target_compile_options(${PROJECT_NAME} PRIVATE
        ${SDL2_CFLAGS_OTHER}
        ${SDL2_TTF_CFLAGS_OTHER}
)

# Link libraries from pkg-config
target_link_libraries(${PROJECT_NAME} PRIVATE
        ${SDL2_TTF_LIBRARIES}
)


# Alternatively, if you have SDL2 Frameworks installed, you can link like this:
# target_link_libraries(${PROJECT_NAME} PRIVATE
#     "-F/Library/Frameworks" "-framework SDL2" "-framework SDL2_ttf" )