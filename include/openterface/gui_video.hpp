#pragma once

#include <cstdint>
#include <vector>
#include <memory>
#include <mutex>

namespace openterface {

    // Forward declarations
    class JpegDecoder;
    struct FrameData;

    // Video frame processing functions
    struct VideoFrame {
        std::vector<uint8_t> data;
        int width = 0;
        int height = 0;
        bool is_rgb = false;
    };

    class VideoProcessor {
    public:
        VideoProcessor();
        ~VideoProcessor();

        // Process incoming frame data
        bool processFrame(const FrameData& frame, VideoFrame& output);
        
        // Get last error message
        const std::string& getLastError() const { return last_error; }

    private:
        std::unique_ptr<JpegDecoder> jpeg_decoder;
        std::string last_error;
    };

    // Buffer rendering functions
    void renderVideoToBuffer(void* buffer, int buffer_width, int buffer_height,
                            const VideoFrame& frame);
    
    void fillBufferWithPattern(void* buffer, int width, int height, uint8_t frame_counter);
    
    void fillBufferWithBlack(void* buffer, int width, int height);

} // namespace openterface