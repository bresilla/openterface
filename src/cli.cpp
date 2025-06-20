#include "openterface/cli.hpp"
#include "openterface/gui.hpp"
#include "openterface/input.hpp"
#include "openterface/serial.hpp"
#include "openterface/video.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
#include <unistd.h> // for access()
#include <vector>

// Linux headers for device detection
#ifdef __linux__
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#endif

namespace openterface {

    CLI::CLI()
        : app("Openterface USB KVM CLI", "openterface") {
        std::cout << "DEBUG: CLI constructor - starting" << std::endl;
        
        std::cout << "DEBUG: Creating Serial" << std::endl;
        serial = std::make_unique<Serial>();
        
        std::cout << "DEBUG: Creating Video" << std::endl;
        video = std::make_unique<Video>();
        
        std::cout << "DEBUG: Creating Input" << std::endl;
        input = std::make_unique<Input>();
        
        std::cout << "DEBUG: Creating GUI" << std::endl;
        gui = std::make_unique<GUI>();
        
        std::cout << "DEBUG: Setting version flag" << std::endl;
        app.set_version_flag("--version", version);
        
        std::cout << "DEBUG: Setting up commands" << std::endl;
        setupCommands();
        
        std::cout << "DEBUG: CLI constructor - complete" << std::endl;
    }

    CLI::~CLI() = default;

    void CLI::setupCommands() {
        // Global options
        app.add_flag("-v,--verbose", verbose, "Enable verbose output");

        // Connect command - simplified unified connection
        auto connect_cmd = app.add_subcommand("connect", "Connect to KVM device");
        connect_cmd->add_option("--video", video_device, "Video device path")->default_val("/dev/video0");
        connect_cmd->add_option("--serial", serial_port, "Serial device path")->default_val("/dev/ttyACM0");
        connect_cmd->add_flag("--dummy", dummy_mode, "Run in dummy mode (no device connection, GUI only)");
        connect_cmd->callback([this]() {
            std::cout << "DEBUG: Enter connect callback" << std::endl;
            
            if (verbose)
                std::cout << "Verbose mode enabled\n";

            std::cout << "DEBUG: Checked verbose flag" << std::endl;

            if (dummy_mode) {
                std::cout << "Starting Openterface KVM in dummy mode..." << std::endl;
                std::cout << "No device connections will be made." << std::endl;
            } else {
                std::cout << "DEBUG: About to start device connection logic" << std::endl;
                std::cout << "Connecting to Openterface KVM..." << std::endl;

                // Auto-detect devices if defaults are used
                if (video_device == "/dev/video0" || serial_port == "/dev/ttyACM0") {
                    auto detected_serials = findOpenterfaceSerialPorts();

                    // Auto-detect video device
                    std::string detected_video;
                    for (int i = 0; i < 10; i++) {
                        std::string device = "/dev/video" + std::to_string(i);
                        if (access(device.c_str(), F_OK) == 0) {
                            std::string device_name = getVideoDeviceName(device);
                            if (device_name.find("Openterface") != std::string::npos) {
                                detected_video = device;
                                break;
                            }
                        }
                    }

                    if (!detected_serials.empty() && serial_port == "/dev/ttyACM0") {
                        serial_port = detected_serials[0];
                        std::cout << "Auto-detected serial: " << serial_port << std::endl;
                    }

                    if (!detected_video.empty() && video_device == "/dev/video0") {
                        video_device = detected_video;
                        std::cout << "Auto-detected video: " << video_device << std::endl;
                    }
                }

                std::cout << "Video: " << video_device << std::endl;
                std::cout << "Serial: " << serial_port << std::endl;

                // Connect video
                if (video->connect(video_device)) {
                    std::cout << "✓ Video connected" << std::endl;
                } else {
                    std::cout << "✗ Video connection failed" << std::endl;
                    return;
                }

                // Connect serial
                if (serial->connect(serial_port, 115200)) {
                    std::cout << "✓ Serial connected" << std::endl;
                } else {
                    std::cout << "✗ Serial connection failed" << std::endl;
                    video->disconnect();
                    return;
                }

                // Setup input forwarding
                input->setSerial(std::shared_ptr<Serial>(serial.get(), [](Serial *) {}));
            }

            std::cout << "DEBUG: About to initialize GUI" << std::endl;
            
            // Initialize and start GUI (both dummy and real modes)
            if (!gui->initialize()) {
                std::cout << "✗ Failed to initialize GUI" << std::endl;
                return;
            }
            std::cout << "✓ GUI initialized" << std::endl;

            std::cout << "DEBUG: About to create window" << std::endl;
            
            // Create window
            std::string window_title = dummy_mode ? "Openterface KVM - Dummy Mode" : "Openterface KVM";
            if (!gui->createWindow(window_title, 1920, 1080)) {
                std::cout << "✗ Failed to create window" << std::endl;
                std::cout << "DEBUG: Window creation failed, about to shutdown GUI" << std::endl;
                gui->shutdown();
                return;
            }
            std::cout << "✓ Window created" << std::endl;

            std::cout << "DEBUG: About to setup video display" << std::endl;

            // Setup video display
            gui->setVideoSource(std::shared_ptr<Video>(video.get(), [](Video *) {}));
            if (gui->startVideoDisplay()) {
                if (dummy_mode) {
                    std::cout << "✓ Video display started (dummy mode - black screen)" << std::endl;
                } else {
                    std::cout << "✓ Video display started" << std::endl;
                }
            } else {
                std::cout << "✗ Failed to start video display" << std::endl;
            }

            // Setup input capture and forwarding
            gui->setInputTarget(std::shared_ptr<Input>(input.get(), [](Input *) {}));
            gui->setSerialForwarder(std::shared_ptr<Serial>(serial.get(), [](Serial *) {}));
            if (gui->startInputCapture()) {
                std::cout << "✓ Input capture started (keyboard/mouse will be forwarded)" << std::endl;
            } else {
                std::cout << "✗ Failed to start input capture" << std::endl;
            }

            std::cout << "\n=== KVM Ready ===" << std::endl;
            if (dummy_mode) {
                std::cout << "- Running in dummy mode (no device connections)" << std::endl;
                std::cout << "- Video will show black screen" << std::endl;
            } else {
                std::cout << "- Device connections established" << std::endl;
                std::cout << "- Video feed active" << std::endl;
            }
            std::cout << "- Input (keyboard/mouse) will be captured and forwarded" << std::endl;
            std::cout << "- Close window or press Ctrl+C to exit" << std::endl;

            std::cout << "DEBUG: About to run GUI event loop" << std::endl;
            
            // Run the GUI event loop (blocking)
            int result = gui->runEventLoop();
            std::cout << "\nGUI exited with code: " << result << std::endl;

            std::cout << "DEBUG: GUI event loop finished, starting cleanup" << std::endl;
            
            // Cleanup
            gui->stopInputCapture();
            std::cout << "DEBUG: Input capture stopped" << std::endl;
            
            gui->stopVideoDisplay();
            std::cout << "DEBUG: Video display stopped" << std::endl;
            
            gui->destroyWindow();
            std::cout << "DEBUG: Window destroyed" << std::endl;
            
            gui->shutdown();
            std::cout << "DEBUG: GUI shutdown complete" << std::endl;

            if (!dummy_mode) {
                std::cout << "DEBUG: Disconnecting devices" << std::endl;
                video->disconnect();
                serial->disconnect();
            }

            std::cout << "✓ Cleanup complete" << std::endl;
        });

        // Status command
        auto status_cmd = app.add_subcommand("status", "Show device status");
        status_cmd->callback([this]() {
            if (verbose)
                std::cout << "Verbose mode enabled\n";

            auto serial_info = serial->getInfo();
            auto video_info = video->getInfo();

            std::cout << "=== Openterface KVM Status ===" << std::endl;
            std::cout << "Serial: " << (serial_info.connected ? "CONNECTED" : "DISCONNECTED");
            if (serial_info.connected) {
                std::cout << " (" << serial_info.port_name << " @ " << serial_info.baudrate << ")";
            }
            std::cout << std::endl;

            std::cout << "Video: " << (video_info.connected ? "CONNECTED" : "DISCONNECTED");
            if (video_info.connected) {
                std::cout << " (" << video_info.width << "x" << video_info.height << " " << video_info.format << ")";
            }
            std::cout << std::endl;

            std::cout << "Target: " << (serial_info.target_connected ? "RESPONSIVE" : "NO RESPONSE") << std::endl;
        });

        // Scan command - find Openterface devices
        auto scan_cmd = app.add_subcommand("scan", "Scan for Openterface devices");
        scan_cmd->callback([this]() {
            if (verbose)
                std::cout << "Verbose mode enabled\n";

            std::cout << "Scanning for Openterface USB KVM devices..." << std::endl;

            // Find video devices by checking device description
            std::cout << "\n=== Video Devices ===" << std::endl;
            std::vector<std::string> video_paths;
            for (int i = 0; i < 10; i++) {
                std::string device = "/dev/video" + std::to_string(i);
                if (access(device.c_str(), F_OK) == 0) {
                    // Check if this is an Openterface device by reading device info
                    std::string device_name = getVideoDeviceName(device);
                    if (device_name.find("Openterface") != std::string::npos) {
                        std::cout << "Found: " << device << " (" << device_name << ")" << std::endl;
                        video_paths.push_back(device);
                    } else if (verbose) {
                        std::cout << "Found: " << device << " (" << device_name << ") - not Openterface" << std::endl;
                    }
                }
            }

            // Find serial devices by checking VID:PID
            std::cout << "\n=== Serial Devices ===" << std::endl;
            std::vector<std::string> serial_paths = findOpenterfaceSerialPorts();
            for (const auto &device : serial_paths) {
                std::cout << "Found: " << device << " (VID:PID 0x1A86:0x7523)" << std::endl;
            }

            std::cout << "\n=== Recommended Connection ===" << std::endl;
            if (!video_paths.empty() && !serial_paths.empty()) {
                std::cout << "Try: openterface connect --video=" << video_paths[0] << " --serial=" << serial_paths[0]
                          << std::endl;
            } else {
                std::cout << "No Openterface devices detected." << std::endl;
                std::cout << "Ensure device is plugged in and recognized by the system." << std::endl;
                std::cout << "Or use: openterface connect --dummy" << std::endl;
            }
        });

        app.require_subcommand(1);
    }

    int CLI::run(int argc, char **argv) {
        try {
            app.parse(argc, argv);
            return 0;
        } catch (const ::CLI::ParseError &e) {
            return app.exit(e);
        }
    }

    std::string CLI::getVideoDeviceName(const std::string &device_path) {
#ifdef __linux__
        int fd = open(device_path.c_str(), O_RDONLY);
        if (fd == -1) {
            return "Unknown";
        }

        struct v4l2_capability cap;
        if (ioctl(fd, VIDIOC_QUERYCAP, &cap) == -1) {
            close(fd);
            return "Unknown";
        }

        close(fd);
        return std::string(reinterpret_cast<char *>(cap.card));
#else
        return "Unknown";
#endif
    }

    std::vector<std::string> CLI::findOpenterfaceSerialPorts() {
        std::vector<std::string> openterface_ports;

#ifdef __linux__
        // Read /sys/class/tty entries to find CH341 devices (VID:PID 0x1A86:0x7523)
        DIR *dir = opendir("/sys/class/tty");
        if (!dir) {
            return openterface_ports;
        }

        struct dirent *entry;
        while ((entry = readdir(dir)) != nullptr) {
            if (strncmp(entry->d_name, "ttyUSB", 6) == 0 || strncmp(entry->d_name, "ttyACM", 6) == 0) {

                std::string uevent_path = "/sys/class/tty/" + std::string(entry->d_name) + "/device/../uevent";
                std::ifstream uevent_file(uevent_path);

                if (uevent_file.is_open()) {
                    std::string line;
                    bool is_openterface = false;

                    while (std::getline(uevent_file, line)) {
                        // Check for CH341 VID:PID (Openterface serial device)
                        if (line.find("PRODUCT=1a86/7523/") == 0) {
                            is_openterface = true;
                            break;
                        }
                    }

                    if (is_openterface) {
                        std::string device_path = "/dev/" + std::string(entry->d_name);
                        if (access(device_path.c_str(), F_OK) == 0) {
                            openterface_ports.push_back(device_path);
                        }
                    }
                }
            }
        }

        closedir(dir);
#endif

        return openterface_ports;
    }

} // namespace openterface
