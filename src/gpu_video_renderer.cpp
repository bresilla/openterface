#include "openterface/gpu_video_renderer.hpp"
#include "openterface/gui_video.hpp"
#include <iostream>
#include <cstring>

namespace openterface {

    // Simple vertex shader for rendering a textured quad
    const char* vertex_shader_source = R"(
        attribute vec2 position;
        attribute vec2 texcoord;
        varying vec2 v_texcoord;
        
        void main() {
            gl_Position = vec4(position, 0.0, 1.0);
            v_texcoord = texcoord;
        }
    )";

    // Simple fragment shader for video texture
    const char* fragment_shader_source = R"(
        precision mediump float;
        uniform sampler2D texture;
        varying vec2 v_texcoord;
        
        void main() {
            gl_FragColor = texture2D(texture, v_texcoord);
        }
    )";

    GPUVideoRenderer::GPUVideoRenderer() = default;

    GPUVideoRenderer::~GPUVideoRenderer() {
        cleanup();
    }

    bool GPUVideoRenderer::initialize(struct wl_display* display, struct wl_surface* surface, int width, int height) {
        wayland_display = display;
        wayland_surface = surface;
        surface_width = width;
        surface_height = height;

        if (!setupEGL()) {
            return false;
        }

        initialized = true;
        return true;
    }

    bool GPUVideoRenderer::initializeInCurrentThread() {
        if (!initialized || context_created) {
            return context_created;
        }

        // Make context current in this thread
        if (!eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context)) {
            printEGLError("eglMakeCurrent in initializeInCurrentThread");
            return false;
        }

        if (!createShaders()) {
            return false;
        }

        if (!setupVertexBuffer()) {
            return false;
        }

        // Create texture for video frames
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        printGLError("texture creation");

        context_created = true;
        return true;
    }

    bool GPUVideoRenderer::setupEGL() {
        // Get EGL display
        egl_display = eglGetDisplay((EGLNativeDisplayType)wayland_display);
        if (egl_display == EGL_NO_DISPLAY) {
            last_error = "Failed to get EGL display";
            return false;
        }

        // Initialize EGL
        EGLint major, minor;
        if (!eglInitialize(egl_display, &major, &minor)) {
            printEGLError("eglInitialize");
            return false;
        }

        std::cout << "[GPU] EGL version: " << major << "." << minor << std::endl;

        // Configure EGL
        EGLint config_attribs[] = {
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_RED_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE, 8,
            EGL_ALPHA_SIZE, 8,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
            EGL_NONE
        };

        EGLint num_configs;
        if (!eglChooseConfig(egl_display, config_attribs, &egl_config, 1, &num_configs) || num_configs == 0) {
            printEGLError("eglChooseConfig");
            return false;
        }

        // Create Wayland EGL window
        egl_window = wl_egl_window_create(wayland_surface, surface_width, surface_height);
        if (!egl_window) {
            last_error = "Failed to create Wayland EGL window";
            return false;
        }

        // Create EGL surface
        egl_surface = eglCreateWindowSurface(egl_display, egl_config, (EGLNativeWindowType)egl_window, nullptr);
        if (egl_surface == EGL_NO_SURFACE) {
            printEGLError("eglCreateWindowSurface");
            return false;
        }

        // Bind OpenGL ES API
        if (!eglBindAPI(EGL_OPENGL_ES_API)) {
            printEGLError("eglBindAPI");
            return false;
        }

        // Create EGL context
        EGLint context_attribs[] = {
            EGL_CONTEXT_CLIENT_VERSION, 2,
            EGL_NONE
        };

        egl_context = eglCreateContext(egl_display, egl_config, EGL_NO_CONTEXT, context_attribs);
        if (egl_context == EGL_NO_CONTEXT) {
            printEGLError("eglCreateContext");
            return false;
        }

        // Make context current temporarily to get info, then release for render thread
        if (!eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context)) {
            printEGLError("eglMakeCurrent");
            return false;
        }

        std::cout << "[GPU] OpenGL ES renderer: " << glGetString(GL_RENDERER) << std::endl;
        std::cout << "[GPU] OpenGL ES version: " << glGetString(GL_VERSION) << std::endl;
        
        // Release context so render thread can claim it
        eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

        return true;
    }

    bool GPUVideoRenderer::createShaders() {
        // Compile vertex shader
        GLuint vertex_shader = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vertex_shader, 1, &vertex_shader_source, nullptr);
        glCompileShader(vertex_shader);

        GLint compile_status;
        glGetShaderiv(vertex_shader, GL_COMPILE_STATUS, &compile_status);
        if (!compile_status) {
            GLchar info_log[512];
            glGetShaderInfoLog(vertex_shader, 512, nullptr, info_log);
            last_error = "Vertex shader compilation failed: " + std::string(info_log);
            return false;
        }

        // Compile fragment shader
        GLuint fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fragment_shader, 1, &fragment_shader_source, nullptr);
        glCompileShader(fragment_shader);

        glGetShaderiv(fragment_shader, GL_COMPILE_STATUS, &compile_status);
        if (!compile_status) {
            GLchar info_log[512];
            glGetShaderInfoLog(fragment_shader, 512, nullptr, info_log);
            last_error = "Fragment shader compilation failed: " + std::string(info_log);
            glDeleteShader(vertex_shader);
            return false;
        }

        // Create shader program
        shader_program = glCreateProgram();
        glAttachShader(shader_program, vertex_shader);
        glAttachShader(shader_program, fragment_shader);
        glLinkProgram(shader_program);

        GLint link_status;
        glGetProgramiv(shader_program, GL_LINK_STATUS, &link_status);
        if (!link_status) {
            GLchar info_log[512];
            glGetProgramInfoLog(shader_program, 512, nullptr, info_log);
            last_error = "Shader program linking failed: " + std::string(info_log);
            glDeleteShader(vertex_shader);
            glDeleteShader(fragment_shader);
            return false;
        }

        // Clean up shaders (they're linked into the program now)
        glDeleteShader(vertex_shader);
        glDeleteShader(fragment_shader);

        // Get attribute and uniform locations
        position_attr = glGetAttribLocation(shader_program, "position");
        texcoord_attr = glGetAttribLocation(shader_program, "texcoord");
        texture_uniform = glGetUniformLocation(shader_program, "texture");

        printGLError("shader creation");

        return true;
    }

    bool GPUVideoRenderer::setupVertexBuffer() {
        // Fullscreen quad vertices (position + texcoord)
        float vertices[] = {
            // Position   // TexCoord
            -1.0f, -1.0f,  0.0f, 1.0f,  // Bottom-left
             1.0f, -1.0f,  1.0f, 1.0f,  // Bottom-right
            -1.0f,  1.0f,  0.0f, 0.0f,  // Top-left
             1.0f,  1.0f,  1.0f, 0.0f,  // Top-right
        };

        glGenBuffers(1, &vbo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

        printGLError("vertex buffer setup");

        return true;
    }

    bool GPUVideoRenderer::renderFrame(const VideoFrame& frame) {
        if (!initialized || !context_created || !frame.is_rgb || frame.data.empty()) {
            return false;
        }

        // Context should already be current in this thread
        // No need to call eglMakeCurrent again

        // Set viewport
        glViewport(0, 0, surface_width, surface_height);

        // Clear screen
        glClear(GL_COLOR_BUFFER_BIT);

        // Upload video frame to texture
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, frame.width, frame.height, 0, GL_RGB, GL_UNSIGNED_BYTE, frame.data.data());

        // Use shader program
        glUseProgram(shader_program);

        // Bind vertex buffer
        glBindBuffer(GL_ARRAY_BUFFER, vbo);

        // Set up vertex attributes
        glEnableVertexAttribArray(position_attr);
        glVertexAttribPointer(position_attr, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);

        glEnableVertexAttribArray(texcoord_attr);
        glVertexAttribPointer(texcoord_attr, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

        // Set texture uniform
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);
        glUniform1i(texture_uniform, 0);

        // Draw fullscreen quad
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        // Disable vertex attributes
        glDisableVertexAttribArray(position_attr);
        glDisableVertexAttribArray(texcoord_attr);

        // Present frame
        if (!eglSwapBuffers(egl_display, egl_surface)) {
            printEGLError("eglSwapBuffers");
            return false;
        }

        printGLError("renderFrame");

        return true;
    }

    bool GPUVideoRenderer::resize(int width, int height) {
        if (!initialized || !egl_window) {
            return false;
        }

        surface_width = width;
        surface_height = height;

        wl_egl_window_resize(egl_window, width, height, 0, 0);

        return true;
    }

    void GPUVideoRenderer::cleanup() {
        if (egl_display != EGL_NO_DISPLAY) {
            eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

            if (texture) {
                glDeleteTextures(1, &texture);
                texture = 0;
            }

            if (vbo) {
                glDeleteBuffers(1, &vbo);
                vbo = 0;
            }

            if (shader_program) {
                glDeleteProgram(shader_program);
                shader_program = 0;
            }

            if (egl_context != EGL_NO_CONTEXT) {
                eglDestroyContext(egl_display, egl_context);
                egl_context = EGL_NO_CONTEXT;
            }

            if (egl_surface != EGL_NO_SURFACE) {
                eglDestroySurface(egl_display, egl_surface);
                egl_surface = EGL_NO_SURFACE;
            }

            if (egl_window) {
                wl_egl_window_destroy(egl_window);
                egl_window = nullptr;
            }

            eglTerminate(egl_display);
            egl_display = EGL_NO_DISPLAY;
        }

        initialized = false;
    }

    void GPUVideoRenderer::printEGLError(const std::string& operation) {
        EGLint error = eglGetError();
        if (error != EGL_SUCCESS) {
            last_error = operation + " failed with EGL error: 0x" + std::to_string(error);
            std::cerr << "[GPU] " << last_error << std::endl;
        }
    }

    void GPUVideoRenderer::printGLError(const std::string& operation) {
        GLenum error = glGetError();
        if (error != GL_NO_ERROR) {
            last_error = operation + " failed with GL error: 0x" + std::to_string(error);
            std::cerr << "[GPU] " << last_error << std::endl;
        }
    }

} // namespace openterface