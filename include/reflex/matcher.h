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
@file      matcher.h
@brief     RE/flex matcher engine
@author    Robert van Engelen - engelen@genivia.com
@copyright (c) 2015-2019, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt
*/

#ifndef REFLEX_MATCHER_H
#define REFLEX_MATCHER_H

#include <reflex/absmatcher.h>
#include <reflex/pattern.h>
#include <stack>

namespace reflex {

/// RE/flex matcher engine class, implements reflex::PatternMatcher pattern matching interface with scan, find, split functors and iterators.
/** More info TODO */
class Matcher : public PatternMatcher<reflex::Pattern> {
 public:
  /// Convert a regex to an acceptable form, given the specified regex library signature `"[decls:]escapes[?+]"`, see reflex::convert.
  template<typename T>
  static std::string convert(T regex, convert_flag_type flags = convert_flag::none)
  {
    return reflex::convert(regex, "imsx#=^:abcdefhijklnprstuvwxzABDHLPQSUW<>?+", flags);
  }
  /// Default constructor.
  Matcher() : PatternMatcher<reflex::Pattern>()
  {
    Matcher::reset();
  }
  /// Construct matcher engine from a pattern or a string regex, and an input character sequence.
  template<typename P> /// @tparam <P> a reflex::Pattern or a string regex 
  Matcher(
      const P     *pattern,         ///< points to a reflex::Pattern or a string regex for this matcher
      const Input& input = Input(), ///< input character sequence for this matcher
      const char  *opt = NULL)      ///< option string of the form `(A|N|T(=[[:digit:]])?|;)*`
    :
      PatternMatcher<reflex::Pattern>(pattern, input, opt)
  {
    reset(opt);
  }
  /// Construct matcher engine from a pattern or a string regex, and an input character sequence.
  template<typename P> /// @tparam <P> a reflex::Pattern or a string regex 
  Matcher(
      const P&     pattern,          ///< a reflex::Pattern or a string regex for this matcher
      const Input& input = Input(),  ///< input character sequence for this matcher
      const char   *opt = NULL)      ///< option string of the form `(A|N|T(=[[:digit:]])?|;)*`
    :
      PatternMatcher<reflex::Pattern>(pattern, input, opt)
  {
    reset(opt);
  }
  /// Copy constructor.
  Matcher(const Matcher& matcher) ///< matcher to copy with pattern (pattern may be shared)
    :
      PatternMatcher<reflex::Pattern>(matcher),
      ded_(matcher.ded_),
      tab_(matcher.tab_)
  {
    bmd_ = matcher.bmd_;
    if (bmd_ != 0)
      std::memcpy(bms_, matcher.bms_, sizeof(bms_));
  }
  /// Assign a matcher.
  Matcher& operator=(const Matcher& matcher) ///< matcher to copy
  {
    PatternMatcher<reflex::Pattern>::operator=(matcher);
    ded_ = matcher.ded_;
    tab_ = matcher.tab_;
    bmd_ = matcher.bmd_;
    if (bmd_ != 0)
      std::memcpy(bms_, matcher.bms_, sizeof(bms_));
    return *this;
  }
  /// Polymorphic cloning.
  virtual Matcher *clone()
  {
    return new Matcher(*this);
  }
  /// Reset this matcher's state to the initial state.
  virtual void reset(const char *opt = NULL)
  {
    DBGLOG("Matcher::reset()");
    PatternMatcher<reflex::Pattern>::reset(opt);
    ded_ = 0;
    tab_.resize(0);
    bmd_ = 0;
  }
  virtual std::pair<const char*,size_t> operator[](size_t n) const
  {
    if (n == 0)
      return std::pair<const char*,size_t>(txt_, len_);
    return std::pair<const char*,size_t>(NULL, 0);
  }
  /// Returns the position of the last indent stop.
  size_t last_stop()
  {
    if (tab_.empty())
      return 0;
    return tab_.back();
  }
  /// Inserts or appends an indent stop position, keeping indent stops sorted.
  void insert_stop(size_t n)
  {
    if (n > 0)
    {
      if (tab_.empty() || tab_.back() < n)
      {
        tab_.push_back(n);
      }
      else
      {
        for (std::vector<size_t>::reverse_iterator i = tab_.rbegin(); i != tab_.rend(); ++i)
        {
          if (*i == n)
            return;
          if (*i < n)
          {
            tab_.insert(i.base(), n);
            return;
          }
        }
        tab_.insert(tab_.begin(), n);
      }
    }
  }
  /// Remove all stop positions from position n and up until the last.
  void delete_stop(size_t n)
  {
    if (!tab_.empty())
    {
      for (std::vector<size_t>::reverse_iterator i = tab_.rbegin(); i != tab_.rend(); ++i)
      {
        if (*i < n)
        {
          tab_.erase(i.base(), tab_.end());
          return;
        }
      }
      tab_.clear();
    }
  }
  /// Returns reference to vector of current indent stop positions.
  std::vector<size_t>& stops()
    /// @returns vector of size_t
  {
    return tab_;
  }
  /// Clear indent stop positions.
  void clear_stops()
  {
    tab_.clear();
  }
  /// Push current indent stops and clear current indent stops.
  void push_stops()
  {
    stk_.push(std::vector<size_t>());
    stk_.top().swap(tab_);
  }
  /// Pop indent stops.
  void pop_stops()
  {
    stk_.top().swap(tab_);
    stk_.pop();
  }
  /// FSM code INIT.
  inline void FSM_INIT(int& c1)
  {
    c1 = fsm_.c1;
  }
  /// FSM code FIND.
  inline void FSM_FIND()
  {
    if (cap_ == 0)
      cur_ = pos_;
  }
  /// FSM code CHAR.
  inline int FSM_CHAR()
  {
    return get();
  }
  /// FSM code HALT.
  inline void FSM_HALT(int c1)
  {
    fsm_.c1 = c1;
  }
  /// FSM code TAKE.
  inline void FSM_TAKE(Pattern::Index cap)
  {
    cap_ = cap;
    cur_ = pos_;
  }
  /// FSM code TAKE.
  inline void FSM_TAKE(Pattern::Index cap, int c1)
  {
    cap_ = cap;
    cur_ = pos_;
    if (c1 != EOF)
      --cur_;
  }
  /// FSM code REDO.
  inline void FSM_REDO()
  {
    cap_ = Const::EMPTY;
    cur_ = pos_;
  }
  /// FSM code REDO.
  inline void FSM_REDO(int c1)
  {
    cap_ = Const::EMPTY;
    cur_ = pos_;
    if (c1 != EOF)
      --cur_;
  }
  /// FSM code HEAD.
  inline void FSM_HEAD(Pattern::Index la)
  {
    if (lap_.size() <= la && la < Pattern::Const::IMAX)
      lap_.resize(la + 1, -1);
    lap_[la] = static_cast<int>(pos_ - (txt_ - buf_));
  }
  /// FSM code TAIL.
  inline void FSM_TAIL(Pattern::Index la)
  {
    if (lap_.size() > la && lap_[la] >= 0)
      cur_ = txt_ - buf_ + static_cast<size_t>(lap_[la]);
  }
  /// FSM code DENT.
  inline bool FSM_DENT()
  {
    if (ded_ > 0)
    {
      fsm_.nul = true;
      return true;
    }
    return false;
  }
  /// FSM extra code POSN returns current position.
  inline size_t FSM_POSN()
  {
    return pos_ - (txt_ - buf_);
  }
  /// FSM extra code BACK position to a previous position returned by FSM_POSN().
  inline void FSM_BACK(size_t pos)
  {
    cur_ = txt_ - buf_ + pos;
  }
  /// FSM code META DED.
  inline bool FSM_META_DED()
  {
    return fsm_.bol && dedent();
  }
  /// FSM code META IND.
  inline bool FSM_META_IND()
  {
    return fsm_.bol && indent();
  }
  /// FSM code META UND.
  inline bool FSM_META_UND()
  {
    bool mrk = mrk_;
    mrk_ = false;
    ded_ = 0;
    return mrk;
  }
  /// FSM code META EOB.
  inline bool FSM_META_EOB(int c1)
  {
    return c1 == EOF;
  }
  /// FSM code META BOB.
  inline bool FSM_META_BOB()
  {
    return at_bob();
  }
  /// FSM code META EOL.
  inline bool FSM_META_EOL(int c1)
  {
    return c1 == EOF || c1 == '\n';
  }
  /// FSM code META BOL.
  inline bool FSM_META_BOL()
  {
    return fsm_.bol;
  }
  /// FSM code META EWE.
  inline bool FSM_META_EWE(int c0, int c1)
  {
    return isword(c0) && !isword(c1);
  }
  /// FSM code META BWE.
  inline bool FSM_META_BWE(int c0, int c1)
  {
    return !isword(c0) && isword(c1);
  }
  /// FSM code META EWB.
  inline bool FSM_META_EWB()
  {
    return at_eow();
  }
  /// FSM code META BWB.
  inline bool FSM_META_BWB()
  {
    return at_bow();
  }
  /// FSM code META NWE.
  inline bool FSM_META_NWE(int c0, int c1)
  {
    return isword(c0) == isword(c1);
  }
  /// FSM code META NWB.
  inline bool FSM_META_NWB()
  {
    return !at_bow() && !at_eow();
  }
 protected:
  typedef std::vector<size_t> Stops; ///< indent margin/tab stops
  /// FSM data for FSM code
  struct FSM {
    FSM() : bol(), nul(), c1() { }
    bool bol;
    bool nul;
    int  c1;
  };
  /// Returns true if input matched the pattern using method Const::SCAN, Const::FIND, Const::SPLIT, or Const::MATCH.
  virtual size_t match(Method method) ///< Const::SCAN, Const::FIND, Const::SPLIT, or Const::MATCH
    /// @returns nonzero if input matched the pattern
    ;
  /// Returns true if able to advance to next possible match
  bool advance()
    /// @returns true if possible match found
    ;
  /// Update indentation column counter for indent() and dedent().
  inline void newline()
  {
    mrk_ = true;
    while (ind_ + 1 < pos_)
      col_ += buf_[ind_++] == '\t' ? 1 + (~col_ & (opt_.T - 1)) : 1;
    DBGLOG("Newline with indent/dedent? col = %zu", col_);
  }
  /// Returns true if looking at indent.
  inline bool indent()
    /// @returns true if indent
  {
    newline();
    return col_ > 0 && (tab_.empty() || tab_.back() < col_);
  }
  /// Returns true if looking at dedent.
  inline bool dedent()
    /// @returns true if dedent
  {
    newline();
    return !tab_.empty() && tab_.back() > col_;
  }
  /// Boyer-Moore preprocessing of the given pattern pat of length len, generates bmd_ > 0 and bms_[] shifts.
  void boyer_moore_init(
      const char *pat, ///< pattern string
      size_t      len) ///< nonzero length of the pattern string, should be less than 256
  {
    // Relative frquency table of English letters, source code, and UTF-8 bytes
    static unsigned char freq[256] = "\0\0\0\0\0\0\0\0\0\73\4\0\0\4\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\73\70\70\1\1\2\2\70\70\70\2\2\70\70\70\2\3\3\3\3\3\3\3\3\3\3\70\70\70\70\70\70\2\35\14\24\26\37\20\17\30\33\11\12\25\22\32\34\15\7\27\31\36\23\13\21\10\16\6\70\1\70\2\70\1\67\46\56\60\72\52\51\62\65\43\44\57\54\64\66\47\41\61\63\71\55\45\53\42\50\40\70\2\70\2\0\47\47\47\47\47\47\47\47\47\47\47\47\47\47\47\47\45\45\45\45\45\45\45\45\45\45\45\45\45\45\45\45\45\45\45\45\45\45\45\45\45\45\45\45\45\45\45\45\44\44\44\44\44\44\44\44\44\44\44\44\44\44\44\44\0\0\5\5\5\5\5\5\5\5\5\5\5\5\5\5\5\5\5\5\5\5\5\5\5\5\5\5\5\5\5\5\46\56\56\56\56\56\56\56\56\56\56\56\56\46\56\56\73\0\0\0\0\0\0\0\0\0\0\0\0\0\0";
    size_t i;
    for (i = 0; i < 256; ++i)
      bms_[i] = static_cast<uint8_t>(len);
    size_t sum = 0;
    lcp_ = 0;
    for (i = 0; i < len; ++i)
    {
      uint8_t pch = static_cast<uint8_t>(pat[i]);
      bms_[pch] = static_cast<uint8_t>(len - i - 1);
      sum += bms_[pch];
      if (freq[static_cast<uint8_t>(pat[lcp_])] > freq[pch])
        lcp_ = i;
    }
    size_t j;
    for (i = len - 1, j = i; j > 0; --j)
      if (pat[j - 1] == pat[i])
        break;
    bmd_ = i - j + 1;
    sum /= len;
    uint8_t fch = freq[static_cast<uint8_t>(pat[lcp_])];
    if (sum > 1 && fch > 35 && (sum > 3 || fch > 48) && fch + sum > 48)
      lcp_ = 0xffff;
  }
  /// Returns true when match is predicted, based on s[0..3..e-1] (e >= s + 4).
  static inline bool predict_match(const Pattern::Pred pmh[], const char *s, size_t n)
  {
    Pattern::Hash h = static_cast<uint8_t>(*s);
    if (pmh[h] & 1)
      return false;
    h = Pattern::hash(h, static_cast<uint8_t>(*++s));
    if (pmh[h] & 2)
      return false;
    h = Pattern::hash(h, static_cast<uint8_t>(*++s));
    if (pmh[h] & 4)
      return false;
    h = Pattern::hash(h, static_cast<uint8_t>(*++s));
    if (pmh[h] & 8)
      return false;
    Pattern::Pred m = 16;
    const char *e = s + n - 3;
    while (++s < e)
    {
      h = Pattern::hash(h, static_cast<uint8_t>(*s));
      if (pmh[h] & m)
        return false;
      m <<= 1;
    }
    return true;
  }
  /// Returns zero when match is predicted or nonzero shift value, based on s[0..3].
  static inline size_t predict_match(const Pattern::Pred pma[], const char *s)
  {
    uint8_t b0 = s[0];
    uint8_t b1 = s[1];
    uint8_t b2 = s[2];
    uint8_t b3 = s[3];
    Pattern::Hash h1 = Pattern::hash(b0, b1);
    Pattern::Hash h2 = Pattern::hash(h1, b2);
    Pattern::Hash h3 = Pattern::hash(h2, b3);
    Pattern::Pred a0 = pma[b0];
    Pattern::Pred a1 = pma[h1];
    Pattern::Pred a2 = pma[h2];
    Pattern::Pred a3 = pma[h3];
    Pattern::Pred p = (a0 & 0xc0) | (a1 & 0x30) | (a2 & 0x0c) | (a3 & 0x03);
    Pattern::Pred m = (p >> 5) | (p >> 3) | (p >> 1) | p;
    if (m != 0xff)
      return 0;
    if ((pma[b1] & 0xc0) != 0xc0)
      return 1;
    if ((pma[b2] & 0xc0) != 0xc0)
      return 2;
    if ((pma[b3] & 0xc0) != 0xc0)
      return 3;
    return 4;
  }
  size_t            ded_;      ///< dedent count
  size_t            col_;      ///< column counter for indent matching, updated by newline(), indent(), and dedent()
  Stops             tab_;      ///< tab stops set by detecting indent margins
  std::vector<int>  lap_;      ///< lookahead position in input that heads a lookahead match (indexed by lookahead number)
  std::stack<Stops> stk_;      ///< stack to push/pop stops
  FSM               fsm_;      ///< local state for FSM code
  size_t            lcp_;      ///< least common character in the pattern prefix or 0xffff
  size_t            bmd_;      ///< Boyer-Moore jump distance on mismatch, B-M is enabled when bmd_ > 0
  uint8_t           bms_[256]; ///< Boyer-Moore skip array
  bool              mrk_;      ///< indent \i or dedent \j in pattern found: should check and update indent stops
};

} // namespace reflex

#endif
