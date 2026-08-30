/**************************************************************************/
/*  design_token_library.h                                                */
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

#include "core/io/resource.h"
#include "core/math/expression.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"

class DesignTokenLibrary : public Resource {
	GDCLASS(DesignTokenLibrary, Resource);

public:
	struct Token {
		String name;
		int type = Variant::NIL;
		Variant value;
		bool is_formula = false;
		String formula;
		// Runtime cache – not serialized, recomputed on load.
		mutable Ref<Expression> compiled;
		mutable Vector<String> deps;
		mutable Variant cached_value;
		mutable bool dirty = true;
		mutable String last_error;
	};

private:
	mutable Vector<Token> tokens;
	HashMap<StringName, int> name_to_index;
	uint64_t structural_version = 0;

	// Reverse deps: token name -> set of formula token indices that depend on it.
	HashMap<StringName, HashSet<int>> dependents;

	void _rebuild_maps();
	void _increment_version();
	void _rebuild_dependents();
	bool _is_valid_token_name(const String &p_name) const;
	bool _is_whitelisted_utility(const StringName &p_func) const;
	Error _compile_formula(int p_index, String &r_error);
	void _invalidate_all_formula_caches();
	Variant _evaluate_token_recursive(int p_index, HashSet<int> &r_visiting, String &r_error) const;
	Variant _get_evaluated_value(int p_index, String *r_error = nullptr) const;

protected:
	static void _bind_methods();
	bool _set(const StringName &p_name, const Variant &p_value);
	bool _get(const StringName &p_name, Variant &r_ret) const;
	void _get_property_list(List<PropertyInfo> *p_list) const;
	bool _property_can_revert(const StringName &p_name) const;
	bool _property_get_revert(const StringName &p_name, Variant &r_property) const;

public:
	void set_token_count(int p_count);
	int get_token_count() const;

	void set_token_name(int p_index, const String &p_name);
	String get_token_name(int p_index) const;

	void set_token_type(int p_index, int p_type);
	int get_token_type(int p_index) const;

	void set_token_value(int p_index, const Variant &p_value);
	Variant get_token_value(int p_index) const;

	void remove_token(int p_index);

	void set_token_indexed(int p_index, const StringName &p_field, const Variant &p_value);

	// Formula API.
	void set_token_is_formula(int p_index, bool p_is_formula);
	bool is_token_formula(int p_index) const;
	void set_token_formula(int p_index, const String &p_formula);
	String get_token_formula(int p_index) const;
	String get_token_error(int p_index) const;
	bool is_token_valid(int p_index) const;

	static bool is_valid_token_name_static(const String &p_name);
	bool is_valid_token_name(const String &p_name) const { return is_valid_token_name_static(p_name); }

	Variant get_token_value_by_name(const String &p_name) const;
	bool has_token(const String &p_name) const;
	Vector<String> get_token_names() const;
	Vector<String> get_token_names_for_type(Variant::Type p_type) const;

	uint64_t get_structural_version() const { return structural_version; }
};
