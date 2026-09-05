/**************************************************************************/
/*  design_token_inspector_plugin.cpp                                     */
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

#include "design_token_inspector_plugin.h"

#include "core/config/project_settings.h"
#include "core/io/resource_loader.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "editor/docks/editor_dock_manager.h"
#include "editor/docks/filesystem_dock.h"
#include "editor/docks/inspector_dock.h"
#include "editor/editor_node.h"
#include "editor/editor_string_names.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor/inspector/editor_inspector.h"
#include "editor/inspector/editor_properties.h"
#include "editor/settings/editor_command_palette.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/check_box.h"
#include "scene/gui/color_picker.h"
#include "scene/gui/control.h"
#include "scene/gui/item_list.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/option_button.h"
#include "scene/gui/popup.h"
#include "scene/gui/scroll_container.h"
#include "scene/gui/separator.h"
#include "scene/main/node.h"
#include "scene/resources/font.h"
#include "scene/resources/style_box.h"
#include "scene/resources/texture.h"
#include "scene/resources/theme.h"

// Links for theme items are stored in a single Dictionary metadata entry:
// theme item paths ("Type/section/item") contain slashes, which Object::set_meta
// rejects as invalid identifier characters, so they can't be stored as
// per-property metadata like node/resource property links ("__design_token_<prop>").
static const char *THEME_LINKS_META_KEY = "_design_token_theme_links";

static Dictionary _get_theme_links(const Object *p_object) {
	return p_object->get_meta(THEME_LINKS_META_KEY, Dictionary());
}

static void _set_theme_links(Object *p_object, const Dictionary &p_links) {
	p_object->set_meta(THEME_LINKS_META_KEY, p_links);
}

// Swaps a token name cell between display mode (name label + pencil button) and
// edit mode (line edit + confirm/cancel buttons), mirroring the theme editor's
// rename row. Child layout: 0 Label, 1 LineEdit, 2 pencil, 3 confirm, 4 cancel.
static void _set_name_edit_mode(HBoxContainer *p_name_box, bool p_edit, const String &p_current_name) {
	Label *name_label = Object::cast_to<Label>(p_name_box->get_child(0));
	LineEdit *name_line_edit = Object::cast_to<LineEdit>(p_name_box->get_child(1));
	Button *pencil_button = Object::cast_to<Button>(p_name_box->get_child(2));
	Button *confirm_button = Object::cast_to<Button>(p_name_box->get_child(3));
	Button *cancel_button = Object::cast_to<Button>(p_name_box->get_child(4));
	ERR_FAIL_NULL(name_label);
	ERR_FAIL_NULL(name_line_edit);
	ERR_FAIL_NULL(pencil_button);
	ERR_FAIL_NULL(confirm_button);
	ERR_FAIL_NULL(cancel_button);

	if (p_edit) {
		name_line_edit->set_text(p_current_name);
		name_line_edit->set_visible(true);
		confirm_button->set_visible(true);
		cancel_button->set_visible(true);

		name_label->set_visible(false);
		pencil_button->set_visible(false);
	} else {
		name_label->set_text(p_current_name);
		name_label->set_visible(true);
		pencil_button->set_visible(true);

		name_line_edit->set_visible(false);
		confirm_button->set_visible(false);
		cancel_button->set_visible(false);
	}
}

// Alphabetical comparators used to sort categories and their token names.
struct TokenTypeAlphCompare {
	_FORCE_INLINE_ bool operator()(int p_a, int p_b) const {
		return String(Variant::get_type_name((Variant::Type)p_a)).naturalnocasecmp_to(String(Variant::get_type_name((Variant::Type)p_b))) < 0;
	}
};

struct TokenNameAlphCompare {
	TokenNameAlphCompare(const Ref<DesignTokenLibrary> &p_library) :
			library(p_library) {}
	_FORCE_INLINE_ bool operator()(int p_a, int p_b) const {
		return String(library->get_token_name(p_a)).naturalnocasecmp_to(String(library->get_token_name(p_b))) < 0;
	}
	const Ref<DesignTokenLibrary> library;
};

// ----------------------------------------------------------------
// DesignTokenPropertyEditor
// ----------------------------------------------------------------

void DesignTokenPropertyEditor::_bind_methods() {
}

void DesignTokenPropertyEditor::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			_refresh_chain_state();
		} break;
		case NOTIFICATION_THEME_CHANGED: {
			_update_chain_visuals();
		} break;
	}
}

void DesignTokenPropertyEditor::_set_read_only(bool p_read_only) {
	inspector_read_only = p_read_only;
	_sync_row_read_only();
}

void DesignTokenPropertyEditor::_sync_row_read_only() {
	// While linked the inspector must keep treating the row as read-only so
	// that neither the revert icon, the "Revert Value" menu item, nor the
	// revert hit-test are able to change the value.
	bool effective = inspector_read_only || !linked_token.is_empty();

	if (sub_editor) {
		sub_editor->set_read_only(effective);
	}

	if (updating_read_only) {
		return;
	}

	if (effective != is_read_only()) {
		updating_read_only = true;
		set_read_only(effective);
		updating_read_only = false;
	}
}

void DesignTokenPropertyEditor::update_property() {
	_refresh_chain_state();
	if (sub_editor) {
		sub_editor->update_property();
	}
}

void DesignTokenPropertyEditor::_on_chain_pressed() {
	if (plugin.is_null()) {
		return;
	}
	Object *obj = get_edited_object();
	if (!obj) {
		return;
	}
	plugin->open_picker(obj, String(get_edited_property()), get_property_type(), chain_button);
}

void DesignTokenPropertyEditor::_refresh_chain_state() {
	Object *obj = get_edited_object();
	String token;
	if (plugin.is_valid() && obj) {
		token = plugin->get_linked_token(obj, String(get_edited_property()));
	}

	if (link_state_checked && token == linked_token) {
		return;
	}

	linked_token = token;
	link_state_checked = true;

	if (plugin.is_valid() && obj && !linked_token.is_empty()) {
		plugin->register_link(obj, String(get_edited_property()));
	}

	_update_chain_visuals();
	_sync_row_read_only();
}

void DesignTokenPropertyEditor::_update_chain_visuals() {
	if (!is_inside_tree()) {
		return;
	}

	// Always use the connected chain icon; state is shown through color only.
	chain_button->set_button_icon(chain_button->get_editor_theme_icon(SNAME("Linked")));

	if (linked_token.is_empty()) {
		Color c(1, 1, 1, 0.85);
		chain_button->set_tooltip_text(TTR("Link this value to a design token"));
		chain_button->add_theme_color_override(SNAME("icon_normal_color"), c);
		chain_button->add_theme_color_override(SNAME("icon_hover_color"), c);
		chain_button->add_theme_color_override(SNAME("icon_pressed_color"), c);
		chain_button->add_theme_color_override(SNAME("icon_focus_color"), c);
		chain_button->add_theme_color_override(SNAME("icon_hover_pressed_color"), c);
	} else {
		Color c = chain_button->get_theme_color(SNAME("accent_color"), EditorStringName(Editor));
		chain_button->set_tooltip_text(vformat(TTR("Linked to \"%s\".\nClick to manage the link."), linked_token));
		chain_button->add_theme_color_override(SNAME("icon_normal_color"), c);
		chain_button->add_theme_color_override(SNAME("icon_hover_color"), c);
		chain_button->add_theme_color_override(SNAME("icon_pressed_color"), c);
		chain_button->add_theme_color_override(SNAME("icon_focus_color"), c);
		chain_button->add_theme_color_override(SNAME("icon_hover_pressed_color"), c);
	}
}

DesignTokenPropertyEditor::DesignTokenPropertyEditor(const Ref<DesignTokenInspectorPlugin> &p_plugin, Object *p_object,
		const String &p_path, const Variant::Type p_type, EditorProperty *p_sub_editor) {
	plugin = p_plugin;
	prop_type = p_type;
	sub_editor = p_sub_editor;
	ERR_FAIL_NULL(sub_editor);

	HBoxContainer *content = memnew(HBoxContainer);
	content->add_theme_constant_override(SNAME("separation"), 2);
	add_child(content);

	chain_button = memnew(Button);
	chain_button->set_flat(true);
	chain_button->set_custom_minimum_size(Size2(20, 20));
	chain_button->connect(SNAME("pressed"), callable_mp(this, &DesignTokenPropertyEditor::_on_chain_pressed));
	content->add_child(chain_button);

	sub_editor->set_object_and_property(p_object, p_path);
	sub_editor->set_name_split_ratio(0);
	sub_editor->set_selectable(false);
	sub_editor->set_use_folding(is_using_folding());
	sub_editor->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	sub_editor->connect(SNAME("property_changed"), callable_mp((EditorProperty *)this, &EditorProperty::emit_changed), CONNECT_DEFERRED);
	sub_editor->connect(SNAME("object_id_selected"), callable_mp(this, &DesignTokenPropertyEditor::_object_id_selected));
	content->add_child(sub_editor);

	// Some default editors occupy a full-width row below the property label.
	if (sub_editor->get_bottom_editor()) {
		set_bottom_editor(content);
	}
}

void DesignTokenPropertyEditor::_object_id_selected(const StringName &p_property, ObjectID p_id) {
	emit_signal(SNAME("object_id_selected"), p_property, p_id);
}

// ----------------------------------------------------------------
// DesignTokenInspectorPlugin
// ----------------------------------------------------------------

void DesignTokenInspectorPlugin::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_library", "library"), &DesignTokenInspectorPlugin::set_library);
	ClassDB::bind_method(D_METHOD("get_library"), &DesignTokenInspectorPlugin::get_library);
	ClassDB::bind_method(D_METHOD("link_property", "object", "property", "token_name"), &DesignTokenInspectorPlugin::link_property);
	ClassDB::bind_method(D_METHOD("unlink_property", "object", "property"), &DesignTokenInspectorPlugin::unlink_property);
	ClassDB::bind_method(D_METHOD("unlink_property_single", "object", "property"), &DesignTokenInspectorPlugin::unlink_property_single);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "library", PROPERTY_HINT_RESOURCE_TYPE, "DesignTokenLibrary"), "set_library", "get_library");
}

void DesignTokenInspectorPlugin::register_link(Object *p_object, const String &p_property) {
	ERR_FAIL_NULL(p_object);
	linked_properties[p_object->get_instance_id()].insert(p_property);
}

void DesignTokenInspectorPlugin::unregister_link(Object *p_object, const String &p_property) {
	ERR_FAIL_NULL(p_object);
	auto iter = linked_properties.find(p_object->get_instance_id());
	if (iter) {
		HashSet<StringName> &set = iter->value;
		set.erase(p_property);
		if (set.is_empty()) {
			linked_properties.remove(iter);
		}
	}
}

void DesignTokenInspectorPlugin::link_property(Object *p_object, const String &p_property, const String &p_token_name) {
	ERR_FAIL_NULL(p_object);
	ERR_FAIL_COND(library.is_null());

	register_link(p_object, p_property);

	if (Theme *theme = Object::cast_to<Theme>(p_object)) {
		Dictionary links = _get_theme_links(theme);
		links[p_property] = p_token_name;
		_set_theme_links(theme, links);
	} else {
		p_object->set_meta("__design_token_" + p_property, p_token_name);
	}

	Variant value = library->get_token_value_by_name(p_token_name);
	if (value.get_type() != Variant::NIL) {
		p_object->set(p_property, value);
	}

	// Theme items apply values through Theme::_set which emits "changed", so any
	// open theme editor pick up the new link through its existing listener.
	if (Theme *theme = Object::cast_to<Theme>(p_object)) {
		theme->emit_changed();
		notify_theme_links_changed(theme);
	}

	_refresh_inspector();
}

void DesignTokenInspectorPlugin::unlink_property(Object *p_object, const String &p_property) {
	ERR_FAIL_NULL(p_object);

	// Capture token name before removal so we can bulk-unlink other items
	// that are connected to the same token (e.g. pinned stylebox followers
	// or multiple theme items linked to the same design token).
	String token_name;
	if (Theme *theme = Object::cast_to<Theme>(p_object)) {
		Dictionary links = _get_theme_links(theme);
		token_name = links.get(p_property, String());
		links.erase(p_property);
		// Also erase any other theme item in the same Theme that is linked
		// to the same token – unlinking via the pinned editor should clear
		// all connected items, not just the one row that was clicked.
		if (!token_name.is_empty()) {
			Array keys = links.keys();
			for (int i = 0; i < keys.size(); i++) {
				Variant k = keys[i];
				if (String(links[k]) == token_name) {
					links.erase(k);
					unregister_link(theme, String(k));
				}
			}
		}
		_set_theme_links(theme, links);
	} else {
		token_name = String(p_object->get_meta("__design_token_" + p_property, String()));
		p_object->remove_meta("__design_token_" + p_property);
		// Bulk unlink for StyleBox resources when the leading pinned
		// stylebox is unlinked. Followers share the same token via the
		// pin propagation, so they must all be cleared together. Only
		// apply bulk logic to StyleBox (and derived) resources to avoid
		// affecting unrelated nodes that happen to share a token name.
		if (!token_name.is_empty() && Object::cast_to<StyleBox>(p_object)) {
			Vector<ObjectID> ids;
			Vector<StringName> props_to_remove;
			for (const KeyValue<ObjectID, HashSet<StringName>> &E : linked_properties) {
				Object *obj = ObjectDB::get_instance(E.key);
				if (!obj || obj == p_object || !Object::cast_to<StyleBox>(obj)) {
					continue;
				}
				for (const StringName &prop : E.value) {
					if (get_linked_token(obj, String(prop)) == token_name) {
						ids.push_back(E.key);
						props_to_remove.push_back(prop);
					}
				}
			}
			for (int i = 0; i < ids.size(); i++) {
				Object *obj = ObjectDB::get_instance(ids[i]);
				if (!obj) {
					continue;
				}
				obj->remove_meta("__design_token_" + String(props_to_remove[i]));
				unregister_link(obj, String(props_to_remove[i]));
			}
		}
	}
	unregister_link(p_object, p_property);

	if (Theme *theme = Object::cast_to<Theme>(p_object)) {
		// No value changes when unlinking, so force a refresh so theme editors
		// re-enable their rows and update the chain button state.
		theme->emit_changed();
		notify_theme_links_changed(theme);
	}

	_refresh_inspector();
}

void DesignTokenInspectorPlugin::unlink_property_single(Object *p_object, const String &p_property) {
	ERR_FAIL_NULL(p_object);

	if (Theme *theme = Object::cast_to<Theme>(p_object)) {
		Dictionary links = _get_theme_links(theme);
		if (!links.has(p_property)) {
			// Still ensure in-memory tracking is cleared for consistency.
			unregister_link(p_object, p_property);
			return;
		}
		links.erase(p_property);
		_set_theme_links(theme, links);
	} else {
		String meta_key = "__design_token_" + p_property;
		if (!p_object->has_meta(meta_key)) {
			// Still ensure in-memory tracking is cleared for consistency.
			unregister_link(p_object, p_property);
			return;
		}
		p_object->remove_meta(meta_key);
	}
	unregister_link(p_object, p_property);

	if (Theme *theme = Object::cast_to<Theme>(p_object)) {
		theme->emit_changed();
		notify_theme_links_changed(theme);
	}

	_refresh_inspector();
}

void DesignTokenInspectorPlugin::_refresh_inspector() {
	EditorInspector *inspector = InspectorDock::get_inspector_singleton();
	if (inspector) {
		// Defer to avoid freeing the EditorProperty that is currently emitting
		// "property_changed" / "color_changed" while we are inside
		// library->emit_changed -> _on_library_changed -> update_tree().
		callable_mp(inspector, &EditorInspector::update_tree).call_deferred();
	}
}

void DesignTokenInspectorPlugin::register_theme_refresh_callback(const Callable &p_callback) {
	if (!p_callback.is_valid()) {
		return;
	}
	if (!theme_refresh_callbacks.has(p_callback)) {
		theme_refresh_callbacks.push_back(p_callback);
	}
}

void DesignTokenInspectorPlugin::unregister_theme_refresh_callback(const Callable &p_callback) {
	theme_refresh_callbacks.erase(p_callback);
}

void DesignTokenInspectorPlugin::notify_theme_links_changed(const Theme *p_theme) {
	for (const Callable &cb : theme_refresh_callbacks) {
		cb.call();
	}
}

bool DesignTokenInspectorPlugin::can_handle(Object *p_object) {
	return p_object != nullptr;
}

void DesignTokenInspectorPlugin::parse_begin(Object *p_object) {
	// DesignTokenLibrary is now edited in the bottom dock (DesignTokenEditor).
	// The inspector shows raw token_N_* properties instead of the custom control.
}

bool DesignTokenInspectorPlugin::parse_property(Object *p_object, const Variant::Type p_type,
		const String &p_path, const PropertyHint p_hint,
		const String &p_hint_text, const BitField<PropertyUsageFlags> p_usage,
		const bool p_wide) {
	if (!p_object) {
		return false;
	}

	// Allow the inspector to show the raw DesignTokenLibrary properties (token_count,
	// token_0_name, etc.) – the friendly editor lives in the bottom dock.
	if (Object::cast_to<DesignTokenLibrary>(p_object)) {
		return false;
	}

	if (p_type == Variant::NIL || p_type == Variant::OBJECT || p_type == Variant::CALLABLE ||
			p_type == Variant::SIGNAL || p_type == Variant::RID) {
		return false;
	}

	if (!(p_usage & PROPERTY_USAGE_EDITOR)) {
		return false;
	}

	if (library.is_null()) {
		return false;
	}

	if (p_path.is_empty()) {
		return false;
	}

	EditorProperty *default_editor = EditorInspectorDefaultPlugin::get_editor_for_property(p_object, p_type, p_path, p_hint, p_hint_text, p_usage, p_wide);
	if (!default_editor) {
		return false;
	}

	DesignTokenPropertyEditor *editor = memnew(DesignTokenPropertyEditor(this, p_object, p_path, p_type, default_editor));
	add_property_editor(p_path, editor);
	return true;
}

String DesignTokenInspectorPlugin::get_linked_token(Object *p_object, const String &p_property) const {
	ERR_FAIL_NULL_V(p_object, String());
	if (const Theme *theme = Object::cast_to<Theme>(p_object)) {
		Dictionary links = _get_theme_links(theme);
		return links.get(p_property, String());
	}
	return p_object->get_meta("__design_token_" + p_property, String());
}

void DesignTokenInspectorPlugin::open_picker(Object *p_object, const String &p_property, Variant::Type p_type, Control *p_anchor) {
	ERR_FAIL_NULL(p_object);

	if (library.is_null()) {
		return;
	}

	picker_anchor = p_anchor;
	pending_object_id = p_object->get_instance_id();
	pending_property = p_property;
	pending_type = p_type;

	_ensure_picker();
	_refresh_token_picker();

	token_picker->set_title(vformat(TTR("Link \"%s\" to a Token"), pending_property));

	// Pin the popup to a fixed size. Popup windows with wrap_controls resize to
	// their content minimum, which on the very first open (before the popup has
	// ever been laid out) can be computed too large and get clamped to the whole
	// embedder rect by Popup::_popup_adjust_rect. Pinning min == max keeps the
	// size deterministic (360x420) on every open.
	const Size2i picker_size = Size2i(360, 420);
	token_picker->set_min_size(picker_size);
	token_picker->set_max_size(picker_size);
	token_picker->reset_size();

	// Position relative to the anchor button, opening above the anchor (which is
	// right under the cursor when invoked from a chain button), horizontally
	// centered on it. Only falls back to below when there isn't room above.
	if (picker_anchor && picker_anchor->is_inside_tree()) {
		Size2 anchor_pos = picker_anchor->get_screen_position();
		float viewport_height = picker_anchor->get_viewport_rect().size.y;
		bool fits_above = anchor_pos.y - picker_size.y >= 0;
		bool fits_below = anchor_pos.y + picker_anchor->get_size().y + picker_size.y <= viewport_height;
		bool show_above = fits_above || !fits_below;
		float v_offset = show_above ? -picker_size.y : picker_anchor->get_size().y;
		float h_offset = (picker_anchor->get_size().x - picker_size.x) / 2.0;
		token_picker->set_position((anchor_pos + Vector2(h_offset, v_offset)).floor());
	} else {
		Rect2i usable_rect = token_picker->get_usable_parent_rect();
		if (usable_rect != Rect2i()) {
			token_picker->set_position(usable_rect.position + (usable_rect.size - picker_size) / 2);
		} else {
			token_picker->set_position(Vector2i());
		}
	}
	token_picker->popup();
	search_line_edit->grab_focus();
}

void DesignTokenInspectorPlugin::_navigate_to_library() {
	if (library.is_null()) {
		return;
	}
	// Open the library in the bottom dock (and inspector). This triggers
	// DesignTokenEditorPlugin::edit via handles()/edit().
	EditorNode::get_singleton()->edit_resource(library);
}

void DesignTokenInspectorPlugin::_ensure_picker() {
	if (token_picker) {
		return;
	}

	token_picker = memnew(PopupPanel);
	token_picker->set_title(TTR("Link to Design Token"));

	VBoxContainer *vbox = memnew(VBoxContainer);
	vbox->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	vbox->set_custom_minimum_size(Size2(0, 300));
	token_picker->add_child(vbox);

	picker_hint = memnew(Label);
	picker_hint->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	picker_hint->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	picker_hint->add_theme_color_override(SNAME("font_color"), Color(0.6, 0.6, 0.6));
	vbox->add_child(picker_hint);

	search_line_edit = memnew(LineEdit);
	search_line_edit->set_placeholder(TTR("Search tokens..."));
	search_line_edit->set_clear_button_enabled(true);
	search_line_edit->connect(SNAME("text_changed"), callable_mp(this, &DesignTokenInspectorPlugin::_on_picker_search));
	vbox->add_child(search_line_edit);

	token_item_list = memnew(ItemList);
	token_item_list->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	token_item_list->connect(SNAME("item_activated"), callable_mp(this, &DesignTokenInspectorPlugin::_on_picker_item_activated));
	vbox->add_child(token_item_list);

	// Create a new token from the current property value.
	HBoxContainer *create_row = memnew(HBoxContainer);
	create_row->add_theme_constant_override(SNAME("separation"), 4);
	new_token_name_edit = memnew(LineEdit);
	new_token_name_edit->set_placeholder(TTR("New token name..."));
	new_token_name_edit->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	new_token_name_edit->connect(SNAME("text_submitted"), callable_mp(this, &DesignTokenInspectorPlugin::_on_picker_create_token));
	create_row->add_child(new_token_name_edit);
	create_token_button = memnew(Button);
	create_token_button->set_text(TTR("Create & Link"));
	create_token_button->connect(SNAME("pressed"), callable_mp(this, &DesignTokenInspectorPlugin::_on_picker_create_token));
	create_row->add_child(create_token_button);
	vbox->add_child(create_row);

	// Open the library / unlink the current link.
	HBoxContainer *action_row = memnew(HBoxContainer);
	action_row->add_theme_constant_override(SNAME("separation"), 4);
	open_in_library_button = memnew(Button);
	open_in_library_button->set_text(TTR("Open in Library..."));
	open_in_library_button->connect(SNAME("pressed"), callable_mp(this, &DesignTokenInspectorPlugin::_on_picker_open_in_library));
	action_row->add_child(open_in_library_button);
	action_row->add_spacer();
	unlink_button = memnew(Button);
	unlink_button->set_text(TTR("Unlink"));
	unlink_button->connect(SNAME("pressed"), callable_mp(this, &DesignTokenInspectorPlugin::_on_picker_unlink));
	action_row->add_child(unlink_button);
	vbox->add_child(action_row);

	EditorNode::get_singleton()->get_gui_base()->add_child(token_picker);
}

void DesignTokenInspectorPlugin::_refresh_token_picker() {
	token_item_list->clear();

	if (library.is_null()) {
		return;
	}

	Vector<String> names = library->get_token_names_for_type(pending_type);
	int current_index = -1;

	Object *obj = ObjectDB::get_instance(pending_object_id);
	String current = (obj && !pending_property.is_empty()) ? get_linked_token(obj, pending_property) : String();

	for (int i = 0; i < names.size(); i++) {
		token_item_list->add_item(names[i]);
		token_item_list->set_item_metadata(i, names[i]);
		if (!current.is_empty() && names[i] == current) {
			current_index = i;
		}
	}

	search_line_edit->set_text("");
	new_token_name_edit->set_text("");

	if (token_item_list->get_item_count() == 0) {
		picker_hint->set_text(vformat(TTR("No \"%s\" tokens yet.\nCreate one below to link this value."), Variant::get_type_name(pending_type)));
	} else {
		picker_hint->set_text(TTR("Select a token, or create a new one from the current value. Double-click to link."));
	}

	if (current_index >= 0) {
		token_item_list->select(current_index);
		token_item_list->ensure_current_is_visible();
	}

	unlink_button->set_disabled(current.is_empty());
}

void DesignTokenInspectorPlugin::_on_picker_search(const String &p_text) {
	ERR_FAIL_COND(library.is_null());

	String search = p_text.to_lower();
	for (int i = 0; i < token_item_list->get_item_count(); i++) {
		String token_name = token_item_list->get_item_metadata(i);
		token_item_list->set_item_disabled(i, !(search.is_empty() || token_name.to_lower().contains(search)));
	}
}

void DesignTokenInspectorPlugin::_on_picker_item_activated(int p_index) {
	ERR_FAIL_COND(p_index < 0 || p_index >= token_item_list->get_item_count());

	Object *obj = ObjectDB::get_instance(pending_object_id);
	ERR_FAIL_NULL(obj);

	String token_name = token_item_list->get_item_metadata(p_index);
	String prop = pending_property;

	String old_token = get_linked_token(obj, prop);
	if (old_token == token_name) {
		token_picker->hide();
		pending_object_id = ObjectID();
		pending_property = String();
		picker_anchor = nullptr;
		return;
	}

	Variant old_value = obj->get(prop);

	EditorUndoRedoManager *ur = EditorUndoRedoManager::get_singleton();
	ur->create_action(TTR("Link Design Token"));
	ur->add_do_method(this, "link_property", obj, prop, token_name);
	if (old_token.is_empty()) {
		ur->add_undo_method(this, "unlink_property_single", obj, prop);
	} else {
		ur->add_undo_method(this, "link_property", obj, prop, old_token);
	}
	ur->add_undo_property(obj, prop, old_value);
	ur->commit_action();

	token_picker->hide();
	pending_object_id = ObjectID();
	pending_property = String();
	picker_anchor = nullptr;
}

void DesignTokenInspectorPlugin::_on_picker_create_token() {
	ERR_FAIL_COND(library.is_null());

	Object *obj = ObjectDB::get_instance(pending_object_id);
	ERR_FAIL_NULL(obj);

	String name = new_token_name_edit->get_text().strip_edges();
	if (name.is_empty()) {
		return;
	}
	if (!DesignTokenLibrary::is_valid_token_name_static(name)) {
		picker_hint->set_text(vformat(TTR("Invalid token name \"%s\"."), name));
		return;
	}
	if (library->has_token(name)) {
		picker_hint->set_text(vformat(TTR("A token named \"%s\" already exists."), name));
		return;
	}

	int index = library->get_token_count();
	String prop = pending_property;
	Variant val = obj->get(prop);
	String old_token = get_linked_token(obj, prop);
	EditorUndoRedoManager *ur = EditorUndoRedoManager::get_singleton();
	ur->create_action(TTR("Create Design Token and Link"));
	ur->add_do_method(*library, "insert_token", index, name, (int)pending_type, val, false, String());
	ur->add_undo_method(*library, "remove_token", index);
	ur->add_do_method(this, "link_property", obj, prop, name);
	if (old_token.is_empty()) {
		ur->add_undo_method(this, "unlink_property_single", obj, prop);
	} else if (old_token != name) {
		ur->add_undo_method(this, "link_property", obj, prop, old_token);
	}
	ur->add_undo_property(obj, prop, val);
	ur->commit_action();

	token_picker->hide();
	pending_object_id = ObjectID();
	pending_property = String();
	picker_anchor = nullptr;
}

void DesignTokenInspectorPlugin::_on_picker_open_in_library() {
	token_picker->hide();
	_navigate_to_library();
}

void DesignTokenInspectorPlugin::_on_picker_unlink() {
	Object *obj = ObjectDB::get_instance(pending_object_id);
	String prop = pending_property;
	if (obj && !prop.is_empty()) {
		String token = get_linked_token(obj, prop);
		if (token.is_empty()) {
			// Nothing linked; nothing to record in undo history.
		} else {
			// Collect every link that unlink_property() will clear in bulk so
			// undo can restore them all by re-linking to the same token.
			Vector<Object *> undo_objs;
			Vector<String> undo_props;
			if (Object::cast_to<Theme>(obj)) {
				Dictionary links = _get_theme_links(obj);
				for (const Variant &k : links.keys()) {
					if (String(links[k]) == token) {
						undo_objs.push_back(obj);
						undo_props.push_back(String(k));
					}
				}
				// Fallback to at least the target when metadata is out of sync.
				if (undo_objs.is_empty()) {
					undo_objs.push_back(obj);
					undo_props.push_back(prop);
				}
			} else if (Object::cast_to<StyleBox>(obj)) {
				undo_objs.push_back(obj);
				undo_props.push_back(prop);
				for (const KeyValue<ObjectID, HashSet<StringName>> &E : linked_properties) {
					Object *other = ObjectDB::get_instance(E.key);
					if (!other || other == obj || !Object::cast_to<StyleBox>(other)) {
						continue;
					}
					for (const StringName &p : E.value) {
						if (get_linked_token(other, String(p)) == token) {
							undo_objs.push_back(other);
							undo_props.push_back(String(p));
						}
					}
				}
			} else {
				undo_objs.push_back(obj);
				undo_props.push_back(prop);
			}

			EditorUndoRedoManager *ur = EditorUndoRedoManager::get_singleton();
			ur->create_action(TTR("Unlink Design Token"));
			ur->add_do_method(this, "unlink_property", obj, prop);
			for (int i = 0; i < undo_objs.size(); i++) {
				ur->add_undo_method(this, "link_property", undo_objs[i], undo_props[i], token);
			}
			ur->commit_action();
		}
	}
	token_picker->hide();
	pending_object_id = ObjectID();
	pending_property = String();
	picker_anchor = nullptr;
}

void DesignTokenInspectorPlugin::_scan_and_propagate_deferred() {
	scan_pending = false;
	if (library.is_null()) {
		return;
	}
	size_t before = linked_properties.size();
	_scan_edited_scenes_links();
	if (linked_properties.size() == before) {
		return;
	}
	// New links discovered, propagate current values to them.
	for (const KeyValue<ObjectID, HashSet<StringName>> &E : linked_properties) {
		Object *obj = ObjectDB::get_instance(E.key);
		if (!obj) {
			continue;
		}
		for (const StringName &prop : E.value) {
			String token_name = get_linked_token(obj, prop);
			if (token_name.is_empty()) {
				continue;
			}
			Variant value = library->get_token_value_by_name(token_name);
			if (value.get_type() != Variant::NIL) {
				obj->set(prop, value);
				if (Theme *theme = Object::cast_to<Theme>(obj)) {
					theme->emit_changed();
				}
			}
		}
	}
}

void DesignTokenInspectorPlugin::_on_library_changed() {
	if (library.is_null()) {
		return;
	}

	// Only scan for previously saved links when structural version changes
	// (add/remove/rename/type). Value-only edits keep the same structural
	// version, so scanning every drag would walk the entire scene tree for
	// no reason. Defer the scan to avoid blocking live drags.
	uint64_t cur_ver = library->get_structural_version();
	if (cur_ver != last_scan_structural_version) {
		last_scan_structural_version = cur_ver;
		if (!scan_pending) {
			scan_pending = true;
			callable_mp(this, &DesignTokenInspectorPlugin::_scan_and_propagate_deferred).call_deferred();
		}
		// Still do an immediate lightweight scan of already-known objects
		// via the incremental register_link path; the full scene walk is deferred.
	} else if (linked_properties.is_empty() && scanned_objects.is_empty()) {
		// First edit after startup with no prior scan – do it now to bootstrap.
		_scan_edited_scenes_links();
		last_scan_structural_version = cur_ver;
	}

	// Propagate token values to every object with linked properties.
	List<ObjectID> stale_ids;
	for (const KeyValue<ObjectID, HashSet<StringName>> &E : linked_properties) {
		Object *obj = ObjectDB::get_instance(E.key);
		if (!obj) {
			stale_ids.push_back(E.key);
			continue;
		}

		for (const StringName &prop : E.value) {
			String token_name = get_linked_token(obj, prop);
			if (token_name.is_empty()) {
				continue;
			}
			Variant value = library->get_token_value_by_name(token_name);
			if (value.get_type() != Variant::NIL) {
				obj->set(prop, value);
				if (Theme *theme = Object::cast_to<Theme>(obj)) {
					// Object::set() on a Theme applies dynamic properties without
					// notifying, so emit "changed" so open theme editors refresh.
					theme->emit_changed();
				}
			}
		}
	}

	for (const ObjectID &oid : stale_ids) {
		linked_properties.erase(oid);
	}

	// Only rebuild the inspector if the currently edited object actually has
	// linked properties, so editing the library itself isn't disrupted.
	// Also refresh the raw DesignTokenLibrary inspector so its token_N_*
	// fields update after undo/redo value changes.
	// Defer to avoid freeing the EditorProperty/ColorPickerButton that is
	// currently emitting "property_changed" -> library->set -> emit_changed.
	EditorInspector *inspector = InspectorDock::get_inspector_singleton();
	if (inspector) {
		Object *inspected = inspector->get_edited_object();
		if (inspected && (linked_properties.has(inspected->get_instance_id()) || Object::cast_to<DesignTokenLibrary>(inspected))) {
			callable_mp(inspector, &EditorInspector::update_tree).call_deferred();
		}
	}
}

void DesignTokenInspectorPlugin::_scan_edited_scenes_links() {
	EditorData &editor_data = EditorNode::get_editor_data();
	const int scene_count = editor_data.get_edited_scene_count();
	for (int i = 0; i < scene_count; i++) {
		Node *root = editor_data.get_edited_scene_root(i);
		if (root) {
			_discover_node_links(root);
		}
	}

	// A Theme (or any other resource) opened in the Inspector may not be a
	// scene root, so scan it as well.
	EditorInspector *inspector = InspectorDock::get_inspector_singleton();
	if (inspector) {
		Object *edited = inspector->get_edited_object();
		if (edited) {
			_discover_object_links(edited);
		}
	}
}

void DesignTokenInspectorPlugin::_discover_node_links(Node *p_node) {
	ERR_FAIL_NULL(p_node);

	_discover_object_links(p_node);

	// Theme items are linked through an explicit Theme resource; discover the
	// effective theme of every control so linked themes propagate too.
	if (Control *control = Object::cast_to<Control>(p_node)) {
		Ref<Theme> theme = control->get_theme();
		if (theme.is_valid()) {
			_discover_object_links(theme.ptr());
		}
	}

	for (int i = 0; i < p_node->get_child_count(); i++) {
		_discover_node_links(p_node->get_child(i));
	}
}

void DesignTokenInspectorPlugin::_discover_object_links(Object *p_object) {
	ERR_FAIL_NULL(p_object);

	const ObjectID id = p_object->get_instance_id();
	if (scanned_objects.has(id)) {
		return;
	}
	scanned_objects.insert(id);

	if (const Theme *theme = Object::cast_to<Theme>(p_object)) {
		_register_theme_links(theme);
		return;
	}

	const String prefix = "__design_token_";
	HashSet<StringName> props;
	List<StringName> meta_keys;
	p_object->get_meta_list(&meta_keys);
	for (const StringName &meta : meta_keys) {
		const String meta_name = String(meta);
		if (meta_name.begins_with(prefix)) {
			props.insert(StringName(meta_name.substr(prefix.length())));
		}
	}

	if (!props.is_empty()) {
		linked_properties[id] = props;
	}

	// Linked values often live on sub-resources (e.g. a Button's
	// theme_override StyleBox or a Label's LabelSettings), not on the node
	// itself. Recurse into Resource properties so those links are registered
	// without having to open each resource in the inspector first.
	_discover_resource_properties(p_object);
}

void DesignTokenInspectorPlugin::_discover_resource_properties(Object *p_object) {
	ERR_FAIL_NULL(p_object);

	// The library itself never holds token links; skip it to avoid noise.
	if (Object::cast_to<DesignTokenLibrary>(p_object)) {
		return;
	}

	List<PropertyInfo> plist;
	p_object->get_property_list(&plist);
	for (const PropertyInfo &pi : plist) {
		if (pi.type != Variant::OBJECT) {
			continue;
		}
		// Scripts can't hold token links; skip to avoid descending into them.
		if (pi.name == SNAME("script")) {
			continue;
		}
		Variant v = p_object->get(pi.name);
		if (v.get_type() != Variant::OBJECT) {
			continue;
		}
		Object *child = v.get_validated_object();
		if (!child) {
			continue;
		}
		// Nodes are covered by the explicit scene-tree walk; only follow
		// Resources here (StyleBox, LabelSettings, Font, ...).
		if (!Object::cast_to<Resource>(child)) {
			continue;
		}
		_discover_object_links(child);
	}
}

void DesignTokenInspectorPlugin::register_theme_links(const Theme *p_theme) {
	ERR_FAIL_NULL(p_theme);
	_register_theme_links(p_theme);
}

void DesignTokenInspectorPlugin::_register_theme_links(const Theme *p_theme) {
	ERR_FAIL_NULL(p_theme);

	const ObjectID id = p_theme->get_instance_id();
	const Dictionary links = _get_theme_links(p_theme);
	HashSet<StringName> props;
	for (const Variant &key : links.keys()) {
		props.insert(key);
	}

	if (!props.is_empty()) {
		linked_properties[id] = props;
	}
	scanned_objects.insert(id);

	// Theme items can point to sub-resources (StyleBox, Font, Texture2D, ...).
	// Links are stored directly on those sub-resources, so discover them too.
	List<StringName> type_list;
	p_theme->get_type_list(&type_list);
	for (const StringName &type_name : type_list) {
		List<StringName> style_list;
		p_theme->get_stylebox_list(type_name, &style_list);
		for (const StringName &style_name : style_list) {
			Ref<StyleBox> style = p_theme->get_stylebox(style_name, type_name);
			if (style.is_valid()) {
				_discover_object_links(style.ptr());
			}
		}
		List<StringName> font_list;
		p_theme->get_font_list(type_name, &font_list);
		for (const StringName &font_name : font_list) {
			Ref<Font> font = p_theme->get_font(font_name, type_name);
			if (font.is_valid()) {
				_discover_object_links(font.ptr());
			}
		}
		List<StringName> icon_list;
		p_theme->get_icon_list(type_name, &icon_list);
		for (const StringName &icon_name : icon_list) {
			Ref<Texture2D> icon = p_theme->get_icon(icon_name, type_name);
			if (icon.is_valid()) {
				_discover_object_links(icon.ptr());
			}
		}
	}
}

void DesignTokenInspectorPlugin::set_library(const Ref<DesignTokenLibrary> &p_library) {
	if (library.is_valid()) {
		library->disconnect("changed", callable_mp(this, &DesignTokenInspectorPlugin::_on_library_changed));
		library->disconnect("token_renamed", callable_mp(this, &DesignTokenInspectorPlugin::_on_token_renamed));
	}

	library = p_library;

	if (library.is_valid()) {
		library->connect("changed", callable_mp(this, &DesignTokenInspectorPlugin::_on_library_changed));
		library->connect("token_renamed", callable_mp(this, &DesignTokenInspectorPlugin::_on_token_renamed));
	}

	// Reloading or swapping the library may add/remove chain buttons.
	_refresh_inspector();

	// Discover links saved in object metadata from previous editor sessions so
	// propagation and chain-button state work before the first token edit.
	last_scan_structural_version = library.is_valid() ? library->get_structural_version() : 0;
	scan_pending = false;
	_scan_edited_scenes_links();
}

Ref<DesignTokenLibrary> DesignTokenInspectorPlugin::get_library() const {
	return library;
}

DesignTokenInspectorPlugin *DesignTokenInspectorPlugin::singleton = nullptr;

DesignTokenInspectorPlugin::DesignTokenInspectorPlugin() {
	singleton = this;
}

DesignTokenInspectorPlugin::~DesignTokenInspectorPlugin() {
	if (singleton == this) {
		singleton = nullptr;
	}
	if (library.is_valid()) {
		library->disconnect("changed", callable_mp(this, &DesignTokenInspectorPlugin::_on_library_changed));
		library->disconnect("token_renamed", callable_mp(this, &DesignTokenInspectorPlugin::_on_token_renamed));
	}
	if (token_picker) {
		if (token_picker->get_parent()) {
			token_picker->get_parent()->remove_child(token_picker);
		}
		memdelete(token_picker);
		token_picker = nullptr;
	}
}

void DesignTokenInspectorPlugin::_on_token_renamed(const String &p_old_name, const String &p_new_name) {
	if (p_old_name.is_empty() || p_old_name == p_new_name) {
		return;
	}

	// Update every link that referenced the old token name so propagation and
	// the chain tooltips follow the rename.
	for (const KeyValue<ObjectID, HashSet<StringName>> &E : linked_properties) {
		Object *obj = ObjectDB::get_instance(E.key);
		if (!obj) {
			continue;
		}
		if (Theme *theme = Object::cast_to<Theme>(obj)) {
			Dictionary links = _get_theme_links(theme);
			bool changed = false;
			for (const StringName &prop : E.value) {
				String token = links.get(String(prop), String());
				if (token == p_old_name) {
					links[String(prop)] = p_new_name;
					changed = true;
				}
			}
			if (changed) {
				_set_theme_links(theme, links);
				theme->emit_changed();
			}
		} else {
			for (const StringName &prop : E.value) {
				String meta_key = "__design_token_" + prop;
				if (obj->has_meta(meta_key) && String(obj->get_meta(meta_key)) == p_old_name) {
					obj->set_meta(meta_key, p_new_name);
				}
			}
		}
	}

	_refresh_inspector();
}

void DesignTokenEditorPlugin::_bind_methods() {
}

void DesignTokenEditorPlugin::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			ProjectSettings::get_singleton()->connect("settings_changed", callable_mp(this, &DesignTokenEditorPlugin::_on_project_settings_changed));
		} break;
		case NOTIFICATION_EXIT_TREE: {
			ProjectSettings::get_singleton()->disconnect("settings_changed", callable_mp(this, &DesignTokenEditorPlugin::_on_project_settings_changed));
		} break;
	}
}

void DesignTokenEditorPlugin::_on_project_settings_changed() {
	String path = GLOBAL_GET("editor/design_tokens/library_path");
	if (path == loaded_library_path) {
		return;
	}
	loaded_library_path = path;

	Ref<DesignTokenLibrary> lib;
	if (!path.is_empty()) {
		lib = ResourceLoader::load(path);
	}

	inspector_plugin->set_library(lib);
}

void DesignTokenEditorPlugin::edit(Object *p_object) {
	DesignTokenLibrary *lib = Object::cast_to<DesignTokenLibrary>(p_object);
	if (lib) {
		design_token_editor->edit(Ref<DesignTokenLibrary>(lib));
	}
}

bool DesignTokenEditorPlugin::handles(Object *p_object) const {
	return Object::cast_to<DesignTokenLibrary>(p_object) != nullptr;
}

void DesignTokenEditorPlugin::make_visible(bool p_visible) {
	if (p_visible) {
		design_token_editor->make_visible();
	} else {
		design_token_editor->close();
	}
}

bool DesignTokenEditorPlugin::can_auto_hide() const {
	return design_token_editor->get_edited_library().is_null();
}

DesignTokenEditorPlugin::DesignTokenEditorPlugin() {
	inspector_plugin.instantiate();

	String path = GLOBAL_GET("editor/design_tokens/library_path");
	loaded_library_path = path;

	if (!path.is_empty()) {
		Ref<DesignTokenLibrary> lib = ResourceLoader::load(path);
		if (lib.is_valid()) {
			inspector_plugin->set_library(lib);
		}
	}

	add_inspector_plugin(inspector_plugin);

	design_token_editor = memnew(DesignTokenEditor);
	design_token_editor->plugin = this;
	EditorDockManager::get_singleton()->add_dock(design_token_editor);
	design_token_editor->close();
}

DesignTokenEditorPlugin::~DesignTokenEditorPlugin() {
	if (design_token_editor) {
		EditorDockManager::get_singleton()->remove_dock(design_token_editor);
		memdelete(design_token_editor);
		design_token_editor = nullptr;
	}
}

// ---------------------------------------------------------------------------
// DesignTokenEditor (bottom dock)
// ---------------------------------------------------------------------------

void DesignTokenEditor::edit(const Ref<DesignTokenLibrary> &p_library) {
	if (library == p_library) {
		return;
	}
	library = p_library;
	if (library_editor) {
		library_editor->set_library(p_library);
	}
	if (library.is_valid()) {
		_update_library_name(library->get_path().get_file());
		if (library->get_path().is_empty()) {
			_update_library_name(TTR("Unsaved DesignTokenLibrary"));
		}
	} else {
		_update_library_name(String());
	}
}

void DesignTokenEditor::_update_library_name(const String &p_name) {
	if (!library_name) {
		return;
	}
	String name = p_name;
	if (name.is_empty() && library.is_valid()) {
		name = library->get_path().is_empty() ? TTR("Unsaved DesignTokenLibrary") : library->get_path().get_file();
	}
	if (name.is_empty()) {
		name = TTR("No DesignTokenLibrary");
	}
	library_name->set_text(name);
	library_name->set_tooltip_text(name);
	// No custom minimum size – let the label expand naturally without
	// adding an extra character width of horizontal empty space.
	library_name->set_custom_minimum_size(Size2(0, 0));
}

void DesignTokenEditor::_save_button_cbk(bool p_save_as) {
	ERR_FAIL_COND_MSG(library.is_null(), "No DesignTokenLibrary to save.");
	if (p_save_as) {
		EditorNode::get_singleton()->save_resource_as(library);
	} else {
		EditorNode::get_singleton()->save_resource(library);
	}
}

void DesignTokenEditor::_edit_button_cbk() {
	if (library.is_valid()) {
		EditorNode::get_singleton()->edit_resource(library);
	}
}

void DesignTokenEditor::_dock_closed_cbk() {
	if (library.is_valid() && InspectorDock::get_inspector_singleton()->get_edited_object() == library.ptr()) {
		EditorNode::get_singleton()->push_item(nullptr);
	}
	library = Ref<DesignTokenLibrary>();
	if (library_editor) {
		library_editor->set_library(Ref<DesignTokenLibrary>());
	}
	_update_library_name(String());
}

void DesignTokenEditor::_scene_closed(const String &p_path) {
	if (library.is_valid() && library->is_built_in() && library->get_path().get_slice("::", 0) == p_path) {
		library = Ref<DesignTokenLibrary>();
		if (library_editor) {
			library_editor->set_library(Ref<DesignTokenLibrary>());
		}
		_update_library_name(String());
		EditorNode::get_singleton()->hide_unused_editors(plugin);
	}
}

void DesignTokenEditor::_resource_saved(const Ref<Resource> &p_resource) {
	if (library.is_valid() && library == p_resource) {
		_update_library_name(library->get_path().get_file());
	}
}

void DesignTokenEditor::_files_moved(const String &p_old_path, const String &p_new_path) {
	if (library.is_valid() && (library->get_path() == p_old_path || library->get_path() == p_new_path)) {
		_update_library_name(p_new_path.get_file());
	}
}

void DesignTokenEditor::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			connect("closed", callable_mp(this, &DesignTokenEditor::_dock_closed_cbk));
			EditorNode::get_singleton()->connect("scene_closed", callable_mp(this, &DesignTokenEditor::_scene_closed));
			EditorNode::get_singleton()->connect("resource_saved", callable_mp(this, &DesignTokenEditor::_resource_saved));
			FileSystemDock::get_singleton()->connect("files_moved", callable_mp(this, &DesignTokenEditor::_files_moved));
		} break;
		case NOTIFICATION_THEME_CHANGED: {
			if (close_button) {
				close_button->set_button_icon(get_editor_theme_icon(SNAME("Close")));
			}
			if (edit_button) {
				edit_button->set_button_icon(get_editor_theme_icon(SNAME("Edit")));
			}
		} break;
	}
}

DesignTokenEditor::DesignTokenEditor() {
	set_name(TTRC("Design Tokens"));
	set_icon_name("ColorPick");
	set_dock_shortcut(ED_SHORTCUT_AND_COMMAND("bottom_panels/toggle_design_tokens_bottom_panel", TTRC("Toggle Design Tokens Dock")));
	set_default_slot(EditorDock::DOCK_SLOT_BOTTOM);
	set_available_layouts(EditorDock::DOCK_LAYOUT_HORIZONTAL | EditorDock::DOCK_LAYOUT_FLOATING);
	set_global(false);
	set_transient(true);
	set_closable(true);
	set_custom_minimum_size(Size2(0, 220 * EDSCALE));

	VBoxContainer *content_vb = memnew(VBoxContainer);
	content_vb->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	content_vb->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	add_child(content_vb);

	HBoxContainer *top_menu = memnew(HBoxContainer);
	content_vb->add_child(top_menu);

	Label *lib_label = memnew(Label);
	lib_label->set_text(TTRC("Library:"));
	top_menu->add_child(lib_label);

	library_name = memnew(Label);
	library_name->set_text(TTR("No DesignTokenLibrary"));
	library_name->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
	library_name->set_theme_type_variation("HeaderSmall");
	library_name->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	library_name->set_auto_translate_mode(AUTO_TRANSLATE_MODE_DISABLED);
	library_name->set_mouse_filter(Control::MOUSE_FILTER_PASS);
	top_menu->add_child(library_name);

	edit_button = memnew(Button);
	edit_button->set_text(TTRC("Edit in Inspector"));
	edit_button->set_tooltip_text(TTRC("Show raw resource in Inspector."));
	edit_button->set_flat(true);
	edit_button->connect(SceneStringName(pressed), callable_mp(this, &DesignTokenEditor::_edit_button_cbk));
	top_menu->add_child(edit_button);

	top_menu->add_child(memnew(VSeparator));

	Button *save_button = memnew(Button);
	save_button->set_text(TTRC("Save"));
	save_button->set_flat(true);
	save_button->connect(SceneStringName(pressed), callable_mp(this, &DesignTokenEditor::_save_button_cbk).bind(false));
	top_menu->add_child(save_button);

	Button *save_as_button = memnew(Button);
	save_as_button->set_text(TTRC("Save As..."));
	save_as_button->set_flat(true);
	save_as_button->connect(SceneStringName(pressed), callable_mp(this, &DesignTokenEditor::_save_button_cbk).bind(true));
	top_menu->add_child(save_as_button);

	top_menu->add_child(memnew(VSeparator));

	close_button = memnew(Button);
	close_button->set_tooltip_text(TTRC("Close"));
	close_button->set_flat(true);
	close_button->connect(SceneStringName(pressed), callable_mp((EditorDock *)this, &EditorDock::close));
	top_menu->add_child(close_button);

	ScrollContainer *sc = memnew(ScrollContainer);
	sc->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	sc->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	sc->set_horizontal_scroll_mode(ScrollContainer::SCROLL_MODE_DISABLED);
	content_vb->add_child(sc);

	library_editor = memnew(DesignTokenLibraryEditor);
	library_editor->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	library_editor->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	sc->add_child(library_editor);

	_update_library_name();
}

// ---------------------------------------------------------------------------
// DesignTokenLibraryEditor
// ---------------------------------------------------------------------------

Vector<Variant::Type> DesignTokenLibraryEditor::get_allowed_types() {
	Vector<Variant::Type> types;
	for (int t = 0; t < Variant::VARIANT_MAX; t++) {
		Variant::Type type = (Variant::Type)t;
		if (type == Variant::NIL || type == Variant::OBJECT || type == Variant::CALLABLE ||
				type == Variant::SIGNAL || type == Variant::RID) {
			continue;
		}
		types.push_back(type);
	}
	return types;
}

void DesignTokenLibraryEditor::_bind_methods() {
}

void DesignTokenLibraryEditor::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			if (type_selector && type_selector->get_item_count() == 0) {
				Vector<Variant::Type> types = get_allowed_types();
				for (int i = 0; i < types.size(); i++) {
					type_selector->add_item(Variant::get_type_name(types[i]), (int)types[i]);
				}
				type_selector->select(0);
			}
		} break;
		case NOTIFICATION_THEME_CHANGED: {
			// Rows are built before entering the tree, so the editor theme icon
			// textures aren't available until the theme has been propagated here.
			for (Button *b : pencil_buttons) {
				b->set_button_icon(get_editor_theme_icon(SNAME("Edit")));
			}
			for (Button *b : confirm_buttons) {
				b->set_button_icon(get_editor_theme_icon(SNAME("ImportCheck")));
			}
			for (Button *b : cancel_buttons) {
				b->set_button_icon(get_editor_theme_icon(SNAME("ImportFail")));
			}
			for (Button *b : delete_buttons) {
				b->set_button_icon(get_editor_theme_icon(SNAME("Remove")));
			}
			for (Button *b : fx_buttons) {
				if (b->is_pressed()) {
					Color accent = get_theme_color(SNAME("accent_color"), EditorStringName(Editor));
					b->add_theme_color_override(SNAME("font_color"), accent);
					b->add_theme_color_override(SNAME("font_pressed_color"), accent);
					b->add_theme_color_override(SNAME("font_hover_color"), accent);
					b->add_theme_color_override(SNAME("font_focus_color"), accent);
				} else {
					b->remove_theme_color_override(SNAME("font_color"));
					b->remove_theme_color_override(SNAME("font_pressed_color"));
					b->remove_theme_color_override(SNAME("font_hover_color"));
					b->remove_theme_color_override(SNAME("font_focus_color"));
				}
			}
		} break;
	}
}

DesignTokenLibraryEditor::DesignTokenLibraryEditor() {
	add_bar = memnew(HBoxContainer);
	add_bar->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	add_bar->add_theme_constant_override(SNAME("separation"), 4);
	add_child(add_bar);

	type_selector = memnew(OptionButton);
	add_bar->add_child(type_selector);
	type_selector->connect(SceneStringName(item_selected), callable_mp(this, &DesignTokenLibraryEditor::_on_type_selected));

	name_edit = memnew(LineEdit);
	name_edit->set_placeholder(TTR("Token name [_a-zA-Z][_a-zA-Z0-9]*"));
	name_edit->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	add_bar->add_child(name_edit);
	name_edit->connect(SceneStringName(text_submitted), callable_mp(this, &DesignTokenLibraryEditor::_on_name_submitted));

	formula_check = memnew(CheckBox);
	formula_check->set_text(TTR("Formula"));
	formula_check->set_tooltip_text(TTR("Create as formula token (expression of other tokens)."));
	add_bar->add_child(formula_check);
	formula_check->connect(SNAME("toggled"), callable_mp(this, &DesignTokenLibraryEditor::_on_formula_check_toggled));

	formula_edit = memnew(LineEdit);
	formula_edit->set_placeholder(TTR("Formula e.g. a * b + c"));
	formula_edit->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	formula_edit->hide();
	add_bar->add_child(formula_edit);
	formula_edit->connect(SceneStringName(text_submitted), callable_mp(this, &DesignTokenLibraryEditor::_on_name_submitted));

	add_button = memnew(Button);
	add_button->set_text(TTR("Add Token"));
	add_bar->add_child(add_button);
	add_button->connect(SceneStringName(pressed), callable_mp(this, &DesignTokenLibraryEditor::_on_add_pressed));

	list_vbox = memnew(VBoxContainer);
	list_vbox->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	list_vbox->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	add_child(list_vbox);

	empty_hint = memnew(Label);
	empty_hint->set_text(TTR("No design tokens yet. Add one above."));
	empty_hint->set_modulate(Color(1, 1, 1, 0.6));
	add_child(empty_hint);
}

DesignTokenLibraryEditor::~DesignTokenLibraryEditor() {
	if (library.is_valid()) {
		library->disconnect("changed", callable_mp(this, &DesignTokenLibraryEditor::_on_library_changed));
	}
}

void DesignTokenLibraryEditor::set_library(const Ref<DesignTokenLibrary> &p_library) {
	if (library.is_valid()) {
		library->disconnect("changed", callable_mp(this, &DesignTokenLibraryEditor::_on_library_changed));
	}
	library = p_library;
	if (library.is_valid()) {
		library->connect("changed", callable_mp(this, &DesignTokenLibraryEditor::_on_library_changed));
	}
	last_structural_version = library.is_valid() ? library->get_structural_version() : 0;
	_rebuild();
}

void DesignTokenLibraryEditor::_on_library_changed() {
	if (library.is_null()) {
		return;
	}
	uint64_t ver = library->get_structural_version();
	if (ver == last_structural_version) {
		// Value-only change. During a live drag (ColorPicker popup or
		// EditorSpinSlider p_changing) keep the live value and avoid
		// refreshing, which would revert the drag.
		if (!drag_start_values.is_empty() || _is_any_color_picker_popup_visible()) {
			return;
		}
		// Defer so EditorPropertyColor::_popup_closed's intermediate
		// `set(last_color)` (revert to original) does not clobber the
		// following `emit_changed(new)`; both will be queued deferred and
		// the final `new` wins. This fixes click-away revert.
		callable_mp(this, &DesignTokenLibraryEditor::_refresh_value_editors).call_deferred();
		return;
	}
	last_structural_version = ver;
	// Defer rebuild so we never delete the EditorProperty (e.g. ColorPickerButton)
	// that is currently emitting "property_changed" -> _on_value_changed -> emit_changed.
	// Direct rebuild would free the emitting control mid-signal and crash.
	if (is_inside_tree()) {
		callable_mp(this, &DesignTokenLibraryEditor::_rebuild).call_deferred();
	} else {
		_rebuild();
	}
}

void DesignTokenLibraryEditor::_refresh_value_editors() {
	if (library.is_null() || !list_vbox) {
		return;
	}
	// Walk sections -> rows -> value editors and refresh from library.
	// Also refresh formula tokens' LineEdit and preview Label.
	// The row layout is: [0] name_box(HBox) [1] fx(Button) [2] value_box(HBox) [3] delete(Button)
	// `value_box` holds either `EditorProperty` (non-formula) or `LineEdit+Label` (formula).
	// We must only touch `value_box` to avoid overwriting the token name Label in `name_box`.
	for (int i = 0; i < list_vbox->get_child_count(); i++) {
		EditorInspectorSection *section = Object::cast_to<EditorInspectorSection>(list_vbox->get_child(i));
		if (!section) {
			continue;
		}
		VBoxContainer *rows = section->get_vbox();
		if (!rows) {
			continue;
		}
		for (int r = 0; r < rows->get_child_count(); r++) {
			HBoxContainer *row = Object::cast_to<HBoxContainer>(rows->get_child(r));
			if (!row || row->get_child_count() < 3) {
				continue;
			}
			// Resolve idx from the name_box Label (first child of name_box).
			String token_name;
			if (HBoxContainer *name_box = Object::cast_to<HBoxContainer>(row->get_child(0))) {
				if (name_box->get_child_count() > 0) {
					if (Label *lbl = Object::cast_to<Label>(name_box->get_child(0))) {
						token_name = lbl->get_text();
					}
				}
			}
			int idx = -1;
			if (!token_name.is_empty()) {
				for (int k = 0; k < library->get_token_count(); k++) {
					if (library->get_token_name(k) == token_name) {
						idx = k;
						break;
					}
				}
			}
			if (idx == -1) {
				continue;
			}
			HBoxContainer *value_box = Object::cast_to<HBoxContainer>(row->get_child(2));
			if (!value_box) {
				continue;
			}
			for (int k = 0; k < value_box->get_child_count(); k++) {
				Control *inner = Object::cast_to<Control>(value_box->get_child(k));
				if (!inner) {
					continue;
				}
				if (EditorProperty *ep = Object::cast_to<EditorProperty>(inner)) {
					ep->update_property();
					continue;
				}
				if (!library->is_token_formula(idx)) {
					continue;
				}
				if (LineEdit *le = Object::cast_to<LineEdit>(inner)) {
					Control *focused = is_inside_tree() && get_viewport() ? get_viewport()->gui_get_focus_owner() : nullptr;
					if (focused != le) {
						le->set_text(library->get_token_formula(idx));
					}
					continue;
				}
				if (Label *lab = Object::cast_to<Label>(inner)) {
					String err = library->get_token_error(idx);
					if (!err.is_empty()) {
						lab->set_text(err);
						lab->add_theme_color_override(SNAME("font_color"), Color(1, 0.3, 0.3));
					} else {
						Variant v = library->get_token_value(idx);
						if (v.get_type() != Variant::NIL) {
							lab->set_text(v.stringify().left(40));
						} else {
							lab->set_text("...");
						}
						lab->remove_theme_color_override(SNAME("font_color"));
					}
				}
			}
		}
	}
}

bool DesignTokenLibraryEditor::_is_any_color_picker_popup_visible() const {
	if (!list_vbox) {
		return false;
	}
	for (int i = 0; i < list_vbox->get_child_count(); i++) {
		EditorInspectorSection *section = Object::cast_to<EditorInspectorSection>(list_vbox->get_child(i));
		if (!section) {
			continue;
		}
		VBoxContainer *rows = section->get_vbox();
		if (!rows) {
			continue;
		}
		for (int r = 0; r < rows->get_child_count(); r++) {
			HBoxContainer *row = Object::cast_to<HBoxContainer>(rows->get_child(r));
			if (!row) {
				continue;
			}
			// Collect EditorProperties from row and its value_box.
			Vector<EditorProperty *> eps;
			for (int c = 0; c < row->get_child_count(); c++) {
				Control *child = Object::cast_to<Control>(row->get_child(c));
				if (!child) {
					continue;
				}
				if (EditorProperty *ep = Object::cast_to<EditorProperty>(child)) {
					eps.push_back(ep);
				}
				if (HBoxContainer *vbox = Object::cast_to<HBoxContainer>(child)) {
					for (int k = 0; k < vbox->get_child_count(); k++) {
						if (EditorProperty *ep2 = Object::cast_to<EditorProperty>(vbox->get_child(k))) {
							eps.push_back(ep2);
						}
					}
				}
			}
			for (EditorProperty *ep : eps) {
				for (int k = 0; k < ep->get_child_count(); k++) {
					ColorPickerButton *cpb = Object::cast_to<ColorPickerButton>(ep->get_child(k));
					if (cpb && cpb->get_popup() && cpb->get_popup()->is_visible()) {
						return true;
					}
					Control *ctrl = Object::cast_to<Control>(ep->get_child(k));
					if (ctrl) {
						for (int d = 0; d < ctrl->get_child_count(); d++) {
							ColorPickerButton *inner = Object::cast_to<ColorPickerButton>(ctrl->get_child(d));
							if (inner && inner->get_popup() && inner->get_popup()->is_visible()) {
								return true;
							}
						}
					}
				}
			}
		}
	}
	return false;
}

bool DesignTokenLibraryEditor::_is_any_color_picker_dirty() const {
	if (library.is_null() || !list_vbox) {
		return false;
	}
	// Check if any ColorPickerButton's displayed colour differs from the
	// library's stored value – indicates an intermediate live preview
	// (e.g. ColorPicker popup) or a pending `set(last_color)` revert.
	for (int i = 0; i < list_vbox->get_child_count(); i++) {
		EditorInspectorSection *section = Object::cast_to<EditorInspectorSection>(list_vbox->get_child(i));
		if (!section) {
			continue;
		}
		VBoxContainer *rows = section->get_vbox();
		if (!rows) {
			continue;
		}
		for (int r = 0; r < rows->get_child_count(); r++) {
			HBoxContainer *row = Object::cast_to<HBoxContainer>(rows->get_child(r));
			if (!row) {
				continue;
			}
			// Find the token index for this row via the name label.
			Label *name_label = nullptr;
			Vector<ColorPickerButton *> cpbs;
			for (int c = 0; c < row->get_child_count(); c++) {
				Control *child = Object::cast_to<Control>(row->get_child(c));
				if (!child) {
					continue;
				}
				HBoxContainer *name_box = Object::cast_to<HBoxContainer>(child);
				if (name_box && name_box->get_child_count() > 0) {
					Label *lbl = Object::cast_to<Label>(name_box->get_child(0));
					if (lbl) {
						name_label = lbl;
					}
				}
				Vector<EditorProperty *> eps;
				if (EditorProperty *ep = Object::cast_to<EditorProperty>(child)) {
					eps.push_back(ep);
				}
				if (HBoxContainer *vbox = Object::cast_to<HBoxContainer>(child)) {
					for (int k = 0; k < vbox->get_child_count(); k++) {
						if (EditorProperty *ep2 = Object::cast_to<EditorProperty>(vbox->get_child(k))) {
							eps.push_back(ep2);
						}
					}
				}
				for (EditorProperty *ep : eps) {
					for (int k = 0; k < ep->get_child_count(); k++) {
						ColorPickerButton *btn = Object::cast_to<ColorPickerButton>(ep->get_child(k));
						if (btn) {
							cpbs.push_back(btn);
							continue;
						}
						Control *ctrl = Object::cast_to<Control>(ep->get_child(k));
						if (ctrl) {
							for (int d = 0; d < ctrl->get_child_count(); d++) {
								ColorPickerButton *inner = Object::cast_to<ColorPickerButton>(ctrl->get_child(d));
								if (inner) {
									cpbs.push_back(inner);
								}
							}
						}
					}
				}
			}
			if (!name_label || cpbs.is_empty()) {
				continue;
			}
			for (ColorPickerButton *cpb : cpbs) {
				if (name_label && cpb) {
					String token_name = name_label->get_text();
					for (int idx = 0; idx < library->get_token_count(); idx++) {
						if (library->get_token_name(idx) == token_name) {
							Variant lib_val = library->get_token_value(idx);
							if (lib_val.get_type() == Variant::COLOR) {
								Color picker_col = cpb->get_pick_color();
								Color lib_col = lib_val;
								if (!picker_col.is_equal_approx(lib_col)) {
									return true;
								}
							}
							break;
						}
					}
				}
			}
		}
	}
	return false;
}

void DesignTokenLibraryEditor::_on_formula_check_toggled(bool p_pressed) {
	if (type_selector) {
		type_selector->set_visible(!p_pressed);
	}
	if (formula_edit) {
		formula_edit->set_visible(p_pressed);
	}
	if (p_pressed) {
		formula_edit->grab_focus();
	}
}

void DesignTokenLibraryEditor::_on_formula_submitted(const String &p_text, int p_idx) {
	if (library.is_null()) {
		return;
	}
	ERR_FAIL_INDEX(p_idx, library->get_token_count());
	String old = library->get_token_formula(p_idx);
	if (old == p_text) {
		return;
	}
	EditorUndoRedoManager *ur = EditorUndoRedoManager::get_singleton();
	ur->create_action(TTR("Set Design Token Formula"));
	ur->add_do_method(*library, "set_token_formula", p_idx, p_text);
	ur->add_undo_method(*library, "set_token_formula", p_idx, old);
	ur->commit_action();
	// Deferred rebuild to avoid freeing LineEdit mid-signal.
	callable_mp(this, &DesignTokenLibraryEditor::_rebuild).call_deferred();
}

void DesignTokenLibraryEditor::_on_formula_toggle_pressed(int p_idx) {
	if (library.is_null()) {
		return;
	}
	ERR_FAIL_INDEX(p_idx, library->get_token_count());
	bool is_formula = library->is_token_formula(p_idx);
	EditorUndoRedoManager *ur = EditorUndoRedoManager::get_singleton();
	ur->create_action(TTR("Toggle Design Token Formula"));
	ur->add_do_method(*library, "set_token_is_formula", p_idx, !is_formula);
	ur->add_undo_method(*library, "set_token_is_formula", p_idx, is_formula);
	ur->commit_action();
}

void DesignTokenLibraryEditor::_rebuild() {
	for (int i = list_vbox->get_child_count() - 1; i >= 0; i--) {
		Node *c = list_vbox->get_child(i);
		list_vbox->remove_child(c);
		memdelete(c);
	}
	pencil_buttons.clear();
	confirm_buttons.clear();
	cancel_buttons.clear();
	delete_buttons.clear();
	fx_buttons.clear();

	if (library.is_null() || library->get_token_count() == 0) {
		empty_hint->show();
		last_structural_version = library.is_valid() ? library->get_structural_version() : 0;
		return;
	}
	empty_hint->hide();

	HashMap<int, Vector<int>> by_type;
	for (int i = 0; i < library->get_token_count(); i++) {
		// For formula with auto type, group by inferred type.
		int t = library->get_token_type(i);
		by_type[t].push_back(i);
	}

	// Categories sorted alphabetically by their display name.
	Vector<int> types_sorted;
	for (const KeyValue<int, Vector<int>> &E : by_type) {
		types_sorted.push_back(E.key);
	}
	types_sorted.sort_custom<TokenTypeAlphCompare>();

	for (int t : types_sorted) {
		Variant::Type vtype = (Variant::Type)t;
		String type_name = Variant::get_type_name(vtype);

		// Use the inspector's own collapsible section header. Fold state is kept
		// on the library object, so it survives rebuilds. On first display the
		// category defaults to expanded (without marking the resource edited).
		if (!library->editor_is_section_unfolded(type_name)) {
			library->editor_set_section_unfold(type_name, true, true);
		}
		EditorInspectorSection *section = memnew(EditorInspectorSection);
		section->setup(type_name, type_name, library.ptr(), Color(), true);
		section->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		list_vbox->add_child(section);

		VBoxContainer *rows = section->get_vbox();
		rows->set_h_size_flags(Control::SIZE_EXPAND_FILL);

		// Token names sorted alphabetically within each category.
		Vector<int> indexes = by_type[t];
		indexes.sort_custom<TokenNameAlphCompare>(library);

		for (int idx : indexes) {
			HBoxContainer *row = memnew(HBoxContainer);
			row->set_h_size_flags(Control::SIZE_EXPAND_FILL);
			row->add_theme_constant_override("separation", 4);
			rows->add_child(row);

			// Name cell: label + pencil, or line edit + confirm/cancel while
			// renaming (same pattern as the theme editor). Child layout:
			// 0 Label, 1 LineEdit, 2 pencil, 3 confirm, 4 cancel.
			HBoxContainer *name_box = memnew(HBoxContainer);
			name_box->add_theme_constant_override("separation", 2);
			name_box->set_h_size_flags(Control::SIZE_EXPAND_FILL);
			name_box->set_stretch_ratio(1.0);
			name_box->set_custom_minimum_size(Size2(140 * EDSCALE, 0));
			row->add_child(name_box);

			Label *name_label = memnew(Label);
			name_label->set_text(library->get_token_name(idx));
			name_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
			name_label->set_clip_text(true);
			name_box->add_child(name_label);

			LineEdit *row_name_edit = memnew(LineEdit);
			row_name_edit->set_h_size_flags(Control::SIZE_EXPAND_FILL);
			name_box->add_child(row_name_edit);
			row_name_edit->hide();
			row_name_edit->connect(SceneStringName(text_submitted), callable_mp(this, &DesignTokenLibraryEditor::_on_confirm_name_submitted).bind(idx, name_box));

			Button *pencil = memnew(Button);
			pencil->set_flat(true);
			pencil->set_tooltip_text(TTR("Rename token"));
			pencil->set_custom_minimum_size(Size2(20, 20));
			if (is_inside_tree()) {
				pencil->set_button_icon(get_editor_theme_icon(SNAME("Edit")));
			}
			name_box->add_child(pencil);
			pencil_buttons.push_back(pencil);
			pencil->connect(SceneStringName(pressed), callable_mp(this, &DesignTokenLibraryEditor::_on_edit_name_pressed).bind(idx, name_box));

			Button *confirm = memnew(Button);
			confirm->set_flat(true);
			confirm->set_tooltip_text(TTR("Confirm token rename"));
			confirm->set_custom_minimum_size(Size2(20, 20));
			if (is_inside_tree()) {
				confirm->set_button_icon(get_editor_theme_icon(SNAME("ImportCheck")));
			}
			name_box->add_child(confirm);
			confirm_buttons.push_back(confirm);
			confirm->hide();
			confirm->connect(SceneStringName(pressed), callable_mp(this, &DesignTokenLibraryEditor::_on_confirm_name).bind(idx, name_box));

			Button *cancel = memnew(Button);
			cancel->set_flat(true);
			cancel->set_tooltip_text(TTR("Cancel token rename"));
			cancel->set_custom_minimum_size(Size2(20, 20));
			if (is_inside_tree()) {
				cancel->set_button_icon(get_editor_theme_icon(SNAME("ImportFail")));
			}
			name_box->add_child(cancel);
			cancel_buttons.push_back(cancel);
			cancel->hide();
			cancel->connect(SceneStringName(pressed), callable_mp(this, &DesignTokenLibraryEditor::_on_cancel_name).bind(idx, name_box));

			bool is_formula = library->is_token_formula(idx);
			// Formula toggle button (fx) – highlighted blue when enabled.
			Button *fx = memnew(Button);
			fx->set_flat(true);
			fx->set_toggle_mode(true);
			fx->set_pressed(is_formula);
			fx->set_tooltip_text(TTR("Toggle formula mode"));
			fx->set_text("fx");
			fx->set_custom_minimum_size(Size2(28, 20));
			if (is_inside_tree() && is_formula) {
				Color accent = get_theme_color(SNAME("accent_color"), EditorStringName(Editor));
				fx->add_theme_color_override(SNAME("font_color"), accent);
				fx->add_theme_color_override(SNAME("font_pressed_color"), accent);
				fx->add_theme_color_override(SNAME("font_hover_color"), accent);
				fx->add_theme_color_override(SNAME("font_focus_color"), accent);
			}
			fx->connect(SNAME("pressed"), callable_mp(this, &DesignTokenLibraryEditor::_on_formula_toggle_pressed).bind(idx));
			row->add_child(fx);
			fx_buttons.push_back(fx);

			// Single expanding value container ensures no empty gap when fx is off
			// and that pencil/fx stay column-aligned (previously formula had 2 children
			// vs 1 for value, causing different HBox distribution).
			HBoxContainer *value_box = memnew(HBoxContainer);
			value_box->set_h_size_flags(Control::SIZE_EXPAND_FILL);
			value_box->add_theme_constant_override("separation", 4);
			row->add_child(value_box);

			if (is_formula) {
				LineEdit *fedit = memnew(LineEdit);
				fedit->set_text(library->get_token_formula(idx));
				fedit->set_placeholder(TTR("Formula e.g. a * b + c"));
				fedit->set_h_size_flags(Control::SIZE_EXPAND_FILL);
				fedit->connect(SceneStringName(text_submitted), callable_mp(this, &DesignTokenLibraryEditor::_on_formula_submitted).bind(idx));
				value_box->add_child(fedit);
				// Preview / error – occupies the value slot.
				String err = library->get_token_error(idx);
				if (!err.is_empty()) {
					Label *err_label = memnew(Label);
					err_label->set_text(err);
					err_label->add_theme_color_override(SNAME("font_color"), Color(1, 0.3, 0.3));
					err_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
					err_label->set_clip_text(true);
					value_box->add_child(err_label);
				} else {
					Variant v = library->get_token_value(idx);
					Label *preview = memnew(Label);
					if (v.get_type() != Variant::NIL) {
						preview->set_text(v.stringify().left(40));
					} else {
						preview->set_text("...");
					}
					preview->set_modulate(Color(1, 1, 1, 0.6));
					preview->set_h_size_flags(Control::SIZE_EXPAND_FILL);
					preview->set_clip_text(true);
					value_box->add_child(preview);
				}
			} else {
				Variant::Type vt = (Variant::Type)library->get_token_type(idx);
				if (vt != Variant::NIL) {
					EditorProperty *vp = EditorInspectorDefaultPlugin::get_editor_for_property(
							library.ptr(), vt, "token_" + itos(idx) + "_value",
							PropertyHint::PROPERTY_HINT_NONE, "", PROPERTY_USAGE_EDITOR, false);
					if (vp) {
						vp->set_object_and_property(library.ptr(), "token_" + itos(idx) + "_value");
						vp->set_label_reference(nullptr);
						vp->set_use_folding(false);
						vp->set_h_size_flags(Control::SIZE_EXPAND_FILL);
						vp->connect(SNAME("property_changed"), callable_mp(this, &DesignTokenLibraryEditor::_on_value_changed), CONNECT_DEFERRED);
						value_box->add_child(vp);
						vp->update_property();
					}
				} else {
					Label *ph = memnew(Label);
					ph->set_text("...");
					ph->set_h_size_flags(Control::SIZE_EXPAND_FILL);
					ph->set_clip_text(true);
					value_box->add_child(ph);
				}
			}

			Button *del = memnew(Button);
			del->set_flat(true);
			del->set_tooltip_text(TTR("Remove Token"));
			del->set_custom_minimum_size(Size2(20, 20));
			if (is_inside_tree()) {
				del->set_button_icon(get_editor_theme_icon(SNAME("Remove")));
			}
			row->add_child(del);
			delete_buttons.push_back(del);
			del->connect(SceneStringName(pressed), callable_mp(this, &DesignTokenLibraryEditor::_on_delete_pressed).bind(idx));
		}
	}

	// Keep the structural version in sync so the next "changed" emission
	// from a pure value edit (e.g. dragging a ColorPicker) does not trigger a
	// spurious rebuild. Rebuilding while the EditorProperty is still emitting
	// "property_changed" would free the emitting control and crash.
	last_structural_version = library->get_structural_version();
}

void DesignTokenLibraryEditor::_on_add_pressed() {
	if (library.is_null()) {
		return;
	}
	String name = name_edit->get_text().strip_edges();
	if (name.is_empty()) {
		return;
	}
	if (!DesignTokenLibrary::is_valid_token_name_static(name)) {
		return;
	}
	if (library->has_token(name)) {
		return;
	}

	bool is_formula = formula_check && formula_check->is_pressed();
	int idx = library->get_token_count();
	EditorUndoRedoManager *ur = EditorUndoRedoManager::get_singleton();

	if (is_formula) {
		String formula = formula_edit ? formula_edit->get_text().strip_edges() : String();
		ur->create_action(TTR("Add Design Token"));
		ur->add_do_method(*library, "insert_token", idx, name, Variant::NIL, Variant(), true, formula);
		ur->add_undo_method(*library, "remove_token", idx);
		ur->commit_action();
		if (formula_edit) {
			formula_edit->clear();
		}
	} else {
		Variant::Type t = (Variant::Type)type_selector->get_selected_id();
		Variant def;
		Callable::CallError ce;
		Variant::construct(t, def, nullptr, 0, ce);
		if (ce.error != Callable::CallError::CALL_OK) {
			def = Variant();
		}
		ur->create_action(TTR("Add Design Token"));
		ur->add_do_method(*library, "insert_token", idx, name, (int)t, def, false, String());
		ur->add_undo_method(*library, "remove_token", idx);
		ur->commit_action();
	}
	name_edit->clear();
}

void DesignTokenLibraryEditor::_on_name_submitted(const String &p_text) {
	_on_add_pressed();
}

void DesignTokenLibraryEditor::_on_type_selected(int p_index) {
	// The selected type is read when adding a token; nothing else to do here.
}

void DesignTokenLibraryEditor::_on_edit_name_pressed(int p_idx, HBoxContainer *p_name_box) {
	ERR_FAIL_NULL(p_name_box);
	if (library.is_null()) {
		return;
	}
	_set_name_edit_mode(p_name_box, true, library->get_token_name(p_idx));
	LineEdit *row_name_edit = Object::cast_to<LineEdit>(p_name_box->get_child(1));
	ERR_FAIL_NULL(row_name_edit);
	row_name_edit->grab_focus();
	row_name_edit->select_all();
}

void DesignTokenLibraryEditor::_on_confirm_name_submitted(const String &p_text, int p_idx, HBoxContainer *p_name_box) {
	_on_confirm_name(p_idx, p_name_box);
}

void DesignTokenLibraryEditor::_on_confirm_name(int p_idx, HBoxContainer *p_name_box) {
	ERR_FAIL_NULL(p_name_box);
	if (library.is_null()) {
		return;
	}

	// Like the theme editor: keep the old name when the new one is empty,
	// unchanged, or would collide with an existing token.
	LineEdit *row_name_edit = Object::cast_to<LineEdit>(p_name_box->get_child(1));
	ERR_FAIL_NULL(row_name_edit);

	const String new_name = row_name_edit->get_text().strip_edges();
	const String old_name = library->get_token_name(p_idx);
	if (new_name.is_empty() || new_name == old_name || library->has_token(new_name)) {
		_set_name_edit_mode(p_name_box, false, old_name);
		return;
	}
	if (!DesignTokenLibrary::is_valid_token_name_static(new_name)) {
		_set_name_edit_mode(p_name_box, false, old_name);
		return;
	}

	// Show the confirmed name in the label before updating the library: the
	// rename makes the plugin refresh the inspector, which rebuilds the tree
	// (destroying these widgets), so nothing below may touch them again.
	_set_name_edit_mode(p_name_box, false, new_name);
	EditorUndoRedoManager *ur = EditorUndoRedoManager::get_singleton();
	ur->create_action(TTR("Rename Design Token"));
	ur->add_do_method(*library, "set_token_name", p_idx, new_name);
	ur->add_undo_method(*library, "set_token_name", p_idx, old_name);
	ur->commit_action();

	// The inspector refresh rebuilds and re-sorts on rename; this is a fallback
	// when that refresh is skipped (e.g. inspector hidden/not visible). Deferred
	// so the emitting button is not freed while its "pressed" signal is being
	// delivered.
	callable_mp(this, &DesignTokenLibraryEditor::_rebuild).call_deferred();
}

void DesignTokenLibraryEditor::_on_cancel_name(int p_idx, HBoxContainer *p_name_box) {
	ERR_FAIL_NULL(p_name_box);
	if (library.is_null()) {
		return;
	}
	_set_name_edit_mode(p_name_box, false, library->get_token_name(p_idx));
}

void DesignTokenLibraryEditor::_on_delete_pressed(int p_row) {
	if (library.is_null()) {
		return;
	}
	ERR_FAIL_INDEX(p_row, library->get_token_count());
	String name = library->get_token_name(p_row);
	int type = library->get_token_type(p_row);
	Variant value = library->get_token_value(p_row);
	bool is_formula = library->is_token_formula(p_row);
	String formula = library->get_token_formula(p_row);

	EditorUndoRedoManager *ur = EditorUndoRedoManager::get_singleton();
	ur->create_action(TTR("Remove Design Token"));
	ur->add_do_method(*library, "remove_token", p_row);
	ur->add_undo_method(*library, "insert_token", p_row, name, type, value, is_formula, formula);
	ur->commit_action();
}

void DesignTokenLibraryEditor::_on_value_changed(const StringName &p_property, const Variant &p_value, const StringName &p_field, bool p_changing) {
	if (library.is_null()) {
		return;
	}
	String prop_str = p_property;
	int sep = prop_str.find("_", 6);
	int idx = -1;
	if (sep != -1) {
		idx = prop_str.substr(6, sep - 6).to_int();
	}
	if (idx < 0 || idx >= library->get_token_count()) {
		// Fallback to direct set if parsing fails.
		library->set(p_property, p_value);
		return;
	}
	if (p_changing) {
		// Continuous drag (ColorPicker). Store the original value at drag
		// start and update the library live without creating an undo step
		// for each intermediate tick. The final `p_changing==false` will
		// create a single undo that reverts to the original.
		if (!drag_start_values.has(idx)) {
			drag_start_values[idx] = library->get_token_value(idx);
		}
		if (library->get_token_value(idx) == p_value) {
			return;
		}
		library->set_token_value(idx, p_value);
		return;
	}
	Variant old_value;
	if (drag_start_values.has(idx)) {
		old_value = drag_start_values[idx];
		drag_start_values.erase(idx);
		if (old_value == p_value) {
			return;
		}
		// Library already holds the last drag value (== p_value in most
		// cases) from the intermediate direct sets, so the `do` is a no-op
		// but we still create the undo to allow revert to the drag start.
	} else {
		old_value = library->get_token_value(idx);
		if (old_value == p_value) {
			return;
		}
	}
	EditorUndoRedoManager *ur = EditorUndoRedoManager::get_singleton();
	ur->create_action(TTR("Set Design Token Value"), UndoRedo::MERGE_ENDS);
	ur->add_do_method(*library, "set_token_value", idx, p_value);
	ur->add_undo_method(*library, "set_token_value", idx, old_value);
	ur->commit_action();
}
