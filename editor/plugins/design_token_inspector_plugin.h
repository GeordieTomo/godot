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
#include "core/variant/callable.h"
#include "editor/inspector/editor_inspector.h"
#include "editor/plugins/editor_plugin.h"
#include "scene/resources/design_token_library.h"

class Button;
class HBoxContainer;
class ItemList;
class LineEdit;
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

	static DesignTokenInspectorPlugin *singleton;

	Ref<DesignTokenLibrary> library;

	// Callbacks registered by theme editors so their chain-button/read-only state
	// updates right away when a theme item link is created or removed. The theme
	// editor's own rebuild on the theme "changed" signal is focus-guarded and may
	// be skipped, so these provide a deterministic refresh.
	Vector<Callable> theme_refresh_callbacks;

	PopupPanel *token_picker = nullptr;
	LineEdit *search_line_edit = nullptr;
	ItemList *token_item_list = nullptr;
	Label *picker_hint = nullptr;
	LineEdit *new_token_name_edit = nullptr;
	Button *create_token_button = nullptr;
	Button *open_in_library_button = nullptr;
	Button *unlink_button = nullptr;

	Object *pending_object = nullptr;
	ObjectID pending_object_id;
	String pending_property;
	Variant::Type pending_type = Variant::NIL;
	Control *picker_anchor = nullptr;

	HashMap<ObjectID, HashSet<StringName>> linked_properties;
	// Objects already scanned for links in their metadata; avoids re-walking
	// every open scene on each token edit. New links always go through
	// link_property()/register_link(), so a scanned object never needs a rescan.
	HashSet<ObjectID> scanned_objects;

	void _refresh_inspector();

	// Token picker popup.
	void _ensure_picker();
	void _refresh_token_picker();
	void _on_picker_search(const String &p_text);
	void _on_picker_item_activated(int p_index);
	void _on_picker_create_token();
	void _on_picker_open_in_library();
	void _on_picker_unlink();
	void _navigate_to_library();

	void _on_library_changed();
	void _on_token_renamed(const String &p_old_name, const String &p_new_name);

	// Re-discovers links saved in object metadata after the editor was reopened,
	// so token changes propagate to linked nodes/resources without having to
	// select each one first.
	void _scan_edited_scenes_links();
	void _discover_object_links(Object *p_object);
	void _discover_node_links(Node *p_node);

	// Registers every link currently saved on a Theme resource (item links live
	// in a single Dictionary metadata entry) so its values follow the tokens.
	void _register_theme_links(const Theme *p_theme);

protected:
	static void _bind_methods();

public:
	virtual bool can_handle(Object *p_object) override;
	virtual bool parse_property(Object *p_object, const Variant::Type p_type,
			const String &p_path, const PropertyHint p_hint,
			const String &p_hint_text, const BitField<PropertyUsageFlags> p_usage,
			const bool p_wide = false) override;

	// Opens the token picker popup for an arbitrary object/property, anchored to
	// the control that triggered it (used by both the inspector chain button and
	// the theme editor rows).
	void open_picker(Object *p_object, const String &p_property, Variant::Type p_type, Control *p_anchor);
	void link_property(Object *p_object, const String &p_property, const String &p_token_name);
	void unlink_property(Object *p_object, const String &p_property);

	// Theme editors register a refresh callback (see theme_refresh_callbacks) so
	// their rows update deterministically when a theme link changes.
	void register_theme_refresh_callback(const Callable &p_callback);
	void unregister_theme_refresh_callback(const Callable &p_callback);
	void notify_theme_links_changed(const Theme *p_theme);

	// Registers a pre-existing link (found in the object's metadata) so value
	// changes on the token library are propagated to it.
	void register_link(Object *p_object, const String &p_property);
	void unregister_link(Object *p_object, const String &p_property);

	// Re-registers the links of a theme that's open in the theme editor dock so
	// token changes always reach it, even when it isn't attached to a scene.
	void register_theme_links(const Theme *p_theme);

	void set_library(const Ref<DesignTokenLibrary> &p_library);
	Ref<DesignTokenLibrary> get_library() const;
	String get_linked_token(Object *p_object, const String &p_property) const;

	static DesignTokenInspectorPlugin *get_singleton() { return singleton; }

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