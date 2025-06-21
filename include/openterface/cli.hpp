#pragma once

#include <CLI/CLI.hpp>
#include <memory>
#include <string>

namespace openterface {

    // Forward declarations
    class Serial;
    class Video;
    class Input;
    class GUI;

    class CLI {
      public:
        CLI();
        ~CLI();

        int run(int argc, char **argv);

      private:
        std::string version = "1.0.0";
        ::CLI::App app;
        bool verbose = false;
        bool dummy_mode = false;
        bool debug_input = false;
        std::string serial_port;
        std::string video_device;

        // Module instances
        std::unique_ptr<Serial> serial;
        std::unique_ptr<Video> video;
        std::unique_ptr<Input> input;
        std::unique_ptr<GUI> gui;

        void setupCommands();

        // Device detection helpers
        std::string getVideoDeviceName(const std::string &device_path);
        std::vector<std::string> findOpenterfaceSerialPorts();
    };

} // namespace openterface
