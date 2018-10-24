#include <project.h>
#include <project_p.h>

#include <appdata.h>
#include <fdguard.h>
#include <gps_state.h>
#include <icon.h>
#include <iconbar.h>
#include <josm_presets.h>
#include <map.h>
#include <project.h>
#include <style.h>
#include <uicontrol.h>

#include <cassert>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <memory>
#include <sys/stat.h>
#include <unistd.h>

#include <osm2go_annotations.h>
#include <osm2go_cpp.h>
#include <osm2go_platform.h>

appdata_t::appdata_t(map_state_t &mstate)
  : uicontrol(nullptr)
  , map_state(mstate)
  , map(nullptr)
  , icons(icon_t::instance())
{
}

appdata_t::~appdata_t()
{
}

static const char *proj_name = "test_proj";

static void testNoFiles(const std::string &tmpdir)
{
  map_state_t dummystate;

  appdata_t appdata(dummystate);
  appdata.project.reset(new project_t(dummystate, proj_name, tmpdir));

  assert(!track_restore(appdata));
  assert(!appdata.track.track);

  const std::string pfile = tmpdir + '/' + std::string(proj_name) + ".proj";
  assert(!project_read(pfile, appdata.project, std::string(), -1));

  int fd = open(pfile.c_str(), O_WRONLY | O_CREAT, 0644);
  assert(fd >= 0);
  const char *xml_minimal = "<a><b/></a>";
  write(fd, xml_minimal, strlen(xml_minimal));
  close(fd);
  fdguard empty(pfile.c_str(), O_RDONLY);
  assert(empty.valid());

  assert(!project_read(pfile, appdata.project, std::string(), -1));

  unlink(pfile.c_str());
}

static void testSave(const std::string &tmpdir, const char *empty_proj)
{
  map_state_t dummystate;
  std::unique_ptr<project_t> project(new project_t(dummystate, proj_name, tmpdir));

  assert(project->save());

  const std::string &pfile = project_filename(*project);

  osm2go_platform::MappedFile empty(empty_proj);
  assert(empty);
  osm2go_platform::MappedFile proj(pfile.c_str());
  assert(proj);

  assert_cmpmem(empty.data(), empty.length(), proj.data(), proj.length());

  project_delete(project.release());
}

static void testNoData(const std::string &tmpdir)
{
  map_state_t dummystate;
  std::unique_ptr<project_t> project(new project_t(dummystate, proj_name, tmpdir));

  assert(project->save());

  const std::string &pfile = project_filename(*project);
  project_read(pfile, project, project->server(std::string()), -1);

  const std::string &ofile = project->osmFile;

  int osmfd = openat(project->dirfd, ofile.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0644);
  assert_cmpnum_op(osmfd, >=, 0);

  bool b = project->parse_osm();
  assert(!b);

  const char *not_gzip = "<?xml version='1.0' encoding='UTF-8'?>\n<osm/>";
  write(osmfd, not_gzip, strlen(not_gzip));
  close(osmfd);

  assert(!project->check_demo(nullptr));
  assert(project->osm_file_exists());
  b = project->parse_osm();
  assert(!b);

  // add an empty directories to see if project_delete() also cleans those
  assert_cmpnum(mkdir((project->path + ".foo").c_str(), 0755), 0);
  assert_cmpnum(mkdirat(project->dirfd, ".bar", 0755), 0);

  project_delete(project.release());
}

static void testServer(const std::string &tmpdir)
{
  map_state_t dummystate;
  const std::string defaultserver = "https://api.openstreetmap.org/api/0.6";
  const std::string oldserver = "http://api.openstreetmap.org/api/0.5";
  project_t project(dummystate, proj_name, tmpdir);

  assert_cmpstr(project.server(defaultserver), defaultserver.c_str());
  assert_cmpstr(project.server(oldserver), oldserver.c_str());
  assert(project.rserver.empty());

  project.adjustServer(defaultserver.c_str(), defaultserver);
  assert(project.rserver.empty());

  project.adjustServer(oldserver.c_str(), defaultserver);
  assert(!project.rserver.empty());
  assert_cmpstr(project.server(defaultserver), oldserver.c_str());

  project.adjustServer(nullptr, defaultserver);
  assert(project.rserver.empty());
  assert_cmpstr(project.server(defaultserver), defaultserver.c_str());

  project.adjustServer(oldserver.c_str(), defaultserver);
  assert(!project.rserver.empty());
  assert_cmpstr(project.server(defaultserver), oldserver.c_str());

  project.adjustServer("", defaultserver);
  assert(project.rserver.empty());
  assert_cmpstr(project.server(defaultserver), defaultserver.c_str());
}

int main(int argc, char **argv)
{
  xmlInitParser();

  if(argc != 2)
    return 1;

  char tmpdir[] = "/tmp/osm2go-project-XXXXXX";

  if(mkdtemp(tmpdir) == nullptr) {
    std::cerr << "cannot create temporary directory" << std::endl;
    return 1;
  }

  std::string osm_path = tmpdir;
  osm_path += '/';

  testNoFiles(osm_path);
  testSave(osm_path, argv[1]);
  testNoData(osm_path);
  testServer(osm_path);

  assert_cmpnum(rmdir(tmpdir), 0);

  xmlCleanupParser();

  return 0;
}

void appdata_t::main_ui_enable()
{
  assert_unreachable();
}

void appdata_t::track_clear()
{
  assert_unreachable();
}
