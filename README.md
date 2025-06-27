# Openterface CLI

A lightweight, cross-platform CLI implementation of the Openterface Mini-KVM project, providing core KVM functionality without Qt dependencies through a native Wayland interface.

## Overview

Openterface CLI is a command-line first alternative to the Qt-based GUI version, designed to provide essential KVM (Keyboard, Video, Mouse) functionality for controlling target devices through the Openterface hardware. The implementation focuses on performance, minimal dependencies, and native Linux integration.

## Features

### ✅ Core KVM Functionality
- **Video Capture**: V4L2-based capture from MS2109 video chip with MJPEG/YUYV support
- **Input Forwarding**: Wayland-based keyboard and mouse control with modifier key support
- **Serial Communication**: CH9329 chip integration for keyboard/mouse commands
- **Device Management**: Auto-detection and connection to Openterface devices
- **Native Display**: GPU-accelerated video rendering through Wayland/OpenGL ES

### 🚧 In Development
- Audio capture and forwarding
- Advanced video features (recording, screenshots)
- Cross-platform support (Windows, macOS)

## Hardware Support

- **Video Capture**: MS2109 chip via V4L2
- **Serial Control**: CH9329 keyboard/mouse chip via USB-to-serial
- **USB Communication**: CH340/CH341 USB-to-serial converters
- **Target Connection**: USB HID device forwarding

## Requirements

### System Dependencies
```bash
# Ubuntu/Debian
sudo apt install build-essential cmake
sudo apt install libwayland-dev wayland-protocols
sudo apt install libegl1-mesa-dev libgles2-mesa-dev
sudo apt install libjpeg-dev
sudo apt install v4l-utils

# Arch Linux
sudo pacman -S base-devel cmake
sudo pacman -S wayland wayland-protocols
sudo pacman -S mesa glu
sudo pacman -S libjpeg-turbo
sudo pacman -S v4l-utils
```

### Hardware Permissions
```bash
# Add user to video and dialout groups
sudo usermod -a -G video,dialout $USER

# Create udev rules (optional)
echo 'SUBSYSTEM=="video4linux", GROUP="video", MODE="0664"' | sudo tee /etc/udev/rules.d/99-openterface.rules
echo 'SUBSYSTEM=="tty", ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="7523", GROUP="dialout", MODE="0664"' | sudo tee -a /etc/udev/rules.d/99-openterface.rules
sudo udevadm control --reload-rules
```

## Building

### Quick Build
```bash
# Clone and build
git clone <repository-url>
cd openterface
make build

# Run
./build/openterface-cli connect
```

### Manual Build
```bash
# Configure
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build -j$(nproc)

# Install (optional)
sudo cmake --install build
```

### Build Options
```bash
# Enable examples
cmake -B build -DOPENTERFACE_BUILD_EXAMPLES=ON

# Enable tests
cmake -B build -DOPENTERFACE_ENABLE_TESTS=ON

# Debug build
cmake -B build -DCMAKE_BUILD_TYPE=Debug
```

## Usage

### Basic Commands
```bash
# Connect to Openterface device
./openterface-cli connect

# Verbose mode for debugging
./openterface-cli connect --verbose

# GUI-only mode (no serial control)
./openterface-cli connect --dummy
```

### Hardware Verification
```bash
# Check video devices
v4l2-ctl --list-devices

# Check USB serial devices
ls /dev/ttyUSB* /dev/ttyACM*

# Test video capture
v4l2-ctl --device=/dev/video0 --list-formats-ext
```

## Architecture

### Core Modules
- **KVM Manager** (`kvm.hpp/cpp`) - Device coordination and management
- **Serial Module** (`serial.hpp/cpp`) - CH9329 communication protocol
- **Video Module** (`video.hpp/cpp`) - V4L2 video capture and processing
- **Input Module** (`input.hpp/cpp`) - Wayland input capture and forwarding
- **GUI Module** (`gui.hpp/cpp`) - Native Wayland display interface
- **CLI Module** (`cli.hpp/cpp`) - Command-line interface using CLI11

### Technology Stack
- **Language**: C++20
- **Build System**: CMake with FetchContent
- **GUI Framework**: Native Wayland + OpenGL ES
- **Video Backend**: V4L2 (Video4Linux2)
- **Serial Backend**: POSIX serial I/O
- **Dependencies**: CLI11, Wayland, EGL, OpenGL ES, libjpeg

## Hardware Protocol

### CH9329 Communication
The CH9329 chip is controlled via USB-to-serial interface using a custom binary protocol:

```bash
# Initialize connection
VID:PID: 0x1A86:0x7523
Baudrate: 115200 (fallback to 9600)
Format: 8N1

# Mouse command format (absolute)
57 AB 00 04 07 02 [button] [x_low] [x_high] [y_low] [y_high] [wheel] [checksum]

# Keyboard command format
57 AB 00 02 08 [modifiers] 00 [key1] [key2] [key3] [key4] [key5] [key6] [checksum]
```

### Video Capture
```bash
# Supported formats
- MJPEG: Hardware-compressed, lower CPU usage
- YUYV: Uncompressed, higher quality

# Common resolutions
- 1920x1080 @ 30fps
- 1280x720 @ 60fps
- 640x480 @ 60fps
```

## Development

### Project Structure
```
openterface/
├── include/openterface/     # Header files
├── src/                     # Implementation files
├── examples/               # Example programs
├── CMakeLists.txt          # Build configuration
├── Makefile                # Convenience wrapper
└── README.md               # This file
```

### Contributing
1. Fork the repository
2. Create a feature branch
3. Follow existing code style (C++20, RAII, smart pointers)
4. Add tests for new features
5. Submit a pull request

### Testing
```bash
# Build with tests enabled
cmake -B build -DOPENTERFACE_ENABLE_TESTS=ON
cmake --build build

# Run tests
ctest --test-dir build
```

## Troubleshooting

### Common Issues

**Video not displaying**:
- Check V4L2 device permissions: `ls -l /dev/video*`
- Verify device detection: `v4l2-ctl --list-devices`
- Test capture: `v4l2-ctl --device=/dev/video0 --stream-mmap`

**Mouse/keyboard not working**:
- Check serial device permissions: `ls -l /dev/ttyUSB*`
- Verify CH9329 connection: Monitor serial output with verbose mode
- Ensure target device is connected to Openterface USB port

**Build failures**:
- Install all dependencies: `sudo apt install <package-list>`
- Update CMake: Requires CMake 3.15+
- Check compiler: Requires C++20 support (GCC 10+, Clang 12+)

**Wayland issues**:
- Ensure running on Wayland session: `echo $XDG_SESSION_TYPE`
- Install Wayland development libraries
- Check compositor compatibility

### Debug Mode
```bash
# Enable verbose logging
./openterface-cli connect --verbose

# Monitor V4L2 activity
sudo dmesg | grep -i video

# Monitor USB serial
sudo dmesg | grep -i ttyUSB
```

## Roadmap

### Near-term Goals
- [ ] Audio capture and forwarding
- [ ] Video recording and screenshot capabilities
- [ ] Configuration file support
- [ ] Improved error handling and recovery

### Long-term Goals
- [ ] Cross-platform support (Windows, macOS)
- [ ] Script automation system
- [ ] Network remote control
- [ ] Firmware update capabilities
- [ ] Multi-device support

## License

[License information - please specify the appropriate license for your project]

## Acknowledgments

- Original Openterface project team
- V4L2 and Wayland communities
- Contributors and testers

## Support

- **Issues**: [Repository issue tracker]
- **Documentation**: See `FEATURES.md` for detailed implementation status
- **Hardware Protocol**: See `PROGRESS.md` for CH9329 communication details

---

*This project provides a lightweight, performant alternative to GUI-based KVM solutions, optimized for server environments and embedded systems where minimal dependencies and command-line operation are preferred.*