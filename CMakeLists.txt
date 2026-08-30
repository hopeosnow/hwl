cmake_minimum_required(VERSION 3.20)
project(hwl C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ============================================================
# 1. 查找依赖 (pkg-config)
# ============================================================
find_package(PkgConfig REQUIRED)

pkg_check_modules(WLROOTS REQUIRED IMPORTED_TARGET wlroots-0.19)
pkg_check_modules(WAYLAND REQUIRED IMPORTED_TARGET wayland-server)
pkg_check_modules(XKBCOMMON REQUIRED IMPORTED_TARGET xkbcommon)
pkg_check_modules(XCBCOMMON REQUIRED IMPORTED_TARGET xcb)
pkg_check_modules(DRM REQUIRED IMPORTED_TARGET libdrm)
pkg_check_modules(EGL REQUIRED IMPORTED_TARGET egl)
pkg_check_modules(GLESv2 REQUIRED IMPORTED_TARGET glesv2)
pkg_check_modules(PIXMAN REQUIRED IMPORTED_TARGET pixman-1)
pkg_check_modules(UDEV REQUIRED IMPORTED_TARGET libudev)

# wayland-scanner 可执行文件
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
    message(FATAL_ERROR "xdg-shell.xml not found at ${XDG_SHELL_XML}\n"
            "Install wayland-protocols: sudo apt install wayland-protocols")
endif()

set(PROTOCOL_SRC "${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-protocol.c")
set(PROTOCOL_HDR "${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-protocol.h")

# 生成 server-side 头文件
add_custom_command(
        OUTPUT "${PROTOCOL_HDR}"
        COMMAND ${WAYLAND_SCANNER} server-header "${XDG_SHELL_XML}" "${PROTOCOL_HDR}"
        DEPENDS "${XDG_SHELL_XML}"
        COMMENT "Generating xdg-shell-protocol.h"
)

# 生成 server-side C 代码
add_custom_command(
        OUTPUT "${PROTOCOL_SRC}"
        COMMAND ${WAYLAND_SCANNER} public-code "${XDG_SHELL_XML}" "${PROTOCOL_SRC}"
        DEPENDS "${XDG_SHELL_XML}"
        COMMENT "Generating xdg-shell-protocol.c"
)

# ============================================================
# 3. 生成 wayland 协议文件 (text-input-unstable-v3)
# ============================================================
set(TEXT_INPUT_V3_XML "${WL_PROT_DIR}/unstable/text-input/text-input-unstable-v3.xml")
if(NOT EXISTS "${TEXT_INPUT_V3_XML}")
    message(FATAL_ERROR "text-input-unstable-v3.xml not found at ${TEXT_INPUT_V3_XML}\n"
            "请安装 wayland-protocols: sudo apt install wayland-protocols")
endif()

set(TEXT_INPUT_V3_SRC "${CMAKE_CURRENT_BINARY_DIR}/text-input-unstable-v3-protocol.c")
set(TEXT_INPUT_V3_HDR "${CMAKE_CURRENT_BINARY_DIR}/text-input-unstable-v3-server-protocol.h")

# 生成 server-side 头文件
add_custom_command(
        OUTPUT "${TEXT_INPUT_V3_HDR}"
        COMMAND ${WAYLAND_SCANNER} server-header "${TEXT_INPUT_V3_XML}" "${TEXT_INPUT_V3_HDR}"
        DEPENDS "${TEXT_INPUT_V3_XML}"
        COMMENT "Generating text-input-unstable-v3-server-protocol.h"
)

# 生成 server-side C 代码
add_custom_command(
        OUTPUT "${TEXT_INPUT_V3_SRC}"
        COMMAND ${WAYLAND_SCANNER} public-code "${TEXT_INPUT_V3_XML}" "${TEXT_INPUT_V3_SRC}"
        DEPENDS "${TEXT_INPUT_V3_XML}"
        COMMENT "Generating text-input-unstable-v3-protocol.c"
)

# ============================================================
# 4. 生成 wayland 协议文件 (input-method-unstable-v2)
# ============================================================
set(INPUT_METHOD_V2_XML "${CMAKE_CURRENT_SOURCE_DIR}/protocols/input-method-unstable-v2.xml")
if(NOT EXISTS "${INPUT_METHOD_V2_XML}")
    message(FATAL_ERROR "input-method-unstable-v2.xml not found at ${INPUT_METHOD_V2_XML}\n"
            "请从 Sway 仓库下载该文件到项目目录的 protocols/ 文件夹中:\n"
            "wget -O protocols/input-method-unstable-v2.xml https://raw.githubusercontent.com/swaywm/wlroots/master/protocol/input-method-unstable-v2.xml")
endif()

set(INPUT_METHOD_V2_SRC "${CMAKE_CURRENT_BINARY_DIR}/input-method-unstable-v2-protocol.c")
set(INPUT_METHOD_V2_HDR "${CMAKE_CURRENT_BINARY_DIR}/input-method-unstable-v2-server-protocol.h")

# 创建一个自定义目标来生成 input-method-unstable-v2 协议文件
add_custom_target(input_method_v2_protocol
        COMMAND ${WAYLAND_SCANNER} server-header "${INPUT_METHOD_V2_XML}" "${INPUT_METHOD_V2_HDR}"
        COMMAND ${WAYLAND_SCANNER} public-code "${INPUT_METHOD_V2_XML}" "${INPUT_METHOD_V2_SRC}"
        DEPENDS "${INPUT_METHOD_V2_XML}"
        BYPRODUCTS "${INPUT_METHOD_V2_HDR}" "${INPUT_METHOD_V2_SRC}"
        COMMENT "Generating input-method-unstable-v2 protocol files"
)

# ============================================================
# 5. 定义可执行文件
# ============================================================
add_executable(hwl
        hwl.c
        "${PROTOCOL_SRC}"
        "${PROTOCOL_HDR}"
        "${TEXT_INPUT_V3_SRC}"
        "${TEXT_INPUT_V3_HDR}"
)

# 让 hwl 目标依赖于 input_method_v2_protocol 这个自定义目标
add_dependencies(hwl input_method_v2_protocol)

# 生成的头文件所在目录加入 include path
target_include_directories(hwl PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

# wlroots 头文件路径
target_include_directories(hwl PRIVATE ${WLROOTS_INCLUDE_DIRS} ${WAYLAND_INCLUDE_DIRS} ${XKBCOMMON_INCLUDE_DIRS} ${DRM_INCLUDE_DIRS} )

# ============================================================
# 6. 链接库
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
        pthread m
)

# ============================================================
# 7. 编译选项
# ============================================================
# _GNU_SOURCE: 通过编译选项定义，避免和源码冲突
target_compile_definitions(hwl PRIVATE _GNU_SOURCE)

# wlroots 0.19 需要的宏
target_compile_definitions(hwl PRIVATE WLR_USE_UNSTABLE HAVE_XWAYLAND )

# 警告选项
target_compile_options(hwl PRIVATE -Wall -Wextra -Wno-unused-parameter )

# ============================================================
# 8. 安装（可选）
# ============================================================
install(TARGETS hwl RUNTIME DESTINATION bin)