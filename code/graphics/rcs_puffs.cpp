#include "graphics/rcs_puffs.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "bmpman/bmpman.h"
#include "globalincs/systemvars.h"
#include "graphics/2d.h"
#include "graphics/material.h"
#include "graphics/vr_settings.h"
#include "io/timer.h"
#include "math/vecmat.h"
#include "model/model.h"
#include "object/object.h"
#include "physics/physics.h"
#include "render/3d.h"
#include "ship/ship.h"

namespace {

struct Puff {
	vec3d pos, vel;
	float age, life, rad;
};

struct Nozzle {
	vec3d pos;	// local, relative to the model's centre
	vec3d dir;	// exhaust direction, local
};

std::vector<Puff> Puffs;
int Puff_bm = -2;

const size_t Max_puffs = 1500;
const float Max_ship_radius = 90.0f;	// fighters and shuttles; capital ships have no visible gas thrusters
const float Max_distance = 1500.0f;
const int Max_nozzles = 32;

void emit(const vec3d& world_pos, const vec3d& world_dir, const vec3d& ship_vel, float ship_radius)
{
	if (Puffs.size() >= Max_puffs) {
		return;
	}
	Puff p;
	p.pos = world_pos;
	p.vel = ship_vel + world_dir * (28.0f + frand() * 14.0f);	// a jet, not a drift
	p.vel.xyz.x += (frand() - 0.5f) * 3.0f;
	p.vel.xyz.y += (frand() - 0.5f) * 3.0f;
	p.vel.xyz.z += (frand() - 0.5f) * 3.0f;
	p.age = 0.0f;
	p.life = 0.28f + frand() * 0.22f;
	p.rad = (0.22f + frand() * 0.14f) * std::min(std::max(ship_radius * 0.12f, 1.0f), 3.0f);
	Puffs.push_back(p);
}

}

void rcs_puffs_clear()
{
	Puffs.clear();
}

void rcs_puffs_frame(float frametime)
{
	if (gr_screen.mode == GraphicsAPI::Stub || frametime <= 0.0f) {
		return;
	}

	for (auto& p : Puffs) {
		p.age += frametime;
		p.pos += p.vel * frametime;
		vm_vec_scale(&p.vel, std::max(0.0f, 1.0f - 4.0f * frametime));	// gas thins out and stops
	}
	Puffs.erase(std::remove_if(Puffs.begin(), Puffs.end(), [](const Puff& p) { return p.age >= p.life; }), Puffs.end());

	const float strength = vrset::get(vrset::THRUSTERS);
	if (strength <= 0.0f) {
		return;
	}

	for (auto so : list_range(&Ship_obj_list)) {
		object* objp = &Objects[so->objnum];
		if (objp->flags[Object::Object_Flags::Should_be_dead] || objp->radius > Max_ship_radius) {
			continue;
		}
		ship* shipp = &Ships[objp->instance];
		if (shipp->flags[Ship::Ship_Flags::Dying]) {
			continue;
		}
		if (vm_vec_dist(&objp->pos, &Eye_position) > Max_distance) {
			continue;
		}

		const physics_info* pi = &objp->phys_info;
		// the angular velocity change the controls ask for, in the ship's own axes
		vec3d want;
		vm_vec_sub(&want, &pi->desired_rotvel, &pi->rotvel);
		static const bool fx_test = getenv("FSO_FX_TEST") != nullptr;
		if (fx_test) {
			// test aid: every ship keeps asking for a turn that changes direction twice a second
			const float t = f2fl(Missiontime);
			want.xyz.x = sinf(t * 3.1f) * pi->max_rotvel.xyz.x;
			want.xyz.y = cosf(t * 2.3f) * pi->max_rotvel.xyz.y;
			want.xyz.z = 0.0f;
		}
		const vec3d maxrot = pi->max_rotvel;
		vec3d norm;
		norm.xyz.x = want.xyz.x / std::max(maxrot.xyz.x, 0.1f);
		norm.xyz.y = want.xyz.y / std::max(maxrot.xyz.y, 0.1f);
		norm.xyz.z = want.xyz.z / std::max(maxrot.xyz.z, 0.1f);
		const float demand = vm_vec_mag(&norm);
		if (demand < 0.12f) {
			continue;
		}

		const polymodel* pm = model_get(Ship_info[shipp->ship_info_index].model_num);
		if (pm == nullptr) {
			continue;
		}
		const vec3d centre = (pm->mins + pm->maxs) * 0.5f;
		const vec3d lo = pm->mins, hi = pm->maxs;
		const float hx = (hi.xyz.x - lo.xyz.x) * 0.5f, hy = (hi.xyz.y - lo.xyz.y) * 0.5f, hz = (hi.xyz.z - lo.xyz.z) * 0.5f;

		// nozzle clusters around the hull: four at the nose, four at the tail (each pair pointing sideways / up-down), the wingtips
		// and a pair on the flanks, so a turn is seen to come from several places
		Nozzle nozzles[Max_nozzles];
		int n = 0;
		auto add = [&](float x, float y, float z, float dx, float dy, float dz) {
			if (n < Max_nozzles) {
				vm_vec_make(&nozzles[n].pos, x, y, z);
				vm_vec_make(&nozzles[n].dir, dx, dy, dz);
				n++;
			}
		};
		for (float sx : { -1.0f, 1.0f }) {
			for (float sy : { -1.0f, 1.0f }) {
				add(sx * hx * 0.3f, sy * hy * 0.35f, hz * 0.8f, sx, 0.0f, 0.0f);
				add(sx * hx * 0.3f, sy * hy * 0.35f, hz * 0.8f, 0.0f, sy, 0.0f);
				add(sx * hx * 0.45f, sy * hy * 0.5f, -hz * 0.85f, sx, 0.0f, 0.0f);
				add(sx * hx * 0.45f, sy * hy * 0.5f, -hz * 0.85f, 0.0f, sy, 0.0f);
				add(sx * hx * 0.95f, 0.0f, -hz * 0.3f + sy * hz * 0.25f, 0.0f, sy, 0.0f);
				add(sx * hx * 0.6f, sy * hy * 0.8f, hz * 0.1f, sx, 0.0f, 0.0f);
			}
		}

		// turn the wanted rotation into the direction every nozzle position would move
		angles ang;
		ang.p = want.xyz.x * 0.05f;
		ang.h = want.xyz.y * 0.05f;
		ang.b = want.xyz.z * 0.05f;
		matrix rot;
		vm_angles_2_matrix(&rot, &ang);

		float score[Max_nozzles];
		float best = 0.0f;
		for (int i = 0; i < n; i++) {
			vec3d moved, move;
			vm_vec_unrotate(&moved, &nozzles[i].pos, &rot);
			vm_vec_sub(&move, &moved, &nozzles[i].pos);
			const float len = vm_vec_mag(&move);
			score[i] = len > 1e-5f ? -vm_vec_dot(&nozzles[i].dir, &move) / len * len : 0.0f;	// exhaust opposite the push
			best = std::max(best, score[i]);
		}
		if (best <= 0.0f) {
			continue;
		}

		const float rate = std::min(demand, 1.5f) * 90.0f * strength;	// puffs per second per active nozzle
		for (int i = 0; i < n; i++) {
			if (score[i] < best * 0.5f || frand() > rate * frametime) {
				continue;
			}
			vec3d lp = nozzles[i].pos + centre, world_pos, world_dir;
			lp.xyz.x += (frand() - 0.5f) * 0.12f;
			lp.xyz.y += (frand() - 0.5f) * 0.12f;
			lp.xyz.z += (frand() - 0.5f) * 0.12f;
			vm_vec_unrotate(&world_pos, &lp, &objp->orient);
			world_pos += objp->pos;
			vm_vec_unrotate(&world_dir, &nozzles[i].dir, &objp->orient);
			emit(world_pos, world_dir, objp->phys_info.vel, objp->radius);
		}
	}
}

void rcs_puffs_render()
{
	if (Puffs.empty() || gr_screen.mode == GraphicsAPI::Stub) {
		return;
	}
	if (Puff_bm == -2) {
		Puff_bm = bm_load("rcs_puff");
	}
	if (Puff_bm < 0) {
		return;
	}
	for (const auto& p : Puffs) {
		const float t = p.age / p.life;
		const float alpha = 0.75f * (1.0f - t) * (1.0f - t);
		vertex v;
		memset(&v, 0, sizeof(v));
		g3_transfer_vertex(&v, &p.pos);
		material mat;
		material_set_unlit(&mat, Puff_bm, alpha, true, true);
		mat.set_color(alpha * 0.85f, alpha * 0.93f, alpha, 1.0f);
		// keep puffs visible from a distance: grow with the distance to the eye
		const float far_scale = std::min(std::max(vm_vec_dist(&p.pos, &Eye_position) / 80.0f, 1.0f), 4.0f);
		g3_render_rect_screen_aligned(&mat, &v, 0, p.rad * (1.0f + t * 2.5f) * far_scale, 0.0f);
	}
}
