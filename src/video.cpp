#include "openterface/video.hpp"
#include <atomic>
#include <cstring>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

// Linux V4L2 headers for USB video capture
#ifdef __linux__
#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

// Wayland headers for display
#ifdef __linux__
#include <wayland-client.h>
#endif

namespace openterface {

    class Video::Impl {
      public:
        std::string device_path;
        int fd = -1;
        VideoInfo info;
        FrameCallback frame_callback;

        // V4L2 buffer management
        struct Buffer {
            void *start = nullptr;
            size_t length = 0;
        };
        std::vector<Buffer> buffers;

        // Capture thread
        std::atomic<bool> capture_running{false};
        std::thread capture_thread;

        // Wayland display
        struct wl_display *wl_display = nullptr;
        struct wl_registry *wl_registry = nullptr;
        struct wl_compositor *wl_compositor = nullptr;
        struct wl_surface *wl_surface = nullptr;
        struct wl_shell *wl_shell = nullptr;
        struct wl_shell_surface *wl_shell_surface = nullptr;

        void log(const std::string &msg) { std::cout << "[VIDEO] " << msg << std::endl; }

        bool setupV4L2();
        void cleanupV4L2();
        bool allocateBuffers();
        void freeBuffers();
        void captureLoop();

        bool setupWayland();
        void cleanupWayland();
    };

    Video::Video() : pImpl(std::make_unique<Impl>()) {}

    Video::~Video() { disconnect(); }

    bool Video::connect(const std::string &device_path) {
        pImpl->device_path = device_path;
        pImpl->info.device_path = device_path;

        // Connect to video device (silent mode)

#ifdef __linux__
        // Open V4L2 device
        pImpl->fd = open(device_path.c_str(), O_RDWR);
        if (pImpl->fd == -1) {
            pImpl->log("Failed to open device: " + std::string(strerror(errno)));
            return false;
        }

        // Check if it's a video capture device
        struct v4l2_capability cap;
        if (ioctl(pImpl->fd, VIDIOC_QUERYCAP, &cap) == -1) {
            pImpl->log("Failed to query device capabilities");
            close(pImpl->fd);
            pImpl->fd = -1;
            return false;
        }

        if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
            pImpl->log("Device does not support video capture");
            close(pImpl->fd);
            pImpl->fd = -1;
            return false;
        }

        if (!pImpl->setupV4L2()) {
            close(pImpl->fd);
            pImpl->fd = -1;
            return false;
        }

        pImpl->info.connected = true;
        // Video device connected successfully (silent)
        return true;
#else
        pImpl->log("Video capture not supported on this platform");
        return false;
#endif
    }

    void Video::disconnect() {
        stopCapture();
        pImpl->cleanupV4L2();
        pImpl->cleanupWayland();

        if (pImpl->fd != -1) {
            close(pImpl->fd);
            pImpl->fd = -1;
        }

        pImpl->info.connected = false;
        // Video device disconnected (silent)
    }

    bool Video::isConnected() const { return pImpl->info.connected; }

    bool Video::startCapture() {
        if (!pImpl->info.connected) {
            pImpl->log("Device not connected");
            return false;
        }

        if (pImpl->capture_running) {
            pImpl->log("Capture already running");
            return true;
        }

#ifdef __linux__
        if (!pImpl->allocateBuffers()) {
            return false;
        }

        // Start streaming
        enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (ioctl(pImpl->fd, VIDIOC_STREAMON, &type) == -1) {
            pImpl->log("Failed to start streaming");
            pImpl->freeBuffers();
            return false;
        }

        pImpl->capture_running = true;
        pImpl->capture_thread = std::thread(&Video::Impl::captureLoop, pImpl.get());

        pImpl->info.capturing = true;
        // Video capture started (silent)
        return true;
#else
        return false;
#endif
    }

    void Video::stopCapture() {
        if (!pImpl->capture_running) {
            return;
        }

        pImpl->capture_running = false;

        if (pImpl->capture_thread.joinable()) {
            pImpl->capture_thread.join();
        }

#ifdef __linux__
        // Stop streaming
        enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ioctl(pImpl->fd, VIDIOC_STREAMOFF, &type);

        pImpl->freeBuffers();
#endif

        pImpl->info.capturing = false;
        // Video capture stopped (silent)
    }

    bool Video::isCapturing() const { return pImpl->info.capturing; }

    bool Video::setResolution(int width, int height) {
        if (pImpl->info.capturing) {
            pImpl->log("Cannot change resolution while capturing");
            return false;
        }

#ifdef __linux__
        if (pImpl->fd == -1) {
            pImpl->log("Device not connected");
            return false;
        }

        struct v4l2_format fmt;
        memset(&fmt, 0, sizeof(fmt));
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        fmt.fmt.pix.width = width;
        fmt.fmt.pix.height = height;
        fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG; // Default to MJPEG
        fmt.fmt.pix.field = V4L2_FIELD_INTERLACED;

        if (ioctl(pImpl->fd, VIDIOC_S_FMT, &fmt) == -1) {
            pImpl->log("Failed to set resolution");
            return false;
        }

        pImpl->info.width = fmt.fmt.pix.width;
        pImpl->info.height = fmt.fmt.pix.height;
        // Resolution set (silent)
        return true;
#else
        return false;
#endif
    }

    bool Video::createWaylandWindow(const std::string &title) {
#ifdef __linux__
        return pImpl->setupWayland();
#else
        pImpl->log("Wayland not supported on this platform");
        return false;
#endif
    }

    void Video::destroyWaylandWindow() { pImpl->cleanupWayland(); }

    void Video::setFrameCallback(FrameCallback callback) { pImpl->frame_callback = callback; }

    VideoInfo Video::getInfo() const { return pImpl->info; }

    std::vector<std::string> Video::getAvailableDevices() const {
        std::vector<std::string> devices;

        // Scan for V4L2 devices
        for (int i = 0; i < 10; i++) {
            std::string device = "/dev/video" + std::to_string(i);
            int fd = open(device.c_str(), O_RDWR);
            if (fd != -1) {
                struct v4l2_capability cap;
                if (ioctl(fd, VIDIOC_QUERYCAP, &cap) != -1) {
                    if (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) {
                        devices.push_back(device);
                    }
                }
                close(fd);
            }
        }

        return devices;
    }

    // Implementation of private methods
    bool Video::Impl::setupV4L2() {
#ifdef __linux__
        // Set format to MJPEG by default with optimal settings
        struct v4l2_format fmt;
        memset(&fmt, 0, sizeof(fmt));
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

        // Get current format first
        if (ioctl(fd, VIDIOC_G_FMT, &fmt) == -1) {
            log("Failed to get current format");
            return false;
        }

        // Set optimal format for 1920x1080 @ 30fps MJPEG
        fmt.fmt.pix.width = 1920;
        fmt.fmt.pix.height = 1080;
        fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
        fmt.fmt.pix.field = V4L2_FIELD_NONE;  // Progressive scan
        fmt.fmt.pix.bytesperline = 0;         // Let driver calculate
        fmt.fmt.pix.sizeimage = 0;            // Let driver calculate
        
        if (ioctl(fd, VIDIOC_S_FMT, &fmt) == -1) {
            log("MJPEG 1920x1080 not supported, trying lower resolution");
            fmt.fmt.pix.width = 1280;
            fmt.fmt.pix.height = 720;
            if (ioctl(fd, VIDIOC_S_FMT, &fmt) == -1) {
                log("MJPEG not supported, trying YUYV");
                fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
                fmt.fmt.pix.width = 1280;
                fmt.fmt.pix.height = 720;
                if (ioctl(fd, VIDIOC_S_FMT, &fmt) == -1) {
                    log("Failed to set video format");
                    return false;
                }
                info.format = "YUYV";
            } else {
                info.format = "MJPG";
            }
        } else {
            info.format = "MJPG";
        }

        info.width = fmt.fmt.pix.width;
        info.height = fmt.fmt.pix.height;

        // Set frame rate to 30fps for optimal performance
        struct v4l2_streamparm streamparm;
        memset(&streamparm, 0, sizeof(streamparm));
        streamparm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        
        // Get current streaming parameters
        if (ioctl(fd, VIDIOC_G_PARM, &streamparm) == 0) {
            // Set to 30 fps
            streamparm.parm.capture.timeperframe.numerator = 1;
            streamparm.parm.capture.timeperframe.denominator = 30;
            streamparm.parm.capture.capturemode = 0; // Normal capture mode
            
            if (ioctl(fd, VIDIOC_S_PARM, &streamparm) == -1) {
                log("Warning: Failed to set frame rate to 30fps");
            } else {
                info.fps = streamparm.parm.capture.timeperframe.denominator / 
                          streamparm.parm.capture.timeperframe.numerator;
                log("Frame rate set to " + std::to_string(info.fps) + " fps");
            }
        } else {
            log("Warning: Failed to get streaming parameters");
        }

        log("Video format: " + info.format + " " + std::to_string(info.width) + "x" + 
            std::to_string(info.height) + " @ " + std::to_string(info.fps) + "fps");

        return true;
#else
        return false;
#endif
    }

    void Video::Impl::cleanupV4L2() { freeBuffers(); }

    bool Video::Impl::allocateBuffers() {
#ifdef __linux__
        struct v4l2_requestbuffers req;
        memset(&req, 0, sizeof(req));
        req.count = 4;  // Use 4 buffers for stable capture
        req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory = V4L2_MEMORY_MMAP;

        if (ioctl(fd, VIDIOC_REQBUFS, &req) == -1) {
            log("Failed to request buffers");
            return false;
        }

        buffers.resize(req.count);

        for (size_t i = 0; i < req.count; i++) {
            struct v4l2_buffer buf;
            memset(&buf, 0, sizeof(buf));
            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;
            buf.index = i;

            if (ioctl(fd, VIDIOC_QUERYBUF, &buf) == -1) {
                log("Failed to query buffer");
                return false;
            }

            buffers[i].length = buf.length;
            buffers[i].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.offset);

            if (buffers[i].start == MAP_FAILED) {
                log("Failed to mmap buffer");
                return false;
            }

            // Queue the buffer
            if (ioctl(fd, VIDIOC_QBUF, &buf) == -1) {
                log("Failed to queue buffer");
                return false;
            }
        }

        log("Allocated " + std::to_string(req.count) + " buffers");
        return true;
#else
        return false;
#endif
    }

    void Video::Impl::freeBuffers() {
        for (auto &buffer : buffers) {
            if (buffer.start != nullptr && buffer.start != MAP_FAILED) {
                munmap(buffer.start, buffer.length);
            }
        }
        buffers.clear();
    }

    void Video::Impl::captureLoop() {
#ifdef __linux__
        log("Capture loop started (30fps target)");

        while (capture_running) {
            fd_set fds;
            struct timeval tv;

            FD_ZERO(&fds);
            FD_SET(fd, &fds);

            // Use shorter timeout for better responsiveness
            // 25ms timeout (40fps rate) for good performance without breaking capture
            tv.tv_sec = 0;
            tv.tv_usec = 25000; // ~25ms timeout for better responsiveness

            int r = select(fd + 1, &fds, NULL, NULL, &tv);
            if (r == -1) {
                if (errno == EINTR)
                    continue;
                log("Select error: " + std::string(strerror(errno)));
                break;
            }

            if (r == 0) {
                // Timeout - frame may not be ready yet, continue polling
                continue;
            }

            struct v4l2_buffer buf;
            memset(&buf, 0, sizeof(buf));
            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;

            if (ioctl(fd, VIDIOC_DQBUF, &buf) == -1) {
                if (errno == EAGAIN)
                    continue;
                log("Failed to dequeue buffer: " + std::string(strerror(errno)));
                break;
            }

            // Process frame with minimal latency
            if (frame_callback && buf.index < buffers.size()) {
                FrameData frame;
                frame.data = static_cast<uint8_t *>(buffers[buf.index].start);
                frame.size = buf.bytesused;
                frame.width = info.width;
                frame.height = info.height;
                frame.timestamp = buf.timestamp.tv_sec * 1000000ULL + buf.timestamp.tv_usec;

                // Call frame callback immediately for minimal latency
                frame_callback(frame);
            }

            // Requeue buffer immediately
            if (ioctl(fd, VIDIOC_QBUF, &buf) == -1) {
                log("Failed to requeue buffer: " + std::string(strerror(errno)));
                break;
            }
        }

        log("Capture loop ended");
#endif
    }

    bool Video::Impl::setupWayland() {
        // Wayland display setup (simulation mode)
        return true;
    }

    void Video::Impl::cleanupWayland() {
        // Wayland cleanup (silent)
    }

    std::vector<std::string> Video::getSupportedFormats() const { return {"MJPG", "YUYV"}; }

    std::vector<std::pair<int, int>> Video::getSupportedResolutions() const {
        return {{1920, 1080}, {1280, 720}, {640, 480}};
    }

    bool Video::setFrameRate(int fps) {
        pImpl->info.fps = fps;
        return true; // Simplified implementation
    }

    bool Video::setFormat(const std::string &format) {
        pImpl->info.format = format;
        return true; // Simplified implementation
    }

    bool Video::getFrame(FrameData &frame, int timeout_ms) {
        // Simplified implementation
        return false;
    }

    bool Video::displayFrame(const FrameData &frame) {
        // Simplified implementation - would need proper Wayland buffer handling
        return true;
    }

    void Video::setWindowTitle(const std::string &title) {
        // Simplified implementation
    }

} // namespace openterface
