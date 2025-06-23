#include "openterface/input.hpp"
#include "openterface/serial.hpp"
#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <thread>

// Wayland headers for input capture
#ifdef __linux__
#include <linux/input-event-codes.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>
#endif

namespace openterface {

    class Input::Impl {
      public:
        InputInfo info;
        std::shared_ptr<Serial> serial;
        KeyCallback key_callback;
        MouseCallback mouse_callback;

        std::atomic<bool> forwarding_enabled{true};
        std::atomic<bool> capture_running{false};
        std::thread event_thread;

        // Wayland objects
        struct wl_display *wl_display = nullptr;
        struct wl_registry *wl_registry = nullptr;
        struct wl_seat *wl_seat = nullptr;
        struct wl_keyboard *wl_keyboard = nullptr;
        struct wl_pointer *wl_pointer = nullptr;

        // XKB context for keyboard handling
        struct xkb_context *xkb_context = nullptr;
        struct xkb_keymap *xkb_keymap = nullptr;
        struct xkb_state *xkb_state = nullptr;

        // Current mouse position
        double mouse_x = 0.0;
        double mouse_y = 0.0;

        void log(const std::string &msg) { std::cout << "[INPUT] " << msg << std::endl; }

        // Wayland event handlers
        static void registryHandler(void *data, struct wl_registry *registry, uint32_t id, const char *interface,
                                    uint32_t version);
        static void registryRemover(void *data, struct wl_registry *registry, uint32_t id);

        static void seatCapabilities(void *data, struct wl_seat *seat, uint32_t capabilities);
        static void seatName(void *data, struct wl_seat *seat, const char *name);

        static void keyboardKeymap(void *data, struct wl_keyboard *keyboard, uint32_t format, int32_t fd,
                                   uint32_t size);
        static void keyboardEnter(void *data, struct wl_keyboard *keyboard, uint32_t serial, struct wl_surface *surface,
                                  struct wl_array *keys);
        static void keyboardLeave(void *data, struct wl_keyboard *keyboard, uint32_t serial,
                                  struct wl_surface *surface);
        static void keyboardKey(void *data, struct wl_keyboard *keyboard, uint32_t serial, uint32_t time, uint32_t key,
                                uint32_t state);
        static void keyboardModifiers(void *data, struct wl_keyboard *keyboard, uint32_t serial,
                                      uint32_t mods_depressed, uint32_t mods_latched, uint32_t mods_locked,
                                      uint32_t group);

        static void pointerEnter(void *data, struct wl_pointer *pointer, uint32_t serial, struct wl_surface *surface,
                                 wl_fixed_t surface_x, wl_fixed_t surface_y);
        static void pointerLeave(void *data, struct wl_pointer *pointer, uint32_t serial, struct wl_surface *surface);
        static void pointerMotion(void *data, struct wl_pointer *pointer, uint32_t time, wl_fixed_t surface_x,
                                  wl_fixed_t surface_y);
        static void pointerButton(void *data, struct wl_pointer *pointer, uint32_t serial, uint32_t time,
                                  uint32_t button, uint32_t state);
        static void pointerAxis(void *data, struct wl_pointer *pointer, uint32_t time, uint32_t axis, wl_fixed_t value);

        bool setupWayland();
        void cleanupWayland();
        void eventLoop();

        // Key code conversion (Linux input codes to USB HID)
        uint8_t linuxToHidKeycode(uint32_t linux_code);
        uint8_t waylandToHidButton(uint32_t wayland_button);
    };

    Input::Input() : pImpl(std::make_unique<Impl>()) {}

    Input::~Input() {
        stopCapture();
        disconnectWayland();
    }

    bool Input::connectWayland() {
#ifdef __linux__
        return pImpl->setupWayland();
#else
        pImpl->log("Wayland not supported on this platform");
        return false;
#endif
    }

    void Input::disconnectWayland() {
        stopCapture();
        pImpl->cleanupWayland();
    }

    bool Input::isWaylandConnected() const { return pImpl->info.wayland_connected; }

    bool Input::startCapture() {
        if (pImpl->capture_running) {
            pImpl->log("Input capture already running");
            return true;
        }

        if (!pImpl->info.wayland_connected) {
            pImpl->log("Wayland not connected");
            return false;
        }

        pImpl->capture_running = true;
        pImpl->event_thread = std::thread(&Input::Impl::eventLoop, pImpl.get());

        pImpl->info.capturing = true;
        // Input capture started (silent)
        return true;
    }

    void Input::stopCapture() {
        if (!pImpl->capture_running) {
            return;
        }

        pImpl->capture_running = false;

        if (pImpl->event_thread.joinable()) {
            pImpl->event_thread.join();
        }

        pImpl->info.capturing = false;
        // Input capture stopped (silent)
    }

    bool Input::isCapturing() const { return pImpl->info.capturing; }

    void Input::setSerial(std::shared_ptr<Serial> serial) { pImpl->serial = serial; }

    void Input::setKeyCallback(KeyCallback callback) { pImpl->key_callback = callback; }

    void Input::setMouseCallback(MouseCallback callback) { pImpl->mouse_callback = callback; }

    void Input::setForwardingEnabled(bool enabled) {
        pImpl->forwarding_enabled = enabled;
        pImpl->log("Input forwarding " + std::string(enabled ? "enabled" : "disabled"));
    }

    bool Input::isForwardingEnabled() const { return pImpl->forwarding_enabled; }

    bool Input::injectKeyPress(uint32_t key_code, uint32_t modifiers) {
        if (!pImpl->serial || !pImpl->serial->isConnected()) {
            pImpl->log("Serial not connected for key injection");
            return false;
        }

        uint8_t hid_code = pImpl->linuxToHidKeycode(key_code);
        return pImpl->serial->sendKeyPress(hid_code, modifiers);
    }

    bool Input::injectKeyRelease(uint32_t key_code, uint32_t modifiers) {
        if (!pImpl->serial || !pImpl->serial->isConnected()) {
            return false;
        }

        uint8_t hid_code = pImpl->linuxToHidKeycode(key_code);
        return pImpl->serial->sendKeyRelease(hid_code, modifiers);
    }

    bool Input::injectMouseMove(int32_t x, int32_t y, bool absolute) {
        if (!pImpl->serial || !pImpl->serial->isConnected()) {
            return false;
        }

        return pImpl->serial->sendMouseMove(x, y, absolute);
    }

    bool Input::injectCtrlAltDel() {
        if (!pImpl->serial || !pImpl->serial->isConnected()) {
            return false;
        }

        return pImpl->serial->sendCtrlAltDel();
    }

    InputInfo Input::getInfo() const { return pImpl->info; }

    std::vector<std::string> Input::getAvailableKeyboards() const {
        // Simplified implementation
        return {"wayland-keyboard"};
    }

    std::vector<std::string> Input::getAvailableMice() const {
        // Simplified implementation
        return {"wayland-pointer"};
    }

    // Wayland event handlers implementation
    void Input::Impl::registryHandler(void *data, struct wl_registry *registry, uint32_t id, const char *interface,
                                      uint32_t version) {
        Input::Impl *impl = static_cast<Input::Impl *>(data);

        // Simplified registry handling
        if (strcmp(interface, "wl_seat") == 0) {
            impl->log("Seat interface found (simulation)");
            impl->info.seat_name = "seat0";
        }
    }

    void Input::Impl::registryRemover(void *data, struct wl_registry *registry, uint32_t id) {
        // Handle registry object removal
    }

    void Input::Impl::seatCapabilities(void *data, struct wl_seat *seat, uint32_t capabilities) {
        Input::Impl *impl = static_cast<Input::Impl *>(data);

        // Simplified capability detection
        impl->info.keyboard_enabled = true;
        impl->info.mouse_enabled = true;
        impl->log("Input capabilities detected (simulation)");
    }

    void Input::Impl::seatName(void *data, struct wl_seat *seat, const char *name) {
        Input::Impl *impl = static_cast<Input::Impl *>(data);
        impl->info.seat_name = name;
        impl->log("Seat name: " + std::string(name));
    }

    void Input::Impl::keyboardKey(void *data, struct wl_keyboard *keyboard, uint32_t serial, uint32_t time,
                                  uint32_t key, uint32_t state) {
        Input::Impl *impl = static_cast<Input::Impl *>(data);

        KeyEvent event;
        event.key_code = key;
        event.pressed = (state == WL_KEYBOARD_KEY_STATE_PRESSED);
        event.timestamp = time;
        event.modifiers = 0; // Would need to track modifiers properly

        // Call callback if set
        if (impl->key_callback) {
            impl->key_callback(event);
        }

        // Forward to serial if enabled
        if (impl->forwarding_enabled && impl->serial && impl->serial->isConnected()) {
            uint8_t hid_code = impl->linuxToHidKeycode(key);
            if (event.pressed) {
                impl->serial->sendKeyPress(hid_code, event.modifiers);
            } else {
                impl->serial->sendKeyRelease(hid_code, event.modifiers);
            }
        }
    }

    void Input::Impl::pointerMotion(void *data, struct wl_pointer *pointer, uint32_t time, wl_fixed_t surface_x,
                                    wl_fixed_t surface_y) {
        Input::Impl *impl = static_cast<Input::Impl *>(data);

        double x = wl_fixed_to_double(surface_x);
        double y = wl_fixed_to_double(surface_y);

        MouseEvent event;
        event.type = MouseEvent::MOVE;
        event.x = static_cast<int32_t>(x - impl->mouse_x);
        event.y = static_cast<int32_t>(y - impl->mouse_y);
        event.timestamp = time;

        impl->mouse_x = x;
        impl->mouse_y = y;

        // Call callback if set
        if (impl->mouse_callback) {
            impl->mouse_callback(event);
        }

        // Forward to serial if enabled
        if (impl->forwarding_enabled && impl->serial && impl->serial->isConnected()) {
            impl->serial->sendMouseMove(event.x, event.y, false); // Relative movement
        }
    }

    void Input::Impl::pointerButton(void *data, struct wl_pointer *pointer, uint32_t serial, uint32_t time,
                                    uint32_t button, uint32_t state) {
        Input::Impl *impl = static_cast<Input::Impl *>(data);

        MouseEvent event;
        event.type = MouseEvent::BUTTON;
        event.button = button;
        event.pressed = (state == WL_POINTER_BUTTON_STATE_PRESSED);
        event.timestamp = time;

        // Call callback if set
        if (impl->mouse_callback) {
            impl->mouse_callback(event);
        }

        // Forward to serial if enabled 
        if (impl->forwarding_enabled && impl->serial && impl->serial->isConnected()) {
            uint8_t hid_button = impl->waylandToHidButton(button);
            impl->serial->sendMouseButton(hid_button, event.pressed, static_cast<int>(impl->mouse_x), static_cast<int>(impl->mouse_y), true);
            impl->log("Mouse button " + std::to_string(button) + (event.pressed ? " pressed" : " released") + " forwarded");
        }
    }

    bool Input::Impl::setupWayland() {
        // Simplified setup without actual Wayland libraries for now
        // Wayland input setup (simulation mode)
        info.wayland_connected = true;
        info.keyboard_enabled = true;
        info.mouse_enabled = true;
        info.seat_name = "seat0";
        return true;
    }

    void Input::Impl::cleanupWayland() {
        info.wayland_connected = false;
        info.keyboard_enabled = false;
        info.mouse_enabled = false;
        // Wayland cleanup (silent)
    }

    void Input::Impl::eventLoop() {
        // Input event loop started (simulation mode)

        while (capture_running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            // Simulate some input events here if needed
        }

        // Input event loop ended (silent)
    }

    // Key code conversion from Linux input codes to USB HID
    uint8_t Input::Impl::linuxToHidKeycode(uint32_t linux_code) {
        // This is a simplified mapping - would need a complete translation table
        static const std::map<uint32_t, uint8_t> keymap = {
            {KEY_A, 0x04},         {KEY_B, 0x05},          {KEY_C, 0x06},         {KEY_D, 0x07},
            {KEY_E, 0x08},         {KEY_F, 0x09},          {KEY_G, 0x0A},         {KEY_H, 0x0B},
            {KEY_I, 0x0C},         {KEY_J, 0x0D},          {KEY_K, 0x0E},         {KEY_L, 0x0F},
            {KEY_M, 0x10},         {KEY_N, 0x11},          {KEY_O, 0x12},         {KEY_P, 0x13},
            {KEY_Q, 0x14},         {KEY_R, 0x15},          {KEY_S, 0x16},         {KEY_T, 0x17},
            {KEY_U, 0x18},         {KEY_V, 0x19},          {KEY_W, 0x1A},         {KEY_X, 0x1B},
            {KEY_Y, 0x1C},         {KEY_Z, 0x1D},          {KEY_1, 0x1E},         {KEY_2, 0x1F},
            {KEY_3, 0x20},         {KEY_4, 0x21},          {KEY_5, 0x22},         {KEY_6, 0x23},
            {KEY_7, 0x24},         {KEY_8, 0x25},          {KEY_9, 0x26},         {KEY_0, 0x27},
            {KEY_ENTER, 0x28},     {KEY_ESC, 0x29},        {KEY_BACKSPACE, 0x2A}, {KEY_TAB, 0x2B},
            {KEY_SPACE, 0x2C},     {KEY_LEFTCTRL, 0xE0},   {KEY_LEFTSHIFT, 0xE1}, {KEY_LEFTALT, 0xE2},
            {KEY_RIGHTCTRL, 0xE4}, {KEY_RIGHTSHIFT, 0xE5}, {KEY_RIGHTALT, 0xE6},  {KEY_DELETE, 0x4C}};

        auto it = keymap.find(linux_code);
        return (it != keymap.end()) ? it->second : 0;
    }

    uint8_t Input::Impl::waylandToHidButton(uint32_t wayland_button) {
        // Convert Wayland button codes to standard mouse button numbers
        // BTN_LEFT = 0x110, BTN_RIGHT = 0x111, BTN_MIDDLE = 0x112
        switch (wayland_button) {
            case 0x110: return 1; // Left button
            case 0x111: return 2; // Right button  
            case 0x112: return 3; // Middle button
            default: return 0;    // Unknown button
        }
    }

    // Stub implementations for remaining methods
    void Input::Impl::keyboardKeymap(void *data, struct wl_keyboard *keyboard, uint32_t format, int32_t fd,
                                     uint32_t size) {}
    void Input::Impl::keyboardEnter(void *data, struct wl_keyboard *keyboard, uint32_t serial,
                                    struct wl_surface *surface, struct wl_array *keys) {}
    void Input::Impl::keyboardLeave(void *data, struct wl_keyboard *keyboard, uint32_t serial,
                                    struct wl_surface *surface) {}
    void Input::Impl::keyboardModifiers(void *data, struct wl_keyboard *keyboard, uint32_t serial,
                                        uint32_t mods_depressed, uint32_t mods_latched, uint32_t mods_locked,
                                        uint32_t group) {}
    void Input::Impl::pointerEnter(void *data, struct wl_pointer *pointer, uint32_t serial, struct wl_surface *surface,
                                   wl_fixed_t surface_x, wl_fixed_t surface_y) {}
    void Input::Impl::pointerLeave(void *data, struct wl_pointer *pointer, uint32_t serial,
                                   struct wl_surface *surface) {}
    void Input::Impl::pointerAxis(void *data, struct wl_pointer *pointer, uint32_t time, uint32_t axis,
                                  wl_fixed_t value) {}

    bool Input::injectMouseButton(uint32_t button, bool pressed) {
        if (!pImpl->serial || !pImpl->serial->isConnected()) {
            pImpl->log("Serial not connected for mouse button injection");
            return false;
        }

        uint8_t hid_button = pImpl->waylandToHidButton(button);
        return pImpl->serial->sendMouseButton(hid_button, pressed, static_cast<int>(pImpl->mouse_x), static_cast<int>(pImpl->mouse_y), true);
    }
    bool Input::injectMouseScroll(int32_t scroll_x, int32_t scroll_y) {
        if (!pImpl->serial || !pImpl->serial->isConnected()) {
            pImpl->log("Serial not connected for mouse scroll injection");
            return false;
        }

        // Send mouse wheel command directly using CH9329 protocol
        if (scroll_y == 0) return true; // Nothing to scroll

        std::vector<uint8_t> cmd = {0x57, 0xAB, 0x00, 0x05, 0x05, 0x01}; // Relative mouse command
        cmd.push_back(0x00); // No button pressed
        cmd.push_back(0x00); // No X movement
        cmd.push_back(0x00); // No Y movement
        
        // Map scroll direction to wheel value based on original Qt implementation
        uint8_t wheel_value = 0;
        if (scroll_y > 0) {
            wheel_value = 0x01; // Scroll up
        } else if (scroll_y < 0) {
            wheel_value = 0xFF; // Scroll down (-1 as unsigned byte)
        }
        cmd.push_back(wheel_value);

        return pImpl->serial->sendData(cmd);
    }
    bool Input::injectEscape() { return injectKeyPress(KEY_ESC); }
    bool Input::injectTab() { return injectKeyPress(KEY_TAB); }
    bool Input::injectEnter() { return injectKeyPress(KEY_ENTER); }
    bool Input::requestKeyboardFocus() { return false; }
    bool Input::requestMouseFocus() { return false; }
    void Input::releaseFocus() {}
    
    void Input::stopMouseTracking() {
        if (pImpl->serial) {
            // Send a "stop tracking" command or reset mouse state
            pImpl->log("Explicitly stopping mouse tracking");
        }
    }

} // namespace openterface
