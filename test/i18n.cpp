#include <osm2go_i18n.h>

#include <osm.h>

#include <osm2go_annotations.h>
#include <osm2go_cpp.h>

#include <iostream>

#ifndef PACKAGE
#define PACKAGE "osm2go"
#endif

int main()
{
  trstring foo = trstring("abc %1 def %2 ghi").arg("nkw").arg(1);
  assert_cmpstr(foo, "abc nkw def 1 ghi");

  // using the same placeholder more than once must work
  foo = trstring("abc %1 def %1 ghi").arg("nkw");
  assert_cmpstr(foo, _("abc nkw def nkw ghi"));

  // and passing additional arguments must not change anything (but may print warnings to the console)
  foo = trstring("abc %1 def %1 ghi").arg("nkw").arg(1);
  assert_cmpstr(foo, _("abc nkw def nkw ghi"));

  foo = trstring("abc %1 def %2 ghi %3").arg("3.14").arg("nkw");
  assert_cmpstr(foo, "abc 3.14 def nkw ghi %3");

  foo = trstring("abc %1 def %2 ghi %3").arg(3).arg("nkw");
  assert_cmpstr(foo, "abc 3 def nkw ghi %3");

  foo = trstring("%1 %n %2", nullptr, 2).arg("a").arg("b");
  assert_cmpstr(foo, "a 2 b");

  foo = trstring("area %1 km²").arg(5.0, 0, 'f', 2);
  assert_cmpstr(foo, "area 5.00 km²");

  foo = trstring("%1 %2", nullptr, 2).arg("a").arg("b");
  assert_cmpstr(foo, "a b");

  item_id_t id = 0x100000011LL;
  foo = trstring("id: %1#").arg(id);
  assert_cmpstr(foo, "id: 4294967313#");

  foo = trstring("Retry %1/%2 ").arg(1).arg(3);
  assert_cmpstr(foo, "Retry 1/3 ");

  foo = trstring("Log generated by %1 v%2 using API 0.6\n").arg(PACKAGE).arg(VERSION);
  assert_cmpstr(foo, "Log generated by " PACKAGE " v" VERSION " using API 0.6\n");

  foo = trstring("%1: member in %2 '%3'").arg("way").arg("boundary").arg("Wennigsen");
  assert_cmpstr(foo, "way: member in boundary 'Wennigsen'");

  return 0;
}
