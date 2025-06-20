#include "openterface/kvm.hpp"
#include "openterface/gui.hpp"
#include "openterface/input.hpp"
#include "openterface/serial.hpp"
#include "openterface/video.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>

namespace openterface {

    class KVMManager::Impl {
      public:
        std::shared_ptr<Serial> serial;
        std::shared_ptr<Video> video;
        std::shared_ptr<Input> input;
        std::shared_ptr<GUI> gui;

        KVMDeviceInfo device_info;
        bool kvm_session_active = false;

        void log(const std::string &msg) { std::cout << "[KVM] " << msg << std::endl; }

        // Check if a device is an Openterface device by vendor/product ID
        bool isOpenterfaceDevice(const std::string &vid, const std::string &pid) {
            // Openterface Mini KVM USB identifiers
            // Note: These would need to be verified with actual hardware
            return (vid == "6666" && pid == "6666") || // Example IDs
                   (vid == "534d" && pid == "2109");   // Another possible set
        }

        std::vector<KVMDevice> scanUSBDevices();
        std::string readFileContent(const std::string &path);
    };

    KVMManager::KVMManager() : pImpl(std::make_unique<Impl>()) {
        pImpl->serial = std::make_shared<Serial>();
        pImpl->video = std::make_shared<Video>();
        pImpl->input = std::make_shared<Input>();
        pImpl->gui = std::make_shared<GUI>();

        // Set up connections between modules
        pImpl->input->setSerial(pImpl->serial);
        pImpl->gui->setVideoSource(pImpl->video);
        pImpl->gui->setInputTarget(pImpl->input);
        pImpl->gui->setSerialForwarder(pImpl->serial);
    }

    KVMManager::~KVMManager() { disconnect(); }

    std::vector<KVMDevice> KVMManager::scanForDevices() {
        pImpl->log("Scanning for Openterface devices...");
        return pImpl->scanUSBDevices();
    }

    bool KVMManager::isOpenterfaceDevice(const std::string &device_path) {
        // This would check device attributes to identify Openterface devices
        pImpl->log("Checking if " + device_path + " is an Openterface device");
        return true; // Placeholder implementation
    }

    bool KVMManager::connect(const std::string &device_id) {
        if (device_id.empty()) {
            // Auto-detect mode
            pImpl->log("Auto-detecting Openterface device...");
            auto devices = scanForDevices();
            if (devices.empty()) {
                pImpl->log("No Openterface devices found");
                return false;
            }

            // Use the first device found
            const auto &device = devices[0];
            return connectByPaths(device.serial_path, device.video_path);
        } else {
            // Connect by specific device ID
            auto devices = scanForDevices();
            for (const auto &device : devices) {
                if (device.device_id == device_id) {
                    return connectByPaths(device.serial_path, device.video_path);
                }
            }
            pImpl->log("Device " + device_id + " not found");
            return false;
        }
    }

    bool KVMManager::connectByPaths(const std::string &serial_path, const std::string &video_path) {
        pImpl->log("Connecting to KVM device:");
        pImpl->log("  Serial: " + serial_path);
        pImpl->log("  Video: " + video_path);

        // Connect serial (for keyboard/mouse control)
        if (!pImpl->serial->connect(serial_path, 115200)) {
            pImpl->log("Failed to connect to serial device");
            return false;
        }

        // Connect video (for capture)
        if (!pImpl->video->connect(video_path)) {
            pImpl->log("Failed to connect to video device");
            pImpl->serial->disconnect();
            return false;
        }

        // Connect input (Wayland input capture)
        if (!pImpl->input->connectWayland()) {
            pImpl->log("Failed to connect to Wayland input");
            // This is not fatal - we can still do manual input via CLI
        }

        pImpl->device_info.serial_path = serial_path;
        pImpl->device_info.video_path = video_path;
        pImpl->device_info.connected = true;
        pImpl->device_info.description = "Openterface Mini KVM";

        pImpl->log("KVM device connected successfully!");
        return true;
    }

    void KVMManager::disconnect() {
        if (!pImpl->device_info.connected) {
            return;
        }

        pImpl->log("Disconnecting KVM device...");

        stopKVMSession();

        if (pImpl->input)
            pImpl->input->disconnectWayland();
        if (pImpl->video)
            pImpl->video->disconnect();
        if (pImpl->serial)
            pImpl->serial->disconnect();

        pImpl->device_info.connected = false;
        pImpl->device_info.video_active = false;
        pImpl->device_info.input_active = false;
        pImpl->device_info.gui_active = false;

        pImpl->log("KVM device disconnected");
    }

    bool KVMManager::isConnected() const { return pImpl->device_info.connected; }

    std::shared_ptr<Serial> KVMManager::getSerial() const { return pImpl->serial; }

    std::shared_ptr<Video> KVMManager::getVideo() const { return pImpl->video; }

    std::shared_ptr<Input> KVMManager::getInput() const { return pImpl->input; }

    std::shared_ptr<GUI> KVMManager::getGUI() const { return pImpl->gui; }

    bool KVMManager::startVideoCapture() {
        if (!pImpl->device_info.connected) {
            pImpl->log("Device not connected");
            return false;
        }

        if (pImpl->video->startCapture()) {
            pImpl->device_info.video_active = true;
            pImpl->log("Video capture started");
            return true;
        }
        return false;
    }

    bool KVMManager::stopVideoCapture() {
        if (pImpl->device_info.video_active) {
            pImpl->video->stopCapture();
            pImpl->device_info.video_active = false;
            pImpl->log("Video capture stopped");
        }
        return true;
    }

    bool KVMManager::startInputForwarding() {
        if (!pImpl->device_info.connected) {
            pImpl->log("Device not connected");
            return false;
        }

        if (pImpl->input->startCapture()) {
            pImpl->device_info.input_active = true;
            pImpl->log("Input forwarding started");
            return true;
        }
        return false;
    }

    bool KVMManager::stopInputForwarding() {
        if (pImpl->device_info.input_active) {
            pImpl->input->stopCapture();
            pImpl->device_info.input_active = false;
            pImpl->log("Input forwarding stopped");
        }
        return true;
    }

    bool KVMManager::startGUI() {
        if (!pImpl->device_info.connected) {
            pImpl->log("Device not connected");
            return false;
        }

        if (!pImpl->gui->initialize()) {
            pImpl->log("Failed to initialize GUI");
            return false;
        }

        if (!pImpl->gui->createWindow("Openterface KVM", 1920, 1080)) {
            pImpl->log("Failed to create GUI window");
            return false;
        }

        pImpl->device_info.gui_active = true;
        pImpl->log("GUI started");
        return true;
    }

    bool KVMManager::stopGUI() {
        if (pImpl->device_info.gui_active) {
            pImpl->gui->shutdown();
            pImpl->device_info.gui_active = false;
            pImpl->log("GUI stopped");
        }
        return true;
    }

    bool KVMManager::startKVMSession() {
        if (!pImpl->device_info.connected) {
            pImpl->log("Device not connected");
            return false;
        }

        pImpl->log("Starting complete KVM session...");

        // Start all components
        bool success = true;

        if (!startVideoCapture()) {
            pImpl->log("Warning: Video capture failed");
            success = false;
        }

        if (!startInputForwarding()) {
            pImpl->log("Warning: Input forwarding failed");
            // Not fatal - can still use CLI input
        }

        if (!startGUI()) {
            pImpl->log("Warning: GUI failed");
            // Not fatal for CLI usage
        } else {
            // Connect GUI to video and input
            pImpl->gui->startVideoDisplay();
            pImpl->gui->startInputCapture();
        }

        pImpl->kvm_session_active = success;

        if (success) {
            pImpl->log("KVM session started successfully!");
        } else {
            pImpl->log("KVM session started with some failures");
        }

        return success;
    }

    void KVMManager::stopKVMSession() {
        if (pImpl->kvm_session_active) {
            pImpl->log("Stopping KVM session...");

            stopGUI();
            stopInputForwarding();
            stopVideoCapture();

            pImpl->kvm_session_active = false;
            pImpl->log("KVM session stopped");
        }
    }

    bool KVMManager::isKVMSessionActive() const { return pImpl->kvm_session_active; }

    KVMDeviceInfo KVMManager::getDeviceInfo() const { return pImpl->device_info; }

    std::string KVMManager::getDeviceDescription() const { return pImpl->device_info.description; }

    // Implementation of USB device scanning
    std::vector<KVMDevice> KVMManager::Impl::scanUSBDevices() {
        std::vector<KVMDevice> devices;

        try {
            // Scan /sys/bus/usb/devices for USB devices
            const std::string usb_devices_path = "/sys/bus/usb/devices";

            if (!std::filesystem::exists(usb_devices_path)) {
                log("USB devices path not found: " + usb_devices_path);
                return devices;
            }

            for (const auto &entry : std::filesystem::directory_iterator(usb_devices_path)) {
                if (!entry.is_directory())
                    continue;

                std::string device_path = entry.path().string();
                std::string vendor_id = readFileContent(device_path + "/idVendor");
                std::string product_id = readFileContent(device_path + "/idProduct");

                if (vendor_id.empty() || product_id.empty())
                    continue;

                // Check if this is an Openterface device
                if (isOpenterfaceDevice(vendor_id, product_id)) {
                    KVMDevice device;
                    device.device_id = std::filesystem::path(device_path).filename();
                    device.vendor_id = vendor_id;
                    device.product_id = product_id;
                    device.serial_number = readFileContent(device_path + "/serial");
                    device.description = "Openterface Mini KVM (" + vendor_id + ":" + product_id + ")";

                    // Try to find corresponding /dev entries
                    // This is simplified - real implementation would need better device mapping
                    device.serial_path = "/dev/ttyUSB0"; // First guess
                    device.video_path = "/dev/video0";   // First guess

                    devices.push_back(device);
                    log("Found Openterface device: " + device.description);
                }
            }
        } catch (const std::exception &e) {
            log("Error scanning USB devices: " + std::string(e.what()));
        }

        // Fallback: Add a default device for testing
        if (devices.empty()) {
            KVMDevice device;
            device.device_id = "default";
            device.vendor_id = "unknown";
            device.product_id = "unknown";
            device.serial_path = "/dev/ttyUSB0";
            device.video_path = "/dev/video0";
            device.description = "Default Openterface Device";
            devices.push_back(device);
            log("No devices found, using default configuration");
        }

        return devices;
    }

    std::string KVMManager::Impl::readFileContent(const std::string &path) {
        try {
            std::ifstream file(path);
            if (!file.is_open())
                return "";

            std::string content;
            std::getline(file, content);

            // Remove whitespace
            content.erase(0, content.find_first_not_of(" \t\n\r"));
            content.erase(content.find_last_not_of(" \t\n\r") + 1);

            return content;
        } catch (...) {
            return "";
        }
    }

} // namespace openterface
