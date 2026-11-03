//
//

#include "line_draw_list.h"
#include "2d.h"
#include "material.h"
#include "tracing/tracing.h"
#include "graphics/render.h"
#include "io/timer.h"
#include <algorithm>
#include <cmath>

namespace graphics {

line_draw_list::line_draw_list() {
}
void line_draw_list::add_line(int x1, int y1, int x2, int y2, int resize_mode) {
	add_vertex(x1, y1, resize_mode, &GR_CURRENT_COLOR);
	add_vertex(x2, y2, resize_mode, &GR_CURRENT_COLOR);
}
void line_draw_list::add_gradient(int x1, int y1, int x2, int y2, int resize_mode) {
	add_vertex(x1, y1, resize_mode, &GR_CURRENT_COLOR);

	color endColor = GR_CURRENT_COLOR;
	endColor.alpha = 0;
	add_vertex(x2, y2, resize_mode, &endColor);
}
void line_draw_list::flush() {
	if (_line_vertices.empty()) {
		// Nothing to do here...
		return;
	}

	GR_DEBUG_SCOPE("Line draw list flush");
	TRACE_SCOPE(tracing::LineDrawListFlush);

	// VR (green-streak hunt): a segment far longer than the screen (or not finite) is a bad projection of a point behind the
	// camera; it would be drawn as a long streak across the HUD layer, so it is dropped and logged
	{
		const float limit = 0.6f * static_cast<float>(std::max(gr_screen.max_w, gr_screen.max_h));
		SCP_vector<line_vertex> kept;
		kept.reserve(_line_vertices.size());
		for (size_t i = 0; i + 1 < _line_vertices.size(); i += 2) {
			const auto& a = _line_vertices[i];
			const auto& b = _line_vertices[i + 1];
			const float dx = b.position.x - a.position.x, dy = b.position.y - a.position.y;
			const float len = sqrtf(dx * dx + dy * dy);
			if (!(len <= limit)) {
				static int stamp = 0;
				const int now = timer_get_milliseconds();
				if (now - stamp > 2000) {
					stamp = now;
					mprintf(("VRHUD dropped line: gauge %d (%.0f,%.0f)-(%.0f,%.0f) len %.0f limit %.0f\n", Gr_debug_hud_gauge, a.position.x, a.position.y,
						b.position.x, b.position.y, len, limit));
				}
				continue;
			}
			kept.push_back(a);
			kept.push_back(b);
		}
		_line_vertices.swap(kept);
		if (_line_vertices.empty()) {
			return;
		}
	}

	material line_mat;
	line_mat.set_blend_mode(ALPHA_BLEND_ALPHA_BLEND_ALPHA);
	line_mat.set_depth_mode(ZBUFFER_TYPE_NONE);
	line_mat.set_cull_mode(false);
	line_mat.set_color(1.0f, 1.0f, 1.0f, 1.0f); // Color is handled by the vertices


	vertex_layout layout;
	layout.add_vertex_component(vertex_format_data::POSITION2, sizeof(line_vertex), offsetof(line_vertex, position));
	layout.add_vertex_component(vertex_format_data::COLOR4F, sizeof(line_vertex), offsetof(line_vertex, color));

	gr_render_primitives_2d_immediate(&line_mat,
									  PRIM_TYPE_LINES,
									  &layout,
									  static_cast<int>(_line_vertices.size()),
									  _line_vertices.data(),
									  _line_vertices.size() * sizeof(line_vertex));

	_line_vertices.clear();
}
void line_draw_list::add_vertex(int x, int y, int resize_mode, const color* color) {
	line_vertex vtx{};
	vtx.position.x = i2fl(x);
	vtx.position.y = i2fl(y);

	float w_scale = 1.0f;
	float h_scale = 1.0f;

	bool do_resize = gr_resize_screen_posf(&vtx.position.x, &vtx.position.y, &w_scale, &h_scale, resize_mode);

	int offset_x = ((do_resize) ? gr_screen.offset_x_unscaled : gr_screen.offset_x);
	int offset_y = ((do_resize) ? gr_screen.offset_y_unscaled : gr_screen.offset_y);

	// m!m - This is silly but there is no better solution at the moment...
	vtx.position.x += i2fl(offset_x) * w_scale;
	vtx.position.y += i2fl(offset_y) * h_scale;

	vtx.color.xyzw.x = color->red / 255.f;
	vtx.color.xyzw.y = color->green / 255.f;
	vtx.color.xyzw.z = color->blue / 255.f;
	vtx.color.xyzw.w = color->is_alphacolor ? color->alpha / 255.f : 1.f;

	_line_vertices.push_back(vtx);
}

}
