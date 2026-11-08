#include "graphics/cockpit_glass.h"
#include "graphics/cockpit_glass_data.h"

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

const float Az_min = -112.5f, Az_max = 112.5f;      // degrees left/right of the nose
const float El_min = -6.0f, El_max = 90.0f;         // degrees below/above the eye level
const int Cols = 45, Rows = 16;                     // 5 / 6 degree cells
const float Glint_power = 6.0f;
const float Gain = 1.6f;

// The canopy glass: the triangles of the glass in the ships' own models (cockpit_glass_data.h, eye-relative). The dome vertices are
// the points where rays from the eye leave the glass; cells whose corners do not all hit the glass are not drawn.
float ray_triangle(const vec3d& dir, const float* t)
{
	const float e1[3] = { t[3] - t[0], t[4] - t[1], t[5] - t[2] }, e2[3] = { t[6] - t[0], t[7] - t[1], t[8] - t[2] };
	const float d[3] = { dir.xyz.x, dir.xyz.y, dir.xyz.z };
	const float p[3] = { d[1] * e2[2] - d[2] * e2[1], d[2] * e2[0] - d[0] * e2[2], d[0] * e2[1] - d[1] * e2[0] };
	const float det = e1[0] * p[0] + e1[1] * p[1] + e1[2] * p[2];
	if (fabsf(det) < 1e-8f) {
		return -1.0f;
	}
	const float inv = 1.0f / det;
	const float s[3] = { -t[0], -t[1], -t[2] };	// ray origin (the eye) minus the first vertex
	const float u = (s[0] * p[0] + s[1] * p[1] + s[2] * p[2]) * inv;
	if (u < 0.0f || u > 1.0f) {
		return -1.0f;
	}
	const float q[3] = { s[1] * e1[2] - s[2] * e1[1], s[2] * e1[0] - s[0] * e1[2], s[0] * e1[1] - s[1] * e1[0] };
	const float v = (d[0] * q[0] + d[1] * q[1] + d[2] * q[2]) * inv;
	if (v < 0.0f || u + v > 1.0f) {
		return -1.0f;
	}
	return (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]) * inv;
}

float glass_distance(const CockpitGlass& g, const vec3d& dir)
{
	float best = -1.0f;
	for (int i = 0; i < g.n_tris; i++) {
		const float t = ray_triangle(dir, g.v + i * 9);
		if (t > 0.02f && (best < 0.0f || t > best)) {	// the far wall of the bubble: the glass seen from inside
			best = t;
		}
	}
	return best;
}

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
	if (!ship_render_player_cockpit_active() || (Viewer_mode & (VM_EXTERNAL | VM_CHASE | VM_OTHER_SHIP | VM_DEAD_VIEW | VM_FREECAMERA | VM_TOPDOWN | VM_WARP_CHASE))) {
		if (early_logged < 5) {
			mprintf(("CANOPY: viewer mode is %d\n", static_cast<int>(Viewer_mode)));
		}
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

	const ship_info* sip = &Ship_info[Ships[viewer->instance].ship_info_index];
	const CockpitGlass* glass = nullptr;
	for (const auto& g : Cockpit_glass) {
		if (stricmp(g.cockpit_pof, sip->cockpit_pof_file) == 0) {
			glass = &g;
		}
	}
	if (glass == nullptr) {
		early("no glass data for this cockpit");
		return;
	}
	// distance from the eye to the glass along every dome direction, computed once per cockpit
	static const CockpitGlass* grid_for = nullptr;
	static float grid[Rows + 1][Cols + 1];
	if (grid_for != glass) {
		for (int r = 0; r <= Rows; r++) {
			for (int c = 0; c <= Cols; c++) {
				const float a = fl_radians(Az_min + (Az_max - Az_min) * c / Cols), e = fl_radians(El_min + (El_max - El_min) * r / Rows);
				vec3d dir;
				dir.xyz.x = sinf(a) * cosf(e);
				dir.xyz.y = sinf(e);
				dir.xyz.z = cosf(a) * cosf(e);
				grid[r][c] = glass_distance(*glass, dir);
			}
		}
		grid_for = glass;
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
			if (grid[r][c] < 0.0f || grid[r][c + 1] < 0.0f || grid[r + 1][c] < 0.0f || grid[r + 1][c + 1] < 0.0f) {
				continue;	// not all of this cell is on the glass
			}
			const float cell_d[4] = { grid[r][c], grid[r][c + 1], grid[r + 1][c + 1], grid[r + 1][c] };
			vec3d centre = ZERO_VECTOR;
			for (int k = 0; k < 4; k++) {
				const float a = fl_radians(az[k]), e = fl_radians(el[k]);
				vec3d local, world, dir;
				dir.xyz.x = sinf(a) * cosf(e);
				dir.xyz.y = sinf(e);
				dir.xyz.z = cosf(a) * cosf(e);
				vm_vec_copy_scale(&local, &dir, cell_d[k]);
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
			material_set_unlit(&mat, Scratch_bm, alpha, true, true);	// depth-tested: the dash and the frame hide the scratches behind them
			mat.set_blend_mode(ALPHA_BLEND_ADDITIVE);
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
		vec3d dir;
		vm_vec_sub(&dir, &o->pos, &viewer->pos);
		vm_vec_normalize(&dir);
		// something big in front of the nose
		if (d < 6000.0f && o->radius > 30.0f && vm_vec_dot(&dir, &viewer->orient.vec.fvec) > 0.85f && (best == nullptr || d < best_dist)) {
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
	mprintf(("FXTEST: decal system active %d, radius %.1f\n", static_cast<int>(decals::decalSystemActive()), best->radius * 0.04f));
}
