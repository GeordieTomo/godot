/**************************************************************************/
/*  design_token_library.cpp                                              */
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

#include "design_token_library.h"

#include "core/object/callable_mp.h"
#include "core/object/class_db.h"

void DesignTokenLibrary::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_token_count", "count"), &DesignTokenLibrary::set_token_count);
	ClassDB::bind_method(D_METHOD("get_token_count"), &DesignTokenLibrary::get_token_count);

	ClassDB::bind_method(D_METHOD("set_token_name", "index", "name"), &DesignTokenLibrary::set_token_name);
	ClassDB::bind_method(D_METHOD("get_token_name", "index"), &DesignTokenLibrary::get_token_name);

	ClassDB::bind_method(D_METHOD("set_token_type", "index", "type"), &DesignTokenLibrary::set_token_type);
	ClassDB::bind_method(D_METHOD("get_token_type", "index"), &DesignTokenLibrary::get_token_type);

	ClassDB::bind_method(D_METHOD("set_token_value", "index", "value"), &DesignTokenLibrary::set_token_value);
	ClassDB::bind_method(D_METHOD("get_token_value", "index"), &DesignTokenLibrary::get_token_value);
	ClassDB::bind_method(D_METHOD("remove_token", "index"), &DesignTokenLibrary::remove_token);

	ClassDB::bind_method(D_METHOD("get_token_value_by_name", "name"), &DesignTokenLibrary::get_token_value_by_name);
	ClassDB::bind_method(D_METHOD("has_token", "name"), &DesignTokenLibrary::has_token);
	ClassDB::bind_method(D_METHOD("get_token_names"), &DesignTokenLibrary::get_token_names);
	ClassDB::bind_method(D_METHOD("get_token_names_for_type", "type"), &DesignTokenLibrary::get_token_names_for_type);

	ADD_SIGNAL(MethodInfo("token_renamed", PropertyInfo(Variant::STRING, "old_name"), PropertyInfo(Variant::STRING, "new_name")));

	ADD_PROPERTY(PropertyInfo(Variant::INT, "token_count", PROPERTY_HINT_RANGE, "0,1024,1,or_greater"), "set_token_count", "get_token_count");

	ADD_GROUP("Tokens", "token_");
}

bool DesignTokenLibrary::_set(const StringName &p_name, const Variant &p_value) {
	String name_str = p_name;

	if (name_str.begins_with("token_")) {
		// Parse "token_N_name", "token_N_type", "token_N_value"
		int sep = name_str.find("_", 6);
		if (sep == -1) {
			return false;
		}
		int index = name_str.substr(6, sep - 6).to_int();
		String sub = name_str.substr(sep + 1);

		ERR_FAIL_INDEX_V(index, tokens.size(), false);

		if (sub == "name") {
			set_token_indexed(index, "name", p_value);
			return true;
		} else if (sub == "type") {
			set_token_indexed(index, "type", p_value);
			return true;
		} else if (sub == "value") {
			set_token_indexed(index, "value", p_value);
			return true;
		}
	}
	return false;
}

bool DesignTokenLibrary::_get(const StringName &p_name, Variant &r_ret) const {
	String name_str = p_name;

	if (name_str.begins_with("token_")) {
		int sep = name_str.find("_", 6);
		if (sep == -1) {
			return false;
		}
		int index = name_str.substr(6, sep - 6).to_int();
		String sub = name_str.substr(sep + 1);

		ERR_FAIL_INDEX_V(index, tokens.size(), false);

		if (sub == "name") {
			r_ret = tokens[index].name;
			return true;
		} else if (sub == "type") {
			r_ret = tokens[index].type;
			return true;
		} else if (sub == "value") {
			r_ret = tokens[index].value;
			return true;
		}
	}
	return false;
}

static String get_type_enum_hint() {
	String hint;
	for (int type = 0; type < Variant::VARIANT_MAX; type++) {
		if (type > 0) {
			hint += ",";
		}
		hint += Variant::get_type_name((Variant::Type)type);
	}
	return hint;
}

void DesignTokenLibrary::_get_property_list(List<PropertyInfo> *p_list) const {
	for (int i = 0; i < tokens.size(); i++) {
		String prefix = "token_" + itos(i) + "_";
		p_list->push_back(PropertyInfo(Variant::STRING, prefix + "name"));
		p_list->push_back(PropertyInfo(Variant::INT, prefix + "type", PROPERTY_HINT_ENUM, get_type_enum_hint()));

		Variant::Type vtype = (Variant::Type)tokens[i].type;
		if (vtype != Variant::NIL) {
			PropertyInfo vp(vtype, prefix + "value");
			p_list->push_back(vp);
		}
	}
}

bool DesignTokenLibrary::_property_can_revert(const StringName &p_name) const {
	return false;
}

bool DesignTokenLibrary::_property_get_revert(const StringName &p_name, Variant &r_property) const {
	return false;
}

void DesignTokenLibrary::_rebuild_maps() {
	name_to_index.clear();
	for (int i = 0; i < tokens.size(); i++) {
		if (!tokens[i].name.is_empty()) {
			name_to_index[StringName(tokens[i].name)] = i;
		}
	}
}

void DesignTokenLibrary::_increment_version() {
	structural_version++;
}

void DesignTokenLibrary::set_token_count(int p_count) {
	ERR_FAIL_COND(p_count < 0);
	if (tokens.size() != p_count) {
		// Clear stale name entries that will be truncated.
		if (p_count < tokens.size()) {
			for (int i = p_count; i < tokens.size(); i++) {
				if (!tokens[i].name.is_empty()) {
					name_to_index.erase(StringName(tokens[i].name));
				}
			}
		}
		tokens.resize(p_count);
		// New slots have empty name/type=NIL; map already consistent.
		_increment_version();
		notify_property_list_changed();
		emit_changed();
	}
}

int DesignTokenLibrary::get_token_count() const {
	return tokens.size();
}

void DesignTokenLibrary::set_token_indexed(int p_index, const StringName &p_field, const Variant &p_value) {
	ERR_FAIL_INDEX(p_index, tokens.size());
	bool changed = false;
	bool structural = false;
	if (p_field == "name") {
		if (tokens[p_index].name != (String)p_value) {
			String old_name = tokens[p_index].name;
			if (!old_name.is_empty()) {
				name_to_index.erase(StringName(old_name));
			}
			String new_name = p_value;
			tokens.write[p_index].name = new_name;
			if (!new_name.is_empty()) {
				name_to_index[StringName(new_name)] = p_index;
			}
			if (!old_name.is_empty()) {
				emit_signal(SNAME("token_renamed"), old_name, new_name);
			}
			changed = true;
			structural = true;
		}
	} else if (p_field == "type") {
		if (tokens[p_index].type != (int)p_value) {
			tokens.write[p_index].type = p_value;
			tokens.write[p_index].value = Variant();
			notify_property_list_changed();
			changed = true;
			structural = true;
		}
	} else if (p_field == "value") {
		if (tokens[p_index].value != p_value) {
			tokens.write[p_index].value = p_value;
			changed = true;
		}
	}
	if (changed) {
		if (structural) {
			_increment_version();
		}
		emit_changed();
	}
}

void DesignTokenLibrary::set_token_name(int p_index, const String &p_name) {
	ERR_FAIL_INDEX(p_index, tokens.size());
	set_token_indexed(p_index, "name", p_name);
}

String DesignTokenLibrary::get_token_name(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, tokens.size(), String());
	return tokens[p_index].name;
}

void DesignTokenLibrary::set_token_type(int p_index, int p_type) {
	ERR_FAIL_INDEX(p_index, tokens.size());
	if (tokens[p_index].type != p_type) {
		tokens.write[p_index].type = p_type;
		tokens.write[p_index].value = Variant();
		_increment_version();
		notify_property_list_changed();
		emit_changed();
	}
}

int DesignTokenLibrary::get_token_type(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, tokens.size(), Variant::NIL);
	return tokens[p_index].type;
}

void DesignTokenLibrary::set_token_value(int p_index, const Variant &p_value) {
	ERR_FAIL_INDEX(p_index, tokens.size());
	tokens.write[p_index].value = p_value;
	emit_changed();
}

Variant DesignTokenLibrary::get_token_value(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, tokens.size(), Variant());
	return tokens[p_index].value;
}

void DesignTokenLibrary::remove_token(int p_index) {
	ERR_FAIL_INDEX(p_index, tokens.size());
	String old_name = tokens[p_index].name;
	if (!old_name.is_empty()) {
		name_to_index.erase(StringName(old_name));
	}
	tokens.remove_at(p_index);
	// Indices shifted, rebuild map for correctness O(n) only on remove.
	_rebuild_maps();
	_increment_version();
	notify_property_list_changed();
	emit_changed();
}

Variant DesignTokenLibrary::get_token_value_by_name(const String &p_name) const {
	const StringName key = StringName(p_name);
	auto it = name_to_index.find(key);
	if (it) {
		int idx = it->value;
		if (idx >= 0 && idx < tokens.size() && tokens[idx].name == p_name) {
			return tokens[idx].value;
		}
	}
	// Fallback linear scan for stale map (e.g., after deserialization).
	for (int i = 0; i < tokens.size(); i++) {
		if (tokens[i].name == p_name) {
			return tokens[i].value;
		}
	}
	return Variant();
}

bool DesignTokenLibrary::has_token(const String &p_name) const {
	const StringName key = StringName(p_name);
	if (name_to_index.has(key)) {
		return true;
	}
	for (int i = 0; i < tokens.size(); i++) {
		if (tokens[i].name == p_name) {
			return true;
		}
	}
	return false;
}

Vector<String> DesignTokenLibrary::get_token_names() const {
	Vector<String> names;
	for (int i = 0; i < tokens.size(); i++) {
		names.push_back(tokens[i].name);
	}
	return names;
}

Vector<String> DesignTokenLibrary::get_token_names_for_type(Variant::Type p_type) const {
	Vector<String> names;
	for (int i = 0; i < tokens.size(); i++) {
		if ((Variant::Type)tokens[i].type == p_type) {
			names.push_back(tokens[i].name);
		}
	}
	return names;
}
