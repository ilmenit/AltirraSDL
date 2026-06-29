#include <dlfcn.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <map>
#include <string>
#include <vector>

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
	const retro_memory_descriptor *descriptors;
	unsigned num_descriptors;
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
static unsigned g_subsystemsRegistered;
static unsigned g_inputDescriptorsValidated;
static unsigned g_inputBitmaskQueries;
static unsigned g_achievementsEnabled;
static unsigned g_memoryMapsRegistered;
static unsigned g_optionKeysValidated;
static unsigned g_optionHardwareCategoryValidated;
static unsigned g_coreOptionsVersion = 2;
static unsigned g_port2InputPolls;
static unsigned g_joypadMaskPolls;
static unsigned g_concurrentJoypadMaskPolls;
static unsigned g_joypadButtonPolls;
static unsigned g_analogInputPolls;
static unsigned g_analogJoystickYPolls;
static unsigned g_keyboardInputPolls;
static unsigned g_mouseInputPolls;
static unsigned g_lightGunInputPolls;
static unsigned g_geometryMatrixChecks;
static unsigned g_vkbdOverlayFrames;
static bool g_variablesUpdated;
static const char *g_videoStandardValue = "pal";
static const char *g_cropOverscanValue = "normal";
static const char *g_aspectValue = "4_3";
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
static const char *g_systemValue = "auto";
static const char *g_inputPort1Value = "auto";
static const char *g_controlSchemeValue = "auto";
static const char *g_vkbdToggleValue = "r_l3_select_r2";
static const char *g_padYKeyValue = "auto";
static bool g_expectContentAuto5200;
static double g_lastSystemAvFps;
static unsigned g_lastGeometryW;
static unsigned g_lastGeometryH;
static float g_lastGeometryAspect;
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
	const retro_subsystem_memory_info *memory;
	unsigned num_memory;
};

struct retro_subsystem_info {
	const char *desc;
	const char *ident;
	const retro_subsystem_rom_info *roms;
	unsigned num_roms;
	unsigned id;
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

struct retro_input_descriptor {
	unsigned port;
	unsigned device;
	unsigned index;
	unsigned id;
	const char *description;
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
	"altirra_disk_write_mode",
	"altirra_artifacting",
	"altirra_performance_tier",
	"altirra_crop_overscan",
	"altirra_aspect",
	"altirra_audio_filters",
	"altirra_stereo_as_mono",
	"altirra_drive_sounds",
	"altirra_input_port1",
	"altirra_input_port2",
	"altirra_control_scheme",
	"altirra_vkbd_toggle",
	"altirra_warm_reset_combo",
	"altirra_cold_reset_combo",
	"altirra_pad_y_key",
	"altirra_pad_x_key",
	"altirra_pad_l2_key",
	"altirra_pad_r2_key",
	"altirra_pad_l3_key",
	"altirra_pad_r3_key",
	"altirra_key_start",
	"altirra_key_select",
	"altirra_key_option",
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

static std::string trim_copy(const std::string& s) {
	const size_t begin = s.find_first_not_of(" \t\r\n");
	if (begin == std::string::npos)
		return {};

	const size_t end = s.find_last_not_of(" \t\r\n");
	return s.substr(begin, end - begin + 1);
}

static bool load_core_info_file(const char *path,
	std::map<std::string, std::string>& out)
{
	if (!path || !*path)
		return true;

	FILE *fp = fopen(path, "rb");
	if (!fp) {
		perror(path);
		return false;
	}

	char line[4096];
	unsigned lineNo = 0;
	while (fgets(line, sizeof line, fp)) {
		++lineNo;

		std::string s = trim_copy(line);
		if (s.empty() || s[0] == '#')
			continue;

		const size_t eq = s.find('=');
		if (eq == std::string::npos) {
			fprintf(stderr, "%s:%u: malformed .info line\n", path, lineNo);
			fclose(fp);
			return false;
		}

		std::string key = trim_copy(s.substr(0, eq));
		std::string value = trim_copy(s.substr(eq + 1));
		if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
			value = value.substr(1, value.size() - 2);

		if (key.empty() || !out.emplace(key, value).second) {
			fprintf(stderr, "%s:%u: duplicate or empty .info key\n",
				path, lineNo);
			fclose(fp);
			return false;
		}
	}

	fclose(fp);
	return true;
}

static bool require_info_value(const std::map<std::string, std::string>& info,
	const char *key, const char *actual)
{
	const auto it = info.find(key);
	if (it == info.end()) {
		fprintf(stderr, "core info missing key: %s\n", key);
		return false;
	}

	const char *actualSafe = actual ? actual : "";
	if (it->second != actualSafe) {
		fprintf(stderr, "core info mismatch for %s: info='%s' core='%s'\n",
			key, it->second.c_str(), actualSafe);
		return false;
	}

	return true;
}

static bool validate_system_info(const retro_system_info& si,
	const std::map<std::string, std::string>& coreInfo)
{
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

	if (!coreInfo.empty()) {
		if (!require_info_value(coreInfo, "corename", si.library_name)
			|| !require_info_value(coreInfo, "display_version",
				si.library_version)
			|| !require_info_value(coreInfo, "supported_extensions",
				si.valid_extensions)
			|| !require_info_value(coreInfo, "cheats", "true")
			|| !require_info_value(coreInfo, "load_subsystem", "true")
			|| !require_info_value(coreInfo, "libretro_saves", "true")
			|| !require_info_value(coreInfo, "memory_descriptors", "true")
			|| !require_info_value(coreInfo, "needs_kbd_mouse_focus", "false"))
		{
			return false;
		}

		const auto it = coreInfo.find("needs_fullpath");
		if (it == coreInfo.end()) {
			fprintf(stderr, "core info missing key: needs_fullpath\n");
			return false;
		}

		if ((it->second == "true") != si.need_fullpath) {
			fprintf(stderr,
				"core info mismatch for needs_fullpath: info='%s' core='%s'\n",
				it->second.c_str(), si.need_fullpath ? "true" : "false");
			return false;
		}
	}

	return true;
}

static bool has_input_descriptor(const retro_input_descriptor *descs,
	unsigned port, unsigned device, unsigned index, unsigned id,
	const char *description)
{
	for (const auto *desc = descs; desc && desc->description; ++desc) {
		if (desc->port == port
			&& desc->device == device
			&& desc->index == index
			&& desc->id == id
			&& !strcmp(desc->description, description))
		{
			return true;
		}
	}

	return false;
}

static bool vkbd_toggle_has_button(unsigned id) {
	const char *value = g_vkbdToggleValue;
	if (strcmp(value, "r_l3_select_r2")
		&& strcmp(value, "r")
		&& strcmp(value, "l3")
		&& strcmp(value, "r3")
		&& strcmp(value, "select_r2")
		&& strcmp(value, "none"))
	{
		value = "r_l3_select_r2";
	}

	if (!strcmp(value, "r_l3_select_r2"))
		return id == 11 || id == 14;
	if (!strcmp(value, "r"))
		return id == 11;
	if (!strcmp(value, "l3"))
		return id == 14;
	if (!strcmp(value, "r3"))
		return id == 15;

	return false;
}

static bool vkbd_toggle_has_select_r2() {
	const char *value = g_vkbdToggleValue;
	if (strcmp(value, "r_l3_select_r2")
		&& strcmp(value, "r")
		&& strcmp(value, "l3")
		&& strcmp(value, "r3")
		&& strcmp(value, "select_r2")
		&& strcmp(value, "none"))
	{
		value = "r_l3_select_r2";
	}

	return !strcmp(value, "r_l3_select_r2")
		|| !strcmp(value, "select_r2");
}

static const char *expected_direct_r_descriptor() {
	return vkbd_toggle_has_button(11) ? "Virtual Keyboard" : "Unassigned";
}

static const char *expected_pad_descriptor(unsigned slot) {
	const char *scheme = g_controlSchemeValue;
	if (strcmp(scheme, "auto")
		&& strcmp(scheme, "common")
		&& strcmp(scheme, "joystick")
		&& strcmp(scheme, "flight")
		&& strcmp(scheme, "adventure")
		&& strcmp(scheme, "5200"))
	{
		scheme = "auto";
	}

	if (!strcmp(scheme, "auto"))
		scheme = (!strcmp(g_systemValue, "5200") || g_expectContentAuto5200)
			? "5200"
			: "common";

	static char descriptions[6][64];
	const char *binding = "Unassigned";

	if (slot == 0 && !strcmp(g_padYKeyValue, "t"))
		binding = "T";
	else
	if (!strcmp(scheme, "joystick") || !strcmp(scheme, "5200")) {
		static const char *const keys[] = {
			"Unassigned", "Unassigned", "Unassigned", "Unassigned",
			"Unassigned", "Unassigned"
		};
		binding = keys[slot];
	} else if (!strcmp(scheme, "flight")) {
		static const char *const keys[] = {
			"F", "A", "M", "S", "G", "Unassigned"
		};
		binding = keys[slot];
	} else if (!strcmp(scheme, "adventure")) {
		static const char *const keys[] = {
			"Space", "Esc", "N", "Return", "Y", "Unassigned"
		};
		binding = keys[slot];
	} else {
		static const char *const keys[] = {
			"Space", "Return", "Esc", "Return", "Unassigned", "Unassigned"
		};
		binding = keys[slot];
	}

	static const unsigned retroIds[] = { 1, 9, 12, 13, 14, 15 };
	if (vkbd_toggle_has_button(retroIds[slot]))
		return "Virtual Keyboard";

	const bool r2Combo = slot == 3 && vkbd_toggle_has_select_r2();
	if (r2Combo && strcmp(binding, "Unassigned")) {
		snprintf(descriptions[slot], sizeof descriptions[slot],
			"%s / VKBD Combo", binding);
		return descriptions[slot];
	}
	if (r2Combo)
		return "VKBD Combo";

	return binding;
}

static bool validate_input_descriptors(const retro_input_descriptor *descs) {
	if (!descs)
		return false;

	if (!has_input_descriptor(descs, 0, 1, 0, 1,
			expected_pad_descriptor(0))
		|| !has_input_descriptor(descs, 0, 1, 0, 9,
			expected_pad_descriptor(1))
		|| !has_input_descriptor(descs, 0, 1, 0, 11,
			expected_direct_r_descriptor())
		|| !has_input_descriptor(descs, 0, 1, 0, 12,
			expected_pad_descriptor(2))
		|| !has_input_descriptor(descs, 0, 1, 0, 13,
			expected_pad_descriptor(3))
		|| !has_input_descriptor(descs, 0, 1, 0, 14,
			expected_pad_descriptor(4))
		|| !has_input_descriptor(descs, 0, 1, 0, 15,
			expected_pad_descriptor(5))
		|| !has_input_descriptor(descs, 0, 5, 0, 0, "Analog X / Paddle Knob")
		|| !has_input_descriptor(descs, 0, 5, 0, 1, "Joystick Analog Y"))
	{
		fprintf(stderr, "missing expected controller input descriptor\n");
		return false;
	}

	return true;
}

static const char *lookup_variable_value(const char *key) {
	if (!strcmp(key, "altirra_system"))
		return g_systemValue;
	if (!strcmp(key, "altirra_video_standard"))
		return g_videoStandardValue;
	if (!strcmp(key, "altirra_input_port1"))
		return g_inputPort1Value;
	if (!strcmp(key, "altirra_input_port2"))
		return "joystick";
	if (!strcmp(key, "altirra_crop_overscan"))
		return g_cropOverscanValue;
	if (!strcmp(key, "altirra_aspect"))
		return g_aspectValue;
	if (!strcmp(key, "altirra_artifacting"))
		return g_artifactingValue;
	if (!strcmp(key, "altirra_performance_tier"))
		return "quality";
	if (!strcmp(key, "altirra_audio_filters"))
		return "auto";
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
	if (!strcmp(key, "altirra_disk_write_mode"))
		return "safe_sidecar";
	if (!strcmp(key, "altirra_stereo_as_mono"))
		return g_stereoAsMonoValue;
	if (!strcmp(key, "altirra_drive_sounds"))
		return g_driveSoundsValue;
	if (!strcmp(key, "altirra_control_scheme"))
		return g_controlSchemeValue;
	if (!strcmp(key, "altirra_vkbd_toggle"))
		return g_vkbdToggleValue;
	if (!strcmp(key, "altirra_warm_reset_combo"))
		return "select_start";
	if (!strcmp(key, "altirra_cold_reset_combo"))
		return "select_l";
	if (!strcmp(key, "altirra_pad_y_key"))
		return g_padYKeyValue;
	if (!strcmp(key, "altirra_pad_x_key")
		|| !strcmp(key, "altirra_pad_l2_key")
		|| !strcmp(key, "altirra_pad_r2_key")
		|| !strcmp(key, "altirra_pad_l3_key")
		|| !strcmp(key, "altirra_pad_r3_key"))
	{
		return "auto";
	}
	if (!strcmp(key, "altirra_key_start"))
		return "none";
	if (!strcmp(key, "altirra_key_select"))
		return "none";
	if (!strcmp(key, "altirra_key_option"))
		return "none";

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
		case 11: {	// SET_INPUT_DESCRIPTORS
			if (!validate_input_descriptors((retro_input_descriptor *)data))
				return false;
			++g_inputDescriptorsValidated;
			return true;
		}
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
		case 34: {	// SET_SUBSYSTEM_INFO
			auto *info = (retro_subsystem_info *)data;
			if (!info || !info[0].desc || !info[0].ident || !info[0].roms
				|| info[0].id != 1
				|| strcmp(info[0].ident, "cart_disk")
				|| info[0].num_roms != 2
				|| !info[0].roms[0].required
				|| !info[0].roms[1].required
				|| !strstr(info[0].roms[0].valid_extensions, "car")
				|| !strstr(info[0].roms[0].valid_extensions, "xex")
				|| !strstr(info[0].roms[1].valid_extensions, "atr")
				|| !strstr(info[0].roms[1].valid_extensions, "m3u"))
			{
				return false;
			}

			++g_subsystemsRegistered;
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
		case 36: {	// SET_MEMORY_MAPS
			auto *map = (retro_memory_map *)data;
			if (!map || !map->descriptors || map->num_descriptors != 1)
				return false;

			const retro_memory_descriptor& desc = map->descriptors[0];
			if (!desc.ptr
				|| desc.offset != 0
				|| desc.start != 0
				|| desc.select != 0xFFFF
				|| desc.disconnect != 0
				|| desc.len != 0x10000)
			{
				return false;
			}

			++g_memoryMapsRegistered;
			return true;
		}
		case 37: {	// SET_GEOMETRY
			auto *geometry = (retro_game_geometry *)data;
			if (geometry) {
				g_lastGeometryW = geometry->base_width;
				g_lastGeometryH = geometry->base_height;
				g_lastGeometryAspect = geometry->aspect_ratio;
			}
			++g_geometryUpdates;
			return true;
		}
		case 42: {	// SET_SUPPORT_ACHIEVEMENTS
			auto *enabled = (bool *)data;
			if (!enabled || !*enabled)
				return false;

			++g_achievementsEnabled;
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

static void video_cb(const void *data, unsigned w, unsigned h, size_t pitch) {
	++g_videoCalls;

	if (data && w && h) {
		++g_nonNullFrames;
		g_lastW = w;
		g_lastH = h;
		if (g_maxObservedW < w)
			g_maxObservedW = w;
		if (g_maxObservedH < h)
			g_maxObservedH = h;

		unsigned selectedPixels = 0;
		const auto *src = (const uint8_t *)data;
		for (unsigned y = 0; y < h; ++y) {
			const auto *row = (const uint32_t *)(src + (size_t)y * pitch);
			for (unsigned x = 0; x < w; ++x) {
				if (row[x] == 0xFFE0A028)
					++selectedPixels;
			}
		}

		if (selectedPixels > 20)
			++g_vkbdOverlayFrames;
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

	if (device == 5 && index == 0 && id == 1) {	// RETRO_DEVICE_ANALOG, left Y
		++g_analogJoystickYPolls;
		return -20000;
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
		if ((g_joypadMask & (uint16_t)((1U << 1) | (1U << 4)))
			== (uint16_t)((1U << 1) | (1U << 4)))
		{
			++g_concurrentJoypadMaskPolls;
		}
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

static bool load_core_game_special(
	bool (*retro_load_game_special)(unsigned, const retro_game_info *, size_t),
	unsigned gameType, const retro_game_info *info, size_t numInfo)
{
	CallbackPhaseScope phase(CallbackPhase::LoadGame);
	return retro_load_game_special(gameType, info, numInfo);
}

static bool write_smoke_atr(const char *path) {
	FILE *f = fopen(path, "wb");
	if (!f) {
		perror(path);
		return false;
	}

	static constexpr size_t kSectorCount = 720;
	static constexpr size_t kSectorSize = 128;
	static constexpr size_t kImageBytes = kSectorCount * kSectorSize;
	static constexpr unsigned kParagraphs = (unsigned)(kImageBytes / 16);

	uint8_t header[16] {};
	header[0] = 0x96;
	header[1] = 0x02;
	header[2] = (uint8_t)kParagraphs;
	header[3] = (uint8_t)(kParagraphs >> 8);
	header[4] = (uint8_t)kSectorSize;
	header[5] = (uint8_t)(kSectorSize >> 8);

	bool ok = fwrite(header, 1, sizeof header, f) == sizeof header;
	uint8_t sector[kSectorSize] {};
	sector[0] = 0xA5;
	for(size_t i = 0; ok && i < kSectorCount; ++i) {
		sector[1] = (uint8_t)i;
		sector[2] = (uint8_t)(i >> 8);
		ok = fwrite(sector, 1, sizeof sector, f) == sizeof sector;
	}

	if (fclose(f))
		ok = false;
	return ok;
}

static bool write_smoke_disk_writer_xex(const char *path) {
	FILE *f = fopen(path, "wb");
	if (!f) {
		perror(path);
		return false;
	}

	static constexpr uint8_t kCode[] = {
		0xFF, 0xFF,
		0x00, 0x20, 0x21, 0x20,
		0xA2, 0x0B,			// LDX #11
		0xBD, 0x16, 0x20,	// LDA dcb,X
		0x9D, 0x00, 0x03,	// STA DDEVIC,X
		0xCA,				// DEX
		0x10, 0xF7,			// BPL copy
		0x20, 0x59, 0xE4,	// JSR SIOV
		0xA9, 0x42,			// LDA #$42
		0x8D, 0xFC, 0x02,	// STA CH
		0x4C, 0x13, 0x20,	// JMP *
		0x31,				// DDEVIC = D1:
		0x01,				// DUNIT
		0x50,				// DCOMND = put sector, no verify
		0x80,				// DSTATS = send data
		0x00, 0x21,			// DBUF = $2100
		0x07, 0x00,			// DTIMLO, unused
		0x80, 0x00,			// DBYT = 128
		0x0A, 0x00,			// DAUX = sector 10
	};
	static constexpr uint8_t kRunAd[] = {
		0xE0, 0x02, 0xE1, 0x02, 0x00, 0x20
	};

	uint8_t sector[128] {};
	const char marker[] = "ALTIRRA-LIBRETRO-SIDECAR-SMOKE";
	memcpy(sector, marker, sizeof marker - 1);
	for(size_t i = sizeof marker - 1; i < sizeof sector; ++i)
		sector[i] = (uint8_t)(0x40 + (i & 0x3F));

	static constexpr uint8_t kSectorHeader[] = {
		0x00, 0x21, 0x7F, 0x21
	};

	bool ok = fwrite(kCode, 1, sizeof kCode, f) == sizeof kCode
		&& fwrite(kSectorHeader, 1, sizeof kSectorHeader, f) == sizeof kSectorHeader
		&& fwrite(sector, 1, sizeof sector, f) == sizeof sector
		&& fwrite(kRunAd, 1, sizeof kRunAd, f) == sizeof kRunAd;

	if (fclose(f))
		ok = false;
	return ok;
}

static bool write_smoke_disk_reader_xex(const char *path) {
	FILE *f = fopen(path, "wb");
	if (!f) {
		perror(path);
		return false;
	}

	static constexpr uint16_t kLoadAddress = 0x2000;
	static constexpr uint16_t kBufferAddress = 0x2100;
	static constexpr uint16_t kChAddress = 0x02FC;
	const char marker[] = "ALTIRRA-LIBRETRO-SIDECAR-SMOKE";
	const uint8_t markerLen = (uint8_t)(sizeof marker - 1);
	std::vector<uint8_t> payload;

	auto emit8 = [&](uint8_t v) { payload.push_back(v); };
	auto emit16 = [&](uint16_t v) {
		payload.push_back((uint8_t)v);
		payload.push_back((uint8_t)(v >> 8));
	};
	auto pc = [&]() {
		return (uint16_t)(kLoadAddress + payload.size());
	};

	emit8(0xA2); emit8(0x0B);			// LDX #11
	const size_t dcbLoadAddrPatch = payload.size() + 1;
	emit8(0xBD); emit16(0);			// LDA dcb,X
	emit8(0x9D); emit16(0x0300);		// STA DDEVIC,X
	emit8(0xCA);					// DEX
	emit8(0x10); emit8(0xF7);			// BPL copy
	emit8(0x20); emit16(0xE459);		// JSR SIOV
	emit8(0xA2); emit8(0x00);			// LDX #0
	const uint16_t compareLoop = pc();
	const size_t markerLoadAddrPatch = payload.size() + 1;
	emit8(0xBD); emit16(0);			// LDA marker,X
	emit8(0xDD); emit16(kBufferAddress);	// CMP $2100,X
	const size_t failBranchPatch = payload.size() + 1;
	emit8(0xD0); emit8(0);			// BNE fail
	emit8(0xE8);					// INX
	emit8(0xE0); emit8(markerLen);		// CPX #markerLen
	emit8(0xD0);
	emit8((uint8_t)((int)compareLoop - (int)(pc() + 1)));
	emit8(0xA9); emit8(0x43);			// LDA #$43 (success)
	emit8(0x8D); emit16(kChAddress);		// STA CH
	const size_t loopJumpPatch = payload.size() + 1;
	emit8(0x4C); emit16(0);			// JMP done
	const uint16_t failLabel = pc();
	emit8(0xA9); emit8(0x44);			// LDA #$44 (failure)
	emit8(0x8D); emit16(kChAddress);		// STA CH
	const uint16_t doneLabel = pc();
	emit8(0x4C); emit16(doneLabel);		// JMP done

	payload[failBranchPatch] = (uint8_t)((int)failLabel
		- (int)(kLoadAddress + failBranchPatch + 1));
	payload[loopJumpPatch] = (uint8_t)doneLabel;
	payload[loopJumpPatch + 1] = (uint8_t)(doneLabel >> 8);

	const uint16_t dcbAddress = pc();
	static constexpr uint8_t kDcb[] = {
		0x31,				// DDEVIC = D1:
		0x01,				// DUNIT
		0x52,				// DCOMND = read sector
		0x40,				// DSTATS = receive data
		(uint8_t)kBufferAddress,
		(uint8_t)(kBufferAddress >> 8),
		0x07, 0x00,			// DTIMLO, unused
		0x80, 0x00,			// DBYT = 128
		0x0A, 0x00,			// DAUX = sector 10
	};
	payload.insert(payload.end(), kDcb, kDcb + sizeof kDcb);

	const uint16_t markerAddress = pc();
	payload.insert(payload.end(), marker, marker + markerLen);
	payload[dcbLoadAddrPatch] = (uint8_t)dcbAddress;
	payload[dcbLoadAddrPatch + 1] = (uint8_t)(dcbAddress >> 8);
	payload[markerLoadAddrPatch] = (uint8_t)markerAddress;
	payload[markerLoadAddrPatch + 1] = (uint8_t)(markerAddress >> 8);

	const uint16_t endAddress =
		(uint16_t)(kLoadAddress + payload.size() - 1);
	const uint8_t header[] = {
		0xFF, 0xFF,
		(uint8_t)kLoadAddress,
		(uint8_t)(kLoadAddress >> 8),
		(uint8_t)endAddress,
		(uint8_t)(endAddress >> 8),
	};
	const uint8_t runAd[] = {
		0xE0, 0x02, 0xE1, 0x02,
		(uint8_t)kLoadAddress,
		(uint8_t)(kLoadAddress >> 8),
	};

	const bool ok = fwrite(header, 1, sizeof header, f) == sizeof header
		&& fwrite(payload.data(), 1, payload.size(), f) == payload.size()
		&& fwrite(runAd, 1, sizeof runAd, f) == sizeof runAd;

	if (fclose(f))
		return false;
	return ok;
}

static bool write_smoke_a52(const char *path) {
	FILE *f = fopen(path, "wb");
	if (!f) {
		perror(path);
		return false;
	}

	uint8_t rom[8192] {};
	for(size_t i = 0; i < sizeof rom; ++i)
		rom[i] = (uint8_t)(0x80 + (i & 0x7F));

	uint32_t checksum = 0;
	for(uint8_t v : rom)
		checksum += v;

	uint8_t header[16] = { 'C', 'A', 'R', 'T' };
	header[7] = 19;		// 5200 8K mapper in the CART format.
	header[8] = (uint8_t)(checksum >> 24);
	header[9] = (uint8_t)(checksum >> 16);
	header[10] = (uint8_t)(checksum >> 8);
	header[11] = (uint8_t)checksum;

	const bool ok = fwrite(header, 1, sizeof header, f) == sizeof header
		&& fwrite(rom, 1, sizeof rom, f) == sizeof rom;
	return fclose(f) == 0 && ok;
}

static uint64_t hash_path_for_save(const char *path) {
	uint64_t h = 1469598103934665603ULL;
	for(const unsigned char *s = (const unsigned char *)path; *s; ++s) {
		unsigned char c = *s;
		if (c == '\\')
			c = '/';
		if (c >= 'A' && c <= 'Z')
			c = (unsigned char)(c - 'A' + 'a');

		h ^= c;
		h *= 1099511628211ULL;
	}
	return h;
}

static std::string save_name_from_path(const char *path) {
	const char *leaf = strrchr(path, '/');
	leaf = leaf ? leaf + 1 : path;

	std::string name = leaf;
	for(char& c : name) {
		const unsigned char ch = (unsigned char)c;
		if (!(ch >= 'a' && ch <= 'z')
			&& !(ch >= 'A' && ch <= 'Z')
			&& !(ch >= '0' && ch <= '9')
			&& c != '.'
			&& c != '-'
			&& c != '_')
		{
			c = '_';
		}
	}

	if (name.empty())
		name = "disk";

	const size_t dot = name.find_last_of('.');
	if (dot != std::string::npos)
		name.erase(dot);
	return name;
}

static std::string make_disk_sidecar_path(const char *sourcePath) {
	char hash[32] {};
	snprintf(hash, sizeof hash, "-%016llx",
		(unsigned long long)hash_path_for_save(sourcePath));

	std::string path = g_saveDir;
	path += "/Altirra/saves/";
	path += save_name_from_path(sourcePath);
	path += hash;
	path += ".atr";
	return path;
}

static bool read_atr_sector(const char *path, unsigned sector,
	uint8_t *data, size_t len)
{
	if (!sector || len != 128)
		return false;

	FILE *f = fopen(path, "rb");
	if (!f)
		return false;

	const long offset = 16 + (long)(sector - 1) * 128;
	const bool ok = fseek(f, offset, SEEK_SET) == 0
		&& fread(data, 1, len, f) == len;
	fclose(f);
	return ok;
}

static void run_core_frame(void (*retro_run)()) {
	CallbackPhaseScope phase(CallbackPhase::Run);
	retro_run();
}

static bool verify_inactive_state(void (*retro_run)(),
	void (*retro_reset)(),
	size_t (*retro_serialize_size)(),
	void *(*retro_get_memory_data)(unsigned),
	const char *label)
{
	const unsigned framesBefore = g_nonNullFrames;

	retro_reset();
	g_variablesUpdated = true;
	run_core_frame(retro_run);

	if (retro_serialize_size() != 0
		|| retro_get_memory_data(2)
		|| (g_diskControl.get_num_images && g_diskControl.get_num_images() != 0)
		|| g_nonNullFrames != framesBefore)
	{
		fprintf(stderr,
			"%s left stale state: state_size=%zu ram=%p disks=%u frames=%u->%u\n",
			label,
			retro_serialize_size(),
			retro_get_memory_data(2),
			g_diskControl.get_num_images ? g_diskControl.get_num_images() : 0,
			framesBefore,
			g_nonNullFrames);
		return false;
	}

	return true;
}

static bool verify_reset_survival(void (*retro_run)(),
	size_t (*retro_get_memory_size)(unsigned),
	void *(*retro_get_memory_data)(unsigned),
	const char *label)
{
	const unsigned framesBefore = g_nonNullFrames;
	for (int i = 0; i < 8; ++i)
		run_core_frame(retro_run);

	if (g_nonNullFrames <= framesBefore
		|| retro_get_memory_size(2) != 0x10000
		|| !retro_get_memory_data(2))
	{
		fprintf(stderr,
			"%s did not resume with video and system RAM available\n",
			label);
		return false;
	}

	return true;
}

static bool prepare_reset_probe(void (*retro_run)(),
	void (*retro_cheat_reset)(),
	void (*retro_cheat_set)(unsigned, bool, const char *),
	void *(*retro_get_memory_data)(unsigned),
	const char *label)
{
	static constexpr unsigned kProbeBase = 0x2000;

	retro_cheat_reset();
	for (unsigned i = 0; i < 8; ++i) {
		char code[32];
		snprintf(code, sizeof code, "POKE %u,%u", kProbeBase + i,
			0xA0 + i);
		retro_cheat_set(i, true, code);
	}
	retro_cheat_set(8, true, "POKE 8,0");
	retro_cheat_set(9, true, "POKE 580,0");
	run_core_frame(retro_run);
	retro_cheat_reset();

	auto *ram = (uint8_t *)retro_get_memory_data(2);
	if (!ram) {
		fprintf(stderr, "%s reset probe could not read system RAM\n", label);
		return false;
	}

	for (unsigned i = 0; i < 8; ++i) {
		if (ram[kProbeBase + i] != (uint8_t)(0xA0 + i)) {
			fprintf(stderr,
				"%s reset probe setup failed at %u: %u\n",
				label, kProbeBase + i,
				(unsigned)ram[kProbeBase + i]);
			return false;
		}
	}

	if (ram[8] != 0) {
		fprintf(stderr, "%s reset probe WARMST setup failed: %u\n",
			label, (unsigned)ram[8]);
		return false;
	}

	if (ram[580] != 0) {
		fprintf(stderr, "%s reset probe COLDST setup failed: %u\n",
			label, (unsigned)ram[580]);
		return false;
	}

	return true;
}

static bool verify_reset_effect(void (*retro_run)(),
	void *(*retro_get_memory_data)(unsigned),
	const char *label, bool cold)
{
	static constexpr unsigned kProbeBase = 0x2000;

	if (!verify_reset_survival(retro_run, [](unsigned) -> size_t {
			return 0x10000;
		}, retro_get_memory_data, label))
	{
		return false;
	}

	auto *ram = (uint8_t *)retro_get_memory_data(2);
	if (!ram) {
		fprintf(stderr, "%s reset effect could not read system RAM\n", label);
		return false;
	}

	unsigned preserved = 0;
	for (unsigned i = 0; i < 8; ++i) {
		if (ram[kProbeBase + i] == (uint8_t)(0xA0 + i))
			++preserved;
	}

	if (cold) {
		if (preserved == 8) {
			fprintf(stderr,
				"%s did not cold-reset user RAM pattern\n", label);
			return false;
		}
	} else {
		if (preserved != 8) {
			fprintf(stderr,
				"%s did not preserve user RAM across warm reset (%u/8), WARMST=%u bytes=%02x %02x %02x %02x %02x %02x %02x %02x\n",
				label, preserved, (unsigned)ram[8],
				(unsigned)ram[kProbeBase + 0],
				(unsigned)ram[kProbeBase + 1],
				(unsigned)ram[kProbeBase + 2],
				(unsigned)ram[kProbeBase + 3],
				(unsigned)ram[kProbeBase + 4],
				(unsigned)ram[kProbeBase + 5],
				(unsigned)ram[kProbeBase + 6],
				(unsigned)ram[kProbeBase + 7]);
			return false;
		}
	}

	if (!cold && ram[8] != 0xFF) {
		fprintf(stderr, "%s did not set OS warm-start flag WARMST: %u\n",
			label, (unsigned)ram[8]);
		return false;
	}

	return true;
}

static bool verify_concurrent_joystick_key(void (*retro_run)(),
	void (*retro_cheat_reset)(),
	void (*retro_cheat_set)(unsigned, bool, const char *),
	void *(*retro_get_memory_data)(unsigned))
{
	// Atari OS RAM shadow: CH=$02FC. The joypad mask counter proves that
	// the core consumed the D-pad direction in the same held interval.
	retro_cheat_reset();
	retro_cheat_set(0, true, "POKE 764,255");
	run_core_frame(retro_run);
	retro_cheat_reset();

	g_joypadMask = 0;
	for (int i = 0; i < 4; ++i)
		run_core_frame(retro_run);

	auto *ram = (uint8_t *)retro_get_memory_data(2);
	if (!ram) {
		fprintf(stderr, "system RAM unavailable for concurrent input test\n");
		return false;
	}

	const uint8_t idleCh = ram[764];
	const unsigned concurrentPollsBefore = g_concurrentJoypadMaskPolls;
	g_joypadMask = (uint16_t)((1U << 1) | (1U << 4));	// Y + Up
	for (int i = 0; i < 8; ++i)
		run_core_frame(retro_run);

	ram = (uint8_t *)retro_get_memory_data(2);
	const uint8_t activeCh = ram ? ram[764] : idleCh;
	const unsigned concurrentPollsAfter = g_concurrentJoypadMaskPolls;
	g_joypadMask = 0;
	for (int i = 0; i < 4; ++i)
		run_core_frame(retro_run);

	if (!ram
		|| concurrentPollsAfter <= concurrentPollsBefore
		|| activeCh == idleCh)
	{
		fprintf(stderr,
			"concurrent joystick+key was not observed: mask_polls %u->%u CH %u->%u\n",
			concurrentPollsBefore, concurrentPollsAfter,
			(unsigned)idleCh, (unsigned)activeCh);
		return false;
	}

	return true;
}

static bool verify_vkbd_key_injection(void (*retro_run)(),
	void (*retro_cheat_reset)(),
	void (*retro_cheat_set)(unsigned, bool, const char *),
	void *(*retro_get_memory_data)(unsigned))
{
	// The VKBD opens with "1" selected. Pressing B must inject that key
	// through the normal keyboard path, visible in Atari OS CH=$02FC.
	retro_cheat_reset();
	retro_cheat_set(0, true, "POKE 764,255");
	run_core_frame(retro_run);
	retro_cheat_reset();

	auto *ram = (uint8_t *)retro_get_memory_data(2);
	if (!ram) {
		fprintf(stderr, "system RAM unavailable for VKBD key test\n");
		return false;
	}

	const uint8_t idleCh = ram[764];
	g_joypadMask = (uint16_t)(1U << 0);	// RETRO_DEVICE_ID_JOYPAD_B
	run_core_frame(retro_run);
	g_joypadMask = 0;
	for (int i = 0; i < 4; ++i)
		run_core_frame(retro_run);

	ram = (uint8_t *)retro_get_memory_data(2);
	const uint8_t activeCh = ram ? ram[764] : idleCh;
	if (!ram || activeCh == idleCh) {
		fprintf(stderr,
			"VKBD key was not visible in OS RAM: CH %u->%u\n",
			(unsigned)idleCh, (unsigned)activeCh);
		return false;
	}

	return true;
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
	if (argc < 2 || argc > 4) {
		fprintf(stderr, "usage: %s CORE [core-options-version] [core-info]\n",
			argv[0]);
		return 2;
	}

	if (argc == 3)
		g_coreOptionsVersion = (unsigned)atoi(argv[2]);
	else if (argc == 4)
		g_coreOptionsVersion = (unsigned)atoi(argv[2]);

	std::map<std::string, std::string> coreInfo;
	if (argc == 4 && !load_core_info_file(argv[3], coreInfo))
		return 1;

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
	auto retro_load_game_special = sym<bool (*)(unsigned, const retro_game_info *, size_t)>(lib, "retro_load_game_special");
	auto retro_unload_game = sym<void (*)()>(lib, "retro_unload_game");
	auto retro_run = sym<void (*)()>(lib, "retro_run");
	auto retro_reset = sym<void (*)()>(lib, "retro_reset");
	auto retro_serialize_size = sym<size_t (*)()>(lib, "retro_serialize_size");
	auto retro_serialize = sym<bool (*)(void *, size_t)>(lib, "retro_serialize");
	auto retro_unserialize = sym<bool (*)(const void *, size_t)>(lib, "retro_unserialize");
	auto retro_cheat_reset = sym<void (*)()>(lib, "retro_cheat_reset");
	auto retro_cheat_set = sym<void (*)(unsigned, bool, const char *)>(lib, "retro_cheat_set");
	auto retro_get_memory_data = sym<void *(*)(unsigned)>(lib, "retro_get_memory_data");
	auto retro_get_memory_size = sym<size_t (*)(unsigned)>(lib, "retro_get_memory_size");

	if (!retro_set_environment || !retro_set_video_refresh || !retro_set_audio_sample
		|| !retro_set_audio_sample_batch || !retro_set_input_poll || !retro_set_input_state
		|| !retro_set_controller_port_device
		|| !retro_init || !retro_deinit || !retro_get_system_info || !retro_get_system_av_info
		|| !retro_load_game || !retro_load_game_special
		|| !retro_unload_game || !retro_run || !retro_reset
		|| !retro_serialize_size || !retro_serialize || !retro_unserialize
		|| !retro_cheat_reset || !retro_cheat_set
		|| !retro_get_memory_data || !retro_get_memory_size)
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
	if (!validate_system_info(si, coreInfo)) {
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

	g_aspectValue = "square_pixels";
	g_variablesUpdated = true;
	for (int i = 0; i < 3; ++i)
		run_core_frame(retro_run);

	const float expectedSquareAspect = (float)normalW / (float)normalH;
	const float aspectDiff = g_lastGeometryAspect > expectedSquareAspect
		? g_lastGeometryAspect - expectedSquareAspect
		: expectedSquareAspect - g_lastGeometryAspect;
	if (aspectDiff > 0.0001f) {
		fprintf(stderr,
			"square-pixel aspect was not reported: got %.6f expected %.6f\n",
			g_lastGeometryAspect, expectedSquareAspect);
		retro_unload_game();
		retro_deinit();
		return 1;
	}

	if (retro_get_memory_size(2) != 0x10000) {
		fprintf(stderr, "unexpected system RAM size: %zu\n",
			retro_get_memory_size(2));
		retro_unload_game();
		retro_deinit();
		return 1;
	}

	auto *ram = (uint8_t *)retro_get_memory_data(2);
	if (!ram) {
		fprintf(stderr, "system RAM pointer was null\n");
		retro_unload_game();
		retro_deinit();
		return 1;
	}

	retro_cheat_reset();
	retro_cheat_set(0, true, "POKE 1536,123");
	run_core_frame(retro_run);
	ram = (uint8_t *)retro_get_memory_data(2);
	if (!ram || ram[1536] != 123) {
		fprintf(stderr, "POKE cheat not visible in system RAM: %u\n",
			ram ? (unsigned)ram[1536] : 0);
		retro_unload_game();
		retro_deinit();
		return 1;
	}
	retro_cheat_reset();

	if (!prepare_reset_probe(retro_run, retro_cheat_reset, retro_cheat_set,
			retro_get_memory_data, "retro_reset"))
	{
		retro_unload_game();
		retro_deinit();
		return 1;
	}

	retro_reset();
	if (!verify_reset_effect(retro_run, retro_get_memory_data,
			"retro_reset", true))
	{
		retro_unload_game();
		retro_deinit();
		return 1;
	}

	retro_set_controller_port_device(0, 1);	// RETRO_DEVICE_JOYPAD
	g_systemValue = "800xl";
	g_inputPort1Value = "joystick";
	g_controlSchemeValue = "auto";
	g_variablesUpdated = true;
	for (int i = 0; i < 8; ++i)
		run_core_frame(retro_run);

	if (!prepare_reset_probe(retro_run, retro_cheat_reset, retro_cheat_set,
			retro_get_memory_data, "Select+Start warm reset"))
	{
		retro_unload_game();
		retro_deinit();
		return 1;
	}

	g_joypadMask = (uint16_t)((1U << 2) | (1U << 3));	// Select+Start
	run_core_frame(retro_run);
	g_joypadMask = 0;
	if (!verify_reset_effect(retro_run, retro_get_memory_data,
			"Select+Start warm reset", false))
	{
		retro_unload_game();
		retro_deinit();
		return 1;
	}

	if (!prepare_reset_probe(retro_run, retro_cheat_reset, retro_cheat_set,
			retro_get_memory_data, "Select+L cold reset"))
	{
		retro_unload_game();
		retro_deinit();
		return 1;
	}

	g_joypadMask = (uint16_t)((1U << 2) | (1U << 10));	// Select+L
	run_core_frame(retro_run);
	g_joypadMask = 0;
	if (!verify_reset_effect(retro_run, retro_get_memory_data,
			"Select+L cold reset", true))
	{
		retro_unload_game();
		retro_deinit();
		return 1;
	}

	const unsigned vkbdOverlayFramesBefore = g_vkbdOverlayFrames;
	g_joypadMask = (uint16_t)(1U << 11);	// RETRO_DEVICE_ID_JOYPAD_R
	run_core_frame(retro_run);
	g_joypadMask = 0;
	for (int i = 0; i < 3; ++i)
		run_core_frame(retro_run);

	if (g_vkbdOverlayFrames == vkbdOverlayFramesBefore) {
		fprintf(stderr, "virtual keyboard overlay did not appear after R\n");
		retro_unload_game();
		retro_deinit();
		return 1;
	}

	if (!verify_vkbd_key_injection(retro_run, retro_cheat_reset,
			retro_cheat_set, retro_get_memory_data))
	{
		retro_unload_game();
		retro_deinit();
		return 1;
	}

	g_joypadMask = (uint16_t)(1U << 8);	// RETRO_DEVICE_ID_JOYPAD_A / close
	run_core_frame(retro_run);
	g_joypadMask = 0;
	for (int i = 0; i < 2; ++i)
		run_core_frame(retro_run);

	const unsigned descriptorsBeforeVkbdRebind = g_inputDescriptorsValidated;
	g_vkbdToggleValue = "l3";
	g_variablesUpdated = true;
	for (int i = 0; i < 3; ++i)
		run_core_frame(retro_run);

	if (g_inputDescriptorsValidated <= descriptorsBeforeVkbdRebind) {
		fprintf(stderr,
			"input descriptors were not refreshed after VKBD toggle rebind\n");
		retro_unload_game();
		retro_deinit();
		return 1;
	}

	g_vkbdToggleValue = "r_l3_select_r2";
	g_variablesUpdated = true;
	for (int i = 0; i < 3; ++i)
		run_core_frame(retro_run);

	const unsigned descriptorsBeforeInvalidOptions = g_inputDescriptorsValidated;
	g_controlSchemeValue = "stale_scheme";
	g_vkbdToggleValue = "stale_toggle";
	g_padYKeyValue = "stale_key";
	g_variablesUpdated = true;
	for (int i = 0; i < 3; ++i)
		run_core_frame(retro_run);

	if (g_inputDescriptorsValidated <= descriptorsBeforeInvalidOptions) {
		fprintf(stderr,
			"input descriptors were not refreshed after invalid option values\n");
		retro_unload_game();
		retro_deinit();
		return 1;
	}

	g_controlSchemeValue = "auto";
	g_vkbdToggleValue = "r_l3_select_r2";
	g_padYKeyValue = "auto";
	g_variablesUpdated = true;
	for (int i = 0; i < 3; ++i)
		run_core_frame(retro_run);

	const unsigned descriptorsBeforeSchemeChange = g_inputDescriptorsValidated;
	g_controlSchemeValue = "flight";
	g_variablesUpdated = true;
	for (int i = 0; i < 3; ++i)
		run_core_frame(retro_run);

	if (g_inputDescriptorsValidated <= descriptorsBeforeSchemeChange) {
		fprintf(stderr,
			"input descriptors were not refreshed after control scheme change\n");
		retro_unload_game();
		retro_deinit();
		return 1;
	}

	const unsigned descriptorsBefore5200Scheme = g_inputDescriptorsValidated;
	g_controlSchemeValue = "auto";
	g_systemValue = "5200";
	g_variablesUpdated = true;
	for (int i = 0; i < 3; ++i)
		run_core_frame(retro_run);

	if (g_inputDescriptorsValidated <= descriptorsBefore5200Scheme) {
		fprintf(stderr,
			"input descriptors were not refreshed after auto 5200 scheme change\n");
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
	g_systemValue = "auto";
	g_controlSchemeValue = "auto";
	g_variablesUpdated = true;
	for (int i = 0; i < 5; ++i)
		run_core_frame(retro_run);

	retro_set_controller_port_device(0, 1);	// RETRO_DEVICE_JOYPAD
	g_padYKeyValue = "t";
	g_variablesUpdated = true;
	for (int i = 0; i < 3; ++i)
		run_core_frame(retro_run);

	if (!verify_concurrent_joystick_key(retro_run, retro_cheat_reset,
			retro_cheat_set, retro_get_memory_data))
	{
		retro_unload_game();
		retro_deinit();
		return 1;
	}
	g_padYKeyValue = "auto";
	g_variablesUpdated = true;
	for (int i = 0; i < 3; ++i)
		run_core_frame(retro_run);

	g_joypadMask = (uint16_t)((1U << 0) | (1U << 3) | (1U << 7));
	for (int i = 0; i < 30; ++i)
		run_core_frame(retro_run);

	g_joypadMask = 0;
	for (int i = 0; i < 60; ++i)
		run_core_frame(retro_run);

	const unsigned descriptorsBeforeControllerDevice = g_inputDescriptorsValidated;
	retro_set_controller_port_device(0, 2);	// RETRO_DEVICE_MOUSE / ST mouse
	if (g_inputDescriptorsValidated <= descriptorsBeforeControllerDevice) {
		fprintf(stderr,
			"input descriptors were not refreshed after controller device change\n");
		retro_unload_game();
		retro_deinit();
		return 1;
	}

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

	retro_game_info invalidDiskInfo {};
	invalidDiskInfo.path = "/tmp/altirra-libretro-not-a-disk.xex";
	char rejectedDiskPath[8] = { 'x' };
	if (g_diskControl.replace_image_index(0, &invalidDiskInfo)
		|| !g_diskControl.get_image_path(0, rejectedDiskPath,
			sizeof rejectedDiskPath)
		|| *rejectedDiskPath)
	{
		fprintf(stderr, "non-disk disk-control replacement was accepted\n");
		retro_unload_game();
		retro_deinit();
		return 1;
	}

	char m3uDiskPath0[320];
	char m3uDiskPath1[320];
	char m3uPath[320];
	snprintf(m3uDiskPath0, sizeof m3uDiskPath0,
		"%s/altirra-libretro-m3u-0.atr", g_saveDir);
	snprintf(m3uDiskPath1, sizeof m3uDiskPath1,
		"%s/altirra-libretro-m3u-1.atr", g_saveDir);
	snprintf(m3uPath, sizeof m3uPath,
		"%s/altirra-libretro-disks.m3u", g_saveDir);
	if (!write_smoke_atr(m3uDiskPath0) || !write_smoke_atr(m3uDiskPath1)) {
		fprintf(stderr, "failed to create disk-control M3U ATR fixtures\n");
		retro_unload_game();
		retro_deinit();
		return 1;
	}

	FILE *m3u = fopen(m3uPath, "wb");
	bool m3uWritten = false;
	if (m3u) {
		m3uWritten =
			fprintf(m3u, "%s\n%s\n", m3uDiskPath0, m3uDiskPath1) >= 0;
		if (fclose(m3u) != 0)
			m3uWritten = false;
	}

	if (!m3uWritten) {
		fprintf(stderr, "failed to create disk-control M3U fixture\n");
		retro_unload_game();
		retro_deinit();
		return 1;
	}

	retro_game_info m3uInfo {};
	m3uInfo.path = m3uPath;
	char m3uDiskPathCheck0[320] {};
	char m3uDiskPathCheck1[320] {};
	if (!g_diskControl.replace_image_index(0, &m3uInfo)
		|| g_diskControl.get_num_images() != 2
		|| !g_diskControl.get_image_path(0, m3uDiskPathCheck0,
			sizeof m3uDiskPathCheck0)
		|| !g_diskControl.get_image_path(1, m3uDiskPathCheck1,
			sizeof m3uDiskPathCheck1)
		|| strcmp(m3uDiskPathCheck0, m3uDiskPath0)
		|| strcmp(m3uDiskPathCheck1, m3uDiskPath1))
	{
		fprintf(stderr, "disk-control M3U replacement did not expand\n");
		retro_unload_game();
		retro_deinit();
		return 1;
	}

	if (!g_diskControl.replace_image_index(1, nullptr)
		|| g_diskControl.get_num_images() != 1)
	{
		fprintf(stderr, "disk-control M3U cleanup failed\n");
		retro_unload_game();
		retro_deinit();
		return 1;
	}

	retro_game_info diskInfo {};
	char diskFixturePath[320];
	snprintf(diskFixturePath, sizeof diskFixturePath,
		"%s/altirra-libretro-smoke-disk.atr", g_saveDir);
	if (!write_smoke_atr(diskFixturePath)) {
		fprintf(stderr, "failed to create smoke ATR fixture\n");
		retro_unload_game();
		retro_deinit();
		return 1;
	}

	diskInfo.path = diskFixturePath;
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

	if (!g_diskControl.set_eject_state(false)
		|| g_diskControl.get_eject_state())
	{
		fprintf(stderr, "disk image mount failed\n");
		retro_unload_game();
		retro_deinit();
		return 1;
	}

	if (g_diskControl.add_image_index()
		|| g_diskControl.get_num_images() != 1)
	{
		fprintf(stderr, "disk image add succeeded while tray was closed\n");
		retro_unload_game();
		retro_deinit();
		return 1;
	}

	for (int i = 0; i < 3; ++i)
		run_core_frame(retro_run);

	if (!g_diskControl.set_eject_state(true) || !g_diskControl.get_eject_state()) {
		fprintf(stderr, "mounted disk image eject failed\n");
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

	uint8_t *badState = (uint8_t *)malloc(stateSize);
	if (!badState) {
		fprintf(stderr, "malloc(%zu) for bad state failed\n", stateSize);
		free(state);
		retro_unload_game();
		retro_deinit();
		return 1;
	}
	memcpy(badState, state, stateSize);
	if (stateSize > 24)
		badState[24] ^= 0x5A;

	if (retro_unserialize(badState, stateSize)) {
		fprintf(stderr, "retro_unserialize accepted a corrupted state\n");
		free(badState);
		free(state);
		retro_unload_game();
		retro_deinit();
		return 1;
	}
	free(badState);

	for (int i = 0; i < 3; ++i)
		run_core_frame(retro_run);

	if (retro_serialize_size() != stateSize
		|| !retro_get_memory_data(2))
	{
		fprintf(stderr,
			"corrupted state rejection damaged active session: state_size=%zu\n",
			retro_serialize_size());
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

	retro_unload_game();
	retro_cheat_set(0, true, "POKE 1536,45");
	run_core_frame(retro_run);
	if (retro_get_memory_data(2)) {
		fprintf(stderr,
			"system RAM pointer remained visible after unload/cheat\n");
		retro_cheat_reset();
		retro_deinit();
		return 1;
	}
	if (!verify_inactive_state(retro_run, retro_reset, retro_serialize_size,
			retro_get_memory_data, "post-unload reset/option update"))
	{
		retro_cheat_reset();
		retro_deinit();
		return 1;
	}
	retro_cheat_reset();

	char writeDiskPath[320];
	snprintf(writeDiskPath, sizeof writeDiskPath,
		"%s/altirra-libretro-saveback.atr", g_saveDir);
	char writerXexPath[320];
	snprintf(writerXexPath, sizeof writerXexPath,
		"%s/altirra-libretro-saveback.xex", g_saveDir);
	char readerXexPath[320];
	snprintf(readerXexPath, sizeof readerXexPath,
		"%s/altirra-libretro-saveback-reader.xex", g_saveDir);

	if (!write_smoke_atr(writeDiskPath)
		|| !write_smoke_disk_writer_xex(writerXexPath)
		|| !write_smoke_disk_reader_xex(readerXexPath))
	{
		fprintf(stderr, "failed to create disk save-back fixtures\n");
		retro_deinit();
		return 1;
	}

	retro_game_info savebackInfo[2] {};
	savebackInfo[0].path = writerXexPath;
	savebackInfo[1].path = writeDiskPath;
	g_systemValue = "auto";
	g_controlSchemeValue = "auto";
	g_variablesUpdated = true;
	if (!load_core_game_special(retro_load_game_special, 1,
			savebackInfo, 2))
	{
		fprintf(stderr, "retro_load_game_special(saveback) failed\n");
		retro_deinit();
		return 1;
	}

	bool writerRan = false;
	for (int i = 0; i < 600; ++i) {
		run_core_frame(retro_run);
		auto *ram = (uint8_t *)retro_get_memory_data(2);
		if (ram && ram[764] == 0x42) {
			writerRan = true;
			break;
		}
	}

	if (!writerRan) {
		fprintf(stderr, "disk save-back writer did not run\n");
		retro_unload_game();
		retro_deinit();
		return 1;
	}

	retro_unload_game();

	const std::string sidecarPath = make_disk_sidecar_path(writeDiskPath);
	const char sectorMarker[] = "ALTIRRA-LIBRETRO-SIDECAR-SMOKE";
	uint8_t sidecarSector[128] {};
	if (!read_atr_sector(sidecarPath.c_str(), 10, sidecarSector,
			sizeof sidecarSector)
		|| memcmp(sidecarSector, sectorMarker, strlen(sectorMarker)) != 0)
	{
		fprintf(stderr, "disk sidecar did not contain written sector: %s\n",
			sidecarPath.c_str());
		retro_deinit();
		return 1;
	}

	uint8_t originalSector[128] {};
	if (!read_atr_sector(writeDiskPath, 10, originalSector,
			sizeof originalSector)
		|| !memcmp(originalSector, sectorMarker, strlen(sectorMarker)))
	{
		fprintf(stderr, "safe sidecar mode modified the original ATR\n");
		retro_deinit();
		return 1;
	}

	retro_game_info savebackReadInfo[2] {};
	savebackReadInfo[0].path = readerXexPath;
	savebackReadInfo[1].path = writeDiskPath;
	g_systemValue = "auto";
	g_controlSchemeValue = "auto";
	g_variablesUpdated = true;
	if (!load_core_game_special(retro_load_game_special, 1,
			savebackReadInfo, 2))
	{
		fprintf(stderr, "retro_load_game_special(saveback reader) failed\n");
		retro_deinit();
		return 1;
	}

	bool sidecarReloaded = false;
	bool sidecarReloadFailed = false;
	for (int i = 0; i < 600; ++i) {
		run_core_frame(retro_run);
		auto *ram = (uint8_t *)retro_get_memory_data(2);
		if (ram && ram[764] == 0x43) {
			sidecarReloaded = true;
			break;
		}
		if (ram && ram[764] == 0x44) {
			sidecarReloadFailed = true;
			break;
		}
	}

	if (!sidecarReloaded) {
		fprintf(stderr,
			"disk sidecar was not reloaded on next launch%s\n",
			sidecarReloadFailed ? " (reader saw original disk data)" : "");
		retro_unload_game();
		retro_deinit();
		return 1;
	}

	retro_unload_game();

	char missingDiskPath[320];
	snprintf(missingDiskPath, sizeof missingDiskPath,
		"%s/altirra-libretro-missing.atr", g_saveDir);
	retro_game_info failedSpecialInfo[2] {};
	failedSpecialInfo[0].path = readerXexPath;
	failedSpecialInfo[1].path = missingDiskPath;
	if (load_core_game_special(retro_load_game_special, 1,
			failedSpecialInfo, 2))
	{
		fprintf(stderr, "retro_load_game_special unexpectedly accepted a missing disk\n");
		retro_unload_game();
		retro_deinit();
		return 1;
	}
	retro_cheat_set(0, true, "POKE 1536,46");
	run_core_frame(retro_run);

	if (retro_serialize_size() != 0
		|| retro_get_memory_data(2)
		|| (g_diskControl.get_num_images && g_diskControl.get_num_images() != 0))
	{
		fprintf(stderr,
			"failed special load left stale state: state_size=%zu ram=%p disks=%u\n",
			retro_serialize_size(),
			retro_get_memory_data(2),
			g_diskControl.get_num_images ? g_diskControl.get_num_images() : 0);
		retro_cheat_reset();
		retro_deinit();
		return 1;
	}
	if (!verify_inactive_state(retro_run, retro_reset, retro_serialize_size,
			retro_get_memory_data, "failed special load reset/option update"))
	{
		retro_cheat_reset();
		retro_deinit();
		return 1;
	}
	retro_cheat_reset();

	retro_game_info missingDiskInfo {};
	missingDiskInfo.path = missingDiskPath;
	if (load_core_game(retro_load_game, &missingDiskInfo)) {
		fprintf(stderr, "retro_load_game unexpectedly accepted a missing disk\n");
		retro_unload_game();
		retro_deinit();
		return 1;
	}
	retro_cheat_set(0, true, "POKE 1536,47");
	run_core_frame(retro_run);

	if (retro_serialize_size() != 0
		|| retro_get_memory_data(2)
		|| (g_diskControl.get_num_images && g_diskControl.get_num_images() != 0))
	{
		fprintf(stderr,
			"failed disk load left stale state: state_size=%zu ram=%p disks=%u\n",
			retro_serialize_size(),
			retro_get_memory_data(2),
			g_diskControl.get_num_images ? g_diskControl.get_num_images() : 0);
		retro_cheat_reset();
		retro_deinit();
		return 1;
	}
	if (!verify_inactive_state(retro_run, retro_reset, retro_serialize_size,
			retro_get_memory_data, "failed disk load reset/option update"))
	{
		retro_cheat_reset();
		retro_deinit();
		return 1;
	}
	retro_cheat_reset();

	char cart5200FixturePath[320];
	snprintf(cart5200FixturePath, sizeof cart5200FixturePath,
		"%s/altirra-libretro-smoke.a52", g_saveDir);
	if (!write_smoke_a52(cart5200FixturePath)) {
		fprintf(stderr, "failed to create smoke A52 fixture\n");
		retro_deinit();
		return 1;
	}

	retro_set_controller_port_device(0, 1);	// RETRO_DEVICE_JOYPAD
	g_systemValue = "auto";
	g_inputPort1Value = "auto";
	g_controlSchemeValue = "auto";
	g_expectContentAuto5200 = true;
	g_variablesUpdated = true;

	const unsigned descriptorsBeforeA52Load = g_inputDescriptorsValidated;
	retro_game_info cart5200Info {};
	cart5200Info.path = cart5200FixturePath;
	if (!load_core_game(retro_load_game, &cart5200Info)) {
		fprintf(stderr, "retro_load_game(.a52) failed\n");
		retro_deinit();
		return 1;
	}

	for (int i = 0; i < 8; ++i)
		run_core_frame(retro_run);

	if (g_inputDescriptorsValidated <= descriptorsBeforeA52Load
		|| !retro_get_memory_data(2))
	{
		fprintf(stderr,
			".a52 content-aware 5200 load did not refresh descriptors/RAM\n");
		retro_unload_game();
		retro_deinit();
		return 1;
	}

	printf("video_calls=%u non_null=%u last=%ux%u\n",
		g_videoCalls, g_nonNullFrames, g_lastW, g_lastH);
	printf("audio_callbacks=%u audio_frames=%zu\n",
		g_audioCallbacks, g_audioFrames);
	printf("state_size=%zu options_registered=%u av_fps=%.4f\n",
		stateSize, g_optionsRegistered, g_lastSystemAvFps);
	printf("option_keys_validated=%u hardware_categories=%u\n",
		g_optionKeysValidated, g_optionHardwareCategoryValidated);
	printf("disk_control_registered=%u\n", g_diskControlRegistered);
	printf("subsystems_registered=%u\n", g_subsystemsRegistered);
	printf("geometry_updates=%u last_geometry=%ux%u\n",
		g_geometryUpdates, g_lastGeometryW, g_lastGeometryH);
	printf("system_av_info_updates=%u wrong_phase=%u\n",
		g_systemAvInfoDuringRun, g_systemAvInfoWrongPhase);
	printf("geometry_matrix_checks=%u max_observed=%ux%u\n",
		g_geometryMatrixChecks, g_maxObservedW, g_maxObservedH);
	printf("controller_info_registered=%u\n", g_controllerInfoRegistered);
	printf("input_descriptors_validated=%u\n", g_inputDescriptorsValidated);
	printf("input_bitmask_queries=%u joypad_mask_polls=%u joypad_button_polls=%u\n",
		g_inputBitmaskQueries, g_joypadMaskPolls, g_joypadButtonPolls);
	printf("achievements_enabled=%u\n", g_achievementsEnabled);
	printf("memory_maps_registered=%u\n", g_memoryMapsRegistered);
	printf("port2_input_polls=%u\n", g_port2InputPolls);
	printf("analog_input_polls=%u\n", g_analogInputPolls);
	printf("analog_joystick_y_polls=%u\n", g_analogJoystickYPolls);
	printf("keyboard_input_polls=%u\n", g_keyboardInputPolls);
	printf("mouse_input_polls=%u\n", g_mouseInputPolls);
	printf("lightgun_input_polls=%u\n", g_lightGunInputPolls);
	printf("vkbd_overlay_frames=%u\n", g_vkbdOverlayFrames);

	retro_unload_game();
	retro_deinit();

	retro_system_info shutdownSi {};
	retro_get_system_info(&shutdownSi);
	if (!validate_system_info(shutdownSi, coreInfo)) {
		fprintf(stderr,
			"retro_get_system_info returned invalid metadata after shutdown\n");
		return 1;
	}

	return g_videoCalls >= 140 && g_nonNullFrames > 0 && g_lastW && g_lastH
		&& g_audioCallbacks > 0 && g_audioFrames > 0 && g_optionsRegistered > 0
		&& g_optionKeysValidated > 0
		&& (g_coreOptionsVersion < 2 || g_optionHardwareCategoryValidated > 0)
		&& g_diskControlRegistered > 0
		&& g_subsystemsRegistered > 0
		&& g_geometryUpdates > 0
		&& g_systemAvInfoDuringRun > 0
		&& g_systemAvInfoWrongPhase == 0
		&& g_geometryMatrixChecks == expectedGeometryChecks
		&& g_controllerInfoRegistered > 0
		&& g_inputDescriptorsValidated > 0
		&& g_inputBitmaskQueries > 0
		&& g_joypadMaskPolls > 0
		&& g_achievementsEnabled > 0
		&& g_memoryMapsRegistered > 0
		&& g_port2InputPolls > 0
		&& g_analogInputPolls > 0
		&& g_analogJoystickYPolls > 0
		&& g_keyboardInputPolls > 0
		&& g_mouseInputPolls > 0
		&& g_lightGunInputPolls > 0
		&& g_vkbdOverlayFrames > 0
		? 0 : 1;
}
