#pragma once

#include <wayland-client.h>
#include <functional>
#include <string>
#include <cstdint>

namespace openterface {

    // Forward declaration
    struct WaylandCallbackData;

    // Input helper functions
    int get_resize_edge(int x, int y, int width, int height, int border_size);
    uint32_t edge_to_xdg_edge(int edge);
    void set_cursor(void *data, struct wl_pointer *pointer, uint32_t serial);

    // Pointer callbacks
    void debug_pointer_enter(void *data, struct wl_pointer *pointer, uint32_t serial, 
                            struct wl_surface *surface, wl_fixed_t sx, wl_fixed_t sy);
    void debug_pointer_leave(void *data, struct wl_pointer *pointer, uint32_t serial,
                            struct wl_surface *surface);
    void debug_pointer_motion(void *data, struct wl_pointer *pointer, uint32_t time, 
                             wl_fixed_t sx, wl_fixed_t sy);
    void debug_pointer_button(void *data, struct wl_pointer *pointer, uint32_t serial, 
                             uint32_t time, uint32_t button, uint32_t state);
    void debug_pointer_axis(void *data, struct wl_pointer *pointer, uint32_t time, 
                           uint32_t axis, wl_fixed_t value);
    void debug_pointer_frame(void *data, struct wl_pointer *pointer);
    void debug_pointer_axis_source(void *data, struct wl_pointer *pointer, uint32_t axis_source);
    void debug_pointer_axis_stop(void *data, struct wl_pointer *pointer, uint32_t time, uint32_t axis);
    void debug_pointer_axis_discrete(void *data, struct wl_pointer *pointer, uint32_t axis, int32_t discrete);
    void debug_pointer_axis_value120(void *data, struct wl_pointer *pointer, uint32_t axis, int32_t value120);
    void debug_pointer_axis_relative_direction(void *data, struct wl_pointer *pointer, 
                                              uint32_t axis, uint32_t direction);
    extern const struct wl_pointer_listener debug_pointer_listener;

    // Keyboard callbacks
    void debug_keyboard_keymap(void *data, struct wl_keyboard *keyboard, uint32_t format, 
                              int32_t fd, uint32_t size);
    void debug_keyboard_enter(void *data, struct wl_keyboard *keyboard, uint32_t serial,
                             struct wl_surface *surface, struct wl_array *keys);
    void debug_keyboard_leave(void *data, struct wl_keyboard *keyboard, uint32_t serial,
                             struct wl_surface *surface);
    void debug_keyboard_key(void *data, struct wl_keyboard *keyboard, uint32_t serial, 
                           uint32_t time, uint32_t key, uint32_t state);
    void debug_keyboard_modifiers(void *data, struct wl_keyboard *keyboard, uint32_t serial,
                                 uint32_t mods_depressed, uint32_t mods_latched, 
                                 uint32_t mods_locked, uint32_t group);
    void debug_keyboard_repeat_info(void *data, struct wl_keyboard *keyboard, 
                                   int32_t rate, int32_t delay);
    extern const struct wl_keyboard_listener debug_keyboard_listener;

} // namespace openterface