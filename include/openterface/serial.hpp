#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>

namespace openterface {

    struct SerialInfo {
        std::string port_name;
        int baudrate = 115200;
        bool connected = false;
        bool target_connected = false;
        bool connecting = false;
    };

    // Callback for connection status updates
    using ConnectionCallback = std::function<void(bool success, const std::string& message)>;

    class Serial {
      public:
        Serial();
        ~Serial();

        bool connect(const std::string &port, int baudrate = 115200);
        void connectAsync(const std::string &port, int baudrate = 115200, ConnectionCallback callback = nullptr);
        void disconnect();
        bool isConnected() const;
        bool isConnecting() const;

        bool sendData(const std::vector<uint8_t> &data);
        std::vector<uint8_t> readData();

        // CH9329 specific commands
        bool sendKeyPress(int key_code, int modifiers = 0);
        bool sendKeyRelease(int key_code, int modifiers = 0);
        bool sendMouseMove(int x, int y, bool absolute = true);
        bool sendMouseButton(int button, bool pressed, int x = 0, int y = 0, bool absolute = true);
        bool sendText(const std::string &text);
        bool sendCtrlAltDel();
        bool resetHID();
        bool factoryReset();

        SerialInfo getInfo() const;
        std::vector<std::string> getAvailablePorts() const;

      private:
        struct Impl;
        std::unique_ptr<Impl> pImpl;
        
        // Internal connection helper for baud rate fallback
        bool connectAtBaudRate(const std::string &port, int baudrate);
    };

} // namespace openterface
