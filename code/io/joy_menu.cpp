#include "io/joy_menu.h"

#include "globalincs/pstypes.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include <SDL3/SDL.h>

#include "controlconfig/controlsconfig.h"
#include "gamesequence/gamesequence.h"
#include "io/joy.h"
#include "io/key.h"
#include "osapi/osapi.h"

namespace {

const float DEADZONE = 0.14f;
const float CURSOR_SPEED = 0.75f;   // screen widths per second at full deflection
const float REPEAT_DELAY = 0.40f, REPEAT_RATE = 0.11f;

bool enabled()
{
	static int on = -1;
	if (on < 0) {
		const char* v = getenv("FSO_JOY_MENU");
		on = (v != nullptr && atoi(v) == 0 && v[0] == '0') ? 0 : 1;
	}
	return on == 1;
}

// joystick button bound to a control action (hat positions are 128 and up, not buttons), or the default
int bound_joy_button(int action, int fallback)
{
	if (action < 0 || static_cast<size_t>(action) >= Control_config.size())
		return fallback;
	for (const CC_bind* b : { &Control_config[action].first, &Control_config[action].second }) {
		if (b->get_cid() >= CID_JOY0 && b->get_cid() < CID_JOY_MAX && b->get_btn() >= 0 && b->get_btn() < 128)
			return b->get_btn();
	}
	return fallback;
}

io::joystick::Joystick* pick_stick()
{
	for (size_t i = 0; i < io::joystick::getJoystickCount(); ++i) {
		auto* j = io::joystick::getJoystick(i);
		if (j == nullptr || !j->isAttached() || j->numAxes() < 2)
			continue;
		if (strstr(j->getName().c_str(), "OpenXR") != nullptr)	// the VR controllers already have their own pointer
			continue;
		return j;
	}
	return nullptr;
}

float axis_value(io::joystick::Joystick* j, int idx)
{
	float v = static_cast<float>(j->getAxis(idx)) / 32767.0f;
	const float a = fabsf(v);
	if (a < DEADZONE)
		return 0.0f;
	const float s = (a - DEADZONE) / (1.0f - DEADZONE);
	return (v < 0 ? -1.0f : 1.0f) * s * s;
}

void click(SDL_Window* window, int button, bool down, float x, float y)
{
	SDL_Event b;
	SDL_zero(b);
	b.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
	b.button.windowID = SDL_GetWindowID(window);
	b.button.button = static_cast<Uint8>(button);
	b.button.down = down;
	b.button.clicks = 1;
	b.button.x = x;
	b.button.y = y;
	SDL_PushEvent(&b);
}

}  // namespace

void joy_menu_update(int gamestate)
{
	static Uint64 last_ticks = 0;
	static bool left_down = false, right_down = false;
	static int hat_key = 0, key_pressed = 0;
	static float hat_held = 0.0f, hat_next = 0.0f;

	const Uint64 now = SDL_GetTicks();
	const float dt = last_ticks == 0 ? 0.0f : std::min(0.1f, static_cast<float>(now - last_ticks) / 1000.0f);
	last_ticks = now;

	SDL_Window* window = os::getSDLMainWindow();
	const bool menu = gamestate != GS_STATE_GAME_PLAY && gamestate != GS_STATE_INVALID;
	io::joystick::Joystick* stick = (enabled() && menu && window != nullptr) ? pick_stick() : nullptr;

	if (key_pressed != 0) {		// release last frame's key press
		key_mark(key_pressed, 0, 0);
		key_pressed = 0;
	}

	if (stick == nullptr) {
		hat_key = 0;
		if (left_down || right_down) {		// leave the flight with no click stuck
			float mx = 0, my = 0;
			SDL_GetMouseState(&mx, &my);
			if (window != nullptr) {
				if (left_down) click(window, SDL_BUTTON_LEFT, false, mx, my);
				if (right_down) click(window, SDL_BUTTON_RIGHT, false, mx, my);
			}
			left_down = right_down = false;
		}
		return;
	}

	int ww = 0, wh = 0;
	SDL_GetWindowSize(window, &ww, &wh);

	// SDL's own mouse state does not follow the events pushed below, so the stick keeps its own cursor position and only
	// follows the real mouse (or the VR pointer) when that one moves
	static bool cursor_init = false;
	static float vx = 0, vy = 0, last_rx = -1, last_ry = -1;
	float rx = 0, ry = 0;
	SDL_GetMouseState(&rx, &ry);
	if (!cursor_init || rx != last_rx || ry != last_ry) {
		vx = rx;
		vy = ry;
		cursor_init = true;
	}
	float mx = vx, my = vy;

	const float ax = axis_value(stick, 0), ay = axis_value(stick, 1);
	if (ax != 0.0f || ay != 0.0f) {
		const float dx = ax * CURSOR_SPEED * static_cast<float>(ww) * dt;
		const float dy = ay * CURSOR_SPEED * static_cast<float>(ww) * dt;
		const float nx = std::max(0.0f, std::min(static_cast<float>(ww - 1), mx + dx));
		const float ny = std::max(0.0f, std::min(static_cast<float>(wh - 1), my + dy));
		SDL_Event ev;
		SDL_zero(ev);
		ev.type = SDL_EVENT_MOUSE_MOTION;
		ev.motion.windowID = SDL_GetWindowID(window);
		ev.motion.x = nx;
		ev.motion.y = ny;
		ev.motion.xrel = nx - mx;
		ev.motion.yrel = ny - my;
		SDL_PushEvent(&ev);
		mx = nx;
		my = ny;
	}
	vx = mx;
	vy = my;
	SDL_GetMouseState(&last_rx, &last_ry);

	// the button bound to "fire primary" clicks, the one bound to "fire secondary" is Escape (the X52 defaults are 0 and 1)
	static io::joystick::Joystick* logged = nullptr;
	static int btn_click = 0, btn_esc = 1;
	if (logged != stick) {
		logged = stick;
		btn_click = bound_joy_button(FIRE_PRIMARY, 0);
		btn_esc = bound_joy_button(FIRE_SECONDARY, 1);
		mprintf(("Joystick menu: using '%s' (%d axes, %d buttons, %d hats), click = button %d, escape = button %d\n", stick->getName().c_str(),
			stick->numAxes(), stick->numButtons(), stick->numHats(), btn_click, btn_esc));
	}
	static bool esc_down = false;
	const bool trig = btn_click >= 0 && btn_click < stick->numButtons() && stick->isButtonDown(btn_click);
	const bool esc = btn_esc >= 0 && btn_esc < stick->numButtons() && stick->isButtonDown(btn_esc);
	if (trig != left_down) {
		left_down = trig;
		click(window, SDL_BUTTON_LEFT, trig, mx, my);
	}
	if (esc && !esc_down) {
		key_mark(KEY_ESC, 1, 0);
		key_pressed = (key_pressed == 0) ? KEY_ESC : key_pressed;
	}
	esc_down = esc;

	// hat = arrow keys, with key repeat
	int want = 0;
	if (stick->numHats() > 0) {
		switch (stick->getHatPosition(0)) {
		case io::joystick::HAT_UP: want = KEY_UP; break;
		case io::joystick::HAT_DOWN: want = KEY_DOWN; break;
		case io::joystick::HAT_LEFT: want = KEY_LEFT; break;
		case io::joystick::HAT_RIGHT: want = KEY_RIGHT; break;
		default: break;
		}
	}
	if (want == 0) {
		hat_key = 0;
	} else if (want != hat_key) {
		hat_key = want;
		hat_held = 0.0f;
		hat_next = REPEAT_DELAY;
		key_mark(want, 1, 0);
		key_pressed = want;
	} else {
		hat_held += dt;
		if (hat_held >= hat_next) {
			hat_next += REPEAT_RATE;
			key_mark(want, 1, 0);
			key_pressed = want;
		}
	}
}
