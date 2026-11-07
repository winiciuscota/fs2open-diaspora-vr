#include "graphics/cockpit_glass.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "bmpman/bmpman.h"
#include "decals/decals.h"
#include "globalincs/systemvars.h"
#include "graphics/2d.h"
#include "graphics/material.h"
#include "graphics/matrix.h"
#include "graphics/vr_settings.h"
#include "lighting/lighting.h"
#include "math/vecmat.h"
#include "model/model.h"
#include "object/object.h"
#include "weapon/weapon.h"
#include "render/3d.h"
#include "ship/ship.h"
#include "ship/shipfx.h"

extern void object_get_eye(vec3d* eye_pos, matrix* eye_orient, const object* obj, bool do_slew, bool local_pos, bool local_orient);

namespace {

const float Dome_radius = 0.6f;     // m around the eye point
const float Az_min = -112.5f, Az_max = 112.5f;      // degrees left/right of the nose
const float El_min = -15.0f, El_max = 90.0f;        // degrees below/above the horizon of the ship
const int Cols = 15, Rows = 7;      // 15 degree cells
const float Glint_power = 6.0f;
const float Gain = 1.6f;

int Scratch_bm = -2;

}

void cockpit_glass_render(object* viewer)
{
	static int early_logged = 0;
	auto early = [&](const char* why) {
		if (early_logged++ < 5) {
			mprintf(("CANOPY: not drawn (%s)\n", why));
		}
	};
	if (gr_screen.mode == GraphicsAPI::Stub || viewer == nullptr || viewer->type != OBJ_SHIP) {
		early("no ship viewer");
		return;
	}
	const float strength = vrset::get(vrset::CANOPY);
	if (strength <= 0.0f) {
		early("strength 0");
		return;
	}
	if (Viewer_mode & (VM_EXTERNAL | VM_CHASE | VM_OTHER_SHIP | VM_DEAD_VIEW | VM_FREECAMERA | VM_TOPDOWN | VM_WARP_CHASE | VM_CAMERA_LOCKED)) {
		early("viewer mode");
		return;
	}
	if (Scratch_bm == -2) {
		Scratch_bm = bm_load("canopy_scratch");
		mprintf(("CANOPY: bitmap canopy_scratch -> %d\n", Scratch_bm));
	}
	if (Scratch_bm < 0) {
		return;
	}

	// suns that reach the pilot
	vec3d suns[4];
	int n_suns = 0;
	const int n_lights = light_get_global_count();
	for (int i = 0; i < n_lights && n_suns < 4; i++) {
		if (!light_has_glare(i) || shipfx_eye_in_shadow(&Eye_position, viewer, i)) {
			continue;
		}
		if (light_get_global_dir(&suns[n_suns], i)) {
			vm_vec_normalize(&suns[n_suns]);
			n_suns++;
		}
	}
	static const bool fx_test = getenv("FSO_FX_TEST") != nullptr;
	if (fx_test) {
		// test aid: a sun straight ahead and a little above the nose
		vm_vec_scale_add(&suns[0], &Eye_matrix.vec.fvec, &Eye_matrix.vec.uvec, 0.3f);
		vm_vec_normalize(&suns[0]);
		n_suns = 1;
	}
	static int diag_frame = 0;
	if (++diag_frame % 600 == 1) {
		mprintf(("CANOPY: strength %.2f, suns reaching the pilot %d, texture %d, viewer mode %d\n", strength, n_suns, Scratch_bm, static_cast<int>(Viewer_mode)));
	}
	if (n_suns == 0) {
		return;
	}

	vec3d eye_local, anchor;
	matrix eye_orient;
	object_get_eye(&eye_local, &eye_orient, viewer, false, true, true);
	vm_vec_unrotate(&anchor, &eye_local, &viewer->orient);
	anchor += viewer->pos;

	// the dome is drawn in the cockpit's depth range (the main near plane would clip it)
	gr_end_proj_matrix();
	gr_end_view_matrix();
	gr_set_proj_matrix(Proj_fov, gr_screen.clip_aspect, Min_draw_distance_cockpit, Max_draw_distance);
	gr_set_view_matrix(&Eye_position, &Eye_matrix);

	int drawn = 0;
	for (int r = 0; r < Rows; r++) {
		for (int c = 0; c < Cols; c++) {
			const float az0 = Az_min + (Az_max - Az_min) * c / Cols, az1 = Az_min + (Az_max - Az_min) * (c + 1) / Cols;
			const float el0 = El_min + (El_max - El_min) * r / Rows, el1 = El_min + (El_max - El_min) * (r + 1) / Rows;
			const float az[4] = { az0, az1, az1, az0 }, el[4] = { el0, el0, el1, el1 };

			vertex v[4];
			memset(v, 0, sizeof(v));
			vec3d centre = ZERO_VECTOR;
			for (int k = 0; k < 4; k++) {
				const float a = fl_radians(az[k]), e = fl_radians(el[k]);
				vec3d local, world;
				local.xyz.x = sinf(a) * cosf(e) * Dome_radius;
				local.xyz.y = sinf(e) * Dome_radius;
				local.xyz.z = cosf(a) * cosf(e) * Dome_radius;
				vm_vec_unrotate(&world, &local, &viewer->orient);
				world += anchor;
				g3_transfer_vertex(&v[k], &world);
				v[k].texture_position.u = (az[k] - Az_min) / (Az_max - Az_min);
				v[k].texture_position.v = 1.0f - (el[k] - El_min) / (El_max - El_min);
				centre += world;
			}
			vm_vec_scale(&centre, 0.25f);

			vec3d ray;
			vm_vec_sub(&ray, &centre, &Eye_position);
			vm_vec_normalize(&ray);
			float glint = 0.0f;
			for (int s = 0; s < n_suns; s++) {
				glint += powf(std::max(vm_vec_dot(&ray, &suns[s]), 0.0f), Glint_power);
			}
			const float alpha = glint * Gain * strength;
			if (alpha < 0.004f) {
				continue;
			}

			material mat;
			material_set_unlit(&mat, Scratch_bm, alpha, true, false);
			mat.set_color(alpha, alpha * 0.93f, alpha * 0.82f, 1.0f);
			g3_render_primitives_textured(&mat, v, 4, PRIM_TYPE_TRIFAN, false);
			drawn++;
		}
	}
	if (diag_frame % 600 == 1) {
		mprintf(("CANOPY: %d cells drawn\n", drawn));
	}

	gr_end_proj_matrix();
	gr_end_view_matrix();
	gr_set_proj_matrix(Proj_fov, gr_screen.clip_aspect, Min_draw_distance, Max_draw_distance);
	gr_set_view_matrix(&Eye_position, &Eye_matrix);
}

// FSO_FX_TEST=1: every two seconds fire a "virtual shot" from the player at the nearest other ship and add the impact decal of
// the next weapon that has one, through the same call as a real hit (collideshipweapon.cpp).
void fx_test_frame(object* viewer)
{
	static const bool fx_test = getenv("FSO_FX_TEST") != nullptr;
	if (!fx_test || viewer == nullptr || viewer->type != OBJ_SHIP) {
		return;
	}
	static int frame = 0;
	static int next_weapon = 0;
	if (++frame % 120 != 0) {
		return;
	}

	object* best = nullptr;
	float best_dist = 6000.0f;
	for (auto so : list_range(&Ship_obj_list)) {
		object* o = &Objects[so->objnum];
		if (o == viewer || o->flags[Object::Object_Flags::Should_be_dead]) {
			continue;
		}
		const float d = vm_vec_dist(&o->pos, &viewer->pos);
		if (d < 6000.0f && (best == nullptr || o->radius / d > best->radius / best_dist)) {
			best_dist = d;
			best = o;
		}
	}
	if (best == nullptr) {
		mprintf(("FXTEST: no ship within 6 km\n"));
		return;
	}

	ship* shipp = &Ships[best->instance];
	ship_info* sip = &Ship_info[shipp->ship_info_index];
	auto pmi = model_get_instance(shipp->model_instance_num);
	auto pm = model_get(pmi->model_num);

	vec3d p0 = viewer->pos, p1 = best->pos;
	mc_info mc;
	mc.model_instance_num = shipp->model_instance_num;
	mc.model_num = sip->model_num;
	mc.submodel_num = -1;
	mc.orient = &best->orient;
	mc.pos = &best->pos;
	mc.p0 = &p0;
	mc.p1 = &p1;
	mc.lod = sip->collision_lod;
	mc.flags = MC_CHECK_MODEL | MC_CHECK_RAY;
	if (!model_collide(&mc)) {
		mprintf(("FXTEST: ray at '%s' (%.0f m) missed\n", shipp->ship_name, best_dist));
		return;
	}

	// next weapon that has an impact decal
	const int n = static_cast<int>(Weapon_info.size());
	const weapon_info* wip = nullptr;
	for (int i = 0; i < n && wip == nullptr; i++) {
		const weapon_info* w = &Weapon_info[(next_weapon + i) % n];
		if (w->impact_decal.definition_handle >= 0) {
			wip = w;
			next_weapon = (next_weapon + i + 1) % n;
		}
	}
	if (wip == nullptr) {
		mprintf(("FXTEST: no weapon has an impact decal\n"));
		return;
	}

	vec3d up;
	model_instance_global_to_local_dir(&up, &viewer->orient.vec.uvec, pm, pmi, mc.hit_submodel, &best->orient);
	matrix decal_orient;
	vm_vector_2_matrix_norm(&decal_orient, &mc.hit_normal, &up);
	mprintf(("FXTEST: decal of '%s' on '%s' (%.0f m) submodel %d\n", wip->name, shipp->ship_name, best_dist, mc.hit_submodel));
	decals::creation_info info = wip->impact_decal;
	info.radius = ::util::UniformFloatRange(best->radius * 0.04f);	// visible from a distance
	decals::addDecal(info, best, mc.hit_submodel, mc.hit_point, decal_orient);
}
