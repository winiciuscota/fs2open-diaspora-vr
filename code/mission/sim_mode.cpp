#include "mission/sim_mode.h"

#include <cstdio>
#include <cstdlib>
#include <set>
#include <unistd.h>

#include "cmdline/cmdline.h"
#include "io/timer.h"
#include "gamesequence/gamesequence.h"
#include "globalincs/systemvars.h"
#include "mission/missiongoals.h"
#include "mission/missionparse.h"
#include "missionui/missionscreencommon.h"
#include "object/object.h"
#include "ship/ship.h"

namespace {

float env_float(const char* name, float fallback)
{
	const char* v = getenv(name);
	return (v != nullptr && atof(v) > 0.0) ? static_cast<float>(atof(v)) : fallback;
}

void json_str(FILE* f, const char* s)
{
	fputc('"', f);
	for (; *s; ++s) {
		if (*s == '"' || *s == '\\')
			fputc('\\', f);
		fputc(*s, f);
	}
	fputc('"', f);
}

void scale_hull(object* objp, ship* shipp, float k)
{
	objp->hull_strength *= k;
	shipp->ship_max_hull_strength *= k;
}

// the player's ship and wing get FSO_SIM_HP, escort ships FSO_SIM_PROTECT, each ship once
void apply_multipliers()
{
	static std::set<int> done;
	const float hp = env_float("FSO_SIM_HP", 1.0f), prot = env_float("FSO_SIM_PROTECT", 1.0f);
	if (hp == 1.0f && prot == 1.0f)
		return;
	for (auto& e : Ship_registry) {
		if (!e.has_shipp() || !e.has_objp() || done.count(e.objnum) > 0)
			continue;
		ship* sp = e.shipp();
		object* op = e.objp();
		done.insert(e.objnum);
		const bool mine = (sp == Player_ship) || (Player_ship != nullptr && sp->wingnum >= 0 && sp->wingnum == Player_ship->wingnum);
		if (mine && hp != 1.0f)
			scale_hull(op, sp, hp);
		else if (sp->flags[Ship::Ship_Flags::Escort] && prot != 1.0f)
			scale_hull(op, sp, prot);
	}
}

void write_report(const char* result)
{
	const char* path = getenv("FSO_SIM_OUT");
	FILE* f = path ? fopen(path, "w") : stdout;
	if (f == nullptr)
		return;
	fprintf(f, "{\"mission\": ");
	json_str(f, The_mission.name);
	fprintf(f, ", \"result\": \"%s\", \"time\": %.1f, \"hp\": %.2f, \"protect\": %.2f,\n \"goals\": [", result, f2fl(Missiontime),
		env_float("FSO_SIM_HP", 1.0f), env_float("FSO_SIM_PROTECT", 1.0f));
	bool first = true;
	for (auto& g : Mission_goals) {
		fprintf(f, "%s{\"name\": ", first ? "" : ", ");
		json_str(f, g.name.c_str());
		fprintf(f, ", \"type\": %d, \"status\": %d}", g.type & GOAL_TYPE_MASK, g.satisfied);
		first = false;
	}
	fprintf(f, "],\n \"destroyed\": [");
	first = true;
	for (auto& x : Ships_exited) {
		if (!x.flags[Ship::Exit_Flags::Destroyed])
			continue;
		fprintf(f, "%s{\"name\": ", first ? "" : ", ");
		json_str(f, x.ship_name);
		fprintf(f, ", \"team\": %d, \"t\": %.1f}", x.team, f2fl(x.time));
		first = false;
	}
	fprintf(f, "],\n \"present\": [");
	first = true;
	for (auto& e : Ship_registry) {
		if (!e.has_shipp() || !e.has_objp())
			continue;
		fprintf(f, "%s{\"name\": ", first ? "" : ", ");
		json_str(f, e.name);
		fprintf(f, ", \"team\": %d, \"hull\": %.2f}", e.shipp()->team, e.objp()->hull_strength / e.shipp()->ship_max_hull_strength);
		first = false;
	}
	fprintf(f, "]}\n");
	if (path)
		fclose(f);
	else
		fflush(f);
}

[[noreturn]] void finish(const char* result)
{
	write_report(result);
	_exit(0);
}

}  // namespace

extern bool Player_use_ai;

void sim_update(int state)
{
	if (!Cmdline_sim)
		return;

	static int frames_in_menu = 0;
	switch (state) {
	case GS_STATE_BRIEFING:
	case GS_STATE_SHIP_SELECT:
	case GS_STATE_WEAPON_SELECT:
		if (++frames_in_menu % 10 == 3)		// try again until the loadout is accepted
			commit_pressed(true);
		if (frames_in_menu > 400)
			_exit(3);
		break;
	case GS_STATE_GAME_PLAY:
		Player_use_ai = (getenv("FSO_SIM_IDLE") == nullptr);	// FSO_SIM_IDLE: the player sits still
		apply_multipliers();
		if (f2fl(Missiontime) > env_float("FSO_SIM_LIMIT", 2400.0f))
			finish("timeout");
		break;
	case GS_STATE_DEBRIEF:
		finish(mission_evaluate_primary_goals() == PRIMARY_GOALS_COMPLETE ? "win" : "fail");
	case GS_STATE_DEATH_DIED:
	case GS_STATE_DEATH_BLEW_UP:
		finish("player_died");
	case GS_STATE_MAIN_MENU:
		if (f2fl(timer_get_approx_seconds()) > 120.0f)
			_exit(4);
		break;
	default:
		break;
	}
}
