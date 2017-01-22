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
 * along with OSM2Go.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef PROJECT_H
#define PROJECT_H

#include "appdata.h"
#include "map.h"
#include "pos.h"
#include "settings.h"

#include <glib.h>
#include <gtk/gtk.h>
#include <libxml/parser.h>

typedef struct project_t {
#ifdef __cplusplus
  project_t(const gchar *n, const char *base_path);
  ~project_t();
#endif

  const char * const name;
  const char * const path;

  xmlChar *desc;
  const char *server; /**< the server string used, either rserver or settings->server */
  gchar *rserver;
  char *osm;

  char *wms_server;
  char *wms_path;
  struct { gint x, y; } wms_offset;

  map_state_t *map_state;

  pos_t min, max;

  gboolean data_dirty;     /* needs to download new data */
} project_t;

#ifdef __cplusplus
extern "C" {
#endif

gboolean project_exists(settings_t *settings, const char *name);
gboolean project_save(GtkWidget *parent, project_t *project);
gboolean project_load(appdata_t *appdata, const char *name);
gboolean project_check_demo(GtkWidget *parent, project_t *project);

void project_free(project_t *project);

#ifdef __cplusplus
}
#endif

#endif // PROJECT_H
