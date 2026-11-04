#pragma once

// Diaspora port: -sim runs a mission without graphics at a fixed 20 Hz virtual clock, flown by the AI (Player_use_ai), and
// writes a JSON result to $FSO_SIM_OUT when it ends (tools/sim_missions.py). Environment: FSO_SIM_LIMIT (mission seconds,
// default 2400), FSO_SIM_HP (hull multiplier for the player ship and its wing), FSO_SIM_PROTECT (hull multiplier for escort ships).
void sim_update(int gamestate);
