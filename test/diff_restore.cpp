#include <appdata.h>
#include <diff.h>
#include <icon.h>
#include <map.h>
#include <misc.h>
#include <osm.h>
#include <project.h>

#include <osm2go_annotations.h>
#include <osm2go_cpp.h>

#include <cassert>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <gtk/gtk.h>
#include <iostream>
#include <sys/stat.h>

void appdata_t::track_clear()
{
  assert_unreachable();
}

static void verify_diff(osm_t *osm)
{
  assert_cmpnum(12, osm->nodes.size());
  assert_cmpnum(3, osm->ways.size());
  assert_cmpnum(4, osm->relations.size());

  // new tag added in diff
  const node_t * const n72 = osm->nodes[638499572];
  assert(n72 != O2G_NULLPTR);
  assert_cmpnum(n72->flags, OSM_FLAG_DIRTY);
  assert(n72->tags.get_value("testtag") != O2G_NULLPTR);
  assert_cmpnum(n72->tags.asMap().size(), 5);
  // in diff, but the same as in .osm
  const node_t * const n23 = osm->nodes[3577031223LL];
  assert(n23 != O2G_NULLPTR);
  assert_cmpnum(n23->flags, 0);
  assert(n23->tags.empty());
  // deleted in diff
  const node_t * const n26 = osm->nodes[3577031226LL];
  assert(n26 != O2G_NULLPTR);
  assert_cmpnum(n26->flags, OSM_FLAG_DELETED);
  const way_t * const w = osm->ways[351899455];
  assert(w != O2G_NULLPTR);
  assert((w->flags & OSM_FLAG_DELETED) != 0);
  assert_cmpnum(w->user, 53064);
  assert(osm->users.find(53064) != osm->users.end());
  assert(osm->users[53064] == "Dakon");
  // added in diff
  const node_t * const nn1 = osm->nodes[-1];
  assert(nn1 != O2G_NULLPTR);
  assert_cmpnum(nn1->pos.lat, 52.2693518);
  assert_cmpnum(nn1->pos.lon, 9.576014);
  assert(nn1->tags.empty());
  // added in diff, same position as existing node
  const node_t * const nn2 = osm->nodes[-2];
  assert(nn2 != O2G_NULLPTR);
  assert_cmpnum(nn2->pos.lat, 52.269497);
  assert_cmpnum(nn2->pos.lon, 9.5752223);
  assert(nn2->tags.empty());
  // which is this one
  const node_t * const n27 = osm->nodes[3577031227LL];
  assert(n27 != O2G_NULLPTR);
  assert_cmpnum(n27->flags, 0);
  assert_cmpnum(nn2->pos.lat, n27->pos.lat);
  assert_cmpnum(nn2->pos.lon, n27->pos.lon);
  // the upstream version has "wheelchair", we have "source"
  // our modification must survive
  const way_t * const w452 = osm->ways[351899452];
  assert(w452 != O2G_NULLPTR);
  assert(w452->tags.get_value("source") != O2G_NULLPTR);
  assert_null(w452->tags.get_value("wheelchair"));
  assert_cmpnum(w452->tags.asMap().size(), 3);
  const way_t * const w453 = osm->ways[351899453];
  assert(w453 != O2G_NULLPTR);
  assert_cmpnum(w453->flags, 0);
  const relation_t * const r66316 = osm->relations[66316];
  assert(r66316 != O2G_NULLPTR);
  assert_cmpnum(r66316->flags, OSM_FLAG_DELETED);
  const relation_t * const r255 = osm->relations[296255];
  assert(r255 != O2G_NULLPTR);
  assert_cmpnum(r255->flags, OSM_FLAG_DIRTY);
  assert_cmpnum(r255->members.size(), 164);
  const object_t r255m572(const_cast<node_t *>(n72));
  std::vector<member_t>::const_iterator r255it = r255->find_member_object(r255m572);
  r255it = r255->find_member_object(r255m572);
  assert(r255it != r255->members.end());
  assert(r255it->role != O2G_NULLPTR);
  assert_cmpstr(r255it->role, "forward_stop");
  assert_cmpnum(r255->tags.asMap().size(), 8);

  const relation_t * const r853 = osm->relations[5827853];
  assert(r853 != O2G_NULLPTR);
  assert_cmpnum(r853->flags, OSM_FLAG_DIRTY);
  for(std::vector<member_t>::const_iterator it = r853->members.begin(); it != r853->members.end(); it++)
    assert_cmpnum(it->object.type, RELATION_ID);

  assert(!diff_is_clean(osm, true));
}

static void compare_with_file(const void *buf, size_t len, const char *fn)
{
  GMappedFile *fdata = g_mapped_file_new(fn, FALSE, O2G_NULLPTR);

  assert(fdata != O2G_NULLPTR);
  assert_cmpnum(g_mapped_file_get_length(fdata), len);

  assert_cmpmem(g_mapped_file_get_contents(fdata), g_mapped_file_get_length(fdata),
                buf, len);

#if GLIB_CHECK_VERSION(2,22,0)
  g_mapped_file_unref(fdata);
#else
  g_mapped_file_free(fdata);
#endif
}

static void test_osmChange(const osm_t *osm, const char *fn)
{
  xmlDocPtr doc = osmchange_init();
  const char *changeset = "42";

  osmchange_delete(osm->modified(), xmlDocGetRootElement(doc), changeset);

  xmlChar *result;
  int len;
  xmlDocDumpFormatMemoryEnc(doc, &result, &len, "UTF-8", 1);
  xmlFreeDoc(doc);

  compare_with_file(result, len, fn);
  xmlFree(result);
}

int main(int argc, char **argv)
{
  int result = 0;

  if(argc != 4)
    return EINVAL;

  xmlInitParser();

  icon_t icons;
  const std::string osm_path = argv[1];
  assert_cmpnum(osm_path[osm_path.size() - 1], '/');

  map_state_t dummystate;
  project_t project(dummystate, argv[2], osm_path);
  project.osm = argv[2] + std::string(".osm");

  osm_t *osm = project.parse_osm(icons);
  if(!osm) {
    std::cerr << "cannot open " << argv[1] << argv[2] << ": " << strerror(errno) << std::endl;
    return 1;
  }

  assert_cmpnum(osm->uploadPolicy, osm_t::Upload_Blocked);
  assert_null(osm->sanity_check());

  const relation_t * const r255 = osm->relations[296255];
  assert(r255 != O2G_NULLPTR);
  assert_cmpnum(r255->flags, 0);
  assert_cmpnum(r255->members.size(), 165);
  assert_cmpnum(r255->tags.asMap().size(), 8);
  const node_t * const n72 = osm->nodes[638499572];
  assert_cmpnum(n72->tags.asMap().size(), 4);
  const object_t r255m572(const_cast<node_t *>(n72));
  std::vector<member_t>::const_iterator r255it = r255->find_member_object(r255m572);
  assert(r255it != r255->members.end());
  assert(r255it->role != O2G_NULLPTR);
  assert_cmpstr(r255it->role, "stop");
  const relation_t * const r66316 = osm->relations[66316];
  assert(r66316 != O2G_NULLPTR);
  object_t rmember(RELATION_ID, 296255);
  assert(!rmember.is_real());
  const std::vector<member_t>::const_iterator r66316it = r66316->find_member_object(rmember);
  assert(r66316it != r66316->members.end());
  // the child relation exists, so it should be stored as real ref
  assert(r66316it->object.is_real());

  assert_cmpnum(10, osm->nodes.size());
  assert_cmpnum(3, osm->ways.size());
  assert_cmpnum(4, osm->relations.size());

  assert(diff_is_clean(osm, true));

  assert(diff_present(&project));
  unsigned int flags = diff_restore_file(O2G_NULLPTR, &project, osm);
  assert_cmpnum(flags, DIFF_RESTORED | DIFF_HAS_HIDDEN);

  verify_diff(osm);

  xmlChar *rel_str = r255->generate_xml("42");
  printf("%s\n", rel_str);
  xmlFree(rel_str);

  rel_str = n72->generate_xml("42");
  printf("%s\n", rel_str);
  xmlFree(rel_str);

  char tmpdir[] = "/tmp/osm2go-diff_restore-XXXXXX";

  if(mkdtemp(tmpdir) == O2G_NULLPTR) {
    std::cerr << "cannot create temporary directory" << std::endl;
    result = 1;
  } else {
    std::string bpath = tmpdir + std::string("/") + argv[2];
    mkdir(bpath.c_str(), 0755);
    bpath.erase(bpath.rfind('/') + 1);
    project_t sproject(dummystate, argv[2], bpath);

    flags = diff_restore_file(O2G_NULLPTR, &sproject, osm);
    assert_cmpnum(flags, DIFF_NONE_PRESENT);

    diff_save(&sproject, osm);
    bpath += argv[2];
    std::string bdiff = bpath;
    bpath += '/';
    bpath += argv[2];
    bpath += '.';
    bpath += "diff";

    bdiff += "/backup.diff";
    assert(diff_present(&sproject));
    rename(bpath.c_str(), bdiff.c_str());
    assert(!diff_present(&sproject));

    delete osm;
    osm = osm_t::parse(project.path, project.osm, icons);
    assert(osm != O2G_NULLPTR);

    flags = diff_restore_file(O2G_NULLPTR, &sproject, osm);
    assert_cmpnum(flags, DIFF_RESTORED | DIFF_HAS_HIDDEN);

    verify_diff(osm);

    unlink(bdiff.c_str());
    bpath.erase(bpath.rfind('/'));
    rmdir(bpath.c_str());
    bpath.erase(bpath.rfind('/'));
    rmdir(bpath.c_str());
  }

  test_osmChange(osm, argv[3]);

  delete osm;

  xmlCleanupParser();

  return result;
}

void appdata_t::main_ui_enable()
{
  assert_unreachable();
}
