#pragma once

#include "graphics/2d.h"
#include "render/3d.h"

extern transform_stack gr_model_matrix_stack;
extern matrix4 gr_view_matrix;
extern matrix4 gr_model_view_matrix;
extern matrix4 gr_projection_matrix;
extern matrix4 gr_last_projection_matrix;
extern matrix4 gr_env_texture_matrix;
extern float gr_near_plane;

void gr_matrix_on_frame();

void gr_start_instance_matrix(const vec3d* offset, const matrix* rotation);
void gr_start_angles_instance_matrix(const vec3d* pos, const angles* rotation);
void gr_end_instance_matrix();

void gr_set_proj_matrix(fov_t fov, float aspect, float z_near, float z_far);
void gr_end_proj_matrix();

// Orthographic projection override... when active gr_set_proj_matrix substitutes an
// orthographic frustum sized to match the perspective view at the given camera distance.
void gr_activate_ortho_proj_override(float camera_distance);
void gr_deactivate_ortho_proj_override();

void gr_set_view_matrix(const vec3d* pos, const matrix* orient);
void gr_end_view_matrix();

// VR world-fixed HUD: while Gr_world_hud_active is set, gr_set_2d_matrix() maps the 2D canvas onto a plane in the 3D
// world (canvas_to_world) as seen through the given eye view/projection, instead of using a screen-space ortho matrix.
extern bool Gr_world_hud_active;
extern float Gr_world_hud_line_scale;	// multiplies 2D stroke widths while the world-fixed HUD is drawn (1px lines shimmer there)
void gr_set_world_hud(const matrix4& eye_view, const matrix4& eye_proj, const matrix4& canvas_to_world);
void gr_clear_world_hud();
// proj * view * canvas_to_world of the active world-fixed HUD (used by the NanoVG vertex shader)
void gr_get_world_hud_matrix(matrix4* out);

void gr_set_2d_matrix(/*int x, int y, int w, int h*/);
void gr_end_2d_matrix();

void gr_push_scale_matrix(const vec3d *scale_factor);
void gr_pop_scale_matrix();

void gr_setup_viewport();

void gr_reset_matrices();

extern matrix4 gr_texture_matrix;

void gr_set_texture_panning(float u, float v, bool enable);

/**
 * @brief Set current matrix uniforms
 *
 * Use this before rendering with a shader requiring the matrix uniforms so that the matrix uniform block point has the
 * up-to-date data.
 */
void gr_matrix_set_uniforms();
