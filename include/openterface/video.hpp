#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace openterface {

    struct VideoInfo {
        std::string device_path;
        int width = 1920;
        int height = 1080;
        int fps = 30;
        std::string format = "MJPG";
        bool connected = false;
        bool capturing = false;
    };

    struct FrameData {
        uint8_t *data;
        size_t size;
        int width;
        int height;
        uint64_t timestamp;
    };

    class Video {
      public:
        Video();
        ~Video();

        // Device management
        bool connect(const std::string &device_path = "/dev/video0");
        void disconnect();
        bool isConnected() const;

        // Capture control
        bool startCapture();
        void stopCapture();
        bool isCapturing() const;

        // Configuration
        bool setResolution(int width, int height);
        bool setFrameRate(int fps);
        bool setFormat(const std::string &format); // MJPG, YUYV, etc.

        // Frame handling
        using FrameCallback = std::function<void(const FrameData &)>;
        void setFrameCallback(FrameCallback callback);
        bool getFrame(FrameData &frame, int timeout_ms = 1000);

        // Wayland display
        bool createWaylandWindow(const std::string &title = "Openterface KVM");
        void destroyWaylandWindow();
        bool displayFrame(const FrameData &frame);
        void setWindowTitle(const std::string &title);

        // Information
        VideoInfo getInfo() const;
        std::vector<std::string> getAvailableDevices() const;
        std::vector<std::string> getSupportedFormats() const;
        std::vector<std::pair<int, int>> getSupportedResolutions() const;

      private:
        class Impl;
        std::unique_ptr<Impl> pImpl;
    };

} // namespace openterface
