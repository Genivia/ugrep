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
@file      linematcher.h
@brief     a matcher engine to match lines and nothing else
@author    Robert van Engelen - engelen@genivia.com
@copyright (c) 2016-2023, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt
*/

#ifndef REFLEX_LINEMATCHER_H
#define REFLEX_LINEMATCHER_H

#include <reflex/absmatcher.h>

namespace reflex {

/// Line matcher engine class implements reflex::PatternMatcher pattern matching interface with scan, find, split functors and iterators for matching lines only, use option 'A' to include newline with FIND, option 'N' to also FIND empty lines and option 'W' to only FIND empty lines
class LineMatcher : public AbstractMatcher {
 public:
  /// Construct matcher engine from an input character sequence.
  LineMatcher(
      const Input& input = Input(), ///< input character sequence for this matcher
      const char  *opt = NULL)      ///< option string of the form `(A|N|T(=[[:digit:]])?|;)*`
    :
      AbstractMatcher(input, opt),
      inc_(false)
  { }
  /// Copy constructor.
  LineMatcher(const LineMatcher& matcher) ///< matcher to copy
    :
      AbstractMatcher(matcher.in, matcher.opt_),
      inc_(false)
  { }
  /// Delete matcher.
  virtual ~LineMatcher()
  { }
  /// Assign a matcher.
  LineMatcher& operator=(const LineMatcher& matcher) ///< matcher to copy
  {
    AbstractMatcher::operator=(matcher);
    inc_ = matcher.inc_;
    return *this;
  }
  /// Polymorphic cloning.
  virtual LineMatcher *clone()
  {
    return new LineMatcher(*this);
  }
  /// Reset this matcher's state to the initial state and when assigned new input.
  virtual void reset(const char *opt = NULL)
  {
    DBGLOG("LineMatcher::reset()");
    AbstractMatcher::reset(opt);
    inc_ = false;
  }
  /// Returns a pair <text(),size() for any n
  virtual std::pair<const char*,size_t> operator[](size_t) ///< ignored
    /// @returns pair.
    const
  {
    return std::pair<const char*,size_t>(txt_, len_);
  }
  /// Returns (0,NULL)
  virtual std::pair<size_t,const char*> group_id()
    /// @returns a pair of size_t and string
  {
    return std::pair<size_t,const char*>(0, static_cast<const char*>(NULL)); // cast to appease MSVC 2010
  }
  /// Returns (0,NULL)
  virtual std::pair<size_t,const char*> group_next_id()
    /// @returns a pair of size_t and string
  {
    return std::pair<size_t,const char*>(0, static_cast<const char*>(NULL)); // cast to appease MSVC 2010
  }
 protected:
  /// The match method Const::SCAN, Const::FIND, Const::SPLIT, or Const::MATCH
  virtual size_t match(Method method) ///< match method Const::SCAN, Const::FIND, Const::SPLIT, or Const::MATCH
    /// @returns nonzero when input matched a line
  {
    DBGLOG("BEGIN LineMatcher::match(%d)", method);
    reset_text();
    got_ = '\n';
find:
    pos_ += inc_;
    txt_ = buf_ + pos_;
    cur_ = txt_ - buf_;
    len_ = 0;
    cap_ = !at_end();
    if (cap_)
    {
      inc_ = false;
      const char *end = eol(true);
      if (end == txt_)
        return cap_ = 0;
      pos_ = end - buf_;
      len_ = end - txt_;
      size_t n;
      switch (method)
      {
        case Const::SCAN:
          break;
        case Const::FIND:
          n = len_ - (*--end == '\n');
          // option A includes the terminating \n in the match, when present
          if (!opt_.A)
          {
            inc_ = (len_ > n);
            len_ = n;
            pos_ = cur_ + n;
          }
          // option N also finds empty lines
          if (n == 0 && !opt_.N)
            goto find;
          // option X only finds empty lines
          if (n > 0 && opt_.X)
            goto find;
          break;
        case Const::SPLIT:
          txt_ += len_ - (*--end == '\n');
          cur_ = txt_ - buf_;
          len_ = pos_ - cur_;
          break;
        case Const::MATCH:
          cap_ = eof_;
          break;
      }
    }
    return cap_;
  }
  bool inc_; ///< true if next find() should skip over \n
};

} // namespace reflex

#endif
