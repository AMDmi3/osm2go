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

/*
  http://josm.openstreetmap.de/svn/trunk/styles/standard/elemstyles.xml
*/

#include "josm_elemstyles.h"
#include "josm_elemstyles_p.h"

#include "josm_presets.h"
#include "map.h"
#include "misc.h"
#include "style.h"
#include "xml_helpers.h"

#include <algorithm>
#include <cassert>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <limits>
#include <map>
#include <strings.h>

#include "osm2go_annotations.h"
#include <osm2go_cpp.h>
#include "osm2go_stl.h"

#ifndef LIBXML_TREE_ENABLED
#error "Tree not enabled in libxml"
#endif

// this assertion is here and not in the header to avoid needless
// additional includes on non-C++11 compilers (i.e. Fremantle)
static_assert(sizeof(color_t) == sizeof(unsigned int), "wrong size for color_t");

typedef std::map<std::string, color_t> ColorMap;

class StyleSax {
  xmlSAXHandler handler;

  enum State {
    DocStart,
    TagRules,
    TagRule,
    TagCondition,
    TagLine,
    TagLineMod,
    TagArea,
    TagIcon,
    TagScaleMin,
    TagScaleMax
  };

  State state;

  struct StateChange {
    StateChange(const char *nm, State os, State ns)
      : name(nm), oldState(os), newState(ns) {}
    const char *name;
    State oldState;
    State newState;
  };

  typedef std::vector<StateChange> StateMap;
  StateMap tags;
  // custom find to avoid memory allocations for std::string
  struct tag_find {
    const char * const name;
    explicit tag_find(const xmlChar *n) : name(reinterpret_cast<const char *>(n)) {}
    bool operator()(const StateMap::value_type &p) {
      return (strcmp(p.name, name) == 0);
    }
  };

public:
  explicit StyleSax();

  bool parse(const std::string &filename);

  std::vector<elemstyle_t *> styles;

private:
  ColorMap colors;

  void characters(const char *ch, int len);
  static void cb_characters(void *ts, const xmlChar *ch, int len) {
    static_cast<StyleSax *>(ts)->characters(reinterpret_cast<const char *>(ch), len);
  }
  void startElement(const xmlChar *name, const char **attrs);
  static void cb_startElement(void *ts, const xmlChar *name, const xmlChar **attrs) {
    static_cast<StyleSax *>(ts)->startElement(name, reinterpret_cast<const char **>(attrs));
  }
  void endElement(const xmlChar *name);
  static void cb_endElement(void *ts, const xmlChar *name) {
    static_cast<StyleSax *>(ts)->endElement(name);
  }
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

/* --------------------- elemstyles.xml parsing ----------------------- */

static bool parse_color(const char *col, color_t &color, ColorMap &colors) {
  bool ret = false;

  /* if the color name contains a # it's a hex representation */
  const char * const hash = strchr(col, '#');
  std::string colname;
  if(hash) {
    ret = osm2go_platform::parse_color_string(hash, color);
    if(ret && hash != col)
      colname.assign(col, hash - col);
  } else {
    colname = col;
  }

  if(!colname.empty()) {
    const ColorMap::const_iterator it = colors.find(colname);
    if(it == colors.end()) {
      if(unlikely(!hash)) {
        printf("found invalid colour name reference '%s'\n", col);
      } else {
        colors[colname] = color;
      }
    } else {
      if(hash == col) {
        color = it->second;
        ret = true;
      } else if(hash)
        // check that the colors are the same if the key is specified multiple times
        assert_cmpnum(it->second, color);
    }
  }

  return ret;
}

bool parse_color(xmlNode *a_node, const char *name, color_t &color)
{
  xmlString color_str(xmlGetProp(a_node, BAD_CAST name));
  bool ret = false;

  if(color_str) {
    ColorMap dummy;
    ret = parse_color(reinterpret_cast<char *>(color_str.get()), color, dummy);
  }
  return ret;
}

static float parse_scale(const char *val_str, int len) {
  char buf[G_ASCII_DTOSTR_BUF_SIZE];
  if(unlikely(static_cast<unsigned int>(len) >= sizeof(buf))) {
    return 0.0;
  } else {
    memcpy(buf, val_str, len);
    buf[len] = '\0';
    return scaledn_to_zoom(strtod(buf, nullptr));
  }
}

static const char *true_values[] = { "1", "yes", "true", nullptr };
static const char *false_values[] = { "0", "no", "false", nullptr };

static bool parse_boolean(const char *bool_str, const char **value_strings) {
  for (int i = 0; value_strings[i]; ++i)
    if (strcasecmp(bool_str, value_strings[i]) == 0)
      return true;
  return false;
}

StyleSax::StyleSax()
  : state(DocStart)
{
  memset(&handler, 0, sizeof(handler));
  handler.characters = cb_characters;
  handler.startElement = cb_startElement;
  handler.endElement = cb_endElement;

  tags.push_back(StateMap::value_type("rules", DocStart, TagRules));
  tags.push_back(StateMap::value_type("rule", TagRules, TagRule));
  tags.push_back(StateMap::value_type("condition", TagRule, TagCondition));
  tags.push_back(StateMap::value_type("line", TagRule, TagLine));
  tags.push_back(StateMap::value_type("linemod", TagRule, TagLineMod));
  tags.push_back(StateMap::value_type("area", TagRule, TagArea));
  tags.push_back(StateMap::value_type("icon", TagRule, TagIcon));
  tags.push_back(StateMap::value_type("scale_min", TagRule, TagScaleMin));
  tags.push_back(StateMap::value_type("scale_max", TagRule, TagScaleMax));
}

bool StyleSax::parse(const std::string &filename)
{
  if (xmlSAXUserParseFile(&handler, this, filename.c_str()) != 0)
    return false;

  return !styles.empty();
}

void StyleSax::characters(const char *ch, int len)
{
  std::string buf;

  switch(state) {
  case TagScaleMin:
    // currently ignored
    break;
  case TagScaleMax:
    styles.back()->zoom_max = parse_scale(ch, len);
    break;
  default:
    for(int pos = 0; pos < len; pos++)
      if(!isspace(ch[pos])) {
        printf("unhandled character data: %*.*s state %i\n", len, len, ch, state);
        break;
      }
  }
}

/* parse "+123", "-123" and "123%" */
static void parse_width_mod(const char *mod_str, elemstyle_width_mod_t &value) {
  if(strlen(mod_str) > 0) {
    if(mod_str[0] == '+') {
      value.mod = ES_MOD_ADD;
      value.width = strtoul(mod_str+1, nullptr, 10);
    } else if(mod_str[0] == '-') {
      value.mod = ES_MOD_SUB;
      value.width = strtoul(mod_str+1, nullptr, 10);
    } else if(mod_str[strlen(mod_str)-1] == '%') {
      value.mod = ES_MOD_PERCENT;
      value.width = strtoul(mod_str, nullptr, 10);
    } else
      printf("warning: unable to parse modifier %s\n", mod_str);
  }
}

static int parse_priority(const char *attr)
{
  char *endch;
  long prio = strtol(attr, &endch, 10);
  if(likely(*endch == '\0'))
    return prio;
  else
    return 0;
}

void StyleSax::startElement(const xmlChar *name, const char **attrs)
{
  StateMap::const_iterator it = std::find_if(tags.begin(), tags.end(), tag_find(name));

  if(unlikely(it == tags.end())) {
    fprintf(stderr, "found unhandled element %s\n", name);
    return;
  }

  if(unlikely(state != it->oldState)) {
    fprintf(stderr, "found element %s in state %i, but expected %i\n",
            name, state, it->oldState);
    return;
  }

  state = it->newState;

  elemstyle_t * const elemstyle = styles.empty() ? nullptr : styles.back();

  switch(state){
  case TagRule:
    styles.push_back(new elemstyle_t());
    break;
  case TagCondition: {
    const char *k = nullptr, *v = nullptr;
    const char *b = nullptr;

    for(unsigned int i = 0; attrs[i]; i += 2) {
      if(strcmp(attrs[i], "k") == 0)
        k = attrs[i + 1];
      else if(strcmp(attrs[i], "v") == 0)
        v = attrs[i + 1];
      else if(strcmp(attrs[i], "b") == 0)
        b = attrs[i + 1];
    }
    assert(k != nullptr);
    elemstyle_condition_t cond = !b ? elemstyle_condition_t(k, v) :
                                 elemstyle_condition_t(k, parse_boolean(b, true_values));
    styles.back()->conditions.push_back(cond);
    break;
  }
  case TagLine: {
    assert_cmpnum(elemstyle->type & (ES_TYPE_LINE | ES_TYPE_LINE_MOD), 0);
    elemstyle->type |= ES_TYPE_LINE;

    bool hasBgWidth = false, hasBgColor = false;
    /* these have to be present */
    bool hasColor = false, hasWidth = false;
    elemstyle_line_t *line = elemstyle->line = new elemstyle_line_t();

    for(unsigned int i = 0; attrs[i]; i += 2) {
      if(strcmp(attrs[i], "colour") == 0) {
        hasColor = parse_color(attrs[i + 1], line->color, colors);
      } else if(strcmp(attrs[i], "width") == 0) {
        char *endch;
        line->width = strtoul(attrs[i + 1], &endch, 10);
        hasWidth = (*endch == '\0');
      } else if(strcmp(attrs[i], "realwidth") == 0) {
        char *endch;
        line->real.width = strtoul(attrs[i + 1], &endch, 10);
        line->real.valid = (*endch == '\0');
      } else if(strcmp(attrs[i], "width_bg") == 0) {
        char *endch;
        line->bg.width = strtoul(attrs[i + 1], &endch, 10);
        hasBgWidth = (*endch == '\0');
      } else if(strcmp(attrs[i], "colour_bg") == 0) {
        hasBgColor = parse_color(attrs[i + 1], line->bg.color, colors);
      } else if(strcmp(attrs[i], "priority") == 0) {
        line->priority = parse_priority(attrs[i + 1]);
      } else if(strcmp(attrs[i], "dashed") == 0) {
        const char * const dval = attrs[i + 1];
        if(parse_boolean(dval, true_values)) {
          line->dash_length_on = DEFAULT_DASH_LENGTH;
          line->dash_length_off = DEFAULT_DASH_LENGTH;
        } else if (parse_boolean(dval, false_values)) {
          line->dash_length_on = 0;
          line->dash_length_off = 0;
        } else {
          char *end;
          line->dash_length_on = strtoul(dval, &end, 10);
          if(*end == ',')
            line->dash_length_off = strtoul(end + 1, &end, 10);
          else
            line->dash_length_off = line->dash_length_on;
          if(unlikely(*end != '\0')) {
            printf("WARNING: invalid value '%s' for dashed\n", dval);
            line->dash_length_on = 0;
            line->dash_length_off = 0;
          }
        }
      }
    }

    line->bg.valid = hasBgColor && hasBgWidth;

    assert(hasColor);
    assert(hasWidth);

    break;
  }
  case TagLineMod: {
    assert_cmpnum(elemstyle->type & (ES_TYPE_LINE | ES_TYPE_LINE_MOD), 0);
    elemstyle->type |= ES_TYPE_LINE_MOD;

    elemstyle_line_mod_t &line_mod = elemstyle->line_mod;

    for(unsigned int i = 0; attrs[i]; i += 2) {
      if(strcmp(attrs[i], "colour") == 0) {
        color_t col;
        if(parse_color(attrs[i + 1], col, colors))
          line_mod.color = col;
      } else if(strcmp(attrs[i], "width") == 0)
        parse_width_mod(attrs[i + 1], line_mod.line);
      else if(strcmp(attrs[i], "width_bg") == 0)
        parse_width_mod(attrs[i + 1], line_mod.bg);
      else if(strcmp(attrs[i], "priority") == 0)
        line_mod.priority = parse_priority(attrs[i + 1]);
    }
    break;
  }
  case TagArea: {
    assert_cmpnum(elemstyle->type & ES_TYPE_AREA, 0);
    elemstyle->type |= ES_TYPE_AREA;

    bool hasColor = false;
    for(unsigned int i = 0; attrs[i] && !hasColor; i += 2) {
      if(strcmp(attrs[i], "colour") == 0)
        hasColor = parse_color(attrs[i + 1], elemstyle->area.color, colors);
      else if(strcmp(attrs[i], "priority") == 0)
        elemstyle->area.priority = parse_priority(attrs[i + 1]);
    }

    /* this has to be present */
    assert(hasColor);
    break;
  }
  case TagIcon:
    for(unsigned int i = 0; attrs[i]; i += 2) {
      if(strcmp(attrs[i], "annotate") == 0)
        elemstyle->icon.annotate = strcmp(attrs[i + 1], "true");
      else if(strcmp(attrs[i], "src") == 0)
        elemstyle->icon.filename = josm_icon_name_adjust(attrs[i + 1]);
      else if(strcmp(attrs[i], "priority") == 0)
        elemstyle->icon.priority = parse_priority(attrs[i + 1]);
    }

    assert(!elemstyle->icon.filename.empty());

    break;
  default:
    break;
  }
}

void StyleSax::endElement(const xmlChar *name)
{
  StateMap::const_iterator it = std::find_if(tags.begin(), tags.end(), tag_find(name));

  assert(it != tags.end());
  assert(state == it->newState);

  if(unlikely(state == TagRule && styles.back()->conditions.empty())) {
    printf("Rule %zu has no conditions\n", styles.size());
    delete styles.back();
    styles.pop_back();
  }

  state = it->oldState;
}

std::vector<elemstyle_t *> josm_elemstyles_load(const char *name) {
  printf("Loading JOSM elemstyles ...\n");

  const std::string &filename = find_file(name);
  if(unlikely(filename.empty())) {
    printf("elemstyle file not found\n");
    return std::vector<elemstyle_t *>();
  }

  StyleSax sx;
  if(unlikely(!sx.parse(filename)))
    fprintf(stderr, "error parsing elemstyles\n");

  return sx.styles;
}

/* ----------------------- cleaning up --------------------- */

static void free_condition(elemstyle_condition_t &cond) {
  free(cond.key);
  if(!cond.isBool)
    free(cond.value);
}

void josm_elemstyles_free(std::vector<elemstyle_t *> &elemstyles) {
  std::for_each(elemstyles.begin(), elemstyles.end(), std::default_delete<elemstyle_t>());
  elemstyles.clear();
}

#define WIDTH_SCALE (1.0)

bool elemstyle_condition_t::matches(const base_object_t &obj) const {
  if(key) {
    const char *v = obj.tags.get_value(key);
    if(isBool) {
      if(v) {
         const char **value_strings = boolValue ? true_values : false_values;
         return parse_boolean(v, value_strings);
      } else {
        return false;
      }
    } else {
      if(!v || (value && strcasecmp(v, value) != 0))
        return false;
    }
  }
  return true;
}

struct condition_not_matches_obj {
  const base_object_t &obj;
  explicit condition_not_matches_obj(const base_object_t *o) : obj(*o) {}
  bool operator()(const elemstyle_condition_t &cond) {
    return !cond.matches(obj);
  }
};

static void node_icon_unref(style_t *style, const node_t *node, icon_t &icons) {
  style_t::IconCache::iterator it = style->node_icons.find(node->id);
  if(it != style->node_icons.end()) {
    icons.icon_free(it->second);
    style->node_icons.erase(it);
  }
}

struct colorize_node {
  style_t * const style;
  node_t * const node;
  icon_t &icons;
  bool &somematch;
  int priority;
  colorize_node(style_t *s, node_t *n, bool &m, icon_t &i)
    : style(s), node(n), icons(i), somematch(m)
    , priority(std::numeric_limits<typeof(priority)>::min()) {}
  void operator()(const elemstyle_t *elemstyle);
};

void colorize_node::operator()(const elemstyle_t *elemstyle)
{
  if(elemstyle->icon.filename.empty())
    return;

  if(priority >= elemstyle->icon.priority)
    return;

  // if any condition mismatches->rule mismatches
  if(std::find_if(elemstyle->conditions.begin(),
                  elemstyle->conditions.end(),
                  condition_not_matches_obj(node)) != elemstyle->conditions.end())
    return;

  somematch = true;

  assert(style->icon.path_prefix != nullptr);
  std::string name = "styles/";
  name += style->icon.path_prefix;
  // the final size is now known, avoid too big allocations
  name.reserve(name.size() + 1 + elemstyle->icon.filename.size());
  name += '/';
  name += elemstyle->icon.filename;

  icon_t::icon_item *buf = icons.load(name);

  /* Free old icon if there's one present, but only after loading (not
   * assigning!) the new one. In case the old and new icon are the same
   * this ensures it still is in the icon cache if this is the only user,
   * avoiding needless image processing. */
  node_icon_unref(style, node, icons);

  if(buf)
    style->node_icons[node->id] = buf;

  if (elemstyle->zoom_max > 0)
    node->zoom_max = elemstyle->zoom_max;

  priority = elemstyle->icon.priority;
}

void josm_elemstyles_colorize_node(style_t *style, node_t *node) {
  node->zoom_max = style->node.zoom_max;

  bool somematch = false;
  icon_t &icons = icon_t::instance();
  if(style->icon.enable) {
    colorize_node fc(style, node, somematch, icons);
    std::for_each(style->elemstyles.begin(), style->elemstyles.end(), fc);
  }

  /* clear icon for node if not matched at least one rule and has an icon attached */
  if(!somematch)
    node_icon_unref(style, node, icons);
}

struct josm_elemstyles_colorize_node_functor {
  style_t * const style;
  explicit josm_elemstyles_colorize_node_functor(style_t *s) : style(s) {}
  void operator()(std::pair<item_id_t, node_t *> pair) {
    josm_elemstyles_colorize_node(style, pair.second);
  }
};

static int line_mod_apply_width(int width, const elemstyle_width_mod_t *mod) {
  switch(mod->mod) {
  case ES_MOD_NONE:
  default:
    return width;

  case ES_MOD_ADD:
    return width + mod->width;

  case ES_MOD_SUB:
    return std::max(width - mod->width, 1);

  case ES_MOD_PERCENT:
    return 100 * width / mod->width;
  }
}

struct josm_elemstyles_colorize_way_functor {
  const style_t * const style;
  explicit josm_elemstyles_colorize_way_functor(const style_t *s) : style(s) {}
  void operator()(way_t *way);
  void operator()(std::pair<item_id_t, way_t *> pair) {
    operator()(pair.second);
  }

  struct apply_condition {
    const style_t * const style;
    way_t * const way;
    /* during the elemstyle search a line_mod may be found. save it here */
    const elemstyle_line_mod_t **line_mod;
    bool way_processed;
    bool way_is_closed;
    apply_condition(const style_t *s, way_t *w, const elemstyle_line_mod_t **l)
      : style(s), way(w), line_mod(l), way_processed(false)
      , way_is_closed(way->is_closed()) {}
    void operator()(const elemstyle_t *elemstyle);
  };
};

void josm_elemstyles_colorize_way_functor::apply_condition::operator()(const elemstyle_t* elemstyle)
{
  /* this entry does not contain line or area descriptions and is */
  /* likely just an icon. ignore this as it doesn't make much sense */
  /* for a way */
  if(elemstyle->type == ES_TYPE_NONE)
    return;

  if(std::find_if(elemstyle->conditions.begin(),
                  elemstyle->conditions.end(),
                  condition_not_matches_obj(way)) != elemstyle->conditions.end())
    return;

  if(elemstyle->type & ES_TYPE_LINE_MOD) {
    /* just save the fact that a line mod was found for later */
    *line_mod = &elemstyle->line_mod;
  }

  if(way_processed)
    return;

  if(!way_is_closed && elemstyle->type & ES_TYPE_LINE) {
    way->draw.color = elemstyle->line->color;
    way->draw.width =  WIDTH_SCALE * elemstyle->line->width;
    if(elemstyle->line->bg.valid) {
      way->draw.flags |= OSM_DRAW_FLAG_BG;
      way->draw.bg.color = elemstyle->line->bg.color;
      way->draw.bg.width =  WIDTH_SCALE * elemstyle->line->bg.width;
    }
    if (elemstyle->zoom_max > 0)
      way->zoom_max = elemstyle->zoom_max;
    else
      way->zoom_max = style->way.zoom_max;

    way->draw.dash_length_on = elemstyle->line->dash_length_on;
    way->draw.dash_length_off = elemstyle->line->dash_length_off;
    way_processed = true;
  } else if(way_is_closed && elemstyle->type & ES_TYPE_AREA) {
    way->draw.flags |= OSM_DRAW_FLAG_AREA;
    /* comment the following line for grey border around all areas */
    /* (potlatch style) */

    if(style->area.has_border_color)
      way->draw.color = style->area.border_color;
    else
      way->draw.color = elemstyle->area.color;

    way->draw.width =  WIDTH_SCALE * style->area.border_width;
    /* apply area alpha */
    way->draw.area.color =
    RGBA_COMBINE(elemstyle->area.color, style->area.color);
    if (elemstyle->zoom_max > 0)
      way->zoom_max = elemstyle->zoom_max;
    else
      way->zoom_max = style->area.zoom_max;

    way_processed = true;
  }
}

void josm_elemstyles_colorize_way_functor::operator()(way_t *way) {
  /* use dark grey/no stroke/not filled for everything unknown */
  way->draw.color = style->way.color;
  way->draw.width = style->way.width;
  way->draw.flags = 0;
  way->zoom_max = 0;   // draw at all zoom levels

  /* during the elemstyle search a line_mod may be found. save it here */
  const elemstyle_line_mod_t *line_mod = nullptr;
  apply_condition fc(style, way, &line_mod);

  std::for_each(style->elemstyles.begin(), style->elemstyles.end(), fc);

  // If this is an area the previous run has done the area style. Run again
  // for the line style of the outer way.
  if(fc.way_is_closed) {
    fc.way_processed = false;
    fc.way_is_closed = false;
    std::for_each(style->elemstyles.begin(), style->elemstyles.end(), fc);
  }

  /* apply the last line mod entry that has been found during search */
  if(line_mod) {
    printf("applying last matching line mod to way #" ITEM_ID_FORMAT "\n",
	   way->id);
    way->draw.width = line_mod_apply_width(way->draw.width, &line_mod->line);

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

    way->draw.bg.width = line_mod_apply_width(way->draw.bg.width, &line_mod->bg);
    if(line_mod->color != 0)
      way->draw.color = line_mod->color;
  }
}

void josm_elemstyles_colorize_way(const style_t *style, way_t *way) {
  josm_elemstyles_colorize_way_functor f(style);
  f(way);
}

void josm_elemstyles_colorize_world(style_t *styles, osm_t::ref osm) {
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

  if(type & ES_TYPE_LINE)
    delete line;
}

// vim:et:ts=8:sw=2:sts=2:ai
