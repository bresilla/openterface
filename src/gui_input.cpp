#include "openterface/gui_input.hpp"
#include "openterface/gui_wayland.hpp"
#include "openterface/input.hpp"
#include "openterface/serial.hpp"
#include <unistd.h>
#include <cstring>
#include <string>

namespace openterface {

    // Helper function to determine resize edge
    int get_resize_edge(int x, int y, int width, int height, int border_size) {
        int edge = 0;

        if (x < border_size)
            edge |= 1; // left
        if (x > width - border_size)
            edge |= 2; // right
        if (y < border_size)
            edge |= 4; // top
        if (y > height - border_size)
            edge |= 8; // bottom

        return edge;
    }

    // Convert internal edge flags to XDG shell resize edges
    uint32_t edge_to_xdg_edge(int edge) {
        // XDG_TOPLEVEL_RESIZE_EDGE constants
        const uint32_t XDG_TOPLEVEL_RESIZE_EDGE_NONE = 0;
        const uint32_t XDG_TOPLEVEL_RESIZE_EDGE_TOP = 1;
        const uint32_t XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM = 2;
        const uint32_t XDG_TOPLEVEL_RESIZE_EDGE_LEFT = 4;
        const uint32_t XDG_TOPLEVEL_RESIZE_EDGE_TOP_LEFT = 5;
        const uint32_t XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_LEFT = 6;
        const uint32_t XDG_TOPLEVEL_RESIZE_EDGE_RIGHT = 8;
        const uint32_t XDG_TOPLEVEL_RESIZE_EDGE_TOP_RIGHT = 9;
        const uint32_t XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT = 10;

        switch (edge) {
        case 1:
            return XDG_TOPLEVEL_RESIZE_EDGE_LEFT; // left
        case 2:
            return XDG_TOPLEVEL_RESIZE_EDGE_RIGHT; // right
        case 4:
            return XDG_TOPLEVEL_RESIZE_EDGE_TOP; // top
        case 8:
            return XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM; // bottom
        case 5:
            return XDG_TOPLEVEL_RESIZE_EDGE_TOP_LEFT; // top-left
        case 6:
            return XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_LEFT; // bottom-left
        case 9:
            return XDG_TOPLEVEL_RESIZE_EDGE_TOP_RIGHT; // top-right
        case 10:
            return XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT; // bottom-right
        default:
            return XDG_TOPLEVEL_RESIZE_EDGE_NONE;
        }
    }

    // Function to set cursor
    void set_cursor(void *data, struct wl_pointer *pointer, uint32_t serial) {
        auto *callback_data = static_cast<WaylandCallbackData *>(data);

        if (!callback_data->default_cursor || !callback_data->cursor_surface) {
            if (callback_data->log_func) {
                callback_data->log_func("Warning: No cursor available to set");
            }
            return;
        }

        struct wl_cursor_image *image = callback_data->default_cursor->images[0];
        if (!image) {
            if (callback_data->log_func) {
                callback_data->log_func("Warning: No cursor image available");
            }
            return;
        }

        // Attach cursor image to cursor surface
        wl_surface_attach(callback_data->cursor_surface, wl_cursor_image_get_buffer(image), 0, 0);
        wl_surface_damage(callback_data->cursor_surface, 0, 0, image->width, image->height);
        wl_surface_commit(callback_data->cursor_surface);

        // Set the cursor
        wl_pointer_set_cursor(pointer, serial, callback_data->cursor_surface, image->hotspot_x, image->hotspot_y);

        if (callback_data->log_func) {
            callback_data->log_func("Cursor set successfully");
        }
    }

    // Pointer callbacks for debugging
    void debug_pointer_enter(void *data, struct wl_pointer *pointer, uint32_t serial, struct wl_surface *surface,
                                    wl_fixed_t sx, wl_fixed_t sy) {
        auto *callback_data = static_cast<WaylandCallbackData *>(data);
        callback_data->mouse_over = true;

        // Store initial mouse position
        callback_data->last_mouse_x = wl_fixed_to_int(sx);
        callback_data->last_mouse_y = wl_fixed_to_int(sy);

        if (callback_data->log_func) {
            callback_data->log_func("🖱️  Mouse ENTERED window");
            if (callback_data->debug_mode) {
                callback_data->log_func("[DEBUG] Mouse enter - input capture will activate when window has focus");
            }
        }

        // Set cursor to make it visible
        set_cursor(data, pointer, serial);
    }

    void debug_pointer_leave(void *data, struct wl_pointer *pointer, uint32_t serial,
                                    struct wl_surface *surface) {
        auto *callback_data = static_cast<WaylandCallbackData *>(data);
        callback_data->mouse_over = false;
        
        // CRITICAL: Explicitly stop mouse tracking when leaving window
        if (callback_data->input_ptr && *callback_data->input_ptr) {
            (*callback_data->input_ptr)->stopMouseTracking();
        }
        
        if (callback_data->log_func) {
            callback_data->log_func("🖱️  Mouse LEFT window - STOPPING all mouse tracking");
            if (callback_data->debug_mode) {
                callback_data->log_func("[DEBUG] Mouse leave - input capture FORCE STOPPED");
            }
        }
    }

    void debug_pointer_motion(void *data, struct wl_pointer *pointer, uint32_t time, wl_fixed_t sx,
                                     wl_fixed_t sy) {
        auto *callback_data = static_cast<WaylandCallbackData *>(data);
        
        // No logging in motion events - can cause ping-pong delays
        
        // Only track when BOTH mouse is over window AND window has focus
        if (!callback_data->mouse_over || !callback_data->input_active)
            return;

        int x = wl_fixed_to_int(sx);
        int y = wl_fixed_to_int(sy);

        // Store position for later processing
        callback_data->last_mouse_x = x;
        callback_data->last_mouse_y = y;
        
        // That's it! NO serial operations, NO logging, NO complex logic
        // The input processing thread will poll these values

        // Check if we're currently resizing
        if (callback_data->is_resizing) {
            // Handle ongoing resize - this would be where you'd send resize requests
            // to the window manager, but for now we'll just log it
            if (callback_data->log_func) {
                std::string msg = "Resizing window at: (" + std::to_string(x) + ", " + std::to_string(y) + ")";
                callback_data->log_func(msg);
            }
            return;
        }

        // Check if mouse is near window edges for resize cursor
        if (callback_data->current_width && callback_data->current_height) {
            int edge = get_resize_edge(x, y, *callback_data->current_width, *callback_data->current_height,
                                       WaylandCallbackData::RESIZE_BORDER);

            if (edge != callback_data->resize_edge) {
                callback_data->resize_edge = edge;

                // Log when entering/leaving resize areas
                if (callback_data->log_func) {
                    if (edge != 0) {
                        std::string msg =
                            "Mouse near window edge - resize available (edge=" + std::to_string(edge) + ")";
                        callback_data->log_func(msg);
                    } else {
                        callback_data->log_func("Mouse in window center - normal cursor");
                    }
                }

                // Here you would set different cursor shapes based on edge:
                // edge == 1|2: horizontal resize cursor
                // edge == 4|8: vertical resize cursor
                // edge == 5|6|9|10: diagonal resize cursors
            }
        }

        // Debug logging for mouse motion
        if (callback_data->debug_mode && callback_data->log_func) {
            // No debug logging in motion events to prevent ping-pong delays
        }
    }

    void debug_pointer_button(void *data, struct wl_pointer *pointer, uint32_t serial, uint32_t time,
                                     uint32_t button, uint32_t state) {
        auto *callback_data = static_cast<WaylandCallbackData *>(data);
        
        // Debug: Always log button events
        if (callback_data->log_func) {
            callback_data->log_func("[DEBUG] Button event received, debug_mode=" + 
                                  std::string(callback_data->debug_mode ? "true" : "false") +
                                  ", mouse_over=" + std::string(callback_data->mouse_over ? "true" : "false"));
        }
        
        // Only track when BOTH mouse is over window AND window has focus
        if (!callback_data->mouse_over || !callback_data->input_active)
            return;

        const char *action = (state == WL_POINTER_BUTTON_STATE_PRESSED) ? "PRESSED" : "RELEASED";
        const char *btn_name = "";
        switch (button) {
        case 0x110:
            btn_name = "LEFT";
            break;
        case 0x111:
            btn_name = "RIGHT";
            break;
        case 0x112:
            btn_name = "MIDDLE";
            break;
        default:
            btn_name = "UNKNOWN";
            break;
        }

        // Handle resize operations with left mouse button
        if (button == 0x110) { // Left mouse button
            if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
                // Check if we're clicking on a resize edge
                if (callback_data->resize_edge != 0 && callback_data->xdg_toplevel && callback_data->seat) {
                    callback_data->is_resizing = true;
                    callback_data->resize_serial = serial; // Store the serial for this resize operation

                    uint32_t xdg_edge = edge_to_xdg_edge(callback_data->resize_edge);

                    if (callback_data->log_func) {
                        callback_data->log_func(
                            "Starting window resize operation (edge=" + std::to_string(callback_data->resize_edge) +
                            ", xdg_edge=" + std::to_string(xdg_edge) + ")");
                    }

                    // Actually start the resize operation
                    xdg_toplevel_resize(callback_data->xdg_toplevel, callback_data->seat, serial, xdg_edge);
                }
            } else if (state == WL_POINTER_BUTTON_STATE_RELEASED) {
                // Stop resizing
                if (callback_data->is_resizing) {
                    callback_data->is_resizing = false;
                    if (callback_data->log_func) {
                        callback_data->log_func("Finished window resize operation");
                    }
                }
            }
        }

        // Forward mouse button events to Input/Serial modules if available
        // Only forward if not currently resizing the window
        if (!callback_data->is_resizing && callback_data->input_ptr && callback_data->serial_ptr && 
            *callback_data->input_ptr && *callback_data->serial_ptr) {
            
            auto input = *callback_data->input_ptr;
            auto serial = *callback_data->serial_ptr;
            
            // Check if input forwarding is enabled and serial is connected
            if (input->isForwardingEnabled() && serial->isConnected()) {
                bool pressed = (state == WL_POINTER_BUTTON_STATE_PRESSED);
                
                // Convert Wayland button codes to standard button numbers
                int button_num = 0;
                switch (button) {
                case 0x110: button_num = 1; break; // Left button
                case 0x111: button_num = 2; break; // Right button  
                case 0x112: button_num = 3; break; // Middle button
                default: button_num = 0; break;    // Unknown button
                }
                
                if (button_num > 0) {
                    // Forward to serial
                    bool success = serial->sendMouseButton(button_num, pressed, 
                                                         callback_data->last_mouse_x, 
                                                         callback_data->last_mouse_y, true);
                    
                    if (callback_data->log_func) {
                        std::string msg = "[INPUT] Mouse button " + std::to_string(button_num) + 
                                        (pressed ? " pressed" : " released") + " forwarded";
                        if (!success) {
                            msg += " [FAILED]";
                        }
                        callback_data->log_func(msg);
                    }
                }
            }
        }

        // Debug logging for mouse buttons
        if (callback_data->debug_mode && callback_data->log_func) {
            std::string msg = "[DEBUG] Mouse " + std::string(btn_name) + " button " + action;
            if (callback_data->resize_edge != 0) {
                msg += " (at resize edge " + std::to_string(callback_data->resize_edge) + ")";
            }
            callback_data->log_func(msg);
        }
    }

    void debug_pointer_axis(void *data, struct wl_pointer *pointer, uint32_t time, uint32_t axis,
                                   wl_fixed_t value) {
        auto *callback_data = static_cast<WaylandCallbackData *>(data);
        
        // Only track when BOTH mouse is over window AND window has focus
        if (!callback_data->mouse_over || !callback_data->input_active)
            return;
            
        const char *axis_name = (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) ? "VERTICAL" : "HORIZONTAL";
        double scroll_value = wl_fixed_to_double(value);
        
        // Forward mouse scroll to Input/Serial modules if available
        if (callback_data->input_ptr && callback_data->serial_ptr && 
            *callback_data->input_ptr && *callback_data->serial_ptr) {
            
            auto input = *callback_data->input_ptr;
            auto serial = *callback_data->serial_ptr;
            
            // Check if input forwarding is enabled and serial is connected
            if (input->isForwardingEnabled() && serial->isConnected()) {
                // Only handle vertical scrolling for now
                if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) {
                    // Convert scroll value to discrete scroll steps
                    int32_t scroll_steps = 0;
                    if (scroll_value > 0) {
                        scroll_steps = 1;  // Scroll down
                    } else if (scroll_value < 0) {
                        scroll_steps = -1; // Scroll up
                    }
                    
                    if (scroll_steps != 0) {
                        // Use the Input module's scroll injection method
                        bool success = input->injectMouseScroll(0, scroll_steps);
                        
                        if (callback_data->log_func) {
                            std::string msg = "[INPUT] Mouse scroll " + std::string(axis_name) + 
                                            " (" + std::to_string(scroll_steps) + ") forwarded";
                            if (!success) {
                                msg += " [FAILED]";
                            }
                            callback_data->log_func(msg);
                        }
                    }
                }
            }
        }
        
        // Debug logging
        if (callback_data->log_func) {
            std::string msg = "Mouse scroll " + std::string(axis_name) + ": " + std::to_string(scroll_value);
            callback_data->log_func(msg);
        }
    }

    void debug_pointer_frame(void *data, struct wl_pointer *pointer) {
        // Frame event - end of pointer event group
    }

    void debug_pointer_axis_source(void *data, struct wl_pointer *pointer, uint32_t axis_source) {
        // Axis source (wheel, finger, etc.)
    }

    void debug_pointer_axis_stop(void *data, struct wl_pointer *pointer, uint32_t time, uint32_t axis) {
        // Axis scrolling stopped
    }

    void debug_pointer_axis_discrete(void *data, struct wl_pointer *pointer, uint32_t axis, int32_t discrete) {
        // Discrete axis events (scroll wheel clicks)
    }

    void debug_pointer_axis_value120(void *data, struct wl_pointer *pointer, uint32_t axis, int32_t value120) {
        // High-resolution scroll wheel events
    }

    void debug_pointer_axis_relative_direction(void *data, struct wl_pointer *pointer, uint32_t axis,
                                                      uint32_t direction) {
        // Relative direction for axis events
    }

    const struct wl_pointer_listener debug_pointer_listener = {
        debug_pointer_enter,
        debug_pointer_leave,
        debug_pointer_motion,
        debug_pointer_button,
        debug_pointer_axis,
        debug_pointer_frame,
        debug_pointer_axis_source,
        debug_pointer_axis_stop,
        debug_pointer_axis_discrete,
        debug_pointer_axis_value120,
        debug_pointer_axis_relative_direction,
    };

    // Keyboard callbacks for debugging
    void debug_keyboard_keymap(void *data, struct wl_keyboard *keyboard, uint32_t format, int32_t fd,
                                      uint32_t size) {
        close(fd); // Just close the keymap fd for now
    }

    void debug_keyboard_enter(void *data, struct wl_keyboard *keyboard, uint32_t serial,
                                     struct wl_surface *surface, struct wl_array *keys) {
        auto *callback_data = static_cast<WaylandCallbackData *>(data);
        callback_data->input_active = true;
        if (callback_data->log_func) {
            callback_data->log_func("⌨️  Window FOCUS gained - input capture ACTIVE");
        }
    }

    void debug_keyboard_leave(void *data, struct wl_keyboard *keyboard, uint32_t serial,
                                     struct wl_surface *surface) {
        auto *callback_data = static_cast<WaylandCallbackData *>(data);
        callback_data->input_active = false;
        
        // CRITICAL: Immediately stop all input tracking when losing focus
        if (callback_data->input_ptr && *callback_data->input_ptr) {
            (*callback_data->input_ptr)->stopMouseTracking();
        }
        
        if (callback_data->log_func) {
            callback_data->log_func("⌨️  Window FOCUS lost - ALL INPUT TRACKING STOPPED");
        }
    }

    void debug_keyboard_key(void *data, struct wl_keyboard *keyboard, uint32_t serial, uint32_t time,
                                   uint32_t key, uint32_t state) {
        auto *callback_data = static_cast<WaylandCallbackData *>(data);
        
        // Debug: Always log keyboard events
        if (callback_data->log_func) {
            callback_data->log_func("[DEBUG] Keyboard event received! debug_mode=" + 
                                  std::string(callback_data->debug_mode ? "true" : "false") +
                                  ", input_active=" + std::string(callback_data->input_active ? "true" : "false"));
            
            const char *action = (state == WL_KEYBOARD_KEY_STATE_PRESSED) ? "PRESSED" : "RELEASED";
            std::string msg = "[DEBUG] Key " + std::to_string(key) + " " + action;
            if (!callback_data->input_active) {
                msg += " [WARNING: no keyboard focus]";
            }
            callback_data->log_func(msg);
        }
        
        // Forward keyboard events to Input/Serial modules if available
        if (callback_data->input_active && callback_data->input_ptr && callback_data->serial_ptr && 
            *callback_data->input_ptr && *callback_data->serial_ptr) {
            
            auto input = *callback_data->input_ptr;
            auto serial = *callback_data->serial_ptr;
            
            // Check if input forwarding is enabled and serial is connected
            if (input->isForwardingEnabled() && serial->isConnected()) {
                // Linux keycodes need +8 to convert to USB HID codes (roughly)
                // This is a simplified mapping - a full implementation would use a proper keymap
                int hid_keycode = key + 8;
                
                // Convert Wayland modifiers to CH9329 format
                int modifiers = 0;
                if (callback_data->current_modifiers & 1) modifiers |= 0x02;  // Shift
                if (callback_data->current_modifiers & 4) modifiers |= 0x01;  // Ctrl
                if (callback_data->current_modifiers & 8) modifiers |= 0x04;  // Alt
                if (callback_data->current_modifiers & 64) modifiers |= 0x08; // Meta/Super
                
                if (state == WL_KEYBOARD_KEY_STATE_PRESSED) {
                    bool success = serial->sendKeyPress(hid_keycode, modifiers);
                    if (callback_data->log_func) {
                        std::string msg = "[INPUT] Key press forwarded: " + std::to_string(hid_keycode);
                        if (!success) {
                            msg += " [FAILED]";
                        }
                        callback_data->log_func(msg);
                    }
                } else {
                    bool success = serial->sendKeyRelease(hid_keycode, modifiers);
                    if (callback_data->log_func) {
                        std::string msg = "[INPUT] Key release forwarded: " + std::to_string(hid_keycode);
                        if (!success) {
                            msg += " [FAILED]";
                        }
                        callback_data->log_func(msg);
                    }
                }
            }
        }
    }

    void debug_keyboard_modifiers(void *data, struct wl_keyboard *keyboard, uint32_t serial,
                                         uint32_t mods_depressed, uint32_t mods_latched, uint32_t mods_locked,
                                         uint32_t group) {
        auto *callback_data = static_cast<WaylandCallbackData *>(data);
        
        // Store current modifiers for use in key events
        callback_data->current_modifiers = mods_depressed;
        
        if (callback_data->input_active && callback_data->log_func && (mods_depressed || mods_latched || mods_locked)) {
            std::string msg = "Modifiers: Ctrl=" + std::to_string((mods_depressed & 4) ? 1 : 0) +
                              " Shift=" + std::to_string((mods_depressed & 1) ? 1 : 0) +
                              " Alt=" + std::to_string((mods_depressed & 8) ? 1 : 0);
            callback_data->log_func(msg);
        }
    }

    void debug_keyboard_repeat_info(void *data, struct wl_keyboard *keyboard, int32_t rate, int32_t delay) {
        // Keyboard repeat info
    }

    const struct wl_keyboard_listener debug_keyboard_listener = {
        debug_keyboard_keymap, debug_keyboard_enter,     debug_keyboard_leave,
        debug_keyboard_key,    debug_keyboard_modifiers, debug_keyboard_repeat_info,
    };

} // namespace openterface