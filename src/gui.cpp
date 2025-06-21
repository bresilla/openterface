#include "openterface/gui.hpp"
#include "openterface/input.hpp"
#include "openterface/serial.hpp"
#include "openterface/video.hpp"
#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>

// Wayland includes
#include "wayland/xdg-shell-client-protocol.h"
#include <fcntl.h>
#include <string.h>
#include <cstring>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <wayland-client.h>
#include <wayland-cursor.h>
#include <errno.h>
#include <poll.h>

// For memfd_create
#include <sys/syscall.h>

// Define memfd_create if not available
#ifndef MFD_CLOEXEC
#define MFD_CLOEXEC 0x0001U
#endif

#ifndef __NR_memfd_create
#define __NR_memfd_create 319
#endif

namespace {
    int create_memfd(const char *name, unsigned int flags) { return syscall(__NR_memfd_create, name, flags); }
} // namespace

namespace openterface {

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
        
        // Resize constants
        static constexpr int RESIZE_BORDER = 10; // 10px border for resize detection
    };

    // XDG shell callbacks
    static void xdg_wm_base_ping(void *data, struct xdg_wm_base *xdg_wm_base, uint32_t serial) {
        // Always respond to ping immediately - this is critical for window responsiveness
        xdg_wm_base_pong(xdg_wm_base, serial);
        
        auto *callback_data = static_cast<WaylandCallbackData *>(data);
        if (callback_data && callback_data->log_func) {
            callback_data->log_func("[PING] Received XDG ping, sent pong (serial=" + std::to_string(serial) + ")");
        }
    }

    static const struct xdg_wm_base_listener xdg_wm_base_listener = {
        xdg_wm_base_ping,
    };

    static void xdg_surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial) {
        xdg_surface_ack_configure(xdg_surface, serial);

        // Commit the surface after acknowledging configure
        auto *callback_data = static_cast<WaylandCallbackData *>(data);
        if (callback_data->log_func) {
            callback_data->log_func("XDG surface configured");
        }
    }

    static const struct xdg_surface_listener xdg_surface_listener = {
        xdg_surface_configure,
    };

    static void xdg_toplevel_configure(void *data, struct xdg_toplevel *xdg_toplevel, int32_t width, int32_t height,
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
                        callback_data->log_func("Window resize triggered: " + std::to_string(width) + "x" + std::to_string(height));
                    }
                } else if (callback_data->log_func) {
                    callback_data->log_func("Resize rate limited, skipping: " + std::to_string(width) + "x" + std::to_string(height));
                }
            }
        } else if (callback_data->log_func) {
            callback_data->log_func("Warning: Invalid resize dimensions: " + std::to_string(width) + "x" + std::to_string(height));
        }
    }

    static void xdg_toplevel_close(void *data, struct xdg_toplevel *xdg_toplevel) {
        // Handle window close request
        auto *callback_data = static_cast<WaylandCallbackData *>(data);
        if (callback_data->log_func) {
            callback_data->log_func("Window close requested");
        }
    }

    static void xdg_toplevel_configure_bounds(void *data, struct xdg_toplevel *xdg_toplevel, int32_t width,
                                              int32_t height) {
        // Handle configure bounds
        auto *callback_data = static_cast<WaylandCallbackData *>(data);
        if (callback_data->log_func) {
            std::string msg = "XDG toplevel bounds: " + std::to_string(width) + "x" + std::to_string(height);
            callback_data->log_func(msg);
        }
    }

    static void xdg_toplevel_wm_capabilities(void *data, struct xdg_toplevel *xdg_toplevel,
                                             struct wl_array *capabilities) {
        // Handle WM capabilities
        auto *callback_data = static_cast<WaylandCallbackData *>(data);
        if (callback_data->log_func) {
            callback_data->log_func("XDG toplevel WM capabilities received");
        }
    }

    static const struct xdg_toplevel_listener xdg_toplevel_listener = {
        xdg_toplevel_configure,
        xdg_toplevel_close,
        xdg_toplevel_configure_bounds,
        xdg_toplevel_wm_capabilities,
    };

    // Wayland registry callbacks
    static void registry_global(void *data, struct wl_registry *registry, uint32_t id, const char *interface,
                                uint32_t version);
    static void registry_global_remove(void *data, struct wl_registry *registry, uint32_t id);

    static const struct wl_registry_listener registry_listener = {registry_global, registry_global_remove};

    class GUI::Impl {
      public:
        GUIInfo info;
        std::shared_ptr<Video> video;
        std::shared_ptr<Input> input;
        std::shared_ptr<Serial> serial;
        std::atomic<bool> exit_requested{false};
        bool initialized = false;

        // Wayland objects
        struct wl_display *display = nullptr;
        struct wl_registry *registry = nullptr;
        struct wl_compositor *compositor = nullptr;
        struct wl_surface *surface = nullptr;
        struct wl_shell *shell = nullptr;
        struct wl_shell_surface *shell_surface = nullptr;
        struct wl_shm *shm = nullptr;

        // XDG shell objects
        struct xdg_wm_base *xdg_wm_base = nullptr;
        struct xdg_surface *xdg_surface = nullptr;
        struct xdg_toplevel *xdg_toplevel = nullptr;

        // Input objects
        struct wl_seat *seat = nullptr;
        struct wl_pointer *pointer = nullptr;
        struct wl_keyboard *keyboard = nullptr;
        
        // Cursor objects
        struct wl_cursor_theme *cursor_theme = nullptr;
        struct wl_cursor *default_cursor = nullptr;
        struct wl_surface *cursor_surface = nullptr;

        // Input state
        bool mouse_over_surface = false;
        bool input_grabbed = false;

        // Buffer for rendering
        struct wl_buffer *buffer = nullptr;
        void *shm_data = nullptr;
        int shm_fd = -1;
        bool needs_resize = false;
        
        // Synchronization
        std::mutex resize_mutex;
        std::atomic<bool> resize_in_progress{false};
        std::chrono::steady_clock::time_point last_resize_time;

        // Video frame storage
        std::vector<uint8_t> current_frame;
        std::mutex frame_mutex;
        int frame_width = 0;
        int frame_height = 0;
        bool has_new_frame = false;

        // Debug mode
        bool debug_input = false;

        WaylandCallbackData callback_data;

        void log(const std::string &msg) {
            if (initialized) {
                std::cout << "[GUI] " << msg << std::endl;
            }
        }

        bool initWayland();
        void cleanupWayland();
        bool createWaylandWindow();
        bool createBuffer(int width, int height);
        void destroyBuffer();
        void onVideoFrame(const FrameData& frame);
    };

    // Simple input callbacks using callback data
    static void simple_seat_capabilities(void *data, struct wl_seat *seat, uint32_t capabilities) {
        auto *callback_data = static_cast<WaylandCallbackData *>(data);

        if (callback_data->log_func) {
            callback_data->log_func("🎯 Seat capabilities callback triggered!");
            callback_data->log_func("🎯 Capabilities: " + std::to_string(capabilities));
        }

        // Forward declarations
        extern const struct wl_pointer_listener debug_pointer_listener;
        extern const struct wl_keyboard_listener debug_keyboard_listener;

        if (capabilities & WL_SEAT_CAPABILITY_POINTER) {
            struct wl_pointer *pointer = wl_seat_get_pointer(seat);
            if (pointer && callback_data->log_func) {
                callback_data->log_func("🖱️  Setting up mouse input capture");
                wl_pointer_add_listener(pointer, &debug_pointer_listener, callback_data);
            }
        }

        if (capabilities & WL_SEAT_CAPABILITY_KEYBOARD) {
            struct wl_keyboard *keyboard = wl_seat_get_keyboard(seat);
            if (keyboard && callback_data->log_func) {
                callback_data->log_func("⌨️  Setting up keyboard input capture");
                wl_keyboard_add_listener(keyboard, &debug_keyboard_listener, callback_data);
            }
        }
    }

    static void simple_seat_name(void *data, struct wl_seat *seat, const char *name) {
        auto *callback_data = static_cast<WaylandCallbackData *>(data);
        if (callback_data->log_func) {
            callback_data->log_func("Input seat name: " + std::string(name));
        }
    }

    static const struct wl_seat_listener simple_seat_listener = {
        simple_seat_capabilities,
        simple_seat_name,
    };

    // Helper function to determine resize edge
    static int get_resize_edge(int x, int y, int width, int height, int border_size) {
        int edge = 0;
        
        if (x < border_size) edge |= 1; // left
        if (x > width - border_size) edge |= 2; // right
        if (y < border_size) edge |= 4; // top
        if (y > height - border_size) edge |= 8; // bottom
        
        return edge;
    }
    
    // Convert internal edge flags to XDG shell resize edges
    static uint32_t edge_to_xdg_edge(int edge) {
        // XDG_TOPLEVEL_RESIZE_EDGE constants
        const uint32_t XDG_TOPLEVEL_RESIZE_EDGE_NONE = 0;
        const uint32_t XDG_TOPLEVEL_RESIZE_EDGE_TOP = 1;
        const uint32_t XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM = 2;
        const uint32_t XDG_TOPLEVEL_RESIZE_EDGE_LEFT = 4;
        const uint32_t XDG_TOPLEVEL_RESIZE_EDGE_TOP_LEFT = 5;
        const uint32_t XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_LEFT = 6;
        const uint32_t XDG_TOPLEVEL_RESIZE_EDGE_RIGHT = 8;
        const uint32_t XDG_TOPLEVEL_RESIZE_EDGE_TOP_RIGHT = 9;
        const uint32_t XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT = 10;
        
        switch (edge) {
            case 1: return XDG_TOPLEVEL_RESIZE_EDGE_LEFT;       // left
            case 2: return XDG_TOPLEVEL_RESIZE_EDGE_RIGHT;      // right  
            case 4: return XDG_TOPLEVEL_RESIZE_EDGE_TOP;        // top
            case 8: return XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM;     // bottom
            case 5: return XDG_TOPLEVEL_RESIZE_EDGE_TOP_LEFT;   // top-left
            case 6: return XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_LEFT; // bottom-left
            case 9: return XDG_TOPLEVEL_RESIZE_EDGE_TOP_RIGHT;  // top-right
            case 10: return XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT; // bottom-right
            default: return XDG_TOPLEVEL_RESIZE_EDGE_NONE;
        }
    }

    // Function to set cursor
    static void set_cursor(void *data, struct wl_pointer *pointer, uint32_t serial) {
        auto *callback_data = static_cast<WaylandCallbackData *>(data);
        
        if (!callback_data->default_cursor || !callback_data->cursor_surface) {
            if (callback_data->log_func) {
                callback_data->log_func("Warning: No cursor available to set");
            }
            return;
        }
        
        struct wl_cursor_image *image = callback_data->default_cursor->images[0];
        if (!image) {
            if (callback_data->log_func) {
                callback_data->log_func("Warning: No cursor image available");
            }
            return;
        }
        
        // Attach cursor image to cursor surface
        wl_surface_attach(callback_data->cursor_surface, wl_cursor_image_get_buffer(image), 0, 0);
        wl_surface_damage(callback_data->cursor_surface, 0, 0, image->width, image->height);
        wl_surface_commit(callback_data->cursor_surface);
        
        // Set the cursor
        wl_pointer_set_cursor(pointer, serial, callback_data->cursor_surface, 
                             image->hotspot_x, image->hotspot_y);
        
        if (callback_data->log_func) {
            callback_data->log_func("✅ Cursor set successfully");
        }
    }

    // Pointer callbacks for debugging
    static void debug_pointer_enter(void *data, struct wl_pointer *pointer, uint32_t serial, struct wl_surface *surface,
                                    wl_fixed_t sx, wl_fixed_t sy) {
        auto *callback_data = static_cast<WaylandCallbackData *>(data);
        callback_data->mouse_over = true;
        
        // Store initial mouse position
        callback_data->last_mouse_x = wl_fixed_to_int(sx);
        callback_data->last_mouse_y = wl_fixed_to_int(sy);
        
        if (callback_data->log_func) {
            callback_data->log_func("🎯 DEBUG: pointer_enter callback called!");
            callback_data->log_func("🖱️  Mouse ENTERED window");
        }
        
        // Set cursor to make it visible
        set_cursor(data, pointer, serial);
    }

    static void debug_pointer_leave(void *data, struct wl_pointer *pointer, uint32_t serial,
                                    struct wl_surface *surface) {
        auto *callback_data = static_cast<WaylandCallbackData *>(data);
        callback_data->mouse_over = false;
        if (callback_data->log_func) {
            callback_data->log_func("🖱️  Mouse LEFT window");
        }
    }

    static void debug_pointer_motion(void *data, struct wl_pointer *pointer, uint32_t time, wl_fixed_t sx,
                                     wl_fixed_t sy) {
        auto *callback_data = static_cast<WaylandCallbackData *>(data);
        if (!callback_data->mouse_over) return;
        
        int x = wl_fixed_to_int(sx);
        int y = wl_fixed_to_int(sy);
        
        // Update stored position
        callback_data->last_mouse_x = x;
        callback_data->last_mouse_y = y;
        
        // Check if we're currently resizing
        if (callback_data->is_resizing) {
            // Handle ongoing resize - this would be where you'd send resize requests
            // to the window manager, but for now we'll just log it
            if (callback_data->log_func) {
                std::string msg = "🔄 Resizing window at: (" + std::to_string(x) + ", " + std::to_string(y) + ")";
                callback_data->log_func(msg);
            }
            return;
        }
        
        // Check if mouse is near window edges for resize cursor
        if (callback_data->current_width && callback_data->current_height) {
            int edge = get_resize_edge(x, y, *callback_data->current_width, *callback_data->current_height, 
                                     WaylandCallbackData::RESIZE_BORDER);
            
            if (edge != callback_data->resize_edge) {
                callback_data->resize_edge = edge;
                
                // Log when entering/leaving resize areas
                if (callback_data->log_func) {
                    if (edge != 0) {
                        std::string msg = "🎯 Mouse near window edge - resize available (edge=" + std::to_string(edge) + ")";
                        callback_data->log_func(msg);
                    } else {
                        callback_data->log_func("🎯 Mouse in window center - normal cursor");
                    }
                }
                
                // Here you would set different cursor shapes based on edge:
                // edge == 1|2: horizontal resize cursor
                // edge == 4|8: vertical resize cursor  
                // edge == 5|6|9|10: diagonal resize cursors
            }
        }
        
        // Debug logging for mouse motion
        if (callback_data->debug_mode && callback_data->log_func) {
            static int log_counter = 0;
            if ((log_counter++ % 10 == 0)) { // Log every 10th motion event in debug mode
                std::string msg = "[DEBUG] Mouse motion: (" + std::to_string(x) + ", " + std::to_string(y) + ")";
                if (callback_data->resize_edge != 0) {
                    msg += " [resize edge: " + std::to_string(callback_data->resize_edge) + "]";
                }
                callback_data->log_func(msg);
            }
        }
    }

    static void debug_pointer_button(void *data, struct wl_pointer *pointer, uint32_t serial, uint32_t time,
                                     uint32_t button, uint32_t state) {
        auto *callback_data = static_cast<WaylandCallbackData *>(data);
        if (!callback_data->mouse_over) return;
        
        const char *action = (state == WL_POINTER_BUTTON_STATE_PRESSED) ? "PRESSED" : "RELEASED";
        const char *btn_name = "";
        switch (button) {
        case 0x110:
            btn_name = "LEFT";
            break;
        case 0x111:
            btn_name = "RIGHT";
            break;
        case 0x112:
            btn_name = "MIDDLE";
            break;
        default:
            btn_name = "UNKNOWN";
            break;
        }
        
        // Handle resize operations with left mouse button
        if (button == 0x110) { // Left mouse button
            if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
                // Check if we're clicking on a resize edge
                if (callback_data->resize_edge != 0 && callback_data->xdg_toplevel && callback_data->seat) {
                    callback_data->is_resizing = true;
                    callback_data->resize_serial = serial; // Store the serial for this resize operation
                    
                    uint32_t xdg_edge = edge_to_xdg_edge(callback_data->resize_edge);
                    
                    if (callback_data->log_func) {
                        callback_data->log_func("🔄 Starting window resize operation (edge=" + 
                                              std::to_string(callback_data->resize_edge) + 
                                              ", xdg_edge=" + std::to_string(xdg_edge) + ")");
                    }
                    
                    // Actually start the resize operation
                    xdg_toplevel_resize(callback_data->xdg_toplevel, callback_data->seat, serial, xdg_edge);
                }
            } else if (state == WL_POINTER_BUTTON_STATE_RELEASED) {
                // Stop resizing
                if (callback_data->is_resizing) {
                    callback_data->is_resizing = false;
                    if (callback_data->log_func) {
                        callback_data->log_func("🔄 Finished window resize operation");
                    }
                }
            }
        }
        
        // Debug logging for mouse buttons
        if (callback_data->debug_mode && callback_data->log_func) {
            std::string msg = "[DEBUG] Mouse " + std::string(btn_name) + " button " + action;
            if (callback_data->resize_edge != 0) {
                msg += " (at resize edge " + std::to_string(callback_data->resize_edge) + ")";
            }
            callback_data->log_func(msg);
        }
    }

    static void debug_pointer_axis(void *data, struct wl_pointer *pointer, uint32_t time, uint32_t axis,
                                   wl_fixed_t value) {
        auto *callback_data = static_cast<WaylandCallbackData *>(data);
        if (callback_data->mouse_over && callback_data->log_func) {
            const char *axis_name = (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) ? "VERTICAL" : "HORIZONTAL";
            double scroll_value = wl_fixed_to_double(value);
            std::string msg = "🖱️  Mouse scroll " + std::string(axis_name) + ": " + std::to_string(scroll_value);
            callback_data->log_func(msg);
        }
    }

    static void debug_pointer_frame(void *data, struct wl_pointer *pointer) {
        // Frame event - end of pointer event group
    }

    static void debug_pointer_axis_source(void *data, struct wl_pointer *pointer, uint32_t axis_source) {
        // Axis source (wheel, finger, etc.)
    }

    static void debug_pointer_axis_stop(void *data, struct wl_pointer *pointer, uint32_t time, uint32_t axis) {
        // Axis scrolling stopped
    }

    static void debug_pointer_axis_discrete(void *data, struct wl_pointer *pointer, uint32_t axis, int32_t discrete) {
        // Discrete axis events (scroll wheel clicks)
    }

    static void debug_pointer_axis_value120(void *data, struct wl_pointer *pointer, uint32_t axis, int32_t value120) {
        // High-resolution scroll wheel events
    }

    static void debug_pointer_axis_relative_direction(void *data, struct wl_pointer *pointer, uint32_t axis,
                                                      uint32_t direction) {
        // Relative direction for axis events
    }

    const struct wl_pointer_listener debug_pointer_listener = {
        debug_pointer_enter,
        debug_pointer_leave,
        debug_pointer_motion,
        debug_pointer_button,
        debug_pointer_axis,
        debug_pointer_frame,
        debug_pointer_axis_source,
        debug_pointer_axis_stop,
        debug_pointer_axis_discrete,
        debug_pointer_axis_value120,
        debug_pointer_axis_relative_direction,
    };

    // Keyboard callbacks for debugging
    static void debug_keyboard_keymap(void *data, struct wl_keyboard *keyboard, uint32_t format, int32_t fd,
                                      uint32_t size) {
        close(fd); // Just close the keymap fd for now
    }

    static void debug_keyboard_enter(void *data, struct wl_keyboard *keyboard, uint32_t serial,
                                     struct wl_surface *surface, struct wl_array *keys) {
        auto *callback_data = static_cast<WaylandCallbackData *>(data);
        callback_data->input_active = true;
        if (callback_data->debug_mode && callback_data->log_func) {
            callback_data->log_func("[DEBUG] Keyboard FOCUS gained - capturing input");
        }
    }

    static void debug_keyboard_leave(void *data, struct wl_keyboard *keyboard, uint32_t serial,
                                     struct wl_surface *surface) {
        auto *callback_data = static_cast<WaylandCallbackData *>(data);
        callback_data->input_active = false;
        if (callback_data->debug_mode && callback_data->log_func) {
            callback_data->log_func("[DEBUG] Keyboard FOCUS lost - input released");
        }
    }

    static void debug_keyboard_key(void *data, struct wl_keyboard *keyboard, uint32_t serial, uint32_t time,
                                   uint32_t key, uint32_t state) {
        auto *callback_data = static_cast<WaylandCallbackData *>(data);
        // Debug logging for keyboard events
        if (callback_data->input_active && callback_data->debug_mode && callback_data->log_func) {
            const char *action = (state == WL_KEYBOARD_KEY_STATE_PRESSED) ? "PRESSED" : "RELEASED";
            std::string msg = "[DEBUG] Key " + std::to_string(key) + " " + action;
            callback_data->log_func(msg);
        }
    }

    static void debug_keyboard_modifiers(void *data, struct wl_keyboard *keyboard, uint32_t serial,
                                         uint32_t mods_depressed, uint32_t mods_latched, uint32_t mods_locked,
                                         uint32_t group) {
        auto *callback_data = static_cast<WaylandCallbackData *>(data);
        if (callback_data->input_active && callback_data->log_func && (mods_depressed || mods_latched || mods_locked)) {
            std::string msg = "⌨️  Modifiers: Ctrl=" + std::to_string((mods_depressed & 4) ? 1 : 0) +
                              " Shift=" + std::to_string((mods_depressed & 1) ? 1 : 0) +
                              " Alt=" + std::to_string((mods_depressed & 8) ? 1 : 0);
            callback_data->log_func(msg);
        }
    }

    static void debug_keyboard_repeat_info(void *data, struct wl_keyboard *keyboard, int32_t rate, int32_t delay) {
        // Keyboard repeat info
    }

    const struct wl_keyboard_listener debug_keyboard_listener = {
        debug_keyboard_keymap, debug_keyboard_enter,     debug_keyboard_leave,
        debug_keyboard_key,    debug_keyboard_modifiers, debug_keyboard_repeat_info,
    };

    GUI::GUI() : pImpl(std::make_unique<Impl>()) {}

    GUI::~GUI() { shutdown(); }

    bool GUI::initialize() {
        std::cout << "DEBUG: GUI::initialize() called" << std::endl;
        pImpl->initialized = true; // Set flag first so logging works
        pImpl->log("DEBUG: Set initialized flag, about to log");
        pImpl->log("Initializing GUI with Wayland");
        pImpl->log("DEBUG: About to call initWayland()");
        std::cout << "DEBUG: Before initWayland call" << std::endl;
        bool result = pImpl->initWayland();
        std::cout << "DEBUG: After initWayland call, result=" << result << std::endl;
        if (!result) {
            pImpl->log("DEBUG: initWayland failed, resetting flag");
            pImpl->initialized = false; // Reset if failed
        }
        pImpl->log("DEBUG: initialize() returning " + std::string(result ? "true" : "false"));
        return result;
    }

    void GUI::shutdown() {
        if (!pImpl->initialized) {
            return; // Don't log anything if never initialized
        }

        stopInputCapture();
        stopVideoDisplay();
        destroyWindow();
        pImpl->cleanupWayland();
        pImpl->log("GUI shutdown complete");
        pImpl->initialized = false;
    }

    bool GUI::isInitialized() const { return pImpl->initialized; }

    bool GUI::createWindow(const std::string &title, int width, int height) {
        std::cout << "DEBUG: createWindow() called with " << title << " " << width << "x" << height << std::endl;
        pImpl->info.window_title = title;
        pImpl->info.window_width = width;
        pImpl->info.window_height = height;

        pImpl->log("Creating Wayland window: " + title + " (" + std::to_string(width) + "x" + std::to_string(height) +
                   ")");

        std::cout << "DEBUG: About to call createWaylandWindow()" << std::endl;
        bool result = pImpl->createWaylandWindow();
        std::cout << "DEBUG: createWaylandWindow() returned " << result << std::endl;
        return result;
    }

    void GUI::destroyWindow() {
        if (pImpl->info.window_created) {
            pImpl->log("Destroying Wayland window");

            // Clean up buffer first
            pImpl->destroyBuffer();

            // Clean up XDG shell objects
            if (pImpl->xdg_toplevel) {
                xdg_toplevel_destroy(pImpl->xdg_toplevel);
                pImpl->xdg_toplevel = nullptr;
            }

            if (pImpl->xdg_surface) {
                xdg_surface_destroy(pImpl->xdg_surface);
                pImpl->xdg_surface = nullptr;
            }

            // Clean up wl_shell objects
            if (pImpl->shell_surface) {
                wl_shell_surface_destroy(pImpl->shell_surface);
                pImpl->shell_surface = nullptr;
            }

            if (pImpl->surface) {
                wl_surface_destroy(pImpl->surface);
                pImpl->surface = nullptr;
            }

            pImpl->info.window_created = false;
            pImpl->info.video_displayed = false;
            pImpl->info.input_captured = false;
        }
    }

    bool GUI::isWindowCreated() const { return pImpl->info.window_created; }

    void GUI::setVideoSource(std::shared_ptr<Video> video) {
        pImpl->video = video;
        pImpl->log("Video source set");
    }

    bool GUI::startVideoDisplay() {
        if (!pImpl->video) {
            pImpl->log("No video source available");
            return false;
        }

        if (!pImpl->info.window_created) {
            pImpl->log("No window created for video display");
            return false;
        }

        pImpl->log("Starting video display");

        // Set up frame callback from video source
        pImpl->video->setFrameCallback([this](const FrameData& frame) {
            pImpl->onVideoFrame(frame);
        });

        // Start video capture
        if (!pImpl->video->startCapture()) {
            pImpl->log("Failed to start video capture");
            return false;
        }

        pImpl->info.video_displayed = true;
        pImpl->log("Video display and capture started successfully");
        return true;
    }

    void GUI::stopVideoDisplay() {
        if (pImpl->info.video_displayed) {
            pImpl->log("Stopping video display");
            pImpl->info.video_displayed = false;
        }
    }

    bool GUI::isVideoDisplaying() const { return pImpl->info.video_displayed; }

    void GUI::setInputTarget(std::shared_ptr<Input> input) {
        pImpl->input = input;
        pImpl->log("Input target set");
    }

    void GUI::setSerialForwarder(std::shared_ptr<Serial> serial) {
        pImpl->serial = serial;
        pImpl->log("Serial forwarder set");
    }

    bool GUI::startInputCapture() {
        if (!pImpl->input || !pImpl->serial) {
            pImpl->log("Input target or serial forwarder not set");
            return false;
        }

        if (!pImpl->info.window_created) {
            pImpl->log("No window created for input capture");
            return false;
        }

        pImpl->log("Starting input capture and forwarding");

        // In a real implementation, this would:
        // 1. Grab keyboard/mouse focus for the window
        // 2. Set up input event callbacks
        // 3. Forward events through serial connection

        pImpl->info.input_captured = true;
        return true;
    }

    void GUI::stopInputCapture() {
        if (pImpl->info.input_captured) {
            pImpl->log("Stopping input capture");
            pImpl->info.input_captured = false;
        }
    }

    bool GUI::isInputCapturing() const { return pImpl->info.input_captured; }

    void GUI::setWindowTitle(const std::string &title) {
        pImpl->info.window_title = title;
        if (pImpl->info.window_created) {
            pImpl->log("Window title changed to: " + title);
        }
    }

    void GUI::setWindowSize(int width, int height) {
        pImpl->info.window_width = width;
        pImpl->info.window_height = height;
        if (pImpl->info.window_created) {
            pImpl->log("Window resized to: " + std::to_string(width) + "x" + std::to_string(height));
        }
    }

    void GUI::setFullscreen(bool fullscreen) {
        pImpl->log(std::string("Fullscreen ") + (fullscreen ? "enabled" : "disabled"));
    }

    bool GUI::isFullscreen() const {
        return false; // Placeholder
    }

    int GUI::runEventLoop() {
        pImpl->log("Starting Wayland event loop");

        // Prepare for polling
        struct pollfd fds[1];
        fds[0].fd = wl_display_get_fd(pImpl->display);
        fds[0].events = POLLIN;

        auto last_frame_time = std::chrono::steady_clock::now();
        const auto frame_duration = std::chrono::milliseconds(16); // Target 60 FPS

        // Wayland event loop
        while (!pImpl->exit_requested && pImpl->display) {
            // Process any already pending events first
            wl_display_dispatch_pending(pImpl->display);
            
            // Flush any pending requests to the compositor
            if (wl_display_flush(pImpl->display) < 0 && errno != EAGAIN) {
                pImpl->log("Error flushing display: " + std::string(strerror(errno)));
                break;
            }
            
            // Poll with a short timeout to stay responsive
            int ret = poll(fds, 1, 5); // 5ms timeout for responsiveness
            
            if (ret > 0) {
                // Events are available, dispatch them directly
                if (wl_display_dispatch(pImpl->display) == -1) {
                    pImpl->log("Error dispatching Wayland events");
                    break;
                }
            } else if (ret == 0) {
                // Timeout - continue loop to stay responsive
            } else {
                // Error in poll
                pImpl->log("Error in poll: " + std::string(strerror(errno)));
                break;
            }
            
            // Handle resize in a separate thread if needed - don't block event loop
            if (pImpl->needs_resize && pImpl->surface) {
                static auto last_resize_attempt = std::chrono::steady_clock::now();
                auto now = std::chrono::steady_clock::now();
                
                // Only attempt resize every 16ms to avoid blocking too often
                if (now - last_resize_attempt > std::chrono::milliseconds(16)) {
                    std::unique_lock<std::mutex> lock(pImpl->resize_mutex, std::try_to_lock);
                    if (lock.owns_lock() && pImpl->needs_resize && !pImpl->resize_in_progress.load()) {
                        pImpl->resize_in_progress = true;
                        
                        // Quick dimension validation only
                        if (pImpl->info.window_width > 0 && pImpl->info.window_height > 0 && 
                            pImpl->info.window_width <= 4096 && pImpl->info.window_height <= 4096) {
                            
                            // Defer heavy buffer operations - just mark for later
                            // This keeps the event loop responsive for ping/pong
                            pImpl->needs_resize = false;
                        }
                        
                        pImpl->resize_in_progress = false;
                    }
                    last_resize_attempt = now;
                }
            }
            
            // Minimal frame commits without heavy operations
            static auto last_commit_time = std::chrono::steady_clock::now();
            auto now = std::chrono::steady_clock::now();
            
            if (now - last_commit_time > std::chrono::milliseconds(16)) {
                if (pImpl->surface && pImpl->buffer) {
                    wl_surface_damage(pImpl->surface, 0, 0, pImpl->info.window_width, pImpl->info.window_height);
                    wl_surface_commit(pImpl->surface);
                }
                last_commit_time = now;
            }
        }

        pImpl->log("GUI event loop exited");
        return 0;
    }

    void GUI::requestExit() {
        pImpl->exit_requested = true;
        pImpl->log("Exit requested");
    }

    GUIInfo GUI::getInfo() const { return pImpl->info; }

    void GUI::setDebugMode(bool enabled) {
        pImpl->debug_input = enabled;
        if (enabled) {
            pImpl->log("Debug mode enabled - input events will be logged");
        }
    }

    // Wayland implementation methods
    bool GUI::Impl::initWayland() {
        std::cout << "DEBUG: initWayland() called" << std::endl;

        // Connect to Wayland display
        std::cout << "DEBUG: About to connect to Wayland display" << std::endl;
        display = wl_display_connect(nullptr);
        if (!display) {
            log("Failed to connect to Wayland display");
            std::cout << "DEBUG: wl_display_connect failed" << std::endl;
            return false;
        }
        log("Connected to Wayland display");
        std::cout << "DEBUG: Successfully connected to Wayland display" << std::endl;

        // Get registry
        std::cout << "DEBUG: About to get registry" << std::endl;
        registry = wl_display_get_registry(display);
        if (!registry) {
            log("Failed to get Wayland registry");
            std::cout << "DEBUG: wl_display_get_registry failed" << std::endl;
            return false;
        }
        std::cout << "DEBUG: Got registry successfully" << std::endl;

        // Setup callback data
        std::cout << "DEBUG: Setting up callback data" << std::endl;
        callback_data.log_func = [this](const std::string &msg) { log(msg); };

        // Add registry listener with callback data
        std::cout << "DEBUG: About to add registry listener" << std::endl;
        wl_registry_add_listener(registry, &registry_listener, &callback_data);

        // Process registry events to get globals
        std::cout << "DEBUG: About to process registry events" << std::endl;
        wl_display_flush(display);
        wl_display_dispatch(display);
        std::cout << "DEBUG: Registry events processed" << std::endl;

        // Copy results back from callback data
        std::cout << "DEBUG: Copying results from callback data" << std::endl;
        compositor = callback_data.compositor;
        shell = callback_data.shell;
        shm = callback_data.shm;
        xdg_wm_base = callback_data.xdg_wm_base;
        seat = callback_data.seat;
        std::cout << "DEBUG: compositor=" << (compositor ? "found" : "null") << std::endl;
        std::cout << "DEBUG: shell=" << (shell ? "found" : "null") << std::endl;
        std::cout << "DEBUG: shm=" << (shm ? "found" : "null") << std::endl;
        std::cout << "DEBUG: xdg_wm_base=" << (xdg_wm_base ? "found" : "null") << std::endl;
        std::cout << "DEBUG: seat=" << (seat ? "found" : "null") << std::endl;

        // Set up seat listener immediately if seat is available
        if (seat) {
            log("Setting up seat listener for input capabilities");
            wl_seat_add_listener(seat, &simple_seat_listener, &callback_data);

            // Process seat events without blocking
            wl_display_flush(display);
            wl_display_dispatch_pending(display);
            log("Seat listener setup complete");
        }
        
        // Initialize cursor theme
        if (shm) {
            log("Setting up cursor theme");
            cursor_theme = wl_cursor_theme_load(nullptr, 24, shm);
            if (cursor_theme) {
                default_cursor = wl_cursor_theme_get_cursor(cursor_theme, "default");
                if (!default_cursor) {
                    // Try alternative cursor names
                    default_cursor = wl_cursor_theme_get_cursor(cursor_theme, "left_ptr");
                }
                if (default_cursor && default_cursor->image_count > 0) {
                    cursor_surface = wl_compositor_create_surface(compositor);
                    log("Cursor theme initialized successfully");
                } else {
                    log("Warning: Could not load default cursor");
                }
            } else {
                log("Warning: Could not load cursor theme");
            }
        }

        // Check if we have required globals (shell is optional for now)
        if (!compositor || !shm) {
            log("Missing required Wayland globals (compositor or shm)");
            std::cout << "DEBUG: Missing required globals, failing" << std::endl;
            return false;
        }

        if (!shell) {
            log("Warning: wl_shell not available (deprecated interface)");
        }

        log("Wayland initialization complete");
        std::cout << "DEBUG: initWayland() completed successfully" << std::endl;
        return true;
    }

    void GUI::Impl::cleanupWayland() {
        // Only cleanup if we have something to clean up
        if (!display) {
            return; // Nothing to clean up
        }

        // Clean up XDG shell objects first
        if (xdg_toplevel) {
            xdg_toplevel_destroy(xdg_toplevel);
            xdg_toplevel = nullptr;
        }

        if (xdg_surface) {
            xdg_surface_destroy(xdg_surface);
            xdg_surface = nullptr;
        }

        if (xdg_wm_base) {
            xdg_wm_base_destroy(xdg_wm_base);
            xdg_wm_base = nullptr;
        }

        // Clean up cursor objects
        if (cursor_surface) {
            wl_surface_destroy(cursor_surface);
            cursor_surface = nullptr;
        }
        
        if (cursor_theme) {
            wl_cursor_theme_destroy(cursor_theme);
            cursor_theme = nullptr;
            default_cursor = nullptr; // Part of theme, don't destroy separately
        }

        // Clean up input objects
        if (keyboard) {
            wl_keyboard_destroy(keyboard);
            keyboard = nullptr;
        }

        if (pointer) {
            wl_pointer_destroy(pointer);
            pointer = nullptr;
        }

        if (seat) {
            wl_seat_destroy(seat);
            seat = nullptr;
        }

        // Clean up wl_shell objects
        if (shell_surface) {
            wl_shell_surface_destroy(shell_surface);
            shell_surface = nullptr;
        }

        if (shell) {
            wl_shell_destroy(shell);
            shell = nullptr;
        }

        // Clean up surface
        if (surface) {
            wl_surface_destroy(surface);
            surface = nullptr;
        }

        // Clean up other globals
        if (compositor) {
            wl_compositor_destroy(compositor);
            compositor = nullptr;
        }

        if (shm) {
            wl_shm_destroy(shm);
            shm = nullptr;
        }

        if (registry) {
            wl_registry_destroy(registry);
            registry = nullptr;
        }

        if (display) {
            wl_display_disconnect(display);
            display = nullptr;
        }

        log("Wayland cleanup complete");
    }

    bool GUI::Impl::createWaylandWindow() {
        if (!compositor) {
            log("Compositor not available");
            return false;
        }

        // Create surface
        surface = wl_compositor_create_surface(compositor);
        if (!surface) {
            log("Failed to create Wayland surface");
            return false;
        }
        log("Created Wayland surface");

        // Set up callback data for window resize handling
        callback_data.compositor = compositor;
        callback_data.shell = shell;
        callback_data.shm = shm;
        callback_data.xdg_wm_base = xdg_wm_base;
        callback_data.seat = seat;
        callback_data.log_func = [this](const std::string& msg) { this->log(msg); };
        callback_data.surface = surface;
        callback_data.buffer_ptr = &buffer;
        callback_data.shm_data_ptr = &shm_data;
        callback_data.shm_fd_ptr = &shm_fd;
        callback_data.current_width = &info.window_width;
        callback_data.current_height = &info.window_height;
        callback_data.needs_resize = &needs_resize;
        callback_data.cursor_theme = cursor_theme;
        callback_data.default_cursor = default_cursor;
        callback_data.cursor_surface = cursor_surface;
        callback_data.debug_mode = debug_input;

        // Use xdg_shell if available (modern), fall back to wl_shell (deprecated)
        if (xdg_wm_base) {
            log("Using XDG shell (modern)");

            // Add xdg_wm_base listener
            xdg_wm_base_add_listener(xdg_wm_base, &xdg_wm_base_listener, &callback_data);

            // Create xdg_surface
            xdg_surface = xdg_wm_base_get_xdg_surface(xdg_wm_base, surface);
            if (!xdg_surface) {
                log("Failed to create XDG surface");
                return false;
            }
            log("Created XDG surface");

            // Add xdg_surface listener
            xdg_surface_add_listener(xdg_surface, &xdg_surface_listener, &callback_data);

            // Create xdg_toplevel
            xdg_toplevel = xdg_surface_get_toplevel(xdg_surface);
            if (!xdg_toplevel) {
                log("Failed to create XDG toplevel");
                return false;
            }
            log("Created XDG toplevel");

            // Add xdg_toplevel listener
            xdg_toplevel_add_listener(xdg_toplevel, &xdg_toplevel_listener, &callback_data);

            // Store xdg_toplevel in callback data for resize operations
            callback_data.xdg_toplevel = xdg_toplevel;

            // Set window properties
            xdg_toplevel_set_title(xdg_toplevel, info.window_title.c_str());
            xdg_toplevel_set_app_id(xdg_toplevel, "com.openterface.openterfaceQT");

            // Make window resizable with reasonable constraints
            // Set minimum size (don't go smaller than this)
            xdg_toplevel_set_min_size(xdg_toplevel, 640, 480);
            
            // Set maximum size (0, 0 means no maximum - fully resizable)
            xdg_toplevel_set_max_size(xdg_toplevel, 0, 0);
            
            // Start with a reasonable default size instead of maximized
            // The window manager will respect this as a size hint

            // Commit surface to trigger initial configure
            wl_surface_commit(surface);

            // Process initial configure events without blocking
            wl_display_flush(display);
            wl_display_dispatch_pending(display);

            // Create buffer with content
            if (!createBuffer(info.window_width, info.window_height)) {
                log("Failed to create buffer");
                return false;
            }

            // Attach buffer and commit
            wl_surface_attach(surface, buffer, 0, 0);
            wl_surface_damage(surface, 0, 0, info.window_width, info.window_height);
            wl_surface_commit(surface);

            // Set up input if seat is available
            if (seat) {
                log("Input capture system ready - move mouse over window and type keys to test");
            } else {
                log("Warning: No input seat available - input capture disabled");
            }

        } else if (shell) {
            log("Using wl_shell (deprecated)");

            // Create shell surface only if shell is available
            shell_surface = wl_shell_get_shell_surface(shell, surface);
            if (!shell_surface) {
                log("Failed to create shell surface");
                return false;
            }
            log("Created shell surface");

            // Set shell surface as toplevel
            wl_shell_surface_set_toplevel(shell_surface);
            wl_shell_surface_set_title(shell_surface, info.window_title.c_str());

            // Commit surface
            wl_surface_commit(surface);

        } else {
            log("No shell interface available - cannot create window");
            return false;
        }

        // Flush display to send all requests
        wl_display_flush(display);

        info.window_created = true;
        log("Wayland window created successfully");
        return true;
    }

    bool GUI::Impl::createBuffer(int width, int height) {
        // Validate dimensions
        if (width <= 0 || height <= 0) {
            log("Invalid buffer dimensions: " + std::to_string(width) + "x" + std::to_string(height));
            return false;
        }
        
        // Reasonable size limits to prevent excessive memory usage
        if (width > 8192 || height > 8192) {
            log("Buffer dimensions too large: " + std::to_string(width) + "x" + std::to_string(height));
            return false;
        }
        
        int stride = width * 4; // RGBA32
        int size = stride * height;
        
        log("Creating buffer: " + std::to_string(width) + "x" + std::to_string(height) + 
            " (stride=" + std::to_string(stride) + ", size=" + std::to_string(size) + " bytes)");

        // Create shared memory file
        shm_fd = create_memfd("openterface-buffer", MFD_CLOEXEC);
        if (shm_fd == -1) {
            log("Failed to create memfd: " + std::string(strerror(errno)));
            return false;
        }

        if (ftruncate(shm_fd, size) == -1) {
            log("Failed to truncate memfd: " + std::string(strerror(errno)));
            close(shm_fd);
            shm_fd = -1;
            return false;
        }

        // Map memory
        shm_data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
        if (shm_data == MAP_FAILED) {
            log("Failed to mmap buffer: " + std::string(strerror(errno)));
            close(shm_fd);
            shm_fd = -1;
            return false;
        }

        // Create wl_shm_pool and buffer
        struct wl_shm_pool *pool = wl_shm_create_pool(shm, shm_fd, size);
        if (!pool) {
            log("Failed to create shm pool");
            destroyBuffer();
            return false;
        }

        buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride, WL_SHM_FORMAT_XRGB8888);
        wl_shm_pool_destroy(pool);

        if (!buffer) {
            log("Failed to create buffer");
            destroyBuffer();
            return false;
        }

        // Fill buffer with video frame data or gradient pattern
        uint32_t *pixels = static_cast<uint32_t *>(shm_data);
        
        {
            std::lock_guard<std::mutex> lock(frame_mutex);
            
            if (has_new_frame && !current_frame.empty() && frame_width > 0 && frame_height > 0) {
                // Display indicator that video frames are being received
                log("Rendering video frame indicator: " + std::to_string(frame_width) + "x" + std::to_string(frame_height) + 
                    " (" + std::to_string(current_frame.size()) + " bytes MJPEG)");
                
                // Create a visual indicator that video is being received
                // This creates a animated pattern to show frames are coming in
                static uint8_t frame_counter = 0;
                frame_counter += 10;
                
                for (int y = 0; y < height; y++) {
                    for (int x = 0; x < width; x++) {
                        int dst_idx = y * width + x;
                        
                        // Create a pattern that changes with each frame to show video activity
                        uint8_t red = static_cast<uint8_t>((x + frame_counter) % 256);
                        uint8_t green = static_cast<uint8_t>((y + frame_counter) % 256);
                        uint8_t blue = frame_counter;
                        
                        // XRGB8888 format: 0xAARRGGBB
                        pixels[dst_idx] = (0xFF << 24) | (red << 16) | (green << 8) | blue;
                    }
                }
                
                has_new_frame = false; // Mark frame as processed
                log("Video frame indicator rendered (MJPEG decoding needed for actual video)");
            } else {
                // Fill with gradient pattern as fallback
                for (int y = 0; y < height; y++) {
                    for (int x = 0; x < width; x++) {
                        int idx = y * width + x;
                        
                        // Create a gradient pattern to help visualize scaling
                        uint8_t red = static_cast<uint8_t>((x * 255) / (width - 1));
                        uint8_t green = static_cast<uint8_t>((y * 255) / (height - 1));
                        uint8_t blue = 64; // Constant blue component
                        
                        // XRGB8888 format: 0xAARRGGBB
                        pixels[idx] = (0xFF << 24) | (red << 16) | (green << 8) | blue;
                    }
                }
                log("No video frame available, using gradient pattern");
            }
        }

        log("Buffer created successfully");
        return true;
    }

    void GUI::Impl::destroyBuffer() {
        std::lock_guard<std::mutex> lock(resize_mutex);
        log("Destroying buffer...");
        
        // Destroy Wayland buffer first
        if (buffer) {
            wl_buffer_destroy(buffer);
            buffer = nullptr;
            log("Wayland buffer destroyed");
        }

        // Unmap shared memory using the original size from when it was mapped
        if (shm_data && shm_data != MAP_FAILED) {
            // Use fstat to get the actual file size instead of calculating from window dimensions
            struct stat sb;
            if (shm_fd != -1 && fstat(shm_fd, &sb) == 0) {
                int result = munmap(shm_data, sb.st_size);
                if (result != 0) {
                    log("Warning: munmap failed: " + std::string(strerror(errno)));
                } else {
                    log("Shared memory unmapped successfully");
                }
            } else {
                log("Warning: Cannot determine buffer size, attempting graceful cleanup");
                // Fallback: unmap with current calculated size but this is less safe
                int buffer_size = info.window_width * info.window_height * 4;
                if (buffer_size > 0) {
                    munmap(shm_data, buffer_size);
                }
            }
            shm_data = nullptr;
        }

        // Close file descriptor
        if (shm_fd != -1) {
            close(shm_fd);
            shm_fd = -1;
            log("File descriptor closed");
        }
        
        log("Buffer destruction complete");
    }

    void GUI::Impl::onVideoFrame(const FrameData& frame) {
        std::lock_guard<std::mutex> lock(frame_mutex);
        
        // Store the frame data
        if (frame.data && frame.size > 0) {
            static int frame_count = 0;
            frame_count++;
            
            // Only log every 30 frames to reduce spam
            if (frame_count % 30 == 1) {
                log("Video frame " + std::to_string(frame_count) + ": " + std::to_string(frame.width) + "x" + std::to_string(frame.height) + 
                    " size=" + std::to_string(frame.size) + " bytes");
            }
            
            current_frame.resize(frame.size);
            memcpy(current_frame.data(), frame.data, frame.size);
            frame_width = frame.width;
            frame_height = frame.height;
            has_new_frame = true;
            
            // Force buffer recreation and surface update
            needs_resize = true;
        }
    }

    // Wayland registry callbacks
    static void registry_global(void *data, struct wl_registry *registry, uint32_t id, const char *interface,
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

    static void registry_global_remove(void *data, struct wl_registry *registry, uint32_t id) {
        // Handle global removal if needed
    }

} // namespace openterface
