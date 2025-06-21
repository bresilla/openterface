#pragma once

#include <functional>
#include <memory>
#include <string>

namespace openterface {

    // Forward declarations
    class Video;
    class Input;
    class Serial;

    struct GUIInfo {
        bool window_created = false;
        bool video_displayed = false;
        bool input_captured = false;
        std::string window_title = "Openterface KVM";
        int window_width = 1920;
        int window_height = 1080;
    };

    class GUI {
      public:
        GUI();
        ~GUI();

        // GUI lifecycle
        bool initialize();
        void shutdown();
        bool isInitialized() const;

        // Window management
        bool createWindow(const std::string &title = "Openterface KVM", int width = 1920, int height = 1080);
        void destroyWindow();
        bool isWindowCreated() const;

        // Video display
        void setVideoSource(std::shared_ptr<Video> video);
        bool startVideoDisplay();
        void stopVideoDisplay();
        bool isVideoDisplaying() const;

        // Input capture and forwarding
        void setInputTarget(std::shared_ptr<Input> input);
        void setSerialForwarder(std::shared_ptr<Serial> serial);
        bool startInputCapture();
        void stopInputCapture();
        bool isInputCapturing() const;

        // Window properties
        void setWindowTitle(const std::string &title);
        void setWindowSize(int width, int height);
        void setFullscreen(bool fullscreen);
        bool isFullscreen() const;

        // Event loop (for standalone GUI mode)
        int runEventLoop(); // Blocking call
        void requestExit();

        // Debug
        void setDebugMode(bool enabled);

        // Status
        GUIInfo getInfo() const;

      private:
        class Impl;
        std::unique_ptr<Impl> pImpl;
    };

} // namespace openterface
