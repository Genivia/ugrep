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
@file      ranges.h
@brief     RE/flex range sets as closed and open-ended set containers
@author    Robert van Engelen - engelen@genivia.com
@copyright (c) 2016-2020, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt

Open-ended ranges are more efficient than `std::set` when the values stored are
adjacent (e.g. integers 2 and 3 are adjacent), since `std::set` stores values
individually whereas open-ended ranges merges adjacent values into ranges.
This lowers storage overhead and reduces insertion, deletion, and search time.
*/

#ifndef REFLEX_RANGES_H
#define REFLEX_RANGES_H

#include <functional> // std::less
#include <set>        // base class container

namespace reflex {

/// Functor to define a total order on ranges (intervals) represented by pairs.
template<typename T>
struct range_compare {
  /// Compares two ranges lhs and rhs and returns true if lhs < rhs.
  bool operator()(
      const std::pair<T,T>& lhs, ///< LHS range to compare
      const std::pair<T,T>& rhs) ///< RHS range to compare
    const
    /// @returns true if lhs < rhs.
  {
    return std::less<T>()(lhs.second, rhs.first);
  }
};

/// RE/flex Ranges template class.
/**
The `std::set` container is the base class of this Ranges class.  Value ranges
[lo,hi] are stored in the underlying `std::set` container as a pair of bounds
`std::pair(lo, hi)`.

Ranges in the set are mutually disjoint (i.e. non-overlapping).  This property
is maintained by the `reflex::Ranges` methods.

The `Ranges::value_type` is `std::pair<bound_type,bound_type>` with
`Ranges::bound_type` the template parameter type `T`.

The `reflexx::Ranges` class introduces several new methods in addition to the
inherited `std::set` methods:

- `std::pair<iterator,bool> insert(const bound_type& lo, const bound_type& hi)`
  updates ranges to include the range [lo,hi].  Returns an iterator to the
  range that contains [lo,hi] and a flag indicating that ranges was updated
  (true) or if the new range was subsumed by current ranges (false).

- `std::pair<iterator,bool> insert(const bound_type& val)`
  updates ranges to include the value [val,val].  Returns an iterator to the
  range that contains val and a flag indicating that ranges was updated (true)
  or if the new range was subsumed by current ranges (false).

- `const_iterator find(const bound_type& lo, const bound_type& hi) const`
  searches for the first range that overlaps with [lo,hi].  Returns an iterator
  to the range found or the end iterator.

- `const_iterator find(const bound_type& val) const` searches for the range
  that includes the given value.  Returns an iterator to the range found or the
  end iterator.

- `Ranges& operator|=(const Ranges& rs)` inserts ranges rs.  Returns reference
  to this object.

- `Ranges& operator+=(const Ranges& rs)` same as above.

- `Ranges& operator&=(const Ranges& rs)` update ranges to intersect with ranges
   rs.  Returns reference to this object.

- `Ranges operator|(const Ranges& rs) const` returns union of ranges.

- `Ranges operator+(const Ranges& rs) const` same as above.

- `Ranges operator&(const Ranges& rs) const` returns intersection of ranges.

- `bool any() const` returns true if this set of ranges contains at least one
  range, i.e. is not empty.

- `bool intersects(const Ranges& rs) const` returns true if this set of ranges
  intersects ranges rs, i.e. has at least one range that overlaps with ranges rs.

- `bool contains(const Ranges& rs) const` returns true if this set of ranges
  contains all ranges rs, i.e. ranges rs is a subset.

- `bound_type lo()` returns the lowest value in the set of ranges.

- `bound_type hi()` returns the highest value in the set of ranges.

@warning Using `std::set::insert()` instead of `Ranges::insert()` may result in
overlapping ranges rather than merging ranges to produce disjoint
non-overlapping ranges.

Example:

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~{.cpp}
    reflex::Ranges<float> intervals;
    intervals.insert(1.0, 2.0);  // insert  1.0..2.0
    intervals.insert(2.0, 3.0);  // insert  2.0..3.0
    intervals.insert(-1.0, 0.0); // insert -1.0..0.0
    std::cout << "Set of " << intervals.size() << " intervals:" << std::endl;
    for (reflex::Ranges<float>::const_iterator i = intervals.begin(); i != intervals.end(); ++i)
      std::cout << "[" << i->first << "," << i->second << "]" << std::endl;
    if (intervals.find(2.5) != intervals.end())
      std::cout << "2.5 is in intervals" << std::endl;
    for (reflex::Ranges<float>::const_iterator i = intervals.find(0.0, 1.0); i != intervals.end() && i->first <= 1.0; ++i)
      std::cout <<  "[" << i->first << "," << i->second << "] overlaps with [0.0,1.0]" << std::endl;
    if (intervals.intersects(reflex::Ranges<float>(2.5, 10.0)))
      std::cout << "intersects [2.5,10.0]" << std::endl;
    if (intervals.contains(reflex::Ranges<float>(1.0, 2.5)))
      std::cout << "contains [1.0,2.5]" << std::endl;
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Output:

    Set of 2 intervals:
    [-1,0]
    [1,3]
    2.5 is in intervals
    [-1,0] overlaps with [0.0,1.0]
    [1,3] overlaps with [0.0,1.0]
    intersects [2.5,10.0]
    contains [1.0,2.5]

*/
template<typename T>
class Ranges : public std::set< std::pair<T,T>,range_compare<T> > {
 public:
  /// Type of the bounds.
  typedef T bound_type;
  /// Synonym type defining the base class container std::set.
  typedef typename std::set< std::pair<T,T>,range_compare<T> > container_type;
  /// Synonym type defining the base class container std::set::value_type.
  typedef typename container_type::value_type value_type;
  /// Synonym type defining the key/value comparison std::set::key_compare.
  typedef typename container_type::key_compare key_compare;
  typedef typename container_type::value_compare value_compare;
  /// Synonym type defining the base class container std::set::iterator.
  typedef typename container_type::iterator iterator;
  /// Synonym type defining the base class container std::set::iterator.
  typedef typename container_type::const_iterator const_iterator;
  /// Construct an empty range.
  Ranges()
  { }
  /// Construct a copy of a range [lo,hi].
  Ranges(const value_type& r)
  {
    insert(r);
  }
  /// Construct a range [lo,hi].
  Ranges(
      const bound_type& lo, ///< lower bound
      const bound_type& hi) ///< upper bound
  {
    insert(lo, hi);
  }
  /// Construct a singleton range [val,val].
  Ranges(const bound_type& val) ///< value
  {
    insert(val, val);
  }
  /// Update ranges to include range [lo,hi] by merging overlapping ranges into one range.
  std::pair<iterator,bool> insert(const value_type& r) ///< range
    /// @returns a pair of an iterator to the range and a flag indicating whether the range was inserted as new.
  {
    return insert(r.first, r.second);
  }
  /// Update ranges to include range [lo,hi] by merging overlapping ranges into one range.
  std::pair<iterator,bool> insert(
      const bound_type& lo, ///< lower bound
      const bound_type& hi) ///< upper bound
    /// @returns a pair of an iterator to the range and a flag indicating whether the range was inserted as new.
  {
    // r = [lo,hi]
    value_type r(lo, hi);
    iterator i = container_type::find(r);
    // if [lo,hi] does not overlap any range in the set then insert [lo,hi]
    if (i == container_type::end())
      return container_type::insert(r);
    // if [lo,hi] is subsumed by ranges in the set then return without inserting
    if (!std::less<bound_type>()(lo, i->first) && !std::less<bound_type>()(i->second, hi))
      return std::pair<iterator,bool>(i, false);
    // while merging [lo,hi] overlaps with range(s) in the set, erase ranges and adjust [lo,hi]
    do
    {
      if (std::less<bound_type>()(i->first, r.first)) // lo = min(lo, i.lo)
        r.first = i->first;
      if (std::less<bound_type>()(r.second, i->second)) // hi = max(hi, i.hi)
        r.second = i->second;
      container_type::erase(i++); // remove old range [i.lo,i.hi]
    }
    while (i != this->end() && !std::less<bound_type>()(hi, i->first));
    // insert merged range [lo,hi]
    return std::pair<iterator,bool>(container_type::insert(i, r), true); // insert with hint i that follows the insertion point
  }
  /// Update ranges to include the range [val,val].
  std::pair<iterator,bool> insert(const bound_type& val) ///< value to insert
    /// @returns a pair of an iterator to the range and a flag indicating whether the range was inserted as new.
  {
    return insert(val, val);
  }
  /// Find the first range [lo',hi'] that overlaps the given range [lo,hi], i.e. lo <= hi' and lo' <= hi.
  const_iterator find(
      const bound_type& lo, ///< lower bound
      const bound_type& hi) ///< upper bound
    const
    /// @returns iterator to the first range that overlaps the given range, or the end iterator.
  {
    return container_type::find(value_type(lo, hi));
  }
  /// Find the range [lo',hi'] that includes the given value val, i.e. lo' <= val <= hi'.
  const_iterator find(const bound_type& val) ///< value to search for
    const
    /// @returns iterator to the range that includes the value, or the end iterator.
  {
    return find(val, val);
  }
  /// Update ranges to insert the given range set, where this method has lower complexity than iterating insert() for each range in rs.
  Ranges& operator|=(const Ranges& rs) ///< ranges to insert
    /// @returns reference to this object.
  {
    iterator i = this->begin();
    const_iterator j = rs.begin();
    while (i != this->end() && j != rs.end())
    {
      if (key_compare()(*i, *j))
      {
        ++i;
      }
      else if (key_compare()(*j, *i))
      {
        container_type::insert(i, *j++); // insert with hint i that follows the insertion point
      }
      else if (!std::less<bound_type>()(j->first, i->first) && !std::less<bound_type>()(i->second, j->second))
      {
        ++j;
      }
      else
      {
        // r = [lo,hi]
        value_type r(j->first, j->second);
        // while [lo,hi] overlaps with range(s) in the set, erase ranges and adjust [lo,hi]
        do
        {
          if (std::less<bound_type>()(i->first, r.first)) // lo = min(i.lo, lo)
            r.first = i->first;
          if (std::less<bound_type>()(r.second, i->second)) // hi = max(hi, i.hi)
            r.second = i->second;
          container_type::erase(i++); // remove old range [i.lo,i.hi]
        }
        while (i != this->end() && !std::less<bound_type>()(j->second, i->first));
        // insert merged range [lo,hi]
        i = container_type::insert(i, r); // insert with hint i that follows the insertion point
        ++j;
      }
    }
    // add ranges that are in rs that are not in this range set
    while (j != rs.end())
      container_type::insert(this->end(), *j++); // insert with hint i == end()
    return *this;
  }
  /// Update ranges to insert the ranges of the given range set, same as Ranges::operator|=(rs).
  Ranges& operator+=(const Ranges& rs) ///< ranges to insert
    /// @returns reference to this object.
  {
    return operator|=(rs);
  }
  /// Update ranges to intersect the ranges with the given range set.
  Ranges& operator&=(const Ranges& rs) ///< ranges to intersect
    /// @returns reference to this object.
  {
    iterator i = this->begin();
    const_iterator j = rs.begin();
    while (i != this->end() && j != rs.end())
    {
      // remove ranges from this range set that are not in rs
      if (key_compare()(*i, *j))
      {
        container_type::erase(i++);
      }
      else if (key_compare()(*j, *i))
      {
        ++j;
      }
      else if (std::less<bound_type>()(j->second, i->second))
      {
        bound_type hi = i->second;
        if (std::less<bound_type>()(i->first, j->first))
        {
          container_type::erase(i++);
          container_type::insert(i, *j); // insert with hint i that follows the insertion point
        }
        else
        {
          value_type r(i->first, j->second);
          container_type::erase(i++);
          container_type::insert(i, r); // insert with hint i that follows the insertion point
        }
        // put upper part of the range back if next range in rs overlaps with it
        if (++j != rs.end())
          if (std::less<bound_type>()(j->first, hi))
            i = container_type::insert(i, value_type(j->first, hi)); // insert with hint i that follows the insertion point
      }
      else if (std::less<bound_type>()(i->second, j->second))
      {
        if (std::less<bound_type>()(i->first, j->first))
        {
          value_type r(j->first, i->second);
          container_type::erase(i++);
          container_type::insert(i, r); // insert with hint i that follows the insertion point
        }
        else
        {
          ++i;
        }
      }
      else if (std::less<bound_type>()(i->first, j->first))
      {
        container_type::erase(i++);
        container_type::insert(i, *j++); // insert with hint i that follows the insertion point
      }
      else
      {
        ++i;
        ++j;
      }
    }
    // remove ranges from this range set that are not in rs
    while (i != this->end())
      container_type::erase(i++);
    return *this;
  }
  /// Returns the union of two range sets.
  Ranges operator|(const Ranges& rs) ///< ranges to merge
    const
    /// @returns the union of this set and rs.
  {
    return Ranges(*this) |= rs;
  }
  /// Returns the union of two range sets, same as Ranges::operator|(rs).
  Ranges operator+(const Ranges& rs) ///< ranges to merge
    const
    /// @returns the union of this set and rs.
  {
    return Ranges(*this) |= rs;
  }
  /// Returns the intersection of two range sets.
  Ranges operator&(const Ranges& rs) ///< range set to intersect
    const
    /// @returns the intersection of this range set and rs.
  {
    const_iterator i = this->begin();
    const_iterator j = rs.begin();
    Ranges r;
    iterator k = r.end();
    while (i != this->end() && j != rs.end())
    {
      if (key_compare()(*i, *j))
      {
        ++i;
      }
      else if (key_compare()(*j, *i))
      {
        ++j;
      }
      else if (std::less<bound_type>()(i->second, j->second))
      {
        if (std::less<bound_type>()(i->first, j->first))
          k = r.container_type::insert(k, value_type(j->first, i->second));
        else
          k = r.container_type::insert(k, *i);
        ++i;
      }
      else if (std::less<bound_type>()(j->second, i->second))
      {
        if (std::less<bound_type>()(i->first, j->first))
          k = r.container_type::insert(k, *j);
        else
          k = r.container_type::insert(k, value_type(i->first, j->second));
        ++j;
      }
      else if (std::less<bound_type>()(i->first, j->first))
      {
        k = r.container_type::insert(k, *j++);
        ++i;
      }
      else
      {
        k = r.container_type::insert(k, *i++);
        ++j;
      }
    }
    return r;
  }
  /// True if this range set is lexicographically less than range set rs.
  bool operator<(const Ranges& rs) ///< ranges
    const
    /// @returns true if this range set is less than rs.
  {
    const_iterator i = this->begin();
    const_iterator j = rs.begin();
    while (i != this->end() && j != rs.end())
    {
      if (std::less<bound_type>()(i->first, j->first))
        return true;
      if (std::less<bound_type>()(j->first, i->first))
        return false;
      if (std::less<bound_type>()(i->second, j->second))
        return true;
      if (std::less<bound_type>()(j->second, i->second))
        return false;
      ++i;
      ++j;
    }
    return false;
  }
  /// True if this range set is lexicographically greater than range set rs.
  bool operator>(const Ranges& rs) ///< ranges
    const
    /// @returns true if this range set is greater than rs.
  {
    return rs.operator<(*this);
  }
  /// True if this range set is lexicographically less or equal to range set rs.
  bool operator<=(const Ranges& rs) ///< ranges
    const
    /// @returns true if this rnage set is less or equal to rs.
  {
    return !operator>(rs);
  }
  /// True if this range set is lexicographically greater or equal to range set rs.
  bool operator>=(const Ranges& rs) ///< ranges
    const
    /// @returns true if this is greater or equal to rs.
  {
    return !operator<(rs);
  }
  /// Return true if this set of ranges contains at least one range, i.e. is not empty.
  bool any() const
    /// @returns true if non empty, false if empty.
  {
    return !container_type::empty();
  }
  /// Return true if this set of ranges intersects with ranges rs, i.e. this set has at least one range [lo',hi'] that overlaps with a range [lo,hi] in rs such that lo <= hi' and lo' <= hi.
  bool intersects(const Ranges& rs) ///< ranges
    const
    /// @returns true if this set intersects rs.
  {
    const_iterator i = this->begin();
    const_iterator j = rs.begin();
    while (i != this->end() && j != rs.end())
    {
      if (key_compare()(*i, *j))
        ++i;
      else if (key_compare()(*j, *i))
        ++j;
      else
        return true;
    }
    return false;
  }
  /// Return true if this set of ranges contains all ranges in rs, i.e. rs is a subset of this set which means that for each range [lo,hi] in rs, there is a range [lo',hi'] such that lo' <= lo and hi <= hi'.
  bool contains(const Ranges& rs) ///< ranges
    const
    /// @returns true if this set contains rs.
  {
    const_iterator i = this->begin();
    const_iterator j = rs.begin();
    while (i != this->end() && j != rs.end())
    {
      if (key_compare()(*i, *j))
      {
        ++i;
      }
      else
      {
        if (key_compare()(*j, *i) || std::less<bound_type>()(j->first, i->first) || std::less<bound_type>()(i->second, j->second))
          return false;
        ++j;
      }
    }
    return true;
  }
  /// Return the lowest value in the set of ranges (the set cannot be empty)
  bound_type lo() const
    /// @returns lowest value
  {
    return this->begin()->first;
  }
  /// Return the highest value in the set of ranges (the set cannot be empty)
  bound_type hi() const
    /// @returns highest value
  {
    return this->rbegin()->second;
  }
};

/// RE/flex ORanges (open-ended, ordinal value range) template class.
/**
The ORanges class is an optimization of the ranges class for ordinal types,
i.e. types with the property that values can be counted (enumerable, e.g.
integers and enumerations).

The optimization merges adjacent ranges.  Two ranges `[a,b]` and `[c,d]` are
adjacent when `b+1=c`.  It is safe to merge adjacent ranges over values of an
ordinal type, because `[a,b](+)[b+1,c]=[a,c]` with `(+)` representing range
merging (set union).

By storing open-ended ranges `[lo,hi+1)` in the ranges class container,
adjacent ranges are merged automatically by the fact that the bounds of
open-ended adjacent ranges overlap.

In addition to the methods inherited from the Range base class, open-ended
ranges can be updated by deleting ranges from the set with:

- `bool erase(const bound_type& lo, const bound_type& hi)` erases a range from
  this set of open-ended ranges.  Returns true if the set was updated.

- `bool erase(const bound_type& val)` erases a value from this set of
  open-ended ranges.  Returns true if the set was updated.

- `ORanges& operator-=(const ORanges& rs)` erases ranges rs from this set of
  open-ended ranges.  Returns reference to this object.

- `ORanges operator-(const ORanges& rs) const` returns difference, i.e. ranges
  rs erased from this set of open-ended ranges.

Open-ended ranges are more efficient than `std::set` when the values stored are
adjacent (e.g. integers 2 and 3 are adjacent), since `std::set` stores values
individually whereas open-ended ranges merges adjacent values into ranges.
This lowers storage overhead and reduces insertion, deletion, and search time.

We can iterate over open-ended ranges.  The iterator dereferences values are
`[lo,hi+1)` pairs, i.e. `lo = i->first` and `hi = i->second - 1`.

Note that the largest value that can be stored in an open-ended range is the
maximum representable value minus 1.  For example, `reflex::ORanges<char>`
holds values -128 to 126 and excludes 127.  The macro WITH_ORANGES_CLAMPED can
be defined to use this library such that the maximum value is clamped to
prevent overflow, e.g. `reflex::ORanges<char> ch = 127` is clamped to 126.
However, this still excludes 127 from the range set.  This feature should not
be required when the library is used with sufficiently wide container value
types.

Example:

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~{.cpp}
    reflex::ORanges<int> ints;
    ints = 0;              // insert 0
    ints.insert(100, 200); // insert 100..200
    ints.insert(300, 400); // insert 300..400
    ints.insert(200, 300); // insert 200..300
    std::cout << "Set of " << ints.size() << " open-ended ranges:" << std::endl;
    for (reflex::ORanges<int>::const_iterator i = ints.begin(); i != ints.end(); ++i)
      std::cout << "[" << i->first << "," << i->second << ")" << std::endl;
    if (ints.find(200) != ints.end())
      std::cout << "200 is in the set" << std::endl;
    if (ints.find(99) == ints.end())
      std::cout << "99 is not in the set" << std::endl;
    if (ints.find(401) == ints.end())
      std::cout << "401 is not in the set" << std::endl;
    ints.erase(250, 350);
    for (reflex::ORanges<int>::const_iterator i = ints.begin(); i != ints.end(); ++i)
      std::cout << "[" << i->first << "," << i->second << ")" << std::endl;
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Output:

   Set of 2 open-ended ranges:
   [0,1)
   [100,401)
   200 is in the set
   99 is not in the set
   401 is not in the set
   [0,1)
   [100,250)
   [351,401)

*/
template<typename T>
class ORanges : public Ranges<T> {
 public:
  using Ranges<T>::insert;
  using Ranges<T>::contains;
  /// Type of the bounds.
  typedef T bound_type;
  /// Synonym type defining the base class container std::set.
  typedef typename std::set< std::pair<T,T>,range_compare<T> > container_type;
  /// Synonym type defining the base class container std::set::value_type.
  typedef typename container_type::value_type value_type;
  /// Synonym type defining the key/value comparison std::set::key_compare.
  typedef typename container_type::key_compare key_compare;
  typedef typename container_type::value_compare value_compare;
  /// Synonym type defining the base class container std::set::iterator.
  typedef typename container_type::iterator iterator;
  /// Synonym type defining the base class container std::set::const_iterator.
  typedef typename container_type::const_iterator const_iterator;
  /// Construct an empty range.
  ORanges()
  { }
  /// Construct a copy of a range [lo,hi].
  ORanges(const value_type& r) ///< range
  {
    insert(r.first, r.second);
  }
  /// Construct a range [lo,hi].
  ORanges(
      const bound_type& lo, ///< lower bound
      const bound_type& hi) ///< upper bound
  {
    insert(lo, hi);
  }
  /// Construct a singleton range [val,val].
  ORanges(const bound_type& val) ///< value
  {
    insert(val, val);
  }
  /// Update ranges to include range [lo,hi] by merging overlapping and adjacent ranges into one range.
  std::pair<iterator,bool> insert(
      const bound_type& lo, ///< lower bound
      const bound_type& hi) ///< upper bound
    /// @returns a pair of an iterator to the range and a flag indicating whether the range was inserted as new.
  {
    return Ranges<T>::insert(lo, bump(hi));
  }
  /// Update ranges to include range [val,val] by merging overlapping and adjacent ranges into one range.
  std::pair<iterator,bool> insert(const bound_type& val) ///< value to insert
    /// @returns a pair of an iterator to the range and a flag indicating whether the range was inserted as new.
  {
    return insert(val, val);
  }
  /// Update ranges by deleting the given range [lo,hi].
  bool erase(
      const bound_type& lo, ///< lower bound
      const bound_type& hi) ///< upper bound
    /// @returns true if ranges was updated.
  {
    // r = [lo+1, hi]
    value_type r(bump(lo), hi);
    iterator i = container_type::find(r);
    // if [lo,hi] does not overlap any range in the set then return
    if (i == container_type::end())
      return false;
    // while [lo,hi] overlaps with ranges in the set, erase ranges and adjust [lo,hi]
    do
    {
      if (std::less<bound_type>()(i->first, r.first)) // lo = min(lo, i.lo)
        r.first = i->first;
      if (std::less<bound_type>()(r.second, i->second)) // hi = max(hi, i.hi)
        r.second = i->second;
      container_type::erase(i++); // remove old range [i.lo,i.hi]
    }
    while (i != this->end() && !std::less<bound_type>()(hi, i->first));
    // put back remaining partial ranges, if any
    if (std::less<bound_type>()(r.first, lo))
      i = container_type::insert(i, value_type(r.first, lo)); // insert with hint i that follows the insertion point
    if (std::less<bound_type>()(bump(hi), r.second))
      container_type::insert(i, value_type(bump(hi), r.second)); // insert with hint i that precedes the insertion point
    return true;
  }
  /// Update ranges by deleting the given range [val,val].
  bool erase(const bound_type& val) ///< value to delete
    /// @returns true if ranges was updated.
  {
    return erase(val, val);
  }
  /// Find the first range that overlaps the given range.
  const_iterator find(
      const bound_type& lo, ///< lower bound
      const bound_type& hi) ///< upper bound
    const
    /// @returns iterator to the first range that overlaps the given range, or the end iterator.
  {
    return Ranges<T>::find(bump(lo), hi);
  }
  /// Find the range that includes the given value.
  const_iterator find(const bound_type& val) ///< value to search for
    const
    /// @returns iterator to the range that includes the value, or the end iterator.
  {
    return find(val, val);
  }
  /// Update ranges to remove ranges rs, has lower complexity than repeating erase().
  ORanges& operator-=(const ORanges& rs)
    /// @returns reference to this object.
  {
    iterator i = this->begin();
    const_iterator j = rs.begin();
    while (i != this->end() && j != rs.end())
    {
      if (std::less<bound_type>()(i->second, bump(j->first)))
      {
        ++i;
      }
      else if (std::less<bound_type>()(j->second, bump(i->first)))
      {
        ++j;
      }
      else
      {
        // r = [lo,hi]
        value_type r(j->first, j->second);
        // while [lo,hi] overlaps with ranges in the set, erase ranges and adjust [lo,hi]
        do
        {
          if (std::less<bound_type>()(i->first, r.first)) // lo = min(lo, i.lo)
            r.first = i->first;
          if (std::less<bound_type>()(r.second, i->second)) // hi = max(hi, i.hi)
            r.second = i->second;
          container_type::erase(i++); // remove old range [i.lo,i.hi]
        }
        while (i != this->end() && !std::less<bound_type>()(j->second, bump(i->first)));
        // put back remaining partial ranges, if any
        if (std::less<bound_type>()(r.first, j->first))
          i = container_type::insert(i, value_type(r.first, j->first)); // insert with hint i that follows the insertion point
        if (std::less<bound_type>()(j->second, r.second))
          i = container_type::insert(i, value_type(j->second, r.second)); // insert with hint i that precedes the insertion point
        ++j;
      }
    }
    return *this;
  }
  /// Update ranges to intersect the ranges of the given range set.
  ORanges& operator&=(const ORanges& rs) ///< ranges to intersect
    /// @returns reference to this object.
  {
    iterator i = this->begin();
    const_iterator j = rs.begin();
    while (i != this->end() && j != rs.end())
    {
      // remove ranges from this range set that are not in rs
      if (std::less<bound_type>()(i->second, bump(j->first)))
      {
        container_type::erase(i++);
      }
      else if (std::less<bound_type>()(j->second, bump(i->first)))
      {
        ++j;
      }
      else if (std::less<bound_type>()(j->second, i->second))
      {
        bound_type hi = i->second;
        if (std::less<bound_type>()(i->first, j->first))
        {
          container_type::erase(i++);
          container_type::insert(i, *j); // insert with hint i that follows the insertion point
        }
        else
        {
          value_type r(i->first, j->second);
          container_type::erase(i++);
          container_type::insert(i, r); // insert with hint i that follows the insertion point
        }
        // put upper part of the range back if next range in rs overlaps with it
        if (++j != rs.end())
          if (std::less<bound_type>()(j->first, hi))
            i = container_type::insert(i, value_type(j->first, hi)); // insert with hint i that follows the insertion point
      }
      else if (std::less<bound_type>()(i->second, j->second))
      {
        if (std::less<bound_type>()(i->first, j->first))
        {
          value_type r(j->first, i->second);
          container_type::erase(i++);
          container_type::insert(i, r); // insert with hint i that follows the insertion point
        }
        else
        {
          ++i;
        }
      }
      else if (std::less<bound_type>()(i->first, j->first))
      {
        container_type::erase(i++);
        container_type::insert(i, *j++); // insert with hint i that follows the insertion point
      }
      else
      {
        ++i;
        ++j;
      }
    }
    // remove ranges from this range set that are not in rs
    while (i != this->end())
      container_type::erase(i++);
    return *this;
  }
  /// Returns the union of two range sets.
  ORanges operator|(const ORanges& rs) ///< ranges to merge
    const
    /// @returns the union of this set and rs.
  {
    ORanges copy(*this);
    copy.Ranges<T>::operator|=(rs);
    return copy;
  }
  /// Returns the union of two range sets.
  ORanges operator+(const ORanges& rs) ///< ranges to merge
    const
    /// @returns the union of this set and rs.
  {
    ORanges copy(*this);
    copy.Ranges<T>::operator+=(rs);
    return copy;
  }
  /// Returns the difference of two open-ended range sets.
  ORanges operator-(const ORanges& rs) ///< ranges
    const
    /// @returns the difference of this set and rs.
  {
    return ORanges(*this) -= rs;
  }
  /// Returns the intersection of two open-ended range sets.
  ORanges operator&(const ORanges& rs) ///< ranges to intersect
    const
    /// @returns the intersection of this set and rs.
  {
    const_iterator i = this->begin();
    const_iterator j = rs.begin();
    ORanges r;
    iterator k = r.end();
    while (i != this->end() && j != rs.end())
    {
      if (std::less<bound_type>()(i->second, bump(j->first)))
      {
        ++i;
      }
      else if (std::less<bound_type>()(j->second, bump(i->first)))
      {
        ++j;
      }
      else if (std::less<bound_type>()(i->second, j->second))
      {
        if (std::less<bound_type>()(i->first, j->first))
          k = r.container_type::insert(k, value_type(j->first, i->second));
        else
          k = r.container_type::insert(k, *i);
        ++i;
      }
      else if (std::less<bound_type>()(j->second, i->second))
      {
        if (std::less<bound_type>()(i->first, j->first))
          k = r.container_type::insert(k, *j);
        else
          k = r.container_type::insert(k, value_type(i->first, j->second));
        ++j;
      }
      else if (std::less<bound_type>()(i->first, j->first))
      {
        k = r.container_type::insert(k, *j++);
        ++i;
      }
      else
      {
        k = r.container_type::insert(k, *i++);
        ++j;
      }
    }
    return r;
  }
  /// Return true if this set of ranges intersects with ranges rs, i.e. this set has at least one range [lo',hi'] that overlaps with a range [lo,hi] in rs such that lo <= hi' and lo' <= hi.
  bool intersects(const ORanges& rs) ///< ranges
    const
    /// @returns true if this set intersects rs.
  {
    const_iterator i = this->begin();
    const_iterator j = rs.begin();
    while (i != this->end() && j != rs.end())
    {
      if (std::less<bound_type>()(i->second, bump(j->first)))
        ++i;
      else if (std::less<bound_type>()(j->second, bump(i->first)))
        ++j;
      else
        return true;
    }
    return false;
  }
  /// Return the highest value in the set of ranges (the set cannot be empty)
  bound_type hi() const
    /// @returns highest value
  {
    return this->rbegin()->second - static_cast<bound_type>(1);
  }
 private:
  /// Bump value.
  static inline bound_type bump(bound_type val) ///< the value to bump
    /// @returns val + 1.
  {
#ifdef WITH_ORANGES_CLAMPED
    bound_type lav = ~val - 1; // trick to get around -Wstrict-overflow warning for signed types
    if (std::less<bound_type>()(~lav, val)) // check integer overflow, if overflow do not bump
      return val;
    return ~lav;
#else
    return static_cast<bound_type>(val + static_cast<bound_type>(1));
#endif
  }
};

} // namespace reflex

#endif
