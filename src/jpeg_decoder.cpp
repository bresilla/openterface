#include "openterface/jpeg_decoder.hpp"
#include <iostream>
#include <cstring>
#include <setjmp.h>

extern "C" {
#include <jpeglib.h>
#include <jerror.h>
}

namespace openterface {

// Custom error handler for libjpeg  
struct JpegErrorMgr {
    struct jpeg_error_mgr pub;
    jmp_buf setjmp_buffer;
    char last_error[JMSG_LENGTH_MAX];
};

void jpegErrorHandler(j_common_ptr cinfo) {
    JpegErrorMgr* err = (JpegErrorMgr*)cinfo->err;
    
    // Create the error message
    (*(cinfo->err->format_message))(cinfo, err->last_error);
    
    // Jump back to error handling code
    longjmp(err->setjmp_buffer, 1);
}

JpegDecoder::JpegDecoder() = default;

JpegDecoder::~JpegDecoder() = default;

bool JpegDecoder::decode(const uint8_t* jpeg_data, size_t jpeg_size, DecodedFrame& output) {
    if (!jpeg_data || jpeg_size == 0) {
        last_error = "Invalid JPEG data";
        return false;
    }

    struct jpeg_decompress_struct cinfo;
    JpegErrorMgr jerr;

    // Set up error handling
    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = jpegErrorHandler;

    if (setjmp(jerr.setjmp_buffer)) {
        // Error occurred during JPEG processing
        last_error = std::string("JPEG decode error: ") + jerr.last_error;
        jpeg_destroy_decompress(&cinfo);
        return false;
    }

    // Initialize decompression
    jpeg_create_decompress(&cinfo);

    // Set up input source
    jpeg_mem_src(&cinfo, const_cast<unsigned char*>(jpeg_data), jpeg_size);

    // Read JPEG header
    int header_result = jpeg_read_header(&cinfo, TRUE);
    if (header_result != JPEG_HEADER_OK) {
        last_error = "Failed to read JPEG header";
        jpeg_destroy_decompress(&cinfo);
        return false;
    }

    // Set decompression parameters for RGB output with performance optimizations
    cinfo.out_color_space = JCS_RGB;
    cinfo.output_components = 3;
    
    // Performance optimizations (like ffplay)
    cinfo.do_fancy_upsampling = FALSE;  // Disable fancy upsampling for speed
    cinfo.do_block_smoothing = FALSE;   // Disable block smoothing for speed
    cinfo.dct_method = JDCT_IFAST;      // Use fastest DCT method

    // Start decompression
    if (!jpeg_start_decompress(&cinfo)) {
        last_error = "Failed to start JPEG decompression";
        jpeg_destroy_decompress(&cinfo);
        return false;
    }

    // Validate decoded dimensions
    if (cinfo.output_width <= 0 || cinfo.output_height <= 0) {
        last_error = "Invalid JPEG dimensions: " + std::to_string(cinfo.output_width) + "x" + std::to_string(cinfo.output_height);
        jpeg_destroy_decompress(&cinfo);
        return false;
    }
    
    // Reasonable size limits to prevent memory issues
    if (cinfo.output_width > 8192 || cinfo.output_height > 8192) {
        last_error = "JPEG dimensions too large: " + std::to_string(cinfo.output_width) + "x" + std::to_string(cinfo.output_height);
        jpeg_destroy_decompress(&cinfo);
        return false;
    }
    
    // Validate output components (should be 3 for RGB)
    if (cinfo.output_components != 3) {
        last_error = "Unexpected JPEG output components: " + std::to_string(cinfo.output_components) + " (expected 3 for RGB)";
        jpeg_destroy_decompress(&cinfo);
        return false;
    }

    // Allocate output buffer
    output.width = cinfo.output_width;
    output.height = cinfo.output_height;
    output.channels = cinfo.output_components;
    
    int row_stride = output.width * output.channels;
    
    // Validate calculated buffer size
    size_t buffer_size = static_cast<size_t>(output.height) * static_cast<size_t>(row_stride);
    if (buffer_size > 200 * 1024 * 1024) { // 200MB limit
        last_error = "JPEG buffer size too large: " + std::to_string(buffer_size) + " bytes";
        jpeg_destroy_decompress(&cinfo);
        return false;
    }
    
    output.rgb_data.resize(buffer_size);

    // Read scanlines one at a time (safer, still fast with other optimizations)
    JSAMPROW row_pointers[1];
    int current_row = 0;
    
    while (cinfo.output_scanline < cinfo.output_height) {
        row_pointers[0] = &output.rgb_data[current_row * row_stride];
        
        int rows_read = jpeg_read_scanlines(&cinfo, row_pointers, 1);
        if (rows_read != 1) {
            last_error = "Failed to read JPEG scanline";
            jpeg_destroy_decompress(&cinfo);
            return false;
        }
        
        current_row++;
    }

    // Finish decompression
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);

    return true;
}

std::string JpegDecoder::getLastError() const {
    return last_error;
}

} // namespace openterface