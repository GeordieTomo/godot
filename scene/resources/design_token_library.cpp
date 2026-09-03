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
#include "core/string/ustring.h"

static bool _is_unicode_identifier_start(char32_t p_c) {
	return is_unicode_identifier_start(p_c);
}
static bool _is_unicode_identifier_continue(char32_t p_c) {
	return is_unicode_identifier_continue(p_c);
}

bool DesignTokenLibrary::is_valid_token_name_static(const String &p_name) {
	if (p_name.is_empty()) {
		return false;
	}
	const String &s = p_name;
	char32_t first = s[0];
	if (!(first == '_' || (first >= 'a' && first <= 'z') || (first >= 'A' && first <= 'Z'))) {
		return false;
	}
	for (int i = 1; i < s.length(); i++) {
		char32_t c = s[i];
		if (!(c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) {
			return false;
		}
	}
	return true;
}

bool DesignTokenLibrary::_is_valid_token_name(const String &p_name) const {
	return is_valid_token_name_static(p_name);
}

bool DesignTokenLibrary::_is_whitelisted_utility(const StringName &p_func) const {
	// Basic math utilities only. Random/general utilities are excluded.
	static const HashSet<StringName> whitelist = [] {
		HashSet<StringName> s;
		s.insert("abs");
		s.insert("absf");
		s.insert("absi");
		s.insert("sign");
		s.insert("signf");
		s.insert("signi");
		s.insert("clamp");
		s.insert("clampi");
		s.insert("clampf");
		s.insert("lerp");
		s.insert("lerpf");
		s.insert("lerp_angle");
		s.insert("inverse_lerp");
		s.insert("remap");
		s.insert("smoothstep");
		s.insert("move_toward");
		s.insert("min");
		s.insert("mini");
		s.insert("minf");
		s.insert("max");
		s.insert("maxi");
		s.insert("maxf");
		s.insert("floor");
		s.insert("floori");
		s.insert("ceil");
		s.insert("ceili");
		s.insert("round");
		s.insert("roundi");
		s.insert("sqrt");
		s.insert("pow");
		s.insert("log");
		s.insert("exp");
		s.insert("sin");
		s.insert("cos");
		s.insert("tan");
		s.insert("asin");
		s.insert("acos");
		s.insert("atan");
		s.insert("atan2");
		s.insert("sinh");
		s.insert("cosh");
		s.insert("tanh");
		s.insert("fposmod");
		s.insert("fmod");
		s.insert("snapped");
		s.insert("snappedi");
		s.insert("snappedf");
		s.insert("deg_to_rad");
		s.insert("rad_to_deg");
		s.insert("linear_to_db");
		s.insert("db_to_linear");
		s.insert("wrap");
		s.insert("wrapi");
		s.insert("wrapf");
		s.insert("pingpong");
		s.insert("is_equal_approx");
		s.insert("is_zero_approx");
		return s;
	}();
	return whitelist.has(p_func);
}

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
	ClassDB::bind_method(D_METHOD("insert_token", "index", "name", "type", "value", "is_formula", "formula"), &DesignTokenLibrary::insert_token, DEFVAL(false), DEFVAL(String()));

	ClassDB::bind_method(D_METHOD("set_token_is_formula", "index", "is_formula"), &DesignTokenLibrary::set_token_is_formula);
	ClassDB::bind_method(D_METHOD("is_token_formula", "index"), &DesignTokenLibrary::is_token_formula);
	ClassDB::bind_method(D_METHOD("set_token_formula", "index", "formula"), &DesignTokenLibrary::set_token_formula);
	ClassDB::bind_method(D_METHOD("get_token_formula", "index"), &DesignTokenLibrary::get_token_formula);
	ClassDB::bind_method(D_METHOD("get_token_error", "index"), &DesignTokenLibrary::get_token_error);
	ClassDB::bind_method(D_METHOD("is_token_valid", "index"), &DesignTokenLibrary::is_token_valid);

	ClassDB::bind_method(D_METHOD("get_token_value_by_name", "name"), &DesignTokenLibrary::get_token_value_by_name);
	ClassDB::bind_method(D_METHOD("has_token", "name"), &DesignTokenLibrary::has_token);
	ClassDB::bind_method(D_METHOD("get_token_names"), &DesignTokenLibrary::get_token_names);
	ClassDB::bind_method(D_METHOD("get_token_names_for_type", "type"), &DesignTokenLibrary::get_token_names_for_type);
	ClassDB::bind_method(D_METHOD("is_valid_token_name", "name"), &DesignTokenLibrary::is_valid_token_name);

	ADD_SIGNAL(MethodInfo("token_renamed", PropertyInfo(Variant::STRING, "old_name"), PropertyInfo(Variant::STRING, "new_name")));

	ADD_PROPERTY(PropertyInfo(Variant::INT, "token_count", PROPERTY_HINT_RANGE, "0,1024,1,or_greater"), "set_token_count", "get_token_count");

	ADD_GROUP("Tokens", "token_");
}

bool DesignTokenLibrary::_set(const StringName &p_name, const Variant &p_value) {
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
			set_token_indexed(index, "name", p_value);
			return true;
		} else if (sub == "type") {
			set_token_indexed(index, "type", p_value);
			return true;
		} else if (sub == "value") {
			set_token_indexed(index, "value", p_value);
			return true;
		} else if (sub == "is_formula") {
			set_token_indexed(index, "is_formula", p_value);
			return true;
		} else if (sub == "formula") {
			set_token_indexed(index, "formula", p_value);
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
		} else if (sub == "is_formula") {
			r_ret = tokens[index].is_formula;
			return true;
		} else if (sub == "formula") {
			r_ret = tokens[index].formula;
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
		p_list->push_back(PropertyInfo(Variant::BOOL, prefix + "is_formula"));
		if (tokens[i].is_formula) {
			p_list->push_back(PropertyInfo(Variant::STRING, prefix + "formula", PROPERTY_HINT_EXPRESSION, ""));
		} else {
			Variant::Type vtype = (Variant::Type)tokens[i].type;
			if (vtype != Variant::NIL) {
				PropertyInfo vp(vtype, prefix + "value");
				p_list->push_back(vp);
			} else {
				// Auto type: still allow editing value as Variant? Provide generic.
				p_list->push_back(PropertyInfo(Variant::NIL, prefix + "value", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_DEFAULT));
			}
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

void DesignTokenLibrary::_rebuild_dependents() {
	dependents.clear();
	for (int i = 0; i < tokens.size(); i++) {
		if (!tokens[i].is_formula) {
			continue;
		}
		// Ensure deps are up to date; compile lazily if needed.
		Token &t = tokens.write[i];
		if (t.compiled.is_null() && !t.formula.is_empty()) {
			String err;
			_compile_formula(i, err);
		}
		for (const String &dep : t.deps) {
			dependents[StringName(dep)].insert(i);
		}
	}
}

void DesignTokenLibrary::_increment_version() {
	structural_version++;
}

void DesignTokenLibrary::_invalidate_all_formula_caches() {
	for (int i = 0; i < tokens.size(); i++) {
		if (tokens[i].is_formula) {
			Token &t = tokens.write[i];
			t.dirty = true;
		}
	}
}

Error DesignTokenLibrary::_compile_formula(int p_index, String &r_error) {
	ERR_FAIL_INDEX_V(p_index, tokens.size(), ERR_INVALID_PARAMETER);
	Token &tok = tokens.write[p_index];
	tok.compiled.unref();
	tok.deps.clear();
	tok.last_error = "";
	tok.dirty = true;

	if (!tok.is_formula) {
		return OK;
	}
	String formula = tok.formula.strip_edges();
	if (formula.is_empty()) {
		r_error = "Empty formula.";
		tok.last_error = r_error;
		return ERR_PARSE_ERROR;
	}

	// Scan for identifiers and function calls.
	Vector<String> deps;
	HashSet<String> deps_set;

	// Helper to check allowed constants.
	HashSet<String> allowed_consts;
	allowed_consts.insert("true");
	allowed_consts.insert("false");
	allowed_consts.insert("null");
	allowed_consts.insert("PI");
	allowed_consts.insert("TAU");
	allowed_consts.insert("INF");
	allowed_consts.insert("NAN");

	int len = formula.length();
	int pos = 0;
	while (pos < len) {
		char32_t c = formula[pos];
		if (c == '\'' || c == '"') {
			// Skip string literals.
			char32_t quote = c;
			pos++;
			while (pos < len) {
				char32_t ch = formula[pos];
				if (ch == '\\') {
					pos += 2;
					continue;
				}
				if (ch == quote) {
					pos++;
					break;
				}
				pos++;
			}
			continue;
		}
		if (_is_unicode_identifier_start(c)) {
			int start = pos;
			pos++;
			while (pos < len && _is_unicode_identifier_continue(formula[pos])) {
				pos++;
			}
			String ident = formula.substr(start, pos - start);
			// Look ahead for '(' to decide if function call.
			int tmp = pos;
			while (tmp < len && formula[tmp] <= 32) {
				tmp++;
			}
			bool is_func = tmp < len && formula[tmp] == '(';
			if (is_func) {
				// Check if it's a whitelisted utility or a Variant type constructor.
				if (_is_whitelisted_utility(StringName(ident))) {
					// ok
				} else if (Variant::get_type_by_name(ident) != Variant::VARIANT_MAX) {
					// Constructor like Color, Vector2, etc. Allowed.
				} else {
					r_error = vformat("Function '%s' is not whitelisted.", ident);
					tok.last_error = r_error;
					return ERR_PARSE_ERROR;
				}
			} else {
				// Variable / token reference.
				if (allowed_consts.has(ident)) {
					// ok constant
				} else if (Variant::has_utility_function(StringName(ident))) {
					// Bare utility name without '(' – treat as error (should be call).
					r_error = vformat("Utility function '%s' must be called with parentheses.", ident);
					tok.last_error = r_error;
					return ERR_PARSE_ERROR;
				} else if (Variant::get_type_by_name(ident) != Variant::VARIANT_MAX) {
					// Type name used without '(' – likely error but allow? Treat as error.
					r_error = vformat("Type '%s' must be used as constructor.", ident);
					tok.last_error = r_error;
					return ERR_PARSE_ERROR;
				} else {
					// Must be a token name.
					if (!has_token(ident)) {
						// Check if ident could be a property/method after '.' – we skip those.
						// Look behind for '.' to allow e.g., Color.RED – but Expression will handle named index via '.'.
						// For simplicity, if preceded by '.' skip.
						bool preceded_by_dot = false;
						int back = start - 1;
						while (back >= 0 && formula[back] <= 32) {
							back--;
						}
						if (back >= 0 && formula[back] == '.') {
							preceded_by_dot = true;
						}
						if (!preceded_by_dot) {
							r_error = vformat("Unknown token '%s'.", ident);
							tok.last_error = r_error;
							return ERR_PARSE_ERROR;
						}
					} else {
						if (!deps_set.has(ident)) {
							deps_set.insert(ident);
							deps.push_back(ident);
						}
					}
				}
			}
			continue;
		}
		pos++;
	}

	// Now compile with Expression using deps as input names.
	Ref<Expression> expr;
	expr.instantiate();
	Vector<String> input_names;
	input_names.resize(deps.size());
	for (int i = 0; i < deps.size(); i++) {
		input_names.write[i] = deps[i];
	}
	Error err = expr->parse(formula, input_names);
	if (err != OK) {
		r_error = expr->get_error_text();
		tok.last_error = r_error;
		return err;
	}
	tok.compiled = expr;
	tok.deps = deps;
	tok.last_error = "";
	return OK;
}

Variant DesignTokenLibrary::_evaluate_token_recursive(int p_index, HashSet<int> &r_visiting, String &r_error) const {
	ERR_FAIL_INDEX_V(p_index, tokens.size(), Variant());
	const Token &ctok = tokens[p_index];
	if (!ctok.is_formula) {
		return ctok.value;
	}
	// Need mutable access for cache.
	DesignTokenLibrary *self = const_cast<DesignTokenLibrary *>(this);
	Token &tok = self->tokens.write[p_index];

	if (r_visiting.has(p_index)) {
		r_error = vformat("Cyclic dependency detected at token '%s'.", tok.name);
		tok.last_error = r_error;
		return Variant();
	}
	if (!tok.dirty && tok.last_error.is_empty() && tok.compiled.is_valid()) {
		return tok.cached_value;
	}
	if (tok.compiled.is_null()) {
		String comp_err;
		Error err = self->_compile_formula(p_index, comp_err);
		if (err != OK) {
			r_error = comp_err;
			return Variant();
		}
	}
	if (!tok.last_error.is_empty()) {
		r_error = tok.last_error;
		return Variant();
	}

	r_visiting.insert(p_index);

	Array inputs;
	inputs.resize(tok.deps.size());
	for (int i = 0; i < tok.deps.size(); i++) {
		String dep_name = tok.deps[i];
		int dep_idx = -1;
		auto it = self->name_to_index.find(StringName(dep_name));
		if (it) {
			dep_idx = it->value;
		} else {
			for (int j = 0; j < self->tokens.size(); j++) {
				if (self->tokens[j].name == dep_name) {
					dep_idx = j;
					break;
				}
			}
		}
		if (dep_idx == -1) {
			r_error = vformat("Unknown token '%s' in formula '%s'.", dep_name, tok.name);
			tok.last_error = r_error;
			r_visiting.erase(p_index);
			return Variant();
		}
		String dep_err;
		Variant dep_val = self->_evaluate_token_recursive(dep_idx, r_visiting, dep_err);
		if (!dep_err.is_empty()) {
			r_error = dep_err;
			tok.last_error = r_error;
			r_visiting.erase(p_index);
			return Variant();
		}
		inputs[i] = dep_val;
	}

	Variant result = tok.compiled->execute(inputs, nullptr, false, true);
	if (tok.compiled->has_execute_failed()) {
		r_error = tok.compiled->get_error_text();
		tok.last_error = r_error;
		tok.dirty = true;
		r_visiting.erase(p_index);
		return Variant();
	}

	// Type auto + override: if stored type != NIL, enforce it.
	if (tok.type != Variant::NIL && result.get_type() != (Variant::Type)tok.type) {
		// Allow INT<->FLOAT conversion.
		if (tok.type == Variant::FLOAT && result.get_type() == Variant::INT) {
			result = Variant(double(result));
		} else if (tok.type == Variant::INT && result.get_type() == Variant::FLOAT) {
			result = Variant(int64_t(result));
		} else if (Variant::can_convert(result.get_type(), (Variant::Type)tok.type)) {
			// Try generic conversion via Variant construction? For now error.
			r_error = vformat("Type mismatch for '%s': formula evaluates to %s but expects %s.", tok.name, Variant::get_type_name(result.get_type()), Variant::get_type_name((Variant::Type)tok.type));
			tok.last_error = r_error;
			tok.dirty = true;
			r_visiting.erase(p_index);
			return Variant();
		} else {
			r_error = vformat("Type mismatch for '%s': formula evaluates to %s but expects %s.", tok.name, Variant::get_type_name(result.get_type()), Variant::get_type_name((Variant::Type)tok.type));
			tok.last_error = r_error;
			tok.dirty = true;
			r_visiting.erase(p_index);
			return Variant();
		}
	}

	tok.cached_value = result;
	tok.dirty = false;
	tok.last_error = "";
	r_visiting.erase(p_index);
	return result;
}

Variant DesignTokenLibrary::_get_evaluated_value(int p_index, String *r_error) const {
	ERR_FAIL_INDEX_V(p_index, tokens.size(), Variant());
	const Token &tok = tokens[p_index];
	if (!tok.is_formula) {
		return tok.value;
	}
	HashSet<int> visiting;
	String err;
	Variant val = _evaluate_token_recursive(p_index, visiting, err);
	if (!err.is_empty() && r_error) {
		*r_error = err;
	}
	return val;
}

void DesignTokenLibrary::set_token_count(int p_count) {
	ERR_FAIL_COND(p_count < 0);
	if (tokens.size() != p_count) {
		if (p_count < tokens.size()) {
			for (int i = p_count; i < tokens.size(); i++) {
				if (!tokens[i].name.is_empty()) {
					name_to_index.erase(StringName(tokens[i].name));
				}
			}
		}
		tokens.resize(p_count);
		_rebuild_maps();
		_rebuild_dependents();
		_invalidate_all_formula_caches();
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
		String new_name = p_value;
		if (new_name != tokens[p_index].name) {
			if (!new_name.is_empty() && !_is_valid_token_name(new_name)) {
				ERR_FAIL_MSG(vformat("Invalid token name '%s': must match [_a-zA-Z][_a-zA-Z0-9]*.", new_name));
			}
			if (!new_name.is_empty() && has_token(new_name) && name_to_index[StringName(new_name)] != p_index) {
				ERR_FAIL_MSG(vformat("Token name '%s' already exists.", new_name));
			}
			String old_name = tokens[p_index].name;
			if (!old_name.is_empty()) {
				name_to_index.erase(StringName(old_name));
			}
			tokens.write[p_index].name = new_name;
			if (!new_name.is_empty()) {
				name_to_index[StringName(new_name)] = p_index;
			}
			if (!old_name.is_empty()) {
				emit_signal(SNAME("token_renamed"), old_name, new_name);
			}
			// Renaming invalidates dependents that reference old name: need to recompile all formulas that used old name.
			for (int i = 0; i < tokens.size(); i++) {
				if (tokens[i].is_formula) {
					Token &t = tokens.write[i];
					if (t.deps.has(old_name) || t.formula.contains(old_name)) {
						String err;
						_compile_formula(i, err);
					}
				}
			}
			_rebuild_dependents();
			_invalidate_all_formula_caches();
			changed = true;
			structural = true;
		}
	} else if (p_field == "type") {
		if (tokens[p_index].type != (int)p_value) {
			tokens.write[p_index].type = p_value;
			// For non-formula tokens, clear value to avoid stale type.
			if (!tokens[p_index].is_formula) {
				tokens.write[p_index].value = Variant();
			} else {
				// For formula, mark dirty to re-evaluate with new expected type.
				tokens.write[p_index].dirty = true;
			}
			notify_property_list_changed();
			changed = true;
			structural = true;
		}
	} else if (p_field == "value") {
		if (tokens[p_index].is_formula) {
			// Disallow setting value on formula token; value is computed.
			ERR_FAIL_MSG("Cannot set value on a formula token. Disable formula first.");
		}
		if (tokens[p_index].value != p_value) {
			tokens.write[p_index].value = p_value;
			_invalidate_all_formula_caches();
			changed = true;
		}
	} else if (p_field == "is_formula") {
		bool is_formula = p_value;
		if (tokens[p_index].is_formula != is_formula) {
			tokens.write[p_index].is_formula = is_formula;
			if (is_formula) {
				// Entering formula mode: keep current value as fallback, compile.
				String err;
				_compile_formula(p_index, err);
				// If type is not set, it stays NIL (auto).
			} else {
				Token &t = tokens.write[p_index];
				t.compiled.unref();
				t.deps.clear();
				t.last_error = "";
				t.dirty = false;
				// Keep formula string for potential re-enable.
			}
			_rebuild_dependents();
			notify_property_list_changed();
			changed = true;
			structural = true;
		}
	} else if (p_field == "formula") {
		String new_formula = p_value;
		if (tokens[p_index].formula != new_formula) {
			tokens.write[p_index].formula = new_formula;
			if (tokens[p_index].is_formula) {
				String err;
				_compile_formula(p_index, err);
				_rebuild_dependents();
				// Mark all dependent formulas dirty.
				_invalidate_all_formula_caches();
			}
			changed = true;
			// Formula change is structural for version but not necessarily for UI grouping.
			structural = true;
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
		if (!tokens[p_index].is_formula) {
			tokens.write[p_index].value = Variant();
		} else {
			tokens.write[p_index].dirty = true;
		}
		_increment_version();
		notify_property_list_changed();
		emit_changed();
	}
}

int DesignTokenLibrary::get_token_type(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, tokens.size(), Variant::NIL);
	const Token &t = tokens[p_index];
	if (t.is_formula && t.type == Variant::NIL) {
		// Auto type: infer from evaluated value.
		String err;
		Variant val = _get_evaluated_value(p_index, &err);
		if (!err.is_empty() || val.get_type() == Variant::NIL) {
			return Variant::NIL;
		}
		return val.get_type();
	}
	return t.type;
}

void DesignTokenLibrary::set_token_value(int p_index, const Variant &p_value) {
	ERR_FAIL_INDEX(p_index, tokens.size());
	if (tokens[p_index].is_formula) {
		ERR_FAIL_MSG("Cannot set value on a formula token.");
	}
	tokens.write[p_index].value = p_value;
	_invalidate_all_formula_caches();
	emit_changed();
}

Variant DesignTokenLibrary::get_token_value(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, tokens.size(), Variant());
	const Token &t = tokens[p_index];
	if (t.is_formula) {
		String err;
		Variant v = _get_evaluated_value(p_index, &err);
		if (!err.is_empty()) {
			return Variant();
		}
		return v;
	}
	return t.value;
}

void DesignTokenLibrary::remove_token(int p_index) {
	ERR_FAIL_INDEX(p_index, tokens.size());
	String old_name = tokens[p_index].name;
	if (!old_name.is_empty()) {
		name_to_index.erase(StringName(old_name));
	}
	tokens.remove_at(p_index);
	_rebuild_maps();
	// Need to recompile formulas that referenced removed token – they will now error.
	for (int i = 0; i < tokens.size(); i++) {
		if (tokens[i].is_formula) {
			Token &t = tokens.write[i];
			if (t.deps.has(old_name)) {
				String err;
				_compile_formula(i, err);
			}
		}
	}
	_rebuild_dependents();
	_invalidate_all_formula_caches();
	_increment_version();
	notify_property_list_changed();
	emit_changed();
}

void DesignTokenLibrary::insert_token(int p_index, const String &p_name, int p_type, const Variant &p_value, bool p_is_formula, const String &p_formula) {
	ERR_FAIL_COND(p_index < 0 || p_index > tokens.size());
	Token tok;
	tok.name = p_name;
	tok.type = p_type;
	tok.value = p_value;
	tok.is_formula = p_is_formula;
	tok.formula = p_formula;
	tok.dirty = true;
	if (!p_name.is_empty()) {
		ERR_FAIL_COND_MSG(has_token(p_name), vformat("Token name '%s' already exists.", p_name));
	}
	if (!p_name.is_empty() && !_is_valid_token_name(p_name)) {
		ERR_FAIL_MSG(vformat("Invalid token name '%s': must match [_a-zA-Z][_a-zA-Z0-9]*.", p_name));
	}
	tokens.insert(p_index, tok);
	_rebuild_maps();
	if (p_is_formula && !p_formula.is_empty()) {
		String err;
		_compile_formula(p_index, err);
	}
	_rebuild_dependents();
	_invalidate_all_formula_caches();
	_increment_version();
	notify_property_list_changed();
	emit_changed();
	if (!p_name.is_empty()) {
		emit_signal(SNAME("token_renamed"), String(), p_name);
	}
}

void DesignTokenLibrary::set_token_is_formula(int p_index, bool p_is_formula) {
	ERR_FAIL_INDEX(p_index, tokens.size());
	set_token_indexed(p_index, "is_formula", p_is_formula);
}

bool DesignTokenLibrary::is_token_formula(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, tokens.size(), false);
	return tokens[p_index].is_formula;
}

void DesignTokenLibrary::set_token_formula(int p_index, const String &p_formula) {
	ERR_FAIL_INDEX(p_index, tokens.size());
	set_token_indexed(p_index, "formula", p_formula);
}

String DesignTokenLibrary::get_token_formula(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, tokens.size(), String());
	return tokens[p_index].formula;
}

String DesignTokenLibrary::get_token_error(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, tokens.size(), String());
	const Token &t = tokens[p_index];
	if (!t.is_formula) {
		return String();
	}
	if (!t.last_error.is_empty()) {
		return t.last_error;
	}
	// Try evaluating to surface lazy errors.
	String err;
	_get_evaluated_value(p_index, &err);
	return err;
}

bool DesignTokenLibrary::is_token_valid(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, tokens.size(), false);
	const Token &t = tokens[p_index];
	if (!t.is_formula) {
		return true;
	}
	String err = get_token_error(p_index);
	return err.is_empty();
}

Variant DesignTokenLibrary::get_token_value_by_name(const String &p_name) const {
	const StringName key = StringName(p_name);
	auto it = name_to_index.find(key);
	if (it) {
		int idx = it->value;
		if (idx >= 0 && idx < tokens.size() && tokens[idx].name == p_name) {
			const Token &t = tokens[idx];
			if (t.is_formula) {
				String err;
				Variant v = _get_evaluated_value(idx, &err);
				if (!err.is_empty()) {
					return Variant();
				}
				return v;
			}
			return t.value;
		}
	}
	for (int i = 0; i < tokens.size(); i++) {
		if (tokens[i].name == p_name) {
			const Token &t = tokens[i];
			if (t.is_formula) {
				String err;
				Variant v = _get_evaluated_value(i, &err);
				if (!err.is_empty()) {
					return Variant();
				}
				return v;
			}
			return t.value;
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
		Variant::Type ttype;
		if (tokens[i].is_formula && tokens[i].type == Variant::NIL) {
			// Auto: infer from evaluated value, but don't trigger heavy eval for filtering? We do.
			String err;
			Variant v = _get_evaluated_value(i, &err);
			if (!err.is_empty()) {
				continue;
			}
			ttype = v.get_type();
		} else {
			ttype = (Variant::Type)tokens[i].type;
		}
		if (ttype == p_type) {
			names.push_back(tokens[i].name);
		}
	}
	return names;
}
