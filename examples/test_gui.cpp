#include "openterface/gui.hpp"
#include <chrono>
#include <iostream>
#include <thread>
#include <csignal>
#include <cstdlib>
#include <cstring>

void signal_handler(int signal) {
    std::cerr << "\n🚨 CRASH DETECTED - Signal " << signal << " received" << std::endl;
    std::cerr << "This indicates a segmentation fault during resize operation." << std::endl;
    std::cerr << "Please report this crash with the log output above." << std::endl;
    std::exit(signal);
}

int main(int argc, char* argv[]) {
    // Check for --debug flag
    bool debug_mode = false;
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--debug") == 0) {
            debug_mode = true;
            break;
        }
    }
    // Install signal handler for crash detection
    std::signal(SIGSEGV, signal_handler);
    std::signal(SIGABRT, signal_handler);
    
    std::cout << "Starting Openterface GUI test..." << std::endl;

    openterface::GUI gui;

    // Initialize the GUI
    if (!gui.initialize()) {
        std::cerr << "Failed to initialize GUI" << std::endl;
        return 1;
    }

    std::cout << "GUI initialized successfully" << std::endl;

    // Enable debug mode if requested
    if (debug_mode) {
        gui.setDebugMode(true);
        std::cout << "Debug mode enabled - input events will be logged" << std::endl;
    }

    // Create a window with reasonable initial size
    if (!gui.createWindow("Openterface KVM Test", 800, 600)) {
        std::cerr << "Failed to create window" << std::endl;
        return 1;
    }

    std::cout << "Window created successfully" << std::endl;
    std::cout << "You should now see a window with a gradient pattern." << std::endl;
    std::cout << "Try the following:" << std::endl;
    std::cout << "1. Move your mouse over the window - cursor should remain visible" << std::endl;
    std::cout << "2. Move mouse to window edges - you should see resize detection messages" << std::endl;
    std::cout << "3. Click and drag from window edges to resize" << std::endl;
    std::cout << "4. The gradient pattern should scale with the window" << std::endl;
    std::cout << "Press Ctrl+C to exit." << std::endl;

    // Run the event loop (blocking)
    return gui.runEventLoop();
}
