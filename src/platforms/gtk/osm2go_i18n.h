/*
 * Copyright (C) 2017 Rolf Eike Beer <eike@sf-mail.de>.
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

#pragma once

#include <cassert>
#include <locale.h>
#include <libintl.h>
#include <string>

#include <osm2go_stl.h>

typedef char gchar;

#define _(String) gettext(String)

class trstring : private std::string {
#if __cplusplus >= 201103L
  explicit inline trstring(std::string &&s) : std::string(std::move(s)) {}
#else
  explicit inline trstring(const std::string &s) : std::string(s) {}
#endif
public:
  class native_type {
    const char *value;
  public:
#if __cplusplus >= 201103L
    // catch if one passes a constant nullptr as argument
    native_type(std::nullptr_t) = delete;
    native_type(native_type &&) = default;
    native_type &operator=(native_type &&) = default;
#endif
    native_type(const native_type &other) : value(other.value) {}
    native_type &operator=(const native_type &other) { value = other.value; return *this; }
    inline native_type(const char *v = nullptr) : value(v) {}
    inline bool isEmpty() const { return value == nullptr; }
    inline void clear() { value = nullptr; }
    inline operator const char *() const { return value; }
    inline std::string toStdString() const { return isEmpty() ? std::string() : value; }
  };
  typedef native_type native_type_arg;
#undef TRSTRING_NATIVE_TYPE_IS_TRSTRING

  explicit inline trstring() : std::string() {}
  explicit inline trstring(const char *s) __attribute__((nonnull(2))) : std::string(gettext(s)) {}
#if __cplusplus >= 201103L
  // catch if one passes a constant nullptr as argument
  trstring(std::nullptr_t) = delete;
  trstring(std::nullptr_t, const char *, int) = delete;
  trstring arg(std::nullptr_t) = delete;
#endif
  trstring(const char *msg, const char *, int n) __attribute__((nonnull(2)));

  trstring arg(const std::string &a) const;
  trstring arg(const char *a) const __attribute__((nonnull(2)));
  inline trstring arg(char *a) const __attribute__((nonnull(2)))
  { return arg(static_cast<const char *>(a)); }
  inline trstring arg(const trstring &a) const
  { return arg(static_cast<std::string>(a)); }
  inline trstring arg(native_type a) const
  {
    assert(!a.isEmpty());
    return arg(static_cast<const char *>(a));
  }
  template<typename T> inline trstring arg(T l) const
  { return arg(std::to_string(l)); }

  const std::string &toStdString() const { return *this; }

  inline void swap(std::string &other)
  {
    std::string::swap(other);
  }

  inline bool isEmpty() const
  { return empty(); }

  // There is intentionally no c_str() here as it would too easily be used in generic code,
  // instead there is a cast to a type that tells everyone "hey, this is glib specific".
#if __cplusplus >= 201103L
  explicit
#endif
  operator const gchar *() const { return c_str(); }
};

static_assert(sizeof(trstring::native_type) <= sizeof(char*), "trstring::native_type is too big");
