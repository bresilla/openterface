#pragma once

#include <wayland-client.h>
#include <wayland-cursor.h>
#include "wayland/xdg-shell-client-protocol.h"
#include <functional>
#include <string>
#include <memory>
#include <queue>
#include <mutex>
#include <condition_variable>

namespace openterface {

    // Forward declarations
    class Input;
    class Serial;
    class GUI;

    // Helper struct for Wayland callbacks
    struct WaylandCallbackData {
        struct wl_compositor *compositor = nullptr;
        struct wl_shell *shell = nullptr;
        struct wl_shm *shm = nullptr;
        struct xdg_wm_base *xdg_wm_base = nullptr;
        struct wl_seat *seat = nullptr;
        std::function<void(const std::string &)> log_func;

        // Input state tracking
        bool mouse_over = false;
        bool input_active = false;
        bool debug_mode = false;

        // Window and buffer management
        struct wl_surface *surface = nullptr;
        struct wl_buffer **buffer_ptr = nullptr;
        void **shm_data_ptr = nullptr;
        int *shm_fd_ptr = nullptr;
        int *current_width = nullptr;
        int *current_height = nullptr;
        bool *needs_resize = nullptr;

        // Resize state tracking
        bool is_resizing = false;
        int resize_edge = 0;
        int last_mouse_x = 0;
        int last_mouse_y = 0;

        // Window objects for resize operations
        struct xdg_toplevel *xdg_toplevel = nullptr;
        uint32_t resize_serial = 0;

        // Cursor objects
        struct wl_cursor_theme *cursor_theme = nullptr;
        struct wl_cursor *default_cursor = nullptr;
        struct wl_surface *cursor_surface = nullptr;
        
        // Input objects - pointers to store them in the parent Impl
        struct wl_pointer **pointer_ptr = nullptr;
        struct wl_keyboard **keyboard_ptr = nullptr;
        
        // Input forwarding objects
        std::shared_ptr<Input> *input_ptr = nullptr;
        std::shared_ptr<Serial> *serial_ptr = nullptr;

        // Input queue for non-blocking event processing
        void *input_queue_ptr = nullptr;  // Will be cast to proper type in implementation
        std::mutex *input_mutex_ptr = nullptr;
        std::condition_variable *input_cv_ptr = nullptr;

        // Resize constants
        static constexpr int RESIZE_BORDER = 10; // 10px border for resize detection
        
        // Keyboard modifier tracking
        uint32_t current_modifiers = 0;
    };

    // Wayland protocol callbacks
    void xdg_wm_base_ping(void *data, struct xdg_wm_base *xdg_wm_base, uint32_t serial);
    extern const struct xdg_wm_base_listener xdg_wm_base_listener;
    
    void xdg_surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial);
    extern const struct xdg_surface_listener xdg_surface_listener;
    
    void xdg_toplevel_configure(void *data, struct xdg_toplevel *xdg_toplevel, 
                               int32_t width, int32_t height, struct wl_array *states);
    void xdg_toplevel_close(void *data, struct xdg_toplevel *xdg_toplevel);
    void xdg_toplevel_configure_bounds(void *data, struct xdg_toplevel *xdg_toplevel, 
                                      int32_t width, int32_t height);
    void xdg_toplevel_wm_capabilities(void *data, struct xdg_toplevel *xdg_toplevel,
                                     struct wl_array *capabilities);
    extern const struct xdg_toplevel_listener xdg_toplevel_listener;
    
    void registry_global(void *data, struct wl_registry *registry, uint32_t id, 
                        const char *interface, uint32_t version);
    void registry_global_remove(void *data, struct wl_registry *registry, uint32_t id);
    extern const struct wl_registry_listener registry_listener;
    
    void simple_seat_capabilities(void *data, struct wl_seat *seat, uint32_t capabilities);
    void simple_seat_name(void *data, struct wl_seat *seat, const char *name);
    extern const struct wl_seat_listener simple_seat_listener;

} // namespace openterface