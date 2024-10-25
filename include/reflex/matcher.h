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
@copyright (c) 2016-2024, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt
*/

#ifndef REFLEX_MATCHER_H
#define REFLEX_MATCHER_H

#include <reflex/absmatcher.h>
#include <reflex/pattern.h>
#include <stack>

namespace reflex {

/// RE/flex matcher engine class, implements reflex::PatternMatcher pattern matching interface with scan, find, split functors and iterators.
class Matcher : public PatternMatcher<reflex::Pattern> {
 public:
  /// Convert a regex to an acceptable form, given the specified regex library signature `"[decls:]escapes[?+]"`, see reflex::convert.
  template<typename T>
  static std::string convert(T regex, convert_flag_type flags = convert_flag::none, bool *multiline = NULL)
  {
    return reflex::convert(regex, "imsx#=^:abcdefhijklnrstuvwxzABDHLNQSUW<>?", flags, multiline);
  }
  /// Default constructor.
  Matcher() : PatternMatcher<reflex::Pattern>()
  {
    Matcher::reset();
  }
  /// Construct matcher engine from a pattern, and an input character sequence.
  Matcher(
      const Pattern *pattern,         ///< points to a reflex::Pattern
      const Input&   input = Input(), ///< input character sequence for this matcher
      const char    *opt = NULL)      ///< option string of the form `(A|N|T(=[[:digit:]])?|;)*`
    :
      PatternMatcher<reflex::Pattern>(pattern, input)
  {
    reset(opt);
  }
  /// Construct matcher engine from a string regex, and an input character sequence.
  Matcher(
      const char   *pattern,         ///< a string regex for this matcher
      const Input&  input = Input(), ///< input character sequence for this matcher
      const char   *opt = NULL)      ///< option string of the form `(A|N|T(=[[:digit:]])?|;)*`
    :
      PatternMatcher<reflex::Pattern>(pattern, input)
  {
    reset(opt);
  }
  /// Construct matcher engine from a pattern, and an input character sequence.
  Matcher(
      const Pattern& pattern,         ///< a reflex::Pattern
      const Input&   input = Input(), ///< input character sequence for this matcher
      const char    *opt = NULL)      ///< option string of the form `(A|N|T(=[[:digit:]])?|;)*`
    :
      PatternMatcher<reflex::Pattern>(pattern, input)
  {
    reset(opt);
  }
  /// Construct matcher engine from a string regex, and an input character sequence.
  Matcher(
      const std::string& pattern,         ///< a reflex::Pattern or a string regex for this matcher
      const Input&       input = Input(), ///< input character sequence for this matcher
      const char        *opt = NULL)      ///< option string of the form `(A|N|T(=[[:digit:]])?|;)*`
    :
      PatternMatcher<reflex::Pattern>(pattern, input)
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
    DBGLOG("Matcher::Matcher(matcher)");
    init_advance();
  }
  /// Assign a matcher, the underlying pattern string is shared (not deep copied).
  Matcher& operator=(const Matcher& matcher) ///< matcher to copy
  {
    PatternMatcher<reflex::Pattern>::operator=(matcher);
    ded_ = matcher.ded_;
    tab_ = matcher.tab_;
    init_advance();
    return *this;
  }
  /// Set the pattern to use with this matcher (the given pattern is shared and must be persistent).
  Matcher& pattern(const Pattern& pattern) ///< pattern object for this matcher
    /// @returns this matcher
  {
    DBGLOG("Matcher::pattern()");
    if (pat_ != &pattern)
    {
      PatternMatcher<reflex::Pattern>::pattern(pattern);
      init_advance();
    }
    return *this;
  }
  /// Set the pattern to use with this matcher (the given pattern is shared and must be persistent).
  Matcher& pattern(const Pattern *pattern) ///< pattern object for this matcher
    /// @returns this matcher
  {
    DBGLOG("Matcher::pattern()");
    if (pat_ != pattern)
    {
      PatternMatcher<reflex::Pattern>::pattern(pattern);
      init_advance();
    }
    return *this;
  }
  /// Set the pattern from a regex string to use with this matcher.
  Matcher& pattern(const char *pattern) ///< regex string to instantiate internal pattern object
    /// @returns this matcher
  {
    DBGLOG("Matcher::pattern(\"%s\")", pattern);
    PatternMatcher<reflex::Pattern>::pattern(pattern);
    init_advance();
    return *this;
  }
  /// Set the pattern from a regex string to use with this matcher.
  Matcher& pattern(const std::string& pattern) ///< regex string to instantiate internal pattern object
    /// @returns this matcher
  {
    DBGLOG("Matcher::pattern(\"%s\")", pattern.c_str());
    PatternMatcher<reflex::Pattern>::pattern(pattern);
    init_advance();
    return *this;
  }
  /// Returns a reference to the pattern associated with this matcher.
  virtual const Pattern& pattern() const
    /// @returns reference to pattern
  {
    ASSERT(pat_ != NULL);
    return *pat_;
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
    init_advance();
  }
  /// Returns captured text as a std::pair<const char*,size_t> with string pointer (non-0-terminated) and length.
  virtual std::pair<const char*,size_t> operator[](size_t n) const
  {
    if (n == 0)
      return std::pair<const char*,size_t>(txt_, len_);
    return std::pair<const char*,size_t>(static_cast<const char*>(NULL), 0); // cast to appease MSVC 2010
  }
  /// Returns the group capture identifier containing the group capture index >0 and name (or NULL) of a named group capture, or (1,NULL) by default
  virtual std::pair<size_t,const char*> group_id()
    /// @returns a pair of size_t and string
  {
    return std::pair<size_t,const char*>(accept(), static_cast<const char*>(NULL)); // cast to appease MSVC 2010
  }
  /// Returns the next group capture identifier containing the group capture index >0 and name (or NULL) of a named group capture, or (0,NULL) when no more groups matched
  virtual std::pair<size_t,const char*> group_next_id()
    /// @returns (0,NULL)
  {
    return std::pair<size_t,const char*>(0, static_cast<const char*>(NULL)); // cast to appease MSVC 2010
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
  inline void FSM_INIT(int& c)
  {
    c = fsm_.ch;
  }
  /// FSM code FIND.
  inline void FSM_FIND()
  {
    if (cap_ == 0 && pos_ > cur_)
    {
      // use bit_[] to check each char in buf_[cur_+1..pos_-1] if it is a starting char, if not then increase cur_
      while (++cur_ < pos_ && (pat_->bit_[static_cast<uint8_t>(buf_[cur_])] & 1))
        continue;
    }
  }
  /// FSM code CHAR.
  inline int FSM_CHAR()
  {
    return get();
  }
  /// FSM code HALT.
  inline void FSM_HALT(int c = AbstractMatcher::Const::UNK)
  {
    fsm_.ch = c;
  }
  /// FSM code TAKE.
  inline void FSM_TAKE(Pattern::Accept cap)
  {
    int ch = peek();
    if (!opt_.W || at_we(ch, pos_))
    {
      cap_ = cap;
      cur_ = pos_;
    }
  }
  /// FSM code TAKE.
  inline void FSM_TAKE(Pattern::Accept cap, int c)
  {
    if (!opt_.W || at_we(c, pos_ - 1))
    {
      cap_ = cap;
      cur_ = pos_;
      if (c != EOF)
        --cur_;
    }
  }
  /// FSM code REDO.
  inline void FSM_REDO()
  {
    cap_ = Const::REDO;
    cur_ = pos_;
  }
  /// FSM code REDO.
  inline void FSM_REDO(int c)
  {
    cap_ = Const::REDO;
    cur_ = pos_;
    if (c != EOF)
      --cur_;
  }
  /// FSM code HEAD.
  inline void FSM_HEAD(Pattern::Lookahead la)
  {
    if (lap_.size() <= la)
      lap_.resize(la + 1, -1);
    lap_[la] = static_cast<int>(pos_ - (txt_ - buf_));
  }
  /// FSM code TAIL.
  inline void FSM_TAIL(Pattern::Lookahead la)
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
#if !defined(WITH_NO_INDENT)
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
    bool mrk = mrk_ && !nodent();
    mrk_ = false;
    ded_ = 0;
    return mrk;
  }
#endif
  /// FSM code META EOB.
  inline bool FSM_META_EOB(int c)
  {
    return c == EOF;
  }
  /// FSM code META BOB.
  inline bool FSM_META_BOB()
  {
    return at_bob();
  }
  /// FSM code META EOL.
  inline bool FSM_META_EOL(int c)
  {
    return c == EOF || c == '\n' || (c == '\r' && peek() == '\n');
  }
  /// FSM code META BOL.
  inline bool FSM_META_BOL()
  {
    return fsm_.bol;
  }
  /// FSM code META EWE.
  inline bool FSM_META_EWE(int c)
  {
    return at_ewe(c);
  }
  /// FSM code META BWE.
  inline bool FSM_META_BWE(int c)
  {
    return at_bwe(c);
  }
  /// FSM code META EWB.
  inline bool FSM_META_EWB()
  {
    return at_ewb();
  }
  /// FSM code META BWB.
  inline bool FSM_META_BWB()
  {
    return at_bwb();
  }
  /// FSM code META NWE.
  inline bool FSM_META_NWE(int c)
  {
    return at_nwe(c);
  }
  /// FSM code META NWB.
  inline bool FSM_META_NWB()
  {
    return at_nwb();
  }
  /// FSM code META WBE.
  inline bool FSM_META_WBE(int c)
  {
    return at_wbe(c);
  }
  /// FSM code META WBB.
  inline bool FSM_META_WBB()
  {
    return at_wbb();
  }
 protected:
  typedef std::vector<size_t> Stops; ///< indent margin/tab stops
  /// FSM data for FSM code
  struct FSM {
    FSM() : bol(), nul(), ch() { }
    bool bol;
    bool nul;
    int  ch;
  };
  /// Return true if Unicode word character.
  static bool iswword(int c) ///< character to test
  {
    // table source: unicode/language_scripts.cpp Word[] array updated to Unicode 15.1
    static const int word[2*712] = {
      48, 57,
      65, 90,
      95, 95,
      97, 122,
      170, 170,
      181, 181,
      186, 186,
      192, 214,
      216, 246,
      248, 705,
      710, 721,
      736, 740,
      748, 748,
      750, 750,
      880, 884,
      886, 887,
      890, 893,
      895, 895,
      902, 902,
      904, 906,
      908, 908,
      910, 929,
      931, 1013,
      1015, 1153,
      1162, 1327,
      1329, 1366,
      1369, 1369,
      1376, 1416,
      1488, 1514,
      1519, 1522,
      1568, 1610,
      1632, 1641,
      1646, 1647,
      1649, 1747,
      1749, 1749,
      1765, 1766,
      1774, 1788,
      1791, 1791,
      1808, 1808,
      1810, 1839,
      1869, 1957,
      1969, 1969,
      1984, 2026,
      2036, 2037,
      2042, 2042,
      2048, 2069,
      2074, 2074,
      2084, 2084,
      2088, 2088,
      2112, 2136,
      2144, 2154,
      2160, 2183,
      2185, 2190,
      2208, 2249,
      2308, 2361,
      2365, 2365,
      2384, 2384,
      2392, 2401,
      2406, 2415,
      2417, 2432,
      2437, 2444,
      2447, 2448,
      2451, 2472,
      2474, 2480,
      2482, 2482,
      2486, 2489,
      2493, 2493,
      2510, 2510,
      2524, 2525,
      2527, 2529,
      2534, 2545,
      2556, 2556,
      2565, 2570,
      2575, 2576,
      2579, 2600,
      2602, 2608,
      2610, 2611,
      2613, 2614,
      2616, 2617,
      2649, 2652,
      2654, 2654,
      2662, 2671,
      2674, 2676,
      2693, 2701,
      2703, 2705,
      2707, 2728,
      2730, 2736,
      2738, 2739,
      2741, 2745,
      2749, 2749,
      2768, 2768,
      2784, 2785,
      2790, 2799,
      2809, 2809,
      2821, 2828,
      2831, 2832,
      2835, 2856,
      2858, 2864,
      2866, 2867,
      2869, 2873,
      2877, 2877,
      2908, 2909,
      2911, 2913,
      2918, 2927,
      2929, 2929,
      2947, 2947,
      2949, 2954,
      2958, 2960,
      2962, 2965,
      2969, 2970,
      2972, 2972,
      2974, 2975,
      2979, 2980,
      2984, 2986,
      2990, 3001,
      3024, 3024,
      3046, 3055,
      3077, 3084,
      3086, 3088,
      3090, 3112,
      3114, 3129,
      3133, 3133,
      3160, 3162,
      3165, 3165,
      3168, 3169,
      3174, 3183,
      3200, 3200,
      3205, 3212,
      3214, 3216,
      3218, 3240,
      3242, 3251,
      3253, 3257,
      3261, 3261,
      3293, 3294,
      3296, 3297,
      3302, 3311,
      3313, 3314,
      3332, 3340,
      3342, 3344,
      3346, 3386,
      3389, 3389,
      3406, 3406,
      3412, 3414,
      3423, 3425,
      3430, 3439,
      3450, 3455,
      3461, 3478,
      3482, 3505,
      3507, 3515,
      3517, 3517,
      3520, 3526,
      3558, 3567,
      3585, 3632,
      3634, 3635,
      3648, 3654,
      3664, 3673,
      3713, 3714,
      3716, 3716,
      3718, 3722,
      3724, 3747,
      3749, 3749,
      3751, 3760,
      3762, 3763,
      3773, 3773,
      3776, 3780,
      3782, 3782,
      3792, 3801,
      3804, 3807,
      3840, 3840,
      3872, 3881,
      3904, 3911,
      3913, 3948,
      3976, 3980,
      4096, 4138,
      4159, 4169,
      4176, 4181,
      4186, 4189,
      4193, 4193,
      4197, 4198,
      4206, 4208,
      4213, 4225,
      4238, 4238,
      4240, 4249,
      4256, 4293,
      4295, 4295,
      4301, 4301,
      4304, 4346,
      4348, 4680,
      4682, 4685,
      4688, 4694,
      4696, 4696,
      4698, 4701,
      4704, 4744,
      4746, 4749,
      4752, 4784,
      4786, 4789,
      4792, 4798,
      4800, 4800,
      4802, 4805,
      4808, 4822,
      4824, 4880,
      4882, 4885,
      4888, 4954,
      4992, 5007,
      5024, 5109,
      5112, 5117,
      5121, 5740,
      5743, 5759,
      5761, 5786,
      5792, 5866,
      5873, 5880,
      5888, 5905,
      5919, 5937,
      5952, 5969,
      5984, 5996,
      5998, 6000,
      6016, 6067,
      6103, 6103,
      6108, 6108,
      6112, 6121,
      6160, 6169,
      6176, 6264,
      6272, 6276,
      6279, 6312,
      6314, 6314,
      6320, 6389,
      6400, 6430,
      6470, 6509,
      6512, 6516,
      6528, 6571,
      6576, 6601,
      6608, 6617,
      6656, 6678,
      6688, 6740,
      6784, 6793,
      6800, 6809,
      6823, 6823,
      6917, 6963,
      6981, 6988,
      6992, 7001,
      7043, 7072,
      7086, 7141,
      7168, 7203,
      7232, 7241,
      7245, 7293,
      7296, 7304,
      7312, 7354,
      7357, 7359,
      7401, 7404,
      7406, 7411,
      7413, 7414,
      7418, 7418,
      7424, 7615,
      7680, 7957,
      7960, 7965,
      7968, 8005,
      8008, 8013,
      8016, 8023,
      8025, 8025,
      8027, 8027,
      8029, 8029,
      8031, 8061,
      8064, 8116,
      8118, 8124,
      8126, 8126,
      8130, 8132,
      8134, 8140,
      8144, 8147,
      8150, 8155,
      8160, 8172,
      8178, 8180,
      8182, 8188,
      8255, 8256,
      8276, 8276,
      8305, 8305,
      8319, 8319,
      8336, 8348,
      8450, 8450,
      8455, 8455,
      8458, 8467,
      8469, 8469,
      8473, 8477,
      8484, 8484,
      8486, 8486,
      8488, 8488,
      8490, 8493,
      8495, 8505,
      8508, 8511,
      8517, 8521,
      8526, 8526,
      8579, 8580,
      11264, 11492,
      11499, 11502,
      11506, 11507,
      11520, 11557,
      11559, 11559,
      11565, 11565,
      11568, 11623,
      11631, 11631,
      11648, 11670,
      11680, 11686,
      11688, 11694,
      11696, 11702,
      11704, 11710,
      11712, 11718,
      11720, 11726,
      11728, 11734,
      11736, 11742,
      11823, 11823,
      12293, 12294,
      12337, 12341,
      12347, 12348,
      12353, 12438,
      12445, 12447,
      12449, 12538,
      12540, 12543,
      12549, 12591,
      12593, 12686,
      12704, 12735,
      12784, 12799,
      13312, 19903,
      19968, 42124,
      42192, 42237,
      42240, 42508,
      42512, 42539,
      42560, 42606,
      42623, 42653,
      42656, 42725,
      42775, 42783,
      42786, 42888,
      42891, 42954,
      42960, 42961,
      42963, 42963,
      42965, 42969,
      42994, 43009,
      43011, 43013,
      43015, 43018,
      43020, 43042,
      43072, 43123,
      43138, 43187,
      43216, 43225,
      43250, 43255,
      43259, 43259,
      43261, 43262,
      43264, 43301,
      43312, 43334,
      43360, 43388,
      43396, 43442,
      43471, 43481,
      43488, 43492,
      43494, 43518,
      43520, 43560,
      43584, 43586,
      43588, 43595,
      43600, 43609,
      43616, 43638,
      43642, 43642,
      43646, 43695,
      43697, 43697,
      43701, 43702,
      43705, 43709,
      43712, 43712,
      43714, 43714,
      43739, 43741,
      43744, 43754,
      43762, 43764,
      43777, 43782,
      43785, 43790,
      43793, 43798,
      43808, 43814,
      43816, 43822,
      43824, 43866,
      43868, 43881,
      43888, 44002,
      44016, 44025,
      44032, 55203,
      55216, 55238,
      55243, 55291,
      63744, 64109,
      64112, 64217,
      64256, 64262,
      64275, 64279,
      64285, 64285,
      64287, 64296,
      64298, 64310,
      64312, 64316,
      64318, 64318,
      64320, 64321,
      64323, 64324,
      64326, 64433,
      64467, 64829,
      64848, 64911,
      64914, 64967,
      65008, 65019,
      65075, 65076,
      65101, 65103,
      65136, 65140,
      65142, 65276,
      65296, 65305,
      65313, 65338,
      65343, 65343,
      65345, 65370,
      65382, 65470,
      65474, 65479,
      65482, 65487,
      65490, 65495,
      65498, 65500,
      65536, 65547,
      65549, 65574,
      65576, 65594,
      65596, 65597,
      65599, 65613,
      65616, 65629,
      65664, 65786,
      66176, 66204,
      66208, 66256,
      66304, 66335,
      66349, 66368,
      66370, 66377,
      66384, 66421,
      66432, 66461,
      66464, 66499,
      66504, 66511,
      66560, 66717,
      66720, 66729,
      66736, 66771,
      66776, 66811,
      66816, 66855,
      66864, 66915,
      66928, 66938,
      66940, 66954,
      66956, 66962,
      66964, 66965,
      66967, 66977,
      66979, 66993,
      66995, 67001,
      67003, 67004,
      67072, 67382,
      67392, 67413,
      67424, 67431,
      67456, 67461,
      67463, 67504,
      67506, 67514,
      67584, 67589,
      67592, 67592,
      67594, 67637,
      67639, 67640,
      67644, 67644,
      67647, 67669,
      67680, 67702,
      67712, 67742,
      67808, 67826,
      67828, 67829,
      67840, 67861,
      67872, 67897,
      67968, 68023,
      68030, 68031,
      68096, 68096,
      68112, 68115,
      68117, 68119,
      68121, 68149,
      68192, 68220,
      68224, 68252,
      68288, 68295,
      68297, 68324,
      68352, 68405,
      68416, 68437,
      68448, 68466,
      68480, 68497,
      68608, 68680,
      68736, 68786,
      68800, 68850,
      68864, 68899,
      68912, 68921,
      69248, 69289,
      69296, 69297,
      69376, 69404,
      69415, 69415,
      69424, 69445,
      69488, 69505,
      69552, 69572,
      69600, 69622,
      69635, 69687,
      69734, 69743,
      69745, 69746,
      69749, 69749,
      69763, 69807,
      69840, 69864,
      69872, 69881,
      69891, 69926,
      69942, 69951,
      69956, 69956,
      69959, 69959,
      69968, 70002,
      70006, 70006,
      70019, 70066,
      70081, 70084,
      70096, 70106,
      70108, 70108,
      70144, 70161,
      70163, 70187,
      70207, 70208,
      70272, 70278,
      70280, 70280,
      70282, 70285,
      70287, 70301,
      70303, 70312,
      70320, 70366,
      70384, 70393,
      70405, 70412,
      70415, 70416,
      70419, 70440,
      70442, 70448,
      70450, 70451,
      70453, 70457,
      70461, 70461,
      70480, 70480,
      70493, 70497,
      70656, 70708,
      70727, 70730,
      70736, 70745,
      70751, 70753,
      70784, 70831,
      70852, 70853,
      70855, 70855,
      70864, 70873,
      71040, 71086,
      71128, 71131,
      71168, 71215,
      71236, 71236,
      71248, 71257,
      71296, 71338,
      71352, 71352,
      71360, 71369,
      71424, 71450,
      71472, 71481,
      71488, 71494,
      71680, 71723,
      71840, 71913,
      71935, 71942,
      71945, 71945,
      71948, 71955,
      71957, 71958,
      71960, 71983,
      71999, 71999,
      72001, 72001,
      72016, 72025,
      72096, 72103,
      72106, 72144,
      72161, 72161,
      72163, 72163,
      72192, 72192,
      72203, 72242,
      72250, 72250,
      72272, 72272,
      72284, 72329,
      72349, 72349,
      72368, 72440,
      72704, 72712,
      72714, 72750,
      72768, 72768,
      72784, 72793,
      72818, 72847,
      72960, 72966,
      72968, 72969,
      72971, 73008,
      73030, 73030,
      73040, 73049,
      73056, 73061,
      73063, 73064,
      73066, 73097,
      73112, 73112,
      73120, 73129,
      73440, 73458,
      73474, 73474,
      73476, 73488,
      73490, 73523,
      73552, 73561,
      73648, 73648,
      73728, 74649,
      74880, 75075,
      77712, 77808,
      77824, 78895,
      78913, 78918,
      82944, 83526,
      92160, 92728,
      92736, 92766,
      92768, 92777,
      92784, 92862,
      92864, 92873,
      92880, 92909,
      92928, 92975,
      92992, 92995,
      93008, 93017,
      93027, 93047,
      93053, 93071,
      93760, 93823,
      93952, 94026,
      94032, 94032,
      94099, 94111,
      94176, 94177,
      94179, 94179,
      94208, 100343,
      100352, 101589,
      101632, 101640,
      110576, 110579,
      110581, 110587,
      110589, 110590,
      110592, 110882,
      110898, 110898,
      110928, 110930,
      110933, 110933,
      110948, 110951,
      110960, 111355,
      113664, 113770,
      113776, 113788,
      113792, 113800,
      113808, 113817,
      119808, 119892,
      119894, 119964,
      119966, 119967,
      119970, 119970,
      119973, 119974,
      119977, 119980,
      119982, 119993,
      119995, 119995,
      119997, 120003,
      120005, 120069,
      120071, 120074,
      120077, 120084,
      120086, 120092,
      120094, 120121,
      120123, 120126,
      120128, 120132,
      120134, 120134,
      120138, 120144,
      120146, 120485,
      120488, 120512,
      120514, 120538,
      120540, 120570,
      120572, 120596,
      120598, 120628,
      120630, 120654,
      120656, 120686,
      120688, 120712,
      120714, 120744,
      120746, 120770,
      120772, 120779,
      120782, 120831,
      122624, 122654,
      122661, 122666,
      122928, 122989,
      123136, 123180,
      123191, 123197,
      123200, 123209,
      123214, 123214,
      123536, 123565,
      123584, 123627,
      123632, 123641,
      124112, 124139,
      124144, 124153,
      124896, 124902,
      124904, 124907,
      124909, 124910,
      124912, 124926,
      124928, 125124,
      125184, 125251,
      125259, 125259,
      125264, 125273,
      126464, 126467,
      126469, 126495,
      126497, 126498,
      126500, 126500,
      126503, 126503,
      126505, 126514,
      126516, 126519,
      126521, 126521,
      126523, 126523,
      126530, 126530,
      126535, 126535,
      126537, 126537,
      126539, 126539,
      126541, 126543,
      126545, 126546,
      126548, 126548,
      126551, 126551,
      126553, 126553,
      126555, 126555,
      126557, 126557,
      126559, 126559,
      126561, 126562,
      126564, 126564,
      126567, 126570,
      126572, 126578,
      126580, 126583,
      126585, 126588,
      126590, 126590,
      126592, 126601,
      126603, 126619,
      126625, 126627,
      126629, 126633,
      126635, 126651,
      130032, 130041,
      131072, 173791,
      173824, 177977,
      177984, 178205,
      178208, 183969,
      183984, 191456,
      191472, 192093,
      194560, 195101,
      196608, 201546,
      201552, 205743,
    };
    static const uint16_t num = sizeof(word) / sizeof(int) / 2;
    uint16_t min = 0;
    uint16_t max = num - 1;
    // binary search in table
    if (c >= word[0] && c <= word[2 * num - 1])
    {
      while (max >= min)
      {
        uint16_t mid = (min + max) / 2;
        if (c < word[2 * mid])
          max = mid - 1;
        else if (c > word[2 * mid + 1])
          min = mid + 1;
        else
          return true;
      }
    }
    return false;
  }
  /// Check if a word begins before a match.
  inline bool at_wb()
  {
#if WITH_SPAN
    int c = got_;
    if (c == Const::BOB || c == Const::UNK || c == '\n')
      return true;
    if (c == '_')
      return false;
    if ((c & 0xc0) == 0x80 && cur_ > 0)
    {
      size_t k = cur_ - 1;
      if (k > 0 && (buf_[--k] & 0xc0) == 0x80)
        if (k > 0 && (buf_[--k] & 0xc0) == 0x80)
          if (k > 0)
            --k;
      c = utf8(&buf_[k]);
      return !iswword(c);
    }
    return !std::isalnum(static_cast<unsigned char>(c));
#else
    return !isword(got_);
#endif
  }
  /// Check if a word ends after the match.
  inline bool at_we(
      int c,    ///< character after the match
      size_t k) ///< position in the buffer of the character after the match
  {
#if WITH_SPAN
    if (c == EOF)
      return true;
    if (c == '_')
      return false;
    if ((c & 0xc0) == 0xc0)
    {
      c = utf8(&buf_[k]);
      return !iswword(c);
    }
    return !std::isalnum(static_cast<unsigned char>(c));
#else
    (void)k;
    return !isword(c);
#endif
  }
  /// Check if match begins a word (after split with len_ > 0 or len_ = 0 for find).
  inline bool at_bw()
  {
#if WITH_SPAN
    int c = static_cast<unsigned char>(txt_[len_]);
    if (c == '_')
      return true;
    if ((c & 0xc0) == 0xc0)
    {
      c = utf8(&txt_[len_]);
      return iswword(c);
    }
    return std::isalnum(static_cast<unsigned char>(c));
#else
    return isword(static_cast<unsigned char>(txt_[len_]))
#endif
  }
  /// Check if match ends a word.
  inline bool at_ew(int c)
  {
    size_t k = pos_ + (c == EOF);
    c = k > 1 ? static_cast<unsigned char>(buf_[k - 2]) : got_;
#if WITH_SPAN
    if (c == Const::BOB || c == Const::UNK || c == '\n')
      return false;
    if (c == '_')
      return true;
    if ((c & 0xc0) == 0x80 && k > 2)
    {
      k -= 3;
      if ((buf_[k] & 0xc0) == 0x80)
        if (k > 0 && (buf_[--k] & 0xc0) == 0x80)
          if (k > 0)
            --k;
      c = utf8(&buf_[k]);
      return iswword(c);
    }
    return std::isalnum(static_cast<unsigned char>(c));
#else
    return isword(c);
#endif
  }
  /// Check end of word at match end boundary MATCH\> at pos.
  inline bool at_ewe(int c) ///< character last read with get()
  {
    return at_we(c, pos_) && at_ew(c);
  }
  /// Check begin of word at match end boundary MATCH\< at pos.
  inline bool at_bwe(int c) ///< character last read with get()
  {
    return !at_we(c, pos_) && !at_ew(c);
  }
  /// Check end of word at match begin boundary \>MATCH after matching (SPLIT len_ > 0 or len_ = 0 for FIND).
  inline bool at_ewb()
  {
    return !at_bw() && !at_wb();
  }
  /// Check begin of word at match begin boundary \<MATCH after matching (SPLIT len_ > 0 or len_ = 0 for FIND).
  inline bool at_bwb()
  {
    return at_bw() && at_wb();
  }
  /// Check not a word boundary at match end MATCH\B at pos.
  inline bool at_nwe(int c) ///< character last read with get()
  {
    return at_we(c, pos_) != at_ew(c);
  }
  /// Check not a word boundary at match begin \BMATCH after matching (SPLIT len_ > 0 or len_ = 0 for FIND).
  inline bool at_nwb()
  {
    return at_bw() != at_wb();
  }
  /// Check word boundary at match end MATCH\b at pos.
  inline bool at_wbe(int c) ///< character last read with get()
  {
    return at_we(c, pos_) == at_ew(c);
  }
  /// Check word boundary at match begin \bMATCH after matching (SPLIT len_ > 0 or len_ = 0 for FIND).
  inline bool at_wbb()
  {
    return at_bw() == at_wb();
  }
  /// Returns true if input matched the pattern using method Const::SCAN, Const::FIND, Const::SPLIT, or Const::MATCH.
  virtual size_t match(Method method) ///< Const::SCAN, Const::FIND, Const::SPLIT, or Const::MATCH
    /// @returns nonzero if input matched the pattern
    ;
  /// match() with optimized AVX512BW string search scheme defined in matcher_avx512bw.cpp
  size_t simd_match_avx512bw(Method method);
  /// match() with optimized AVX2 string search scheme defined in matcher_avx2.cpp
  size_t simd_match_avx2(Method method);
  /// Initialize specialized (+ SSE2/NEON) pattern search methods to advance the engine to a possible match
  void init_advance();
  /// Initialize specialized AVX2 pattern search methods to advance the engine to a possible match
  void simd_init_advance_avx2();
  /// Initialize specialized AVX512BW pattern search methods to advance the engine to a possible match
  void simd_init_advance_avx512bw();
  /// Default method is none (unset)
  bool advance_none(size_t loc);
  // Single needle SSE2/NEON methods
  bool advance_pattern_pin1_one(size_t loc);
  bool advance_pattern_pin1_pma(size_t loc);
  template <uint8_t MIN> bool advance_pattern_pin1_pmh(size_t loc);
  // Generated multi-needle SSE2/NEON methods
  bool advance_pattern_pin2_one(size_t loc);
  bool advance_pattern_pin2_pma(size_t loc);
  template <uint8_t MIN> bool advance_pattern_pin2_pmh(size_t loc);
  bool advance_pattern_pin3_one(size_t loc);
  bool advance_pattern_pin3_pma(size_t loc);
  template <uint8_t MIN> bool advance_pattern_pin3_pmh(size_t loc);
  bool advance_pattern_pin4_one(size_t loc);
  bool advance_pattern_pin4_pma(size_t loc);
  template <uint8_t MIN> bool advance_pattern_pin4_pmh(size_t loc);
  bool advance_pattern_pin5_one(size_t loc);
  bool advance_pattern_pin5_pma(size_t loc);
  template <uint8_t MIN> bool advance_pattern_pin5_pmh(size_t loc);
  bool advance_pattern_pin6_one(size_t loc);
  bool advance_pattern_pin6_pma(size_t loc);
  template <uint8_t MIN> bool advance_pattern_pin6_pmh(size_t loc);
  bool advance_pattern_pin7_one(size_t loc);
  bool advance_pattern_pin7_pma(size_t loc);
  template <uint8_t MIN> bool advance_pattern_pin7_pmh(size_t loc);
  bool advance_pattern_pin8_one(size_t loc);
  bool advance_pattern_pin8_pma(size_t loc);
  template <uint8_t MIN> bool advance_pattern_pin8_pmh(size_t loc);
  // Single needle AVX2 methods
  bool simd_advance_pattern_pin1_pma_avx2(size_t loc);
  template <uint8_t MIN> bool simd_advance_pattern_pin1_pmh_avx2(size_t loc);
  // Generated AVX2 multi-needle methods
  bool simd_advance_pattern_pin2_one_avx2(size_t loc);
  bool simd_advance_pattern_pin2_pma_avx2(size_t loc);
  template <uint8_t MIN> bool simd_advance_pattern_pin2_pmh_avx2(size_t loc);
  bool simd_advance_pattern_pin3_one_avx2(size_t loc);
  bool simd_advance_pattern_pin3_pma_avx2(size_t loc);
  template <uint8_t MIN> bool simd_advance_pattern_pin3_pmh_avx2(size_t loc);
  bool simd_advance_pattern_pin4_one_avx2(size_t loc);
  bool simd_advance_pattern_pin4_pma_avx2(size_t loc);
  template <uint8_t MIN> bool simd_advance_pattern_pin4_pmh_avx2(size_t loc);
  bool simd_advance_pattern_pin5_one_avx2(size_t loc);
  bool simd_advance_pattern_pin5_pma_avx2(size_t loc);
  template <uint8_t MIN> bool simd_advance_pattern_pin5_pmh_avx2(size_t loc);
  bool simd_advance_pattern_pin6_one_avx2(size_t loc);
  bool simd_advance_pattern_pin6_pma_avx2(size_t loc);
  template <uint8_t MIN> bool simd_advance_pattern_pin6_pmh_avx2(size_t loc);
  bool simd_advance_pattern_pin7_one_avx2(size_t loc);
  bool simd_advance_pattern_pin7_pma_avx2(size_t loc);
  template <uint8_t MIN> bool simd_advance_pattern_pin7_pmh_avx2(size_t loc);
  bool simd_advance_pattern_pin8_one_avx2(size_t loc);
  bool simd_advance_pattern_pin8_pma_avx2(size_t loc);
  template <uint8_t MIN> bool simd_advance_pattern_pin8_pmh_avx2(size_t loc);
  bool simd_advance_pattern_pin16_one_avx2(size_t loc);
  bool simd_advance_pattern_pin16_pma_avx2(size_t loc);
  template <uint8_t MIN> bool simd_advance_pattern_pin16_pmh_avx2(size_t loc);
  // Minimal patterns
  bool advance_pattern_min1(size_t loc);
  bool advance_pattern_min2(size_t loc);
  bool advance_pattern_min3(size_t loc);
  template <uint8_t MIN> bool advance_pattern_min4(size_t loc);
#ifdef WITH_BITAP_AVX2 // use in case vectorized bitap (hashed) is faster than serial version (typically not!!)
  // Minimal 4 byte long patterns using bitap hashed pairs with AVX2
  template <uint8_t MIN> bool simd_advance_pattern_min4_avx2(size_t loc);
#endif
  // Minimal patterns
  bool advance_pattern_pma(size_t loc);
  // One char methods
  bool advance_char(size_t loc);
  bool advance_char_pma(size_t loc);
  bool advance_char_pmh(size_t loc);
  // Few chars methods
  template <uint8_t LEN> bool advance_chars(size_t loc);
  template <uint8_t LEN> bool advance_chars_pma(size_t loc);
  template <uint8_t LEN> bool advance_chars_pmh(size_t loc);
  // Few chars AVX2 methods
  template <uint8_t LEN> bool simd_advance_chars_avx2(size_t loc);
  template <uint8_t LEN> bool simd_advance_chars_pma_avx2(size_t loc);
  template <uint8_t LEN> bool simd_advance_chars_pmh_avx2(size_t loc);
  // Few chars AVX512BW methods
  template <uint8_t LEN> bool simd_advance_chars_avx512bw(size_t loc);
  template <uint8_t LEN> bool simd_advance_chars_pma_avx512bw(size_t loc);
  template <uint8_t LEN> bool simd_advance_chars_pmh_avx512bw(size_t loc);
  // String methods
  bool advance_string(size_t loc);
  bool advance_string_pma(size_t loc);
  bool advance_string_pmh(size_t loc);
  // String AVX2 metnods
  bool simd_advance_string_avx2(size_t loc);
  bool simd_advance_string_pma_avx2(size_t loc);
  bool simd_advance_string_pmh_avx2(size_t loc);
  // String AVX512BW metnods
  bool simd_advance_string_avx512bw(size_t loc);
  bool simd_advance_string_pma_avx512bw(size_t loc);
  bool simd_advance_string_pmh_avx512bw(size_t loc);
  // String NEON metnods
  bool simd_advance_string_neon(const char *&s, const char *e);
  bool simd_advance_string_pma_neon(const char *&s, const char *e);
  bool simd_advance_string_pmh_neon(const char *&s, const char *e);
  // Fallback Boyer-Moore methods
  bool advance_string_bm(size_t loc);
  bool advance_string_bm_pma(size_t loc);
  bool advance_string_bm_pmh(size_t loc);
#if !defined(WITH_NO_INDENT)
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
  /// Returns true if nodent.
  inline bool nodent()
    /// @returns true if nodent
  {
    newline();
    return (col_ <= 0 || (!tab_.empty() && tab_.back() >= col_)) && (tab_.empty() || tab_.back() <= col_);
  }
#endif
  size_t            ded_; ///< dedent count
  size_t            col_; ///< column counter for indent matching, updated by newline(), indent(), and dedent()
  Stops             tab_; ///< tab stops set by detecting indent margins
  std::vector<int>  lap_; ///< lookahead position in input that heads a lookahead match (indexed by lookahead number)
  std::stack<Stops> stk_; ///< stack to push/pop stops
  FSM               fsm_; ///< local state for FSM code
  bool (Matcher::*  adv_)(size_t loc); ///< advance FIND function pointer
  bool              mrk_; ///< indent \i or dedent \j in pattern found: should check and update indent stops
};

} // namespace reflex

#endif
