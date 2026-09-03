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
#include "editor/docks/editor_dock.h"
#include "editor/inspector/editor_inspector.h"
#include "editor/plugins/editor_plugin.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/check_box.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/option_button.h"
#include "scene/resources/design_token_library.h"

class HBoxContainer;
class ItemList;
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

// Custom inspector control shown when a DesignTokenLibrary resource is selected.
// It replaces the auto-generated token_N_* properties with a friendlier editor:
// a top bar to add a new token (type selector | name | Add Token), and the
// existing tokens grouped by type in collapsible inspector sections (sorted
// alphabetically), each row showing name | value | delete. Names are renamed
// with a pencil button like the theme editor (Edit -> confirm/cancel).
class DesignTokenLibraryEditor : public VBoxContainer {
	GDCLASS(DesignTokenLibraryEditor, VBoxContainer);

	Ref<DesignTokenLibrary> library;

	HBoxContainer *add_bar = nullptr;
	OptionButton *type_selector = nullptr;
	LineEdit *name_edit = nullptr;
	CheckBox *formula_check = nullptr;
	LineEdit *formula_edit = nullptr;
	Button *add_button = nullptr;

	VBoxContainer *list_vbox = nullptr;

	Label *empty_hint = nullptr;

	// Icon buttons (pencil / confirm / cancel / delete). Theme icons can't be
	// resolved until the control is inside the tree, so the buttons are
	// collected on rebuild and their icons assigned in NOTIFICATION_THEME_CHANGED.
	// Buttons created after the control is already inside the tree get their
	// icon assigned immediately in _rebuild as well so they never appear blank
	// after a structural refresh (e.g. add/remove or spurious value edit).
	Vector<Button *> pencil_buttons;
	Vector<Button *> confirm_buttons;
	Vector<Button *> cancel_buttons;
	Vector<Button *> delete_buttons;
	Vector<Button *> fx_buttons;

	// Structural version used to decide whether a rebuild is needed after the
	// library emits "changed". Value edits keep the same version, so the
	// editor (and any focused LineEdit) is preserved; only add/remove/type/
	// rename changes bump the version (DesignTokenLibrary::structural_version).
	uint64_t last_structural_version = 0;

	// Drag handling for colour and other continuous editors: store the value
	// at drag start so the final undo can revert to the original.
	HashMap<int, Variant> drag_start_values;

	void _rebuild();

	void _on_library_changed();
	void _refresh_value_editors();
	bool _is_any_color_picker_popup_visible() const;
	bool _is_any_color_picker_dirty() const;
	void _on_add_pressed();
	void _on_name_submitted(const String &p_text);
	void _on_type_selected(int p_index);
	void _on_edit_name_pressed(int p_idx, HBoxContainer *p_name_box);
	void _on_confirm_name_submitted(const String &p_text, int p_idx, HBoxContainer *p_name_box);
	void _on_confirm_name(int p_idx, HBoxContainer *p_name_box);
	void _on_cancel_name(int p_idx, HBoxContainer *p_name_box);
	void _on_delete_pressed(int p_row);
	void _on_value_changed(const StringName &p_property, const Variant &p_value, const StringName &p_field, bool p_changing);
	void _on_formula_check_toggled(bool p_pressed);
	void _on_formula_submitted(const String &p_text, int p_idx);
	void _on_formula_toggle_pressed(int p_idx);

	static Vector<Variant::Type> get_allowed_types();

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_library(const Ref<DesignTokenLibrary> &p_library);

	DesignTokenLibraryEditor();
	~DesignTokenLibraryEditor();
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
	uint64_t last_scan_structural_version = 0;
	bool scan_pending = false;

	void _refresh_inspector();
	void _scan_and_propagate_deferred();

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
	virtual void parse_begin(Object *p_object) override;
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

class DesignTokenEditor;

class DesignTokenEditorPlugin : public EditorPlugin {
	GDCLASS(DesignTokenEditorPlugin, EditorPlugin);

	Ref<DesignTokenInspectorPlugin> inspector_plugin;
	String loaded_library_path;

	DesignTokenEditor *design_token_editor = nullptr;

	void _on_project_settings_changed();

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	virtual String get_plugin_name() const override { return "DesignTokens"; }
	virtual void edit(Object *p_object) override;
	virtual bool handles(Object *p_object) const override;
	virtual void make_visible(bool p_visible) override;
	virtual bool can_auto_hide() const override;
	DesignTokenEditorPlugin();
	~DesignTokenEditorPlugin();
};

// Bottom dock for DesignTokenLibrary, mirroring ThemeEditor (EditorDock).
class DesignTokenEditor : public EditorDock {
	GDCLASS(DesignTokenEditor, EditorDock);

	friend class DesignTokenEditorPlugin;
	DesignTokenEditorPlugin *plugin = nullptr;

	Ref<DesignTokenLibrary> library;

	Label *library_name = nullptr;
	Button *edit_button = nullptr;
	Button *close_button = nullptr;

	DesignTokenLibraryEditor *library_editor = nullptr;

	void _update_library_name(const String &p_name = String());
	void _dock_closed_cbk();
	void _scene_closed(const String &p_path);
	void _resource_saved(const Ref<Resource> &p_resource);
	void _files_moved(const String &p_old_path, const String &p_new_path);
	void _save_button_cbk(bool p_save_as);
	void _edit_button_cbk();

protected:
	void _notification(int p_what);

public:
	void edit(const Ref<DesignTokenLibrary> &p_library);
	Ref<DesignTokenLibrary> get_edited_library() const { return library; }

	DesignTokenEditor();
};
