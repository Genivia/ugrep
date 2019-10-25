/******************************************************************************\
* Copyright (c) 2016, Robert van Engelen, Genivia Inc. All rights reserved.    *
*                                                                              *
* Redistribution and use in source and binary forms, with or without           *
* modification, are permitted provided that the following conditions are met:  *
*                                                                              *
*   (1) Redistributions of source code must retain the above copyright notice, *
*       this list of conditions and the following disclaimer.                  *
*                                                                              *
*   (2) Redistributions in binary form must reproduce the above copyright      *
*       notice, this list of conditions and the following disclaimer in the    *
*       documentation and/or other materials provided with the distribution.   *
*                                                                              *
*   (3) The name of the author may not be used to endorse or promote products  *
*       derived from this software without specific prior written permission.  *
*                                                                              *
* THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED *
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF         *
* MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO   *
* EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,       *
* SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, *
* PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;  *
* OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,     *
* WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR      *
* OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF       *
* ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.                                   *
\******************************************************************************/

/**
@file      setop.h
@brief     RE/flex operations on STL containers and sets
@author    Robert van Engelen - engelen@genivia.com
@copyright (c) 2015-2017, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt

Usage
-----

`reflex::is_disjoint<S1,S2>(S1 s1, S2 s2)` is true if `s1` and `s2` are
disjoint sets.

`reflex::is_subset<S1,S2>(S1 s1, S2 s2)` is true if `s1` is a subset of `s2`.

`reflex::is_in_set<T,S>(T x, S s)` is true if `x` is in `s`.

`reflex::set_insert<S1,S2>(S1 s1, S2 s2)` inserts elements of `s2` into `s1`.

`reflex::set_delete<S1,S2>(S1 s1, S2 s2)` deletes elements of `s2` from `s1`.

`reflex::lazy_intersection<S1,S2>(S1 s1, S2 s2)` is a structure with an
iterator over elements that are in `s1` and in `s2`.

`reflex::lazy_union<S1,S2>(S1 s1, S2 s2)` is a structure with an iterator over
elements that are in `s1` or in `s2`.

The rationale for using `reflex::lazy_intersection` and `reflex::lazy_union` is
to save memory when the results do not need to be stored in a set, since the
elements are lazely produced by an iterator.

Example
-------

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~{.cpp}
    std::set<int> s1;
    s1.insert(1);
    assert(reflex::is_in_set(1, s1) == true);
    std::set<int> s2;
    s2.insert(1);
    s2.insert(2);
    assert(reflex::is_disjoint(s1, s2) == false);
    assert(reflex::is_subset(s1, s2) == true);
    reflex::lazy_union< std::set<int>,std::set<int> > U(s1, s2);
    for (reflex::lazy_union< std::set<int>,std::set<int> >::iterator i = U.begin(); i != U.end(); ++i)
      std::cout << *i << std::endl; // prints 1 and 2
    reflex::lazy_intersection< std::set<int>,std::set<int> > I(s1, s2);
    for (reflex::lazy_intersection< std::set<int>,std::set<int> >::iterator i = I.begin(); i != I.end(); ++i)
      std::cout << *i << std::endl; // prints 1
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

*/

#ifndef REFLEX_SETOP_H
#define REFLEX_SETOP_H

namespace reflex {

/// Check if sets `s1` and `s2` are disjoint.
template<typename S1, typename S2> 
bool is_disjoint(
    const S1& s1,
    const S2& s2)
  /// @returns true or false
{
  if (s1.empty() || s2.empty())
    return true;
  typename S1::const_iterator i1 = s1.begin();
  typename S2::const_iterator i2 = s2.begin();
  typename S1::key_compare lt = s1.key_comp();
  if (lt(*s2.rbegin(), *i1) || lt(*s1.rbegin(), *i2))
    return true;
  typename S1::const_iterator i1_end = s1.end();
  typename S2::const_iterator i2_end = s2.end();
  while (i1 != i1_end && i2 != i2_end)
  {
    if (lt(*i1, *i2))
      i1++;
    else if (lt(*i2, *i1))
      i2++;
    else
      return false;
  }
  return true;
}

/// Check if value `x` is in set `s`.
template<typename T, typename S> 
inline bool is_in_set(
    const T& x,
    const S& s)
  /// @returns true or false
{
  return s.find(x) != s.end();
}

/// Check if set `s1` is a subset of set `s2`.
template<typename S1, typename S2> 
bool is_subset(
    const S1& s1,
    const S2& s2)
  /// @returns true or false
{
  if (s1.empty())
    return true;
  if (s2.empty())
    return false;
  typename S1::const_iterator i1 = s1.begin();
  typename S2::const_iterator i2 = s2.begin();
  typename S1::key_compare lt = s1.key_comp();
  if (lt(*s2.rbegin(), *i1) || lt(*s1.rbegin(), *i2))
    return false;
  typename S1::const_iterator i1_end = s1.end();
  typename S2::const_iterator i2_end = s2.end();
  while (i1 != i1_end && i2 != i2_end)
  {
    if (lt(*i1, *i2))
      return false;
    if (!lt(*i2, *i1))
      i1++;
    i2++;
  }
  return (i1 == i1_end);
}

/// Insert set `s2` into set `s1`.
template<typename S1, typename S2>
inline void set_insert(
    S1& s1,
    const S2& s2)
{
  s1.insert(s2.begin(), s2.end());
}

/// Delete elements of set `s2` from set `s1`.
template<typename S1, typename S2>
void set_delete(
    S1& s1,
    const S2& s2)
{
  if (s1.empty() || s2.empty())
    return;
  typename S1::const_iterator i1 = s1.begin();
  typename S2::const_iterator i2 = s2.begin();
  typename S1::key_compare lt = s1.key_comp();
  if (lt(*s2.rbegin(), *i1) || lt(*s1.rbegin(), *i2))
    return;
  typename S1::const_iterator i1_end = s1.end();
  typename S2::const_iterator i2_end = s2.end();
  while (i1 != i1_end && i2 != i2_end)
  {
    if (lt(*i1, *i2))
      i1++;
    else if (lt(*i2, *i1))
      i2++;
    else
    {
      s1.erase(i1++);
      i2++;
    }
  }
}

/// Intersection of two ordered sets, with an iterator to get elements lazely.
template<typename S1, typename S2> 
struct lazy_intersection {
  /// Iterator to lazely get elements of a set intersection.
  struct iterator {
    iterator(
        const S1& s1,
        const S2& s2)
      :
        i1(s1.begin()),
        i1_end(s1.end()),
        i2(s2.begin()),
        i2_end(s2.end()),
        lt(s1.key_comp())
    {
      find();
    }
    iterator(
        const typename S1::const_iterator& i1,
        const typename S2::const_iterator& i2)
      :
        i1(i1),
        i1_end(i1),
        i2(i2),
        i2_end(i2)
    {
      find();
    }
    const typename S1::key_type operator*() const
    {
      return *i1;
    }
    iterator& operator++()
    {
      next();
      return *this;
    }
    iterator operator++(int)
    {
      iterator copy = *this;
      next();
      return copy;
    }
    bool operator==(const iterator& rhs) const
    {
      return (i1 == rhs.i1 && i2 == rhs.i2) || (i1 == i1_end && rhs.i1 == rhs.i1_end) || (i2 == i2_end && rhs.i2 == rhs.i2_end);
    }
    bool operator!=(const iterator& rhs) const
    {
      return !operator==(rhs);
    }
    void find(void)
    {
      while (i1 != i1_end && i2 != i2_end)
      {
        if (lt(*i1, *i2))
          i1++;
        else if (lt(*i2, *i1))
          i2++;
        else
          break;
      }
    }
    void next(void)
    {
      if (i1 != i1_end)
        i1++;
      find();
    }
    typename S1::const_iterator i1, i1_end;
    typename S2::const_iterator i2, i2_end;
    typename S1::key_compare lt;
  };
  typedef struct iterator const_iterator;
  lazy_intersection(
      const S1& s1,
      const S2& s2)
    :
      s1(s1),
      s2(s2)
  { }
  const_iterator begin(void) const
  {
    return const_iterator(s1, s2);
  }
  const_iterator end(void) const
  {
    return const_iterator(s1.end(), s2.end());
  }
  const S1& s1;
  const S2& s2;
};

/// Union of two ordered sets, with an iterator to get elements lazely.
template<typename S1, typename S2> 
struct lazy_union {
  /// Iterator to lazely get elements of a set union.
  struct iterator {
    iterator(
        const S1& s1,
        const S2& s2)
      :
        i1(s1.begin()),
        i1_end(s1.end()),
        i2(s2.begin()),
        i2_end(s2.end()),
        lt(s1.key_comp())
    {
      find();
    }
    iterator(
        const typename S1::const_iterator& i1,
        const typename S2::const_iterator& i2)
      :
        i1(i1),
        i1_end(i1),
        i2(i2),
        i2_end(i2)
    {
      find();
    }
    const typename S1::key_type operator*() const
    {
      return second ? *i2 : *i1;
    }
    iterator& operator++()
    {
      next();
      return *this;
    }
    iterator operator++(int)
    {
      iterator copy = *this;
      next();
      return copy;
    }
    bool operator==(const iterator& rhs) const
    {
      return (i1 == rhs.i1 && i2 == rhs.i2) || (i1 == i1_end && rhs.i1 == rhs.i1_end && i2 == i2_end && rhs.i2 == rhs.i2_end);
    }
    bool operator!=(const iterator& rhs) const
    {
      return !operator==(rhs);
    }
    void find(void)
    {
      if (i1 == i1_end)
      {
        second = true;
      }
      else
      {
        while (i2 != i2_end && !lt(*i1, *i2) && !lt(*i2, *i1))
          ++i2;
      }
    }
    void next(void)
    {
      if (i1 == i1_end)
      {
        if (i2 != i2_end)
          ++i2;
        second = true;
      }
      else if (i2 == i2_end)
      { 
        if (i1 != i1_end)
          ++i1;
        second = false;
      }
      else
      {
        if (second)
        {
          ++i2;
          second = false;
        }
        else
        {
          ++i1;
          second = true;
        }
        while (i1 != i1_end && i2 != i2_end)
        {
          if (lt(*i1, *i2))
          {
            second = false;
            break;
          }
          else if (lt(*i2, *i1))
          {
            second = true;
            break;
          }
          else
          {
            ++i2;
          }
        }
      }
    }
    typename S1::const_iterator i1, i1_end;
    typename S2::const_iterator i2, i2_end;
    typename S1::key_compare lt;
    bool second;
  };
  typedef struct iterator const_iterator;
  lazy_union(
      const S1& s1,
      const S2& s2)
    :
      s1(s1),
      s2(s2)
  { }
  const_iterator begin(void) const
  {
    return const_iterator(s1, s2);
  }
  const_iterator end(void) const
  {
    return const_iterator(s1.end(), s2.end());
  }
  const S1& s1;
  const S2& s2;
};

} // namespace reflex

#endif
