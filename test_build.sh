#!/bin/bash
# Test build script to verify the refactored GUI compiles

# Create build directory
mkdir -p build
cd build

# Configure with examples enabled
cmake .. -DOPENTERFACE_BUILD_EXAMPLES=ON

# Build
make -j$(nproc)

# Check if the build succeeded
if [ $? -eq 0 ]; then
    echo "Build succeeded!"
else
    echo "Build failed!"
    exit 1
fi