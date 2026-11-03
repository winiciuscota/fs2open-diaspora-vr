#include "graphics/vr_settings.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "freespace.h"
#include "gamesequence/gamesequence.h"
#include "gamesnd/gamesnd.h"
#include "globalincs/alphacolors.h"
#include "graphics/2d.h"
#include "graphics/font.h"
#include "io/key.h"
#include "io/mouse.h"
#include "osapi/osapi.h"
#include "ui/ui.h"

namespace vrset {

namespace {

// env, label, default, min, max, step, toggle
const Info Table[COUNT] = {
	{ "FSO_VR_HUD_DISTANCE", "HUD distance (m)",              10.0f,   4.0f,  40.0f,  1.0f,  false },
	{ "FSO_VR_HUD_ZOOM",     "HUD size",                      0.9f,    0.5f,  1.4f,   0.05f, false },
	{ "FSO_VR_HUD_HEIGHT",   "HUD height (1 = full)",         1.0f,    0.3f,  1.0f,   0.05f, false },
	{ "FSO_VR_HUD_STORES_DX", "Weapons panel: left/right",    0.0f,    -300.0f, 300.0f, 10.0f, false },
	{ "FSO_VR_HUD_STORES_DY", "Weapons panel: up/down",       -150.0f, -400.0f, 100.0f, 10.0f, false },
	{ "FSO_VR_HUD_LEFT_DX",  "Target box: left/right",        60.0f,   -300.0f, 300.0f, 10.0f, false },
	{ "FSO_VR_HUD_LEFT_DY",  "Target box: up/down",           -150.0f, -400.0f, 100.0f, 10.0f, false },
	{ "FSO_VR_HUD_SHIP_DX",  "Ship outline: left/right",      0.0f,    -300.0f, 300.0f, 10.0f, false },
	{ "FSO_VR_HUD_WEAPONS_DX", "Weapon text: left/right",     25.0f,   -300.0f, 300.0f, 5.0f,  false },
	{ "FSO_VR_MENU_SCALE",   "Menu size",                     1.35f,   0.8f,  2.2f,   0.05f, false },
	{ "FSO_VR_FLASH",        "Explosion flash strength",      0.35f,   0.0f,  1.0f,   0.05f, false },
	{ "FSO_VR_SUNGLARE",     "Sun glare strength",            0.3f,    0.0f,  1.0f,   0.05f, false },
	{ "FSO_VR_SHAFTS",       "Sun light shafts",              1.0f,    0.0f,  1.0f,   1.0f,  true },
	{ "FSO_VR_SHAKE",        "Camera shake",                  0.0f,    0.0f,  1.0f,   1.0f,  true },
	{ "FSO_VR_PADLOCK",      "Padlock views (off: hat targets)", 0.0f, 0.0f,  1.0f,   1.0f,  true },
	{ "FSO_VR_GAZE_TARGET",  "Look to target",                1.0f,    0.0f,  1.0f,   1.0f,  true },
};

float Values[COUNT];
bool Loaded = false;

float clampv(Id id, float v)
{
	return std::min(Table[id].max, std::max(Table[id].min, v));
}

SCP_string ini_path()
{
	return os_get_config_path("data/diaspora_vr_settings.ini");
}

void load()
{
	if (Loaded)
		return;
	Loaded = true;
	for (int i = 0; i < COUNT; i++) {
		float v = Table[i].def;
		if (const char* e = getenv(Table[i].env)) {
			// toggles used to be "1" / "0"; a positive number overrides the others
			if (Table[i].toggle)
				v = atoi(e) != 0 ? 1.0f : 0.0f;
			else if (atof(e) > 0.0 || (Table[i].min < 0.0f && e[0] != '\0'))
				v = static_cast<float>(atof(e));
		}
		Values[i] = clampv(static_cast<Id>(i), v);
	}
	if (FILE* f = fopen(ini_path().c_str(), "r")) {
		char line[256];
		while (fgets(line, sizeof(line), f)) {
			char* eq = strchr(line, '=');
			if (!eq)
				continue;
			*eq = '\0';
			for (int i = 0; i < COUNT; i++) {
				if (!strcmp(line, Table[i].env))
					Values[i] = clampv(static_cast<Id>(i), static_cast<float>(atof(eq + 1)));
			}
		}
		fclose(f);
	}
}

}  // namespace

const Info& info(Id id)
{
	return Table[id];
}

float get(Id id)
{
	load();
	return Values[id];
}

void set(Id id, float value)
{
	load();
	Values[id] = clampv(id, value);
}

void reset(Id id)
{
	load();
	Values[id] = Table[id].def;
}

void save()
{
	load();
	if (FILE* f = fopen(ini_path().c_str(), "w")) {
		for (int i = 0; i < COUNT; i++)
			fprintf(f, "%s=%g\n", Table[i].env, Values[i]);
		fclose(f);
	}
}

}  // namespace vrset

// ---------------------------------------------------------------------------------------------------------------------
// In-game settings screen: a modal loop like the game's popups, driven by the keyboard (arrows, Enter, R, Esc) and by
// the mouse / the VR menu laser (click a row, click < or >).

void vr_settings_menu()
{
	using namespace vrset;
	load();

	const int screen_id = gr_save_screen();
	const int old_w = gr_screen.max_w_unscaled, old_h = gr_screen.max_h_unscaled;
	const int old_wz = gr_screen.max_w_unscaled_zoomed, old_hz = gr_screen.max_h_unscaled_zoomed;
	gr_reset_screen_scale();

	UI_WINDOW win;
	win.create(0, 0, gr_screen.max_w_unscaled, gr_screen.max_h_unscaled, 0);

	int sel = 0;
	bool done = false;
	int top = 0;
	while (!done) {
		os_poll();
		game_set_frametime(-1);
		game_do_state_common(gameseq_get_state());
		gr_restore_screen(screen_id);

		const int k = win.process();
		const int W = gr_screen.max_w_unscaled, H = gr_screen.max_h_unscaled;

		// layout: a panel in the middle, scaled with the screen
		gr_set_font(font::FONT1);
		int cw = 0, ch = 0;
		gr_get_string_size(&cw, &ch, "M");
		const float s = std::max(1.0f, std::min(static_cast<float>(W) / 1024.0f, static_cast<float>(H) / 768.0f));
		const int rowh = static_cast<int>(ch * 1.9f * s);
		const int pw = std::min(W - 20, static_cast<int>(760.0f * s));
		const int px = (W - pw) / 2;
		const int visible = std::max(4, std::min(COUNT, (H - 6 * rowh) / rowh));
		const int ph = (visible + 4) * rowh;
		const int py = (H - ph) / 2;

		int mx = 0, my = 0;
		mouse_get_pos_unscaled(&mx, &my);
		const bool click = B1_JUST_RELEASED != 0;

		auto step = [&](int i, int dir) {
			const Info& in = Table[i];
			set(static_cast<Id>(i), in.toggle ? (get(static_cast<Id>(i)) >= 0.5f ? 0.0f : 1.0f) : get(static_cast<Id>(i)) + dir * in.step);
		};

		switch (k) {
		case KEY_UP: sel = (sel + COUNT - 1) % COUNT; break;
		case KEY_DOWN: sel = (sel + 1) % COUNT; break;
		case KEY_LEFT: step(sel, -1); break;
		case KEY_RIGHT: case KEY_ENTER: step(sel, 1); break;
		case KEY_R: reset(static_cast<Id>(sel)); break;
		case KEY_ESC: case KEY_F9: done = true; break;
		default: break;
		}

		if (sel < top) top = sel;
		if (sel >= top + visible) top = sel - visible + 1;

		// mouse / laser: click a row to select it, the left or right part of the value to change it
		if (click) {
			for (int r = 0; r < visible; r++) {
				const int ry = py + (r + 2) * rowh;
				if (my >= ry && my < ry + rowh && mx >= px && mx < px + pw) {
					sel = top + r;
					if (mx > px + pw * 6 / 10) {
						step(sel, mx > px + pw * 8 / 10 ? 1 : -1);
					}
				}
			}
			if (my >= py + (visible + 2) * rowh && my < py + (visible + 3) * rowh && mx >= px && mx < px + pw) {
				done = true;   // the "Save and close" line
			}
		}

		// draw
		color bg;
		gr_init_alphacolor(&bg, 6, 12, 20, 235, AC_TYPE_BLEND);
		gr_set_color_fast(&bg);
		gr_rect(px, py, pw, ph, GR_RESIZE_NONE);
		gr_set_color_fast(&Color_bright_white);
		gr_string(px + rowh / 2, py + rowh / 2, "VR SETTINGS", GR_RESIZE_NONE);
		for (int r = 0; r < visible; r++) {
			const int i = top + r;
			const int ry = py + (r + 2) * rowh;
			const Info& in = Table[i];
			const float v = get(static_cast<Id>(i));
			if (i == sel) {
				color hl;
				gr_init_alphacolor(&hl, 40, 80, 120, 150, AC_TYPE_BLEND);
				gr_set_color_fast(&hl);
				gr_rect(px + 4, ry, pw - 8, rowh - 2, GR_RESIZE_NONE);
			}
			gr_set_color_fast(i == sel ? &Color_bright_white : &Color_text_normal);
			gr_string(px + rowh / 2, ry + (rowh - ch) / 2, in.label, GR_RESIZE_NONE);
			char val[40];
			if (in.toggle)
				snprintf(val, sizeof(val), "%s", v >= 0.5f ? "On" : "Off");
			else if (in.step >= 1.0f)
				snprintf(val, sizeof(val), "%.0f", v);
			else
				snprintf(val, sizeof(val), "%.2f", v);
			char line[64];
			snprintf(line, sizeof(line), "<  %s  >", val);
			int lw = 0, lh = 0;
			gr_get_string_size(&lw, &lh, line);
			gr_string(px + pw * 8 / 10 - lw / 2 + 0, ry + (rowh - ch) / 2, line, GR_RESIZE_NONE);
		}
		gr_set_color_fast(&Color_text_selected);
		gr_string(px + rowh / 2, py + (visible + 2) * rowh + (rowh - ch) / 2, "Save and close  (Esc)", GR_RESIZE_NONE);
		gr_set_color_fast(&Color_text_normal);
		gr_string(px + rowh / 2, py + (visible + 3) * rowh + (rowh - ch) / 2,
				  "Up/Down: choose   Left/Right or click < >: change   R: reset the row", GR_RESIZE_NONE);

		gr_flip();
	}

	save();
	win.destroy();
	gr_set_screen_scale(old_w, old_h, old_wz, old_hz);
	gr_free_screen(screen_id);
}
