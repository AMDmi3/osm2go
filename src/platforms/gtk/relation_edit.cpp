/*
 * Copyright (C) 2008 Till Harbaum <till@harbaum.org>.
 *
 * This file is part of OSM2Go.
 *
 * OSM2Go is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * OSM2Go is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with OSM2Go.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "relation_edit.h"

#include <info.h>
#include <josm_presets.h>
#include "list.h"
#include <map.h>

#include <algorithm>
#include <cassert>
#include <cstring>
#include <set>
#include <string>
#include <strings.h>

#include <osm2go_annotations.h>
#include <osm2go_cpp.h>
#include "osm2go_i18n.h"
#include "osm2go_platform.h"
#include "osm2go_platform_gtk.h"

/* --------------- relation dialog for an item (node or way) ----------- */

namespace {

struct relitem_context_t {
  relitem_context_t(object_t &o, const presets_items *pr, osm_t::ref os);
  relitem_context_t() O2G_DELETED_FUNCTION;
  relitem_context_t(const relitem_context_t &) O2G_DELETED_FUNCTION;
  relitem_context_t &operator=(const relitem_context_t &) O2G_DELETED_FUNCTION;
#if __cplusplus >= 201103L
  relitem_context_t(relitem_context_t &&) = delete;
  relitem_context_t &operator=(relitem_context_t &&) = delete;
  ~relitem_context_t() = default;
#endif

  object_t &item;
  const presets_items * const presets;
  osm_t::ref osm;
  osm2go_platform::DialogGuard dialog;
  std::unique_ptr<GtkListStore, g_object_deleter> store;
  GtkTreeSelection * selection;
};

enum {
  RELITEM_COL_TYPE = 0,
  RELITEM_COL_ROLE,
  RELITEM_COL_NAME,
  RELITEM_COL_DATA,
  RELITEM_NUM_COLS
};

relitem_context_t::relitem_context_t(object_t &o, const presets_items *pr, osm_t::ref os)
  : item(o)
  , presets(pr)
  , osm(os)
  , store(nullptr)
  , selection(nullptr)
{
}

struct entry_insert_text {
  GtkWidget * const entry;
  explicit entry_insert_text(GtkWidget *en) : entry(en) {}
  inline void operator()(const std::string &role) {
    osm2go_platform::combo_box_append_text(entry, role.c_str());
  }
};

struct relation_context_t {
  inline relation_context_t(map_t *m, osm_t::ref o, presets_items *p, GtkWidget *d)
    : map(m), osm(o), presets(p), dialog(d), list(nullptr) {}
  relation_context_t() O2G_DELETED_FUNCTION;
  relation_context_t(const relation_context_t &) O2G_DELETED_FUNCTION;
  relation_context_t &operator=(const relation_context_t &) O2G_DELETED_FUNCTION;
#if __cplusplus >= 201103L
  relation_context_t(relation_context_t &&) = delete;
  relation_context_t &operator=(relation_context_t &&) = delete;
  ~relation_context_t() = default;
#endif

  map_t * const map;
  osm_t::ref osm;
  presets_items * const presets;
  osm2go_platform::DialogGuard dialog;
  GtkWidget *list;
  std::unique_ptr<GtkListStore, g_object_deleter> store;
};

bool
relation_add_item(GtkWidget *parent, relation_t *relation, const object_t &object, const presets_items *presets)
{
  g_debug("add object of type %d to relation #" ITEM_ID_FORMAT, object.type, relation->id);

  const std::set<std::string> &roles = presets->roles(relation, object);

  /* ask the user for the role of the new object in this relation */
  /* ------------------ role dialog ---------------- */
  osm2go_platform::DialogGuard dialog(gtk_dialog_new_with_buttons(static_cast<const gchar *>(_("Select role")), GTK_WINDOW(parent),
                                              GTK_DIALOG_MODAL,
                                              GTK_STOCK_CANCEL, GTK_RESPONSE_REJECT,
                                              GTK_STOCK_OK, GTK_RESPONSE_ACCEPT,
                                              nullptr));

  gtk_dialog_set_default_response(dialog, GTK_RESPONSE_ACCEPT);

  const char *type = relation->tags.get_value("type");

  { // scope to free info_str earlier
    const trstring info_str = type != nullptr ?
                      trstring("In relation of type: %1").arg(type) :
                      trstring("In relation #%1").arg(relation->id);
    gtk_box_pack_start(dialog.vbox(), gtk_label_new(static_cast<const gchar *>(info_str)),
                       TRUE, TRUE, 0);
  }

  const char *name = relation->tags.get_value("name");
  if(name != nullptr)
    gtk_box_pack_start(dialog.vbox(), gtk_label_new(name), TRUE, TRUE, 0);

  GtkWidget *hbox = gtk_hbox_new(FALSE, 8);

#ifdef FREMANTLE
  if(roles.empty())
#endif
    gtk_box_pack_start(GTK_BOX(hbox), gtk_label_new(_("Role:")), TRUE, TRUE, 0);

  GtkWidget *entry = nullptr;
  if(!roles.empty()) {
    entry = osm2go_platform::combo_box_entry_new(static_cast<const char *>(_("Role")));

    /* fill combo box with presets */
    std::for_each(roles.begin(), roles.end(), entry_insert_text(entry));
  } else
    entry = osm2go_platform::entry_new();

  gtk_box_pack_start(GTK_BOX(hbox), entry, TRUE, TRUE, 0);
  gtk_box_pack_start(dialog.vbox(), hbox, TRUE, TRUE, 0);

  gtk_widget_show_all(dialog.get());
  if(GTK_RESPONSE_ACCEPT != gtk_dialog_run(dialog)) {
    g_debug("user clicked cancel");
    return false;
  }

  g_debug("user clicked ok");

  /* get role from dialog */
  const char *role = nullptr;
  std::string rstr;

  if(osm2go_platform::isComboBoxEntryWidget(entry)) {
    rstr = osm2go_platform::combo_box_get_active_text(entry);
    if(!rstr.empty())
      role = rstr.c_str();
  } else {
    const gchar *ptr = gtk_entry_get_text(GTK_ENTRY(entry));
    if(ptr != nullptr && *ptr != '\0')
      role = ptr;
  }

  // create new member
  // must be done before the widget is destroyed as it may reference the
  // internal string from the text entry
  relation->members.push_back(member_t(object, role));

  assert(object.is_real());

  relation->flags |= OSM_FLAG_DIRTY;
  return true;
}

bool
relation_info_dialog(relation_context_t *context, relation_t *relation)
{
  object_t object(relation);
  return info_dialog(context->dialog.get(), context->map, context->osm, context->presets, object);
}

gboolean
changed_foreach(GtkTreeModel *model, GtkTreePath *, GtkTreeIter *iter, gpointer data)
{
  relitem_context_t *context = static_cast<relitem_context_t *>(data);
  relation_t *relation = nullptr;
  gtk_tree_model_get(model, iter, RELITEM_COL_DATA, &relation, -1);
  assert(relation != nullptr);

  const std::vector<member_t>::iterator itEnd = relation->members.end();
  const std::vector<member_t>::iterator it = relation->find_member_object(context->item);

  gboolean isSelected = gtk_tree_selection_iter_is_selected(context->selection, iter);

  if(it == itEnd && isSelected == TRUE) {
    g_debug("selected: " ITEM_ID_FORMAT, relation->id);

    /* either accept this or unselect again */
    if(relation_add_item(context->dialog.get(), relation, context->item, context->presets)) {
      // the item is now the last one in the chain
      const member_t &member = relation->members.back();
      gtk_list_store_set(GTK_LIST_STORE(model), iter, RELITEM_COL_ROLE, member.role, -1);
    } else
      gtk_tree_selection_unselect_iter(context->selection, iter);

    return TRUE;
  } else if(it != itEnd && isSelected == FALSE) {
    g_debug("deselected: " ITEM_ID_FORMAT, relation->id);

    relation->remove_member(it);
    gtk_list_store_set(GTK_LIST_STORE(model), iter, RELITEM_COL_ROLE, nullptr, -1);

    return TRUE;
  }

  return FALSE;
}

void
changed(relitem_context_t *context)
{
  g_debug("relation-edit changed event");

  gtk_tree_model_foreach(GTK_TREE_MODEL(context->store.get()), changed_foreach, context);
}

#ifndef FREMANTLE
/* we handle these events on our own in order to implement */
/* a very direct selection mechanism (multiple selections usually */
/* require the control key to be pressed). This interferes with */
/* fremantle finger scrolling, but fortunately the fremantle */
/* default behaviour already is what we want. */
gboolean
on_view_clicked(GtkWidget *widget, GdkEventButton *event, gpointer)
{
  if(event->window == gtk_tree_view_get_bin_window(GTK_TREE_VIEW(widget))) {
    GtkTreePath *path;

    if(gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget), event->x, event->y, &path,
                                     nullptr, nullptr, nullptr) == TRUE) {
      GtkTreeSelection *sel =
	gtk_tree_view_get_selection(GTK_TREE_VIEW(widget));

      if(gtk_tree_selection_path_is_selected(sel, path) != TRUE)
	gtk_tree_selection_select_path(sel, path);
      else
	gtk_tree_selection_unselect_path(sel, path);
    }
    return TRUE;
  }
  return FALSE;
}
#endif

struct relation_list_insert_functor {
  relitem_context_t &context;
  GtkTreeIter &sel_iter;
  std::string &selname; /* name of sel_iter */
  inline relation_list_insert_functor(relitem_context_t &c, std::string &sn, GtkTreeIter &it)
    : context(c), sel_iter(it), selname(sn) {}
  void operator()(std::pair<item_id_t, relation_t *> pair);
};

void relation_list_insert_functor::operator()(std::pair<item_id_t, relation_t *> pair)
{
  const relation_t * const relation = pair.second;

  if(relation->isDeleted())
    return;

  GtkTreeIter iter;
  /* try to find something descriptive */
  std::string name = relation->descriptive_name();

  const std::vector<member_t>::const_iterator it = relation->find_member_object(context.item);
  const bool isMember = it != relation->members.end();

  /* Append a row and fill in some data */
  gtk_list_store_insert_with_values(context.store.get(), &iter, -1,
                                    RELITEM_COL_TYPE, relation->tags.get_value("type"),
                                    RELITEM_COL_ROLE, isMember ? it->role : nullptr,
                                    RELITEM_COL_NAME, name.c_str(),
                                    RELITEM_COL_DATA, relation,
                                    -1);

  /* select all relations the current object is part of */

  if(isMember) {
    gtk_tree_selection_select_iter(context.selection, &iter);
    /* check if this element is earlier by name in the list */
    if(selname.empty() || name.compare(selname) < 0) {
      selname.swap(name);
      sel_iter = iter;
    }
  }
}

GtkWidget *
relation_item_list_widget(relitem_context_t &context)
{
  GtkTreeView *view = osm2go_platform::tree_view_new();

#ifdef FREMANTLE
  /* hildon hides these by default */
  gtk_tree_view_set_headers_visible(view, TRUE);
#endif

  /* change list mode to "multiple" */
  context.selection = gtk_tree_view_get_selection(view);
  gtk_tree_selection_set_mode(context.selection, GTK_SELECTION_MULTIPLE);

#ifndef FREMANTLE
  /* catch views button-press event for our custom handling */
  g_signal_connect(view, "button-press-event",
		   G_CALLBACK(on_view_clicked), &context);
#endif

  /* --- "Name" column --- */
  GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
  g_object_set(renderer, "ellipsize", PANGO_ELLIPSIZE_END, nullptr );
  GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes(
       static_cast<const gchar *>(_("Name")), renderer, "text", RELITEM_COL_NAME, nullptr);
  gtk_tree_view_column_set_expand(column, TRUE);
  gtk_tree_view_insert_column(view, column, -1);

  /* --- "Type" column --- */
  renderer = gtk_cell_renderer_text_new();
  gtk_tree_view_insert_column_with_attributes(view,
      -1, static_cast<const gchar *>(_("Type")), renderer, "text", RELITEM_COL_TYPE, nullptr);

  /* --- "Role" column --- */
  renderer = gtk_cell_renderer_text_new();
  gtk_tree_view_insert_column_with_attributes(view,
      -1, static_cast<const gchar *>(_("Role")), renderer, "text", RELITEM_COL_ROLE, nullptr);

  /* build and fill the store */
  context.store.reset(gtk_list_store_new(RELITEM_NUM_COLS, G_TYPE_STRING, G_TYPE_STRING,
                                         G_TYPE_STRING, G_TYPE_POINTER));

  gtk_tree_view_set_model(view, GTK_TREE_MODEL(context.store.get()));

  // Debatable whether to sort by the "selected" or the "Name" column by
  // default. Both are be useful, in different ways.
  gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(context.store.get()),
                                       RELITEM_COL_NAME, GTK_SORT_ASCENDING);

  /* build a list of iters of all items that should be selected */
  std::string selname;
  GtkTreeIter sel_iter;
  relation_list_insert_functor inserter(context, selname, sel_iter);

  std::for_each(context.osm->relations.begin(),
                context.osm->relations.end(), inserter);

  if(!selname.empty())
    list_view_scroll(view, context.selection, &sel_iter);

  g_signal_connect_swapped(context.selection, "changed", G_CALLBACK(changed), &context);

  return osm2go_platform::scrollable_container(GTK_WIDGET(view));
}

}

void relation_membership_dialog(GtkWidget *parent, const presets_items *presets,
                                osm_t::ref osm, object_t &object) {
  relitem_context_t context(object, presets, osm);

  { // scope to free str earlier
    const trstring str = trstring("Relation memberships of %1 #%2").arg(object.type_string()).arg(object.get_id());

    context.dialog.reset(gtk_dialog_new_with_buttons(static_cast<const gchar *>(str),
                                                     GTK_WINDOW(parent), GTK_DIALOG_MODAL,
                                                     GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
                                                     nullptr));
  }

  osm2go_platform::dialog_size_hint(context.dialog, osm2go_platform::MISC_DIALOG_LARGE);
  gtk_dialog_set_default_response(context.dialog, GTK_RESPONSE_CLOSE);

  gtk_box_pack_start(context.dialog.vbox(), relation_item_list_widget(context), TRUE, TRUE, 0);

  /* ----------------------------------- */

  gtk_widget_show_all(context.dialog.get());
  gtk_dialog_run(context.dialog);
}

/* -------------------- global relation list ----------------- */

namespace {

enum {
  RELATION_COL_TYPE = 0,
  RELATION_COL_NAME,
  RELATION_COL_MEMBERS,
  RELATION_COL_DATA,
  RELATION_NUM_COLS
};

relation_t *
get_selected_relation(relation_context_t *context)
{
  GtkTreeSelection *selection;
  GtkTreeModel     *model;
  GtkTreeIter       iter;

  selection = list_get_selection(context->list);
  if(gtk_tree_selection_get_selected(selection, &model, &iter) == TRUE) {
    relation_t *relation;
    gtk_tree_model_get(model, &iter, RELATION_COL_DATA, &relation, -1);
    return(relation);
  }
  return nullptr;
}

void
relation_list_selected(GtkWidget *list, relation_t *selected)
{
  list_button_enable(list, LIST_BUTTON_USER0,
		     (selected != nullptr) && (!selected->members.empty()));
  list_button_enable(list, LIST_BUTTON_USER1,
		     (selected != nullptr) && (!selected->members.empty()));

  list_button_enable(list, LIST_BUTTON_REMOVE, selected != nullptr);
  list_button_enable(list, LIST_BUTTON_EDIT, selected != nullptr);
}

void
relation_list_changed(GtkTreeSelection *selection, gpointer userdata)
{
  GtkWidget *list = static_cast<relation_context_t *>(userdata)->list;
  GtkTreeModel *model = nullptr;
  GtkTreeIter iter;

  if(gtk_tree_selection_get_selected(selection, &model, &iter) == TRUE) {
    relation_t *relation = nullptr;
    gtk_tree_model_get(model, &iter, RELATION_COL_DATA, &relation, -1);
    relation_list_selected(list, relation);
  }
}

struct member_context_t {
  inline member_context_t(const relation_t *r, osm_t::ref o, GtkWidget *parent)
    : relation(r)
    , dialog(gtk_dialog_new_with_buttons(static_cast<const gchar *>(trstring("Members of relation \"%1\"").arg(relation->descriptive_name())),
                                         GTK_WINDOW(parent), GTK_DIALOG_MODAL,
                                         GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
                                         nullptr))
    , osm(o)
  {
  }

  member_context_t() O2G_DELETED_FUNCTION;
  member_context_t(const member_context_t &) O2G_DELETED_FUNCTION;
  member_context_t &operator=(const member_context_t &) O2G_DELETED_FUNCTION;
#if __cplusplus >= 201103L
  member_context_t(member_context_t &&) = delete;
  member_context_t &operator=(member_context_t &&) = delete;
#endif
  const relation_t * const relation;
  GtkWidget * const dialog;
  osm_t::ref osm;
};

enum {
  MEMBER_COL_TYPE = 0,
  MEMBER_COL_ID,
  MEMBER_COL_NAME,
  MEMBER_COL_ROLE,
  MEMBER_COL_REF_ONLY,
  MEMBER_COL_DATA,
  MEMBER_NUM_COLS
};

gboolean
member_list_selection_func(GtkTreeSelection *, GtkTreeModel *model, GtkTreePath *path, gboolean, gpointer)
{
  GtkTreeIter iter;

  if(gtk_tree_model_get_iter(model, &iter, path) == TRUE) {
    assert_cmpnum(gtk_tree_path_get_depth(path), 1);

    const member_t *member = nullptr;
    gtk_tree_model_get(model, &iter, MEMBER_COL_DATA, &member, -1);
    if(member != nullptr && member->object.is_real())
      return TRUE;
  }

  return FALSE;
}

struct members_list_functor {
  GtkListStore * const store;
  osm_t::ref osm;
  explicit inline members_list_functor(GtkListStore *s, osm_t::ref o) : store(s), osm(o) {}
  void operator()(const member_t &member);
};

void members_list_functor::operator()(const member_t &member)
{
  GtkTreeIter iter;

  const std::string &id = member.object.id_string();

  /* Append a row and fill in some data */
  bool realObj = member.object.is_real();
  gtk_list_store_insert_with_values(store, &iter, -1,
                                    MEMBER_COL_TYPE,     static_cast<const gchar *>(member.object.type_string()),
                                    MEMBER_COL_ID,       id.c_str(),
                                    MEMBER_COL_NAME,     realObj ? static_cast<const gchar *>(member.object.get_name(*osm)) : nullptr,
                                    MEMBER_COL_ROLE,     member.role,
                                    MEMBER_COL_REF_ONLY, realObj ? FALSE : TRUE,
                                    MEMBER_COL_DATA,     &member,
                                    -1);
}

GtkWidget *
member_list_widget(member_context_t &context)
{
  GtkWidget *vbox = gtk_vbox_new(FALSE,3);
  GtkTreeView * const view = osm2go_platform::tree_view_new();

  gtk_tree_selection_set_select_function(gtk_tree_view_get_selection(view),
                                         member_list_selection_func, &context, nullptr);

  /* --- "type" column --- */
  GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
  g_object_set(renderer, "foreground", "grey", nullptr);
  GtkTreeViewColumn *column =
    gtk_tree_view_column_new_with_attributes(static_cast<const gchar *>(_("Type")), renderer,
		     "text", MEMBER_COL_TYPE,
		     "foreground-set", MEMBER_COL_REF_ONLY,  nullptr);
  gtk_tree_view_column_set_sort_column_id(column, MEMBER_COL_TYPE);
  gtk_tree_view_insert_column(view, column, -1);

  /* --- "id" column --- */
  renderer = gtk_cell_renderer_text_new();
  g_object_set(renderer, "foreground", "grey", nullptr);
  column = gtk_tree_view_column_new_with_attributes(static_cast<const gchar *>(_("Id")), renderer,
		     "text", MEMBER_COL_ID,
		     "foreground-set", MEMBER_COL_REF_ONLY,  nullptr);
  gtk_tree_view_column_set_sort_column_id(column, MEMBER_COL_ID);
  gtk_tree_view_insert_column(view, column, -1);

  /* --- "Name" column --- */
  renderer = gtk_cell_renderer_text_new();
  g_object_set(renderer, "foreground", "grey", nullptr);
  g_object_set(renderer, "ellipsize", PANGO_ELLIPSIZE_END, nullptr);
  column = gtk_tree_view_column_new_with_attributes(static_cast<const gchar *>(_("Name")), renderer,
		     "text", MEMBER_COL_NAME,
		     "foreground-set", MEMBER_COL_REF_ONLY,  nullptr);
  gtk_tree_view_column_set_expand(column, TRUE);
  gtk_tree_view_column_set_sort_column_id(column, MEMBER_COL_NAME);
  gtk_tree_view_insert_column(view, column, -1);

  /* --- "role" column --- */
  renderer = gtk_cell_renderer_text_new();
  g_object_set(renderer, "foreground", "grey", nullptr);
  column = gtk_tree_view_column_new_with_attributes(static_cast<const gchar *>(_("Role")), renderer,
		     "text", MEMBER_COL_ROLE,
		     "foreground-set", MEMBER_COL_REF_ONLY,  nullptr);
  gtk_tree_view_column_set_sort_column_id(column, MEMBER_COL_ROLE);
  gtk_tree_view_insert_column(view, column, -1);


  /* build and fill the store */
  GtkListStore * const store = gtk_list_store_new(MEMBER_NUM_COLS,
                                                  G_TYPE_STRING, G_TYPE_STRING,
                                                  G_TYPE_STRING, G_TYPE_STRING,
                                                  G_TYPE_BOOLEAN, G_TYPE_POINTER);

  gtk_tree_view_set_model(view, GTK_TREE_MODEL(store));

  std::for_each(context.relation->members.begin(), context.relation->members.end(),
                members_list_functor(store, context.osm));

  gtk_box_pack_start(GTK_BOX(vbox), osm2go_platform::scrollable_container(GTK_WIDGET(view)), TRUE, TRUE, 0);

  return vbox;
}

} // namespace

void relation_show_members(GtkWidget *parent, const relation_t *relation, osm_t::ref osm) {
  member_context_t mcontext(relation, osm, parent);

  osm2go_platform::dialog_size_hint(GTK_WINDOW(mcontext.dialog), osm2go_platform::MISC_DIALOG_MEDIUM);
  gtk_dialog_set_default_response(GTK_DIALOG(mcontext.dialog),
				  GTK_RESPONSE_CLOSE);

  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(mcontext.dialog)->vbox),
      		     member_list_widget(mcontext), TRUE, TRUE, 0);

  /* ----------------------------------- */

  gtk_widget_show_all(mcontext.dialog);
  gtk_dialog_run(GTK_DIALOG(mcontext.dialog));
  gtk_widget_destroy(mcontext.dialog);
}

namespace {

/* user clicked "members" button in relation list */
void
on_relation_members(relation_context_t *context)
{
  relation_t *sel = get_selected_relation(context);

  if(sel != nullptr)
    relation_show_members(context->dialog.get(), sel, context->osm);
}

/* user clicked "select" button in relation list */
void
on_relation_select(relation_context_t *context, GtkWidget *but)
{
  relation_t *sel = get_selected_relation(context);
  context->map->item_deselect();

  if(sel != nullptr) {
    context->map->select_relation(sel);

    /* tell dialog to close as we want to see the selected relation */

    GtkWidget *toplevel = gtk_widget_get_toplevel(GTK_WIDGET(but));
    assert(GTK_IS_DIALOG(toplevel) == TRUE);

    /* emit a "response" signal so we might close the dialog */
    gtk_dialog_response(GTK_DIALOG(toplevel), GTK_RESPONSE_CLOSE);
  }
}

void
on_relation_add(relation_context_t *context)
{
  /* create a new relation */

  std::unique_ptr<relation_t> relation(std::make_unique<relation_t>(0));
  if(!relation_info_dialog(context, relation.get())) {
    g_debug("tag edit cancelled");
    relation->cleanup();
  } else {
    relation_t *r = context->osm->relation_attach(relation.release());

    /* append a row for the new data */

    const std::string &name = r->descriptive_name();

    /* Append a row and fill in some data */
    GtkTreeIter iter;
    gtk_list_store_insert_with_values(context->store.get(), &iter, -1,
                                      RELATION_COL_TYPE,    r->tags.get_value("type"),
                                      RELATION_COL_NAME,    name.c_str(),
                                      RELATION_COL_MEMBERS, r->members.size(),
                                      RELATION_COL_DATA,    r,
                                      -1);

    gtk_tree_selection_select_iter(list_get_selection(context->list), &iter);
  }
}

struct relation_edit_context {
  const relation_t *sel;
  GtkWidget *list;
};

gboolean
relation_edit_foreach(GtkTreeModel *model, GtkTreePath *, GtkTreeIter *iter, gpointer data)
{
  const relation_edit_context * const context = static_cast<relation_edit_context *>(data);

  relation_t *row_rel;
  gtk_tree_model_get(model, iter, RELATION_COL_DATA, &row_rel, -1);
  if (row_rel != context->sel)
    return FALSE;

  const std::string &name = context->sel->descriptive_name();
  // Found it. Update all visible fields.
  gtk_list_store_set(GTK_LIST_STORE(model), iter,
                      RELATION_COL_TYPE,    context->sel->tags.get_value("type"),
                      RELATION_COL_NAME,    name.c_str(),
                      RELATION_COL_MEMBERS, context->sel->members.size(),
                      -1);

  // Order will probably have changed, so refocus
  list_focus_on(context->list, iter);
  return TRUE;
}

/* user clicked "edit..." button in relation list */
void
on_relation_edit(relation_context_t *context)
{
  relation_t *sel = get_selected_relation(context);
  if(sel == nullptr)
    return;

  g_debug("edit relation #" ITEM_ID_FORMAT, sel->id);

  if (!relation_info_dialog(context, sel))
    return;

  relation_edit_context ctx;
  ctx.sel = sel;
  ctx.list = context->list;
  gtk_tree_model_foreach(GTK_TREE_MODEL(context->store.get()), relation_edit_foreach, &ctx);
}

/* remove the selected relation */
void
on_relation_remove(relation_context_t *context)
{
  relation_t *sel = get_selected_relation(context);
  if(sel == nullptr)
    return;

  g_debug("remove relation #" ITEM_ID_FORMAT, sel->id);

  if(!sel->members.empty()) {
    trstring msg(ngettext("This relation still has %zu member. Delete it anyway?",
                          "This relation still has %zu members. Delete it anyway?",
                          sel->members.size()), nullptr, sel->members.size());
    if(!osm2go_platform::yes_no(_("Delete non-empty relation?"), msg, 0, context->dialog.get()))
      return;
  }

  /* first remove selected row from list */
  GtkTreeIter       iter;
  GtkTreeModel      *model;
  if(list_get_selected(context->list, &model, &iter))
    gtk_list_store_remove(context->store.get(), &iter);

  /* then really delete it */
  context->osm->relation_delete(sel);

  relation_list_selected(context->list, nullptr);
}

struct relation_list_widget_functor {
  GtkListStore * const store;
  explicit inline relation_list_widget_functor(GtkListStore *s) : store(s) {}
  void operator()(const relation_t *rel);
  inline void operator()(std::pair<item_id_t, relation_t *> pair) {
    operator()(pair.second);
  }
};

void relation_list_widget_functor::operator()(const relation_t *rel)
{
  if(rel->isDeleted())
    return;

  const std::string &name = rel->descriptive_name();

  /* Append a row and fill in some data */
  gtk_list_store_insert_with_values(store, nullptr, -1,
                                    RELATION_COL_TYPE, rel->tags.get_value("type"),
                                    RELATION_COL_NAME, name.c_str(),
                                    RELATION_COL_MEMBERS, rel->members.size(),
                                    RELATION_COL_DATA, rel,
                                    -1);
}

GtkWidget *
relation_list_widget(relation_context_t &context)
{
  std::vector<list_view_column> columns;
  columns.push_back(list_view_column(_("Type"),    0));
  columns.push_back(list_view_column(_("Name"),    LIST_FLAG_ELLIPSIZE));
  columns.push_back(list_view_column(_("Members"), 0));

  std::vector<list_button> buttons;
  buttons.push_back(list_button(_("_New"), G_CALLBACK(on_relation_add)));
  buttons.push_back(list_button(_("_Edit"), G_CALLBACK(on_relation_edit)));
  buttons.push_back(list_button(_("Remove"), G_CALLBACK(on_relation_remove)));
  buttons.push_back(list_button(_("Members"), G_CALLBACK(on_relation_members)));
  buttons.push_back(list_button(_("Select"),  G_CALLBACK(on_relation_select)));

  /* build and fill the store */
  context.store.reset(gtk_list_store_new(RELATION_NUM_COLS,
                                         G_TYPE_STRING, G_TYPE_STRING, G_TYPE_UINT,
                                         G_TYPE_POINTER));

  context.list = list_new(LIST_HILDON_WITH_HEADERS, &context,
                          relation_list_changed, buttons, columns,
                          GTK_TREE_MODEL(context.store.get()));

  // Sorting by ref/name by default is useful for places with lots of numbered
  // bus routes. Especially for small screens.
  gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(context.store.get()),
                                       RELATION_COL_NAME, GTK_SORT_ASCENDING);

  relation_list_widget_functor fc(context.store.get());

  const std::map<item_id_t, relation_t *> &rchain = context.osm->relations;
  std::for_each(rchain.begin(), rchain.end(), fc);

  relation_list_selected(context.list, nullptr);

  return context.list;
}

}

/* a global view on all relations */
void relation_list(GtkWidget *parent, map_t *map, osm_t::ref osm, presets_items *presets) {
  relation_context_t context(map, osm, presets,
                             gtk_dialog_new_with_buttons(static_cast<const gchar *>(_("All relations")),
                                                         GTK_WINDOW(parent),
                                                         GTK_DIALOG_MODAL,
                                                         GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
                                                         nullptr));

  osm2go_platform::dialog_size_hint(context.dialog, osm2go_platform::MISC_DIALOG_LARGE);
  gtk_dialog_set_default_response(context.dialog, GTK_RESPONSE_CLOSE);

  gtk_box_pack_start(context.dialog.vbox(), relation_list_widget(context), TRUE, TRUE, 0);

  /* ----------------------------------- */


  gtk_widget_show_all(context.dialog.get());
  gtk_dialog_run(context.dialog);
}
