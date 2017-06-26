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

#ifndef JOSM_PRESETS_H
#define JOSM_PRESETS_H

#include <gtk/gtk.h>
#include <string>

struct presets_items;

presets_items *josm_presets_load(void);
void josm_presets_free(presets_items *presets);

struct appdata_t;
class tag_context_t;

std::string josm_icon_name_adjust(const char *xname, const std::string &basepath = std::string());

GtkWidget *josm_build_presets_button(appdata_t *appdata, tag_context_t *tag_context);

#endif // JOSM_PRESETS_H
