#pragma once

#include <array>
#include "camera/camera.h"

struct OpenXRTrackingInfo {
	struct OpenXREyeInfo {
		vec3d offset;
		matrix orientation;
		fov_t zoom; //zoom, not fov! difference of a factor of PROJ_FOV_FACTOR
	};
	std::array<OpenXREyeInfo, 2> eyes;
};

/**
 * @brief Performs required Setup actions for OpenXR before actually trying to initialize the graphics
 * @param hudscale Sets the scale at which the HUD is rendered. May need to be tweakable to make the hud visible to players with small FoV
 */
void openxr_prepare(float hudscale = 0.5f);

/**
 * @brief Initializes the OpenXR API and opens the XR system
 * @param req_ar The aspect ratio as specified by the user resolution
 * @param scale Controls the scale of the OpenXR coordinates. This mainly affects eye distance and how far real movement translates to ingame movement
 * @returns the expected aspect ratio of the swapchain
 */
float openxr_preinit(float req_ar, float scale = 1.0f);

/**
 * @brief Opens an OpenXR session to the point that we can render. As this waits for OpenXR, this may take a while
 */
void openxr_init();

/**
 * @brief Shuts down OpenXR and cleans up memory
 */
void openxr_close();

/**
 * @brief Polls OpenXR state. Does NOT poll HMD position!
 */
void openxr_poll();

/**
 * @brief Grabs the last known HMD position and sets it as the zero-state (whereever the specified eyepoint is in mission / ship), as well as initializing buffers for the mission
 */
void openxr_start_mission();

/**
 * @brief Polls whether OpenXR is active and can render
 */
bool openxr_enabled();
// true between the left and the right eye of a stereo frame: the first gr_flip() of the pair submits nothing
bool openxr_stereo_frame_open();
// position of the pointing controller on the menu plane as seen from the head, in 0..1 screen units (may lie outside the screen)
bool openxr_get_pointer_hand(float* u, float* v);

// VR HUD composition layer: in stereo frames the HUD is drawn once (with the ship camera) between begin() and end(),
// copied into its own swapchain and shown by the runtime as a quad fixed in the cockpit (tracking space), instead of
// being drawn into the eye images. begin() returns false when the backend has no HUD layer (then draw it as before).
bool openxr_hud_layer_begin();
void openxr_hud_layer_end();
// half vertical FOV (radians) of the symmetric camera the HUD layer is drawn with, and the quad's distance (metres)
float openxr_hud_half_fov();
float openxr_hud_distance();
// size factor of the flat screen that shows menus, briefings and cutscenes (FSO_VR_MENU_SCALE, default 1.35)
float openxr_menu_scale();
// where the player's head points (world space), updated every VR frame: "Target Ship in Reticle" uses it in VR
extern bool Openxr_gaze_valid;
extern vec3d Openxr_gaze_pos, Openxr_gaze_dir;

// set by the graphics backend that implements the HUD layer
extern bool (*Openxr_hud_layer_begin_fn)();
extern void (*Openxr_hud_layer_end_fn)();

/**
 * @brief Polls whether OpenXR is requested to start. Required if you need to poll for OpenXR during some initialization where it might not yet be running
 */
bool openxr_requested();

/**
 * @brief Polls HMD position and sets up the OpenXR state machine to wait for and accept two frames, one for each eye.
 * You must ensure that after calling this, the next two gr_flip() calls correspond to the frames for each eye!
 * @returns The HMD tracking data including position and rotation in FSO coordinates.
 */
OpenXRTrackingInfo openxr_start_stereo_frame();