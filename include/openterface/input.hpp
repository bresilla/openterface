#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace openterface {

    // Forward declare Serial for dependency
    class Serial;

    struct InputInfo {
        bool keyboard_enabled = false;
        bool mouse_enabled = false;
        bool wayland_connected = false;
        std::string seat_name;
        bool capturing = false;
    };

    struct KeyEvent {
        uint32_t key_code;
        uint32_t modifiers;
        bool pressed; // true for press, false for release
        uint64_t timestamp;
    };

    struct MouseEvent {
        enum Type { MOVE, BUTTON, SCROLL };
        Type type;
        int32_t x, y;               // position or delta
        uint32_t button;            // button number for button events
        int32_t scroll_x, scroll_y; // scroll deltas
        bool pressed;               // for button events
        uint64_t timestamp;
    };

    class Input {
      public:
        Input();
        ~Input();

        // Wayland input capture setup
        bool connectWayland();
        void disconnectWayland();
        bool isWaylandConnected() const;

        // Input capture control
        bool startCapture();
        void stopCapture();
        bool isCapturing() const;

        // Serial output device (for forwarding input to target)
        void setSerial(std::shared_ptr<Serial> serial);

        // Input event callbacks (for local processing)
        using KeyCallback = std::function<void(const KeyEvent &)>;
        using MouseCallback = std::function<void(const MouseEvent &)>;
        void setKeyCallback(KeyCallback callback);
        void setMouseCallback(MouseCallback callback);

        // Input forwarding control
        void setForwardingEnabled(bool enabled);
        bool isForwardingEnabled() const;
        
        // Mouse tracking control
        void stopMouseTracking();

        // Manual input injection (for CLI commands)
        bool injectKeyPress(uint32_t key_code, uint32_t modifiers = 0);
        bool injectKeyRelease(uint32_t key_code, uint32_t modifiers = 0);
        bool injectMouseMove(int32_t x, int32_t y, bool absolute = true);
        bool injectMouseButton(uint32_t button, bool pressed);
        bool injectMouseScroll(int32_t scroll_x, int32_t scroll_y);

        // Special key combinations
        bool injectCtrlAltDel();
        bool injectEscape();
        bool injectTab();
        bool injectEnter();

        // Input device information
        InputInfo getInfo() const;
        std::vector<std::string> getAvailableKeyboards() const;
        std::vector<std::string> getAvailableMice() const;

        // Wayland-specific functionality
        bool requestKeyboardFocus();
        bool requestMouseFocus();
        void releaseFocus();

      private:
        class Impl;
        std::unique_ptr<Impl> pImpl;
    };

} // namespace openterface
