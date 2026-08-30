/**************************************************************************/
/*  style_box_bevel.h                                                     */
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

#pragma once

#include "scene/resources/curve.h"
#include "scene/resources/style_box_flat.h"

class StyleBoxBevel : public StyleBoxFlat {
	GDCLASS(StyleBoxBevel, StyleBoxFlat);

public:
	enum BevelBlendFunction {
		BEVEL_BLEND_LINEAR,
		BEVEL_BLEND_SMOOTHSTEP,
		BEVEL_BLEND_EASE_IN,
		BEVEL_BLEND_EASE_OUT,
		BEVEL_BLEND_EASE_IN_OUT,
		BEVEL_BLEND_CURVE,
	};

private:
	// 1.0 = fully outset, -1.0 = fully inset; values in between blend between the two.
	real_t bevel_style = 1.0;
	Color bevel_lighting_color = Color(1, 1, 1);
	Color bevel_darkening_color = Color(0, 0, 0);
	real_t bevel_lighting_intensity = 0.3;
	real_t bevel_darkening_intensity = 0.3;
	real_t bevel_lighting_angle = 135.0;
	real_t bevel_max_intensity_angle_ratio = 0.5;
	BevelBlendFunction bevel_blend_function = BEVEL_BLEND_LINEAR;
	Ref<Curve> bevel_blend_curve;

protected:
	static void _bind_methods();

public:
	void set_bevel_style(float p_style);
	float get_bevel_style() const;

	void set_bevel_lighting_color(const Color &p_color);
	Color get_bevel_lighting_color() const;

	void set_bevel_darkening_color(const Color &p_color);
	Color get_bevel_darkening_color() const;

	void set_bevel_lighting_intensity(float p_intensity);
	float get_bevel_lighting_intensity() const;

	void set_bevel_darkening_intensity(float p_intensity);
	float get_bevel_darkening_intensity() const;

	void set_bevel_lighting_angle(float p_angle);
	float get_bevel_lighting_angle() const;

	void set_bevel_max_intensity_angle_ratio(float p_ratio);
	float get_bevel_max_intensity_angle_ratio() const;

	void set_bevel_blend_function(BevelBlendFunction p_function);
	BevelBlendFunction get_bevel_blend_function() const;

	void set_bevel_blend_curve(Ref<Curve> p_curve);
	Ref<Curve> get_bevel_blend_curve() const;

	virtual void draw(RID p_canvas_item, const Rect2 &p_rect) const override;
};

VARIANT_ENUM_CAST(StyleBoxBevel::BevelBlendFunction)
