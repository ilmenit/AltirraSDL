#include <dlfcn.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef bool (*retro_environment_t)(unsigned, void *);
typedef void (*retro_video_refresh_t)(const void *, unsigned, unsigned, size_t);
typedef void (*retro_audio_sample_t)(int16_t, int16_t);
typedef size_t (*retro_audio_sample_batch_t)(const int16_t *, size_t);
typedef void (*retro_input_poll_t)(void);
typedef int16_t (*retro_input_state_t)(unsigned, unsigned, unsigned, unsigned);

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
	retro_game_geometry geometry;
	retro_system_timing timing;
};

static unsigned g_videoCalls;
static unsigned g_nonNullFrames;
static unsigned g_lastW;
static unsigned g_lastH;
static unsigned g_maxObservedW;
static unsigned g_maxObservedH;
static unsigned g_audioCallbacks;
static size_t g_audioFrames;
static unsigned g_optionsRegistered;
static unsigned g_diskControlRegistered;
static unsigned g_geometryUpdates;
static unsigned g_controllerInfoRegistered;
static unsigned g_inputBitmaskQueries;
static unsigned g_achievementsDisabled;
static unsigned g_optionKeysValidated;
static unsigned g_optionHardwareCategoryValidated;
static unsigned g_coreOptionsVersion = 2;
static unsigned g_port2InputPolls;
static unsigned g_joypadMaskPolls;
static unsigned g_joypadButtonPolls;
static unsigned g_analogInputPolls;
static unsigned g_keyboardInputPolls;
static unsigned g_mouseInputPolls;
static unsigned g_lightGunInputPolls;
static unsigned g_geometryMatrixChecks;
static bool g_variablesUpdated;
static const char *g_videoStandardValue = "pal";
static const char *g_cropOverscanValue = "normal";
static const char *g_artifactingValue = "auto";
static const char *g_cpuValue = "65c02";
static const char *g_illegalInstructionsValue = "disabled";
static const char *g_randomLaunchDelayValue = "disabled";
static const char *g_randomizeExeMemoryValue = "enabled";
static const char *g_stereoPokeyValue = "disabled";
static const char *g_vbxeValue = "disabled";
static const char *g_covoxValue = "disabled";
static const char *g_soundBoardValue = "disabled";
static const char *g_rapidusValue = "disabled";
static const char *g_stereoAsMonoValue = "enabled";
static const char *g_driveSoundsValue = "enabled";
static double g_lastSystemAvFps;
static unsigned g_lastGeometryW;
static unsigned g_lastGeometryH;
enum class CallbackPhase {
	None,
	LoadGame,
	Run,
};
static CallbackPhase g_callbackPhase = CallbackPhase::None;
static unsigned g_systemAvInfoDuringRun;
static unsigned g_systemAvInfoWrongPhase;
static uint16_t g_joypadMask;
static int16_t g_analogX = 12000;
static bool g_keyboardLeftDown;
static bool g_mouseLeftDown;
static bool g_lightGunTriggerDown;
static char g_systemDir[256];
static char g_saveDir[256];

struct retro_variable {
	const char *key;
	const char *value;
};

struct retro_core_option_value {
	const char *value;
	const char *label;
};

static constexpr size_t RETRO_NUM_CORE_OPTION_VALUES_MAX = 128;

struct retro_core_option_definition {
	const char *key;
	const char *desc;
	const char *info;
	retro_core_option_value values[RETRO_NUM_CORE_OPTION_VALUES_MAX];
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
	retro_core_option_value values[RETRO_NUM_CORE_OPTION_VALUES_MAX];
	const char *default_value;
};

struct retro_core_options_v2 {
	retro_core_option_v2_category *categories;
	retro_core_option_v2_definition *definitions;
};

struct retro_core_options_v2_intl {
	retro_core_options_v2 *us;
	retro_core_options_v2 *local;
};

static_assert(RETRO_NUM_CORE_OPTION_VALUES_MAX == 128);
static_assert(offsetof(retro_core_option_definition, values)
	== sizeof(const char *) * 3);
static_assert(offsetof(retro_core_option_v2_definition, values)
	== sizeof(const char *) * 6);
static_assert(sizeof(((retro_core_option_definition *)nullptr)->values)
	== sizeof(retro_core_option_value) * RETRO_NUM_CORE_OPTION_VALUES_MAX);
static_assert(sizeof(((retro_core_option_v2_definition *)nullptr)->values)
	== sizeof(retro_core_option_value) * RETRO_NUM_CORE_OPTION_VALUES_MAX);

struct retro_game_info {
	const char *path;
	const void *data;
	size_t size;
	const char *meta;
};

struct retro_disk_control_ext_callback {
	bool (*set_eject_state)(bool ejected);
	bool (*get_eject_state)(void);
	unsigned (*get_image_index)(void);
	bool (*set_image_index)(unsigned index);
	unsigned (*get_num_images)(void);
	bool (*replace_image_index)(unsigned index, const void *info);
	bool (*add_image_index)(void);
	bool (*set_initial_image)(unsigned index, const char *path);
	bool (*get_image_path)(unsigned index, char *path, size_t len);
	bool (*get_image_label)(unsigned index, char *label, size_t len);
};

struct retro_keyboard_callback {
	void (*callback)(bool down, unsigned keycode, uint32_t character,
		uint16_t key_modifiers);
};

struct retro_controller_description {
	const char *desc;
	unsigned id;
};

struct retro_controller_info {
	const retro_controller_description *types;
	unsigned num_types;
};

static retro_keyboard_callback g_keyboardCallback {};
static retro_disk_control_ext_callback g_diskControl {};

static const char *const kRequiredOptionKeys[] = {
	"altirra_system",
	"altirra_memory",
	"altirra_video_standard",
	"altirra_basic",
	"altirra_cpu",
	"altirra_illegal_instructions",
	"altirra_random_launch_delay",
	"altirra_randomize_exe_memory",
	"altirra_stereo_pokey",
	"altirra_vbxe",
	"altirra_covox",
	"altirra_soundboard",
	"altirra_rapidus",
	"altirra_sio_patch",
	"altirra_artifacting",
	"altirra_crop_overscan",
	"altirra_audio_filters",
	"altirra_stereo_as_mono",
	"altirra_drive_sounds",
	"altirra_input_port1",
	"altirra_input_port2",
};

static bool validate_option_keys_v1(const retro_core_option_definition *defs) {
	if (!defs)
		return false;

	for (const char *required : kRequiredOptionKeys) {
		bool found = false;
		for (const auto *def = defs; def->key; ++def) {
			if (!strcmp(def->key, required)
				&& def->values[0].value && def->default_value) {
				bool defaultFound = false;
				for (const auto *value = def->values; value->value; ++value) {
					if (!strcmp(value->value, def->default_value)) {
						defaultFound = true;
						break;
					}
				}

				if (!defaultFound) {
					fprintf(stderr, "default value missing in V1 option: %s\n",
						required);
					return false;
				}

				found = true;
				break;
			}
		}

		if (!found) {
			fprintf(stderr, "missing V1 core option: %s\n", required);
			return false;
		}
	}

	return true;
}

static bool validate_option_keys_v2(const retro_core_options_v2 *opts) {
	if (!opts || !opts->categories || !opts->definitions)
		return false;

	bool sawHardware = false;
	for (const auto *cat = opts->categories; cat->key; ++cat) {
		if (!strcmp(cat->key, "hardware"))
			sawHardware = true;
	}

	if (!sawHardware) {
		fprintf(stderr, "missing V2 hardware option category\n");
		return false;
	}

	for (const char *required : kRequiredOptionKeys) {
		bool found = false;
		for (const auto *def = opts->definitions; def->key; ++def) {
			if (!strcmp(def->key, required)
				&& def->values[0].value && def->default_value
				&& def->category_key) {
				bool defaultFound = false;
				for (const auto *value = def->values; value->value; ++value) {
					if (!strcmp(value->value, def->default_value)) {
						defaultFound = true;
						break;
					}
				}

				if (!defaultFound) {
					fprintf(stderr, "default value missing in V2 option: %s\n",
						required);
					return false;
				}

				found = true;
				break;
			}
		}

		if (!found) {
			fprintf(stderr, "missing V2 core option: %s\n", required);
			return false;
		}
	}

	++g_optionHardwareCategoryValidated;
	return true;
}

static bool validate_option_variables(const retro_variable *vars) {
	if (!vars)
		return false;

	for (const char *required : kRequiredOptionKeys) {
		bool found = false;
		for (const auto *var = vars; var->key; ++var) {
			if (!strcmp(var->key, required) && var->value) {
				found = true;
				break;
			}
		}

		if (!found) {
			fprintf(stderr, "missing legacy core option variable: %s\n",
				required);
			return false;
		}
	}

	return true;
}

static bool extension_list_contains(const char *extensions, const char *needle) {
	if (!extensions || !needle || !*needle)
		return false;

	const size_t needleLen = strlen(needle);
	const char *p = extensions;
	while (*p) {
		const char *end = strchr(p, '|');
		const size_t len = end ? (size_t)(end - p) : strlen(p);
		if (len == needleLen && !strncmp(p, needle, needleLen))
			return true;

		if (!end)
			break;

		p = end + 1;
	}

	return false;
}

static bool validate_system_info(const retro_system_info& si) {
	if (!si.library_name || strcmp(si.library_name, "Altirra")) {
		fprintf(stderr, "unexpected library_name: %s\n",
			si.library_name ? si.library_name : "(null)");
		return false;
	}

	if (!si.library_version || !*si.library_version) {
		fprintf(stderr, "missing library_version\n");
		return false;
	}

	if (!si.need_fullpath) {
		fprintf(stderr, "need_fullpath should be true\n");
		return false;
	}

	static const char *const requiredExts[] = {
		"atr", "atx", "xex", "exe", "bas", "cas", "car", "a52", "m3u"
	};

	for (const char *ext : requiredExts) {
		if (!extension_list_contains(si.valid_extensions, ext)) {
			fprintf(stderr, "missing valid extension: %s\n", ext);
			return false;
		}
	}

	return true;
}

static const char *lookup_variable_value(const char *key) {
	if (!strcmp(key, "altirra_video_standard"))
		return g_videoStandardValue;
	if (!strcmp(key, "altirra_input_port2"))
		return "joystick";
	if (!strcmp(key, "altirra_crop_overscan"))
		return g_cropOverscanValue;
	if (!strcmp(key, "altirra_artifacting"))
		return g_artifactingValue;
	if (!strcmp(key, "altirra_cpu"))
		return g_cpuValue;
	if (!strcmp(key, "altirra_illegal_instructions"))
		return g_illegalInstructionsValue;
	if (!strcmp(key, "altirra_random_launch_delay"))
		return g_randomLaunchDelayValue;
	if (!strcmp(key, "altirra_randomize_exe_memory"))
		return g_randomizeExeMemoryValue;
	if (!strcmp(key, "altirra_stereo_pokey"))
		return g_stereoPokeyValue;
	if (!strcmp(key, "altirra_vbxe"))
		return g_vbxeValue;
	if (!strcmp(key, "altirra_covox"))
		return g_covoxValue;
	if (!strcmp(key, "altirra_soundboard"))
		return g_soundBoardValue;
	if (!strcmp(key, "altirra_rapidus"))
		return g_rapidusValue;
	if (!strcmp(key, "altirra_stereo_as_mono"))
		return g_stereoAsMonoValue;
	if (!strcmp(key, "altirra_drive_sounds"))
		return g_driveSoundsValue;

	return nullptr;
}

static bool env_cb(unsigned cmd, void *data) {
	switch (cmd) {
		case 9: {	// GET_SYSTEM_DIRECTORY
			const char **path = (const char **)data;
			*path = g_systemDir;
			return true;
		}
		case 10:	// SET_PIXEL_FORMAT
		case 18:	// SET_SUPPORT_NO_GAME
			return true;
		case 12: {	// SET_KEYBOARD_CALLBACK
			auto *cb = (retro_keyboard_callback *)data;
			if (!cb || !cb->callback)
				return false;
			g_keyboardCallback = *cb;
			return true;
		}
		case 15: {	// GET_VARIABLE
			auto *var = (retro_variable *)data;
			if (var && var->key) {
				var->value = lookup_variable_value(var->key);
				if (var->value)
					return true;
			}
			return false;
		}
		case 16: {	// SET_VARIABLES
			if (!validate_option_variables((retro_variable *)data))
				return false;
			++g_optionKeysValidated;
			++g_optionsRegistered;
			return true;
		}
		case 53: {	// SET_CORE_OPTIONS
			if (!validate_option_keys_v1(
				(retro_core_option_definition *)data))
			{
				return false;
			}
			++g_optionKeysValidated;
			++g_optionsRegistered;
			return true;
		}
		case 67: {	// SET_CORE_OPTIONS_V2
			if (!validate_option_keys_v2((retro_core_options_v2 *)data))
				return false;
			++g_optionKeysValidated;
			++g_optionsRegistered;
			return true;
		}
		case 68: {	// SET_CORE_OPTIONS_V2_INTL
			auto *intl = (retro_core_options_v2_intl *)data;
			if (!intl || !validate_option_keys_v2(intl->us))
				return false;
			++g_optionKeysValidated;
			++g_optionsRegistered;
			return true;
		}
		case 17: {	// GET_VARIABLE_UPDATE
			auto *updated = (bool *)data;
			*updated = g_variablesUpdated;
			g_variablesUpdated = false;
			return true;
		}
		case 32: {	// SET_SYSTEM_AV_INFO
			auto *av = (retro_system_av_info *)data;
			if (g_callbackPhase == CallbackPhase::Run) {
				++g_systemAvInfoDuringRun;
			} else {
				++g_systemAvInfoWrongPhase;
				fprintf(stderr,
					"SET_SYSTEM_AV_INFO outside retro_run phase\n");
			}

			if (av)
				g_lastSystemAvFps = av->timing.fps;
			return true;
		}
		case 35: {	// SET_CONTROLLER_INFO
			auto *info = (retro_controller_info *)data;
			if (!info || !info[0].types || info[0].num_types < 3
				|| !info[1].types || info[1].num_types < 3)
			{
				return false;
			}

			for (unsigned port = 0; port < 2; ++port) {
				bool sawJoypad = false;
				bool sawAnalog = false;
				bool sawMouse = false;
				bool sawLightGun = false;
				bool sawNone = false;

				for (unsigned i = 0; i < info[port].num_types; ++i) {
					switch (info[port].types[i].id) {
						case 0: sawNone = true; break;	// RETRO_DEVICE_NONE
						case 1: sawJoypad = true; break;	// RETRO_DEVICE_JOYPAD
						case 2: sawMouse = true; break;	// RETRO_DEVICE_MOUSE
						case 4: sawLightGun = true; break;	// RETRO_DEVICE_LIGHTGUN
						case 5: sawAnalog = true; break;	// RETRO_DEVICE_ANALOG
					}
				}

				if (!sawJoypad || !sawAnalog || !sawMouse
					|| !sawLightGun || !sawNone)
				{
					return false;
				}
			}

			++g_controllerInfoRegistered;
			return true;
		}
		case 37: {	// SET_GEOMETRY
			auto *geometry = (retro_game_geometry *)data;
			if (geometry) {
				g_lastGeometryW = geometry->base_width;
				g_lastGeometryH = geometry->base_height;
			}
			++g_geometryUpdates;
			return true;
		}
		case 42: {	// SET_SUPPORT_ACHIEVEMENTS
			auto *enabled = (bool *)data;
			if (!enabled || *enabled)
				return false;

			++g_achievementsDisabled;
			return true;
		}
		case 31: {	// GET_SAVE_DIRECTORY
			const char **path = (const char **)data;
			*path = g_saveDir;
			return true;
		}
		case 0x10000 + 51: {	// GET_INPUT_BITMASKS
			auto *supported = (bool *)data;
			*supported = true;
			++g_inputBitmaskQueries;
			return true;
		}
		case 52: {	// GET_CORE_OPTIONS_VERSION
			auto *version = (unsigned *)data;
			*version = g_coreOptionsVersion;
			return true;
		}
		case 58: {	// SET_DISK_CONTROL_EXT_INTERFACE
			auto *cb = (retro_disk_control_ext_callback *)data;
			if (!cb || !cb->set_eject_state || !cb->get_eject_state
				|| !cb->get_image_index || !cb->set_image_index
				|| !cb->get_num_images || !cb->replace_image_index
				|| !cb->add_image_index || !cb->set_initial_image
				|| !cb->get_image_path || !cb->get_image_label)
			{
				return false;
			}
			g_diskControl = *cb;
			++g_diskControlRegistered;
			return true;
		}
		default:
			return false;
	}
}

static void video_cb(const void *data, unsigned w, unsigned h, size_t) {
	++g_videoCalls;

	if (data && w && h) {
		++g_nonNullFrames;
		g_lastW = w;
		g_lastH = h;
		if (g_maxObservedW < w)
			g_maxObservedW = w;
		if (g_maxObservedH < h)
			g_maxObservedH = h;
	}
}

static void audio_cb(int16_t, int16_t) {
	++g_audioCallbacks;
	++g_audioFrames;
}

static size_t audio_batch_cb(const int16_t *, size_t frames) {
	if (frames) {
		++g_audioCallbacks;
		g_audioFrames += frames;
	}

	return frames;
}
static void input_poll_cb() {}
static int16_t input_state_cb(unsigned port, unsigned device, unsigned index, unsigned id) {
	if (port == 1)
		++g_port2InputPolls;

	if (device == 5 && index == 0 && id == 0) {	// RETRO_DEVICE_ANALOG, left X
		++g_analogInputPolls;
		return g_analogX;
	}

	if (device == 3) {	// RETRO_DEVICE_KEYBOARD
		++g_keyboardInputPolls;
		return (id == 276 && g_keyboardLeftDown) ? 1 : 0;	// RETROK_LEFT
	}

	if (device == 2) {	// RETRO_DEVICE_MOUSE
		++g_mouseInputPolls;
		if (id == 0)
			return 4;	// X delta
		if (id == 1)
			return -3;	// Y delta
		if (id == 2)
			return g_mouseLeftDown ? 1 : 0;
		return 0;
	}

	if (device == 4) {	// RETRO_DEVICE_LIGHTGUN
		++g_lightGunInputPolls;
		if (id == 13)
			return 8192;	// SCREEN_X
		if (id == 14)
			return -4096;	// SCREEN_Y
		if (id == 2)
			return g_lightGunTriggerDown ? 1 : 0;
		return 0;
	}

	if (device == 1 && id == 256) {	// RETRO_DEVICE_JOYPAD, MASK
		++g_joypadMaskPolls;
		return (int16_t)g_joypadMask;
	}

	if (device != 1 || id >= 16)
		return 0;

	++g_joypadButtonPolls;
	return (g_joypadMask & (uint16_t)(1U << id)) ? 1 : 0;
}

template<typename T>
static T sym(void *lib, const char *name) {
	void *p = dlsym(lib, name);
	if (!p)
		fprintf(stderr, "missing %s: %s\n", name, dlerror());
	return reinterpret_cast<T>(p);
}

struct CallbackPhaseScope {
	CallbackPhaseScope(CallbackPhase phase)
		: oldPhase(g_callbackPhase)
	{
		g_callbackPhase = phase;
	}

	~CallbackPhaseScope() {
		g_callbackPhase = oldPhase;
	}

	CallbackPhase oldPhase;
};

static bool load_core_game(bool (*retro_load_game)(const void *),
	const retro_game_info *game)
{
	CallbackPhaseScope phase(CallbackPhase::LoadGame);
	return retro_load_game(game);
}

static void run_core_frame(void (*retro_run)()) {
	CallbackPhaseScope phase(CallbackPhase::Run);
	retro_run();
}

static bool run_geometry_case(void (*retro_run)(), const char *standard,
	const char *artifacting, const char *crop, unsigned maxW, unsigned maxH)
{
	g_videoStandardValue = standard;
	g_artifactingValue = artifacting;
	g_cropOverscanValue = crop;
	g_variablesUpdated = true;

	for (int i = 0; i < 6; ++i)
		run_core_frame(retro_run);

	++g_geometryMatrixChecks;
	if (!g_lastW || !g_lastH || g_lastW > maxW || g_lastH > maxH) {
		fprintf(stderr,
			"geometry case exceeded advertised max: standard=%s artifact=%s crop=%s frame=%ux%u max=%ux%u\n",
			standard, artifacting, crop, g_lastW, g_lastH, maxW, maxH);
		return false;
	}

	if (g_lastGeometryW && (g_lastGeometryW > maxW || g_lastGeometryH > maxH)) {
		fprintf(stderr,
			"geometry callback exceeded advertised max: standard=%s artifact=%s crop=%s geometry=%ux%u max=%ux%u\n",
			standard, artifacting, crop, g_lastGeometryW, g_lastGeometryH,
			maxW, maxH);
		return false;
	}

	return true;
}

int main(int argc, char **argv) {
	if (argc != 2 && argc != 3) {
		fprintf(stderr, "usage: %s CORE [core-options-version]\n", argv[0]);
		return 2;
	}

	if (argc == 3)
		g_coreOptionsVersion = (unsigned)atoi(argv[2]);

	strcpy(g_systemDir, "/tmp/altirra-libretro-system-XXXXXX");
	strcpy(g_saveDir, "/tmp/altirra-libretro-save-XXXXXX");
	if (!mkdtemp(g_systemDir) || !mkdtemp(g_saveDir)) {
		perror("mkdtemp");
		return 1;
	}
	char systemAltirra[320];
	snprintf(systemAltirra, sizeof systemAltirra, "%s/Altirra", g_systemDir);
	mkdir(systemAltirra, 0755);

	void *lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
	if (!lib) {
		fprintf(stderr, "dlopen failed: %s\n", dlerror());
		return 1;
	}

	auto retro_set_environment = sym<void (*)(retro_environment_t)>(lib, "retro_set_environment");
	auto retro_set_video_refresh = sym<void (*)(retro_video_refresh_t)>(lib, "retro_set_video_refresh");
	auto retro_set_audio_sample = sym<void (*)(retro_audio_sample_t)>(lib, "retro_set_audio_sample");
	auto retro_set_audio_sample_batch = sym<void (*)(retro_audio_sample_batch_t)>(lib, "retro_set_audio_sample_batch");
	auto retro_set_input_poll = sym<void (*)(retro_input_poll_t)>(lib, "retro_set_input_poll");
	auto retro_set_input_state = sym<void (*)(retro_input_state_t)>(lib, "retro_set_input_state");
	auto retro_set_controller_port_device = sym<void (*)(unsigned, unsigned)>(lib, "retro_set_controller_port_device");
	auto retro_init = sym<void (*)()>(lib, "retro_init");
	auto retro_deinit = sym<void (*)()>(lib, "retro_deinit");
	auto retro_get_system_info = sym<void (*)(retro_system_info *)>(lib, "retro_get_system_info");
	auto retro_get_system_av_info = sym<void (*)(retro_system_av_info *)>(lib, "retro_get_system_av_info");
	auto retro_load_game = sym<bool (*)(const void *)>(lib, "retro_load_game");
	auto retro_unload_game = sym<void (*)()>(lib, "retro_unload_game");
	auto retro_run = sym<void (*)()>(lib, "retro_run");
	auto retro_serialize_size = sym<size_t (*)()>(lib, "retro_serialize_size");
	auto retro_serialize = sym<bool (*)(void *, size_t)>(lib, "retro_serialize");
	auto retro_unserialize = sym<bool (*)(const void *, size_t)>(lib, "retro_unserialize");

	if (!retro_set_environment || !retro_set_video_refresh || !retro_set_audio_sample
		|| !retro_set_audio_sample_batch || !retro_set_input_poll || !retro_set_input_state
		|| !retro_set_controller_port_device
		|| !retro_init || !retro_deinit || !retro_get_system_info || !retro_get_system_av_info
		|| !retro_load_game || !retro_unload_game || !retro_run
		|| !retro_serialize_size || !retro_serialize || !retro_unserialize)
		return 1;

	retro_set_environment(env_cb);
	retro_set_video_refresh(video_cb);
	retro_set_audio_sample(audio_cb);
	retro_set_audio_sample_batch(audio_batch_cb);
	retro_set_input_poll(input_poll_cb);
	retro_set_input_state(input_state_cb);
	retro_init();
	retro_set_controller_port_device(0, 5);	// RETRO_DEVICE_ANALOG / paddle

	retro_system_info si {};
	retro_get_system_info(&si);
	if (!validate_system_info(si)) {
		retro_deinit();
		return 1;
	}

	retro_system_av_info av {};
	retro_get_system_av_info(&av);
	printf("core=%s version=%s base=%ux%u max=%ux%u fps=%.4f\n",
		si.library_name ? si.library_name : "",
		si.library_version ? si.library_version : "",
		av.geometry.base_width, av.geometry.base_height,
		av.geometry.max_width, av.geometry.max_height,
		av.timing.fps);

	if (!g_diskControl.set_initial_image
		|| !g_diskControl.set_initial_image(0, ""))
	{
		fprintf(stderr, "empty initial disk selection was rejected\n");
		retro_deinit();
		return 1;
	}

	if (!load_core_game(retro_load_game, nullptr)) {
		fprintf(stderr, "retro_load_game(NULL) failed\n");
		retro_deinit();
		return 1;
	}

	if (g_systemAvInfoWrongPhase) {
		fprintf(stderr,
			"SET_SYSTEM_AV_INFO was called outside retro_run during load\n");
		retro_unload_game();
		retro_deinit();
		return 1;
	}

	if (!g_keyboardCallback.callback) {
		fprintf(stderr, "keyboard callback was not registered\n");
		retro_deinit();
		return 1;
	}

	g_keyboardLeftDown = true;
	for (int i = 0; i < 5; ++i)
		run_core_frame(retro_run);
	g_keyboardLeftDown = false;
	for (int i = 0; i < 5; ++i)
		run_core_frame(retro_run);

	if (!g_keyboardInputPolls) {
		fprintf(stderr, "keyboard polling fallback was not exercised\n");
		retro_unload_game();
		retro_deinit();
		return 1;
	}

	g_keyboardCallback.callback(true, 276, 0, 0);	// RETROK_LEFT
	g_keyboardCallback.callback(false, 276, 0, 0);
	g_keyboardCallback.callback(true, 304, 0, 0);	// RETROK_LSHIFT
	g_keyboardCallback.callback(true, 97, 'A', 0);	// RETROK_a
	g_keyboardCallback.callback(false, 97, 0, 0);
	g_keyboardCallback.callback(false, 304, 0, 0);
	g_keyboardCallback.callback(true, 288, 0, 0);	// RETROK_F7 / Break
	g_keyboardCallback.callback(false, 288, 0, 0);

	for (int i = 0; i < 30; ++i)
		run_core_frame(retro_run);

	const unsigned normalW = g_lastW;
	const unsigned normalH = g_lastH;
	if (normalW != 336 || normalH != 240) {
		fprintf(stderr, "unexpected normal overscan frame %ux%u\n",
			normalW, normalH);
		retro_unload_game();
		retro_deinit();
		return 1;
	}

	g_cropOverscanValue = "full";
	g_variablesUpdated = true;
	for (int i = 0; i < 5; ++i)
		run_core_frame(retro_run);

	if (g_lastW <= normalW || g_lastH <= normalH
		|| g_lastGeometryW != g_lastW || g_lastGeometryH != g_lastH) {
		fprintf(stderr,
			"full overscan geometry did not expand/update: frame=%ux%u geometry=%ux%u normal=%ux%u\n",
			g_lastW, g_lastH, g_lastGeometryW, g_lastGeometryH, normalW, normalH);
		retro_unload_game();
		retro_deinit();
		return 1;
	}

	g_videoStandardValue = "ntsc";
	g_variablesUpdated = true;
	for (int i = 0; i < 5; ++i)
		run_core_frame(retro_run);

	if (g_lastSystemAvFps < 59.0 || g_lastSystemAvFps > 60.5) {
		fprintf(stderr, "NTSC core option did not update AV fps, got %.4f\n",
			g_lastSystemAvFps);
		retro_unload_game();
		retro_deinit();
		return 1;
	}

	g_videoStandardValue = "pal";
	g_variablesUpdated = true;
	for (int i = 0; i < 5; ++i)
		run_core_frame(retro_run);

	if (g_lastSystemAvFps < 49.0 || g_lastSystemAvFps > 50.5) {
		fprintf(stderr, "PAL core option did not update AV fps, got %.4f\n",
			g_lastSystemAvFps);
		retro_unload_game();
		retro_deinit();
		return 1;
	}

	const char *const standards[] = {
		"ntsc", "pal", "secam", "ntsc50", "pal60"
	};
	const char *const artifacts[] = {
		"none", "auto", "ntsc", "ntschi", "pal", "palhi"
	};
	const char *const crops[] = {
		"normal", "off", "extended", "full"
	};
	const unsigned expectedGeometryChecks =
		(unsigned)(sizeof standards / sizeof standards[0])
		* (unsigned)(sizeof artifacts / sizeof artifacts[0])
		* (unsigned)(sizeof crops / sizeof crops[0]);

	for (const char *standard : standards) {
		for (const char *artifacting : artifacts) {
			for (const char *crop : crops) {
				if (!run_geometry_case(retro_run, standard, artifacting, crop,
					av.geometry.max_width, av.geometry.max_height))
				{
					retro_unload_game();
					retro_deinit();
					return 1;
				}
			}
		}
	}

	if (g_geometryMatrixChecks != expectedGeometryChecks) {
		fprintf(stderr, "geometry matrix incomplete: %u/%u\n",
			g_geometryMatrixChecks, expectedGeometryChecks);
		retro_unload_game();
		retro_deinit();
		return 1;
	}

	g_videoStandardValue = "ntsc";
	g_artifactingValue = "auto";
	g_cropOverscanValue = "normal";
	g_variablesUpdated = true;
	for (int i = 0; i < 5; ++i)
		run_core_frame(retro_run);

	g_joypadMask = (uint16_t)((1U << 0) | (1U << 3) | (1U << 7));
	for (int i = 0; i < 30; ++i)
		run_core_frame(retro_run);

	g_joypadMask = 0;
	for (int i = 0; i < 60; ++i)
		run_core_frame(retro_run);

	retro_set_controller_port_device(0, 2);	// RETRO_DEVICE_MOUSE / ST mouse
	g_mouseLeftDown = true;
	for (int i = 0; i < 5; ++i)
		run_core_frame(retro_run);
	g_mouseLeftDown = false;
	for (int i = 0; i < 5; ++i)
		run_core_frame(retro_run);

	retro_set_controller_port_device(0, 4);	// RETRO_DEVICE_LIGHTGUN
	g_lightGunTriggerDown = true;
	for (int i = 0; i < 5; ++i)
		run_core_frame(retro_run);
	g_lightGunTriggerDown = false;
	for (int i = 0; i < 5; ++i)
		run_core_frame(retro_run);

	if (!g_diskControl.get_num_images || g_diskControl.get_num_images() != 0) {
		fprintf(stderr, "unexpected initial disk image count\n");
		retro_unload_game();
		retro_deinit();
		return 1;
	}

	if (!g_diskControl.set_image_index(0) || g_diskControl.get_image_index() != 0) {
		fprintf(stderr, "empty disk list restore selection failed\n");
		retro_unload_game();
		retro_deinit();
		return 1;
	}

	char emptyDiskPath[8] = { 'x' };
	char emptyDiskLabel[8] = { 'x' };
	if (!g_diskControl.get_image_path(0, emptyDiskPath, sizeof emptyDiskPath)
		|| *emptyDiskPath
		|| !g_diskControl.get_image_label(0, emptyDiskLabel, sizeof emptyDiskLabel)
		|| *emptyDiskLabel)
	{
		fprintf(stderr,
			"empty disk list path/label query failed: path=%s label=%s\n",
			emptyDiskPath, emptyDiskLabel);
		retro_unload_game();
		retro_deinit();
		return 1;
	}

	if (!g_diskControl.set_eject_state(true) || !g_diskControl.get_eject_state()) {
		fprintf(stderr, "disk eject state did not set\n");
		retro_unload_game();
		retro_deinit();
		return 1;
	}

	if (!g_diskControl.add_image_index() || g_diskControl.get_num_images() != 1) {
		fprintf(stderr, "disk image add failed\n");
		retro_unload_game();
		retro_deinit();
		return 1;
	}

	retro_game_info diskInfo {};
	diskInfo.path = "/tmp/altirra-libretro-smoke-disk.atr";
	if (!g_diskControl.replace_image_index(0, &diskInfo)
		|| !g_diskControl.set_image_index(0)
		|| g_diskControl.get_image_index() != 0)
	{
		fprintf(stderr, "disk image replace/select failed\n");
		retro_unload_game();
		retro_deinit();
		return 1;
	}

	char diskPath[256] {};
	char diskLabel[256] {};
	if (!g_diskControl.get_image_path(0, diskPath, sizeof diskPath)
		|| strcmp(diskPath, diskInfo.path)
		|| !g_diskControl.get_image_label(0, diskLabel, sizeof diskLabel)
		|| strcmp(diskLabel, "altirra-libretro-smoke-disk"))
	{
		fprintf(stderr, "disk image path/label mismatch: path=%s label=%s\n",
			diskPath, diskLabel);
		retro_unload_game();
		retro_deinit();
		return 1;
	}

	const unsigned removeIndex = g_diskControl.get_num_images();
	if (!g_diskControl.set_image_index(removeIndex)
		|| g_diskControl.get_image_index() != removeIndex
		|| !g_diskControl.set_eject_state(false)
		|| g_diskControl.get_eject_state())
	{
		fprintf(stderr, "disk removal/close failed\n");
		retro_unload_game();
		retro_deinit();
		return 1;
	}

	if (!g_diskControl.set_eject_state(true)
		|| !g_diskControl.replace_image_index(0, nullptr)
		|| g_diskControl.get_num_images() != 0
		|| !g_diskControl.set_eject_state(false))
	{
		fprintf(stderr, "disk image null replacement/removal failed\n");
		retro_unload_game();
		retro_deinit();
		return 1;
	}

	g_stereoPokeyValue = "enabled";
	g_vbxeValue = "enabled";
	g_covoxValue = "enabled";
	g_soundBoardValue = "enabled";
	g_variablesUpdated = true;
	for (int i = 0; i < 8; ++i)
		run_core_frame(retro_run);

	if (!g_lastW || !g_lastH
		|| g_lastW > av.geometry.max_width
		|| g_lastH > av.geometry.max_height)
	{
		fprintf(stderr,
			"add-on option update produced invalid geometry: frame=%ux%u max=%ux%u\n",
			g_lastW, g_lastH, av.geometry.max_width, av.geometry.max_height);
		retro_unload_game();
		retro_deinit();
		return 1;
	}

	const size_t stateSize = retro_serialize_size();
	if (!stateSize) {
		fprintf(stderr, "retro_serialize_size returned 0\n");
		retro_unload_game();
		retro_deinit();
		return 1;
	}

	void *state = malloc(stateSize);
	if (!state) {
		fprintf(stderr, "malloc(%zu) failed\n", stateSize);
		retro_unload_game();
		retro_deinit();
		return 1;
	}

	if (!retro_serialize(state, stateSize)) {
		fprintf(stderr, "retro_serialize failed for %zu-byte state\n", stateSize);
		free(state);
		retro_unload_game();
		retro_deinit();
		return 1;
	}

	for (int i = 0; i < 5; ++i)
		run_core_frame(retro_run);

	const size_t stateSize2 = retro_serialize_size();
	if (stateSize2 > stateSize) {
		fprintf(stderr, "retro_serialize_size increased: %zu -> %zu\n",
			stateSize, stateSize2);
		free(state);
		retro_unload_game();
		retro_deinit();
		return 1;
	}

	if (!retro_unserialize(state, stateSize)) {
		fprintf(stderr, "retro_unserialize failed for %zu-byte state\n", stateSize);
		free(state);
		retro_unload_game();
		retro_deinit();
		return 1;
	}

	free(state);

	for (int i = 0; i < 5; ++i)
		run_core_frame(retro_run);

	printf("video_calls=%u non_null=%u last=%ux%u\n",
		g_videoCalls, g_nonNullFrames, g_lastW, g_lastH);
	printf("audio_callbacks=%u audio_frames=%zu\n",
		g_audioCallbacks, g_audioFrames);
	printf("state_size=%zu options_registered=%u av_fps=%.4f\n",
		stateSize, g_optionsRegistered, g_lastSystemAvFps);
	printf("option_keys_validated=%u hardware_categories=%u\n",
		g_optionKeysValidated, g_optionHardwareCategoryValidated);
	printf("disk_control_registered=%u\n", g_diskControlRegistered);
	printf("geometry_updates=%u last_geometry=%ux%u\n",
		g_geometryUpdates, g_lastGeometryW, g_lastGeometryH);
	printf("system_av_info_updates=%u wrong_phase=%u\n",
		g_systemAvInfoDuringRun, g_systemAvInfoWrongPhase);
	printf("geometry_matrix_checks=%u max_observed=%ux%u\n",
		g_geometryMatrixChecks, g_maxObservedW, g_maxObservedH);
	printf("controller_info_registered=%u\n", g_controllerInfoRegistered);
	printf("input_bitmask_queries=%u joypad_mask_polls=%u joypad_button_polls=%u\n",
		g_inputBitmaskQueries, g_joypadMaskPolls, g_joypadButtonPolls);
	printf("achievements_disabled=%u\n", g_achievementsDisabled);
	printf("port2_input_polls=%u\n", g_port2InputPolls);
	printf("analog_input_polls=%u\n", g_analogInputPolls);
	printf("keyboard_input_polls=%u\n", g_keyboardInputPolls);
	printf("mouse_input_polls=%u\n", g_mouseInputPolls);
	printf("lightgun_input_polls=%u\n", g_lightGunInputPolls);

	retro_unload_game();
	retro_deinit();

	return g_videoCalls >= 140 && g_nonNullFrames > 0 && g_lastW && g_lastH
		&& g_audioCallbacks > 0 && g_audioFrames > 0 && g_optionsRegistered > 0
		&& g_optionKeysValidated > 0
		&& (g_coreOptionsVersion < 2 || g_optionHardwareCategoryValidated > 0)
		&& g_diskControlRegistered > 0
		&& g_geometryUpdates > 0
		&& g_systemAvInfoDuringRun > 0
		&& g_systemAvInfoWrongPhase == 0
		&& g_geometryMatrixChecks == expectedGeometryChecks
		&& g_controllerInfoRegistered > 0
		&& g_inputBitmaskQueries > 0
		&& g_joypadMaskPolls > 0
		&& g_achievementsDisabled > 0
		&& g_port2InputPolls > 0
		&& g_analogInputPolls > 0
		&& g_keyboardInputPolls > 0
		&& g_mouseInputPolls > 0
		&& g_lightGunInputPolls > 0
		? 0 : 1;
}
