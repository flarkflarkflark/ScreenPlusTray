cmake_minimum_required(VERSION 3.16)
project(screenplus-tray VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "$ENV{HOME}/.local" CACHE PATH "Install prefix" FORCE)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(DBUS REQUIRED IMPORTED_TARGET dbus-1)

add_executable(screenplus-tray
    src/main.c
    src/client.c
    src/sni.c
    src/json.c
)

target_compile_definitions(screenplus-tray PRIVATE
    _POSIX_C_SOURCE=200809L
    _DEFAULT_SOURCE
    SP_INSTALL_ICON_DIR="${CMAKE_INSTALL_PREFIX}/share/icons"
)

target_compile_options(screenplus-tray PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

target_link_libraries(screenplus-tray PRIVATE
    PkgConfig::DBUS
    m
)
