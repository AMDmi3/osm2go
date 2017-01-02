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

/*
  http://josm.openstreetmap.de/svn/trunk/styles/standard/elemstyles.xml
*/

#include "josm_elemstyles.h"

#include "appdata.h"
#include "josm_presets.h"
#include "map.h"
#include "misc.h"
#include "style.h"

#include <algorithm>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <strings.h>

#ifndef LIBXML_TREE_ENABLED
#error "Tree not enabled in libxml"
#endif

struct elemstyle_condition_t {
    xmlChar *key;
    xmlChar *value;
};

struct elemstyle_t {
  elemstyle_t()
    : type(ES_TYPE_NONE)
    , line_mod(0)
    , icon(0)
  {
  }
  ~elemstyle_t();

  std::vector<elemstyle_condition_t> conditions;

  elemstyle_type_t type;

  union {
    elemstyle_line_mod_t *line_mod;
    elemstyle_line_t *line;
    elemstyle_area_t *area;
  };

  elemstyle_icon_t *icon;
};

// ratio conversions

// Scaling constants. Our "zoom" is a screenpx:canvasunit ratio, and the figure
// given by an elemstyles.xml is the denominator of a screen:real ratio.

#define N810_PX_PER_METRE (800 / 0.09)
    // XXX should probably ask the windowing system for DPI and
    // work from that instead

float scaledn_to_zoom(const float scaledn) {
  return N810_PX_PER_METRE / scaledn;
}

float zoom_to_scaledn(const float zoom) {
  return N810_PX_PER_METRE / zoom;
}


/* --------------------- elemstyles.xml parsing ----------------------- */

gboolean parse_color(xmlNode *a_node, const char *name,
		     elemstyle_color_t *color) {
  xmlChar *color_str = xmlGetProp(a_node, BAD_CAST name);
  gboolean ret = FALSE;

  if(color_str) {
    /* if the color name contains a # it's a hex representation */
    /* we parse this directly since gdk_color_parse doesn't cope */
    /* with the alpha channel that may be present */
    if(*color_str == '#' && strlen((const char*)color_str) == 9) {
      char *err;

      *color = strtoul((const char*)color_str + 1, &err, 16);

      ret = (*err == '\0') ? TRUE : FALSE;
    } else {
      GdkColor gdk_color;
      if(gdk_color_parse((const gchar*)color_str, &gdk_color)) {
        *color =
	  ((gdk_color.red   << 16) & 0xff000000) |
	  ((gdk_color.green <<  8) & 0xff0000) |
	  ((gdk_color.blue       ) & 0xff00) |
	  (0xff);

        ret = TRUE;
      }
    }
    xmlFree(color_str);
  }
  return ret;
}

static gboolean parse_gint(xmlNode *a_node, const char *name, gint *val) {
  xmlChar *num_str = xmlGetProp(a_node, BAD_CAST name);
  if(num_str) {
    *val = strtoul((const char*)num_str, NULL, 10);
    xmlFree(num_str);
    return TRUE;
  }
  return FALSE;
}

static gboolean parse_scale_max(xmlNode *a_node, float *val) {
  xmlChar *val_str = xmlNodeGetContent(a_node);
  if (val_str) {
    *val = scaledn_to_zoom(strtod((char *)val_str, NULL));
    xmlFree(val_str);
    return TRUE;
  }
  return FALSE;
}

static gboolean parse_gboolean(xmlNode *a_node, const char *name) {
  xmlChar *bool_str = xmlGetProp(a_node, BAD_CAST name);
  if (!bool_str) {
    return FALSE;
  }
  static const char *true_str[]  = { "1", "yes", "true", 0 };
  gboolean ret = FALSE;
  for (int i = 0; true_str[i]; ++i) {
    if (strcasecmp((const char*)bool_str, true_str[i])==0) {
      ret = TRUE;
      break;
    }
  }
  xmlFree(bool_str);
  return ret;
}

static elemstyle_line_t *parse_line(xmlNode *a_node) {
  elemstyle_line_t *line = g_new0(elemstyle_line_t, 1);

  /* these have to be present */
  g_assert(parse_color(a_node, "colour", &line->color));
  g_assert(parse_gint(a_node, "width", &line->width));

  line->real.valid =
    parse_gint(a_node, "realwidth", &line->real.width);

  line->bg.valid =
    parse_gint(a_node, "width_bg", &line->bg.width) &&
    parse_color(a_node, "colour_bg", &line->bg.color);

  line->dashed = parse_gboolean(a_node, "dashed");
  if (!parse_gint(a_node, "dash_length", &line->dash_length)) {
    line->dash_length = DEFAULT_DASH_LENGTH;
  }

  return line;
}

/* parse "+123", "-123" and "123%" */
static void parse_width_mod(xmlNode *a_node, const char *name,
			    elemstyle_width_mod_t *value) {
  char *mod_str = (char*)xmlGetProp(a_node, BAD_CAST name);
  if(!mod_str)
    return;
  if(strlen(mod_str) > 0) {
    if(mod_str[0] == '+') {
      value->mod = ES_MOD_ADD;
      value->width = strtoul(mod_str+1, NULL, 10);
    } else if(mod_str[0] == '-') {
      value->mod = ES_MOD_SUB;
      value->width = strtoul(mod_str+1, NULL, 10);
    } else if(mod_str[strlen(mod_str)-1] == '%') {
      value->mod = ES_MOD_PERCENT;
      value->width = strtoul(mod_str, NULL, 10);
    } else
      printf("warning: unable to parse modifier %s\n", mod_str);
  }
  xmlFree(mod_str);
}

static elemstyle_line_mod_t *parse_line_mod(xmlNode *a_node) {
  elemstyle_line_mod_t *line_mod = g_new0(elemstyle_line_mod_t, 1);

  parse_width_mod(a_node, "width", &line_mod->line);
  parse_width_mod(a_node, "width_bg", &line_mod->bg);

  return line_mod;
}

static elemstyle_area_t *parse_area(xmlNode *a_node) {
  elemstyle_area_t *area = g_new0(elemstyle_area_t, 1);

  /* these have to be present */
  g_assert(parse_color(a_node, "colour", &area->color));
  return area;
}

static elemstyle_icon_t *parse_icon(xmlNode *a_node) {
  elemstyle_icon_t *icon = g_new0(elemstyle_icon_t, 1);

  icon->annotate = xml_get_prop_is(a_node, "annotate", "true");

  icon->filename = (char*)xmlGetProp(a_node, BAD_CAST "src");
  g_assert(icon->filename);

  icon->filename = josm_icon_name_adjust(icon->filename);

  icon->zoom_max = 0;

  return icon;
}

static elemstyle_t *parse_rule(xmlNode *a_node) {
  xmlNode *cur_node = NULL;
  elemstyle_t *elemstyle = new elemstyle_t();

  for (cur_node = a_node->children; cur_node; cur_node = cur_node->next) {
    if (cur_node->type == XML_ELEMENT_NODE) {
      if(strcasecmp((char*)cur_node->name, "condition") == 0) {
        /* ------ parse condition ------ */
        elemstyle_condition_t newcond;
        newcond.key   = xmlGetProp(cur_node, BAD_CAST "k");
        newcond.value = xmlGetProp(cur_node, BAD_CAST "v");
        elemstyle->conditions.push_back(newcond);
	/* todo: add support for "b" (boolean) value */
      } else if(strcasecmp((char*)cur_node->name, "line") == 0) {
	/* ------ parse line ------ */
	g_assert(elemstyle->type == ES_TYPE_NONE);
	elemstyle->type = ES_TYPE_LINE;
	elemstyle->line = parse_line(cur_node);
      } else if(strcasecmp((char*)cur_node->name, "linemod") == 0) {
	/* ------ parse linemod ------ */
	g_assert(elemstyle->type == ES_TYPE_NONE);
	elemstyle->type = ES_TYPE_LINE_MOD;
	elemstyle->line_mod = parse_line_mod(cur_node);
      } else if(strcasecmp((char*)cur_node->name, "area") == 0) {
	/* ------ parse area ------ */
	g_assert(elemstyle->type == ES_TYPE_NONE);
	elemstyle->type = ES_TYPE_AREA;
	elemstyle->area = parse_area(cur_node);
      } else if(strcasecmp((char*)cur_node->name, "icon") == 0) {
	elemstyle->icon = parse_icon(cur_node);
      } else if(strcasecmp((char*)cur_node->name, "scale_min") == 0) {
	/* scale_min is currently ignored */
      } else if(strcasecmp((char*)cur_node->name, "scale_max") == 0) {
	switch (elemstyle->type) {
	case ES_TYPE_LINE:
          parse_scale_max(cur_node, &elemstyle->line->zoom_max);
	  break;
	case ES_TYPE_AREA:
          parse_scale_max(cur_node, &elemstyle->area->zoom_max);
	  break;
	default:
	  if (elemstyle->icon) {
            parse_scale_max(cur_node, &elemstyle->icon->zoom_max);
	  }
	  else {
	    printf("scale_max for unhandled elemstyletype=0x02%x\n",
		   elemstyle->type);
	  }
	  break;
	}
      } else {
	printf("found unhandled rules/rule/%s\n", cur_node->name);
      }
    }
  }

  return elemstyle;
}

static std::vector<elemstyle_t *> parse_rules(xmlNode *a_node) {
  xmlNode *cur_node = NULL;
  std::vector<elemstyle_t *> ret;

  for (cur_node = a_node->children; cur_node; cur_node = cur_node->next) {
    if (cur_node->type == XML_ELEMENT_NODE) {
      if(strcasecmp((char*)cur_node->name, "rule") == 0) {
        elemstyle_t *elemstyle = parse_rule(cur_node);
        if(elemstyle)
          ret.push_back(elemstyle);
      } else
	printf("found unhandled rules/%s\n", cur_node->name);
    }
  }
  return ret;
}

static std::vector<elemstyle_t *> parse_doc(xmlDocPtr doc) {
  /* Get the root element node */
  xmlNode *cur_node = NULL;
  std::vector<elemstyle_t *> elemstyles;

  for(cur_node = xmlDocGetRootElement(doc);
      cur_node; cur_node = cur_node->next) {
    if (cur_node->type == XML_ELEMENT_NODE) {
      if(strcasecmp((char*)cur_node->name, "rules") == 0) {
	elemstyles = parse_rules(cur_node);
      } else
	printf("found unhandled %s\n", cur_node->name);
    }
  }

  xmlFreeDoc(doc);
  return elemstyles;
}

std::vector<elemstyle_t *> josm_elemstyles_load(const char *name) {
  std::vector<elemstyle_t *> elemstyles;

  printf("Loading JOSM elemstyles ...\n");

  const std::string &filename = find_file(name, NULL, NULL);
  if(filename.empty()) {
    printf("elemstyle file not found\n");
    return elemstyles;
  }

  /* parse the file and get the DOM */
  xmlDoc *doc = NULL;
  if((doc = xmlReadFile(filename.c_str(), NULL, 0)) == NULL) {
    xmlErrorPtr errP = xmlGetLastError();
    printf("elemstyles download failed: "
	   "XML error while parsing:\n"
	   "%s\n", errP->message);
  } else {
    printf("ok, parse doc tree\n");
    elemstyles = parse_doc(doc);
  }

  return elemstyles;
}

/* ----------------------- cleaning up --------------------- */

static void free_line(elemstyle_line_t *line) {
  g_free(line);
}

static void free_line_mod(elemstyle_line_mod_t *line_mod) {
  g_free(line_mod);
}

static void free_area(elemstyle_area_t *area) {
  g_free(area);
}

static void free_icon(elemstyle_icon_t *icon) {
  if(icon->filename) xmlFree(icon->filename);
  g_free(icon);
}

static void free_condition(elemstyle_condition_t cond) {
  xmlFree(cond.key);
  xmlFree(cond.value);
}

static inline void elemstyle_free(elemstyle_t *elemstyle) {
  delete elemstyle;
}

void josm_elemstyles_free(std::vector<elemstyle_t *> &elemstyles) {
  std::for_each(elemstyles.begin(), elemstyles.end(), elemstyle_free);
  elemstyles.clear();
}

#define WIDTH_SCALE (1.0)

struct condition_not_matches_obj {
  const base_object_t * const obj;
  condition_not_matches_obj(const base_object_t *o) : obj(o) {}
  bool operator()(const elemstyle_condition_t &cond);
};

bool condition_not_matches_obj::operator()(const elemstyle_condition_t &cond) {
  if(cond.key) {
    const char *value = obj->get_value((char*)cond.key);
    if(!value || (cond.value && strcasecmp(value, (char*)cond.value) != 0))
      return true;
  } else if(cond.value) {
    if(!obj->has_value((char*)cond.value))
      return true;
  }
  return false;
}

struct colorize_node {
  const style_t * const style;
  node_t * const node;
  bool &somematch;
  colorize_node(const style_t *s, node_t *n, bool &m) : style(s), node(n), somematch(m) {}
  void operator()(elemstyle_t *elemstyle);
};

void colorize_node::operator()(elemstyle_t *elemstyle)
{
  // Rule without conditions matches everything (should it?)
  // For rule with conditions, if any condition mismatches->rule mismatches
  bool match = std::find_if(elemstyle->conditions.begin(),
                            elemstyle->conditions.end(),
                            condition_not_matches_obj(node)) == elemstyle->conditions.end();

  somematch |= match;

  if(match && elemstyle->icon) {
    char *name = g_strjoin("/", "styles", style->icon.path_prefix,
                           elemstyle->icon->filename, 0);

    /* free old icon if there's one present */
    if(node->icon_buf) {
      icon_free(style->iconP, node->icon_buf);
      node->icon_buf = NULL;
    }

    node->icon_buf = icon_load(style->iconP, name);
    g_free(name);

    if (elemstyle->icon->zoom_max > 0)
      node->zoom_max = elemstyle->icon->zoom_max;
  }
}

void josm_elemstyles_colorize_node(const style_t *style, node_t *node) {
  node->zoom_max = style->node.zoom_max;

  bool somematch = false;
  colorize_node fc(style, node, somematch);
  std::for_each(style->elemstyles.begin(), style->elemstyles.end(), fc);

  /* clear icon for node if not matched at least one rule and has an icon attached */
  if (!somematch && node->icon_buf) {
    icon_free(style->iconP, node->icon_buf);
    node->icon_buf = NULL;
  }
}

struct josm_elemstyles_colorize_node_functor {
  const style_t * const styles;
  josm_elemstyles_colorize_node_functor(const style_t *s) : styles(s) {}
  void operator()(std::pair<item_id_t, node_t *> pair);
};

void josm_elemstyles_colorize_node_functor::operator()(std::pair<item_id_t, node_t *> pair) {
  node_t * const node = pair.second;
  /* remove all icon references that may still be there from */
  /* an old style */
  if(node->icon_buf) {
    icon_free(styles->iconP, node->icon_buf);
    node->icon_buf = NULL;
  }

  josm_elemstyles_colorize_node(styles, node);
}

static void line_mod_apply(gint *width, const elemstyle_width_mod_t *mod) {
  switch(mod->mod) {
  case ES_MOD_NONE:
    break;

  case ES_MOD_ADD:
    *width += mod->width;
    break;

  case ES_MOD_SUB:
    *width -= mod->width;
    break;

  case ES_MOD_PERCENT:
    *width = 100 * *width / mod->width;
    break;
  }
}

struct josm_elemstyles_colorize_way_functor {
  const style_t * const style;
  josm_elemstyles_colorize_way_functor(const style_t *s) : style(s) {}
  void operator()(way_t *way);
  void operator()(std::pair<item_id_t, way_t *> pair) {
    operator()(pair.second);
  }

  struct apply_condition {
    const style_t * const style;
    way_t * const way;
    /* during the elemstyle search a line_mod may be found. save it here */
    elemstyle_line_mod_t **line_mod;
    bool way_processed;
    bool way_is_closed;
    apply_condition(const style_t *s, way_t *w, elemstyle_line_mod_t **l)
      : style(s), way(w), line_mod(l), way_processed(false)
      , way_is_closed(way->is_closed()) {}
    void operator()(const elemstyle_t *elemstyle);
  };
};

void josm_elemstyles_colorize_way_functor::apply_condition::operator()(const elemstyle_t* elemstyle)
{
  bool match = std::find_if(elemstyle->conditions.begin(),
                            elemstyle->conditions.end(),
                            condition_not_matches_obj(way)) == elemstyle->conditions.end();

  if(!match)
    return;

  switch(elemstyle->type) {
  case ES_TYPE_NONE:
    /* this entry does not contain line or area descriptions and is */
    /* likely just an icon. ignore this as it doesn't make much sense */
    /* for a way */
    break;

  case ES_TYPE_LINE:
    if(!way_processed) {
      way->draw.color = elemstyle->line->color;
      way->draw.width =  WIDTH_SCALE * elemstyle->line->width;
      if(elemstyle->line->bg.valid) {
        way->draw.flags |= OSM_DRAW_FLAG_BG;
        way->draw.bg.color = elemstyle->line->bg.color;
        way->draw.bg.width =  WIDTH_SCALE * elemstyle->line->bg.width;
      }
      if (elemstyle->line->zoom_max > 0)
        way->draw.zoom_max = elemstyle->line->zoom_max;
      else
        way->draw.zoom_max = style->way.zoom_max;

      way->draw.dashed = elemstyle->line->dashed;
      way->draw.dash_length = elemstyle->line->dash_length;
      way_processed = true;
    }
    break;

  case ES_TYPE_LINE_MOD:
    /* just save the fact that a line mod was found for later */
    *line_mod = elemstyle->line_mod;
    break;

  case ES_TYPE_AREA:
    if(way_is_closed && !way_processed) {
      way->draw.flags |= OSM_DRAW_FLAG_AREA;
      /* comment the following line for grey border around all areas */
      /* (potlatch style) */

      if(style->area.has_border_color)
        way->draw.color = style->area.border_color;
      else
        way->draw.color = elemstyle->area->color;

      way->draw.width =  WIDTH_SCALE * style->area.border_width;
      /* apply area alpha */
      way->draw.area.color =
      RGBA_COMBINE(elemstyle->area->color, style->area.color);
      if (elemstyle->area->zoom_max > 0)
        way->draw.zoom_max = elemstyle->area->zoom_max;
      else
        way->draw.zoom_max = style->area.zoom_max;

      way_processed = true;
    }
    break;
  }
}

void josm_elemstyles_colorize_way_functor::operator()(way_t *way) {
  /* use dark grey/no stroke/not filled for everything unknown */
  way->draw.color = style->way.color;
  way->draw.width = style->way.width;
  way->draw.flags = 0;
  way->draw.zoom_max = 0;   // draw at all zoom levels

  /* during the elemstyle search a line_mod may be found. save it here */
  elemstyle_line_mod_t *line_mod = NULL;
  apply_condition fc(style, way, &line_mod);

  std::for_each(style->elemstyles.begin(), style->elemstyles.end(), fc);

  /* apply the last line mod entry that has been found during search */
  if(line_mod) {
    printf("applying last matching line mod to way #" ITEM_ID_FORMAT "\n",
	   way->id);
    line_mod_apply(&way->draw.width, &line_mod->line);

    /* special case: the way does not have a background, but it is to */
    /* be modified */
    if((line_mod->bg.mod != ES_MOD_NONE) &&
       (!(way->draw.flags & OSM_DRAW_FLAG_BG))) {
      printf("forcing background\n");

      /* add a background in black color */
      way->draw.flags |= OSM_DRAW_FLAG_BG;
      way->draw.bg.color = (0) | 0xff;
      way->draw.bg.width =  way->draw.width;
    }

    line_mod_apply(&way->draw.bg.width, &line_mod->bg);
  }
}

void josm_elemstyles_colorize_way(const style_t *style, way_t *way) {
  josm_elemstyles_colorize_way_functor f(style);
  f(way);
}

void josm_elemstyles_colorize_world(const style_t *styles, osm_t *osm) {

  printf("preparing colors\n");

  /* colorize ways */
  std::for_each(osm->ways.begin(), osm->ways.end(),
      josm_elemstyles_colorize_way_functor(styles));

  /* icons */
  std::for_each(osm->nodes.begin(), osm->nodes.end(),
      josm_elemstyles_colorize_node_functor(styles));
}

elemstyle_t::~elemstyle_t()
{
  std::for_each(conditions.begin(), conditions.end(), free_condition);

  switch(type) {
  case ES_TYPE_NONE:
    break;
  case ES_TYPE_LINE:
    free_line(line);
    break;
  case ES_TYPE_AREA:
    free_area(area);
    break;
  case ES_TYPE_LINE_MOD:
    free_line_mod(line_mod);
    break;
  }

  if(icon)
    free_icon(icon);
}

// vim:et:ts=8:sw=2:sts=2:ai
