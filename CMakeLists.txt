cmake_minimum_required(VERSION 3.20)
project(hwl C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ============================================================
# 1. 查找依赖 (pkg-config)
# ============================================================
find_package(PkgConfig REQUIRED)

pkg_check_modules(WLROOTS  REQUIRED IMPORTED_TARGET wlroots-0.19)
pkg_check_modules(WAYLAND   REQUIRED IMPORTED_TARGET wayland-server)
pkg_check_modules(XKBCOMMON REQUIRED IMPORTED_TARGET xkbcommon)
pkg_check_modules(XCBCOMMON REQUIRED IMPORTED_TARGET xcb)
pkg_check_modules(DRM       REQUIRED IMPORTED_TARGET libdrm)
pkg_check_modules(EGL       REQUIRED IMPORTED_TARGET egl)
pkg_check_modules(GLESv2    REQUIRED IMPORTED_TARGET glesv2)
pkg_check_modules(PIXMAN    REQUIRED IMPORTED_TARGET pixman-1)
pkg_check_modules(UDEV      REQUIRED IMPORTED_TARGET libudev)

# wayland-scanner 可执行文件
# wayland-scanner 可执行文件：直接查找（不依赖 pkg-config）
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)
message(STATUS "wayland-scanner: ${WAYLAND_SCANNER}")

# wayland-protocols XML 目录
pkg_get_variable(WL_PROT_DIR wayland-protocols pkgdatadir)
if(NOT WL_PROT_DIR)
    set(WL_PROT_DIR "/usr/share/wayland-protocols")
endif()
message(STATUS "wayland-protocols dir: ${WL_PROT_DIR}")

# ============================================================
# 2. 生成 wayland 协议文件 (xdg-shell)
# ============================================================
set(XDG_SHELL_XML "${WL_PROT_DIR}/stable/xdg-shell/xdg-shell.xml")

if(NOT EXISTS "${XDG_SHELL_XML}")
    message(FATAL_ERROR "xdg-shell.xml not found at ${XDG_SELL_XML}\n"
            "Install wayland-protocols: sudo apt install wayland-protocols")
endif()

set(PROTOCOL_SRC "${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-protocol.c")
set(PROTOCOL_HDR "${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-protocol.h")

# 生成 server-side 头文件
add_custom_command(
        OUTPUT "${PROTOCOL_HDR}"
        COMMAND ${WAYLAND_SCANNER} server-header
        "${XDG_SHELL_XML}" "${PROTOCOL_HDR}"
        DEPENDS "${XDG_SHELL_XML}"
        COMMENT "Generating xdg-shell-protocol.h"
)

# 生成 server-side C 代码
add_custom_command(
        OUTPUT "${PROTOCOL_SRC}"
        COMMAND ${WAYLAND_SCANNER} code
        "${XDG_SHELL_XML}" "${PROTOCOL_SRC}"
        DEPENDS "${XDG_SHELL_XML}"
        COMMENT "Generating xdg-shell-protocol.c"
)

# ============================================================
# 3. 定义可执行文件
# ============================================================
add_executable(hwl
        hwl.c
        "${PROTOCOL_SRC}"
        "${PROTOCOL_HDR}"
)

# 生成的头文件所在目录加入 include path
target_include_directories(hwl PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}"
)

# wlroots 头文件路径（Ubuntu 装在 /usr/include/wlroots-0.19/）
target_include_directories(hwl PRIVATE
        ${WLROOTS_INCLUDE_DIRS}
        ${WAYLAND_INCLUDE_DIRS}
        ${XKBCOMMON_INCLUDE_DIRS}
        ${DRM_INCLUDE_DIRS}
)

# ============================================================
# 4. 链接库
# ============================================================
target_link_libraries(hwl PRIVATE
        PkgConfig::WLROOTS
        PkgConfig::WAYLAND
        PkgConfig::XKBCOMMON
        PkgConfig::XCBCOMMON
        PkgConfig::DRM
        PkgConfig::EGL
        PkgConfig::GLESv2
        PkgConfig::PIXMAN
        PkgConfig::UDEV
        pthread
        m
)

# ============================================================
# 5. 编译选项
# ============================================================
# _GNU_SOURCE: 通过编译选项定义，避免和源码冲突
target_compile_definitions(hwl PRIVATE _GNU_SOURCE)

# wlroots 0.19 需要的宏
target_compile_definitions(hwl PRIVATE
        WLR_USE_UNSTABLE
        HAVE_XWAYLAND
)

# 警告选项
target_compile_options(hwl PRIVATE
        -Wall -Wextra -Wno-unused-parameter
)

# ============================================================
# 6. 安装（可选）
# ============================================================
install(TARGETS hwl RUNTIME DESTINATION bin)
