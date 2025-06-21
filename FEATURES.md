# Openterface CLI - Feature Implementation Status

## Overview

This is a CLI implementation of the Openterface Mini-KVM project, designed to provide a lightweight, cross-platform alternative to the Qt-based GUI version. The implementation focuses on core KVM functionality while removing Qt dependencies and providing a command-line first interface.

## Architecture

**Language**: C++20  
**Build System**: CMake  
**Primary Dependencies**: CLI11, Wayland (Linux)  
**Target Platform**: Linux (with V4L2 and Wayland support)

### Core Modules

- **KVM Manager** (`kvm.hpp/cpp`) - Central coordination and device management
- **Serial Module** (`serial.hpp/cpp`) - CH9329 chip communication via USB-to-serial
- **Video Module** (`video.hpp/cpp`) - V4L2-based video capture from MS2109 chip
- **Input Module** (`input.hpp/cpp`) - Wayland input capture and forwarding
- **GUI Module** (`gui.hpp/cpp`) - Native Wayland display interface
- **CLI Module** (`cli.hpp/cpp`) - Command-line interface using CLI11

## Feature Implementation Status

### ✅ **Currently Implemented**

#### Core KVM Functionality
- **Device Connection**: Auto-detection and connection to Openterface devices
- **Serial Communication**: CH9329 protocol implementation for keyboard/mouse control
- **Video Capture**: V4L2-based capture from MS2109 video chip
  - Multiple format support (MJPG, YUYV)
  - Resolution and framerate configuration
  - Frame capture with callback system
- **Input Forwarding**: Wayland-based input capture and forwarding to target
  - Keyboard input with modifier keys
  - Mouse movement (absolute and relative modes)
  - Mouse button support

#### User Interface
- **Command-Line Interface**: CLI11-based command system
  - `connect` command with auto-detection
  - Verbose and dummy modes
- **GUI Display**: Native Wayland window for video display
  - Window management and resizing
  - Video frame rendering
  - Input capture from window

#### Development Infrastructure
- **Build System**: CMake with FetchContent for dependencies
- **Development Tools**: Makefile wrapper, changelog generation
- **Documentation**: Automated documentation generation support
- **Testing**: Framework setup (though tests not yet comprehensive)

### 🚧 **Partially Implemented**

#### Serial Communication
- Basic CH9329 protocol implemented but may need hardware validation
- Special commands (Ctrl+Alt+Del, HID reset) defined but not fully tested
- Some simulation/dummy modes present for development without hardware

#### Video Processing
- V4L2 integration complete but advanced features missing
- Basic display working but no recording/screenshot capabilities

### ❌ **Not Yet Implemented**

#### Missing from Original QT Version

**Audio Features**:
- Audio capture from target device
- Real-time audio streaming
- Volume control
- Audio device management

**Advanced Video Features**:
- Video recording
- Screenshot capture (full screen and area selection)
- Video zoom capabilities
- Picture-in-picture mode
- Full-screen mode enhancements

**USB Management**:
- Switchable USB port control
- USB device switching between host and target
- USB device status monitoring

**Firmware Management**:
- Firmware version checking
- Firmware updates
- Factory reset capabilities
- Hardware information modification (VID/PID)

**Scripting System**:
- Script editor and execution engine
- Script commands (Sleep, Send, Click, etc.)
- Script file management
- Automation capabilities

**Network Features**:
- TCP server for remote control
- Remote command execution
- Network-based script execution
- Status reporting to remote clients

**Advanced Configuration**:
- Preferences/settings system
- Multi-language support (6 languages in Qt version)
- Keyboard layout management
- Logging configuration and viewer

**System Integration**:
- Cross-platform support (Windows, other Linux distros)
- Clipboard integration
- Screen saver inhibition
- Status monitoring and indicators

**User Interface Enhancements**:
- Advanced menu systems
- Toolbar with quick access
- Status bar with detailed information
- Multiple keyboard layout support

## Hardware Integration Status

### ✅ **Supported Hardware**
- **Video Capture**: MS2109 chip via V4L2
- **Serial Control**: CH9329 keyboard/mouse chip via USB-to-serial
- **USB HID**: Basic HID device communication

### ❌ **Missing Hardware Support**
- CH340/CH341 USB-to-serial specific optimizations
- GPIO control for hardware switching
- Hardware switch status detection
- Advanced HID device management

## Command-Line Interface

### Current Commands
```bash
# Connect to Openterface device with auto-detection
./openterface-cli connect

# Verbose mode for debugging
./openterface-cli connect --verbose

# GUI-only mode (no serial control)
./openterface-cli connect --dummy
```

### Planned Commands (Not Implemented)
- Configuration management
- Script execution
- Firmware updates
- Audio control
- Recording/screenshot capture

## Build and Development

### Building the Project
```bash
# Configure build
cmake -B build

# Build all targets
cmake --build build

# Run main example
./build/examples/openterface-cli connect
```

### Development Tools
```bash
# Use Makefile wrapper for common tasks
make build      # Build project
make clean      # Clean build directory
make install    # Install built binaries
```

## Testing with Real Hardware

When testing with actual Openterface hardware:

1. **Video Connection**: Ensure MS2109 video capture device is detected by V4L2
2. **Serial Connection**: Verify CH9329 chip is accessible via USB-to-serial
3. **Permissions**: Set up proper udev rules for device access
4. **Dependencies**: Install required Wayland development libraries

### Required System Setup
```bash
# Install dependencies (Ubuntu/Debian)
sudo apt install build-essential cmake
sudo apt install libwayland-dev wayland-protocols
sudo apt install v4l-utils  # For video device testing

# Check video devices
v4l2-ctl --list-devices

# Check USB serial devices
ls /dev/ttyUSB* /dev/ttyACM*
```

## Migration Path from Qt Version

This CLI implementation provides a foundation for gradually porting features from the Qt version:

**Priority 1 (Core KVM)**:
- ✅ Video display and capture
- ✅ Keyboard/mouse input forwarding
- ✅ Basic device management

**Priority 2 (Essential Features)**:
- ❌ Audio capture and forwarding
- ❌ Advanced video features (recording, screenshots)
- ❌ Configuration management

**Priority 3 (Advanced Features)**:
- ❌ Scripting system
- ❌ Network control
- ❌ Firmware management
- ❌ Multi-platform support

## Contributing

The codebase is structured for easy contribution:
- Each module is self-contained with clear interfaces
- CMake build system supports incremental development
- Modern C++ practices with smart pointers and RAII
- Documentation generation support for API reference

For hardware testing, the modular design allows testing individual components (video, serial, input) independently before integration.