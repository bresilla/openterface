#pragma once

#include <memory>
#include <string>
#include <vector>

namespace openterface {

    // Forward declarations
    class Serial;
    class Video;
    class Input;
    class GUI;

    struct KVMDeviceInfo {
        std::string device_id;
        std::string serial_path;
        std::string video_path;
        std::string description;
        bool connected = false;
        bool video_active = false;
        bool input_active = false;
        bool gui_active = false;
    };

    struct KVMDevice {
        std::string device_id;
        std::string vendor_id;
        std::string product_id;
        std::string serial_number;
        std::string serial_path;
        std::string video_path;
        std::string description;
    };

    class KVMManager {
      public:
        KVMManager();
        ~KVMManager();

        // Device discovery
        std::vector<KVMDevice> scanForDevices();
        bool isOpenterfaceDevice(const std::string &device_path);

        // Connection management
        bool connect(const std::string &device_id = ""); // Auto-detect if empty
        bool connectByPaths(const std::string &serial_path, const std::string &video_path);
        void disconnect();
        bool isConnected() const;

        // Module access
        std::shared_ptr<Serial> getSerial() const;
        std::shared_ptr<Video> getVideo() const;
        std::shared_ptr<Input> getInput() const;
        std::shared_ptr<GUI> getGUI() const;

        // Unified operations
        bool startVideoCapture();
        bool stopVideoCapture();
        bool startInputForwarding();
        bool stopInputForwarding();
        bool startGUI();
        bool stopGUI();

        // Complete KVM session
        bool startKVMSession(); // Video + Input + GUI
        void stopKVMSession();
        bool isKVMSessionActive() const;

        // Information
        KVMDeviceInfo getDeviceInfo() const;
        std::string getDeviceDescription() const;

      private:
        class Impl;
        std::unique_ptr<Impl> pImpl;
    };

} // namespace openterface
