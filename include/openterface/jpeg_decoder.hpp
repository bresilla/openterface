#pragma once

#include <vector>
#include <cstdint>
#include <string>

namespace openterface {

struct DecodedFrame {
    std::vector<uint8_t> rgb_data;  // RGB24 pixel data
    int width;
    int height;
    int channels;  // Should be 3 for RGB
};

class JpegDecoder {
public:
    JpegDecoder();
    ~JpegDecoder();

    // Decode MJPEG frame to RGB24
    bool decode(const uint8_t* jpeg_data, size_t jpeg_size, DecodedFrame& output);
    
    // Get last error message
    std::string getLastError() const;

private:
    std::string last_error;
};

} // namespace openterface