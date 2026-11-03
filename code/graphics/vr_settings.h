#pragma once

// VR options of the Diaspora port. Every value starts from its default, can be overridden by the environment variable
// of the same name (vr.env, FSO_VR_*), and is finally overridden by what the player saved from the in-game VR settings
// screen (F9 in the main hall and in the Options screen): <profile>/data/diaspora_vr_settings.ini.

namespace vrset {

enum Id {
	HUD_DISTANCE,   // distance of the HUD layer in front of the cockpit (m)
	HUD_ZOOM,       // size of the HUD layer
	HUD_HEIGHT,     // < 1 squeezes the HUD panels towards the middle
	STORES_DX,      // lower right gauge group (weapons) offset, 1024-wide HUD units
	STORES_DY,
	LEFT_DX,        // lower left gauge group (target box)
	LEFT_DY,
	SHIP_DX,        // ship outline nudge
	WEAPONS_DX,     // weapon stores text nudge
	MENU_SCALE,     // menu quad scale
	FLASH,          // strength of full-view flashes
	SUNGLARE,       // strength of the sun glare flash (when light shafts are off)
	SHAFTS,         // light shafts (sun rays) on/off
	SHAKE,          // camera shake on/off
	PADLOCK,        // padlock views on/off (off: the hat targets instead)
	GAZE_TARGET,    // look-to-target on/off
	COUNT
};

struct Info {
	const char* env;    // environment variable and ini key
	const char* label;  // text in the settings screen
	float def, min, max, step;
	bool toggle;
};

const Info& info(Id id);
float get(Id id);
inline bool on(Id id) { return get(id) >= 0.5f; }
void set(Id id, float value);   // clamped; call save() to keep it
void reset(Id id);
void save();

}  // namespace vrset

// modal in-game screen to change and save the settings above
void vr_settings_menu();
