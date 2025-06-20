#include "openterface/serial.hpp"
#include <iostream>
#include <memory>

namespace openterface {

    struct Serial::Impl {
        std::string port_name;
        int baudrate = 115200;
        bool connected = false;
        bool target_connected = false;

        void log(const std::string &msg) { std::cout << "[SERIAL] " << msg << std::endl; }
    };

    Serial::Serial() : pImpl(std::make_unique<Impl>()) {}

    Serial::~Serial() = default;

    bool Serial::connect(const std::string &port, int baudrate) {
        pImpl->port_name = port;
        pImpl->baudrate = baudrate;
        pImpl->log("Connecting to " + port + " @ " + std::to_string(baudrate));

        // Simulate successful connection
        pImpl->connected = true;
        pImpl->target_connected = true;
        pImpl->log("Connected successfully");
        return true;
    }

    void Serial::disconnect() {
        if (pImpl->connected) {
            pImpl->log("Disconnecting from " + pImpl->port_name);
            pImpl->connected = false;
            pImpl->target_connected = false;
        }
    }

    bool Serial::isConnected() const { return pImpl->connected; }

    bool Serial::sendData(const std::vector<uint8_t> &data) {
        if (!pImpl->connected)
            return false;

        pImpl->log("Sending " + std::to_string(data.size()) + " bytes");
        return true;
    }

    std::vector<uint8_t> Serial::readData() {
        if (!pImpl->connected)
            return {};

        // Simulate reading some data
        return {0x57, 0xAB, 0x00, 0x01, 0x00};
    }

    bool Serial::sendKeyPress(int key_code, int modifiers) {
        if (!pImpl->connected)
            return false;

        pImpl->log("Sending key press: " + std::to_string(key_code) + " (mod: " + std::to_string(modifiers) + ")");

        // CH9329 keyboard command format
        std::vector<uint8_t> cmd = {
            0x57, 0xAB, 0x00, 0x02, 0x08, 0x00, static_cast<uint8_t>(modifiers), 0x00, static_cast<uint8_t>(key_code),
            0x00, 0x00, 0x00, 0x00};
        return sendData(cmd);
    }

    bool Serial::sendKeyRelease(int key_code, int modifiers) {
        if (!pImpl->connected)
            return false;

        pImpl->log("Sending key release: " + std::to_string(key_code));

        // Send all zeros to release
        std::vector<uint8_t> cmd = {0x57, 0xAB, 0x00, 0x02, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        return sendData(cmd);
    }

    bool Serial::sendMouseMove(int x, int y, bool absolute) {
        if (!pImpl->connected)
            return false;

        pImpl->log("Mouse move: " + std::to_string(x) + "," + std::to_string(y) + (absolute ? " (abs)" : " (rel)"));

        // CH9329 mouse command
        std::vector<uint8_t> cmd;
        if (absolute) {
            cmd = {0x57, 0xAB, 0x00, 0x04, 0x07, 0x02};
        } else {
            cmd = {0x57, 0xAB, 0x00, 0x05, 0x05, 0x01};
        }

        // Add coordinates
        cmd.push_back(static_cast<uint8_t>(x & 0xFF));
        cmd.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
        cmd.push_back(static_cast<uint8_t>(y & 0xFF));
        cmd.push_back(static_cast<uint8_t>((y >> 8) & 0xFF));

        return sendData(cmd);
    }

    bool Serial::sendText(const std::string &text) {
        if (!pImpl->connected)
            return false;

        pImpl->log("Sending text: '" + text + "'");

        // Send each character as a key press/release
        for (char c : text) {
            sendKeyPress(static_cast<int>(c), 0);
            sendKeyRelease(static_cast<int>(c), 0);
        }
        return true;
    }

    bool Serial::sendCtrlAltDel() {
        if (!pImpl->connected)
            return false;

        pImpl->log("Sending Ctrl+Alt+Del");

        // Send Ctrl+Alt+Del key combination
        sendKeyPress(0x4C, 0x05); // Del key with Ctrl+Alt modifiers
        sendKeyRelease(0x4C, 0x00);
        return true;
    }

    bool Serial::resetHID() {
        if (!pImpl->connected)
            return false;

        pImpl->log("Resetting CH9329 HID");

        // CH9329 reset command
        std::vector<uint8_t> cmd = {0x57, 0xAB, 0x00, 0x0F, 0x00};
        return sendData(cmd);
    }

    SerialInfo Serial::getInfo() const {
        SerialInfo info;
        info.port_name = pImpl->port_name;
        info.baudrate = pImpl->baudrate;
        info.connected = pImpl->connected;
        info.target_connected = pImpl->target_connected;
        return info;
    }

    std::vector<std::string> Serial::getAvailablePorts() const {
        // Simulate finding serial ports
        return {"/dev/ttyUSB0", "/dev/ttyUSB1", "/dev/ttyACM0"};
    }

} // namespace openterface
