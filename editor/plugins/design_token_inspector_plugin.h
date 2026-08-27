/**************************************************************************/
/*  design_token_inspector_plugin.h                                       */
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

#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"
#include "editor/inspector/editor_inspector.h"
#include "editor/plugins/editor_plugin.h"
#include "scene/resources/design_token_library.h"

class Button;
class HBoxContainer;
class ItemList;
class LineEdit;
class PopupMenu;
class PopupPanel;
class DesignTokenInspectorPlugin;

// EditorProperty row that renders [chain button][default editor]. The chain
// button links the property to a design token; while linked the value becomes
// read-only (it may only be edited inside the design token library).
class DesignTokenPropertyEditor : public EditorProperty {
	GDCLASS(DesignTokenPropertyEditor, EditorProperty);

	Ref<DesignTokenInspectorPlugin> plugin;
	EditorProperty *sub_editor = nullptr;
	Button *chain_button = nullptr;

	Variant::Type prop_type = Variant::NIL;

	String linked_token;
	bool link_state_checked = false;

	// Read-only state as intended by the inspector; the row reports itself as
	// read-only whenever a token is linked so that the value can't be reverted.
	bool inspector_read_only = false;
	bool updating_read_only = false;

	void _on_chain_pressed();
	void _refresh_chain_state();
	void _sync_row_read_only();
	void _update_chain_visuals();
	void _object_id_selected(const StringName &p_property, ObjectID p_id);

protected:
	void _notification(int p_what);
	void _set_read_only(bool p_read_only) override;
	static void _bind_methods();

public:
	void update_property() override;

	String get_linked_token() const { return linked_token; }
	Button *get_chain_button() const { return chain_button; }
	Variant::Type get_property_type() const { return prop_type; }
	String get_property_path() const { return String(get_edited_property()); }

	DesignTokenPropertyEditor(const Ref<DesignTokenInspectorPlugin> &p_plugin, Object *p_object,
			const String &p_path, const Variant::Type p_type, EditorProperty *p_sub_editor);
};

// Inspector plugin: injects the chain button into editable properties and
// propagates token changes to every object that is linked to a token.
class DesignTokenInspectorPlugin : public EditorInspectorPlugin {
	GDCLASS(DesignTokenInspectorPlugin, EditorInspectorPlugin);

	friend class DesignTokenPropertyEditor;

	Ref<DesignTokenLibrary> library;

	PopupPanel *token_picker = nullptr;
	LineEdit *search_line_edit = nullptr;
	ItemList *token_item_list = nullptr;
	Label *picker_hint = nullptr;
	LineEdit *new_token_name_edit = nullptr;
	Button *create_token_button = nullptr;
	Button *open_in_library_button = nullptr;
	Button *unlink_button = nullptr;
	PopupMenu *linked_menu = nullptr;

	Object *pending_editor = nullptr;
	ObjectID pending_object_id;
	String pending_property;
	Variant::Type pending_type = Variant::NIL;

	HashMap<ObjectID, HashSet<StringName>> linked_properties;

	void _register_link(Object *p_object, const String &p_property);
	void _unregister_link(Object *p_object, const String &p_property);
	void _link_to(Object *p_object, const String &p_property, const String &p_token_name);
	void _unlink_property(Object *p_object, const String &p_property);
	void _refresh_inspector();

	// Chain button interactions.
	void _open_token_picker(DesignTokenPropertyEditor *p_editor);
	void _open_linked_menu(DesignTokenPropertyEditor *p_editor);
	void _on_linked_menu_id(int p_id);
	void _navigate_to_library();

	// Token picker popup.
	void _ensure_picker();
	void _refresh_token_picker();
	void _on_picker_search(const String &p_text);
	void _on_picker_item_activated(int p_index);
	void _on_picker_create_token();
	void _on_picker_open_in_library();
	void _on_picker_unlink();

	void _on_library_changed();
	void _on_token_renamed(const String &p_old_name, const String &p_new_name);

protected:
	static void _bind_methods();

public:
	virtual bool can_handle(Object *p_object) override;
	virtual bool parse_property(Object *p_object, const Variant::Type p_type,
			const String &p_path, const PropertyHint p_hint,
			const String &p_hint_text, const BitField<PropertyUsageFlags> p_usage,
			const bool p_wide = false) override;

	void set_library(const Ref<DesignTokenLibrary> &p_library);
	Ref<DesignTokenLibrary> get_library() const;
	String get_linked_token(Object *p_object, const String &p_property) const;

	DesignTokenInspectorPlugin();
	~DesignTokenInspectorPlugin();
};

class DesignTokenEditorPlugin : public EditorPlugin {
	GDCLASS(DesignTokenEditorPlugin, EditorPlugin);

	Ref<DesignTokenInspectorPlugin> inspector_plugin;
	String loaded_library_path;

	void _on_project_settings_changed();

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	virtual String get_plugin_name() const override { return "DesignTokens"; }
	DesignTokenEditorPlugin();
};