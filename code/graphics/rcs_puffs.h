#pragma once

// Diaspora port: visible cold-gas manoeuvring thrusters. When a fighter is turning (its commanded rotation differs from its actual
// rotation) the nozzles around the hull that would produce that torque puff short bursts of gas that stay behind in space.
// Runs outside the particle system (own list, additive billboards, texture "rcs_puff").
// rcs_puffs_frame: once per simulation frame. rcs_puffs_render: every render pass, with the world view and projection set.
void rcs_puffs_frame(float frametime);
void rcs_puffs_render();
void rcs_puffs_clear();
