/* Minimal vendored libretro API header subset for Altirra's core adapter.
 *
 * Source of truth: https://github.com/libretro/libretro-common/blob/master/include/libretro.h
 * This file intentionally contains the ABI types and constants used by this
 * adapter; expand from upstream as additional interfaces are implemented.
 *
 * The canonical libretro.h is MIT-licensed by the libretro project. This
 * subset contains ABI declarations/constants only; keep this comment and record
 * the exact upstream commit SHA in README.md when refreshing it.
 */
#ifndef ALTIRRA_LIBRETRO_H
#define ALTIRRA_LIBRETRO_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <limits.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
#define RETRO_CALLCONV __cdecl
#define RETRO_API __declspec(dllexport)
#elif defined(__GNUC__) && __GNUC__ >= 4
#define RETRO_CALLCONV
#define RETRO_API __attribute__((visibility("default")))
#else
#define RETRO_CALLCONV
#define RETRO_API
#endif

#define RETRO_API_VERSION 1

#define RETRO_ENVIRONMENT_EXPERIMENTAL 0x10000

#define RETRO_DEVICE_TYPE_SHIFT 8
#define RETRO_DEVICE_MASK ((1 << RETRO_DEVICE_TYPE_SHIFT) - 1)
#define RETRO_DEVICE_SUBCLASS(base, id) (((id + 1) << RETRO_DEVICE_TYPE_SHIFT) | base)
#define RETRO_DEVICE_NONE 0
#define RETRO_DEVICE_JOYPAD 1
#define RETRO_DEVICE_MOUSE 2
#define RETRO_DEVICE_KEYBOARD 3
#define RETRO_DEVICE_LIGHTGUN 4
#define RETRO_DEVICE_ANALOG 5
#define RETRO_DEVICE_POINTER 6

#define RETRO_DEVICE_ID_JOYPAD_B 0
#define RETRO_DEVICE_ID_JOYPAD_Y 1
#define RETRO_DEVICE_ID_JOYPAD_SELECT 2
#define RETRO_DEVICE_ID_JOYPAD_START 3
#define RETRO_DEVICE_ID_JOYPAD_UP 4
#define RETRO_DEVICE_ID_JOYPAD_DOWN 5
#define RETRO_DEVICE_ID_JOYPAD_LEFT 6
#define RETRO_DEVICE_ID_JOYPAD_RIGHT 7
#define RETRO_DEVICE_ID_JOYPAD_A 8
#define RETRO_DEVICE_ID_JOYPAD_X 9
#define RETRO_DEVICE_ID_JOYPAD_L 10
#define RETRO_DEVICE_ID_JOYPAD_R 11
#define RETRO_DEVICE_ID_JOYPAD_L2 12
#define RETRO_DEVICE_ID_JOYPAD_R2 13
#define RETRO_DEVICE_ID_JOYPAD_L3 14
#define RETRO_DEVICE_ID_JOYPAD_R3 15
#define RETRO_DEVICE_ID_JOYPAD_MASK 256

#define RETRO_DEVICE_INDEX_ANALOG_LEFT 0
#define RETRO_DEVICE_INDEX_ANALOG_RIGHT 1
#define RETRO_DEVICE_INDEX_ANALOG_BUTTON 2
#define RETRO_DEVICE_ID_ANALOG_X 0
#define RETRO_DEVICE_ID_ANALOG_Y 1

#define RETRO_DEVICE_ID_MOUSE_X 0
#define RETRO_DEVICE_ID_MOUSE_Y 1
#define RETRO_DEVICE_ID_MOUSE_LEFT 2
#define RETRO_DEVICE_ID_MOUSE_RIGHT 3
#define RETRO_DEVICE_ID_MOUSE_WHEELUP 4
#define RETRO_DEVICE_ID_MOUSE_WHEELDOWN 5
#define RETRO_DEVICE_ID_MOUSE_MIDDLE 6
#define RETRO_DEVICE_ID_MOUSE_HORIZ_WHEELUP 7
#define RETRO_DEVICE_ID_MOUSE_HORIZ_WHEELDOWN 8
#define RETRO_DEVICE_ID_MOUSE_BUTTON_4 9
#define RETRO_DEVICE_ID_MOUSE_BUTTON_5 10

#define RETRO_DEVICE_ID_LIGHTGUN_SCREEN_X 13
#define RETRO_DEVICE_ID_LIGHTGUN_SCREEN_Y 14
#define RETRO_DEVICE_ID_LIGHTGUN_IS_OFFSCREEN 15
#define RETRO_DEVICE_ID_LIGHTGUN_TRIGGER 2
#define RETRO_DEVICE_ID_LIGHTGUN_RELOAD 16
#define RETRO_DEVICE_ID_LIGHTGUN_AUX_A 3
#define RETRO_DEVICE_ID_LIGHTGUN_AUX_B 4
#define RETRO_DEVICE_ID_LIGHTGUN_START 6
#define RETRO_DEVICE_ID_LIGHTGUN_SELECT 7
#define RETRO_DEVICE_ID_LIGHTGUN_AUX_C 8
#define RETRO_DEVICE_ID_LIGHTGUN_DPAD_UP 9
#define RETRO_DEVICE_ID_LIGHTGUN_DPAD_DOWN 10
#define RETRO_DEVICE_ID_LIGHTGUN_DPAD_LEFT 11
#define RETRO_DEVICE_ID_LIGHTGUN_DPAD_RIGHT 12
#define RETRO_DEVICE_ID_LIGHTGUN_X 0
#define RETRO_DEVICE_ID_LIGHTGUN_Y 1
#define RETRO_DEVICE_ID_LIGHTGUN_CURSOR 3
#define RETRO_DEVICE_ID_LIGHTGUN_TURBO 4
#define RETRO_DEVICE_ID_LIGHTGUN_PAUSE 5

#define RETRO_DEVICE_ID_POINTER_X 0
#define RETRO_DEVICE_ID_POINTER_Y 1
#define RETRO_DEVICE_ID_POINTER_PRESSED 2
#define RETRO_DEVICE_ID_POINTER_COUNT 3
#define RETRO_DEVICE_ID_POINTER_IS_OFFSCREEN 15

#define RETRO_MEMORY_SAVE_RAM 0
#define RETRO_MEMORY_RTC 1
#define RETRO_MEMORY_SYSTEM_RAM 2
#define RETRO_MEMORY_VIDEO_RAM 3

#define RETRO_REGION_NTSC 0
#define RETRO_REGION_PAL 1

enum retro_key {
   RETROK_UNKNOWN = 0,
   RETROK_BACKSPACE = 8,
   RETROK_TAB = 9,
   RETROK_RETURN = 13,
   RETROK_ESCAPE = 27,
   RETROK_SPACE = 32,
   RETROK_0 = 48,
   RETROK_1 = 49,
   RETROK_2 = 50,
   RETROK_3 = 51,
   RETROK_4 = 52,
   RETROK_5 = 53,
   RETROK_6 = 54,
   RETROK_7 = 55,
   RETROK_8 = 56,
   RETROK_9 = 57,
   RETROK_SEMICOLON = 59,
   RETROK_EQUALS = 61,
   RETROK_COMMA = 44,
   RETROK_MINUS = 45,
   RETROK_PERIOD = 46,
   RETROK_SLASH = 47,
   RETROK_LEFTBRACKET = 91,
   RETROK_BACKSLASH = 92,
   RETROK_RIGHTBRACKET = 93,
   RETROK_BACKQUOTE = 96,
   RETROK_a = 97,
   RETROK_b = 98,
   RETROK_c = 99,
   RETROK_d = 100,
   RETROK_e = 101,
   RETROK_f = 102,
   RETROK_g = 103,
   RETROK_h = 104,
   RETROK_i = 105,
   RETROK_j = 106,
   RETROK_k = 107,
   RETROK_l = 108,
   RETROK_m = 109,
   RETROK_n = 110,
   RETROK_o = 111,
   RETROK_p = 112,
   RETROK_q = 113,
   RETROK_r = 114,
   RETROK_s = 115,
   RETROK_t = 116,
   RETROK_u = 117,
   RETROK_v = 118,
   RETROK_w = 119,
   RETROK_x = 120,
   RETROK_y = 121,
   RETROK_z = 122,
   RETROK_DELETE = 127,
   RETROK_KP0 = 256,
   RETROK_KP1 = 257,
   RETROK_KP2 = 258,
   RETROK_KP3 = 259,
   RETROK_KP4 = 260,
   RETROK_KP5 = 261,
   RETROK_KP6 = 262,
   RETROK_KP7 = 263,
   RETROK_KP8 = 264,
   RETROK_KP9 = 265,
   RETROK_KP_PERIOD = 266,
   RETROK_KP_DIVIDE = 267,
   RETROK_KP_MULTIPLY = 268,
   RETROK_KP_MINUS = 269,
   RETROK_KP_PLUS = 270,
   RETROK_KP_ENTER = 271,
   RETROK_UP = 273,
   RETROK_DOWN = 274,
   RETROK_RIGHT = 275,
   RETROK_LEFT = 276,
   RETROK_INSERT = 277,
   RETROK_HOME = 278,
   RETROK_END = 279,
   RETROK_PAGEUP = 280,
   RETROK_PAGEDOWN = 281,
   RETROK_F1 = 282,
   RETROK_F2 = 283,
   RETROK_F3 = 284,
   RETROK_F4 = 285,
   RETROK_F5 = 286,
   RETROK_F6 = 287,
   RETROK_F7 = 288,
   RETROK_F8 = 289,
   RETROK_F9 = 290,
   RETROK_F10 = 291,
   RETROK_F11 = 292,
   RETROK_F12 = 293,
   RETROK_RSHIFT = 303,
   RETROK_LSHIFT = 304,
   RETROK_RCTRL = 305,
   RETROK_LCTRL = 306,
   RETROK_BREAK = 318
};

#define RETRO_ENVIRONMENT_SET_ROTATION 1
#define RETRO_ENVIRONMENT_GET_OVERSCAN 2
#define RETRO_ENVIRONMENT_GET_CAN_DUPE 3
#define RETRO_ENVIRONMENT_SET_MESSAGE 6
#define RETRO_ENVIRONMENT_SHUTDOWN 7
#define RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL 8
#define RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY 9
#define RETRO_ENVIRONMENT_SET_PIXEL_FORMAT 10
#define RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS 11
#define RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK 12
#define RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE 13
#define RETRO_ENVIRONMENT_SET_HW_RENDER 14
#define RETRO_ENVIRONMENT_GET_VARIABLE 15
#define RETRO_ENVIRONMENT_SET_VARIABLES 16
#define RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE 17
#define RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME 18
#define RETRO_ENVIRONMENT_GET_LIBRETRO_PATH 19
#define RETRO_ENVIRONMENT_SET_FRAME_TIME_CALLBACK 21
#define RETRO_ENVIRONMENT_SET_AUDIO_CALLBACK 22
#define RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE 23
#define RETRO_ENVIRONMENT_GET_INPUT_DEVICE_CAPABILITIES 24
#define RETRO_ENVIRONMENT_GET_SENSOR_INTERFACE 25
#define RETRO_ENVIRONMENT_GET_CAMERA_INTERFACE 26
#define RETRO_ENVIRONMENT_GET_LOG_INTERFACE 27
#define RETRO_ENVIRONMENT_GET_PERF_INTERFACE 28
#define RETRO_ENVIRONMENT_GET_LOCATION_INTERFACE 29
#define RETRO_ENVIRONMENT_GET_CORE_ASSETS_DIRECTORY 30
#define RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY 31
#define RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO 32
#define RETRO_ENVIRONMENT_SET_PROC_ADDRESS_CALLBACK 33
#define RETRO_ENVIRONMENT_SET_SUBSYSTEM_INFO 34
#define RETRO_ENVIRONMENT_SET_CONTROLLER_INFO 35
#define RETRO_ENVIRONMENT_SET_MEMORY_MAPS 36
#define RETRO_ENVIRONMENT_SET_GEOMETRY 37
#define RETRO_ENVIRONMENT_GET_USERNAME 38
#define RETRO_ENVIRONMENT_GET_LANGUAGE 39
#define RETRO_ENVIRONMENT_GET_CURRENT_SOFTWARE_FRAMEBUFFER 40
#define RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE 41
#define RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS 42
#define RETRO_ENVIRONMENT_SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE 43
#define RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS 44
#define RETRO_ENVIRONMENT_SET_HW_SHARED_CONTEXT 44
#define RETRO_ENVIRONMENT_GET_VFS_INTERFACE 45
#define RETRO_ENVIRONMENT_GET_LED_INTERFACE 46
#define RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE 47
#define RETRO_ENVIRONMENT_GET_MIDI_INTERFACE 48
#define RETRO_ENVIRONMENT_GET_FASTFORWARDING 49
#define RETRO_ENVIRONMENT_GET_TARGET_REFRESH_RATE 50
#define RETRO_ENVIRONMENT_GET_INPUT_BITMASKS (51 | RETRO_ENVIRONMENT_EXPERIMENTAL)
#define RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION 52
#define RETRO_ENVIRONMENT_SET_CORE_OPTIONS 53
#define RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL 54
#define RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY 55
#define RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER 56
#define RETRO_ENVIRONMENT_GET_DISK_CONTROL_INTERFACE_VERSION 57
#define RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE 58
#define RETRO_ENVIRONMENT_GET_MESSAGE_INTERFACE_VERSION 59
#define RETRO_ENVIRONMENT_SET_MESSAGE_EXT 60
#define RETRO_ENVIRONMENT_GET_INPUT_MAX_USERS 61
#define RETRO_ENVIRONMENT_SET_AUDIO_BUFFER_STATUS_CALLBACK 62
#define RETRO_ENVIRONMENT_SET_MINIMUM_AUDIO_LATENCY 63
#define RETRO_ENVIRONMENT_SET_FASTFORWARDING_OVERRIDE 64
#define RETRO_ENVIRONMENT_SET_CONTENT_INFO_OVERRIDE 65
#define RETRO_ENVIRONMENT_GET_GAME_INFO_EXT 66
#define RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2 67
#define RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL 68

enum retro_log_level {
   RETRO_LOG_DEBUG = 0,
   RETRO_LOG_INFO,
   RETRO_LOG_WARN,
   RETRO_LOG_ERROR,

   RETRO_LOG_DUMMY = INT_MAX
};

typedef void (RETRO_CALLCONV *retro_log_printf_t)(
      enum retro_log_level level, const char *fmt, ...);

struct retro_log_callback {
   retro_log_printf_t log;
};

typedef void (RETRO_CALLCONV *retro_set_led_state_t)(int led, int state);

struct retro_led_interface {
   retro_set_led_state_t set_led_state;
};

enum retro_pixel_format {
   RETRO_PIXEL_FORMAT_0RGB1555 = 0,
   RETRO_PIXEL_FORMAT_XRGB8888 = 1,
   RETRO_PIXEL_FORMAT_RGB565 = 2,
};

struct retro_game_info {
   const char *path;
   const void *data;
   size_t size;
   const char *meta;
};

struct retro_subsystem_memory_info {
   const char *extension;
   unsigned type;
};

struct retro_subsystem_rom_info {
   const char *desc;
   const char *valid_extensions;
   bool need_fullpath;
   bool block_extract;
   bool required;
   const struct retro_subsystem_memory_info *memory;
   unsigned num_memory;
};

struct retro_subsystem_info {
   const char *desc;
   const char *ident;
   const struct retro_subsystem_rom_info *roms;
   unsigned num_roms;
   unsigned id;
};

struct retro_system_info {
   const char *library_name;
   const char *library_version;
   const char *valid_extensions;
   bool need_fullpath;
   bool block_extract;
};

struct retro_game_geometry {
   unsigned base_width;
   unsigned base_height;
   unsigned max_width;
   unsigned max_height;
   float aspect_ratio;
};

struct retro_system_timing {
   double fps;
   double sample_rate;
};

struct retro_system_av_info {
   struct retro_game_geometry geometry;
   struct retro_system_timing timing;
};

#define RETRO_MEMDESC_CONST     (1 << 0)
#define RETRO_MEMDESC_BIGENDIAN (1 << 1)
#define RETRO_MEMDESC_ALIGN_2   (1 << 16)
#define RETRO_MEMDESC_ALIGN_4   (2 << 16)
#define RETRO_MEMDESC_ALIGN_8   (3 << 16)
#define RETRO_MEMDESC_MINSIZE_2 (1 << 24)
#define RETRO_MEMDESC_MINSIZE_4 (2 << 24)
#define RETRO_MEMDESC_MINSIZE_8 (3 << 24)

struct retro_memory_descriptor {
   uint64_t flags;
   void *ptr;
   size_t offset;
   size_t start;
   size_t select;
   size_t disconnect;
   size_t len;
   const char *addrspace;
};

struct retro_memory_map {
   const struct retro_memory_descriptor *descriptors;
   unsigned num_descriptors;
};

struct retro_variable {
   const char *key;
   const char *value;
};

struct retro_disk_control_ext_callback {
   bool (*set_eject_state)(bool ejected);
   bool (*get_eject_state)(void);
   unsigned (*get_image_index)(void);
   bool (*set_image_index)(unsigned index);
   unsigned (*get_num_images)(void);
   bool (*replace_image_index)(unsigned index, const struct retro_game_info *info);
   bool (*add_image_index)(void);
   bool (*set_initial_image)(unsigned index, const char *path);
   bool (*get_image_path)(unsigned index, char *path, size_t len);
   bool (*get_image_label)(unsigned index, char *label, size_t len);
};

struct retro_keyboard_callback {
   void (*callback)(bool down, unsigned keycode, uint32_t character,
      uint16_t key_modifiers);
};

typedef int64_t retro_time_t;
typedef int64_t retro_usec_t;

typedef void (RETRO_CALLCONV *retro_frame_time_callback_t)(
      retro_usec_t usec);

struct retro_frame_time_callback {
   retro_frame_time_callback_t callback;
   retro_usec_t reference;
};

typedef void (RETRO_CALLCONV *retro_audio_buffer_status_callback_t)(
      bool active, unsigned occupancy, bool underrun_likely);

struct retro_audio_buffer_status_callback {
   retro_audio_buffer_status_callback_t callback;
};

struct retro_fastforwarding_override {
   float ratio;
   bool fastforward;
   bool notification;
   bool inhibit_toggle;
};

struct retro_core_option_value {
   const char *value;
   const char *label;
};

#define RETRO_NUM_CORE_OPTION_VALUES_MAX 128

struct retro_core_option_definition {
   const char *key;
   const char *desc;
   const char *info;
   struct retro_core_option_value values[RETRO_NUM_CORE_OPTION_VALUES_MAX];
   const char *default_value;
};

struct retro_core_option_v2_category {
   const char *key;
   const char *desc;
   const char *info;
};

struct retro_core_option_v2_definition {
   const char *key;
   const char *desc;
   const char *desc_categorized;
   const char *info;
   const char *info_categorized;
   const char *category_key;
   struct retro_core_option_value values[RETRO_NUM_CORE_OPTION_VALUES_MAX];
   const char *default_value;
};

struct retro_core_options_v2 {
   struct retro_core_option_v2_category *categories;
   struct retro_core_option_v2_definition *definitions;
};

struct retro_core_options_v2_intl {
   struct retro_core_options_v2 *us;
   struct retro_core_options_v2 *local;
};

struct retro_core_option_display {
   const char *key;
   bool visible;
};

struct retro_input_descriptor {
   unsigned port;
   unsigned device;
   unsigned index;
   unsigned id;
   const char *description;
};

struct retro_controller_description {
   const char *desc;
   unsigned id;
};

struct retro_controller_info {
   const struct retro_controller_description *types;
   unsigned num_types;
};

typedef bool (RETRO_CALLCONV *retro_environment_t)(unsigned cmd, void *data);
typedef void (RETRO_CALLCONV *retro_video_refresh_t)(const void *data, unsigned width, unsigned height, size_t pitch);
typedef void (RETRO_CALLCONV *retro_audio_sample_t)(int16_t left, int16_t right);
typedef size_t (RETRO_CALLCONV *retro_audio_sample_batch_t)(const int16_t *data, size_t frames);
typedef void (RETRO_CALLCONV *retro_input_poll_t)(void);
typedef int16_t (RETRO_CALLCONV *retro_input_state_t)(unsigned port, unsigned device, unsigned index, unsigned id);

#ifdef __cplusplus
}
#endif

#endif
