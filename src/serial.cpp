#include "openterface/serial.hpp"
#include <iostream>
#include <memory>
#include <cstring>

// Linux serial port headers
#ifdef __linux__
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <errno.h>
#include <cstdio>
#endif

namespace openterface {

    struct Serial::Impl {
        std::string port_name;
        int baudrate = 115200;
        bool connected = false;
        bool target_connected = false;
        int fd = -1;  // File descriptor for serial port

        void log(const std::string &msg) { std::cout << "[SERIAL] " << msg << std::endl; }
        
        // Calculate checksum for CH9329 commands (matches Qt implementation)
        uint8_t calculateChecksum(const std::vector<uint8_t> &data) {
            uint32_t sum = 0;
            for (auto byte : data) {
                sum += static_cast<uint8_t>(byte);
            }
            return static_cast<uint8_t>(sum % 256);
        }
        
        // Send command with proper checksum
        bool sendCommandWithChecksum(const std::vector<uint8_t> &cmd_base) {
            auto cmd = cmd_base;  // Copy the command
            uint8_t checksum = calculateChecksum(cmd);
            cmd.push_back(checksum);  // Add checksum as last byte
            return sendDataRaw(cmd);
        }
        
        // Raw data send without checksum (for mouse commands that don't use checksums)
        bool sendDataRaw(const std::vector<uint8_t> &data) {
            if (!connected)
                return false;

    #ifdef __linux__
            // Log the hex data being sent
            std::string hex_str = "Sending " + std::to_string(data.size()) + " bytes: ";
            for (auto byte : data) {
                char hex[4];
                sprintf(hex, "%02X ", byte);
                hex_str += hex;
            }
            log(hex_str);

            // Actually write to serial port
            ssize_t bytes_written = write(fd, data.data(), data.size());
            if (bytes_written != static_cast<ssize_t>(data.size())) {
                log("Failed to write all bytes to serial port: " + std::string(strerror(errno)));
                return false;
            }
            
            // Flush the output
            if (tcdrain(fd) != 0) {
                log("Failed to flush serial port: " + std::string(strerror(errno)));
                return false;
            }
            
            return true;
    #else
            log("Sending " + std::to_string(data.size()) + " bytes (simulation)");
            return true;
    #endif
        }
    };

    Serial::Serial() : pImpl(std::make_unique<Impl>()) {}

    Serial::~Serial() = default;

    bool Serial::connect(const std::string &port, int baudrate) {
        pImpl->port_name = port;
        pImpl->baudrate = baudrate;
        
        // Try primary baud rate first, then fallback to 9600 (matching Qt implementation)
        std::vector<int> baud_rates = {baudrate};
        if (baudrate != 9600) {
            baud_rates.push_back(9600);  // Add fallback baud rate
        }
        
        for (int baud : baud_rates) {
            pImpl->log("Connecting to " + port + " @ " + std::to_string(baud));
            if (connectAtBaudRate(port, baud)) {
                pImpl->baudrate = baud;  // Store successful baud rate
                return true;
            }
        }
        
        pImpl->log("Failed to connect at any baud rate");
        return false;
    }
    
    bool Serial::connectAtBaudRate(const std::string &port, int baudrate) {
        pImpl->log("Attempting connection at " + std::to_string(baudrate) + " baud");

#ifdef __linux__
        // Open serial port
        pImpl->fd = open(port.c_str(), O_RDWR | O_NOCTTY | O_NDELAY);
        if (pImpl->fd == -1) {
            pImpl->log("Failed to open serial port: " + std::string(strerror(errno)));
            return false;
        }

        // Configure serial port
        struct termios options;
        tcgetattr(pImpl->fd, &options);
        
        // Set baud rate
        speed_t speed = B115200;
        switch (baudrate) {
            case 9600: speed = B9600; break;
            case 19200: speed = B19200; break;
            case 38400: speed = B38400; break;
            case 57600: speed = B57600; break;
            case 115200: speed = B115200; break;
            default: speed = B115200; break;
        }
        cfsetispeed(&options, speed);
        cfsetospeed(&options, speed);
        
        // Configure port settings
        options.c_cflag |= (CLOCAL | CREAD);    // Enable receiver, ignore modem control lines
        options.c_cflag &= ~PARENB;             // No parity
        options.c_cflag &= ~CSTOPB;             // 1 stop bit
        options.c_cflag &= ~CSIZE;              // Clear data size bits
        options.c_cflag |= CS8;                 // 8 data bits
        options.c_cflag &= ~CRTSCTS;            // No hardware flow control
        
        // Configure input processing
        options.c_iflag &= ~(IXON | IXOFF | IXANY); // No software flow control
        options.c_iflag &= ~(ICANON | ECHO | ECHOE | ISIG); // Raw input
        
        // Configure output processing
        options.c_oflag &= ~OPOST;              // Raw output
        
        // Set timeouts
        options.c_cc[VMIN] = 0;                 // Non-blocking read
        options.c_cc[VTIME] = 1;                // 0.1 second timeout
        
        // Apply settings
        if (tcsetattr(pImpl->fd, TCSANOW, &options) != 0) {
            pImpl->log("Failed to configure serial port: " + std::string(strerror(errno)));
            close(pImpl->fd);
            pImpl->fd = -1;
            return false;
        }

        pImpl->connected = true;
        
        // Send CH9329 initialization commands
        pImpl->log("Initializing CH9329 chip...");
        
        // Small delay to let port settle
        usleep(50000); // 50ms
        
        // Send CMD_GET_PARA_CFG to check chip configuration (with checksum)
        std::vector<uint8_t> get_para_cmd = {0x57, 0xAB, 0x00, 0x08, 0x00};
        if (!pImpl->sendCommandWithChecksum(get_para_cmd)) {
            pImpl->log("Failed to send parameter config command");
            close(pImpl->fd);
            pImpl->fd = -1;
            pImpl->connected = false;
            return false;
        }
        
        usleep(50000); // 50ms delay for chip to be ready
        
        // Send CMD_GET_INFO to check target connection (with checksum)
        std::vector<uint8_t> get_info_cmd = {0x57, 0xAB, 0x00, 0x01, 0x00};
        if (!pImpl->sendCommandWithChecksum(get_info_cmd)) {
            pImpl->log("Failed to send info command");
            close(pImpl->fd);
            pImpl->fd = -1;
            pImpl->connected = false;
            return false;
        }
        
        usleep(100000); // 100ms delay to allow response
        
        // Read response to check if target is connected
        auto response = readData();
        if (!response.empty()) {
            pImpl->log("CH9329 responded to info command - chip is active");
            pImpl->target_connected = true;
        } else {
            pImpl->log("Warning: No response from CH9329 to info command");
            pImpl->target_connected = false; // Still continue, but note the issue
        }
        
        pImpl->log("CH9329 initialized successfully");
        return true;
#else
        pImpl->log("Serial communication not supported on this platform");
        return false;
#endif
    }

    void Serial::disconnect() {
        if (pImpl->connected) {
            pImpl->log("Disconnecting from " + pImpl->port_name);
#ifdef __linux__
            if (pImpl->fd != -1) {
                close(pImpl->fd);
                pImpl->fd = -1;
            }
#endif
            pImpl->connected = false;
            pImpl->target_connected = false;
        }
    }

    bool Serial::isConnected() const { return pImpl->connected; }

    bool Serial::sendData(const std::vector<uint8_t> &data) {
        // ALL commands including mouse commands use checksums in Qt implementation
        return pImpl->sendCommandWithChecksum(data);
    }

    std::vector<uint8_t> Serial::readData() {
        if (!pImpl->connected)
            return {};

#ifdef __linux__
        std::vector<uint8_t> buffer;
        uint8_t byte_buffer[256];
        
        // Use non-blocking read
        ssize_t bytes_read = read(pImpl->fd, byte_buffer, sizeof(byte_buffer));
        if (bytes_read > 0) {
            buffer.assign(byte_buffer, byte_buffer + bytes_read);
            
            // Log received data
            std::string hex_str = "Received " + std::to_string(bytes_read) + " bytes: ";
            for (auto byte : buffer) {
                char hex[4];
                sprintf(hex, "%02X ", byte);
                hex_str += hex;
            }
            pImpl->log(hex_str);
        } else if (bytes_read == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
            pImpl->log("Error reading from serial port: " + std::string(strerror(errno)));
        }
        
        return buffer;
#else
        // Simulate reading some data
        return {0x57, 0xAB, 0x00, 0x01, 0x00};
#endif
    }

    bool Serial::sendKeyPress(int key_code, int modifiers) {
        if (!pImpl->connected)
            return false;

        pImpl->log("Sending key press: " + std::to_string(key_code) + " (mod: " + std::to_string(modifiers) + ")");

        // CH9329 keyboard command format: 57 AB 00 02 08 [mod] 00 [key1-6]
        std::vector<uint8_t> cmd = {
            0x57, 0xAB, 0x00, 0x02, 0x08,
            static_cast<uint8_t>(modifiers), // Byte 5: modifiers (Ctrl=0x01, Shift=0x02, Alt=0x04, Meta=0x08)
            0x00,                            // Byte 6: reserved
            static_cast<uint8_t>(key_code),  // Byte 7: first key
            0x00, 0x00, 0x00, 0x00, 0x00     // Bytes 8-12: remaining keys (up to 6 total)
        };
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

        // EXACT Qt implementation approach: build command without checksum, then sendData adds it
        std::vector<uint8_t> cmd;
        if (absolute) {
            // Absolute mouse prefix: 57 AB 00 04 07 02
            cmd = {0x57, 0xAB, 0x00, 0x04, 0x07, 0x02};
            cmd.push_back(0x00); // mouse_event (no button for move)
            cmd.push_back(static_cast<uint8_t>(x & 0xFF));        // x_low
            cmd.push_back(static_cast<uint8_t>((x >> 8) & 0xFF)); // x_high  
            cmd.push_back(static_cast<uint8_t>(y & 0xFF));        // y_low
            cmd.push_back(static_cast<uint8_t>((y >> 8) & 0xFF)); // y_high
            cmd.push_back(0x00); // wheel (no wheel for move)
        } else {
            // Relative mouse prefix: 57 AB 00 05 05 01  
            cmd = {0x57, 0xAB, 0x00, 0x05, 0x05, 0x01};
            cmd.push_back(0x00); // mouse_event (no button for move)
            cmd.push_back(static_cast<uint8_t>(x & 0xFF)); // dx
            cmd.push_back(static_cast<uint8_t>(y & 0xFF)); // dy  
            cmd.push_back(0x00); // wheel (no wheel for move)
        }

        // sendData will add checksum (matching Qt's sendCommandAsync behavior)
        return sendData(cmd);
    }

    bool Serial::sendMouseButton(int button, bool pressed, int x, int y, bool absolute) {
        if (!pImpl->connected)
            return false;

        pImpl->log("Mouse button " + std::to_string(button) + (pressed ? " pressed" : " released"));

        // CH9329 mouse button command based on original Qt implementation
        std::vector<uint8_t> cmd;
        if (absolute) {
            // Absolute mouse: 57 AB 00 04 07 02 [button] [x_low] [x_high] [y_low] [y_high] [wheel] [checksum]
            cmd = {0x57, 0xAB, 0x00, 0x04, 0x07, 0x02};
        } else {
            // Relative mouse: 57 AB 00 05 05 01 [button] [dx] [dy] [wheel] [checksum]
            cmd = {0x57, 0xAB, 0x00, 0x05, 0x05, 0x01};
        }
        
        // Button mapping based on Qt::MouseButton values: 1=left, 2=right, 4=middle
        uint8_t button_mask = 0;
        if (pressed) {
            switch (button) {
                case 1: button_mask = 0x01; break; // Left button (Qt::LeftButton)
                case 2: button_mask = 0x02; break; // Right button (Qt::RightButton)  
                case 3: case 4: button_mask = 0x04; break; // Middle button (Qt::MiddleButton)
            }
        }
        
        cmd.push_back(button_mask);
        if (absolute) {
            cmd.push_back(static_cast<uint8_t>(x & 0xFF));
            cmd.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
            cmd.push_back(static_cast<uint8_t>(y & 0xFF));
            cmd.push_back(static_cast<uint8_t>((y >> 8) & 0xFF));
            cmd.push_back(0x00); // No wheel movement
        } else {
            cmd.push_back(static_cast<uint8_t>(x & 0xFF));
            cmd.push_back(static_cast<uint8_t>(y & 0xFF));
            cmd.push_back(0x00); // No wheel movement
        }

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
