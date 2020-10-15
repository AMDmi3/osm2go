#include "dummy_map.h"

#include <map.h>

#include <appdata.h>
#include <gps_state.h>
#include <iconbar.h>
#include <josm_presets.h>
#include <osm.h>
#include <osm2go_annotations.h>
#include <style.h>
#include <uicontrol.h>

#include <iostream>
#include <memory>
#include <unistd.h>

void test_map::test_function()
{
  way_add_begin();
  way_add_cancel();
}

namespace {

void set_bounds(osm_t::ref o)
{
  bool b = o->bounds.init(pos_area(pos_t(52.2692786, 9.5750497), pos_t(52.2695463, 9.5755)));
  assert(b);
}

void test_map_delete()
{
  appdata_t a;
  std::unique_ptr<map_t> m(std::make_unique<test_map>(a));
}

void test_map_delete_items()
{
  appdata_t a;
  std::unique_ptr<map_t> m(std::make_unique<test_map>(a));
  std::unique_ptr<osm_t> o(std::make_unique<osm_t>());
  set_bounds(o);

  way_t *w = o->attach(new way_t());

  // keep it here, it ill only be reset, but not freed as that is done through the map
  std::unique_ptr<map_item_t> mi(new map_item_t(object_t(w), nullptr));
  w->map_item = mi.get();

  o->way_delete(w, m.get());

  lpos_t p(10, 10);
  node_t *n = o->node_new(p);
  o->attach(n);
  n->map_item = mi.get();

  o->node_delete(n);
}

void test_draw_deleted(const std::string &tmpdir)
{
  appdata_t a;
  a.project.reset(new project_t("foo", tmpdir));
  std::unique_ptr<map_t> m(std::make_unique<test_map>(a));
  m->style.reset(new style_t());
  a.project->osm.reset(new osm_t());
  osm_t::ref o = a.project->osm;
  set_bounds(o);

  lpos_t p(10, 10);
  base_attributes ba(123);
  ba.version = 1;
  node_t *n = o->node_new(p.toPos(o->bounds), ba);
  o->insert(n);
  assert(!n->isDeleted());
  assert_cmpnum(n->flags, 0);
  o->node_delete(n);
  assert(n->isDeleted());

  // deleted nodes are not drawn
  m->draw(n);

  way_t *w = new way_t(ba);
  o->insert(w);
  assert(!w->isDeleted());
  assert_cmpnum(w->flags, 0);
  o->way_delete(w, m.get());
  assert(w->isDeleted());

  // deleted ways are not drawn
  m->draw(w);
}

void test_draw_hidden(const std::string &tmpdir)
{
  appdata_t a;
  a.project.reset(new project_t("foo", tmpdir));
  std::unique_ptr<map_t> m(std::make_unique<test_map>(a));
  m->style.reset(new style_t());
  a.project->osm.reset(new osm_t());
  osm_t::ref o = a.project->osm;
  set_bounds(o);
  MainUiDummy * const ui = static_cast<MainUiDummy *>(a.uicontrol.get());

  base_attributes ba(123);
  ba.version = 1;
  way_t *w = new way_t(ba);
  o->insert(w);
  assert(!w->isDeleted());
  assert_cmpnum(w->flags, 0);

  for (int i = 0; i < 4; i++) {
    lpos_t p(10, 10 + i);
    node_t *n = o->node_new(p);
    o->attach(n);
    assert(!n->isDeleted());
    assert_cmpnum(n->flags, OSM_FLAG_DIRTY);
    w->append_node(n);
  }

  o->waySetHidden(w);
  assert(o->wayIsHidden(w));

  // hidden ways are not drawn
  m->draw(w);

  // trick the way to become unhidden bit still not drawn: also set deleted marker
  w->flags |= OSM_FLAG_DELETED;

  ui->m_actions[MainUi::MENU_ITEM_MAP_SHOW_ALL] = false;
  m->show_all();

  assert_cmpnum(o->hiddenWays.size(), 0);
  w->flags = 0;

  // delete a node from a hidden way: this should trigger a redraw, but again it's not actually drawn
  o->waySetHidden(w);
  o->node_delete(w->node_chain.front(), m.get());
}

void test_way_add_cancel(const std::string &tmpdir)
{
  appdata_t a;
  std::unique_ptr<test_map> m(std::make_unique<test_map>(a));

  a.project.reset(new project_t("foo", tmpdir));
  a.project->osm.reset(new osm_t());
  set_bounds(a.project->osm);

  m->test_function();
}

void test_map_item_deleter(const std::string &tmpdir)
{
  appdata_t a;
  a.project.reset(new project_t("foo", tmpdir));
  std::unique_ptr<map_t> m(std::make_unique<test_map>(a));
  m->style.reset(new style_t());
  a.project->osm.reset(new osm_t());
  osm_t::ref o = a.project->osm;
  set_bounds(o);

  way_t * const w = new way_t();
  o->attach(w);
  w->map_item = new map_item_t(object_t(w));

  map_item_destroyer mid(w->map_item);

  w->item_chain_destroy(m.get());

  assert_null(w->map_item);
  mid.run(nullptr);
}

} // namespace

int main()
{
  char tmpdir[] = "/tmp/osm2go-project-XXXXXX";

  if(mkdtemp(tmpdir) == nullptr) {
    std::cerr << "cannot create temporary directory" << std::endl;
    return 1;
  }

  std::string osm_path = tmpdir;
  osm_path += '/';

  test_map_delete();
  test_map_delete_items();
  test_draw_deleted(osm_path);
  test_draw_hidden(osm_path);
  test_way_add_cancel(osm_path);
  test_map_item_deleter(osm_path);

  assert_cmpnum(rmdir(tmpdir), 0);

  return 0;
}

#include "dummy_appdata.h"
