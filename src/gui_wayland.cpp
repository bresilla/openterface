#include "openterface/gui_wayland.hpp"
#include "openterface/gui_input.hpp"
#include "openterface/input.hpp"
#include "openterface/serial.hpp"
#include <cstring>
#include <iostream>
#include <chrono>

namespace openterface {

    // XDG shell callbacks
    void xdg_wm_base_ping(void *data, struct xdg_wm_base *xdg_wm_base, uint32_t serial) {
        // CRITICAL: Respond to ping immediately with ZERO blocking operations
        // No logging, no string operations, no I/O - sub-millisecond response required
        xdg_wm_base_pong(xdg_wm_base, serial);
        
        // Note: Logging removed to prevent I/O blocking ping responses
        // The std::endl in log_func was causing buffer flushes that could delay pong
    }

    const struct xdg_wm_base_listener xdg_wm_base_listener = {
        xdg_wm_base_ping,
    };

    void xdg_surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial) {
        xdg_surface_ack_configure(xdg_surface, serial);

        // Commit the surface after acknowledging configure
        auto *callback_data = static_cast<WaylandCallbackData *>(data);
        if (callback_data->log_func) {
            callback_data->log_func("XDG surface configured");
        }
    }

    const struct xdg_surface_listener xdg_surface_listener = {
        xdg_surface_configure,
    };

    void xdg_toplevel_configure(void *data, struct xdg_toplevel *xdg_toplevel, int32_t width, int32_t height,
                                       struct wl_array *states) {
        // Handle window configure (resize, etc.)
        auto *callback_data = static_cast<WaylandCallbackData *>(data);

        // Add null pointer checks
        if (!callback_data) {
            return;
        }

        if (callback_data->log_func) {
            std::string msg = "XDG toplevel configured: " + std::to_string(width) + "x" + std::to_string(height);
            callback_data->log_func(msg);
        }

        // Validate pointers before dereferencing
        if (!callback_data->current_width || !callback_data->current_height || !callback_data->needs_resize) {
            if (callback_data->log_func) {
                callback_data->log_func("Warning: Invalid callback data pointers in configure");
            }
            return;
        }

        // If we have valid dimensions and they're different from current size, trigger resize
        if (width > 0 && height > 0 && width <= 4096 && height <= 4096) {
            if (*callback_data->current_width != width || *callback_data->current_height != height) {
                // Only update if we haven't resized very recently (prevent rapid resizes)
                auto now = std::chrono::steady_clock::now();
                static auto last_update = std::chrono::steady_clock::time_point{};
                auto time_since_last = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_update);

                if (time_since_last.count() > 16) { // Limit to ~60 FPS
                    *callback_data->current_width = width;
                    *callback_data->current_height = height;
                    *callback_data->needs_resize = true;
                    last_update = now;
                    if (callback_data->log_func) {
                        callback_data->log_func("Window resize triggered: " + std::to_string(width) + "x" +
                                                std::to_string(height));
                    }
                } else if (callback_data->log_func) {
                    callback_data->log_func("Resize rate limited, skipping: " + std::to_string(width) + "x" +
                                            std::to_string(height));
                }
            }
        } else if (callback_data->log_func) {
            callback_data->log_func("Warning: Invalid resize dimensions: " + std::to_string(width) + "x" +
                                    std::to_string(height));
        }
    }

    void xdg_toplevel_close(void *data, struct xdg_toplevel *xdg_toplevel) {
        // Handle window close request
        auto *callback_data = static_cast<WaylandCallbackData *>(data);
        if (callback_data->log_func) {
            callback_data->log_func("Window close requested");
        }
    }

    void xdg_toplevel_configure_bounds(void *data, struct xdg_toplevel *xdg_toplevel, int32_t width,
                                              int32_t height) {
        // Handle configure bounds
        auto *callback_data = static_cast<WaylandCallbackData *>(data);
        if (callback_data->log_func) {
            std::string msg = "XDG toplevel bounds: " + std::to_string(width) + "x" + std::to_string(height);
            callback_data->log_func(msg);
        }
    }

    void xdg_toplevel_wm_capabilities(void *data, struct xdg_toplevel *xdg_toplevel,
                                             struct wl_array *capabilities) {
        // Handle WM capabilities
        auto *callback_data = static_cast<WaylandCallbackData *>(data);
        if (callback_data->log_func) {
            callback_data->log_func("XDG toplevel WM capabilities received");
        }
    }

    const struct xdg_toplevel_listener xdg_toplevel_listener = {
        xdg_toplevel_configure,
        xdg_toplevel_close,
        xdg_toplevel_configure_bounds,
        xdg_toplevel_wm_capabilities,
    };

    // Wayland registry callbacks
    void registry_global(void *data, struct wl_registry *registry, uint32_t id, const char *interface,
                                uint32_t version) {
        auto *callback_data = static_cast<WaylandCallbackData *>(data);

        if (strcmp(interface, wl_compositor_interface.name) == 0) {
            callback_data->compositor =
                static_cast<wl_compositor *>(wl_registry_bind(registry, id, &wl_compositor_interface, 1));
            if (callback_data->log_func)
                callback_data->log_func("Found compositor");
        } else if (strcmp(interface, wl_shell_interface.name) == 0) {
            callback_data->shell = static_cast<wl_shell *>(wl_registry_bind(registry, id, &wl_shell_interface, 1));
            if (callback_data->log_func)
                callback_data->log_func("Found shell (deprecated)");
        } else if (strcmp(interface, wl_shm_interface.name) == 0) {
            callback_data->shm = static_cast<wl_shm *>(wl_registry_bind(registry, id, &wl_shm_interface, 1));
            if (callback_data->log_func)
                callback_data->log_func("Found shared memory");
        } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
            callback_data->xdg_wm_base =
                static_cast<xdg_wm_base *>(wl_registry_bind(registry, id, &xdg_wm_base_interface, 1));
            if (callback_data->log_func)
                callback_data->log_func("Found xdg_wm_base");
        } else if (strcmp(interface, wl_seat_interface.name) == 0) {
            callback_data->seat = static_cast<wl_seat *>(wl_registry_bind(registry, id, &wl_seat_interface, 1));
            if (callback_data->log_func)
                callback_data->log_func("Found seat");
        }
    }

    void registry_global_remove(void *data, struct wl_registry *registry, uint32_t id) {
        // Handle global removal if needed
    }

    const struct wl_registry_listener registry_listener = {
        registry_global, 
        registry_global_remove
    };

    // Seat callbacks
    void simple_seat_capabilities(void *data, struct wl_seat *seat, uint32_t capabilities) {
        auto *callback_data = static_cast<WaylandCallbackData *>(data);

        if (callback_data->log_func) {
            callback_data->log_func("Seat capabilities callback triggered!");
            callback_data->log_func("Capabilities: " + std::to_string(capabilities));
        }

        if (capabilities & WL_SEAT_CAPABILITY_POINTER) {
            struct wl_pointer *pointer = wl_seat_get_pointer(seat);
            if (pointer && callback_data->log_func) {
                callback_data->log_func("Setting up mouse input capture");
                wl_pointer_add_listener(pointer, &debug_pointer_listener, callback_data);
                
                // Store pointer in GUI::Impl for proper cleanup
                if (callback_data->pointer_ptr) {
                    *callback_data->pointer_ptr = pointer;
                    callback_data->log_func("Mouse pointer stored for cleanup");
                }
            }
        }

        if (capabilities & WL_SEAT_CAPABILITY_KEYBOARD) {
            struct wl_keyboard *keyboard = wl_seat_get_keyboard(seat);
            if (keyboard && callback_data->log_func) {
                callback_data->log_func("Setting up keyboard input capture");
                wl_keyboard_add_listener(keyboard, &debug_keyboard_listener, callback_data);
                
                // Store keyboard in GUI::Impl for proper cleanup
                if (callback_data->keyboard_ptr) {
                    *callback_data->keyboard_ptr = keyboard;
                    callback_data->log_func("Keyboard stored for cleanup");
                }
            }
        }
    }

    void simple_seat_name(void *data, struct wl_seat *seat, const char *name) {
        auto *callback_data = static_cast<WaylandCallbackData *>(data);
        if (callback_data->log_func) {
            callback_data->log_func("Input seat name: " + std::string(name));
        }
    }

    const struct wl_seat_listener simple_seat_listener = {
        simple_seat_capabilities,
        simple_seat_name,
    };

} // namespace openterface