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

        // Calculate scaling to fit video into buffer while maintaining aspect ratio
        float scale_x = (float)buffer_width / (float)frame.width;
        float scale_y = (float)buffer_height / (float)frame.height;
        float scale = std::min(scale_x, scale_y);

        int scaled_width = (int)((float)frame.width * scale);
        int scaled_height = (int)((float)frame.height * scale);
        int offset_x = (buffer_width - scaled_width) / 2;
        int offset_y = (buffer_height - scaled_height) / 2;

        // Render actual decoded RGB video data with proper scaling
        for (int y = 0; y < scaled_height; y++) {
            for (int x = 0; x < scaled_width; x++) {
                int dst_x = x + offset_x;
                int dst_y = y + offset_y;

                // Bounds check for destination
                if (dst_x >= 0 && dst_x < buffer_width && dst_y >= 0 && dst_y < buffer_height) {
                    // Map scaled coordinates back to source frame
                    int src_x = (x * frame.width) / scaled_width;
                    int src_y = (y * frame.height) / scaled_height;

                    // Bounds check for source
                    if (src_x < frame.width && src_y < frame.height) {
                        int dst_idx = dst_y * buffer_width + dst_x;
                        int src_idx = (src_y * frame.width + src_x) * 3;

                        uint8_t red = rgb_data[src_idx];
                        uint8_t green = rgb_data[src_idx + 1];
                        uint8_t blue = rgb_data[src_idx + 2];

                        // XRGB8888 format: 0xAARRGGBB
                        pixels[dst_idx] = (0xFF << 24) | (red << 16) | (green << 8) | blue;
                    }
                }
            }
        }
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