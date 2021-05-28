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
@file      boostmatcher.h
@brief     Boost::regex-based matcher engines for pattern matching
@author    Robert van Engelen - engelen@genivia.com
@copyright (c) 2016-2020, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt
*/

#ifndef REFLEX_BOOSTMATCHER_H
#define REFLEX_BOOSTMATCHER_H

#include <reflex/absmatcher.h>
#include <boost/regex.hpp>

namespace reflex {

/// Boost matcher engine class implements reflex::PatternMatcher pattern matching interface with scan, find, split functors and iterators, using the Boost::regex library.
class BoostMatcher : public PatternMatcher<boost::regex> {
 public:
  /// Convert a regex to an acceptable form, given the specified regex library signature `"[decls:]escapes[?+]"`, see reflex::convert.
  template<typename T>
  static std::string convert(T regex, convert_flag_type flags = convert_flag::none)
  {
    return reflex::convert(regex, "imPRsx!#<>=&'(0123456789:abcdefghklnrstuvwxzABCDHLNQSUWZ0123456789<>?+", flags);
  }
  /// Default constructor.
  BoostMatcher()
    :
      PatternMatcher<boost::regex>(),
      flg_(boost::regex_constants::match_partial | boost::regex_constants::match_not_dot_newline)
  {
    reset();
  }
  /// Construct matcher engine from a boost::regex object or string regex, and an input character sequence.
  template<typename P> /// @tparam <P> pattern is a boost::regex or a string regex
  BoostMatcher(
      const P     *pattern,         ///< points to a boost::regex or a string regex for this matcher
      const Input& input = Input(), ///< input character sequence for this matcher
      const char  *opt = NULL)      ///< option string of the form `(A|N|T(=[[:digit:]])?|;)*`
    :
      PatternMatcher(pattern, input, opt),
      flg_(boost::regex_constants::match_partial | boost::regex_constants::match_not_dot_newline)
  {
    reset();
  }
  /// Construct matcher engine from a boost::regex object or string regex, and an input character sequence.
  template<typename P> /// @tparam <P> pattern is a boost::regex or a string regex
  BoostMatcher(
      const P&     pattern,         ///< a boost::regex or a string regex for this matcher
      const Input& input = Input(), ///< input character sequence for this matcher
      const char  *opt = NULL)      ///< option string of the form `(A|N|T(=[[:digit:]])?|;)*`
    :
      PatternMatcher(pattern, input, opt),
      flg_(boost::regex_constants::match_partial | boost::regex_constants::match_not_dot_newline)
  {
    reset();
  }
  /// Copy constructor.
  BoostMatcher(const BoostMatcher& matcher) ///< matcher to copy
    :
      PatternMatcher<boost::regex>(matcher),
      flg_(matcher.flg_)
  { }
  /// Assign a matcher.
  BoostMatcher& operator=(const BoostMatcher& matcher) ///< matcher to copy
  {
    PatternMatcher<boost::regex>::operator=(matcher);
    flg_ = matcher.flg_;
    return *this;
  }
  /// Polymorphic cloning.
  virtual BoostMatcher *clone()
  {
    return new BoostMatcher(*this);
  }
  /// Reset this matcher's state to the initial state and when assigned new input.
  virtual void reset(const char *opt = NULL)
  {
    DBGLOG("BoostMatcher::reset()");
    itr_ = fin_ = boost::cregex_iterator();
    grp_ = 0;
    PatternMatcher::reset(opt);
  }
  using PatternMatcher::pattern;
  /// Set the pattern to use with this matcher as a shared pointer to another matcher pattern.
  virtual PatternMatcher& pattern(const BoostMatcher& matcher) ///< the other matcher
    /// @returns this matcher.
  {
    opt_ = matcher.opt_;
    flg_ = matcher.flg_;
    return this->pattern(matcher.pattern());
  }
  /// Set the pattern to use with this matcher (the given pattern is shared and must be persistent).
  virtual PatternMatcher& pattern(const Pattern& pattern) ///< boost::regex for this matcher
    /// @returns this matcher.
  {
    itr_ = fin_;
    return PatternMatcher::pattern(pattern);
  }
  /// Set the pattern to use with this matcher (the given pattern is shared and must be persistent).
  virtual PatternMatcher& pattern(const Pattern *pattern) ///< boost::regex for this matcher
    /// @returns this matcher.
  {
    itr_ = fin_;
    return PatternMatcher::pattern(pattern);
  }
  /// Set the pattern from a regex string to use with this matcher.
  virtual PatternMatcher& pattern(const char *pattern) ///< regex string to instantiate internal pattern object
    /// @returns this matcher.
  {
    itr_ = fin_;
    return PatternMatcher::pattern(pattern);
  }
  /// Set the pattern from a regex string to use with this matcher.
  virtual PatternMatcher& pattern(const std::string& pattern) ///< regex string to instantiate internal pattern object
    /// @returns this matcher.
  {
    itr_ = fin_;
    return PatternMatcher::pattern(pattern);
  }
  /// Returns a pair of pointer and length of the captured match for n > 0 capture index or <text(),size() for n == 0.
  virtual std::pair<const char*,size_t> operator[](size_t n) ///< nth capture index > 0 or 0
    /// @returns pair.
    const
  {
    if (n == 0)
      return std::pair<const char*,size_t>(txt_, len_);
    if (itr_ == fin_ || n >= (*itr_).size() || !(*itr_)[n].matched)
      return std::pair<const char*,size_t>(static_cast<const char*>(NULL), 0); // cast to appease MSVC 2010
    return std::pair<const char*,size_t>((*itr_)[n].first, (*itr_)[n].second - (*itr_)[n].first);
  }
  /// Returns the group capture identifier containing the group capture index >0 and name (or NULL) of a named group capture, or (1,NULL) by default
  virtual std::pair<size_t,const char*> group_id()
    /// @returns a pair of size_t and string
  {
    grp_ = 1;
    if (itr_ == fin_ || (*itr_).size() <= 1)
      return std::pair<size_t,const char*>(0, static_cast<const char*>(NULL)); // cast to appease MSVC 2010
    if ((*itr_)[1].matched)
      return std::pair<size_t,const char*>(1, static_cast<const char*>(NULL)); // cast to appease MSVC 2010
    return group_next_id();
  }
  /// Returns the next group capture identifier containing the group capture index >0 and name (or NULL) of a named group capture, or (0,NULL) when no more groups matched
  virtual std::pair<size_t,const char*> group_next_id()
    /// @returns a pair of size_t and string
  {
    if (itr_ == fin_)
      return std::pair<size_t,const char*>(0, static_cast<const char*>(NULL)); // cast to appease MSVC 2010
    size_t n = (*itr_).size();
    while (++grp_ < n)
      if ((*itr_)[grp_].matched)
        break;
    if (grp_ < n)
      return std::pair<size_t,const char*>(grp_, static_cast<const char*>(NULL)); // cast to appease MSVC 2010
    return std::pair<size_t,const char*>(1, static_cast<const char*>(NULL)); // cast to appease MSVC 2010
  }
 protected:
  /// The match method Const::SCAN, Const::FIND, Const::SPLIT, or Const::MATCH, implemented with boost::regex.
  virtual size_t match(Method method) ///< match method Const::SCAN, Const::FIND, Const::SPLIT, or Const::MATCH
    /// @returns nonzero when input matched the pattern using method Const::SCAN, Const::FIND, Const::SPLIT, or Const::MATCH.
  {
    DBGLOG("BEGIN BoostMatcher::match(%d)", method);
    reset_text();
    txt_ = buf_ + cur_; // set first of text(), cur_ was last pos_, or cur_ was set with more()
    cur_ = pos_;
    if (itr_ != fin_) // if regex iterator is still valid then
    {
      if ((*itr_)[0].second == buf_ + pos_) // if last of regex iterator is still valid in buf_[] then
      {
        DBGLOGN("Continue iterating, pos = %zu", pos_);
        ++itr_;
        if (itr_ != fin_) // set pos_ to last of the (partial) match
          pos_ = (*itr_)[0].second - buf_;
      }
      else
      {
        itr_ = fin_; // need new iterator
      }
    }
    while (pos_ == end_ || itr_ == fin_) // fetch more data while pos_ is hitting the end_ or no iterator
    {
      if (pos_ == end_ && !eof_)
      {
        if (end_ + blk_ + 1 >= max_ && grow()) // make sure we have enough storage to read input
          itr_ = fin_; // buffer shifting/growing invalidates iterator
        (void)peek_more();
        DBGLOGN("Got more input pos = %zu end = %zu max = %zu", pos_, end_, max_);
      }
      if (pos_ == end_) // if pos_ is hitting the end_ then
      {
        if (method == Const::SPLIT)
        {
          DBGLOGN("Split end");
          if (got_ == Const::EOB)
          {
            cap_ = 0;
            len_ = 0;
          }
          else
          {
            if (!eof_ && itr_ == fin_)
              new_itr(method);
            if (itr_ != fin_ && (*itr_)[0].matched && cur_ != pos_)
            {
              size_t n = (*itr_).size();
              for (cap_ = 1; cap_ < n && !(*itr_)[cap_].matched; ++cap_)
                continue; // set cap_ to the capture index
              len_ = (*itr_)[0].first - txt_; // size() spans txt_ to cur_ in buf_[]
            }
            else
            {
              DBGLOGN("Matched empty end");
              cap_ = Const::EMPTY;
              len_ = pos_ - (txt_ - buf_); // size() spans txt_ to cur_ in buf_[]
              got_ = Const::EOB;
              eof_ = true;
            }
            itr_ = fin_;
            cur_ = pos_;
            DBGLOGN("Split: act = %zu txt = '%s' len = %zu pos = %zu eof = %d", cap_, txt_, len_, pos_, eof_ == true);
          }
          DBGLOG("END BoostMatcher::match()");
          return cap_;
        }
        if (method == Const::FIND && opt_.N && eof_ && (itr_ == fin_ || (*itr_)[0].first == buf_ + end_))
        {
          DBGLOGN("No match, pos = %zu", pos_);
          DBGLOG("END BoostMatcher::match()");
          return 0;
        }
        if (itr_ != fin_)
          break; // OK if iterator is still valid
      }
      new_itr(method); // need new iterator
      if (itr_ != fin_)
      {
        DBGLOGN("Possible (partial) match, pos = %zu", pos_);
        pos_ = (*itr_)[0].second - buf_; // set pos_ to last of the (partial) match
        if (pos_ == cur_ && !at_bob()) // match is at same pos as previous
        {
          ++itr_; // advance to next match
          if (itr_ != fin_)
            pos_ = (*itr_)[0].second - buf_; // set pos_ to last of the (partial) match
          else
            pos_ = end_;
        }
      }
      else // no (partial) match
      {
        if ((method == Const::SCAN || method == Const::MATCH))
        {
          pos_ = cur_;
          len_ = 0;
          cap_ = 0;
          DBGLOGN("No (partial) match, pos = %zu", pos_);
          DBGLOG("END BoostMatcher::match()");
          return 0;
        }
        pos_ = end_;
        if (eof_)
        {
          if (method == Const::SPLIT)
            continue;
          len_ = 0;
          cap_ = 0;
          DBGLOGN("No match at EOF, pos = %zu", pos_);
          DBGLOG("END BoostMatcher::match()");
          return 0;
        }
      }
    }
    if (method == Const::SPLIT)
    {
      DBGLOGN("Split match");
      size_t n = (*itr_).size();
      for (cap_ = 1; cap_ < n && !(*itr_)[cap_].matched; ++cap_)
        continue; // set cap_ to the capture index
      len_ = (*itr_)[0].first - txt_;
      set_current(pos_);
      DBGLOGN("Split: act = %zu txt = '%s' len = %zu pos = %zu", cap_, txt_, len_, pos_);
      DBGLOG("END BoostMatcher::match()");
      return cap_;
    }
    else if ((cur_ == end_ && eof_ && method != Const::MATCH) || !(*itr_)[0].matched || (buf_ + cur_ != (*itr_)[0].first && method != Const::FIND)) // if no match at first and we're not searching then
    {
      itr_ = fin_;
      pos_ = cur_;
      len_ = 0;
      cap_ = 0;
      DBGLOGN("No match, pos = %zu", pos_);
      DBGLOG("END BoostMatcher::match()");
      return 0;
    }
    if (method == Const::FIND)
      txt_ = const_cast<char*>((*itr_)[0].first);
    size_t n = (*itr_).size();
    for (cap_ = 1; cap_ < n && !(*itr_)[cap_].matched; ++cap_)
      continue; // set cap_ to the capture group index
    set_current(pos_);
    len_ = cur_ - (txt_ - buf_); // size() spans txt_ to cur_ in buf_[]
    if (len_ == 0 && cap_ != 0 && opt_.N && pos_ + 1 == end_)
      set_current(end_);
    DBGLOGN("Accept: act = %zu txt = '%s' len = %zu", cap_, txt_, len_);
    DBGCHK(len_ != 0 || method == Const::MATCH || (method == Const::FIND && opt_.N));
    DBGLOG("END BoostMatcher::match()");
    return cap_;
  }
  /// Create a new boost::regex iterator to (continue to) advance over input.
  inline void new_itr(Method method)
  {
    DBGLOGN("New iterator");
    boost::match_flag_type flg = flg_;
    if (!at_bob())
      flg |= boost::regex_constants::match_not_bob;
    if (!at_bol())
      flg |= boost::regex_constants::match_not_bol;
    if (isword(got_))
      flg |= boost::regex_constants::match_not_bow;
    if (method == Const::SCAN)
      flg |= boost::regex_constants::match_continuous | boost::regex_constants::match_not_null;
    else if (method == Const::FIND && !opt_.N)
      flg |= boost::regex_constants::match_not_null;
    else if (method == Const::MATCH)
      flg |= boost::regex_constants::match_continuous;
    ASSERT(pat_ != NULL);
    itr_ = boost::cregex_iterator(txt_, buf_ + end_, *pat_, flg);
  }
  boost::match_flag_type flg_; ///< boost::regex match flags
  boost::cregex_iterator itr_; ///< const boost::regex iterator
  boost::cregex_iterator fin_; ///< const boost::regex iterator final end
  size_t                 grp_; ///< last group index for group_next_id()
};

/// Boost matcher engine class, extends reflex::BoostMatcher for Boost POSIX regex matching.
/**
Boost POSIX regex matching enables Boost match flags `match_posix` and `match_not_dot_newline`. Lazy quantifiers are not supported by this matcher
engine.
*/
class BoostPosixMatcher : public BoostMatcher {
 public:
  /// Convert a regex to an acceptable form, given the specified regex library signature `"[decls:]escapes[?+]"`, see reflex::convert.
  template<typename T>
  static std::string convert(T regex, convert_flag_type flags = convert_flag::none)
  {
    return reflex::convert(regex, "imsx!#<=:abcdefghlnrstuvwxzABDHLNQSUWZ0<>", flags);
  }
  /// Default constructor.
  BoostPosixMatcher() : BoostMatcher()
  { }
  /// Construct a POSIX matcher engine from a boost::regex pattern and an input character sequence.
  template<typename P>
  BoostPosixMatcher(
      const P     *pattern,         ///< points to a boost::regex or a string regex for this matcher
      const Input& input = Input(), ///< input character sequence for this matcher
      const char  *opt = NULL)      ///< option string of the form `(A|N|T(=[[:digit:]])?|;)*`
    :
      BoostMatcher(pattern, input, opt)
  {
    flg_ |= boost::regex_constants::match_posix;
  }
  /// Construct a POSIX matcher engine from a boost::regex pattern and an input character sequence.
  template<typename P> /// @tparam <P> pattern is a boost::regex or a string regex
  BoostPosixMatcher(
      const P&     pattern,         ///< a boost::regex or a string regex for this matcher
      const Input& input = Input(), ///< input character sequence for this matcher
      const char  *opt = NULL)      ///< option string of the form `(A|N|T(=[[:digit:]])?|;)*`
    :
      BoostMatcher(pattern, input, opt)
  {
    flg_ |= boost::regex_constants::match_posix;
  }
};

/// Boost matcher engine class, extends reflex::BoostMatcher for Boost Perl regex matching.
/**
Boost Perl regex matching enables Boost match flag `match_perl` and `match_not_dot_newline`.
*/
class BoostPerlMatcher : public BoostMatcher {
 public:
  /// Default constructor.
  BoostPerlMatcher() : BoostMatcher()
  { }
  /// Construct a Perl matcher engine from a boost::regex pattern and an input character sequence.
  template<typename P>
  BoostPerlMatcher(
      const P     *pattern,         ///< points to a boost::regex or a string regex for this matcher
      const Input& input = Input(), ///< input character sequence for this matcher
      const char  *opt = NULL)      ///< option string of the form `(A|N|T(=[[:digit:]])?|;)*`
    :
      BoostMatcher(pattern, input, opt)
  {
    flg_ |= boost::regex_constants::match_perl;
  }
  /// Construct a Perl matcher engine from a boost::regex pattern and an input character sequence.
  template<typename P> /// @tparam <P> pattern is a boost::regex or a string regex
  BoostPerlMatcher(
      const P&     pattern,         ///< a boost::regex or a string regex for this matcher
      const Input& input = Input(), ///< input character sequence for this matcher
      const char  *opt = NULL)      ///< option string of the form `(A|N|T(=[[:digit:]])?|;)*`
    :
      BoostMatcher(pattern, input, opt)
  {
    flg_ |= boost::regex_constants::match_perl;
  }
};

} // namespace reflex

#endif
