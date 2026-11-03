#include "openxr_internal.h"
#include "graphics/vr_settings.h"

#include "camera/camera.h"
#include "graphics/2d.h"
#include "globalincs/version.h"
#include "mod_table/mod_table.h"
#include "render/3d.h"
#include "starfield/starfield.h"
#include "hud/hudparse.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <SDL3/SDL_joystick.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_video.h>
#include "osapi/osapi.h"
#include <SDL3/SDL_timer.h>

std::unique_ptr<star[]> Stars_XRBuffer;

#ifdef FS_OPENXR

#define XR_MAKE_VERSION_SHORT(major, minor, patch) \
    ((((major) & 0x3ffU) << 20) | (((minor) & 0x3ffU) << 10) | ((patch) & 0x3ffU))

bool openxr_req = false;
bool openxr_initialized = false;
bool openxr_recieve = false;
static bool xr_has_touch_plus = false;

XrInstance xr_instance;
XrSystemId xr_system;
XrSession xr_session;
XrSpace xr_space;
XrTime xr_time;
XrDebugUtilsMessengerEXT xr_debugMessenger;
std::array<std::unique_ptr<XrSwapchainHandler>, 2> xr_swapchains;
std::array<XrView, 2> xr_views;
std::array<XrViewConfigurationView, 2> xr_configurationviews;
vec3d xr_offset = ZERO_VECTOR;
XrFrameState xr_state;
OpenXRFBStage xr_stage = OpenXRFBStage::NONE;
float xr_scale = 1.0f;

#if !defined(NDEBUG) && defined(FS_OPENXR_DEBUG)
static XrBool32 handleXRError(XrDebugUtilsMessageSeverityFlagsEXT severity, XrDebugUtilsMessageTypeFlagsEXT type, const XrDebugUtilsMessengerCallbackDataEXT* callbackData, void* /*userData*/) {
	SCP_string message;
	switch (type)
	{
	case XR_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT:
		message = "OpenXR: general\n%s";
		break;
	case XR_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT:
		message = "OpenXR: validation\n%s";
		break;
	case XR_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT:
		message = "OpenXR: performance\n%s";
		break;
	case XR_DEBUG_UTILS_MESSAGE_TYPE_CONFORMANCE_BIT_EXT:
		message = "OpenXR: conformance\n%s";
		break;
	}

	switch (severity)
	{
	case XR_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
	case XR_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
		Information(LOCATION, message.c_str(), callbackData->message);
		break;
	case XR_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
		Warning(LOCATION, message.c_str(), callbackData->message);
		break;
	case XR_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
		Error(LOCATION, message.c_str(), callbackData->message);
		break;
	}

	return XR_FALSE;
}
#endif

void openxr_prepare(float hudscale) {
	Hud_global_scale = hudscale;
	openxr_req = true;
}

static void openxr_check_api_layers() {
	uint32_t layerCount = 0;
	if (xrEnumerateApiLayerProperties(0, &layerCount, nullptr) != XR_SUCCESS || layerCount == 0)
		return;

	SCP_vector<XrApiLayerProperties> layers(layerCount, XrApiLayerProperties { XR_TYPE_API_LAYER_PROPERTIES, nullptr, "", 0, 0, "" });
	if (xrEnumerateApiLayerProperties(layerCount, &layerCount, layers.data()) != XR_SUCCESS)
		return;

	for (const auto& layer : layers) {
		mprintf(("OpenXR API layer: %s (version %u)\n", layer.layerName, layer.layerVersion));

		if (!strcmp(layer.layerName, "XR_APILAYER_NOVENDOR_toolkit")) {
			ReleaseWarning(LOCATION, "OpenXR Toolkit is installed and active. There are known compatibility issues with FSO that will likely crash the game.\nPlease disable or uninstall OpenXR Toolkit to use VR with FSO.");
		}
	}
}

static bool openxr_init_instance() {
	openxr_check_api_layers();

	auto extensions = gr_openxr_get_extensions();

	const gameversion::version& fso_version = gameversion::get_executable_version();
	const gameversion::version& mod_version(Mod_version);

#if !defined(NDEBUG) && defined(FS_OPENXR_DEBUG)
	extensions.emplace_back("XR_EXT_debug_utils");
#endif

	// Quest 3 controllers use the touch_controller_plus profile, which needs its extension enabled before bindings can be suggested
	{
		uint32_t extCount = 0;
		if (xrEnumerateInstanceExtensionProperties(nullptr, 0, &extCount, nullptr) == XR_SUCCESS) {
			SCP_vector<XrExtensionProperties> props(extCount, XrExtensionProperties{ XR_TYPE_EXTENSION_PROPERTIES, nullptr });
			if (xrEnumerateInstanceExtensionProperties(nullptr, extCount, &extCount, props.data()) == XR_SUCCESS) {
				for (const auto& prop : props) {
					if (strcmp(prop.extensionName, "XR_META_touch_controller_plus") == 0) {
						extensions.emplace_back("XR_META_touch_controller_plus");
						xr_has_touch_plus = true;
					}
				}
			}
		}
	}

	XrInstanceCreateInfo instanceCreateInfo {
		XR_TYPE_INSTANCE_CREATE_INFO,
		nullptr,
		0,
		XrApplicationInfo {
			"", //Modname
			//Mod version. Should not contain negative numbers or only 0's.
			XR_MAKE_VERSION_SHORT(mod_version.major < 1 ? 1U : static_cast<uint32_t>(mod_version.major),
				mod_version.minor < 0 ? 0U : static_cast<uint32_t>(mod_version.minor),
				mod_version.build < 0 ? 0U : static_cast<uint32_t>(mod_version.build)),
			"FreeSpace Open",
			XR_MAKE_VERSION_SHORT(static_cast<uint32_t>(fso_version.major), static_cast<uint32_t>(fso_version.minor), static_cast<uint32_t>(fso_version.build)),
			XR_CURRENT_API_VERSION
		},
		0,
		nullptr,
		static_cast<uint32_t>(extensions.size()),
		extensions.data()
	};

	strncpy_s(instanceCreateInfo.applicationInfo.applicationName, Mod_title.empty() ? "FreeSpace Open" : Mod_title.c_str(), XR_MAX_APPLICATION_NAME_SIZE);

	XrResult create = xrCreateInstance(&instanceCreateInfo, &xr_instance);
	if (create != XR_SUCCESS) {
		if (create == XR_ERROR_EXTENSION_NOT_PRESENT) {
			//This is to be expected to be one of THE common issue cases, since not all OpenXR runtimes, notably WMR, don't supply the OpenGL extensions
			ReleaseWarning(LOCATION, "The OpenXR runtime does not support the required backend! Try to use a different OpenXR runtime.");
		}

		mprintf(("Failed to create OpenXR instance with code %d\n", static_cast<int>(create)));

		return false;
	}

#if !defined(NDEBUG) && defined(FS_OPENXR_DEBUG)
	XrDebugUtilsMessengerCreateInfoEXT debugMessengerCreateInfo {
		XR_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
		nullptr,
		XR_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT | XR_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | XR_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
		XR_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | XR_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | XR_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT | XR_DEBUG_UTILS_MESSAGE_TYPE_CONFORMANCE_BIT_EXT,
		(PFN_xrDebugUtilsMessengerCallbackEXT)(&handleXRError),
		nullptr
	};

	openxr_callExtensionFunction<PFN_xrCreateDebugUtilsMessengerEXT>("xrCreateDebugUtilsMessengerEXT", xr_instance, &debugMessengerCreateInfo, &xr_debugMessenger);
#endif

	return true;
}

static bool openxr_init_system() {
	XrSystemGetInfo systemGetInfo {
	XR_TYPE_SYSTEM_GET_INFO,
	nullptr,
	XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY
	};

	XrResult systemInit = xrGetSystem(xr_instance, &systemGetInfo, &xr_system);
	if (systemInit != XR_SUCCESS) {
		mprintf(("Failed to create OpenXR system with code %d\n", static_cast<int>(systemInit)));
		xr_system = XR_NULL_SYSTEM_ID;
		return false;
	}

	return true;
}

static bool openxr_init_configuration_views() {
	uint32_t configurationViewsCount = static_cast<uint32_t>(xr_configurationviews.size());
	for (auto& configView : xr_configurationviews)
		configView = { XR_TYPE_VIEW_CONFIGURATION_VIEW, nullptr, 0, 0, 0, 0, 0, 0 };

	if (xrEnumerateViewConfigurationViews(xr_instance, xr_system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, configurationViewsCount, &configurationViewsCount, xr_configurationviews.data()) != XR_SUCCESS) {
		mprintf(("Failed to find OpenXR configuration views for stereo displays!\n"));
		return false;
	}

	return true;
}

static bool openxr_init_swapchains() {
	uint32_t formatCount = 0;
	if (xrEnumerateSwapchainFormats(xr_session, 0, &formatCount, nullptr) != XR_SUCCESS) {
		return false;
	}

	SCP_vector<int64_t> formats(formatCount);
	if (xrEnumerateSwapchainFormats(xr_session, formatCount, &formatCount, formats.data()) != XR_SUCCESS) {
		return false;
	}

	int64_t chosenFormat = gr_openxr_get_swapchain_format(formats);

	std::array<XrSwapchain, 2> swapchains;
	for (uint32_t i = 0; i < 2; i++)
	{
		XrSwapchainCreateInfo swapchainCreateInfo {
			XR_TYPE_SWAPCHAIN_CREATE_INFO,
			nullptr,
			0,
			//Vulkan runtimes hand back images they expect to receive again in
			//VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, which requires the color
			//attachment usage. Inert for OpenGL.
			XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT,
			chosenFormat,
			1,
			xr_configurationviews[i].recommendedImageRectWidth,
			xr_configurationviews[i].recommendedImageRectHeight,
			1,
			1,
			1
		};

		XrResult swapchainInit = xrCreateSwapchain(xr_session, &swapchainCreateInfo, &swapchains[i]);
		if (swapchainInit != XR_SUCCESS) {
			mprintf(("Failed to create OpenXR swapchain %d with code %d\n", i, static_cast<int>(swapchainInit)));
			return false;
		}

		xr_swapchains[i].reset(new XrSwapchainHandler {
			swapchains[i],
			chosenFormat,
			xr_configurationviews[i].recommendedImageRectWidth,
			xr_configurationviews[i].recommendedImageRectHeight
		});
	}

	if (!gr_openxr_acquire_swapchain_buffers()) {
		return false;
	}

	return true;
}

static bool openxr_init_space() {
	XrReferenceSpaceCreateInfo spaceCreateInfo {
		XR_TYPE_REFERENCE_SPACE_CREATE_INFO,
		nullptr,
		XR_REFERENCE_SPACE_TYPE_LOCAL,
		XrPosef{ XrQuaternionf{ 0, 0, 0, 1 }, XrVector3f{ 0, 0, 0 } },
	};

	if (xrCreateReferenceSpace(xr_session, &spaceCreateInfo, &xr_space) != XR_SUCCESS) {
		return false;
	}

	return true;
}

// ---- Controller input -------------------------------------------------------------------------------------------
// The engine has no native OpenXR input, so the controllers are exposed to the game as an SDL virtual joystick
// ("OpenXR Controllers") which the regular joystick code binds to Joy-0 (or the first free slot).
//   axis 0 (X)  = right stick X (bank)        axis 1 (Y)  = right stick Y (pitch, forward = negative)
//   axis 5 (RZ) = left stick X (yaw)          axis 2 (Z)  = absolute throttle, integrated from left stick Y
//   axis 3 (RX) = left trigger                axis 4 (RY) = right trigger
//   buttons: 0 R trigger, 1 A, 2 B, 3 X, 4 Y, 5 L trigger, 6 L squeeze, 7 R squeeze, 8 L stick, 9 R stick, 10 menu
static constexpr int XR_PAD_AXES = 6;
static constexpr int XR_PAD_BUTTONS = 11;

static XrActionSet xr_actionset = XR_NULL_HANDLE;
static XrAction xr_act_stick = XR_NULL_HANDLE, xr_act_trigger = XR_NULL_HANDLE, xr_act_squeeze = XR_NULL_HANDLE;
static XrAction xr_act_primary = XR_NULL_HANDLE, xr_act_secondary = XR_NULL_HANDLE, xr_act_stickclick = XR_NULL_HANDLE, xr_act_menu = XR_NULL_HANDLE;
static std::array<XrPath, 2> xr_hand_paths{ XR_NULL_PATH, XR_NULL_PATH };
static bool xr_input_ready = false;
static XrAction xr_act_aim = XR_NULL_HANDLE;
static std::array<XrSpace, 2> xr_aim_spaces{ XR_NULL_HANDLE, XR_NULL_HANDLE };
static float xr_pointer_u = -1.0f, xr_pointer_v = -1.0f;
static bool xr_pointer_down = false;
// where the right controller appears on the menu plane when seen from the head (0..1 = on the screen, outside = beyond its edge)
static bool xr_pointer_hand_valid = false;
static float xr_pointer_hand_u = 0.0f, xr_pointer_hand_v = 0.0f;
static SDL_Joystick* xr_pad = nullptr;
static bool xr_pad_attach_failed = false;
static float xr_throttle = 0.5f;
static Uint64 xr_input_last_ns = 0;

static XrAction openxr_make_action(const char* name, XrActionType type) {
	XrActionCreateInfo info{ XR_TYPE_ACTION_CREATE_INFO, nullptr };
	strncpy(info.actionName, name, XR_MAX_ACTION_NAME_SIZE - 1);
	strncpy(info.localizedActionName, name, XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
	info.actionType = type;
	info.countSubactionPaths = 2;
	info.subactionPaths = xr_hand_paths.data();
	XrAction action = XR_NULL_HANDLE;
	if (xrCreateAction(xr_actionset, &info, &action) != XR_SUCCESS)
		return XR_NULL_HANDLE;
	return action;
}

static void openxr_suggest(const char* profile, const SCP_vector<std::pair<XrAction, const char*>>& binds) {
	XrPath profilePath;
	if (xrStringToPath(xr_instance, profile, &profilePath) != XR_SUCCESS)
		return;

	SCP_vector<XrActionSuggestedBinding> suggested;
	for (const auto& b : binds) {
		XrPath path;
		if (b.first != XR_NULL_HANDLE && xrStringToPath(xr_instance, b.second, &path) == XR_SUCCESS)
			suggested.push_back({ b.first, path });
	}

	XrInteractionProfileSuggestedBinding info{ XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING, nullptr, profilePath,
		static_cast<uint32_t>(suggested.size()), suggested.data() };
	if (xrSuggestInteractionProfileBindings(xr_instance, &info) != XR_SUCCESS)
		mprintf(("OpenXR: runtime rejected controller bindings for %s\n", profile));
}

static void openxr_init_input() {
	XrActionSetCreateInfo setInfo{ XR_TYPE_ACTION_SET_CREATE_INFO, nullptr };
	strcpy(setInfo.actionSetName, "flight");
	strcpy(setInfo.localizedActionSetName, "Flight");
	if (xrCreateActionSet(xr_instance, &setInfo, &xr_actionset) != XR_SUCCESS) {
		mprintf(("OpenXR: could not create the input action set, controllers disabled\n"));
		return;
	}

	xrStringToPath(xr_instance, "/user/hand/left", &xr_hand_paths[0]);
	xrStringToPath(xr_instance, "/user/hand/right", &xr_hand_paths[1]);

	xr_act_stick = openxr_make_action("stick", XR_ACTION_TYPE_VECTOR2F_INPUT);
	xr_act_trigger = openxr_make_action("trigger", XR_ACTION_TYPE_FLOAT_INPUT);
	xr_act_squeeze = openxr_make_action("squeeze", XR_ACTION_TYPE_FLOAT_INPUT);
	xr_act_primary = openxr_make_action("primary", XR_ACTION_TYPE_BOOLEAN_INPUT);      // A / X
	xr_act_secondary = openxr_make_action("secondary", XR_ACTION_TYPE_BOOLEAN_INPUT);  // B / Y
	xr_act_stickclick = openxr_make_action("stick_click", XR_ACTION_TYPE_BOOLEAN_INPUT);
	xr_act_menu = openxr_make_action("menu", XR_ACTION_TYPE_BOOLEAN_INPUT);
	xr_act_aim = openxr_make_action("aim", XR_ACTION_TYPE_POSE_INPUT);

	const SCP_vector<std::pair<XrAction, const char*>> touch = {
		{ xr_act_stick, "/user/hand/left/input/thumbstick" }, { xr_act_stick, "/user/hand/right/input/thumbstick" },
		{ xr_act_trigger, "/user/hand/left/input/trigger/value" }, { xr_act_trigger, "/user/hand/right/input/trigger/value" },
		{ xr_act_squeeze, "/user/hand/left/input/squeeze/value" }, { xr_act_squeeze, "/user/hand/right/input/squeeze/value" },
		{ xr_act_primary, "/user/hand/left/input/x/click" }, { xr_act_primary, "/user/hand/right/input/a/click" },
		{ xr_act_secondary, "/user/hand/left/input/y/click" }, { xr_act_secondary, "/user/hand/right/input/b/click" },
		{ xr_act_stickclick, "/user/hand/left/input/thumbstick/click" }, { xr_act_stickclick, "/user/hand/right/input/thumbstick/click" },
		{ xr_act_menu, "/user/hand/left/input/menu/click" },
		{ xr_act_aim, "/user/hand/left/input/aim/pose" }, { xr_act_aim, "/user/hand/right/input/aim/pose" },
	};
	openxr_suggest("/interaction_profiles/oculus/touch_controller", touch);
	if (xr_has_touch_plus)
		openxr_suggest("/interaction_profiles/meta/touch_controller_plus", touch);
	openxr_suggest("/interaction_profiles/khr/simple_controller", {
		{ xr_act_trigger, "/user/hand/left/input/select/click" }, { xr_act_trigger, "/user/hand/right/input/select/click" },
		{ xr_act_menu, "/user/hand/left/input/menu/click" }, { xr_act_menu, "/user/hand/right/input/menu/click" },
		{ xr_act_aim, "/user/hand/left/input/aim/pose" }, { xr_act_aim, "/user/hand/right/input/aim/pose" },
	});

	for (int hand = 0; hand < 2 && xr_act_aim != XR_NULL_HANDLE; hand++) {
		XrActionSpaceCreateInfo spaceInfo{ XR_TYPE_ACTION_SPACE_CREATE_INFO, nullptr, xr_act_aim, xr_hand_paths[hand],
			XrPosef{ XrQuaternionf{ 0, 0, 0, 1 }, XrVector3f{ 0, 0, 0 } } };
		if (xrCreateActionSpace(xr_session, &spaceInfo, &xr_aim_spaces[hand]) != XR_SUCCESS)
			xr_aim_spaces[hand] = XR_NULL_HANDLE;
	}

	XrSessionActionSetsAttachInfo attach{ XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO, nullptr, 1, &xr_actionset };
	if (xrAttachSessionActionSets(xr_session, &attach) != XR_SUCCESS) {
		mprintf(("OpenXR: could not attach the input action set, controllers disabled\n"));
		return;
	}

	xr_input_ready = true;
	mprintf(("OpenXR: controller input enabled (virtual joystick 'OpenXR Controllers')\n"));
}

static Sint16 openxr_axis16(float v) {
	v = std::max(-1.0f, std::min(1.0f, v));
	return static_cast<Sint16>(v * 32767.0f);
}

// Menus are drawn as a flat plane (x in +-2*clip_aspect, y in +-2, z = 4 in FSO space, see gr_opengl_openxr_flip) and the
// engine paints its own cursor at the mouse position, so the right controller's aim ray is turned into mouse input.
bool (*Openxr_hud_layer_begin_fn)() = nullptr;
void (*Openxr_hud_layer_end_fn)() = nullptr;
bool Openxr_gaze_valid = false;
vec3d Openxr_gaze_pos = ZERO_VECTOR, Openxr_gaze_dir = ZERO_VECTOR;

bool openxr_hud_layer_begin() {
	return openxr_enabled() && Openxr_hud_layer_begin_fn != nullptr && Openxr_hud_layer_begin_fn();
}

void openxr_hud_layer_end() {
	if (Openxr_hud_layer_end_fn != nullptr)
		Openxr_hud_layer_end_fn();
}

static float openxr_hud_env(const char* name, float fallback) {
	const char* v = getenv(name);
	return (v != nullptr && atof(v) > 0.0) ? static_cast<float>(atof(v)) : fallback;
}

// FSO's 2D HUD projection (reticle at the canvas centre, target brackets, lead indicators) only matches a symmetric
// frustum, so the HUD camera is symmetric and centred on the ship's nose. Horizontally it spans the eyes' average FOV
// (scaled by FSO_VR_HUD_ZOOM), vertically it follows the canvas aspect.
float openxr_hud_half_fov() {
	if (!openxr_enabled() || gr_screen.max_w <= 0 || gr_screen.max_h <= 0)
		return 0.0f;
	const float zoom = vrset::get(vrset::HUD_ZOOM);
	const float h = 0.25f * (xr_views[0].fov.angleRight - xr_views[0].fov.angleLeft + xr_views[1].fov.angleRight - xr_views[1].fov.angleLeft);
	const float v = 0.25f * (xr_views[0].fov.angleUp - xr_views[0].fov.angleDown + xr_views[1].fov.angleUp - xr_views[1].fov.angleDown);
	const float aspect = i2fl(gr_screen.max_w) / i2fl(gr_screen.max_h);
	return atanf(tanf(std::max(h, v)) * zoom / aspect);
}

float openxr_menu_scale() {
	return vrset::get(vrset::MENU_SCALE);
}

// far enough that head movement barely shifts the reticle against distant targets (FSO_VR_HUD_DISTANCE overrides)
float openxr_hud_distance() {
	return vrset::get(vrset::HUD_DISTANCE);
}

bool openxr_get_pointer_hand(float* u, float* v) {
	if (!xr_pointer_hand_valid)
		return false;
	*u = xr_pointer_hand_u;
	*v = xr_pointer_hand_v;
	return true;
}

static void openxr_update_pointer(bool trigger, bool squeeze) {
	xr_pointer_hand_valid = false;
	SDL_Window* window = os::getSDLMainWindow();
	if (window == nullptr || xr_aim_spaces[1] == XR_NULL_HANDLE || xr_stage != OpenXRFBStage::NONE)
		return;

	XrSpaceLocation loc{ XR_TYPE_SPACE_LOCATION };
	if (xrLocateSpace(xr_aim_spaces[1], xr_space, xr_state.predictedDisplayTime, &loc) != XR_SUCCESS)
		return;
	const XrSpaceLocationFlags need = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
	if ((loc.locationFlags & need) != need)
		return;

	// controller forward (-Z) rotated by the pose orientation: v' = v + 2w(q x v) + 2 q x (q x v)
	const XrQuaternionf& q = loc.pose.orientation;
	const float vx = 0.0f, vy = 0.0f, vz = -1.0f;
	const float c1x = q.y * vz - q.z * vy, c1y = q.z * vx - q.x * vz, c1z = q.x * vy - q.y * vx;
	const float c2x = q.y * c1z - q.z * c1y, c2y = q.z * c1x - q.x * c1z, c2z = q.x * c1y - q.y * c1x;
	const float dx = vx + 2.0f * (q.w * c1x + c2x);
	const float dy = vy + 2.0f * (q.w * c1y + c2y);
	const float dz = -(vz + 2.0f * (q.w * c1z + c2z));	// to FSO space (z flipped)

	const float ox = loc.pose.position.x - xr_offset.xyz.x;
	const float oy = loc.pose.position.y - xr_offset.xyz.y;
	const float oz = -loc.pose.position.z - xr_offset.xyz.z;

	if (dz < 0.05f)
		return;
	const float t = (4.0f - oz) / dz;
	const float hx = ox + t * dx, hy = oy + t * dy;

	const float halfh = 2.0f * openxr_menu_scale();
	const float halfw = halfh * gr_screen.clip_aspect;
	const float u = (hx / halfw + 1.0f) * 0.5f;
	const float v = (1.0f - hy / halfh) * 0.5f;	// 0 = top of the screen
	if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f)
		return;

	// where the beam starts as seen from the head (the origin of this frame): the controller itself, or - when it hangs
	// beside/behind the head plane - the first point of the pointing ray that is 0.3 m ahead of it. Projected onto the
	// menu plane, the laser from there to the cursor is the line between the two points in the menu image.
	{
		float s0 = 0.0f;
		if (oz < 0.3f)
			s0 = (0.3f - oz) / dz;
		const float sx = ox + s0 * dx, sy = oy + s0 * dy, sz = oz + s0 * dz;
		const float k = 4.0f / sz;
		xr_pointer_hand_u = (sx * k / halfw + 1.0f) * 0.5f;
		xr_pointer_hand_v = (1.0f - sy * k / halfh) * 0.5f;
		xr_pointer_hand_valid = true;
	}

	int ww = 0, wh = 0;
	SDL_GetWindowSize(window, &ww, &wh);

	SDL_Event ev;
	SDL_zero(ev);
	ev.type = SDL_EVENT_MOUSE_MOTION;
	ev.motion.windowID = SDL_GetWindowID(window);
	ev.motion.x = u * static_cast<float>(ww);
	ev.motion.y = v * static_cast<float>(wh);
	ev.motion.xrel = (xr_pointer_u < 0.0f) ? 0.0f : (u - xr_pointer_u) * static_cast<float>(ww);
	ev.motion.yrel = (xr_pointer_v < 0.0f) ? 0.0f : (v - xr_pointer_v) * static_cast<float>(wh);
	SDL_PushEvent(&ev);
	xr_pointer_u = u;
	xr_pointer_v = v;

	// right trigger = left click, right squeeze = right click
	static bool right_down = false;
	auto click = [&](int button, bool& state, bool now) {
		if (now == state)
			return;
		state = now;
		SDL_Event b;
		SDL_zero(b);
		b.type = now ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
		b.button.windowID = SDL_GetWindowID(window);
		b.button.button = static_cast<Uint8>(button);
		b.button.down = now;
		b.button.clicks = 1;
		b.button.x = ev.motion.x;
		b.button.y = ev.motion.y;
		SDL_PushEvent(&b);
	};
	click(SDL_BUTTON_LEFT, xr_pointer_down, trigger);
	click(SDL_BUTTON_RIGHT, right_down, squeeze);
}

static void openxr_update_input() {
	if (!xr_input_ready)
		return;

	if (xr_pad == nullptr) {
		if (xr_pad_attach_failed)
			return;

		SDL_VirtualJoystickDesc desc;
		SDL_INIT_INTERFACE(&desc);
		desc.type = SDL_JOYSTICK_TYPE_FLIGHT_STICK;
		desc.naxes = XR_PAD_AXES;
		desc.nbuttons = XR_PAD_BUTTONS;
		desc.vendor_id = 0x2833;	// Meta
		desc.product_id = 0x0186;
		desc.name = "OpenXR Controllers";
		SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
		if (id == 0) {
			// the joystick subsystem may not be up yet, try again on a later frame a few times
			static int tries = 0;
			if (++tries > 600)
				xr_pad_attach_failed = true;
			return;
		}
		xr_pad = SDL_OpenJoystick(id);
		if (xr_pad == nullptr) {
			xr_pad_attach_failed = true;
			return;
		}
	}

	XrActiveActionSet active{ xr_actionset, XR_NULL_PATH };
	XrActionsSyncInfo sync{ XR_TYPE_ACTIONS_SYNC_INFO, nullptr, 1, &active };
	const XrResult sync_result = xrSyncActions(xr_session, &sync);

	static const bool debug_diag = getenv("FSO_XR_INPUT_DEBUG") != nullptr;
	if (debug_diag) {
		static Uint64 last_diag = 0;
		const Uint64 t = SDL_GetTicksNS();
		if (t - last_diag > 2000000000ULL) {
			last_diag = t;
			XrInteractionProfileState prof{ XR_TYPE_INTERACTION_PROFILE_STATE };
			char path[XR_MAX_PATH_LENGTH] = "";
			uint32_t len = 0;
			XrPath rpath = XR_NULL_PATH;
			xrStringToPath(xr_instance, "/user/hand/right", &rpath);
			if (xrGetCurrentInteractionProfile(xr_session, rpath, &prof) == XR_SUCCESS && prof.interactionProfile != XR_NULL_PATH)
				xrPathToString(xr_instance, prof.interactionProfile, sizeof(path), &len, path);
			XrActionStateGetInfo gi{ XR_TYPE_ACTION_STATE_GET_INFO, nullptr, xr_act_trigger, xr_hand_paths[1] };
			XrActionStateFloat st{ XR_TYPE_ACTION_STATE_FLOAT };
			xrGetActionStateFloat(xr_session, &gi, &st);
			mprintf(("OpenXR input diag: sync=%d right profile='%s' trigger active=%d value=%.2f\n", static_cast<int>(sync_result), path, static_cast<int>(st.isActive), st.currentState));
		}
	}

	if (sync_result != XR_SUCCESS)
		return;

	auto getFloat = [](XrAction action, int hand) {
		XrActionStateGetInfo gi{ XR_TYPE_ACTION_STATE_GET_INFO, nullptr, action, xr_hand_paths[hand] };
		XrActionStateFloat st{ XR_TYPE_ACTION_STATE_FLOAT };
		if (xrGetActionStateFloat(xr_session, &gi, &st) == XR_SUCCESS && st.isActive)
			return st.currentState;
		return 0.0f;
	};
	auto getBool = [](XrAction action, int hand) {
		XrActionStateGetInfo gi{ XR_TYPE_ACTION_STATE_GET_INFO, nullptr, action, xr_hand_paths[hand] };
		XrActionStateBoolean st{ XR_TYPE_ACTION_STATE_BOOLEAN };
		return xrGetActionStateBoolean(xr_session, &gi, &st) == XR_SUCCESS && st.isActive && st.currentState;
	};
	auto getStick = [](int hand) {
		XrActionStateGetInfo gi{ XR_TYPE_ACTION_STATE_GET_INFO, nullptr, xr_act_stick, xr_hand_paths[hand] };
		XrActionStateVector2f st{ XR_TYPE_ACTION_STATE_VECTOR2F };
		if (xrGetActionStateVector2f(xr_session, &gi, &st) == XR_SUCCESS && st.isActive)
			return st.currentState;
		return XrVector2f{ 0.0f, 0.0f };
	};

	const XrVector2f left = getStick(0), right = getStick(1);

	const Uint64 now = SDL_GetTicksNS();
	float dt = xr_input_last_ns ? static_cast<float>(now - xr_input_last_ns) * 1e-9f : 0.0f;
	xr_input_last_ns = now;
	dt = std::min(dt, 0.1f);

	// the throttle lever holds its position: pushing the left stick forward raises it, back lowers it
	if (std::fabs(left.y) > 0.15f)
		xr_throttle = std::max(0.0f, std::min(1.0f, xr_throttle + left.y * dt * 0.6f));

	SDL_SetJoystickVirtualAxis(xr_pad, 0, openxr_axis16(right.x));
	SDL_SetJoystickVirtualAxis(xr_pad, 1, openxr_axis16(-right.y));
	// FSO's absolute throttle axis: minimum = full throttle
	SDL_SetJoystickVirtualAxis(xr_pad, 2, openxr_axis16(1.0f - 2.0f * xr_throttle));
	SDL_SetJoystickVirtualAxis(xr_pad, 3, openxr_axis16(getFloat(xr_act_trigger, 0) * 2.0f - 1.0f));
	SDL_SetJoystickVirtualAxis(xr_pad, 4, openxr_axis16(getFloat(xr_act_trigger, 1) * 2.0f - 1.0f));
	SDL_SetJoystickVirtualAxis(xr_pad, 5, openxr_axis16(left.x));

	SDL_SetJoystickVirtualButton(xr_pad, 0, getFloat(xr_act_trigger, 1) > 0.5f);
	SDL_SetJoystickVirtualButton(xr_pad, 1, getBool(xr_act_primary, 1));
	SDL_SetJoystickVirtualButton(xr_pad, 2, getBool(xr_act_secondary, 1));
	SDL_SetJoystickVirtualButton(xr_pad, 3, getBool(xr_act_primary, 0));
	SDL_SetJoystickVirtualButton(xr_pad, 4, getBool(xr_act_secondary, 0));
	SDL_SetJoystickVirtualButton(xr_pad, 5, getFloat(xr_act_trigger, 0) > 0.5f);
	SDL_SetJoystickVirtualButton(xr_pad, 6, getFloat(xr_act_squeeze, 0) > 0.5f);
	SDL_SetJoystickVirtualButton(xr_pad, 7, getFloat(xr_act_squeeze, 1) > 0.5f);
	SDL_SetJoystickVirtualButton(xr_pad, 8, getBool(xr_act_stickclick, 0));
	SDL_SetJoystickVirtualButton(xr_pad, 9, getBool(xr_act_stickclick, 1));
	SDL_SetJoystickVirtualButton(xr_pad, 10, getBool(xr_act_menu, 0));

	openxr_update_pointer(getFloat(xr_act_trigger, 1) > 0.5f, getFloat(xr_act_squeeze, 1) > 0.5f);

	// FSO_XR_INPUT_DEBUG=1 logs the pad state a few times per second while it changes
	static const bool debug_input = getenv("FSO_XR_INPUT_DEBUG") != nullptr;
	if (debug_input) {
		static Uint64 last_log = 0;
		static SCP_string last_state;
		SCP_string state;
		for (int i = 0; i < XR_PAD_AXES; i++)
			state += std::to_string(SDL_GetJoystickAxis(xr_pad, i)) + " ";
		for (int i = 0; i < XR_PAD_BUTTONS; i++)
			state += SDL_GetJoystickButton(xr_pad, i) ? "1" : "0";
		if (state != last_state && now - last_log > 250000000ULL) {
			mprintf(("OpenXR pad: %s\n", state.c_str()));
			last_state = state;
			last_log = now;
		}
	}
}

static void openxr_init_post() {
	// Some runtimes (e.g. Monado) reject xrLocateViews with a zero time, so submit one empty frame to obtain a valid predicted time first.
	if (xr_time == 0) {
		XrFrameState frameState { XR_TYPE_FRAME_STATE, nullptr, 0, 0, 0 };
		XrFrameWaitInfo waitInfo { XR_TYPE_FRAME_WAIT_INFO, nullptr };
		XrFrameBeginInfo beginInfo { XR_TYPE_FRAME_BEGIN_INFO, nullptr };
		if (xrWaitFrame(xr_session, &waitInfo, &frameState) == XR_SUCCESS && xrBeginFrame(xr_session, &beginInfo) == XR_SUCCESS) {
			XrFrameEndInfo endInfo { XR_TYPE_FRAME_END_INFO, nullptr, frameState.predictedDisplayTime, XR_ENVIRONMENT_BLEND_MODE_OPAQUE, 0, nullptr };
			xrEndFrame(xr_session, &endInfo);
			xr_time = frameState.predictedDisplayTime;
		}
	}

	XrViewLocateInfo viewLocateInfo {
		XR_TYPE_VIEW_LOCATE_INFO,
		nullptr,
		XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
		xr_time,
		xr_space
	};

	XrViewState viewState {
		XR_TYPE_VIEW_STATE,
		nullptr,
		0
	};

	xr_views.fill({ XR_TYPE_VIEW, nullptr, XrPosef{}, XrFovf{} });
	uint32_t views;
	xrLocateViews(
		xr_session,
		&viewLocateInfo,
		&viewState,
		2,
		&views,
		xr_views.data()
	);

	openxr_reset_offset();

	vec3d xr_offset_debug {{ {
		(xr_views[0].pose.position.x - xr_views[1].pose.position.x) * xr_scale,
		(xr_views[0].pose.position.y - xr_views[1].pose.position.y) * xr_scale,
		(xr_views[0].pose.position.z - xr_views[1].pose.position.z) * xr_scale} }};

	mprintf(("OpenXR HMD data:\n\tLeft Res: %dx%d\n\tLeft FoV (l|r|t|b): %f, %f, %f, %f\n\tRight Res: %dx%d\n\tRight FoV (l|r|t|b): %f, %f, %f, %f\n\tIPD: %f\n",
		xr_swapchains[0]->width, xr_swapchains[0]->height,
		xr_views[0].fov.angleLeft, xr_views[0].fov.angleRight, xr_views[0].fov.angleUp, xr_views[0].fov.angleDown,
		xr_swapchains[1]->width, xr_swapchains[1]->height,
		xr_views[1].fov.angleLeft, xr_views[1].fov.angleRight, xr_views[1].fov.angleUp, xr_views[1].fov.angleDown,
		vm_vec_mag(&xr_offset_debug)));

	VIEWER_ZOOM_DEFAULT = COCKPIT_ZOOM_DEFAULT = asymmetric_fov { 
		xr_views[0].fov.angleLeft + xr_views[1].fov.angleLeft,
		xr_views[0].fov.angleRight + xr_views[1].fov.angleRight,
		xr_views[0].fov.angleUp + xr_views[1].fov.angleUp,
		xr_views[0].fov.angleDown + xr_views[1].fov.angleDown
	} * (1.0f / (2.0f * PROJ_FOV_FACTOR));
}

float openxr_preinit(float req_ar, float scale) {
	xr_scale = scale;

	mprintf(("Attempting to pre-initialize OpenXR...\n"));

	if (!openxr_init_instance()) {
		openxr_req = false;
		return req_ar;
	}

	if (!openxr_init_system()) {
		openxr_req = false;
		return req_ar;
	}

	if (!openxr_init_configuration_views()) {
		openxr_req = false;
		return req_ar;
	}

	return  i2fl(xr_configurationviews[0].recommendedImageRectWidth) / i2fl(xr_configurationviews[0].recommendedImageRectHeight);
}

void openxr_init() {
	if (!openxr_req)
		return;

	mprintf(("Attempting to initialize OpenXR and get a session...\n"));

	if (!gr_openxr_test_capabilities())
		return;

	//Non OpenGL backends, like vulkan, may need to query certain extensions to be enabled here as well

	if (!gr_openxr_create_session())
		return;
	
	if (!openxr_init_swapchains())
		return;

	if (!openxr_init_space())
		return;

	openxr_init_input();

	openxr_initialized = true;

	mprintf(("OpenXR initialized!\n"));

	while (openxr_initialized && !openxr_recieve) {
		os_poll();
	}

	mprintf(("OpenXR recieving!\n"));

	openxr_init_post();
}

void openxr_close() {
	xrDestroySpace(xr_space);
	for (auto& sc : xr_swapchains)
		sc.release();
	xrDestroySession(xr_session);

#if !defined(NDEBUG) && defined(FS_OPENXR_DEBUG)
	openxr_callExtensionFunction<PFN_xrDestroyDebugUtilsMessengerEXT>("xrDestroyDebugUtilsMessengerEXT", xr_debugMessenger);
#endif

	xrDestroyInstance(xr_instance);
}

bool openxr_enabled() {
	return openxr_initialized && openxr_recieve;
}

bool openxr_stereo_frame_open() {
	return xr_stage == OpenXRFBStage::FIRST;
}

bool openxr_requested() {
	return openxr_req;
}

void openxr_reset_offset() {
	xr_offset = ZERO_VECTOR;
	for (uint32_t i = 0; i < 2; i++) {
		const auto& pos = xr_views[i].pose.position;
		xr_offset += vec3d{ { {pos.x * xr_scale, pos.y * xr_scale, pos.z * -xr_scale} } };
	}
	xr_offset /= 2;
}

void openxr_start_mission() {
	if (!openxr_initialized)
		return;

	openxr_reset_offset();

	if (!static_cast<bool>(Stars_XRBuffer))
		Stars_XRBuffer = make_unique<star[]>(MAX_STARS);

	extern std::unique_ptr<star[]> Stars;
	std::copy(Stars.get(), Stars.get() + MAX_STARS, Stars_XRBuffer.get());
}

void openxr_poll() {
	if (!openxr_initialized)
		return;

	XrEventDataBuffer eventData{
		XR_TYPE_EVENT_DATA_BUFFER,
		nullptr,
		{0}
	};

	XrResult result = xrPollEvent(xr_instance, &eventData);
	if (result == XR_EVENT_UNAVAILABLE) {
		//No event
	}
	else if (result != XR_SUCCESS) {
		openxr_initialized = false;
		openxr_close();
	}
	else {
		switch (eventData.type)
		{
		case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
			openxr_initialized = false;
			openxr_close();
			break;
		case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED:
		{
			auto event = (XrEventDataSessionStateChanged*)&eventData;

			xr_time = event->time;

			switch (event->state)
			{
			case XR_SESSION_STATE_IDLE:
				openxr_recieve = false;
				break;
			case XR_SESSION_STATE_READY:
			{
				XrSessionBeginInfo sessionBeginInfo{
					XR_TYPE_SESSION_BEGIN_INFO,
					nullptr,
					XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO
				};

				if (xrBeginSession(xr_session, &sessionBeginInfo) != XR_SUCCESS) {
					openxr_initialized = false;
					openxr_close();
				}
			}
			/*fallthrough*/
			case XR_SESSION_STATE_SYNCHRONIZED:
			case XR_SESSION_STATE_VISIBLE:
			case XR_SESSION_STATE_FOCUSED:
				openxr_recieve = true;
				break;
			case XR_SESSION_STATE_STOPPING:
				xrEndSession(xr_session);
			/*fallthrough*/
			case XR_SESSION_STATE_LOSS_PENDING:
			case XR_SESSION_STATE_EXITING:
				openxr_initialized = false;
				openxr_close();
				break;
			default:
				break;
			}
			break;
		}
		default:
			break;
		}
	}
}

OpenXRTrackingInfo openxr_start_stereo_frame() {
	if (!openxr_initialized)
		return OpenXRTrackingInfo{};

	xr_stage = OpenXRFBStage::FIRST;

	openxr_start_frame();

	OpenXRTrackingInfo info{};

	for (uint32_t i = 0; i < 2; i++) {
		const auto& pos = xr_views[i].pose.position;
		const auto& ori = xr_views[i].pose.orientation;
		info.eyes[i].offset = vec3d{{ {pos.x * xr_scale, pos.y * xr_scale, pos.z * -xr_scale} }} - xr_offset;
		vm_quaternion_to_matrix(&info.eyes[i].orientation, ori.x, ori.y, -ori.z, -ori.w);
		info.eyes[i].zoom = asymmetric_fov{ xr_views[i].fov.angleLeft, xr_views[i].fov.angleRight, xr_views[i].fov.angleUp, xr_views[i].fov.angleDown } * (1.0f / PROJ_FOV_FACTOR);
	}

	return info;
}

//Internal Helper functions

void openxr_start_frame() {
	XrFrameWaitInfo frameWaitInfo {
		XR_TYPE_FRAME_WAIT_INFO,
		nullptr
	};
	xr_state = XrFrameState {
		XR_TYPE_FRAME_STATE,
		nullptr,
		0,
		0,
		0
	};

	const XrResult wait_result = xrWaitFrame(xr_session, &frameWaitInfo, &xr_state);
	if (XR_FAILED(wait_result)) {
		// the runtime went away (headset removed, WiVRn/Monado stopped): carry on as a flat game instead of spinning on dead frames
		mprintf(("OpenXR: xrWaitFrame failed with %d, the runtime connection was lost. Continuing without VR.\n", static_cast<int>(wait_result)));
		openxr_initialized = false;
		xr_stage = OpenXRFBStage::NONE;
		return;
	}

	openxr_update_input();

	XrViewLocateInfo viewLocateInfo {
		XR_TYPE_VIEW_LOCATE_INFO,
		nullptr,
		XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
		xr_state.predictedDisplayTime,
		xr_space
	};

	xr_time = xr_state.predictedDisplayTime;

	XrViewState viewState {
		XR_TYPE_VIEW_STATE,
		nullptr,
		0
	};

	xr_views.fill({ XR_TYPE_VIEW, nullptr, XrPosef{}, XrFovf{} });

	uint32_t views;
	xrLocateViews(
		xr_session,
		&viewLocateInfo,
		&viewState,
		2,
		&views,
		xr_views.data()
	);

	XrFrameBeginInfo beginFrameInfo{ XR_TYPE_FRAME_BEGIN_INFO, nullptr };
	xrBeginFrame(xr_session, &beginFrameInfo);
}

#else
// Stubs for when building without OpenXR support.
// NOTE: macOS has issues linking with OpenXR.

void openxr_prepare(float /*hudscale*/) {}

float openxr_preinit(float /*req_ar*/, float /*scale*/) {
	mprintf(("Cannot create OpenXR session. Not built with OpenXR support.\n"));
	return 0.0f;
}

void openxr_init() {}

void openxr_close() {}

void openxr_poll() {}

void openxr_start_mission() {}

bool openxr_enabled() { return false; }
bool openxr_stereo_frame_open() { return false; }

bool openxr_requested() { return false; }

OpenXRTrackingInfo openxr_start_stereo_frame() { return OpenXRTrackingInfo{}; }

bool openxr_get_pointer_hand(float* /*u*/, float* /*v*/) { return false; }

bool (*Openxr_hud_layer_begin_fn)() = nullptr;
void (*Openxr_hud_layer_end_fn)() = nullptr;
bool Openxr_gaze_valid = false;
vec3d Openxr_gaze_pos = ZERO_VECTOR, Openxr_gaze_dir = ZERO_VECTOR;

bool openxr_hud_layer_begin() { return false; }

void openxr_hud_layer_end() {}

float openxr_hud_half_fov() { return 0.0f; }

float openxr_hud_distance() { return 0.0f; }

float openxr_menu_scale() { return 1.0f; }

#endif
