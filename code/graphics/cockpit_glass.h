#pragma once

class object;

// Diaspora port: scratches and dust on the canopy glass. A dome of quads around the pilot's eye point, fixed to the ship (so
// moving the head in VR shifts the scratches against the world), drawn additively with the map "canopy_scratch" and lit only
// by the sun: each cell brightens when its line of sight from the eye runs close to the direction of a sun.
// Strength: vrset::CANOPY (FSO_VR_CANOPY). Call after the cockpit has been drawn, with the world view and projection set.
void cockpit_glass_render(object* viewer);
