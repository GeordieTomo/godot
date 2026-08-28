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
#include "editor/docks/inspector_dock.h"
#include "editor/editor_node.h"
#include "editor/editor_string_names.h"
#include "editor/inspector/editor_properties.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/control.h"
#include "scene/gui/item_list.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/popup.h"
#include "scene/main/node.h"
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
	sub_editor->connect(SNAME("property_changed"), callable_mp((EditorProperty *)this, &EditorProperty::emit_changed));
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

	if (Theme *theme = Object::cast_to<Theme>(p_object)) {
		Dictionary links = _get_theme_links(theme);
		links.erase(p_property);
		_set_theme_links(theme, links);
	} else {
		p_object->remove_meta("__design_token_" + p_property);
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

void DesignTokenInspectorPlugin::_refresh_inspector() {
	EditorInspector *inspector = InspectorDock::get_inspector_singleton();
	if (inspector) {
		inspector->update_tree();
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

bool DesignTokenInspectorPlugin::parse_property(Object *p_object, const Variant::Type p_type,
		const String &p_path, const PropertyHint p_hint,
		const String &p_hint_text, const BitField<PropertyUsageFlags> p_usage,
		const bool p_wide) {
	if (!p_object) {
		return false;
	}

	// Properties of the library itself must not be linkable.
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
	search_line_edit->set_placeholder(TTR("Search tokens…"));
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
	new_token_name_edit->set_placeholder(TTR("New token name…"));
	new_token_name_edit->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	new_token_name_edit->connect(SNAME("text_submitted"), callable_mp(this, &DesignTokenInspectorPlugin::_on_picker_create_token));
	create_row->add_child(new_token_name_edit);
	create_token_button = memnew(Button);
	create_token_button->set_text(TTR("Create && Link"));
	create_token_button->connect(SNAME("pressed"), callable_mp(this, &DesignTokenInspectorPlugin::_on_picker_create_token));
	create_row->add_child(create_token_button);
	vbox->add_child(create_row);

	// Open the library / unlink the current link.
	HBoxContainer *action_row = memnew(HBoxContainer);
	action_row->add_theme_constant_override(SNAME("separation"), 4);
	open_in_library_button = memnew(Button);
	open_in_library_button->set_text(TTR("Open in Library…"));
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
	link_property(obj, pending_property, token_name);

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

	if (library->has_token(name)) {
		picker_hint->set_text(vformat(TTR("A token named \"%s\" already exists."), name));
		return;
	}

	int index = library->get_token_count();
	library->set_token_count(index + 1);
	library->set_token_type(index, (int)pending_type);
	library->set_token_value(index, obj->get(pending_property));
	library->set_token_name(index, name);

	link_property(obj, pending_property, name);

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
	if (obj) {
		unlink_property(obj, pending_property);
	}
	token_picker->hide();
	pending_object_id = ObjectID();
	pending_property = String();
	picker_anchor = nullptr;
}

void DesignTokenInspectorPlugin::_on_library_changed() {
	if (library.is_null()) {
		return;
	}

	// Find links saved in object metadata from a previous editor session. The
	// inspector only registers links for objects that have been displayed since
	// startup, so without this token changes would be silently dropped for
	// linked nodes/resources that weren't selected yet.
	_scan_edited_scenes_links();

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
	EditorInspector *inspector = InspectorDock::get_inspector_singleton();
	if (inspector) {
		Object *inspected = inspector->get_edited_object();
		if (inspected && linked_properties.has(inspected->get_instance_id())) {
			inspector->update_tree();
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
}