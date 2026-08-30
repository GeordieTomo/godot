/**************************************************************************/
/*  style_box_bevel.cpp                                                   */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "style_box_bevel.h"

#include "core/config/engine.h"
#include "core/math/math_funcs.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "scene/resources/texture.h"
#include "servers/rendering/rendering_server.h"
#include "servers/text/text_server.h"

#include <cfloat> // FLT_EPSILON

#define BEVEL_INNER_COLOR_A(p_corner_idx) (p_inner_colors[(p_corner_idx) * 2 + 1])
#define BEVEL_INNER_COLOR_B(p_corner_idx) (p_inner_colors[((p_corner_idx) * 2 + 2) % 8])
#define BEVEL_OUTER_COLOR_A(p_corner_idx) (p_outer_colors[(p_corner_idx) * 2 + 1])
#define BEVEL_OUTER_COLOR_B(p_corner_idx) (p_outer_colors[((p_corner_idx) * 2 + 2) % 8])

namespace {
void bevel_draw_ring(Vector<Vector2> &verts, Vector<int> &indices, Vector<Color> &colors, const Rect2 &style_rect, const real_t corner_radius[4],
		const Rect2 &ring_rect, const Rect2 &inner_rect, const Color (&p_inner_colors)[8], const Color (&p_outer_colors)[8], const int corner_detail, const Vector2 &skew, bool is_filled = false);

// Maps the normalized position across a blended border band (0 = outer edge, 1 =
// inner edge) to the blend amount, based on the selected blend function. A tiny
// epsilon is used so `sample(1.0)` still reaches the curve's end value.
real_t bevel_blend_factor(StyleBoxBevel::BevelBlendFunction p_function, const Ref<Curve> &p_curve, real_t p_t) {
	real_t t = CLAMP(p_t, 0.0, 1.0);
	switch (p_function) {
		case StyleBoxBevel::BEVEL_BLEND_SMOOTHSTEP: {
			return t * t * (3.0 - 2.0 * t);
		}
		case StyleBoxBevel::BEVEL_BLEND_EASE_IN: {
			return t * t;
		}
		case StyleBoxBevel::BEVEL_BLEND_EASE_OUT: {
			real_t inv = 1.0 - t;
			return 1.0 - inv * inv;
		}
		case StyleBoxBevel::BEVEL_BLEND_EASE_IN_OUT: {
			if (t < 0.5) {
				return 2.0 * t * t;
			}
			real_t inv = 1.0 - t;
			return 1.0 - 2.0 * inv * inv;
		}
		case StyleBoxBevel::BEVEL_BLEND_CURVE: {
			if (p_curve.is_valid()) {
				return CLAMP(p_curve->sample(t), 0.0, 1.0);
			}
			[[fallthrough]];
		}
		case StyleBoxBevel::BEVEL_BLEND_LINEAR:
		default: {
			return t;
		}
	}
}

// Draws the border→background fade as several concentric sub-bands of the border
// ring. Each band interpolates the colors at its two edges, so any blend curve can
// be approximated without needing a custom shader. With a linear function the
// result matches the single unsplit ring exactly.
void bevel_draw_blend_bands(Vector<Vector2> &verts, Vector<int> &indices, Vector<Color> &colors, const Rect2 &style_rect, const real_t corner_radius[4],
		const Rect2 &outer_rect, const Rect2 &inner_rect, const Color (&p_outer_colors)[8], const Color (&p_blend_colors)[8],
		StyleBoxBevel::BevelBlendFunction p_function, const Ref<Curve> &p_curve, const int corner_detail, const Vector2 &skew) {
	constexpr int band_count = 8;
	for (int b = 0; b < band_count; b++) {
		const real_t t0 = (real_t)b / band_count;
		const real_t t1 = (real_t)(b + 1) / band_count;
		const real_t f0 = bevel_blend_factor(p_function, p_curve, t0);
		const real_t f1 = bevel_blend_factor(p_function, p_curve, t1);

		const Rect2 band_outer = Rect2(outer_rect.position.lerp(inner_rect.position, t0), outer_rect.size.lerp(inner_rect.size, t0));
		const Rect2 band_inner = Rect2(outer_rect.position.lerp(inner_rect.position, t1), outer_rect.size.lerp(inner_rect.size, t1));

		Color band_inner_colors[8];
		Color band_outer_colors[8];
		for (int i = 0; i < 8; i++) {
			band_outer_colors[i] = p_outer_colors[i].lerp(p_blend_colors[i], f0);
			band_inner_colors[i] = p_outer_colors[i].lerp(p_blend_colors[i], f1);
		}
		bevel_draw_ring(verts, indices, colors, style_rect, corner_radius,
				band_outer, band_inner, band_inner_colors, band_outer_colors, corner_detail, skew);
	}
}

void bevel_set_inner_corner_radius(const Rect2 style_rect, const Rect2 inner_rect, const real_t corner_radius[4], real_t *inner_corner_radius) {
	real_t border_left = inner_rect.position.x - style_rect.position.x;
	real_t border_top = inner_rect.position.y - style_rect.position.y;
	real_t border_right = style_rect.size.width - inner_rect.size.width - border_left;
	real_t border_bottom = style_rect.size.height - inner_rect.size.height - border_top;

	inner_corner_radius[0] = MAX(corner_radius[0] - MIN(border_top, border_left), 0); // Top left.
	inner_corner_radius[1] = MAX(corner_radius[1] - MIN(border_top, border_right), 0); // Top right.
	inner_corner_radius[2] = MAX(corner_radius[2] - MIN(border_bottom, border_right), 0); // Bottom right.
	inner_corner_radius[3] = MAX(corner_radius[3] - MIN(border_bottom, border_left), 0); // Bottom left.
}

void bevel_set_corner_scale(const Rect2 &style_rect, const Rect2 &inner_rect, const real_t corner_radius[4], Point2 *inner_scale) {
	real_t border_left = inner_rect.position.x - style_rect.position.x;
	real_t border_top = inner_rect.position.y - style_rect.position.y;
	real_t border_right = style_rect.size.width - inner_rect.size.width - border_left;
	real_t border_bottom = style_rect.size.height - inner_rect.size.height - border_top;

	// Amount of overflow along an edge.
	// Ex. SIDE_LEFT edge is the overflow between top_left and bottom_left corners.
	// MIN(0,) is to ignore underflow, and negating is to make values positive.
	real_t edge_overflow[4] = {
		-MIN(0, inner_rect.size.y - corner_radius[CORNER_TOP_LEFT] - corner_radius[CORNER_BOTTOM_LEFT]),
		-MIN(0, inner_rect.size.x - corner_radius[CORNER_TOP_LEFT] - corner_radius[CORNER_TOP_RIGHT]),
		-MIN(0, inner_rect.size.y - corner_radius[CORNER_TOP_RIGHT] - corner_radius[CORNER_BOTTOM_RIGHT]),
		-MIN(0, inner_rect.size.x - corner_radius[CORNER_BOTTOM_LEFT] - corner_radius[CORNER_BOTTOM_RIGHT])
	};

	// Sums of borders.
	real_t hb_sum = border_left + border_right;
	real_t vb_sum = border_top + border_bottom;

	// Ratio of each side to the sum of itself and opposite side.
	// Since overflow only happens with opposite borders, you only need to get the ratio of each border relative to the sum of involved borders.
	real_t ratios[4] = {
		// Prevent divide by 0 errors.
		hb_sum > 0 ? (border_left / hb_sum) : 0,
		vb_sum > 0 ? (border_top / vb_sum) : 0,
		hb_sum > 0 ? (border_right / hb_sum) : 0,
		vb_sum > 0 ? (border_bottom / vb_sum) : 0
	};

	// Raw amount each corner should shrink.
	Point2 corner_reduction[4] = {
		Point2(edge_overflow[SIDE_TOP] * ratios[SIDE_LEFT], edge_overflow[SIDE_LEFT] * ratios[SIDE_TOP]),
		Point2(edge_overflow[SIDE_TOP] * ratios[SIDE_RIGHT], edge_overflow[SIDE_RIGHT] * ratios[SIDE_TOP]),
		Point2(edge_overflow[SIDE_BOTTOM] * ratios[SIDE_RIGHT], edge_overflow[SIDE_RIGHT] * ratios[SIDE_BOTTOM]),
		Point2(edge_overflow[SIDE_BOTTOM] * ratios[SIDE_LEFT], edge_overflow[SIDE_LEFT] * ratios[SIDE_BOTTOM]),
	};

	// Corner Radii as Point2s.
	Point2 pcr[4] = {
		Point2(corner_radius[0], corner_radius[0]),
		Point2(corner_radius[1], corner_radius[1]),
		Point2(corner_radius[2], corner_radius[2]),
		Point2(corner_radius[3], corner_radius[3]),
	};

	// If corner radii are too small, they won't shrink the full amount.
	// Adjacent corners will have to shrink the leftovers if they can.
	// Minf(0) is to ignore non-leftovers, and negating is to make values positive.
	Point2 leftovers[4] = {
		-((pcr[0] - corner_reduction[0]).minf(0)),
		-((pcr[1] - corner_reduction[1]).minf(0)),
		-((pcr[2] - corner_reduction[2]).minf(0)),
		-((pcr[3] - corner_reduction[3]).minf(0)),
	};

	// New shrunken radii after distributing the leftovers.
	Point2 distributed[4] = {
		((pcr[0] - corner_reduction[0] - leftovers[3] - leftovers[1]).maxf(0)),
		((pcr[1] - corner_reduction[1] - leftovers[0] - leftovers[2]).maxf(0)),
		((pcr[2] - corner_reduction[2] - leftovers[1] - leftovers[3]).maxf(0)),
		((pcr[3] - corner_reduction[3] - leftovers[2] - leftovers[0]).maxf(0)),
	};

	// How much the curve should scale to achieve the shrunken radii.
	for (int i = 0; i < 4; i++) {
		// Unshrinkable is how much is still left over, even after distributing leftovers.
		// Exclude it from the final scale.
		Point2 unshrinkable = (leftovers[(i + 1) % 4] + leftovers[(i + 4 - 1) % 4] - distributed[i]).maxf(0);
		inner_scale[i] = distributed[i] / (pcr[i] - unshrinkable).maxf(FLT_EPSILON);
	}
}

// Same geometry as StyleBoxFlat's ring, but with a per-corner color gradient
// along each side instead of a single uniform color.
// `p_inner_colors`/`p_outer_colors` hold 8 colors each. For every side (LEFT,
// TOP, RIGHT, BOTTOM) a pair of consecutive colors is used, so indices
// 0-1=left, 2-3=top, 4-5=right, 6-7=bottom. Any corner blends between the two
// colors adjacent to it.
void bevel_draw_ring(Vector<Vector2> &verts, Vector<int> &indices, Vector<Color> &colors, const Rect2 &style_rect, const real_t corner_radius[4],
		const Rect2 &ring_rect, const Rect2 &inner_rect, const Color (&p_inner_colors)[8], const Color (&p_outer_colors)[8], const int corner_detail, const Vector2 &skew, bool is_filled) {
	int vert_offset = verts.size();
	int adapted_corner_detail = (corner_radius[0] > 0) || (corner_radius[1] > 0) || (corner_radius[2] > 0) || (corner_radius[3] > 0) ? corner_detail : 1;

	bool draw_border = !is_filled;

	real_t ring_corner_radius[4];
	bevel_set_inner_corner_radius(style_rect, ring_rect, corner_radius, ring_corner_radius);

	Point2 ring_scale[4];
	bevel_set_corner_scale(style_rect, ring_rect, ring_corner_radius, ring_scale);

	// Corner radius center points.
	Vector<Point2> outer_points = {
		ring_rect.position + Vector2(ring_corner_radius[0], ring_corner_radius[0]) * ring_scale[0], //tl
		Point2(ring_rect.position.x + ring_rect.size.x - ring_corner_radius[1] * ring_scale[1].x, ring_rect.position.y + ring_corner_radius[1] * ring_scale[1].y), //tr
		ring_rect.position + ring_rect.size - Vector2(ring_corner_radius[2], ring_corner_radius[2]) * ring_scale[2], //br
		Point2(ring_rect.position.x + ring_corner_radius[3] * ring_scale[3].x, ring_rect.position.y + ring_rect.size.y - ring_corner_radius[3] * ring_scale[3].y) //bl
	};

	real_t inner_corner_radius[4];
	bevel_set_inner_corner_radius(style_rect, inner_rect, corner_radius, inner_corner_radius);

	Point2 inner_scale[4];
	bevel_set_corner_scale(style_rect, inner_rect, inner_corner_radius, inner_scale);

	Vector<Point2> inner_points = {
		inner_rect.position + Vector2(inner_corner_radius[0], inner_corner_radius[0]) * inner_scale[0], //tl
		Point2(inner_rect.position.x + inner_rect.size.x - inner_corner_radius[1] * inner_scale[1].x, inner_rect.position.y + inner_corner_radius[1] * inner_scale[1].y), //tr
		inner_rect.position + inner_rect.size - Vector2(inner_corner_radius[2], inner_corner_radius[2]) * inner_scale[2], //br
		Point2(inner_rect.position.x + inner_corner_radius[3] * inner_scale[3].x, inner_rect.position.y + inner_rect.size.y - inner_corner_radius[3] * inner_scale[3].y) //bl
	};

	// Calculate the vertices.

	// If the center is filled, we do not draw the border and directly use the inner ring as reference. Because all calls to this
	// method either draw a ring or a filled rounded rectangle, but not both.
	const real_t quarter_arc_rad = Math::PI / 2.0;
	const Point2 style_rect_center = style_rect.get_center();

	const int colors_size = colors.size();
	const int verts_size = verts.size();
	const int new_verts_amount = (adapted_corner_detail + 1) * (draw_border ? 8 : 4);

	colors.resize(colors_size + new_verts_amount);
	verts.resize(verts_size + new_verts_amount);
	Color *colors_ptr = colors.ptrw();
	Vector2 *verts_ptr = verts.ptrw();

	for (int corner_idx = 0; corner_idx < 4; corner_idx++) {
		for (int detail = 0; detail <= adapted_corner_detail; detail++) {
			int idx_ofs = (adapted_corner_detail + 1) * corner_idx + detail;
			if (draw_border) {
				idx_ofs *= 2;
			}

			const real_t pt_angle = (corner_idx + detail / (double)adapted_corner_detail) * quarter_arc_rad + Math::PI;
			const real_t angle_cosine = Math::cos(pt_angle);
			const real_t angle_sine = Math::sin(pt_angle);
			const real_t blend = detail / (double)adapted_corner_detail;

			{
				const real_t x = inner_corner_radius[corner_idx] * angle_cosine * inner_scale[corner_idx].x + inner_points[corner_idx].x;
				const real_t y = inner_corner_radius[corner_idx] * angle_sine * inner_scale[corner_idx].y + inner_points[corner_idx].y;
				const float x_skew = -skew.x * (y - style_rect_center.y);
				const float y_skew = -skew.y * (x - style_rect_center.x);
				verts_ptr[verts_size + idx_ofs] = Vector2(x + x_skew, y + y_skew);
				const Color color_a = BEVEL_INNER_COLOR_A(corner_idx);
				const Color color_b = BEVEL_INNER_COLOR_B(corner_idx);
				colors_ptr[colors_size + idx_ofs] = color_a.lerp(color_b, blend);
			}

			if (draw_border) {
				const real_t x = ring_corner_radius[corner_idx] * angle_cosine * ring_scale[corner_idx].x + outer_points[corner_idx].x;
				const real_t y = ring_corner_radius[corner_idx] * angle_sine * ring_scale[corner_idx].y + outer_points[corner_idx].y;
				const float x_skew = -skew.x * (y - style_rect_center.y);
				const float y_skew = -skew.y * (x - style_rect_center.x);
				verts_ptr[verts_size + idx_ofs + 1] = Vector2(x + x_skew, y + y_skew);
				const Color color_a = BEVEL_OUTER_COLOR_A(corner_idx);
				const Color color_b = BEVEL_OUTER_COLOR_B(corner_idx);
				colors_ptr[colors_size + idx_ofs + 1] = color_a.lerp(color_b, blend);
			}
		}
	}

	int ring_vert_count = verts.size() - vert_offset;

	// Fill the indices and the colors for the border.

	if (draw_border) {
		int indices_size = indices.size();
		indices.resize(indices_size + ring_vert_count * 3);
		int *indices_ptr = indices.ptrw();

		for (int i = 0; i < ring_vert_count; i++) {
			int idx_ofs = indices_size + i * 3;
			indices_ptr[idx_ofs] = vert_offset + i % ring_vert_count;
			indices_ptr[idx_ofs + 1] = vert_offset + (i + 2) % ring_vert_count;
			indices_ptr[idx_ofs + 2] = vert_offset + (i + 1) % ring_vert_count;
		}
	}

	if (is_filled) {
		// Compute the triangles pattern to draw the rounded rectangle.
		// Consists of vertical stripes of two triangles each.

		int stripes_count = ring_vert_count / 2 - 1;
		int last_vert_id = ring_vert_count - 1;

		int indices_size = indices.size();
		indices.resize(indices_size + stripes_count * 6);
		int *indices_ptr = indices.ptrw();

		for (int i = 0; i < stripes_count; i++) {
			int idx_ofs = indices_size + i * 6;
			// Polygon 1.
			indices_ptr[idx_ofs] = vert_offset + i;
			indices_ptr[idx_ofs + 1] = vert_offset + last_vert_id - i - 1;
			indices_ptr[idx_ofs + 2] = vert_offset + i + 1;
			// Polygon 2.
			indices_ptr[idx_ofs + 3] = vert_offset + i;
			indices_ptr[idx_ofs + 4] = vert_offset + last_vert_id - i;
			indices_ptr[idx_ofs + 5] = vert_offset + last_vert_id - i - 1;
		}
	}
}
} // namespace

void StyleBoxBevel::set_bevel_style(float p_style) {
	bevel_style = CLAMP(p_style, -1.0f, 1.0f);
	emit_changed();
}

float StyleBoxBevel::get_bevel_style() const {
	return bevel_style;
}

void StyleBoxBevel::set_bevel_lighting_color(const Color &p_color) {
	bevel_lighting_color = p_color;
	emit_changed();
}

Color StyleBoxBevel::get_bevel_lighting_color() const {
	return bevel_lighting_color;
}

void StyleBoxBevel::set_bevel_darkening_color(const Color &p_color) {
	bevel_darkening_color = p_color;
	emit_changed();
}

Color StyleBoxBevel::get_bevel_darkening_color() const {
	return bevel_darkening_color;
}

void StyleBoxBevel::set_bevel_lighting_intensity(float p_intensity) {
	bevel_lighting_intensity = CLAMP(p_intensity, 0.0f, 4.0f);
	emit_changed();
}

float StyleBoxBevel::get_bevel_lighting_intensity() const {
	return bevel_lighting_intensity;
}

void StyleBoxBevel::set_bevel_darkening_intensity(float p_intensity) {
	bevel_darkening_intensity = CLAMP(p_intensity, 0.0f, 4.0f);
	emit_changed();
}

float StyleBoxBevel::get_bevel_darkening_intensity() const {
	return bevel_darkening_intensity;
}

void StyleBoxBevel::set_bevel_lighting_angle(float p_angle) {
	bevel_lighting_angle = Math::fposmod(p_angle, 360.0f);
	emit_changed();
}

float StyleBoxBevel::get_bevel_lighting_angle() const {
	return bevel_lighting_angle;
}

void StyleBoxBevel::set_bevel_max_intensity_angle_ratio(float p_ratio) {
	bevel_max_intensity_angle_ratio = CLAMP(p_ratio, 0.05f, 1.0f);
	emit_changed();
}

float StyleBoxBevel::get_bevel_max_intensity_angle_ratio() const {
	return bevel_max_intensity_angle_ratio;
}

void StyleBoxBevel::set_bevel_blend_function(BevelBlendFunction p_function) {
	bevel_blend_function = p_function;
	emit_changed();
}

StyleBoxBevel::BevelBlendFunction StyleBoxBevel::get_bevel_blend_function() const {
	return bevel_blend_function;
}

void StyleBoxBevel::set_bevel_blend_curve(Ref<Curve> p_curve) {
	if (bevel_blend_curve == p_curve) {
		return;
	}
	if (bevel_blend_curve.is_valid()) {
		bevel_blend_curve->disconnect_changed(callable_mp((Resource *)this, &StyleBoxBevel::emit_changed));
	}
	bevel_blend_curve = p_curve;
	if (bevel_blend_curve.is_valid()) {
		bevel_blend_curve->connect_changed(callable_mp((Resource *)this, &StyleBoxBevel::emit_changed));
	}
	emit_changed();
}

Ref<Curve> StyleBoxBevel::get_bevel_blend_curve() const {
	return bevel_blend_curve;
}

void StyleBoxBevel::draw(RID p_canvas_item, const Rect2 &p_rect) const {
	// Sets up the draw-time animation state (enter/exit transitions, transforms).
	begin_draw(p_canvas_item, p_rect);

	Rect2 rect_animated = get_animated_value(SNAME("rect"), p_rect);

	Color bg_color_animated = get_animated_value(SNAME("bg_color"), bg_color);
	Color border_color_animated = get_animated_value(SNAME("border/color"), border_color);

	real_t expand_margin_animated[4] = {
		get_animated_value(SNAME("expand/margin_left"), expand_margin[SIDE_LEFT]),
		get_animated_value(SNAME("expand/margin_top"), expand_margin[SIDE_TOP]),
		get_animated_value(SNAME("expand/margin_right"), expand_margin[SIDE_RIGHT]),
		get_animated_value(SNAME("expand/margin_bottom"), expand_margin[SIDE_BOTTOM])
	};

	real_t border_width_animated[4] = {
		get_animated_value(SNAME("border/width_left"), border_width[SIDE_LEFT]),
		get_animated_value(SNAME("border/width_top"), border_width[SIDE_TOP]),
		get_animated_value(SNAME("border/width_right"), border_width[SIDE_RIGHT]),
		get_animated_value(SNAME("border/width_bottom"), border_width[SIDE_BOTTOM])
	};
	for (int i = 0; i < 4; i++) {
		border_width_animated[i] = MAX(0, border_width_animated[i]);
	}

	real_t corner_radius_animated[4];
	corner_radius_animated[0] = MAX(0.0, (real_t)get_animated_value(SNAME("corner_radius/top_left"), corner_radius[CORNER_TOP_LEFT]));
	corner_radius_animated[1] = MAX(0.0, (real_t)get_animated_value(SNAME("corner_radius/top_right"), corner_radius[CORNER_TOP_RIGHT]));
	corner_radius_animated[2] = MAX(0.0, (real_t)get_animated_value(SNAME("corner_radius/bottom_right"), corner_radius[CORNER_BOTTOM_RIGHT]));
	corner_radius_animated[3] = MAX(0.0, (real_t)get_animated_value(SNAME("corner_radius/bottom_left"), corner_radius[CORNER_BOTTOM_LEFT]));

	real_t shadow_size_animated = get_animated_value(SNAME("shadow/size"), shadow_size);
	Vector2 shadow_offset_animated = get_animated_value(SNAME("shadow/offset"), shadow_offset);
	Color shadow_color_animated = get_animated_value(SNAME("shadow/color"), shadow_color);

	Color bevel_lighting_color_animated = get_animated_value(SNAME("bevel/lighting_color"), bevel_lighting_color);
	Color bevel_darkening_color_animated = get_animated_value(SNAME("bevel/darkening_color"), bevel_darkening_color);
	real_t bevel_lighting_intensity_animated = get_animated_value(SNAME("bevel/lighting_intensity"), bevel_lighting_intensity);
	real_t bevel_darkening_intensity_animated = get_animated_value(SNAME("bevel/darkening_intensity"), bevel_darkening_intensity);
	real_t bevel_lighting_angle_animated = get_animated_value(SNAME("bevel/lighting_angle"), bevel_lighting_angle);
	real_t bevel_max_intensity_angle_ratio_animated = get_animated_value(SNAME("bevel/max_intensity_angle_ratio"), bevel_max_intensity_angle_ratio);
	real_t bevel_style_animated = get_animated_value(SNAME("bevel/style"), bevel_style);

	bool draw_border = (border_width_animated[0] > 0) || (border_width_animated[1] > 0) || (border_width_animated[2] > 0) || (border_width_animated[3] > 0);
	bool draw_shadow = (shadow_size_animated > 0);
	if (!draw_border && !draw_center && !draw_shadow) {
		return;
	}

	Rect2 style_rect = rect_animated.grow_individual(expand_margin_animated[SIDE_LEFT], expand_margin_animated[SIDE_TOP], expand_margin_animated[SIDE_RIGHT], expand_margin_animated[SIDE_BOTTOM]);
	if (Math::is_zero_approx(style_rect.size.width) || Math::is_zero_approx(style_rect.size.height)) {
		return;
	}

	const bool rounded_corners = (corner_radius_animated[0] > 0) || (corner_radius_animated[1] > 0) || (corner_radius_animated[2] > 0) || (corner_radius_animated[3] > 0);
	// Only enable antialiasing if it is actually needed. This improves performance
	// and maximizes sharpness for non-skewed StyleBoxes with sharp corners.
	const bool aa_on = (rounded_corners || !skew.is_zero_approx()) && anti_aliased;

	const bool blend_on = blend_border && draw_border;

	const Color &base_color = border_color_animated;

	// Compute the color of each side, based on the lighting angle.
	Color light_blend = base_color.lerp(bevel_lighting_color_animated, bevel_lighting_intensity_animated);
	Color dark_blend = base_color.lerp(bevel_darkening_color_animated, bevel_darkening_intensity_animated);
	Color side_colors[4];
	for (int i = 0; i < 4; i++) {
		real_t ref_angle = Math::fposmod(((4 + 2 - i) % 4) * 90.0 - bevel_lighting_angle_animated, 360.0);
		real_t linear_exposure = (ref_angle <= 180.0) ? (-(ref_angle / 90.0) + 1.0) : ((ref_angle / 90.0) - 3.0);
		real_t ratio_to_remap = 1.0 - bevel_max_intensity_angle_ratio_animated;
		real_t ref_exposure = (ratio_to_remap <= 0.0001) ? CLAMP(linear_exposure, (real_t)-1.0, (real_t)1.0) : CLAMP(Math::remap(linear_exposure, -ratio_to_remap, ratio_to_remap, (real_t)-1.0, (real_t)1.0), (real_t)-1.0, (real_t)1.0);
		side_colors[i] = (ref_exposure >= 0.0) ? base_color.lerp(light_blend, ref_exposure) : base_color.lerp(dark_blend, -ref_exposure);
	}

	// Outset bevels map the side colors directly to their sides (LEFT=0-1, TOP=2-3,
	// RIGHT=4-5, BOTTOM=6-7). Inset bevels shade each side with the opposing side's
	// color (light and shadow swap). bevel_style is a float in [-1, 1]: 1 = fully
	// outset, -1 = fully inset. Values in between simply mix each slot's outset color
	// with its inset color, so the bevel reads as an extrusion growing or shrinking in
	// depth rather than a rotating light. At 0 the opposite shades cancel out to a
	// flat, unshaded box.
	const real_t mix_factor = CLAMP((1.0 - bevel_style_animated) * 0.5, 0.0, 1.0);
	Color border_colors[8];
	Color border_alpha_colors[8];
	Color border_blend_colors[8];
	Color border_inner_colors[8];
	Color border_aa_outer_colors[8];
	Color border_aa_alpha_colors[8];
	Color bg_colors[8];
	for (int i = 0; i < 8; i++) {
		border_colors[i] = Color(0, 0, 0, 1);
		bg_colors[i] = bg_color_animated;
	}
	for (int i = 0; i < 8; i++) {
		const Color outset = side_colors[i >> 1];
		const Color inset = side_colors[(((i >> 1) + 2) & 3)];
		border_colors[i] = outset.lerp(inset, mix_factor);
	}
	for (int i = 0; i < 8; i++) {
		border_alpha_colors[i] = Color(border_colors[i].r, border_colors[i].g, border_colors[i].b, 0);
		border_blend_colors[i] = (draw_center ? bg_colors[i] : border_alpha_colors[i]);
		border_inner_colors[i] = blend_on ? border_blend_colors[i] : border_colors[i];
	}

	real_t aa_size_scaled = 1.0f;
	if (aa_on) {
		real_t scale_factor = TextServer::get_current_drawn_item_oversampling();
		if (scale_factor == 0.0) {
			scale_factor = 1.0;
		}

		// Adjust AA feather size to account for the 2D scale factor, so that
		// antialiasing doesn't become blurry at viewport resolutions higher
		// than the default when using the `canvas_items` stretch mode
		// (or when using `oversampling` values different than `1.0`).
		aa_size_scaled = aa_size / scale_factor;
	}

	// Adapt borders (prevent weird overlapping/glitchy drawings).
	real_t aa_shrink = aa_on ? aa_size_scaled * 2 : 0;
	real_t width = MAX(style_rect.size.width - aa_shrink, 0);
	real_t height = MAX(style_rect.size.height - aa_shrink, 0);
	real_t adapted_border[4] = { 1000000.0, 1000000.0, 1000000.0, 1000000.0 };
	{
		real_t value_a = border_width_animated[SIDE_TOP];
		real_t value_b = border_width_animated[SIDE_BOTTOM];
		real_t factor = (value_a + value_b == 0.0) ? 1.0 : MIN(1.0, height / (value_a + value_b));
		adapted_border[SIDE_TOP] = MIN(MIN(value_a * factor, height), adapted_border[SIDE_TOP]);
		adapted_border[SIDE_BOTTOM] = MIN(MIN(value_b * factor, height), adapted_border[SIDE_BOTTOM]);
	}
	{
		real_t value_a = border_width_animated[SIDE_LEFT];
		real_t value_b = border_width_animated[SIDE_RIGHT];
		real_t factor = (value_a + value_b == 0.0) ? 1.0 : MIN(1.0, width / (value_a + value_b));
		adapted_border[SIDE_LEFT] = MIN(MIN(value_a * factor, width), adapted_border[SIDE_LEFT]);
		adapted_border[SIDE_RIGHT] = MIN(MIN(value_b * factor, width), adapted_border[SIDE_RIGHT]);
	}

	// Adapt corners (prevent weird overlapping/glitchy drawings).
	real_t adapted_corner[4] = { 1000000.0, 1000000.0, 1000000.0, 1000000.0 };
	{
		real_t value_a = corner_radius_animated[CORNER_TOP_RIGHT];
		real_t value_b = corner_radius_animated[CORNER_BOTTOM_RIGHT];
		real_t factor = (value_a + value_b == 0.0) ? 1.0 : MIN(1.0, height / (value_a + value_b));
		adapted_corner[CORNER_TOP_RIGHT] = MIN(MIN(value_a * factor, height - adapted_border[SIDE_BOTTOM]), adapted_corner[CORNER_TOP_RIGHT]);
		adapted_corner[CORNER_BOTTOM_RIGHT] = MIN(MIN(value_b * factor, height - adapted_border[SIDE_TOP]), adapted_corner[CORNER_BOTTOM_RIGHT]);
	}
	{
		real_t value_a = corner_radius_animated[CORNER_TOP_LEFT];
		real_t value_b = corner_radius_animated[CORNER_BOTTOM_LEFT];
		real_t factor = (value_a + value_b == 0.0) ? 1.0 : MIN(1.0, height / (value_a + value_b));
		adapted_corner[CORNER_TOP_LEFT] = MIN(MIN(value_a * factor, height - adapted_border[SIDE_BOTTOM]), adapted_corner[CORNER_TOP_LEFT]);
		adapted_corner[CORNER_BOTTOM_LEFT] = MIN(MIN(value_b * factor, height - adapted_border[SIDE_TOP]), adapted_corner[CORNER_BOTTOM_LEFT]);
	}
	{
		real_t value_a = corner_radius_animated[CORNER_TOP_LEFT];
		real_t value_b = corner_radius_animated[CORNER_TOP_RIGHT];
		real_t factor = (value_a + value_b == 0.0) ? 1.0 : MIN(1.0, width / (value_a + value_b));
		adapted_corner[CORNER_TOP_LEFT] = MIN(MIN(value_a * factor, width - adapted_border[SIDE_RIGHT]), adapted_corner[CORNER_TOP_LEFT]);
		adapted_corner[CORNER_TOP_RIGHT] = MIN(MIN(value_b * factor, width - adapted_border[SIDE_LEFT]), adapted_corner[CORNER_TOP_RIGHT]);
	}
	{
		real_t value_a = corner_radius_animated[CORNER_BOTTOM_LEFT];
		real_t value_b = corner_radius_animated[CORNER_BOTTOM_RIGHT];
		real_t factor = (value_a + value_b == 0.0) ? 1.0 : MIN(1.0, width / (value_a + value_b));
		adapted_corner[CORNER_BOTTOM_LEFT] = MIN(MIN(value_a * factor, width - adapted_border[SIDE_RIGHT]), adapted_corner[CORNER_BOTTOM_LEFT]);
		adapted_corner[CORNER_BOTTOM_RIGHT] = MIN(MIN(value_b * factor, width - adapted_border[SIDE_LEFT]), adapted_corner[CORNER_BOTTOM_RIGHT]);
	}

	Rect2 infill_rect = style_rect.grow_individual(-adapted_border[SIDE_LEFT], -adapted_border[SIDE_TOP], -adapted_border[SIDE_RIGHT], -adapted_border[SIDE_BOTTOM]);

	Rect2 border_style_rect = style_rect;

	if (aa_on) {
		for (int i = 0; i < 4; i++) {
			if (border_width_animated[i] > 0) {
				border_style_rect = border_style_rect.grow_side((Side)i, -aa_size_scaled);
			}
		}
	}

	Vector<Point2> verts;
	Vector<int> indices;
	Vector<Color> colors;
	Vector<Point2> uvs;

	Vector<int> fi;
	Vector<int> bi;
	Vector<int> si;

	bool share_indices = bg_texture.is_null() && border_texture.is_null() && shadow_texture.is_null();
	Vector<int> &fill_indices = share_indices ? indices : fi;
	Vector<int> &border_indices = share_indices ? indices : bi;
	Vector<int> &shadow_indices = share_indices ? indices : si;

	// Create shadow.
	if (draw_shadow) {
		Rect2 shadow_inner_rect = style_rect;
		shadow_inner_rect.position += shadow_offset_animated;

		Rect2 shadow_rect = style_rect.grow(shadow_size_animated);
		shadow_rect.position += shadow_offset_animated;

		Color shadow_color_transparent = Color(shadow_color_animated.r, shadow_color_animated.g, shadow_color_animated.b, 0);
		Color shadow_colors[8] = {
			shadow_color_animated, shadow_color_animated, shadow_color_animated, shadow_color_animated,
			shadow_color_animated, shadow_color_animated, shadow_color_animated, shadow_color_animated
		};
		Color shadow_alpha_colors[8] = {
			shadow_color_transparent, shadow_color_transparent, shadow_color_transparent, shadow_color_transparent,
			shadow_color_transparent, shadow_color_transparent, shadow_color_transparent, shadow_color_transparent
		};

		bevel_draw_ring(verts, shadow_indices, colors, shadow_inner_rect, adapted_corner,
				shadow_rect, shadow_inner_rect, shadow_colors, shadow_alpha_colors, corner_detail, skew);

		if (draw_center) {
			bevel_draw_ring(verts, shadow_indices, colors, shadow_inner_rect, adapted_corner,
					shadow_inner_rect, shadow_inner_rect, shadow_colors, shadow_colors, corner_detail, skew, true);
		}
	}

	// Create border (no AA).
	if (draw_border && !aa_on) {
		if (blend_on) {
			bevel_draw_blend_bands(verts, border_indices, colors, border_style_rect, adapted_corner,
					border_style_rect, infill_rect, border_colors, border_blend_colors, bevel_blend_function, bevel_blend_curve, corner_detail, skew);
		} else {
			bevel_draw_ring(verts, border_indices, colors, border_style_rect, adapted_corner,
					border_style_rect, infill_rect, border_inner_colors, border_colors, corner_detail, skew);
		}
	}

	// Create infill (no AA).
	if (draw_center && (!aa_on || blend_on)) {
		bevel_draw_ring(verts, fill_indices, colors, border_style_rect, adapted_corner,
				infill_rect, infill_rect, bg_colors, bg_colors, corner_detail, skew, true);
	}

	if (aa_on) {
		real_t aa_border_width[4];
		real_t aa_border_width_half[4];
		real_t aa_fill_width[4];
		real_t aa_fill_width_half[4];

		if (draw_border) {
			for (int i = 0; i < 4; i++) {
				if (border_width_animated[i] > 0) {
					aa_border_width[i] = aa_size_scaled;
					aa_border_width_half[i] = aa_size_scaled * 0.5;
					aa_fill_width[i] = 0;
					aa_fill_width_half[i] = 0;
				} else {
					aa_border_width[i] = 0;
					aa_border_width_half[i] = 0;
					aa_fill_width[i] = aa_size_scaled;
					aa_fill_width_half[i] = aa_size_scaled * 0.5;
				}
			}
		} else {
			for (int i = 0; i < 4; i++) {
				aa_border_width[i] = 0;
				aa_border_width_half[i] = 0;
				aa_fill_width[i] = aa_size_scaled;
				aa_fill_width_half[i] = aa_size_scaled * 0.5;
			}
		}

		if (draw_center) {
			// Infill rect, transparent side of antialiasing gradient (base infill rect enlarged by AA size)
			Rect2 infill_rect_aa_transparent = infill_rect.grow_individual(aa_fill_width_half[SIDE_LEFT], aa_fill_width_half[SIDE_TOP],
					aa_fill_width_half[SIDE_RIGHT], aa_fill_width_half[SIDE_BOTTOM]);
			// Infill rect, colored side of antialiasing gradient (base infill rect shrunk by AA size)
			Rect2 infill_rect_aa_colored = infill_rect_aa_transparent.grow_individual(-aa_fill_width[SIDE_LEFT], -aa_fill_width[SIDE_TOP],
					-aa_fill_width[SIDE_RIGHT], -aa_fill_width[SIDE_BOTTOM]);
			if (!blend_on) {
				// Create center fill, not antialiased yet
				bevel_draw_ring(verts, fill_indices, colors, border_style_rect, adapted_corner,
						infill_rect_aa_colored, infill_rect_aa_colored, bg_colors, bg_colors, corner_detail, skew, true);
			}
			if (!blend_on || !draw_border) {
				Color alpha_bg = Color(bg_color_animated.r, bg_color_animated.g, bg_color_animated.b, 0);
				Color bg_alpha_colors[8] = {
					alpha_bg, alpha_bg, alpha_bg, alpha_bg,
					alpha_bg, alpha_bg, alpha_bg, alpha_bg
				};
				// Add antialiasing on the center fill
				bevel_draw_ring(verts, fill_indices, colors, border_style_rect, adapted_corner,
						infill_rect_aa_transparent, infill_rect_aa_colored, bg_colors, bg_alpha_colors, corner_detail, skew);
			}
		}

		if (draw_border) {
			if (blend_on) {
				// The border blends toward the background across its width. Sample the
				// actual color at the border's outer boundary so the outer AA feather
				// fades from the real edge color (which may already be fully blended
				// toward the background) instead of the unblended bevel color.
				const real_t blend_outer = bevel_blend_factor(bevel_blend_function, bevel_blend_curve, 0.0);
				for (int i = 0; i < 8; i++) {
					border_aa_outer_colors[i] = border_colors[i].lerp(border_blend_colors[i], blend_outer);
					border_aa_alpha_colors[i] = Color(border_aa_outer_colors[i].r, border_aa_outer_colors[i].g, border_aa_outer_colors[i].b, 0);
				}
			} else {
				for (int i = 0; i < 8; i++) {
					border_aa_outer_colors[i] = border_colors[i];
					border_aa_alpha_colors[i] = border_alpha_colors[i];
				}
			}

			// Inner border recct, fully colored side of antialiasing gradient (base inner rect enlarged by AA size)
			Rect2 inner_rect_aa_colored = infill_rect.grow_individual(aa_border_width_half[SIDE_LEFT], aa_border_width_half[SIDE_TOP],
					aa_border_width_half[SIDE_RIGHT], aa_border_width_half[SIDE_BOTTOM]);
			// Inner border rect, transparent side of antialiasing gradient (base inner rect shrunk by AA size)
			Rect2 inner_rect_aa_transparent = inner_rect_aa_colored.grow_individual(-aa_border_width[SIDE_LEFT], -aa_border_width[SIDE_TOP],
					-aa_border_width[SIDE_RIGHT], -aa_border_width[SIDE_BOTTOM]);
			// Outer border rect, transparent side of antialiasing gradient (base outer rect enlarged by AA size)
			Rect2 outer_rect_aa_transparent = style_rect.grow_individual(aa_border_width_half[SIDE_LEFT], aa_border_width_half[SIDE_TOP],
					aa_border_width_half[SIDE_RIGHT], aa_border_width_half[SIDE_BOTTOM]);
			// Outer border rect, colored side of antialiasing gradient (base outer rect shrunk by AA size)
			Rect2 outer_rect_aa_colored = border_style_rect.grow_individual(aa_border_width_half[SIDE_LEFT], aa_border_width_half[SIDE_TOP],
					aa_border_width_half[SIDE_RIGHT], aa_border_width_half[SIDE_BOTTOM]);

			if (blend_on) {
				// Draw the border ring, not antialiased yet. The blend curve is
				// applied across the ring width as concentric sub-bands.
				bevel_draw_blend_bands(verts, border_indices, colors, border_style_rect, adapted_corner,
						outer_rect_aa_colored, infill_rect, border_colors, border_blend_colors, bevel_blend_function, bevel_blend_curve, corner_detail, skew);
			} else {
				// Create border ring, not antialiased yet
				bevel_draw_ring(verts, border_indices, colors, border_style_rect, adapted_corner,
						outer_rect_aa_colored, inner_rect_aa_colored, border_inner_colors, border_colors, corner_detail, skew);
			}
			if (!blend_on) {
				// Add antialiasing on the ring inner border
				bevel_draw_ring(verts, border_indices, colors, border_style_rect, adapted_corner,
						inner_rect_aa_colored, inner_rect_aa_transparent, border_blend_colors, border_colors, corner_detail, skew);
			}
			// Add antialiasing on the ring outer border
			bevel_draw_ring(verts, border_indices, colors, border_style_rect, adapted_corner,
					outer_rect_aa_transparent, outer_rect_aa_colored, border_aa_outer_colors, border_aa_alpha_colors, corner_detail, skew);
		}
	}

	// Compute UV coordinates.
	Rect2 uv_rect = style_rect.grow(aa_on ? aa_size_scaled : 0);
	uvs.resize(verts.size());
	Point2 *uvs_ptr = uvs.ptrw();
	for (int i = 0; i < verts.size(); i++) {
		uvs_ptr[i].x = (verts[i].x - uv_rect.position.x) / uv_rect.size.width;
		uvs_ptr[i].y = (verts[i].y - uv_rect.position.y) / uv_rect.size.height;
	}

	// Draw stylebox.
	RenderingServer *vs = RenderingServer::get_singleton();
	if (!share_indices) {
		if (draw_shadow) {
			vs->canvas_item_add_triangle_array(p_canvas_item, shadow_indices, verts, colors, uvs, {}, {}, shadow_texture.is_valid() ? shadow_texture->get_rid() : RID());
		}
		if (draw_border) {
			vs->canvas_item_add_triangle_array(p_canvas_item, border_indices, verts, colors, uvs, {}, {}, border_texture.is_valid() ? border_texture->get_rid() : RID());
		}
		if (draw_center) {
			vs->canvas_item_add_triangle_array(p_canvas_item, fill_indices, verts, colors, uvs, {}, {}, bg_texture.is_valid() ? bg_texture->get_rid() : RID());
		}
	} else {
		vs->canvas_item_add_triangle_array(p_canvas_item, indices, verts, colors, uvs);
	}

	end_draw(p_canvas_item, p_rect);
}

void StyleBoxBevel::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_bevel_style", "style"), &StyleBoxBevel::set_bevel_style);
	ClassDB::bind_method(D_METHOD("get_bevel_style"), &StyleBoxBevel::get_bevel_style);

	ClassDB::bind_method(D_METHOD("set_bevel_lighting_color", "color"), &StyleBoxBevel::set_bevel_lighting_color);
	ClassDB::bind_method(D_METHOD("get_bevel_lighting_color"), &StyleBoxBevel::get_bevel_lighting_color);

	ClassDB::bind_method(D_METHOD("set_bevel_darkening_color", "color"), &StyleBoxBevel::set_bevel_darkening_color);
	ClassDB::bind_method(D_METHOD("get_bevel_darkening_color"), &StyleBoxBevel::get_bevel_darkening_color);

	ClassDB::bind_method(D_METHOD("set_bevel_lighting_intensity", "intensity"), &StyleBoxBevel::set_bevel_lighting_intensity);
	ClassDB::bind_method(D_METHOD("get_bevel_lighting_intensity"), &StyleBoxBevel::get_bevel_lighting_intensity);

	ClassDB::bind_method(D_METHOD("set_bevel_darkening_intensity", "intensity"), &StyleBoxBevel::set_bevel_darkening_intensity);
	ClassDB::bind_method(D_METHOD("get_bevel_darkening_intensity"), &StyleBoxBevel::get_bevel_darkening_intensity);

	ClassDB::bind_method(D_METHOD("set_bevel_lighting_angle", "angle"), &StyleBoxBevel::set_bevel_lighting_angle);
	ClassDB::bind_method(D_METHOD("get_bevel_lighting_angle"), &StyleBoxBevel::get_bevel_lighting_angle);

	ClassDB::bind_method(D_METHOD("set_bevel_max_intensity_angle_ratio", "ratio"), &StyleBoxBevel::set_bevel_max_intensity_angle_ratio);
	ClassDB::bind_method(D_METHOD("get_bevel_max_intensity_angle_ratio"), &StyleBoxBevel::get_bevel_max_intensity_angle_ratio);

	ClassDB::bind_method(D_METHOD("set_bevel_blend_function", "function"), &StyleBoxBevel::set_bevel_blend_function);
	ClassDB::bind_method(D_METHOD("get_bevel_blend_function"), &StyleBoxBevel::get_bevel_blend_function);
	BIND_ENUM_CONSTANT(BEVEL_BLEND_LINEAR);
	BIND_ENUM_CONSTANT(BEVEL_BLEND_SMOOTHSTEP);
	BIND_ENUM_CONSTANT(BEVEL_BLEND_EASE_IN);
	BIND_ENUM_CONSTANT(BEVEL_BLEND_EASE_OUT);
	BIND_ENUM_CONSTANT(BEVEL_BLEND_EASE_IN_OUT);
	BIND_ENUM_CONSTANT(BEVEL_BLEND_CURVE);

	ClassDB::bind_method(D_METHOD("set_bevel_blend_curve", "curve"), &StyleBoxBevel::set_bevel_blend_curve);
	ClassDB::bind_method(D_METHOD("get_bevel_blend_curve"), &StyleBoxBevel::get_bevel_blend_curve);

	ADD_GROUP("Bevel", "bevel_");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "bevel_style", PROPERTY_HINT_RANGE, "-1,1,0.01"), "set_bevel_style", "get_bevel_style");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "bevel_lighting_color"), "set_bevel_lighting_color", "get_bevel_lighting_color");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "bevel_darkening_color"), "set_bevel_darkening_color", "get_bevel_darkening_color");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "bevel_lighting_intensity", PROPERTY_HINT_RANGE, "0,4,0.01"), "set_bevel_lighting_intensity", "get_bevel_lighting_intensity");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "bevel_darkening_intensity", PROPERTY_HINT_RANGE, "0,4,0.01"), "set_bevel_darkening_intensity", "get_bevel_darkening_intensity");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "bevel_lighting_angle", PROPERTY_HINT_RANGE, "0,360,0.1,slider,degrees"), "set_bevel_lighting_angle", "get_bevel_lighting_angle");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "bevel_max_intensity_angle_ratio", PROPERTY_HINT_RANGE, "0.05,1,0.01"), "set_bevel_max_intensity_angle_ratio", "get_bevel_max_intensity_angle_ratio");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "bevel_blend_function", PROPERTY_HINT_ENUM, "Linear,Smoothstep,Ease In,Ease Out,Ease In Out,Curve"), "set_bevel_blend_function", "get_bevel_blend_function");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "bevel_blend_curve", PROPERTY_HINT_RESOURCE_TYPE, "Curve"), "set_bevel_blend_curve", "get_bevel_blend_curve");
}
