#pragma once

class object;

// Diaspora port: scratches and dust on the canopy glass. A dome of quads around the pilot's eye point, fixed to the ship (so
// moving the head in VR shifts the scratches against the world), drawn additively with the map "canopy_scratch" and lit only
// by the sun: each cell brightens when its line of sight from the eye runs close to the direction of a sun.
// Strength: vrset::CANOPY (FSO_VR_CANOPY). Call after the cockpit has been drawn, with the world view and projection set.
void cockpit_glass_render(object* viewer);

// FSO_FX_TEST=1 only: adds impact decals on the nearest ship every two seconds (and forces a sun for the canopy glass)
void fx_test_frame(object* viewer);

// A weapon blew up at pos (flak burst, missile hit): its light reaches the canopy scratches for half a second. size ~ metres of blast.
struct vec3d;
void cockpit_glass_flash(const vec3d* pos, float size);
