/*
 Hydronos desktop enviroment
 hydron wayland (hwl) Project
 version code:1.0.0
 windows manager and wayland compositer for hde
 hydronos
 Fuzhou 2026
*/
//#define _GNU_SOURCE
#define HDE_DOCK_HEIGHT 70  /* hde-dock 底部预留高度（80px dock - 10px 上移） */
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>
#include <pthread.h>
#include <string.h>
#include <assert.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <wlr/types/wlr_shm.h>
#include <wayland-server-core.h>
#include <wlr/xwayland.h>
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>
#include <xcb/xcb.h>
// 在 #include <xcb/xcb.h> 之后添加
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include <drm_fourcc.h>
enum hwl_cursor_mode
{
	HWL_CURSOR_PASSTHROUGH,
	HWL_CURSOR_MOVE,
	HWL_CURSOR_RESIZE,
};

struct hwl_server
{
	pthread_mutex_t toplevels_mutex;
	struct wl_display *wl_display;
	struct wlr_backend *backend;
	struct wlr_compositor *compositor;
	struct wlr_renderer *renderer;
	struct wlr_allocator *allocator;
	struct wlr_scene *scene;
	struct wlr_scene_output_layout *scene_layout;
	struct wlr_xwayland *xwayland;
	struct wl_listener xwayland_new_surface;
	struct wl_list xwayland_surfaces;   // 用于存储 XWayland 窗口
	struct wlr_xdg_shell *xdg_shell;
	struct wl_listener new_xdg_toplevel;
	struct wl_listener new_xdg_popup;
	struct wl_list toplevels;

	struct wlr_cursor *cursor;
	struct wlr_xcursor_manager *cursor_mgr;
	struct wl_listener cursor_motion;
	struct wl_listener cursor_motion_absolute;
	struct wl_listener cursor_button;
	struct wl_listener cursor_axis;
	struct wl_listener cursor_frame;

	struct wlr_seat *seat;
	struct wl_listener new_input;
	struct wl_listener request_cursor;
	struct wl_listener pointer_focus_change;
	struct wl_listener request_set_selection;
	struct wl_list keyboards;
	enum hwl_cursor_mode cursor_mode;
	struct hwl_toplevel *grabbed_toplevel;
	double grab_x, grab_y;
	struct wlr_box grab_geobox;
	uint32_t resize_edges;

	struct wlr_output_layout *output_layout;
	struct wl_list outputs;
	struct wl_listener new_output;
	struct wl_listener xwayland_ready;
	char *startup_cmd;     // 启动命令（XWayland ready 后才执行）
	bool startup_spawned;  // 防止重复 spawn

	struct wlr_scene_tree *background_tree;
    struct wlr_buffer *wallpaper_buffer;
};
struct hwl_xwayland_surface {
    struct wl_list link;
    struct hwl_server *server;
    struct wlr_xwayland_surface *xwayland_surface;
    struct wlr_scene_tree *scene_tree;
    bool mapped;  // 跟踪映射状态，防止重复操作
    bool title_notified;  // 防止重复通知 HDE
    bool minimized;
    bool maximized;
    struct wlr_box saved_geo;  // 保存原始位置尺寸，用于还原
    struct wl_listener associate;
    struct wl_listener dissociate;
    struct wl_listener destroy;
    struct wl_listener set_title;
    struct wl_listener set_class;
    struct wl_listener request_configure;
    struct wl_listener request_move;
    struct wl_listener request_resize;
    struct wl_listener request_minimize;
    struct wl_listener request_maximize;
    struct wl_listener request_activate;
    bool destroyed;
};
struct hwl_output
{
	struct wl_list link;
	struct hwl_server *server;
	struct wlr_output *wlr_output;
	struct wl_listener frame;
	struct wl_listener request_state;
	struct wl_listener destroy;
	struct wlr_scene_buffer *bg_buffer;
};

struct hwl_toplevel
{
	struct wl_list link;
	struct hwl_server *server;
	struct wlr_xdg_toplevel *xdg_toplevel;
	struct wlr_scene_tree *scene_tree;
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener commit;
	struct wl_listener destroy;
	struct wl_listener request_move;
	struct wl_listener request_resize;
	struct wl_listener request_maximize;
	struct wl_listener request_fullscreen;
	struct wl_listener request_minimize;
	bool destroyed;
	bool minimized;
	bool maximized;
	bool fullscreen;
	bool is_desktop;
	struct wlr_box saved_geo;  // 保存窗口原始位置尺寸，用于还原
};

struct hwl_popup
{
	struct wlr_xdg_popup *xdg_popup;
	struct wl_listener commit;
	struct wl_listener destroy;
	struct wlr_box output_box;
	struct hwl_server *server;

};

struct hwl_keyboard
{
	struct wl_list link;
	struct hwl_server *server;
	struct wlr_keyboard *wlr_keyboard;
	struct wl_listener modifiers;
	struct wl_listener key;
	struct wl_listener destroy;
};
//------------xwayland--------------
// 声明 XWayland 回调函数
static void xwayland_surface_associate(struct wl_listener *listener, void *data);
static void xwayland_surface_dissociate(struct wl_listener *listener, void *data);
static void xwayland_surface_destroy(struct wl_listener *listener, void *data);
static void xwayland_surface_set_title(struct wl_listener *listener, void *data);
static void xwayland_surface_set_class(struct wl_listener *listener, void *data);
static void xwayland_surface_request_activate(struct wl_listener *listener, void *data);
static void xwayland_surface_request_move(struct wl_listener *listener, void *data);
static void xwayland_surface_request_resize(struct wl_listener *listener, void *data);
static void xwayland_surface_request_maximize(struct wl_listener *listener, void *data);
static void xwayland_surface_request_minimize(struct wl_listener *listener, void *data);
static void xwayland_surface_request_configure(struct wl_listener *listener, void *data);
static void focus_xwayland_surface(struct hwl_xwayland_surface *hwl_xsurface);

// XDG toplevel 前置声明（handle_keybinding 中引用）
static void xdg_toplevel_request_maximize(struct wl_listener *listener, void *data);
static void xdg_toplevel_minimize(struct hwl_toplevel *toplevel, bool minimized);

// IPC 广播函数前向声明（定义在 ipc_thread_func 附近）
static void ipc_notify_window_opened(struct hwl_server *server, uintptr_t id, const char *title, const char *app, int is_x11);
static void ipc_notify_window_closed(struct hwl_server *server, uintptr_t id);
static void ipc_notify_window_focused(struct hwl_server *server, uintptr_t id, int is_x11);
static void notify_x11_window_opened(struct hwl_server *server, struct hwl_xwayland_surface *hwl_xsurface);
//xwayland
static void xwayland_new_surface(struct wl_listener *listener, void *data) {
    struct hwl_server *server = wl_container_of(listener, server, xwayland_new_surface);
    struct wlr_xwayland_surface *xsurface = data;
    wlr_log(WLR_INFO, "New XWayland surface: title=%s class=%s",
            xsurface->title ? xsurface->title : "(null)",
            xsurface->class ? xsurface->class : "(null)");

    struct hwl_xwayland_surface *surface = calloc(1, sizeof(*surface));
    if (!surface) {
        wlr_log(WLR_ERROR, "failed to allocate hwl_xwayland_surface");
        return;
    }
    surface->server = server;
    surface->xwayland_surface = xsurface;

    // 创建场景树作为容器（延迟添加到 scene，等 map 时再显示）
    surface->scene_tree = wlr_scene_tree_create(&server->scene->tree);
    if (!surface->scene_tree) {
        wlr_log(WLR_ERROR, "failed to create scene tree for XWayland surface");
        free(surface);
        return;
    }
    // 初始隐藏，associate 时再显示
    wlr_scene_node_set_enabled(&surface->scene_tree->node, false);
    surface->scene_tree->node.data = surface;

    // 监听所有必要事件
    surface->associate.notify = xwayland_surface_associate;
    wl_signal_add(&xsurface->events.associate, &surface->associate);

    surface->dissociate.notify = xwayland_surface_dissociate;
    wl_signal_add(&xsurface->events.dissociate, &surface->dissociate);

    surface->destroy.notify = xwayland_surface_destroy;
    wl_signal_add(&xsurface->events.destroy, &surface->destroy);

    surface->set_title.notify = xwayland_surface_set_title;
    wl_signal_add(&xsurface->events.set_title, &surface->set_title);

    surface->set_class.notify = xwayland_surface_set_class;
    wl_signal_add(&xsurface->events.set_class, &surface->set_class);

    surface->request_activate.notify = xwayland_surface_request_activate;
    wl_signal_add(&xsurface->events.request_activate, &surface->request_activate);

    surface->request_move.notify = xwayland_surface_request_move;
    wl_signal_add(&xsurface->events.request_move, &surface->request_move);

    surface->request_resize.notify = xwayland_surface_request_resize;
    wl_signal_add(&xsurface->events.request_resize, &surface->request_resize);

    surface->request_configure.notify = xwayland_surface_request_configure;
    wl_signal_add(&xsurface->events.request_configure, &surface->request_configure);

    surface->request_maximize.notify = xwayland_surface_request_maximize;
    wl_signal_add(&xsurface->events.request_maximize, &surface->request_maximize);

    surface->request_minimize.notify = xwayland_surface_request_minimize;
    wl_signal_add(&xsurface->events.request_minimize, &surface->request_minimize);

    wl_list_insert(&server->xwayland_surfaces, &surface->link);
    wlr_log(WLR_INFO, "XWayland surface registered: %p", (void *)surface);
}
static void xwayland_ready(struct wl_listener *listener, void *data) {
    struct hwl_server *server = wl_container_of(listener, server, xwayland_ready);
    wlr_log(WLR_INFO, "XWayland is ready!");
    wlr_xwayland_set_seat(server->xwayland, server->seat);
    server->xwayland_new_surface.notify = xwayland_new_surface;
    wl_signal_add(&server->xwayland->events.new_surface, &server->xwayland_new_surface);

    // 关键：根据 wlroots 文档，合成器必须自己设置 DISPLAY 环境变量
    // display_name 在 events.ready 触发后才有效
    if (server->xwayland->display_name) {
        setenv("DISPLAY", server->xwayland->display_name, 1);
        wlr_log(WLR_INFO, "DISPLAY set to: %s", server->xwayland->display_name);
    } else {
        wlr_log(WLR_ERROR, "XWayland display_name is NULL");
    }

    wlr_log(WLR_INFO, "XWayland setup complete. Ready for X11 clients.");

    // XWayland 完全就绪后才启动 startup_cmd — 此时 DISPLAY 已设置
    if (!server->startup_spawned && server->startup_cmd) {
        server->startup_spawned = true;
        pid_t pid = fork();
        if (pid == 0) {
            // 子进程：先 export DISPLAY 再执行命令
            // 强制使用新的 display name，覆盖父进程可能传入的旧 DISPLAY
            const char *new_display = server->xwayland->display_name;
            if (new_display) {
                setenv("DISPLAY", new_display, 1);
            }
            // 用 sh -c 包装 export DISPLAY; cmd
            char wrapped_cmd[4096];
            if (new_display) {
                snprintf(wrapped_cmd, sizeof(wrapped_cmd),
                         "export DISPLAY=%s; %s", new_display, server->startup_cmd);
            } else {
                snprintf(wrapped_cmd, sizeof(wrapped_cmd), "%s", server->startup_cmd);
            }
            execl("/bin/sh", "/bin/sh", "-c", wrapped_cmd, (void *)NULL);
            _exit(127);
        } else if (pid > 0) {
            wlr_log(WLR_INFO, "startup_cmd spawned (pid=%d) with DISPLAY=%s: %s",
                    pid, server->xwayland->display_name ? server->xwayland->display_name : "(null)",
                    server->startup_cmd);
        } else {
            wlr_log(WLR_ERROR, "failed to fork startup_cmd");
            server->startup_spawned = false;
        }
    }
}

/* ========== XWayland surface 事件回调 ========== */

// associate: wl_surface 绑定到 xsurface — 此时 xsurface->surface 可用
// 在这里创建 scene surface（唯一创建点），启用显示，并设置位置偏移避免重叠
static void xwayland_surface_associate(struct wl_listener *listener, void *data) {
    struct hwl_xwayland_surface *surface = wl_container_of(listener, surface, associate);
    struct wlr_xwayland_surface *xsurface = surface->xwayland_surface;
    struct hwl_server *server = surface->server;

    // 防止重复关联
    if (surface->mapped) {
        return;
    }

    if (!xsurface->surface) {
        wlr_log(WLR_ERROR, "XWayland associate but xsurface->surface is NULL");
        return;
    }

    // 创建 scene surface — 唯一创建点
    struct wlr_scene_surface *scene_surface =
        wlr_scene_surface_create(surface->scene_tree, xsurface->surface);

    if (!scene_surface) {
        wlr_log(WLR_ERROR, "failed to create scene surface for XWayland: %s",
                xsurface->title ? xsurface->title : "(null)");
        return;
    }

    // 计算位置：使用 xsurface 的 x,y，但添加基于窗口数量的偏移防止重叠
    int offset = 0;
    int count = 0;
    struct hwl_xwayland_surface *other;
    wl_list_for_each(other, &server->xwayland_surfaces, link) {
        if (other != surface && other->mapped) {
            count++;
        }
    }
    offset = count * 30;  // 每个已存在的 X11 窗口偏移 30px

    surface->mapped = true;

    // KDE 做法：保证最小尺寸，不要让 X11 客户端以 1x1 运行
    // xterm 等客户端初始创建 1x1 窗口，然后发 ConfigureRequest 期望 WM 给合理尺寸
    int cfg_w = xsurface->width > 100 ? xsurface->width : 600;
    int cfg_h = xsurface->height > 100 ? xsurface->height : 400;
    int cfg_x = xsurface->x + offset;
    int cfg_y = xsurface->y + offset;

    wlr_scene_node_set_position(&surface->scene_tree->node, cfg_x, cfg_y);
    wlr_scene_node_set_enabled(&surface->scene_tree->node, true);

    wlr_log(WLR_INFO, "XWayland surface associated+mapped: %s pos=(%d,%d) %dx%d (client had %dx%d) offset=%d",
            xsurface->title ? xsurface->title : "(null)",
            cfg_x, cfg_y, cfg_w, cfg_h,
            xsurface->width, xsurface->height, offset);

    // KDE 做法：先 configure（给客户端合理尺寸），然后 activate
    wlr_xwayland_surface_configure(xsurface, cfg_x, cfg_y, cfg_w, cfg_h);

    // 只做 X11 activate（安全），不做 wlr_seat_keyboard_notify_enter
    // 后者从 associate 回调链内调用会造成 wlroots reentrancy → Broken pipe
    // keyboard enter 会在用户首次点击窗口时通过 server_cursor_button 触发
    wlr_xwayland_surface_activate(xsurface, true);
    wlr_scene_node_raise_to_top(&surface->scene_tree->node);

    wlr_log(WLR_INFO, "XWayland surface activated: %s (kb enter deferred to first click)",
            xsurface->title ? xsurface->title : "(null)");

    // 如果 title 已经设置（在 associate 之前 set_title 就触发了），立即通知 HDE
    if (xsurface->title && xsurface->title[0] && !surface->title_notified) {
        surface->title_notified = true;
        notify_x11_window_opened(server, surface);
    }
}

// dissociate: wl_surface 与 xsurface 解绑 — 清理映射状态
// 注意: 此回调后 xsurface->surface 为 NULL，不可再访问！
static void xwayland_surface_dissociate(struct wl_listener *listener, void *data) {
    struct hwl_xwayland_surface *surface = wl_container_of(listener, surface, dissociate);
    const char *title = "(unknown)";
    if (surface->xwayland_surface && !surface->destroyed) {
        title = surface->xwayland_surface->title ?
                surface->xwayland_surface->title : "(null)";
    }
    wlr_log(WLR_INFO, "XWayland surface dissociated: %s mapped=%d", title, surface->mapped);
    if (surface->mapped) {
        surface->mapped = false;
        if (surface->scene_tree) {
            wlr_scene_node_set_enabled(&surface->scene_tree->node, false);
        }
    }
}

// destroy: 清理所有资源（带防重复保护）
static void xwayland_surface_destroy(struct wl_listener *listener, void *data) {
    struct hwl_xwayland_surface *surface = wl_container_of(listener, surface, destroy);

    // 防止重复调用
    if (surface->destroyed) {
        wlr_log(WLR_ERROR, "XWayland surface destroy called twice, skipping");
        return;
    }

    // 通知 HDE 窗口关闭（暂时跳过调试）
    // if (surface->mapped && surface->scene_tree) {
    //     uintptr_t id = (uintptr_t)&surface->scene_tree->node;
    //     ipc_notify_window_closed(surface->server, id);
    // }

    surface->destroyed = true;
    surface->mapped = false;

    // 清理所有信号监听器
    wl_list_remove(&surface->associate.link);
    wl_list_remove(&surface->dissociate.link);
    wl_list_remove(&surface->destroy.link);
    wl_list_remove(&surface->set_title.link);
    wl_list_remove(&surface->set_class.link);
    wl_list_remove(&surface->request_activate.link);
    wl_list_remove(&surface->request_move.link);
    wl_list_remove(&surface->request_resize.link);
    wl_list_remove(&surface->request_configure.link);
    wl_list_remove(&surface->request_maximize.link);
    wl_list_remove(&surface->request_minimize.link);
    wl_list_remove(&surface->link);

    // 销毁场景树
    if (surface->scene_tree) {
        wlr_scene_node_destroy(&surface->scene_tree->node);
        surface->scene_tree = NULL;
    }

    free(surface);
    wlr_log(WLR_INFO, "XWayland surface destroyed successfully");
}

/* ========== XWayland 焦点管理辅助函数 ========== */

// 安全地激活一个 XWayland 表面（自动取消之前焦点）
static void focus_xwayland_surface(struct hwl_xwayland_surface *hwl_xsurface) {
    if (!hwl_xsurface || hwl_xsurface->destroyed) {
        return;
    }
    struct wlr_xwayland_surface *xsurface = hwl_xsurface->xwayland_surface;
    struct hwl_server *server = hwl_xsurface->server;

    if (!xsurface || !xsurface->surface) {
        wlr_log(WLR_ERROR, "focus_xwayland_surface: xsurface or surface is NULL");
        return;
    }

    // 1. 取消之前焦点的 XDG toplevel
    struct wlr_surface *prev = server->seat->keyboard_state.focused_surface;
    if (prev && prev != xsurface->surface) {
        struct wlr_xdg_toplevel *prev_toplevel =
            wlr_xdg_toplevel_try_from_wlr_surface(prev);
        if (prev_toplevel) {
            wlr_xdg_toplevel_set_activated(prev_toplevel, false);
        }
        // 2. 安全地遍历列表，取消之前焦点的 XWayland surface
        struct hwl_xwayland_surface *pxs;
        wl_list_for_each(pxs, &server->xwayland_surfaces, link) {
            // 双重检查：destroyed 标志 + xsurface->surface 非空 + 匹配 prev
            if (pxs->destroyed || pxs == hwl_xsurface) {
                continue;
            }
            if (pxs->xwayland_surface &&
                pxs->xwayland_surface->surface &&
                pxs->xwayland_surface->surface == prev) {
                wlr_xwayland_surface_activate(pxs->xwayland_surface, false);
                break;  // 只取消前一个
            }
        }
    }

    // 3. 激活新的 XWayland surface
    wlr_xwayland_surface_activate(xsurface, true);

    // 4. 提升到顶层并确保可见
    if (hwl_xsurface->mapped && hwl_xsurface->scene_tree) {
        wlr_scene_node_raise_to_top(&hwl_xsurface->scene_tree->node);
        wlr_scene_node_set_enabled(&hwl_xsurface->scene_tree->node, true);
    }

    // 5. 发送键盘焦点
    struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat);
    if (keyboard) {
        wlr_seat_keyboard_notify_enter(server->seat,
            xsurface->surface,
            keyboard->keycodes, keyboard->num_keycodes,
            &keyboard->modifiers);
    }

    wlr_log(WLR_INFO, "XWayland surface focused: %s",
            xsurface->title ? xsurface->title : "(null)");

    // 通知 HDE 窗口获得焦点（暂时跳过调试）
    // if (hwl_xsurface->scene_tree) {
    //     uintptr_t id = (uintptr_t)&hwl_xsurface->scene_tree->node;
    //     ipc_notify_window_focused(hwl_xsurface->server, id, 1);
    // }
}

/* set_title: X11 客户端设置了窗口标题（WM_NAME 属性变更） */
static void xwayland_surface_set_title(struct wl_listener *listener, void *data) {
    struct hwl_xwayland_surface *surface = wl_container_of(listener, surface, set_title);
    struct wlr_xwayland_surface *xsurface = surface->xwayland_surface;

    if (surface->destroyed) return;

    wlr_log(WLR_INFO, "XWayland set_title: %s (mapped=%d)",
            xsurface->title ? xsurface->title : "(null)", surface->mapped);

    // 如果已 mapped 且尚未通知 HDE，现在补通知（带上真正的标题）
    if (surface->mapped && !surface->title_notified) {
        surface->title_notified = true;
        notify_x11_window_opened(surface->server, surface);
    }
}

/* set_class: X11 客户端设置了窗口类名（WM_CLASS 属性变更） */
static void xwayland_surface_set_class(struct wl_listener *listener, void *data) {
    struct hwl_xwayland_surface *surface = wl_container_of(listener, surface, set_class);
    struct wlr_xwayland_surface *xsurface = surface->xwayland_surface;

    if (surface->destroyed) return;

    wlr_log(WLR_INFO, "XWayland set_class: %s / %s (mapped=%d)",
            xsurface->class ? xsurface->class : "(null)",
            xsurface->instance ? xsurface->instance : "(null)",
            surface->mapped);
}

static void xwayland_surface_request_activate(struct wl_listener *listener, void *data) {
    struct hwl_xwayland_surface *surface =
        wl_container_of(listener, surface, request_activate);

    if (surface->destroyed) {
        return;
    }

    wlr_log(WLR_INFO, "XWayland surface requested activate: %s",
            surface->xwayland_surface->title ?
            surface->xwayland_surface->title : "(null)");

    // 使用统一的焦点函数
    focus_xwayland_surface(surface);
}

static void xwayland_surface_request_move(struct wl_listener *listener, void *data) {
    // 暂不实现
}

/* request_resize: X11 客户端请求交互式调整大小 */
static void xwayland_surface_request_resize(struct wl_listener *listener, void *data) {
    struct hwl_xwayland_surface *surface =
        wl_container_of(listener, surface, request_resize);
    struct wlr_xwayland_resize_event *event = data;
    struct wlr_xwayland_surface *xsurface = surface->xwayland_surface;

    if (surface->destroyed || !surface->mapped) return;

    wlr_log(WLR_INFO, "XWayland request_resize: %s edges=%d size=%dx%d",
            xsurface->title ? xsurface->title : "(null)",
            event->edges, xsurface->width, xsurface->height);

    // resize_event 只有 edges，用当前尺寸 re-confirm
    wlr_xwayland_surface_configure(xsurface,
        xsurface->x, xsurface->y,
        xsurface->width, xsurface->height);
}

/* request_configure: X11 客户端发出 ConfigureRequest（KDE 做法——必须响应） */
static void xwayland_surface_request_configure(struct wl_listener *listener, void *data) {
    struct hwl_xwayland_surface *surface =
        wl_container_of(listener, surface, request_configure);
    struct wlr_xwayland_surface *xsurface = surface->xwayland_surface;

    if (surface->destroyed || !surface->mapped) return;

    // KDE 做法：X11 客户端通过 ConfigureRequest 告诉 WM 它想要的大小
    // 合成器应该尊重客户端的请求，但保证最小可用尺寸
    int w = xsurface->width > 100 ? xsurface->width : 600;
    int h = xsurface->height > 100 ? xsurface->height : 400;

    wlr_log(WLR_INFO, "XWayland request_configure: %s -> %dx%d+%d+%d",
            xsurface->title ? xsurface->title : "(null)",
            w, h, xsurface->x, xsurface->y);

    // 重新配置窗口位置和大小
    wlr_scene_node_set_position(&surface->scene_tree->node,
        xsurface->x, xsurface->y);
    wlr_xwayland_surface_configure(xsurface,
        xsurface->x, xsurface->y, w, h);
}

/* ========== XWayland 最大化 ========== */
static void xwayland_surface_request_maximize(struct wl_listener *listener, void *data) {
    struct hwl_xwayland_surface *surface =
        wl_container_of(listener, surface, request_maximize);
    struct wlr_xwayland_surface *xsurface = surface->xwayland_surface;
    struct hwl_server *server = surface->server;

    if (surface->destroyed || !surface->mapped) return;

    struct wlr_box output_box;
    wlr_output_layout_get_box(server->output_layout, NULL, &output_box);
    int max_h = output_box.height - HDE_DOCK_HEIGHT;
    if (max_h < 200) max_h = output_box.height;  // 小屏幕保护

    if (!surface->maximized) {
        // 保存当前几何信息
        surface->saved_geo.x = xsurface->x;
        surface->saved_geo.y = xsurface->y;
        surface->saved_geo.width = xsurface->width;
        surface->saved_geo.height = xsurface->height;
        surface->maximized = true;

        // 铺满输出但底部预留 dock 空间
        wlr_scene_node_set_position(&surface->scene_tree->node, 0, 0);
        wlr_xwayland_surface_configure(xsurface, 0, 0,
            output_box.width, max_h);
        wlr_log(WLR_INFO, "XWayland maximized: %s -> %dx%d (dock=%d)",
                xsurface->title ? xsurface->title : "(null)",
                output_box.width, max_h, HDE_DOCK_HEIGHT);
    } else {
        // 还原
        surface->maximized = false;
        wlr_scene_node_set_position(&surface->scene_tree->node,
            surface->saved_geo.x, surface->saved_geo.y);
        wlr_xwayland_surface_configure(xsurface,
            surface->saved_geo.x, surface->saved_geo.y,
            surface->saved_geo.width, surface->saved_geo.height);
        wlr_log(WLR_INFO, "XWayland unmaximized: %s -> %dx%d+%d+%d",
                xsurface->title ? xsurface->title : "(null)",
                surface->saved_geo.width, surface->saved_geo.height,
                surface->saved_geo.x, surface->saved_geo.y);
    }
}

/* ========== XWayland 最小化 ========== */
static void xwayland_surface_request_minimize(struct wl_listener *listener, void *data) {
    struct hwl_xwayland_surface *surface =
        wl_container_of(listener, surface, request_minimize);
    struct wlr_xwayland_surface *xsurface = surface->xwayland_surface;

    if (surface->destroyed || !surface->mapped) return;

    if (!surface->minimized) {
        surface->minimized = true;
        // 保存位置
        surface->saved_geo.x = xsurface->x;
        surface->saved_geo.y = xsurface->y;
        wlr_scene_node_set_enabled(&surface->scene_tree->node, false);
        wlr_xwayland_surface_set_minimized(xsurface, true);
        wlr_log(WLR_INFO, "XWayland minimized: %s",
                xsurface->title ? xsurface->title : "(null)");
    }
}

/* 从外部恢复 XWayland 最小化窗口（IPC focus 时调用） */
static void xwayland_surface_unminimize(struct hwl_xwayland_surface *surface) {
    if (!surface->minimized) return;
    struct wlr_xwayland_surface *xsurface = surface->xwayland_surface;

    surface->minimized = false;
    wlr_scene_node_set_enabled(&surface->scene_tree->node, true);
    wlr_xwayland_surface_set_minimized(xsurface, false);
    wlr_xwayland_surface_restack(xsurface, NULL, XCB_STACK_MODE_ABOVE);
    wlr_log(WLR_INFO, "XWayland unminimized: %s",
            xsurface->title ? xsurface->title : "(null)");
}
//xwayland
static void focus_toplevel(struct hwl_toplevel *toplevel)
{
	if (toplevel == NULL || toplevel->is_desktop)
		return;
	struct hwl_server *server = toplevel->server;
	struct wlr_seat *seat = server->seat;
	struct wlr_surface *prev_surface = seat->keyboard_state.focused_surface;
	struct wlr_surface *surface = toplevel->xdg_toplevel->base->surface;
	if (prev_surface == surface)
		return;
	if (prev_surface)
	{
		struct wlr_xdg_toplevel *prev_toplevel =
			wlr_xdg_toplevel_try_from_wlr_surface(prev_surface);
		if (prev_toplevel != NULL)
			wlr_xdg_toplevel_set_activated(prev_toplevel, false);
	}
	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
	wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);
	pthread_mutex_lock(&server->toplevels_mutex);
	wl_list_remove(&toplevel->link);
	wl_list_insert(&server->toplevels, &toplevel->link);
	pthread_mutex_unlock(&server->toplevels_mutex);
	wlr_xdg_toplevel_set_activated(toplevel->xdg_toplevel, true);
	if (keyboard != NULL)
	{
		wlr_seat_keyboard_notify_enter(seat, surface,
									   keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
	}

	// 通知 HDE 窗口获得焦点 (XDG)（暂时跳过调试）
	// uintptr_t id = (uintptr_t)surface;
	// ipc_notify_window_focused(server, id, 0);
}

static void keyboard_handle_modifiers(struct wl_listener *listener, void *data)
{
	struct hwl_keyboard *keyboard = wl_container_of(listener, keyboard, modifiers);
	wlr_seat_set_keyboard(keyboard->server->seat, keyboard->wlr_keyboard);
	wlr_seat_keyboard_notify_modifiers(keyboard->server->seat,
									   &keyboard->wlr_keyboard->modifiers);
}

static bool handle_keybinding(struct hwl_server *server, xkb_keysym_t sym)
{
	switch (sym)
	{
	case XKB_KEY_Escape:
		wl_display_terminate(server->wl_display);
		break;
	case XKB_KEY_F1:
		if (wl_list_length(&server->toplevels) < 2)
			break;
		{
			struct hwl_toplevel *next_toplevel =
				wl_container_of(server->toplevels.prev, next_toplevel, link);
			focus_toplevel(next_toplevel);
		}
		break;
	case XKB_KEY_Up:
	case XKB_KEY_KP_Up:
	{
		// Alt+Up: 最大化/还原当前聚焦窗口（已最小化时先恢复）
		struct wlr_surface *focused = server->seat->keyboard_state.focused_surface;
		if (!focused) break;

		// 尝试 XDG toplevel
		struct wlr_xdg_toplevel *xdg_tl =
			wlr_xdg_toplevel_try_from_wlr_surface(focused);
		if (xdg_tl && xdg_tl->base) {
			struct wlr_scene_tree *tree = xdg_tl->base->data;
			if (tree && tree->node.data) {
				struct hwl_toplevel *tl = tree->node.data;
				if (tl->minimized) {
					xdg_toplevel_minimize(tl, false);
					focus_toplevel(tl);
				}
				xdg_toplevel_request_maximize(&tl->request_maximize, NULL);
			}
			break;
		}

		// 尝试 XWayland
		struct wlr_xwayland_surface *xsurface =
			wlr_xwayland_surface_try_from_wlr_surface(focused);
		if (xsurface) {
			struct hwl_xwayland_surface *hx;
			wl_list_for_each(hx, &server->xwayland_surfaces, link) {
				if (!hx->destroyed && hx->xwayland_surface == xsurface) {
					if (hx->minimized) {
						xwayland_surface_unminimize(hx);
						focus_xwayland_surface(hx);
					}
					xwayland_surface_request_maximize(&hx->request_maximize, NULL);
					break;
				}
			}
		}
		break;
	}
	case XKB_KEY_Down:
	case XKB_KEY_KP_Down:
	{
		// Alt+Down: 最小化当前聚焦窗口
		struct wlr_surface *focused = server->seat->keyboard_state.focused_surface;
		if (!focused) break;

		// 尝试 XDG toplevel
		struct wlr_xdg_toplevel *xdg_tl =
			wlr_xdg_toplevel_try_from_wlr_surface(focused);
		if (xdg_tl && xdg_tl->base) {
			struct wlr_scene_tree *tree = xdg_tl->base->data;
			if (tree && tree->node.data) {
				struct hwl_toplevel *tl = tree->node.data;
				xdg_toplevel_minimize(tl, true);
			}
			break;
		}

		// 尝试 XWayland
		struct wlr_xwayland_surface *xsurface =
			wlr_xwayland_surface_try_from_wlr_surface(focused);
		if (xsurface) {
			struct hwl_xwayland_surface *hx;
			wl_list_for_each(hx, &server->xwayland_surfaces, link) {
				if (!hx->destroyed && hx->xwayland_surface == xsurface) {
					xwayland_surface_request_minimize(&hx->request_minimize, NULL);
					break;
				}
			}
		}
		break;
	}
	default:
		return false;
	}
	return true;
}

static void keyboard_handle_key(struct wl_listener *listener, void *data)
{
	struct hwl_keyboard *keyboard = wl_container_of(listener, keyboard, key);
	struct hwl_server *server = keyboard->server;
	struct wlr_keyboard_key_event *event = data;
	struct wlr_seat *seat = server->seat;

	uint32_t keycode = event->keycode + 8;
	const xkb_keysym_t *syms;
	int nsyms = xkb_state_key_get_syms(
		keyboard->wlr_keyboard->xkb_state, keycode, &syms);

	bool handled = false;
	uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard->wlr_keyboard);
	if ((modifiers & WLR_MODIFIER_ALT) &&
		event->state == WL_KEYBOARD_KEY_STATE_PRESSED)
	{
		for (int i = 0; i < nsyms; i++)
			handled = handle_keybinding(server, syms[i]);
	}

	if (!handled)
	{
		wlr_seat_set_keyboard(seat, keyboard->wlr_keyboard);
		wlr_seat_keyboard_notify_key(seat, event->time_msec,
									 event->keycode, event->state);
	}
}

static void keyboard_handle_destroy(struct wl_listener *listener, void *data)
{
	struct hwl_keyboard *keyboard = wl_container_of(listener, keyboard, destroy);
	wl_list_remove(&keyboard->modifiers.link);
	wl_list_remove(&keyboard->key.link);
	wl_list_remove(&keyboard->destroy.link);
	wl_list_remove(&keyboard->link);
	free(keyboard);
}

static void server_new_keyboard(struct hwl_server *server,
								struct wlr_input_device *device)
{
	struct wlr_keyboard *wlr_keyboard = wlr_keyboard_from_input_device(device);
	struct hwl_keyboard *keyboard = calloc(1, sizeof(*keyboard));
	keyboard->server = server;
	keyboard->wlr_keyboard = wlr_keyboard;

	struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	struct xkb_keymap *keymap = xkb_keymap_new_from_names(context, NULL,
														  XKB_KEYMAP_COMPILE_NO_FLAGS);
	wlr_keyboard_set_keymap(wlr_keyboard, keymap);
	xkb_keymap_unref(keymap);
	xkb_context_unref(context);
	wlr_keyboard_set_repeat_info(wlr_keyboard, 25, 600);

	keyboard->modifiers.notify = keyboard_handle_modifiers;
	wl_signal_add(&wlr_keyboard->events.modifiers, &keyboard->modifiers);
	keyboard->key.notify = keyboard_handle_key;
	wl_signal_add(&wlr_keyboard->events.key, &keyboard->key);
	keyboard->destroy.notify = keyboard_handle_destroy;
	wl_signal_add(&device->events.destroy, &keyboard->destroy);

	wlr_seat_set_keyboard(server->seat, keyboard->wlr_keyboard);
	wl_list_insert(&server->keyboards, &keyboard->link);
}

static void server_new_pointer(struct hwl_server *server,
							   struct wlr_input_device *device)
{
	wlr_cursor_attach_input_device(server->cursor, device);
}

static void server_new_input(struct wl_listener *listener, void *data)
{
	struct hwl_server *server = wl_container_of(listener, server, new_input);
	struct wlr_input_device *device = data;
	switch (device->type)
	{
	case WLR_INPUT_DEVICE_KEYBOARD:
		server_new_keyboard(server, device);
		break;
	case WLR_INPUT_DEVICE_POINTER:
		server_new_pointer(server, device);
		break;
	default:
		break;
	}
	uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
	if (!wl_list_empty(&server->keyboards))
		caps |= WL_SEAT_CAPABILITY_KEYBOARD;
	wlr_seat_set_capabilities(server->seat, caps);
}

static void seat_request_cursor(struct wl_listener *listener, void *data)
{
	struct hwl_server *server = wl_container_of(listener, server, request_cursor);
	struct wlr_seat_pointer_request_set_cursor_event *event = data;
	struct wlr_seat_client *focused_client =
		server->seat->pointer_state.focused_client;
	if (focused_client == event->seat_client)
	{
		wlr_cursor_set_surface(server->cursor, event->surface,
							   event->hotspot_x, event->hotspot_y);
	}
}

static void seat_pointer_focus_change(struct wl_listener *listener, void *data)
{
	struct hwl_server *server = wl_container_of(listener, server, pointer_focus_change);
	struct wlr_seat_pointer_focus_change_event *event = data;
	if (event->new_surface == NULL)
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
}

static void seat_request_set_selection(struct wl_listener *listener, void *data)
{
	struct hwl_server *server = wl_container_of(listener, server, request_set_selection);
	struct wlr_seat_request_set_selection_event *event = data;
	wlr_seat_set_selection(server->seat, event->source, event->serial);
}

static void reset_cursor_mode(struct hwl_server *server)
{
	server->cursor_mode = HWL_CURSOR_PASSTHROUGH;
	server->grabbed_toplevel = NULL;
}

static void process_cursor_move(struct hwl_server *server)
{
	struct hwl_toplevel *toplevel = server->grabbed_toplevel;
	wlr_scene_node_set_position(&toplevel->scene_tree->node,
								server->cursor->x - server->grab_x,
								server->cursor->y - server->grab_y);
}

static void process_cursor_resize(struct hwl_server *server)
{
	struct hwl_toplevel *toplevel = server->grabbed_toplevel;
	double border_x = server->cursor->x - server->grab_x;
	double border_y = server->cursor->y - server->grab_y;
	int new_left = server->grab_geobox.x;
	int new_right = server->grab_geobox.x + server->grab_geobox.width;
	int new_top = server->grab_geobox.y;
	int new_bottom = server->grab_geobox.y + server->grab_geobox.height;

	if (server->resize_edges & WLR_EDGE_TOP)
	{
		new_top = border_y;
		if (new_top >= new_bottom)
			new_top = new_bottom - 1;
	}
	else if (server->resize_edges & WLR_EDGE_BOTTOM)
	{
		new_bottom = border_y;
		if (new_bottom <= new_top)
			new_bottom = new_top + 1;
	}
	if (server->resize_edges & WLR_EDGE_LEFT)
	{
		new_left = border_x;
		if (new_left >= new_right)
			new_left = new_right - 1;
	}
	else if (server->resize_edges & WLR_EDGE_RIGHT)
	{
		new_right = border_x;
		if (new_right <= new_left)
			new_right = new_left + 1;
	}

	struct wlr_box geo_box = toplevel->xdg_toplevel->base->current.geometry;
	wlr_scene_node_set_position(&toplevel->scene_tree->node,
								new_left - geo_box.x, new_top - geo_box.y);

	int new_width = new_right - new_left;
	int new_height = new_bottom - new_top;
	wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, new_width, new_height);
}

static void process_cursor_motion(struct hwl_server *server, uint32_t time)
{
    // 处理交互式移动/调整大小（仅适用于 XDG toplevel）
    if (server->cursor_mode == HWL_CURSOR_MOVE)
    {
        process_cursor_move(server);
        return;
    }
    else if (server->cursor_mode == HWL_CURSOR_RESIZE)
    {
        process_cursor_resize(server);
        return;
    }

    double sx, sy;
    struct wlr_seat *seat = server->seat;
    struct wlr_surface *surface = NULL;

    // 使用 scene graph 直接查找光标下的表面 — 兼容 XDG 和 XWayland
    struct wlr_scene_node *node = wlr_scene_node_at(
        &server->scene->tree.node, server->cursor->x, server->cursor->y, &sx, &sy);

    if (node && node->type == WLR_SCENE_NODE_BUFFER) {
        struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_from_node(node);
        struct wlr_scene_surface *scene_surface =
            wlr_scene_surface_try_from_buffer(scene_buffer);
        if (scene_surface) {
            surface = scene_surface->surface;
        }
    }

    if (surface) {
        wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
        wlr_seat_pointer_notify_motion(seat, time, sx, sy);
    } else {
        wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
        wlr_seat_pointer_clear_focus(seat);
    }
}

static void server_cursor_motion(struct wl_listener *listener, void *data)
{
	struct hwl_server *server = wl_container_of(listener, server, cursor_motion);
	struct wlr_pointer_motion_event *event = data;
	wlr_cursor_move(server->cursor, &event->pointer->base,
					event->delta_x, event->delta_y);
	process_cursor_motion(server, event->time_msec);
}

static void server_cursor_motion_absolute(struct wl_listener *listener, void *data)
{
	struct hwl_server *server = wl_container_of(listener, server, cursor_motion_absolute);
	struct wlr_pointer_motion_absolute_event *event = data;
	wlr_cursor_warp_absolute(server->cursor, &event->pointer->base, event->x, event->y);
	process_cursor_motion(server, event->time_msec);
}

static void server_cursor_button(struct wl_listener *listener, void *data)
{
    struct hwl_server *server = wl_container_of(listener, server, cursor_button);
    struct wlr_pointer_button_event *event = data;
    wlr_seat_pointer_notify_button(server->seat,
        event->time_msec, event->button, event->state);
    if (event->state == WL_POINTER_BUTTON_STATE_RELEASED)
    {
        reset_cursor_mode(server);
    }
    else
    {
        double sx, sy;
        struct wlr_surface *surface = NULL;

        // 直接用 scene graph 查找点击的 surface（兼容 XDG + XWayland）
        struct wlr_scene_node *node = wlr_scene_node_at(
            &server->scene->tree.node, server->cursor->x, server->cursor->y, &sx, &sy);

        if (node && node->type == WLR_SCENE_NODE_BUFFER) {
            struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_from_node(node);
            struct wlr_scene_surface *scene_surface =
                wlr_scene_surface_try_from_buffer(scene_buffer);
            if (scene_surface) {
                surface = scene_surface->surface;
            }
        }

        if (surface) {
            // 优先检查是否为 XDG toplevel
            struct wlr_xdg_surface *xdg_surface =
                wlr_xdg_surface_try_from_wlr_surface(surface);
            if (xdg_surface != NULL &&
                xdg_surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
                // XDG toplevel — 获取 hwl_toplevel 并聚焦
                struct wlr_scene_tree *tree = node->parent;
                while (tree && !tree->node.data)
                    tree = tree->node.parent;
                if (tree && tree->node.data) {
                    struct hwl_toplevel *toplevel = tree->node.data;
                    focus_toplevel(toplevel);
                }
            } else {
                // 尝试作为 XWayland surface
                struct wlr_xwayland_surface *xsurface =
                    wlr_xwayland_surface_try_from_wlr_surface(surface);
                if (xsurface) {
                    // 在列表中查找对应的 hwl 包装
                    struct hwl_xwayland_surface *hwl_xsurface = NULL;
                    struct hwl_xwayland_surface *xs;
                    wl_list_for_each(xs, &server->xwayland_surfaces, link) {
                        if (!xs->destroyed &&
                            xs->xwayland_surface == xsurface) {
                            hwl_xsurface = xs;
                            break;
                        }
                    }
                    if (hwl_xsurface) {
                        focus_xwayland_surface(hwl_xsurface);
                        wlr_log(WLR_INFO, "XWayland window clicked and focused: %s",
                                xsurface->title ? xsurface->title : "(null)");
                    }
                }
            }
        }
    }
}

static void server_cursor_axis(struct wl_listener *listener, void *data)
{
	struct hwl_server *server = wl_container_of(listener, server, cursor_axis);
	struct wlr_pointer_axis_event *event = data;
	wlr_seat_pointer_notify_axis(server->seat,
								 event->time_msec, event->orientation, event->delta,
								 event->delta_discrete, event->source, event->relative_direction);
}

static void server_cursor_frame(struct wl_listener *listener, void *data)
{
	struct hwl_server *server = wl_container_of(listener, server, cursor_frame);
	wlr_seat_pointer_notify_frame(server->seat);
}

static void output_frame(struct wl_listener *listener, void *data)
{
	struct hwl_output *output = wl_container_of(listener, output, frame);
	struct wlr_scene *scene = output->server->scene;
	struct wlr_scene_output *scene_output = wlr_scene_get_scene_output(
		scene, output->wlr_output);
	wlr_scene_output_commit(scene_output, NULL);
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	wlr_scene_output_send_frame_done(scene_output, &now);
}

static void output_request_state(struct wl_listener *listener, void *data)
{
	struct hwl_output *output = wl_container_of(listener, output, request_state);
	const struct wlr_output_event_request_state *event = data;
	wlr_output_commit_state(output->wlr_output, event->state);
}

static void output_destroy(struct wl_listener *listener, void *data)
{
	struct hwl_output *output = wl_container_of(listener, output, destroy);
	if(output->bg_buffer){
		wlr_scene_node_destroy(&output->bg_buffer->node);
		output->bg_buffer = NULL;
	}
	wl_list_remove(&output->frame.link);
	wl_list_remove(&output->frame.link);
	wl_list_remove(&output->request_state.link);
	wl_list_remove(&output->destroy.link);
	wl_list_remove(&output->link);
	free(output);
}

static void server_new_output(struct wl_listener *listener, void *data)
{
	struct hwl_server *server = wl_container_of(listener, server, new_output);
	struct wlr_output *wlr_output = data;
	wlr_output_init_render(wlr_output, server->allocator, server->renderer);
	struct wlr_output_state state;
	wlr_output_state_init(&state);
	wlr_output_state_set_enabled(&state, true);
	struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
	if (mode != NULL)
		wlr_output_state_set_mode(&state, mode);
	wlr_output_commit_state(wlr_output, &state);
	wlr_output_state_finish(&state);

	struct hwl_output *output = calloc(1, sizeof(*output));
	output->wlr_output = wlr_output;
	output->server = server;

	output->frame.notify = output_frame;
	wl_signal_add(&wlr_output->events.frame, &output->frame);
	output->request_state.notify = output_request_state;
	wl_signal_add(&wlr_output->events.request_state, &output->request_state);
	output->destroy.notify = output_destroy;
	wl_signal_add(&wlr_output->events.destroy, &output->destroy);
	wl_list_insert(&server->outputs, &output->link);

	struct wlr_output_layout_output *l_output = wlr_output_layout_add_auto(server->output_layout,
																		   wlr_output);
	struct wlr_scene_output *scene_output = wlr_scene_output_create(server->scene, wlr_output);
	wlr_scene_output_layout_add_output(server->scene_layout, l_output, scene_output);
	//create
	// 创建背景
	if (server->wallpaper_buffer) {
		output->bg_buffer = wlr_scene_buffer_create(server->background_tree, server->wallpaper_buffer);
		if (output->bg_buffer) {
			wlr_scene_buffer_set_dest_size(output->bg_buffer, wlr_output->width, wlr_output->height);
			wlr_scene_node_lower_to_bottom(&output->bg_buffer->node);
		}
	} else {
		// 后备纯色（深灰色）
		float color[4] = {0.1f, 0.1f, 0.1f, 1.0f};
		struct wlr_scene_rect *rect = wlr_scene_rect_create(
			server->background_tree,
			wlr_output->width,
			wlr_output->height,
			color
		);
		if (rect) {
			wlr_scene_node_lower_to_bottom(&rect->node);
		}
}
}

static void xdg_toplevel_map(struct wl_listener *listener, void *data)
{
	struct hwl_toplevel *toplevel = wl_container_of(listener, toplevel, map);
	struct hwl_server *server = toplevel->server;
	pthread_mutex_lock(&server->toplevels_mutex);
	wl_list_insert(&toplevel->server->toplevels, &toplevel->link);
	pthread_mutex_unlock(&server->toplevels_mutex);

	// 通知 HDE 窗口打开 (XDG)（暂时跳过调试）
	// if (toplevel->xdg_toplevel && toplevel->xdg_toplevel->base) {
	// 	uintptr_t id = (uintptr_t)toplevel->xdg_toplevel->base->surface;
	// 	const char *title = toplevel->xdg_toplevel->title ? toplevel->xdg_toplevel->title : " ";
	// 	const char *app = toplevel->xdg_toplevel->app_id ? toplevel->xdg_toplevel->app_id : " ";
	// 	ipc_notify_window_opened(server, id, title, app, 0);
	// }

	focus_toplevel(toplevel);
	//search for hde dsktop
	if (toplevel->xdg_toplevel->title != NULL && strcmp(toplevel->xdg_toplevel->title,"hde-desktop")==0)
	{
		toplevel->is_desktop = true;
		//置底
		if (server->background_tree)
		{
			wlr_scene_node_reparent(&toplevel->scene_tree->node,server->background_tree);
			wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);

		}
		else
		{
			wlr_scene_node_lower_to_bottom(&toplevel->scene_tree->node);
		}
		//鼠标穿透
		/*struct wlr_region *empty_region = wlr_region_create();
		if (empty_region)
		{
			wlr_surface_set_input_region(toplevel->xdg_toplevel->base->surface,empty_region);
			wlr_region_destroy(empty_region);
		}
		return;
		*/
		return;
	}
	/*search for hde dock*/
	if (toplevel->xdg_toplevel->title != NULL && strcmp(toplevel->xdg_toplevel->title, "hde-dock") == 0)
	{
		struct wlr_box layout_box;

		wlr_output_layout_get_box(server->output_layout, NULL, &layout_box);
		// 获取尺寸
		struct wlr_box geo = toplevel->xdg_toplevel->base->current.geometry;
		if (geo.width > 0 && geo.height > 0)
		{
			int x = (layout_box.width - geo.width) / 2;
			int y = (layout_box.height - geo.height);
			// 设置窗口位置
			wlr_scene_node_set_position(&toplevel->scene_tree->node, x, y - 5);
			// 设置顶层
			wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);
		}
		return;
	}
	//其他设置窗口位置
	else{
		struct wlr_box layout_box;
		wlr_output_layout_get_box(server->output_layout, NULL, &layout_box);
		struct wlr_box geo = toplevel->xdg_toplevel->base->current.geometry;
		if (geo.width > 0 && geo.height > 0)
		{
			int x = (layout_box.width - geo.width) / 2;
			int y = (layout_box.height - geo.height);
			// 设置窗口位置
			wlr_scene_node_set_position(&toplevel->scene_tree->node, x, y - 90);
			// 设置顶层
			wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);
		}
	}
}

static void xdg_toplevel_unmap(struct wl_listener *listener, void *data)
{
	struct hwl_toplevel *toplevel = wl_container_of(listener, toplevel, unmap);
	if (toplevel == toplevel->server->grabbed_toplevel)
		reset_cursor_mode(toplevel->server);
	pthread_mutex_lock(&toplevel->server->toplevels_mutex);
	wl_list_remove(&toplevel->link);
	pthread_mutex_unlock(&toplevel->server->toplevels_mutex);

}



static void hwl_toplevel_destroy(struct wl_listener *listener, void *data)
{
	struct hwl_toplevel *toplevel = wl_container_of(listener, toplevel, destroy);

	// 通知 HDE 窗口关闭 (XDG)（暂时跳过调试）
	// if (toplevel->xdg_toplevel && toplevel->xdg_toplevel->base) {
	// 	uintptr_t id = (uintptr_t)toplevel->xdg_toplevel->base->surface;
	// 	ipc_notify_window_closed(toplevel->server, id);
	// }

	toplevel->destroyed = true;
	wl_list_remove(&toplevel->map.link);
	wl_list_remove(&toplevel->unmap.link);
	wl_list_remove(&toplevel->commit.link);
	wl_list_remove(&toplevel->destroy.link);
	wl_list_remove(&toplevel->request_move.link);
	wl_list_remove(&toplevel->request_resize.link);
	wl_list_remove(&toplevel->request_maximize.link);
	wl_list_remove(&toplevel->request_fullscreen.link);
	wl_list_remove(&toplevel->request_minimize.link);
	free(toplevel);
}


static void begin_interactive(struct hwl_toplevel *toplevel,
							  enum hwl_cursor_mode mode, uint32_t edges)
{
	struct hwl_server *server = toplevel->server;
	server->grabbed_toplevel = toplevel;
	server->cursor_mode = mode;

	if (mode == HWL_CURSOR_MOVE)
	{
		server->grab_x = server->cursor->x - toplevel->scene_tree->node.x;
		server->grab_y = server->cursor->y - toplevel->scene_tree->node.y;
	}
	else
	{
		struct wlr_box geo_box = toplevel->xdg_toplevel->base->current.geometry;
		double border_x = (toplevel->scene_tree->node.x + geo_box.x) +
						  ((edges & WLR_EDGE_RIGHT) ? geo_box.width : 0);
		double border_y = (toplevel->scene_tree->node.y + geo_box.y) +
						  ((edges & WLR_EDGE_BOTTOM) ? geo_box.height : 0);
		server->grab_x = server->cursor->x - border_x;
		server->grab_y = server->cursor->y - border_y;
		server->grab_geobox = geo_box;
		server->grab_geobox.x += toplevel->scene_tree->node.x;
		server->grab_geobox.y += toplevel->scene_tree->node.y;
		server->resize_edges = edges;
	}
}

static void xdg_toplevel_request_move(struct wl_listener *listener, void *data)
{
	struct hwl_toplevel *toplevel = wl_container_of(listener, toplevel, request_move);
	begin_interactive(toplevel, HWL_CURSOR_MOVE, 0);
}

static void xdg_toplevel_request_resize(struct wl_listener *listener, void *data)
{
	struct wlr_xdg_toplevel_resize_event *event = data;
	struct hwl_toplevel *toplevel = wl_container_of(listener, toplevel, request_resize);
	begin_interactive(toplevel, HWL_CURSOR_RESIZE, event->edges);
}

static void xdg_toplevel_request_maximize(struct wl_listener *listener, void *data)
{
	struct hwl_toplevel *toplevel = wl_container_of(listener, toplevel, request_maximize);
	struct hwl_server *server = toplevel->server;

	if (!toplevel->xdg_toplevel->base->initialized) return;

	if (!toplevel->maximized) {
		// 保存当前几何信息
		toplevel->saved_geo.x = toplevel->scene_tree->node.x;
		toplevel->saved_geo.y = toplevel->scene_tree->node.y;
		toplevel->saved_geo.width = toplevel->xdg_toplevel->base->current.geometry.width;
		toplevel->saved_geo.height = toplevel->xdg_toplevel->base->current.geometry.height;
		toplevel->maximized = true;

		struct wlr_box output_box;
		wlr_output_layout_get_box(server->output_layout, NULL, &output_box);
		// 最大化时底部预留 dock 空间
		int max_h = output_box.height - HDE_DOCK_HEIGHT-22;
		if (max_h < 200) max_h = output_box.height;  // 小屏幕保护
		wlr_scene_node_set_position(&toplevel->scene_tree->node, 0, 0);
		wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel,
			output_box.width, max_h-5);
		wlr_xdg_toplevel_set_maximized(toplevel->xdg_toplevel, true);
		wlr_log(WLR_INFO, "XDG maximized: %dx%d (dock=%d)",
			output_box.width, max_h, HDE_DOCK_HEIGHT);
	} else {
		// 还原
		toplevel->maximized = false;
		wlr_scene_node_set_position(&toplevel->scene_tree->node,
			toplevel->saved_geo.x, toplevel->saved_geo.y);
		wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel,
			toplevel->saved_geo.width, toplevel->saved_geo.height);
		wlr_xdg_toplevel_set_maximized(toplevel->xdg_toplevel, false);
		wlr_log(WLR_INFO, "XDG unmaximized: %dx%d+%d+%d",
			toplevel->saved_geo.width, toplevel->saved_geo.height,
			toplevel->saved_geo.x, toplevel->saved_geo.y);
	}
}

static void xdg_toplevel_request_fullscreen(struct wl_listener *listener, void *data)
{
	struct hwl_toplevel *toplevel = wl_container_of(listener, toplevel, request_fullscreen);
	struct hwl_server *server = toplevel->server;
	struct wlr_xdg_toplevel *xdg_toplevel = toplevel->xdg_toplevel;

	if (!xdg_toplevel->base->initialized) return;

	if (!toplevel->fullscreen) {
		// 保存当前几何信息
		toplevel->saved_geo.x = toplevel->scene_tree->node.x;
		toplevel->saved_geo.y = toplevel->scene_tree->node.y;
		toplevel->saved_geo.width = xdg_toplevel->base->current.geometry.width;
		toplevel->saved_geo.height = xdg_toplevel->base->current.geometry.height;
		toplevel->fullscreen = true;

		struct wlr_box output_box;
		wlr_output_layout_get_box(server->output_layout, NULL, &output_box);
		wlr_scene_node_set_position(&toplevel->scene_tree->node, 0, 0);
		wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel,
			output_box.width, output_box.height);
		wlr_xdg_toplevel_set_fullscreen(toplevel->xdg_toplevel, true);
		wlr_log(WLR_INFO, "XDG fullscreen: %dx%d",
			output_box.width, output_box.height);
	} else {
		// 退出全屏
		toplevel->fullscreen = false;
		wlr_scene_node_set_position(&toplevel->scene_tree->node,
			toplevel->saved_geo.x, toplevel->saved_geo.y);
		wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel,
			toplevel->saved_geo.width, toplevel->saved_geo.height);
		wlr_xdg_toplevel_set_fullscreen(toplevel->xdg_toplevel, false);
		wlr_log(WLR_INFO, "XDG unfullscreen: %dx%d+%d+%d",
			toplevel->saved_geo.width, toplevel->saved_geo.height,
			toplevel->saved_geo.x, toplevel->saved_geo.y);
	}
}

/* ========== XDG 最小化 ========== */
static void xdg_toplevel_minimize(struct hwl_toplevel *toplevel, bool minimized)
{
	if (!toplevel || toplevel->destroyed) return;

	if (minimized && !toplevel->minimized) {
		toplevel->minimized = true;
		toplevel->saved_geo.x = toplevel->scene_tree->node.x;
		toplevel->saved_geo.y = toplevel->scene_tree->node.y;
		wlr_scene_node_set_enabled(&toplevel->scene_tree->node, false);
		wlr_xdg_toplevel_set_activated(toplevel->xdg_toplevel, false);
		wlr_log(WLR_INFO, "XDG minimized: %s",
			toplevel->xdg_toplevel->title ? toplevel->xdg_toplevel->title : "(null)");
	} else if (!minimized && toplevel->minimized) {
		toplevel->minimized = false;
		wlr_scene_node_set_position(&toplevel->scene_tree->node,
			toplevel->saved_geo.x, toplevel->saved_geo.y);
		wlr_scene_node_set_enabled(&toplevel->scene_tree->node, true);
		wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);
		wlr_log(WLR_INFO, "XDG unminimized: %s",
			toplevel->xdg_toplevel->title ? toplevel->xdg_toplevel->title : "(null)");
	}
}
static void xdg_toplevel_request_minimize(struct wl_listener *listener, void *data) {
    struct hwl_toplevel *toplevel = wl_container_of(listener, toplevel, request_minimize);
    xdg_toplevel_minimize(toplevel, true); // XDG 客户端请求只做最小化
}

// ========== wlroots 0.19 关键修复：处理顶层窗口初始提交 ==========
static void xdg_toplevel_commit(struct wl_listener *listener, void *data) {
    struct hwl_toplevel *toplevel = wl_container_of(listener, toplevel, commit);
    // wlroots 0.19 要求合成器响应 initial_commit 以完成握手
    // 传递 0,0 表示让客户端决定其首选尺寸（对于已设置固定大小的窗口有效）
    if (toplevel->xdg_toplevel->base->initial_commit) {
        wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, 0, 0);
    }
}

// ========== wlroots 0.19 关键修复：处理弹出菜单初始提交 ==========
static void xdg_popup_commit(struct wl_listener *listener, void *data) {
    struct hwl_popup *popup = wl_container_of(listener, popup, commit);

    // 只在第一次 commit（初始化握手）时处理
    if (popup->xdg_popup->base->initial_commit) {
        // 1. 获取屏幕的绝对区域
        struct wlr_box output_box;
        wlr_output_layout_get_box(popup->server->output_layout, NULL, &output_box);

        // 2. 【关键步骤】坐标转换！
        // 获取父窗口 (通常是 Dock)
        struct wlr_xdg_surface *parent = wlr_xdg_surface_try_from_wlr_surface(popup->xdg_popup->parent);

        if (parent && parent->data) {
            struct wlr_scene_tree *parent_tree = parent->data;

            // 核心逻辑：将屏幕绝对坐标减去 Dock 的当前位置
            // 这样就得到了"相对于 Dock 的屏幕边界"
            // Qt 根据此信息才能正确地将菜单显示在 Dock 上方
            output_box.x -= parent_tree->node.x;
            output_box.y -= parent_tree->node.y;
        }

        // 3. 使用转换后的坐标进行约束
        wlr_xdg_popup_unconstrain_from_box(popup->xdg_popup, &output_box);
    }
}

static void server_new_xdg_toplevel(struct wl_listener *listener, void *data) {
    struct hwl_server *server = wl_container_of(listener, server, new_xdg_toplevel);
    struct wlr_xdg_toplevel *xdg_toplevel = data;
    struct hwl_toplevel *toplevel = calloc(1, sizeof(*toplevel));
    toplevel->server = server;
    toplevel->xdg_toplevel = xdg_toplevel;
	toplevel->is_desktop = false;
    toplevel->scene_tree = wlr_scene_xdg_surface_create(&toplevel->server->scene->tree, xdg_toplevel->base);
    toplevel->scene_tree->node.data = toplevel;
    xdg_toplevel->base->data = toplevel->scene_tree;

    toplevel->map.notify = xdg_toplevel_map;
    wl_signal_add(&xdg_toplevel->base->surface->events.map, &toplevel->map);
    toplevel->unmap.notify = xdg_toplevel_unmap;
    wl_signal_add(&xdg_toplevel->base->surface->events.unmap, &toplevel->unmap);

    // 【关键】关联顶层窗口的 commit 处理函数
    toplevel->commit.notify = xdg_toplevel_commit;
    wl_signal_add(&xdg_toplevel->base->surface->events.commit, &toplevel->commit);

    toplevel->destroy.notify = hwl_toplevel_destroy;
    wl_signal_add(&xdg_toplevel->events.destroy, &toplevel->destroy);
    toplevel->request_move.notify = xdg_toplevel_request_move;
    wl_signal_add(&xdg_toplevel->events.request_move, &toplevel->request_move);
    toplevel->request_resize.notify = xdg_toplevel_request_resize;
    wl_signal_add(&xdg_toplevel->events.request_resize, &toplevel->request_resize);
    toplevel->request_maximize.notify = xdg_toplevel_request_maximize;
    wl_signal_add(&xdg_toplevel->events.request_maximize, &toplevel->request_maximize);
    toplevel->request_fullscreen.notify = xdg_toplevel_request_fullscreen;
    wl_signal_add(&xdg_toplevel->events.request_fullscreen, &toplevel->request_fullscreen);
    toplevel->request_minimize.notify = xdg_toplevel_request_minimize;
    wl_signal_add(&xdg_toplevel->events.request_minimize, &toplevel->request_minimize);
}

static void hwl_popup_destroy(struct wl_listener *listener, void *data) {
    struct hwl_popup *popup = wl_container_of(listener, popup, destroy);
    wl_list_remove(&popup->commit.link);
    wl_list_remove(&popup->destroy.link);
    free(popup);
}

static void server_new_xdg_popup(struct wl_listener *listener, void *data) {
    struct wlr_xdg_popup *xdg_popup = data;
    struct hwl_server *server = wl_container_of(listener, server, new_xdg_popup);
    struct hwl_popup *popup = calloc(1, sizeof(*popup));
    if (!popup) {
        wlr_log(WLR_ERROR, "failed to allocate popup");
        return;
    }
    popup->xdg_popup = xdg_popup;
    popup->server = server;

    // 获取父 surface 的 scene tree
    struct wlr_xdg_surface *parent = wlr_xdg_surface_try_from_wlr_surface(xdg_popup->parent);
    if (!parent || !parent->data) {
        wlr_log(WLR_ERROR, "xdg popup parent or parent->data is NULL");
        free(popup);
        return;
    }
    struct wlr_scene_tree *parent_tree = parent->data;

    // 创建 scene 节点
    struct wlr_scene_tree *popup_tree = wlr_scene_xdg_surface_create(parent_tree, xdg_popup->base);
    if (!popup_tree) {
        wlr_log(WLR_ERROR, "failed to create scene for popup");
        free(popup);
        return;
    }
    xdg_popup->base->data = popup_tree;

    // 【关键】监听 commit 事件，用于响应 initial_commit
    popup->commit.notify = xdg_popup_commit;
    wl_signal_add(&xdg_popup->base->surface->events.commit, &popup->commit);

    // destroy 监听
    popup->destroy.notify = hwl_popup_destroy;
    wl_signal_add(&xdg_popup->events.destroy, &popup->destroy);
}



/*创建socket与hde通信*/

/* IPC 广播辅助函数 - 推送事件到所有连接的 HDE 客户端 */
static int ipc_client_count = 0;
static int ipc_clients[16];  // 最多追踪 16 个连接

static void ipc_broadcast_event(struct hwl_server *server, const char *event_json) {
    (void)server;
    for (int i = 0; i < ipc_client_count; i++) {
        if (ipc_clients[i] >= 0) {
            write(ipc_clients[i], event_json, strlen(event_json));
        }
    }
}

static void ipc_notify_window_opened(struct hwl_server *server,
                                     uintptr_t id, const char *title,
                                     const char *app, int is_x11) {
    char buf[1024];
    snprintf(buf, sizeof(buf),
        "{\"event\":\"window_opened\",\"id\":\"%lu\",\"title\":\"%s\",\"app\":\"%s\",\"x11\":%s}\n",
        id, title ? title : " ", app ? app : " ", is_x11 ? "true" : "false");
    ipc_broadcast_event(server, buf);
}

static void ipc_notify_window_closed(struct hwl_server *server, uintptr_t id) {
    char buf[256];
    snprintf(buf, sizeof(buf),
        "{\"event\":\"window_closed\",\"id\":\"%lu\"}\n", id);
    ipc_broadcast_event(server, buf);
}

static void ipc_notify_window_focused(struct hwl_server *server,
                                      uintptr_t id, int is_x11) {
    char buf[256];
    snprintf(buf, sizeof(buf),
        "{\"event\":\"window_focused\",\"id\":\"%lu\",\"x11\":%s}\n",
        id, is_x11 ? "true" : "false");
    ipc_broadcast_event(server, buf);
}

/* X11 surface 通知 HDE 窗口打开 */
static void notify_x11_window_opened(struct hwl_server *server,
                                     struct hwl_xwayland_surface *hwl_xsurface) {
    if (hwl_xsurface->destroyed || !hwl_xsurface->xwayland_surface)
        return;
    struct wlr_xwayland_surface *xs = hwl_xsurface->xwayland_surface;
    // 用 scene tree node 的地址作为唯一 id（X11 window id 可能为 0）
    uintptr_t id = (uintptr_t)&hwl_xsurface->scene_tree->node;
    const char *title = xs->title ? xs->title : " ";
    const char *app = xs->class ? xs->class : " ";
    ipc_notify_window_opened(server, id, title, app, 1);
    wlr_log(WLR_INFO, "IPC: X11 window_opened id=%lu title=%s app=%s",
            id, title, app);
}

static void *ipc_thread_func(void *arg)
{
	struct hwl_server *server = (struct hwl_server *)arg;
	unlink("/tmp/hwl-ipc.sock");
	int server_fd = socket(AF_UNIX, SOCK_STREAM, 0); /*AF_UNIX：本地通信协议（不同于网络通信)SOCK_STREAM：流式套接字（TCP 风格）0：自动选择协议。*/
	if (server_fd == -1)
	{
		perror("socket");
		return NULL;
	}
	struct sockaddr_un addr;
	memset(&addr, 0, sizeof(addr)); // 清空结构体
	addr.sun_family = AF_UNIX;
	strcpy(addr.sun_path, "/tmp/hwl-ipc.sock");
	if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) == -1)
	{
		perror("bind");
		close(server_fd);
		return NULL;
	}
	// 监听
	if (listen(server_fd, 5) == -1)
	{ // 5为最大挂起数目
		perror("listen");
		close(server_fd);
		return NULL;
	}

	// 循环接受连接
	while (1)
	{
		char buffer[4096];
		int clientfd = accept(server_fd, NULL, NULL);
		if (clientfd == -1)
		{
			perror("accept");
			continue;
		}
		wlr_log(WLR_INFO,"ipc connetced");
		// 注册到广播列表
		if (ipc_client_count < 16) {
			ipc_clients[ipc_client_count++] = clientfd;
		}
		int n = read(clientfd,buffer,sizeof(buffer)-1);
		if (n<=0){
			// 从广播列表移除
			for (int i = 0; i < ipc_client_count; i++) {
				if (ipc_clients[i] == clientfd) {
					ipc_clients[i] = -1;
					break;
				}
			}
			close(clientfd);
			continue;
		}
		buffer[n] = '\0';
		wlr_log(WLR_INFO,"recieved:%s",buffer);
		char *reply = NULL;
		if (strcmp(buffer, "get_windows\n") == 0)
		{
			reply = strdup("[");
			int first = 1;

			// 1) XDG toplevel
			struct hwl_toplevel *toplevel;
			pthread_mutex_lock(&server->toplevels_mutex);
			wl_list_for_each(toplevel, &server->toplevels, link)
			{
				if (!toplevel || !toplevel->xdg_toplevel || !toplevel->xdg_toplevel->base || toplevel->destroyed)
					continue;
				const char *title = toplevel->xdg_toplevel->title ? toplevel->xdg_toplevel->title : " ";
				const char *app_id = toplevel->xdg_toplevel->app_id ? toplevel->xdg_toplevel->app_id : " ";
				uintptr_t id = (uintptr_t)toplevel->xdg_toplevel->base->surface;

				char *entry;
				asprintf(&entry, "%s{\"id\":\"%lu\",\"title\":\"%s\",\"app\":\"%s\",\"x11\":false,\"minimized\":%s,\"maximized\":%s}",
						 first ? "" : ",", id, title, app_id,
						 toplevel->minimized ? "true" : "false",
						 toplevel->maximized ? "true" : "false");
				char *new_reply;
				asprintf(&new_reply, "%s%s", reply, entry);
				free(reply);
				reply = new_reply;
				free(entry);
				first = 0;
			}
			pthread_mutex_unlock(&server->toplevels_mutex);

			// 2) XWayland (X11) toplevel
			struct hwl_xwayland_surface *hwl_xsurface;
			wl_list_for_each(hwl_xsurface, &server->xwayland_surfaces, link)
			{
				if (!hwl_xsurface || hwl_xsurface->destroyed)
					continue;
				if (!hwl_xsurface->mapped)
					continue;
				struct wlr_xwayland_surface *xs = hwl_xsurface->xwayland_surface;
				if (!xs) continue;
				// 过滤掉非顶层（override-redirect 弹窗）
				if (xs->override_redirect) continue;
				const char *title = xs->title ? xs->title : " ";
				const char *app = xs->class ? xs->class : " ";
				uintptr_t id = (uintptr_t)&hwl_xsurface->scene_tree->node;

				char *entry;
				asprintf(&entry, "%s{\"id\":\"%lu\",\"title\":\"%s\",\"app\":\"%s\",\"x11\":true,\"minimized\":%s,\"maximized\":%s}",
						 first ? "" : ",", id, title, app,
						 hwl_xsurface->minimized ? "true" : "false",
						 hwl_xsurface->maximized ? "true" : "false");
				char *new_reply;
				asprintf(&new_reply, "%s%s", reply, entry);
				free(reply);
				reply = new_reply;
				free(entry);
				first = 0;
			}

			char *final_reply;
			asprintf(&final_reply, "%s]", reply);
			free(reply);
			reply = final_reply;
		}
		else if (strncmp(buffer, "focus:", 6) == 0)
		{
			// 提取 ID（第 6 个字符开始，到换行结束）
			char *id_str = buffer + 6;
			// 去掉末尾换行
			size_t len = strlen(id_str);
			if (len > 0 && id_str[len - 1] == '\n')
				id_str[len - 1] = '\0';
			uintptr_t target_id = strtoul(id_str, NULL, 10);
			int found = 0;
			struct hwl_toplevel *target_tl = NULL;  // 先找到，锁外再操作


			pthread_mutex_lock(&server->toplevels_mutex);
			{
				struct hwl_toplevel *toplevel;
				wl_list_for_each(toplevel, &server->toplevels, link)
				{
					if (!toplevel || toplevel->destroyed)
						continue;
					uintptr_t id = (uintptr_t)toplevel->xdg_toplevel->base->surface;
					if (id == target_id)
					{
						found = 1;
						target_tl = toplevel;
						break;
					}
				}
			}
			pthread_mutex_unlock(&server->toplevels_mutex);

			// 锁外执行 Wayland 操作
			if (target_tl) {
				if (target_tl->minimized) {
					xdg_toplevel_minimize(target_tl, false);
				}
				focus_toplevel(target_tl);
			}


			if (!found) {
				struct hwl_xwayland_surface *hwl_xsurface;
				wl_list_for_each(hwl_xsurface, &server->xwayland_surfaces, link) {
					if (!hwl_xsurface || hwl_xsurface->destroyed)
						continue;
					uintptr_t id = (uintptr_t)&hwl_xsurface->scene_tree->node;
					if (id == target_id) {

						if (hwl_xsurface->minimized) {
							xwayland_surface_unminimize(hwl_xsurface);
						}
						focus_xwayland_surface(hwl_xsurface);
						found = 1;
						break;
					}
				}
			}
			reply = strdup(found ? "ok" : "not found");
		}
		else if (strncmp(buffer, "toggle_maximize:", 16) == 0)
		{
			char *id_str = buffer + 16;
			size_t len = strlen(id_str);
			if (len > 0 && id_str[len - 1] == '\n')
				id_str[len - 1] = '\0';
			uintptr_t target_id = strtoul(id_str, NULL, 10);
			int found = 0;
			struct hwl_toplevel *target_tl = NULL;

			pthread_mutex_lock(&server->toplevels_mutex);
			{
				struct hwl_toplevel *toplevel;
				wl_list_for_each(toplevel, &server->toplevels, link) {
					if (!toplevel || toplevel->destroyed) continue;
					uintptr_t id = (uintptr_t)toplevel->xdg_toplevel->base->surface;
					if (id == target_id) {
						found = 1;
						target_tl = toplevel;
						break;
					}
				}
			}
			pthread_mutex_unlock(&server->toplevels_mutex);

			if (target_tl) {
				xdg_toplevel_request_maximize(&target_tl->request_maximize, NULL);
			}

			// 2) 查 XWayland
			if (!found) {
				struct hwl_xwayland_surface *hwl_xsurface;
				wl_list_for_each(hwl_xsurface, &server->xwayland_surfaces, link) {
					if (!hwl_xsurface || hwl_xsurface->destroyed) continue;
					uintptr_t id = (uintptr_t)&hwl_xsurface->scene_tree->node;
					if (id == target_id) {
						found = 1;
						xwayland_surface_request_maximize(&hwl_xsurface->request_maximize, NULL);
						break;
					}
				}
			}
			reply = strdup(found ? "ok" : "not found");
		}
		else if (strncmp(buffer, "minimize:", 9) == 0)
		{
			char *id_str = buffer + 9;
			size_t len = strlen(id_str);
			if (len > 0 && id_str[len - 1] == '\n')
				id_str[len - 1] = '\0';
			uintptr_t target_id = strtoul(id_str, NULL, 10);
			int found = 0;
			struct hwl_toplevel *target_tl = NULL;

			// 1) 查 XDG toplevel（锁内只查，不操作）
			pthread_mutex_lock(&server->toplevels_mutex);
			{
				struct hwl_toplevel *toplevel;
				wl_list_for_each(toplevel, &server->toplevels, link) {
					if (!toplevel || toplevel->destroyed) continue;
					uintptr_t id = (uintptr_t)toplevel->xdg_toplevel->base->surface;
					if (id == target_id) {
						found = 1;
						target_tl = toplevel;
						break;
					}
				}
			}
			pthread_mutex_unlock(&server->toplevels_mutex);

			if (target_tl) {
				xdg_toplevel_minimize(target_tl, true);
			}

			// 2) 查 XWayland
			if (!found) {
				struct hwl_xwayland_surface *hwl_xsurface;
				wl_list_for_each(hwl_xsurface, &server->xwayland_surfaces, link) {
					if (!hwl_xsurface || hwl_xsurface->destroyed) continue;
					uintptr_t id = (uintptr_t)&hwl_xsurface->scene_tree->node;
					if (id == target_id) {
						found = 1;
						xwayland_surface_request_minimize(&hwl_xsurface->request_minimize, NULL);
						break;
					}
				}
			}
			reply = strdup(found ? "ok" : "not found");
		}
		else if (strncmp(buffer, "close:", 6) == 0)
		{
			char *id_str = buffer + 6;
			size_t len = strlen(id_str);
			if (len > 0 && id_str[len - 1] == '\n')
				id_str[len - 1] = '\0';
			uintptr_t target_id = strtoul(id_str, NULL, 10);
			int found = 0;
			// 1) 查 XDG toplevel（锁内只查，不操作）
			struct hwl_toplevel *target_tl = NULL;
			pthread_mutex_lock(&server->toplevels_mutex);
			{
			    struct hwl_toplevel *toplevel;
				wl_list_for_each(toplevel, &server->toplevels, link) {
					if (!toplevel || toplevel->destroyed) continue;
					uintptr_t id = (uintptr_t)toplevel->xdg_toplevel->base->surface;
					if (id == target_id) {
						found = 1;
						target_tl = toplevel;
						break;
					}

				}

			}
			pthread_mutex_unlock(&server->toplevels_mutex);

			if (target_tl) {
				wlr_xdg_toplevel_send_close(target_tl->xdg_toplevel);
				reply = strdup("closed");
				//
			}
			else{
				reply = strdup("not found");
			}
		}
		else{
			reply = strdup("[]");
		}


		//发送reply
		if(reply != NULL){
			write(clientfd,reply,strlen(reply));
			free(reply);
		}
		// 输出日志
		wlr_log(WLR_INFO, "Ipc client connected");
		// 保持连接不断开以接收事件广播 - 不在这里 close
		// 实际设计: 客户端只发请求, server 在事件循环中向所有客户端推送事件
		// 为简化: 此处保留 clientfd 用于推送事件(会有泄漏问题,但 demo 阶段可接受)
		}
	return NULL;
}
int main(int argc, char *argv[])
{
	wlr_log_init(WLR_DEBUG, NULL);
	char *startup_cmd = NULL;
	int c;
	while ((c = getopt(argc, argv, "s:h")) != -1)
	{
		switch (c)
		{
		case 's':
			startup_cmd = optarg;
			break;
		default:
			printf("Usage: %s [-s startup command]\n", argv[0]);
			return 0;
		}
	}
	if (optind < argc)
	{
		printf("Usage: %s [-s startup command]\n", argv[0]);
		return 0;
	}
	/*
			here is start cmd 在这里添加默认启动命令
	*/
	//路径启动hde
	if (startup_cmd == NULL)
	{
		startup_cmd = "cd /home/hopeos/hde/ && python3 main.py & foot";//cd /mnt/hgfs/hde/ && python3 main.py & foot
	}
	struct hwl_server server = {0};
	server.startup_cmd = startup_cmd;
	server.startup_spawned = false;
	pthread_mutex_init(&server.toplevels_mutex, NULL);
	server.wl_display = wl_display_create();
	server.backend = wlr_backend_autocreate(wl_display_get_event_loop(server.wl_display), NULL);
	if (server.backend == NULL)
	{
		wlr_log(WLR_ERROR, "failed to create wlr_backend");
		return 1;
	}
	server.renderer = wlr_renderer_autocreate(server.backend);
	if (server.renderer == NULL)
	{
		wlr_log(WLR_ERROR, "failed to create wlr_renderer");
		return 1;
	}
	wlr_renderer_init_wl_display(server.renderer, server.wl_display);

	server.allocator = wlr_allocator_autocreate(server.backend, server.renderer);
	if (server.allocator == NULL)
	{
		wlr_log(WLR_ERROR, "failed to create wlr_allocator");
		return 1;
	}

	server.compositor=wlr_compositor_create(server.wl_display, 5, server.renderer);
	wlr_subcompositor_create(server.wl_display);
	wlr_data_device_manager_create(server.wl_display);

	// linux_dmabuf_v1 已在 line 1393 通过 wlr_renderer_init_wl_display 注册，勿重复！
	// 重复注册会导致 Wayland 协议全局对象冲突，XWayland 渲染时崩溃

	server.output_layout = wlr_output_layout_create(server.wl_display);
	wl_list_init(&server.outputs);
	server.new_output.notify = server_new_output;
	wl_signal_add(&server.backend->events.new_output, &server.new_output);
	server.scene = wlr_scene_create();
	server.scene_layout = wlr_scene_attach_output_layout(server.scene, server.output_layout);
	wl_list_init(&server.toplevels);
	wl_list_init(&server.xwayland_surfaces);
	server.xdg_shell = wlr_xdg_shell_create(server.wl_display, 3);
	server.new_xdg_toplevel.notify = server_new_xdg_toplevel;
	wl_signal_add(&server.xdg_shell->events.new_toplevel, &server.new_xdg_toplevel);
	server.new_xdg_popup.notify = server_new_xdg_popup;
	wl_signal_add(&server.xdg_shell->events.new_popup, &server.new_xdg_popup);

	server.cursor = wlr_cursor_create();
	wlr_cursor_attach_output_layout(server.cursor, server.output_layout);
	server.cursor_mgr = wlr_xcursor_manager_create(NULL, 24);

	server.cursor_mode = HWL_CURSOR_PASSTHROUGH;
	server.cursor_motion.notify = server_cursor_motion;
	wl_signal_add(&server.cursor->events.motion, &server.cursor_motion);
	server.cursor_motion_absolute.notify = server_cursor_motion_absolute;
	wl_signal_add(&server.cursor->events.motion_absolute, &server.cursor_motion_absolute);
	server.cursor_button.notify = server_cursor_button;
	wl_signal_add(&server.cursor->events.button, &server.cursor_button);
	server.cursor_axis.notify = server_cursor_axis;
	wl_signal_add(&server.cursor->events.axis, &server.cursor_axis);
	server.cursor_frame.notify = server_cursor_frame;
	wl_signal_add(&server.cursor->events.frame, &server.cursor_frame);

	wl_list_init(&server.keyboards);
	server.new_input.notify = server_new_input;
	wl_signal_add(&server.backend->events.new_input, &server.new_input);
	server.seat = wlr_seat_create(server.wl_display, "seat0");
	server.request_cursor.notify = seat_request_cursor;
	wl_signal_add(&server.seat->events.request_set_cursor, &server.request_cursor);
	server.pointer_focus_change.notify = seat_pointer_focus_change;
	wl_signal_add(&server.seat->pointer_state.events.focus_change, &server.pointer_focus_change);
	server.request_set_selection.notify = seat_request_set_selection;
	wl_signal_add(&server.seat->events.request_set_selection, &server.request_set_selection);
	//background
	// 创建背景图层树
	server.background_tree = wlr_scene_tree_create(&server.scene->tree);
	wlr_scene_node_lower_to_bottom(&server.background_tree->node);

	// 暂时注释掉复杂的壁纸加载代码，因为 wlroots 0.18 API 变更导致编译错误
	// 你的 server_new_output 函数里已经有了纯色背景的后备方案，会显示深灰色背景
	server.wallpaper_buffer = NULL;

	/*
	// --- 以下为旧版壁纸加载代码，已注释 ---
	const char *wallpaper_path = "/hde/backgrounds/background.jpg";
	// ... (这里省略中间所有的 stb_load 和 allocator 代码) ...
	*/

	// =========================================
	//sock
	const char *socket = wl_display_add_socket_auto(server.wl_display);
	if (!socket)
	{
		wlr_backend_destroy(server.backend);
		return 1;
	}

	setenv("WAYLAND_DISPLAY", socket, true);

	// XWayland 必须在 backend_start 之前创建！
	// 原因1: wlr_xwayland_create 会设置 DISPLAY 环境变量，startup_cmd 需要继承它
	// 原因2: XWayland 全局对象需要在客户端连接前注册
	// 清理可能残留的 X11 socket 文件（上次异常退出未清理）
	for (int i = 0; i < 10; i++) {
		char path[64];
		snprintf(path, sizeof(path), "/tmp/.X11-unix/X%d", i);
		unlink(path);
	}

	// 初始化 listener link（防止 xwayland_ready 从未触发时退出清理崩溃）
	wl_list_init(&server.xwayland_new_surface.link);

	server.xwayland = wlr_xwayland_create(server.wl_display, server.compositor, false);
	if (!server.xwayland)
	{
		wlr_log(WLR_ERROR, "failed to create XWayland");
	}
	else
	{
		server.xwayland_ready.notify = xwayland_ready;
		wl_signal_add(&server.xwayland->events.ready, &server.xwayland_ready);
	}

	if (!wlr_backend_start(server.backend))
	{
		wlr_backend_destroy(server.backend);
		wl_display_destroy(server.wl_display);
		return 1;
	}

	wlr_log(WLR_INFO, "Running Wayland compositor on WAYLAND_DISPLAY=%s", socket);
	// 创建线程
	pthread_t ipc_thread;
	if (pthread_create(&ipc_thread, NULL, ipc_thread_func, (void *)&server) != 0)
	{
		wlr_log(WLR_ERROR, "failed to create ipc socket");
	}
	pthread_detach(ipc_thread); // 线程独立运行

	// 注意：startup_cmd 必须在 xwayland_ready 后才能启动（否则 DISPLAY 还不存在）
	// spawn 逻辑在 xwayland_ready 回调中执行

	wl_display_run(server.wl_display);

	wl_display_destroy_clients(server.wl_display);

	wl_list_remove(&server.new_xdg_toplevel.link);
	wl_list_remove(&server.new_xdg_popup.link);
	wl_list_remove(&server.cursor_motion.link);
	wl_list_remove(&server.cursor_motion_absolute.link);
	wl_list_remove(&server.cursor_button.link);
	wl_list_remove(&server.cursor_axis.link);
	wl_list_remove(&server.cursor_frame.link);
	wl_list_remove(&server.new_input.link);
	wl_list_remove(&server.request_cursor.link);
	wl_list_remove(&server.pointer_focus_change.link);
	wl_list_remove(&server.request_set_selection.link);
	wl_list_remove(&server.new_output.link);

	//
	if (server.xwayland) {
		wl_list_remove(&server.xwayland_ready.link);
		wl_list_remove(&server.xwayland_new_surface.link);
		wlr_xwayland_destroy(server.xwayland);
	}

	wlr_scene_node_destroy(&server.scene->tree.node);
	wlr_xcursor_manager_destroy(server.cursor_mgr);
	wlr_cursor_destroy(server.cursor);
	wlr_allocator_destroy(server.allocator);
	wlr_renderer_destroy(server.renderer);
	wlr_backend_destroy(server.backend);
	wl_display_destroy(server.wl_display);
	return 0;
}