#include "openterface/gui.hpp"
#include "openterface/input.hpp"
#include "openterface/serial.hpp"
#include "openterface/video.hpp"
#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <thread>

// Wayland includes
#include "wayland/xdg-shell-client-protocol.h"
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>

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
    };

    // XDG shell callbacks
    static void xdg_wm_base_ping(void *data, struct xdg_wm_base *xdg_wm_base, uint32_t serial) {
        xdg_wm_base_pong(xdg_wm_base, serial);
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
        if (callback_data->log_func) {
            std::string msg = "XDG toplevel configured: " + std::to_string(width) + "x" + std::to_string(height);
            callback_data->log_func(msg);
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

        // Input state
        bool mouse_over_surface = false;
        bool input_grabbed = false;

        // Buffer for rendering
        struct wl_buffer *buffer = nullptr;
        void *shm_data = nullptr;
        int shm_fd = -1;

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

    // Pointer callbacks for debugging
    static void debug_pointer_enter(void *data, struct wl_pointer *pointer, uint32_t serial,
                                    struct wl_surface *surface, wl_fixed_t sx, wl_fixed_t sy) {
        auto *callback_data = static_cast<WaylandCallbackData *>(data);
        callback_data->mouse_over = true;
        if (callback_data->log_func) {
            callback_data->log_func("🎯 DEBUG: pointer_enter callback called!");
            callback_data->log_func("🖱️  Mouse ENTERED window");
        }
    }

    static void debug_pointer_leave(void *data, struct wl_pointer *pointer, uint32_t serial,
                                    struct wl_surface *surface) {
        auto *callback_data = static_cast<WaylandCallbackData *>(data);
        callback_data->mouse_over = false;
        if (callback_data->log_func) {
            callback_data->log_func("🖱️  Mouse LEFT window");
        }
    }

    static void debug_pointer_motion(void *data, struct wl_pointer *pointer, uint32_t time,
                                     wl_fixed_t sx, wl_fixed_t sy) {
        auto *callback_data = static_cast<WaylandCallbackData *>(data);
        if (callback_data->mouse_over && callback_data->log_func) {
            int x = wl_fixed_to_int(sx);
            int y = wl_fixed_to_int(sy);
            std::string msg = "🖱️  Mouse motion: (" + std::to_string(x) + ", " + std::to_string(y) + ")";
            callback_data->log_func(msg);
        }
    }

    static void debug_pointer_button(void *data, struct wl_pointer *pointer, uint32_t serial,
                                     uint32_t time, uint32_t button, uint32_t state) {
        auto *callback_data = static_cast<WaylandCallbackData *>(data);
        if (callback_data->mouse_over && callback_data->log_func) {
            const char *action = (state == WL_POINTER_BUTTON_STATE_PRESSED) ? "PRESSED" : "RELEASED";
            const char *btn_name = "";
            switch(button) {
                case 0x110: btn_name = "LEFT"; break;
                case 0x111: btn_name = "RIGHT"; break;
                case 0x112: btn_name = "MIDDLE"; break;
                default: btn_name = "UNKNOWN"; break;
            }
            std::string msg = "🖱️  Mouse " + std::string(btn_name) + " button " + action;
            callback_data->log_func(msg);
        }
    }

    static void debug_pointer_axis(void *data, struct wl_pointer *pointer, uint32_t time,
                                   uint32_t axis, wl_fixed_t value) {
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

    static void debug_pointer_axis_relative_direction(void *data, struct wl_pointer *pointer, uint32_t axis, uint32_t direction) {
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
    static void debug_keyboard_keymap(void *data, struct wl_keyboard *keyboard, uint32_t format,
                                      int32_t fd, uint32_t size) {
        close(fd); // Just close the keymap fd for now
    }

    static void debug_keyboard_enter(void *data, struct wl_keyboard *keyboard, uint32_t serial,
                                     struct wl_surface *surface, struct wl_array *keys) {
        auto *callback_data = static_cast<WaylandCallbackData *>(data);
        callback_data->input_active = true;
        if (callback_data->log_func) {
            callback_data->log_func("⌨️  Keyboard FOCUS gained - capturing input");
        }
    }

    static void debug_keyboard_leave(void *data, struct wl_keyboard *keyboard, uint32_t serial,
                                     struct wl_surface *surface) {
        auto *callback_data = static_cast<WaylandCallbackData *>(data);
        callback_data->input_active = false;
        if (callback_data->log_func) {
            callback_data->log_func("⌨️  Keyboard FOCUS lost - input released");
        }
    }

    static void debug_keyboard_key(void *data, struct wl_keyboard *keyboard, uint32_t serial,
                                   uint32_t time, uint32_t key, uint32_t state) {
        auto *callback_data = static_cast<WaylandCallbackData *>(data);
        if (callback_data->input_active && callback_data->log_func) {
            const char *action = (state == WL_KEYBOARD_KEY_STATE_PRESSED) ? "PRESSED" : "RELEASED";
            std::string msg = "⌨️  Key " + std::to_string(key) + " " + action;
            callback_data->log_func(msg);
        }
    }

    static void debug_keyboard_modifiers(void *data, struct wl_keyboard *keyboard, uint32_t serial,
                                         uint32_t mods_depressed, uint32_t mods_latched,
                                         uint32_t mods_locked, uint32_t group) {
        auto *callback_data = static_cast<WaylandCallbackData *>(data);
        if (callback_data->input_active && callback_data->log_func && 
            (mods_depressed || mods_latched || mods_locked)) {
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
        debug_keyboard_keymap,
        debug_keyboard_enter,
        debug_keyboard_leave,
        debug_keyboard_key,
        debug_keyboard_modifiers,
        debug_keyboard_repeat_info,
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

        // In a real implementation, this would:
        // 1. Set up frame callback from video source
        // 2. Create rendering surface (OpenGL/Vulkan/etc.)
        // 3. Start rendering loop

        pImpl->info.video_displayed = true;
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

        // Wayland event loop
        while (!pImpl->exit_requested && pImpl->display) {
            if (wl_display_dispatch(pImpl->display) == -1) {
                pImpl->log("Wayland display dispatch failed");
                break;
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

        // Roundtrip to get globals
        std::cout << "DEBUG: About to do roundtrip" << std::endl;
        wl_display_roundtrip(display);
        std::cout << "DEBUG: Roundtrip completed" << std::endl;

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
            
            // Trigger another roundtrip to get seat capabilities
            wl_display_roundtrip(display);
            log("Seat listener setup complete");
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

            // Set window properties
            xdg_toplevel_set_title(xdg_toplevel, info.window_title.c_str());
            xdg_toplevel_set_app_id(xdg_toplevel, "com.openterface.openterfaceQT");

            // Maximize the window
            xdg_toplevel_set_maximized(xdg_toplevel);

            // Set minimum size to ensure window is visible
            xdg_toplevel_set_min_size(xdg_toplevel, 800, 600);

            // Commit surface to trigger initial configure
            wl_surface_commit(surface);

            // Wait for configure event
            wl_display_roundtrip(display);

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
        int stride = width * 4; // RGBA32
        int size = stride * height;

        // Create shared memory file
        shm_fd = create_memfd("openterface-buffer", MFD_CLOEXEC);
        if (shm_fd == -1) {
            log("Failed to create memfd");
            return false;
        }

        if (ftruncate(shm_fd, size) == -1) {
            log("Failed to truncate memfd");
            close(shm_fd);
            shm_fd = -1;
            return false;
        }

        // Map memory
        shm_data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
        if (shm_data == MAP_FAILED) {
            log("Failed to mmap buffer");
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

        // Fill buffer with dark gray color (so it's visible)
        uint32_t *pixels = static_cast<uint32_t *>(shm_data);
        for (int i = 0; i < width * height; i++) {
            pixels[i] = 0xFF404040; // Dark gray (ARGB format)
        }

        log("Buffer created successfully");
        return true;
    }

    void GUI::Impl::destroyBuffer() {
        if (buffer) {
            wl_buffer_destroy(buffer);
            buffer = nullptr;
        }

        if (shm_data && shm_data != MAP_FAILED) {
            munmap(shm_data, info.window_width * info.window_height * 4);
            shm_data = nullptr;
        }

        if (shm_fd != -1) {
            close(shm_fd);
            shm_fd = -1;
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
