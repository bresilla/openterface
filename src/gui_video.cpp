#include "openterface/gui_video.hpp"
#include "openterface/jpeg_decoder.hpp"
#include "openterface/video.hpp"
#include <cstring>
#include <algorithm>

namespace openterface {

    VideoProcessor::VideoProcessor() : jpeg_decoder(std::make_unique<JpegDecoder>()) {}

    VideoProcessor::~VideoProcessor() = default;

    bool VideoProcessor::processFrame(const FrameData& frame, VideoFrame& output) {
        // Clear previous frame data first
        output.data.clear();
        output.width = 0;
        output.height = 0;
        output.is_rgb = false;

        if (!frame.data || frame.size == 0) {
            last_error = "Invalid frame data";
            return false;
        }

        // Try to decode MJPEG frame
        DecodedFrame decoded_frame;
        if (jpeg_decoder->decode(frame.data, frame.size, decoded_frame)) {
            // Successfully decoded MJPEG to RGB
            output.data.resize(decoded_frame.rgb_data.size());
            memcpy(output.data.data(), decoded_frame.rgb_data.data(), decoded_frame.rgb_data.size());
            output.width = decoded_frame.width;
            output.height = decoded_frame.height;
            output.is_rgb = true;
            return true;
        } else {
            last_error = "MJPEG decode failed: " + jpeg_decoder->getLastError();
            return false;
        }
    }

    void renderVideoToBuffer(void* buffer, int buffer_width, int buffer_height,
                            const VideoFrame& frame) {
        if (!buffer || buffer_width <= 0 || buffer_height <= 0 || 
            !frame.is_rgb || frame.data.empty() || frame.width <= 0 || frame.height <= 0) {
            return;
        }

        uint32_t* pixels = static_cast<uint32_t*>(buffer);
        const uint8_t* rgb_data = frame.data.data();

        // Validate RGB data size
        size_t expected_rgb_size = (size_t)(frame.width * frame.height * 3);
        if (frame.data.size() != expected_rgb_size) {
            return;
        }

        // Clear buffer to black
        size_t buffer_size = buffer_width * buffer_height * 4;
        memset(pixels, 0, buffer_size);

        // Calculate scaling to FILL the entire buffer (may stretch/warp to use full window)
        float scale_x = (float)buffer_width / (float)frame.width;
        float scale_y = (float)buffer_height / (float)frame.height;
        
        // Use full window space - scale independently for X and Y axes
        int scaled_width = buffer_width;   // Fill entire width
        int scaled_height = buffer_height; // Fill entire height
        int offset_x = 0; // No horizontal offset - use full width
        int offset_y = 0; // No vertical offset - use full height
        
        // OPTIMIZATION: Use fast 1:1 copy when no scaling needed (like QT does)
        if (scale_x >= 0.99f && scale_x <= 1.01f && scale_y >= 0.99f && scale_y <= 1.01f &&
            frame.width == buffer_width && frame.height == buffer_height) {
            // Direct copy - no scaling needed (fastest path)
            for (int y = 0; y < frame.height; y++) {
                const uint8_t* src_row = rgb_data + y * frame.width * 3;
                uint32_t* dst_row = pixels + y * buffer_width;
                
                for (int x = 0; x < frame.width; x++) {
                    const uint8_t* src_pixel = src_row + x * 3;
                    dst_row[x] = (0xFF << 24) | (src_pixel[0] << 16) | (src_pixel[1] << 8) | src_pixel[2];
                }
            }
            return;
        }

        // Optimized rendering with line-based processing (similar to QT's approach)
        // Pre-calculate source row pointers and scaling factors for better performance
        int* src_x_map = new int[scaled_width];
        for (int x = 0; x < scaled_width; x++) {
            src_x_map[x] = (x * frame.width) / scaled_width;
        }
        
        for (int y = 0; y < scaled_height; y++) {
            int dst_y = y + offset_y;
            if (dst_y < 0 || dst_y >= buffer_height) continue;
            
            // Calculate source row
            int src_y = (y * frame.height) / scaled_height;
            if (src_y >= frame.height) continue;
            
            // Get pointers to destination and source rows
            uint32_t* dst_row = pixels + dst_y * buffer_width + offset_x;
            const uint8_t* src_row = rgb_data + src_y * frame.width * 3;
            
            // Process entire row at once for better cache performance
            for (int x = 0; x < scaled_width; x++) {
                int src_x = src_x_map[x];
                if (src_x < frame.width) {
                    const uint8_t* src_pixel = src_row + src_x * 3;
                    uint8_t red = src_pixel[0];
                    uint8_t green = src_pixel[1];
                    uint8_t blue = src_pixel[2];
                    
                    // XRGB8888 format: 0xAARRGGBB
                    dst_row[x] = (0xFF << 24) | (red << 16) | (green << 8) | blue;
                }
            }
        }
        
        delete[] src_x_map;
    }

    void fillBufferWithPattern(void* buffer, int width, int height, uint8_t frame_counter) {
        if (!buffer || width <= 0 || height <= 0) {
            return;
        }

        uint32_t* pixels = static_cast<uint32_t*>(buffer);

        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                int idx = y * width + x;

                uint8_t red = static_cast<uint8_t>((x + frame_counter) % 256);
                uint8_t green = static_cast<uint8_t>((y + frame_counter) % 256);
                uint8_t blue = frame_counter;

                pixels[idx] = (0xFF << 24) | (red << 16) | (green << 8) | blue;
            }
        }
    }

    void fillBufferWithBlack(void* buffer, int width, int height) {
        if (!buffer || width <= 0 || height <= 0) {
            return;
        }

        uint32_t* pixels = static_cast<uint32_t*>(buffer);
        size_t pixel_count = width * height;

        // Fill with black (0xFF000000 = black with full alpha)
        for (size_t i = 0; i < pixel_count; i++) {
            pixels[i] = 0xFF000000;
        }
    }

} // namespace openterface