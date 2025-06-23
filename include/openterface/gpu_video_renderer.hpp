#pragma once

#include <wayland-client.h>
#include <wayland-egl.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <memory>
#include <string>

namespace openterface {

    struct VideoFrame;

    class GPUVideoRenderer {
    public:
        GPUVideoRenderer();
        ~GPUVideoRenderer();

        // Initialize EGL context with Wayland surface
        bool initialize(struct wl_display* display, struct wl_surface* surface, int width, int height);
        
        // Initialize EGL context in current thread (for threading)
        bool initializeInCurrentThread();
        
        // Render video frame using GPU acceleration
        bool renderFrame(const VideoFrame& frame);
        
        // Resize the rendering surface
        bool resize(int width, int height);
        
        // Cleanup
        void cleanup();
        
        // Check if GPU acceleration is available
        bool isInitialized() const { return initialized; }
        
        // Get last error message
        const std::string& getLastError() const { return last_error; }

    private:
        bool setupEGL();
        bool createShaders();
        bool setupVertexBuffer();
        void printEGLError(const std::string& operation);
        void printGLError(const std::string& operation);

        // EGL context
        EGLDisplay egl_display = EGL_NO_DISPLAY;
        EGLContext egl_context = EGL_NO_CONTEXT;
        EGLSurface egl_surface = EGL_NO_SURFACE;
        EGLConfig egl_config = nullptr;
        
        // Wayland EGL
        struct wl_egl_window* egl_window = nullptr;
        struct wl_display* wayland_display = nullptr;
        struct wl_surface* wayland_surface = nullptr;
        
        // OpenGL resources
        GLuint shader_program = 0;
        GLuint texture = 0;
        GLuint vbo = 0;
        GLuint vao = 0;
        
        // Shader attribute/uniform locations
        GLint position_attr = -1;
        GLint texcoord_attr = -1;
        GLint texture_uniform = -1;
        
        // State
        bool initialized = false;
        bool context_created = false;
        int surface_width = 0;
        int surface_height = 0;
        std::string last_error;
    };

} // namespace openterface