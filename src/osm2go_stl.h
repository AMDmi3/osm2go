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

#ifndef OSM2GO_STL_H
#define OSM2GO_STL_H

#include <osm2go_cpp.h>

// some compat code

#if __cplusplus < 201402L
namespace std {
template<typename T> typename T::const_iterator cbegin(const T &c) {
#if __cplusplus >= 201103L
  return c.cbegin();
#else
  return typename T::const_iterator(c.begin());
#endif
}
}
#endif

// gcc did not set the C++ level before it was officially released
#if __cplusplus < 201103L && !defined(_UNIQUE_PTR_H)
// taken from gcc 4.8.5, stripped down to what is actually needed
namespace std {
  template<typename T>
  struct default_delete {
    void operator()(T *p) const {
      delete p;
    }
  };

  template <typename _Tp, typename _Dp = default_delete<_Tp> >
    class unique_ptr {
    public:
      typedef _Tp                       element_type;
      typedef _Dp                       deleter_type;

      typedef std::pair<_Tp *, _Dp>  __tuple_type;
      __tuple_type                  _M_t;

      // Constructors.
      explicit unique_ptr(_Tp *__p = O2G_NULLPTR, deleter_type __d = deleter_type())
      : _M_t(__p, __d) { }

      // Destructor.
      ~unique_ptr() {
        _Tp *&__ptr = _M_t.first;
        if (__ptr != O2G_NULLPTR)
          get_deleter()(__ptr);
        __ptr = O2G_NULLPTR;
      }

      // Observers.
      element_type operator*() const {
        return *get();
      }

      _Tp *operator->() const {
        return get();
      }

      _Tp *get() const
      { return _M_t.first; }

      deleter_type& get_deleter()
      { return _M_t.second; }

      const deleter_type& get_deleter() const
      { return _M_t.first; }

      operator bool() const
      { return get() == O2G_NULLPTR ? false : true; }

      // Modifiers.
      _Tp *release() {
        _Tp *__p = get();
        _M_t.first = O2G_NULLPTR;
        return __p;
      }

      void reset(_Tp *__p = O2G_NULLPTR) {
        using std::swap;
        swap(_M_t.first, __p);
        if (__p != O2G_NULLPTR)
          get_deleter()(__p);
      }

    private:
      // Disable copy from lvalue.
      unique_ptr(const unique_ptr &);
      unique_ptr& operator=(const unique_ptr &);
  };

  template<typename _Tp>
    class unique_ptr<_Tp[]>
    {
    public:
      typedef _Tp                       element_type;

      _Tp *                  _M_t;


      // Constructors.

      /** Takes ownership of a pointer.
       *
       * @param __p  A pointer to an array of a type safely convertible
       * to an array of @c element_type
       *
       * The deleter will be value-initialized.
       */
      explicit
      unique_ptr(_Tp *__p)
      : _M_t(__p)
        { }

      /// Destructor, invokes the deleter if the stored pointer is not null.
      ~unique_ptr()
      {
        _Tp *&__ptr = _M_t;
        if (__ptr != O2G_NULLPTR)
          delete[] (__ptr);
        __ptr = O2G_NULLPTR;
      }

      // Observers.

      /// Access an element of owned array.
      const element_type &
      operator[](size_t __i) const
      {
        return get()[__i];
      }

      element_type &
      operator[](size_t __i)
      {
        return get()[__i];
      }

      /// Return the stored pointer.
      element_type *
      get()
      { return _M_t; }

      const element_type *
      get() const
      { return _M_t; }

      /// Return @c true if the stored pointer is not null.
      operator bool() const
      { return get() == O2G_NULLPTR ? false : true; }

      // Modifiers.

      /// Release ownership of any stored pointer.
      element_type *
      release()
      {
        element_type * __p = get();
        _M_t._M_ptr() = O2G_NULLPTR;
        return __p;
      }

      /** @brief Replace the stored pointer.
       *
       * @param __p  The new pointer to store.
       *
       * The deleter will be invoked if a pointer is already owned.
       */
      void
      reset(_Tp *__p = O2G_NULLPTR)
      {
        element_type *__ptr = __p;
        using std::swap;
        swap(_M_t._M_ptr(), __ptr);
        if (__ptr != O2G_NULLPTR)
          delete[] (__ptr);
      }

      /// Exchange the pointer and deleter with another object.
      void
      swap(unique_ptr& __u)
      {
        using std::swap;
        swap(_M_t, __u._M_t);
      }

    private:
      // Disable copy from lvalue.
      unique_ptr(const unique_ptr&) O2G_DELETED_FUNCTION;
      unique_ptr& operator=(const unique_ptr&) O2G_DELETED_FUNCTION;
    };
}
#endif

template<typename T> void shrink_to_fit(T &v) {
#if __cplusplus >= 201103L
  v.shrink_to_fit();
#else
  T tmp;
  tmp.resize(v.size());
  tmp = v;
  tmp.swap(v);
#endif
}

#endif // OSM2GO_STL_H
