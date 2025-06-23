#include "openterface/gui.hpp"
#include "openterface/gui_wayland.hpp"
#include "openterface/gui_input.hpp"
#include "openterface/gui_video.hpp"
#include "openterface/gui_threading.hpp"
#include "openterface/gpu_video_renderer.hpp"
#include "openterface/input.hpp"
#include "openterface/serial.hpp"
#include "openterface/video.hpp"
#include "openterface/jpeg_decoder.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>

// Wayland includes
#include "wayland/xdg-shell-client-protocol.h"
#include <cstring>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>
#include <wayland-cursor.h>

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
        
        // Buffer dimensions (separate from window dimensions)
        int buffer_width = 0;
        int buffer_height = 0;

        // Synchronization
        std::mutex resize_mutex;
        std::atomic<bool> resize_in_progress{false};
        std::chrono::steady_clock::time_point last_resize_time;

        // Video frame storage
        VideoFrame current_frame;
        std::mutex frame_mutex;
        bool has_new_frame = false;

        // Debug mode
        bool debug_input = false;

        // Video processor for frame decoding
        VideoProcessor video_processor;
        
        // GPU-accelerated video renderer (like QT)
        GPUVideoRenderer gpu_renderer;
        bool use_gpu_acceleration = true;
        
        // Thread-safe surface update queue
        SurfaceUpdateQueue surface_update_queue;

        // Thread management
        ThreadManager thread_manager;
        void *render_buffer = nullptr;
        
        // Inter-thread communication queues
        std::queue<InputEvent> input_queue;
        std::mutex input_queue_mutex;

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
        void onVideoFrame(const FrameData &frame);
        void renderThreadFunction();
        void waylandEventThreadFunction();
        void inputThreadFunction();
        void queueInputEvent(const InputEvent& event);
        void processSurfaceUpdates();
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
        pImpl->video->setFrameCallback([this](const FrameData &frame) { pImpl->onVideoFrame(frame); });

        // Start video capture
        if (!pImpl->video->startCapture()) {
            pImpl->log("Failed to start video capture");
            return false;
        }

        pImpl->info.video_displayed = true;
        
        // Allocate render buffer before starting thread
        if (pImpl->buffer_width > 0 && pImpl->buffer_height > 0) {
            size_t buffer_size = pImpl->buffer_width * pImpl->buffer_height * 4;
            pImpl->render_buffer = malloc(buffer_size);
            if (!pImpl->render_buffer) {
                pImpl->log("Failed to allocate render buffer");
                return false;
            }
        }
        
        // Start render thread for non-blocking frame processing
        pImpl->thread_manager.startRenderThread([this]() { pImpl->renderThreadFunction(); });
        
        pImpl->log("Video display and capture started successfully");
        return true;
    }

    void GUI::stopVideoDisplay() {
        if (pImpl->info.video_displayed) {
            pImpl->log("Stopping video display");
            
            // Stop render thread
            pImpl->thread_manager.stopRenderThread();
            
            // Free render buffer
            if (pImpl->render_buffer) {
                free(pImpl->render_buffer);
                pImpl->render_buffer = nullptr;
            }
            
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
        pImpl->log("Starting application main thread (4-thread architecture)");

        // Start all specialized threads
        pImpl->thread_manager.startWaylandEventThread([this]() { pImpl->waylandEventThreadFunction(); });
        pImpl->thread_manager.startInputThread([this]() { pImpl->inputThreadFunction(); });
        // Render thread already started in startVideoDisplay()
        // Serial thread already started via connectAsync()

        pImpl->log("All threads started - application running");

        // Simplified application main thread - just handle buffer swaps and exit condition
        auto last_frame_time = std::chrono::steady_clock::now();
        const auto frame_duration = std::chrono::milliseconds(16); // Target 60 FPS

        // Application control loop (much simpler than Wayland event loop)
        while (!pImpl->exit_requested && pImpl->display) {
            auto now = std::chrono::steady_clock::now();

            // Simplified application thread - just handle buffer swaps
            if (now - last_frame_time > frame_duration) {
                if (pImpl->surface && pImpl->buffer && pImpl->shm_data) {
                    // Lightweight frame update - just swap buffers if render thread completed processing
                    if (pImpl->thread_manager.buffer_swap_ready.load()) {
                        // Quick buffer swap - no heavy processing in main thread
                        if (pImpl->render_buffer && pImpl->shm_data && 
                            pImpl->buffer_width > 0 && pImpl->buffer_height > 0) {
                            
                            size_t buffer_size = pImpl->buffer_width * pImpl->buffer_height * 4;
                            
                            // Fast memory copy from render buffer to display buffer
                            memcpy(pImpl->shm_data, pImpl->render_buffer, buffer_size);
                            
                            static int swap_count = 0;
                            if (++swap_count % 30 == 1) {
                                pImpl->log("Buffer swap #" + std::to_string(swap_count) + " (render thread -> display)");
                            }
                            
                            // Leave buffer_swap_ready = true so Wayland thread can commit the surface
                        }
                    }
                }
                last_frame_time = now;
            }
            
            // Short sleep to avoid busy waiting while keeping responsive
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        // Stop all threads before exiting
        pImpl->thread_manager.stopWaylandEventThread();
        pImpl->thread_manager.stopInputThread();
        // Render thread will be stopped in stopVideoDisplay()
        
        pImpl->log("Application main thread exited - all threads stopped");
        return 0;
    }

    void GUI::requestExit() {
        pImpl->exit_requested = true;
        pImpl->log("Exit requested");
    }

    GUIInfo GUI::getInfo() const { return pImpl->info; }

    void GUI::setDebugMode(bool enabled) {
        pImpl->debug_input = enabled;
        pImpl->callback_data.debug_mode = enabled;  // Update callback data too
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
        callback_data.debug_mode = debug_input;  // Pass debug mode to callback data

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
            // Store pointer locations for callback data
            callback_data.pointer_ptr = &pointer;
            callback_data.keyboard_ptr = &keyboard;
            
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
        // Cleanup GPU renderer first
        if (use_gpu_acceleration) {
            gpu_renderer.cleanup();
            log("GPU renderer cleaned up");
        }
        
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
        callback_data.log_func = [this](const std::string &msg) { this->log(msg); };
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
        callback_data.pointer_ptr = &pointer;
        callback_data.keyboard_ptr = &keyboard;
        callback_data.input_ptr = &input;
        callback_data.serial_ptr = &serial;

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

            // Initialize GPU acceleration if available
            if (use_gpu_acceleration) {
                log("Initializing GPU-accelerated video rendering...");
                if (gpu_renderer.initialize(display, surface, info.window_width, info.window_height)) {
                    log("GPU acceleration enabled (like QT)");
                    // Skip CPU buffer creation when using GPU
                } else {
                    log("GPU acceleration failed, falling back to CPU rendering: " + gpu_renderer.getLastError());
                    use_gpu_acceleration = false;
                }
            }

            // Create CPU buffer only if not using GPU acceleration
            if (!use_gpu_acceleration) {
                if (!createBuffer(info.window_width, info.window_height)) {
                    log("Failed to create buffer");
                    return false;
                }
            }

            // Attach buffer and commit (only for CPU rendering)
            if (!use_gpu_acceleration && buffer) {
                wl_surface_attach(surface, buffer, 0, 0);
                wl_surface_damage(surface, 0, 0, info.window_width, info.window_height);
                wl_surface_commit(surface);
            }

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
        log("DEBUG: createBuffer called! " + std::to_string(width) + "x" + std::to_string(height));
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
        
        // Store actual buffer dimensions
        buffer_width = width;
        buffer_height = height;

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

            if (has_new_frame && current_frame.is_rgb && !current_frame.data.empty() && 
                current_frame.width > 0 && current_frame.height > 0) {
                log("Rendering decoded video frame: " + std::to_string(current_frame.width) + "x" +
                    std::to_string(current_frame.height) + " (" + std::to_string(current_frame.data.size()) + " bytes RGB)");

                renderVideoToBuffer(pixels, width, height, current_frame);
                log("RGB video frame rendered successfully with scaling");
                has_new_frame = false; // Mark frame as processed
            } else {
                // Fill with black background
                fillBufferWithBlack(pixels, width, height);
                log("No video frame available, using black background");
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
        
        // Clear buffer dimensions
        buffer_width = 0;
        buffer_height = 0;

        log("Buffer destruction complete");
    }

    void GUI::Impl::onVideoFrame(const FrameData &frame) {
        std::lock_guard<std::mutex> lock(frame_mutex);

        // ALWAYS clear previous frame data first to prevent stale data issues
        has_new_frame = false;  // Set this FIRST to stop any rendering immediately
        current_frame.data.clear();
        current_frame.width = 0;
        current_frame.height = 0;
        current_frame.is_rgb = false;

        // Store the frame data
        if (frame.data && frame.size > 0) {
            static int frame_count = 0;
            frame_count++;

            // Only log every 30 frames to reduce spam
            if (frame_count % 30 == 1) {
                log("Video frame " + std::to_string(frame_count) + ": " + std::to_string(frame.width) + "x" +
                    std::to_string(frame.height) + " size=" + std::to_string(frame.size) + " bytes");
            }

            // Process the frame using VideoProcessor
            if (video_processor.processFrame(frame, current_frame)) {
                has_new_frame = true;
                
                // Signal rendering thread to process the frame
                thread_manager.frame_ready_for_render = true;
                thread_manager.notifyRender();
                
                if (frame_count % 30 == 1) {
                    log("MJPEG frame decoded successfully: " + std::to_string(current_frame.width) + "x" + 
                        std::to_string(current_frame.height) + " RGB");
                }
            } else {
                // MJPEG decode failed
                log("MJPEG decode failed: " + video_processor.getLastError());
                has_new_frame = false;
            }

            // Don't force buffer recreation - let event loop handle updates
            // needs_resize = true;
        }
    }

    void GUI::Impl::renderThreadFunction() {
        log("Rendering thread started (optimized for low latency)");
        
        // Initialize GPU context in this thread if needed
        bool gpu_initialized_in_thread = false;
        if (use_gpu_acceleration && gpu_renderer.isInitialized()) {
            if (gpu_renderer.initializeInCurrentThread()) {
                log("GPU context initialized in render thread");
                gpu_initialized_in_thread = true;
            } else {
                log("GPU context initialization failed in render thread: " + gpu_renderer.getLastError());
                use_gpu_acceleration = false;
            }
        }
        
        while (thread_manager.render_thread_running.load()) {
            std::unique_lock<std::mutex> lock(thread_manager.render_mutex);
            
            // Use timeout to prevent blocking and check for frames regularly
            auto timeout = std::chrono::milliseconds(1);  // 1ms timeout for responsive rendering
            thread_manager.render_cv.wait_for(lock, timeout, [this] { 
                return thread_manager.frame_ready_for_render.load() || !thread_manager.render_thread_running.load(); 
            });
            
            if (!thread_manager.render_thread_running.load()) break;
            if (!thread_manager.frame_ready_for_render.load()) continue;
            
            // Process the frame in this background thread with minimal locking
            {
                std::lock_guard<std::mutex> frame_lock(frame_mutex);
                
                if (has_new_frame && current_frame.is_rgb && !current_frame.data.empty() && 
                    current_frame.width > 0 && current_frame.height > 0) {
                    
                    if (use_gpu_acceleration && gpu_initialized_in_thread) {
                        // GPU-accelerated rendering (like QT) - much faster!
                        if (gpu_renderer.renderFrame(current_frame)) {
                            // GPU rendering is complete, no need for buffer swap
                            has_new_frame = false;
                        } else {
                            log("GPU rendering failed: " + gpu_renderer.getLastError());
                        }
                    } else if (shm_data && render_buffer) {
                        // CPU fallback rendering
                        renderVideoToBuffer(render_buffer, buffer_width, buffer_height, current_frame);
                        
                        // Signal that buffer is ready for swap IMMEDIATELY
                        thread_manager.buffer_swap_ready = true;
                        has_new_frame = false;
                    }
                }
            }
            
            // Clear frame ready flag IMMEDIATELY after processing
            thread_manager.frame_ready_for_render = false;
        }
        
        log("Rendering thread stopped");
    }


    void GUI::Impl::waylandEventThreadFunction() {
        // CRITICAL: This thread must handle ALL Wayland events but NOT block on serial I/O
        // Also prioritize video frame updates for smooth 30fps display
        
        while (thread_manager.wayland_thread_running.load() && display) {
            
            // Handle all Wayland events (ping-pong, input, etc) - this is critical for responsiveness
            wl_display_dispatch_pending(display);
            wl_display_flush(display);
            
            // PRIORITY: Check for video frame updates FREQUENTLY for smooth 30fps
            // Only handle CPU buffer swaps (GPU renders directly to surface)
            if (!use_gpu_acceleration && thread_manager.buffer_swap_ready.load() && surface && buffer) {
                wl_surface_attach(surface, buffer, 0, 0);
                wl_surface_damage(surface, 0, 0, buffer_width, buffer_height);
                wl_surface_commit(surface);
                thread_manager.buffer_swap_ready = false;
                
                // Immediately flush after video frame commit for minimal latency
                wl_display_flush(display);
            }
            
            // Poll for new events with very short timeout to maintain responsiveness
            struct pollfd pfd;
            pfd.fd = wl_display_get_fd(display);
            pfd.events = POLLIN;
            
            // Use non-blocking poll first
            if (poll(&pfd, 1, 0) > 0) {
                wl_display_dispatch(display);
            }
            
            // Ultra-minimal sleep to allow 30fps video updates (33ms per frame)
            // Use nanosleep for sub-millisecond timing
            struct timespec ts = {0, 1000000}; // 1ms sleep = 1000fps max update rate
            nanosleep(&ts, nullptr);
        }
    }

    void GUI::Impl::processSurfaceUpdates() {
        // Process any pending surface update requests
        SurfaceCommitRequest request;
        while (surface_update_queue.pop(request)) {
            switch (request.type) {
                case SurfaceCommitRequest::ATTACH_BUFFER:
                    if (surface && buffer) {
                        wl_surface_attach(surface, buffer, 0, 0);
                    }
                    break;
                case SurfaceCommitRequest::DAMAGE:
                    if (surface) {
                        wl_surface_damage(surface, request.x, request.y, request.width, request.height);
                    }
                    break;
                case SurfaceCommitRequest::COMMIT:
                    if (surface) {
                        wl_surface_commit(surface);
                    }
                    break;
            }
        }
    }


    void GUI::Impl::inputThreadFunction() {
        log("Input processing thread started");
        
        // Track last processed mouse position
        int last_processed_x = 0;
        int last_processed_y = 0;
        
        while (thread_manager.input_thread_running.load()) {
            
            // Poll current mouse position from callback data (non-blocking)
            if (callback_data.mouse_over && callback_data.input_active) {
                int current_x = callback_data.last_mouse_x;
                int current_y = callback_data.last_mouse_y;
                
                // Check if position changed
                if (current_x != last_processed_x || current_y != last_processed_y) {
                    int delta_x = current_x - last_processed_x;
                    int delta_y = current_y - last_processed_y;
                    
                    // Forward mouse movement (this can block but it's in separate thread)
                    if (serial && serial->isConnected() && (delta_x != 0 || delta_y != 0)) {
                        serial->sendMouseMove(delta_x, delta_y, false);
                        if (debug_input) {
                            log("[INPUT] Mouse motion forwarded: (" + 
                                std::to_string(delta_x) + ", " + std::to_string(delta_y) + ")");
                        }
                    }
                    
                    last_processed_x = current_x;
                    last_processed_y = current_y;
                }
            }
            
            // Sleep to avoid busy waiting
            std::this_thread::sleep_for(std::chrono::milliseconds(5)); // 200Hz polling
        }
        
        log("Input processing thread stopped");
    }

    void GUI::Impl::queueInputEvent(const InputEvent& event) {
        {
            std::lock_guard<std::mutex> lock(input_queue_mutex);
            input_queue.push(event);
        }
        thread_manager.notifyInput();
    }

    // Wayland registry callbacks

} // namespace openterface
