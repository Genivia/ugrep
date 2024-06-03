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
@file      pattern.cpp
@brief     RE/flex regular expression pattern compiler
@author    Robert van Engelen - engelen@genivia.com
@copyright (c) 2016-2023, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt
*/

#include <reflex/pattern.h>
#include <reflex/simd.h>
#include <reflex/timer.h>
#include <algorithm>
#include <cstdlib>
#include <cerrno>
#include <cmath>

/// DFA compaction: -1 == reverse order edge compression (best); 1 == edge compression; 0 == no edge compression.
/** Edge compression reorders edges to produce fewer tests when executed in the compacted order.
    For example ([a-cg-ik]|d|[e-g]|j|y|[x-z]) after reverse edge compression has only 2 edges:
    c = m.FSM_CHAR();
    if ('x' <= c && c <= 'z') goto S3;
    if ('a' <= c && c <= 'k') goto S3;
    return m.FSM_HALT(c);
*/
#define WITH_COMPACT_DFA -1

#ifdef DEBUG
# define DBGLOGPOS(p) \
  if ((p).accept()) \
  { \
    DBGLOGA(" (%u)", (p).accepts()); \
    if ((p).lazy()) \
      DBGLOGA("?%u", (p).lazy()); \
  } \
  else \
  { \
    DBGLOGA(" "); \
    if ((p).iter()) \
      DBGLOGA("%u.", (p).iter()); \
    DBGLOGA("%u", (p).loc()); \
    if ((p).lazy()) \
      DBGLOGA("?%u", (p).lazy()); \
    if ((p).anchor()) \
      DBGLOGA("^"); \
    if ((p).ticked()) \
      DBGLOGA("'"); \
    if ((p).negate()) \
      DBGLOGA("-"); \
  }
#endif

namespace reflex {

#if (defined(__WIN32__) || defined(_WIN32) || defined(WIN32) || defined(_WIN64) || defined(__BORLANDC__)) && !defined(__CYGWIN__) && !defined(__MINGW32__) && !defined(__MINGW64__)
inline int fopen_s(FILE **file, const char *name, const char *mode) { return ::fopen_s(file, name, mode); }
#else
inline int fopen_s(FILE **file, const char *name, const char *mode) { return (*file = ::fopen(name, mode)) ? 0 : errno; }
#endif

#ifndef WITH_NO_CODEGEN
static void print_char(FILE *file, int c, bool h = false)
{
  if (c >= '\a' && c <= '\r')
    ::fprintf(file, "'\\%c'", "abtnvfr"[c - '\a']);
  else if (c == '\\')
    ::fprintf(file, "'\\\\'");
  else if (c == '\'')
    ::fprintf(file, "'\\''");
  else if (std::isprint(c))
    ::fprintf(file, "'%c'", c);
  else if (h)
    ::fprintf(file, "%02x", c);
  else
    ::fprintf(file, "%u", c);
}
#endif

#ifndef WITH_NO_CODEGEN
static const char *meta_label[] = {
  NULL,
  "WBB",
  "WBE",
  "NWB",
  "NWE",
  "BWB",
  "EWB",
  "BWE",
  "EWE",
  "BOL",
  "EOL",
  "BOB",
  "EOB",
  "UND",
  "IND",
  "DED",
};
#endif

static const char *posix_class[] = {
  "ASCII",
  "Space",
  "XDigit",
  "Cntrl",
  "Print",
  "Alnum",
  "Alpha",
  "Blank",
  "Digit",
  "Graph",
  "Lower",
  "Punct",
  "Upper",
  "Word",
};

const std::string Pattern::operator[](Accept choice) const
{
  if (choice == 0)
    return rex_;
  if (choice <= size())
  {
    Location loc = end_.at(choice - 1);
    Location prev = 0;
    if (choice >= 2)
      prev = end_.at(choice - 2) + 1;
    return rex_.substr(prev, loc - prev);
  }
  return "";
}

void Pattern::error(regex_error_type code, size_t pos) const
{
  regex_error err(code, rex_, pos);
  if (opt_.w)
    std::cerr << err.what();
  if (code == regex_error::exceeds_length || code == regex_error::exceeds_limits || opt_.r)
    throw err;
}

void Pattern::init(const char *options, const uint8_t *pred)
{
  init_options(options);
  nop_ = 0;
  len_ = 0;
  min_ = 0;
  pin_ = 0;
  lcp_ = 0;
  lcs_ = 0;
  bmd_ = 0;
  npy_ = 0;
  one_ = false;
  bol_ = false;
  vno_ = 0;
  eno_ = 0;
  hno_ = 0;
  pms_ = 0.0;
  vms_ = 0.0;
  ems_ = 0.0;
  wms_ = 0.0;
  ams_ = 0.0;
  cut_ = 0;
  lbk_ = 0;
  lbm_ = 0;
  cbk_.reset();
  fst_.reset();
  if (opc_ != NULL || fsm_ != NULL )
  {
    if (pred != NULL)
    {
      len_ = pred[0];
      min_ = pred[1] & 0x0f;
      one_ = pred[1] & 0x10;
      bol_ = pred[1] & 0x40;
      memcpy(chr_, pred + 2, len_);
      size_t n = 2 + len_;
      if (len_ == 0)
      {
        // get bitap bit_[] parameters
        for (size_t i = 0; i < 256; ++i)
          bit_[i] = ~pred[i + n];
        n += 256;
      }
      if (min_ < 4)
      {
        // get predict match PM4 pma_[] parameters
        for (size_t i = 0; i < Const::HASH; ++i)
          pma_[i] = ~pred[i + n];
      }
      else
      {
        // get predict match hash pmh_[] parameters
        for (size_t i = 0; i < Const::HASH; ++i)
          pmh_[i] = ~pred[i + n];
      }
      n += Const::HASH;
      if ((pred[1] & 0x20) != 0)
      {
        // get lookback parameters lbk_ lbm_ and cbk_[] after s-t cut and first s-t cut pattern characters fst_[]
        lbk_ = pred[n + 0] | (pred[n + 1] << 8);
        lbm_ = pred[n + 2] | (pred[n + 3] << 8);
        for (size_t i = 0; i < 256; ++i)
          cbk_.set(i, pred[n + 4 + (i >> 3)] & (1 << (i & 7)));
        for (size_t i = 0; i < 256; ++i)
          fst_.set(i, pred[n + 4 + 32 + (i >> 3)] & (1 << (i & 7)));
        n += 4 + 32 + 32;
      }
      else
      {
        // get first pattern characters fst_[] from bitap
        for (size_t i = 0; i < 256; ++i)
          fst_.set(i, (bit_[i] & 1) == 0);
      }
    }
  }
  else
  {
    Positions startpos;
    Follow    followpos;
    Lazypos   lazypos;
    Mods      modifiers;
    Map       lookahead;
    // parse the regex pattern to construct the followpos NFA without epsilon transitions
    parse(startpos, followpos, lazypos, modifiers, lookahead);
    // start state = startpos = firstpost of the followpos NFA, also merge the tree DFA root when non-NULL
#ifdef WITH_TREE_DFA
    DFA::State *start;
    if (startpos.empty())
    {
      // all patterns are strings, do not construct a DFA with subset construction
      start = tfa_.root();
      if (opt_.i)
      {
        // convert edges to case-insensitive by adding upper case transitions for alphas normalized to lower case
        timer_type et;
        timer_start(et);
        for (DFA::State *state = start; state != NULL; state = state->next)
        {
          for (DFA::State::Edges::iterator t = state->edges.begin(); t != state->edges.end(); ++t)
          {
            Char c = t->first;
            if (c >= 'a' && c <= 'z')
            {
              state->edges[uppercase(c)] = DFA::State::Edge(uppercase(c), t->second.second);
              ++eno_;
            }
          }
        }
        ems_ += timer_elapsed(et);
      }
    }
    else
    {
      // combine tree DFA (if any) with the DFA start state to construct a combined DFA with subset construction
      start = dfa_.state(tfa_.root(), startpos);
      // compile the NFA into a DFA
      compile(start, followpos, lazypos, modifiers, lookahead);
    }
#else
    DFA::State *start = dfa_.state(tfa_.tree, startpos);
    // compile the NFA into a DFA
    compile(start, followpos, lazypos, modifiers, lookahead);
#endif
    // assemble DFA opcode tables or direct code
    assemble(start);
    // delete the DFA
    dfa_.clear();
    // delete the tree DFA
    tfa_.clear();
  }
  // clean up bitap and compute bitap entropy
  if (len_ == 0)
  {
    // bitap entropy to estimate false positive rate
    npy_ = 0;
    if (min_ > 0)
    {
      for (Char i = 0; i < 256; ++i)
      {
        bit_[i] |= ~((1 << min_) - 1);
        npy_ += (bit_[i] & 0x01) == 0;
        npy_ += (bit_[i] & 0x02) == 0;
        npy_ += (bit_[i] & 0x04) == 0;
        npy_ += (bit_[i] & 0x08) == 0;
        npy_ += (bit_[i] & 0x10) == 0;
        npy_ += (bit_[i] & 0x20) == 0;
        npy_ += (bit_[i] & 0x40) == 0;
        npy_ += (bit_[i] & 0x80) == 0;
      }
      // average entropy per bitap position, we don't use bitap when entropy is too high for short patterns
      npy_ /= min_;
    }
    // needle count and frequency thresholds to enable needle-based search
    uint16_t pinmax = 8;
    uint8_t freqmax = 251;
#if defined(HAVE_AVX512BW) || defined(HAVE_AVX2) || defined(HAVE_SSE2)
    if (have_HW_AVX512BW() || have_HW_AVX2())
      pinmax = 16;
    else if (have_HW_SSE2())
      pinmax = 8;
    else
      pinmax = 1;
#elif defined(HAVE_NEON)
    pinmax = 8;
#else
    pinmax = 1;
#endif
    // get needles
    pin_ = 0;
    lcp_ = 0;
    lcs_ = 0;
    uint16_t nlcp = 65535; // max and undefined
    uint16_t nlcs = 65535; // max and undefined
    uint8_t freqlcp = 255; // max
    uint8_t freqlcs = 255; // max
    size_t min = (min_ == 0 ? 1 : min_);
    for (uint16_t k = 0; k < min; ++k)
    {
      Pred mask = 1 << k;
      uint16_t n = 0;
      uint8_t max = 0;
      // at position k count the matching characters and find the max character frequency
      for (uint16_t i = 0; i < 256; ++i)
      {
        if ((bit_[i] & mask) == 0)
        {
          ++n;
          uint8_t freq = frequency(static_cast<uint8_t>(i));
          if (freq > max)
            max = freq;
        }
      }
      if (n <= pinmax)
      {
        // pick the fewest and rarest (less frequently occurring) needles to search
        if (max < freqlcp || (n < nlcp && max == freqlcp))
        {
          lcs_ = lcp_;
          nlcs = nlcp;
          freqlcs = freqlcp;
          lcp_ = static_cast<uint8_t>(k);
          nlcp = n;
          freqlcp = max;
        }
        else if (n < nlcs ||
            (n == nlcs &&
             (max < freqlcs ||
              abs(static_cast<int>(lcp_) - static_cast<int>(lcs_)) < abs(static_cast<int>(lcp_) - static_cast<int>(k)))))
        {
          lcs_ = static_cast<uint8_t>(k);
          nlcs = n;
          freqlcs = max;
        }
      }
    }
    // one position to pin: make lcp and lcs equal to 0 (only one position at 0)
    if (min == 1 || nlcs == 65535)
    {
      nlcs = nlcp;
      lcs_ = lcp_;
    }
    // number of needles required
    uint16_t n = nlcp > nlcs ? nlcp : nlcs;
    DBGLOG("min=%zu lcp=%hu(%hu) pin=%hu nlcp=%hu(%hu) freq=%hu(%hu) freqsum=%hu npy=%zu", min, lcp_, lcs_, n, nlcp, nlcs, freqlcp, freqlcs, freqsum, npy_);
    // determine if a needle-based search is worthwhile, below or meeting the thresholds
    if (n <= pinmax && freqlcp <= freqmax)
    {
      // bridge the gap from 9 to 16 to handle 9 to 16 combined
      if (n > 8)
        n = 16;
      uint16_t j = 0, k = n;
      Pred masklcp = 1 << lcp_;
      Pred masklcs = 1 << lcs_;
      for (uint16_t i = 0; i < 256; ++i)
      {
        if ((bit_[i] & masklcp) == 0)
          chr_[j++] = static_cast<uint8_t>(i);
        if ((bit_[i] & masklcs) == 0)
          chr_[k++] = static_cast<uint8_t>(i);
      }
      // fill up the rest of the character tables with duplicates if necessary
      for (; j < n; ++j)
        chr_[j] = chr_[j - 1];
      for (; k < 2*n; ++k)
        chr_[k] = chr_[k - 1];
      pin_ = n;
    }
  }
  else if (len_ > 1)
  {
    // produce lcp and lcs positions and Boyer-Moore bms_[] shifts when bmd_ > 0
    uint8_t n = static_cast<uint8_t>(len_); // okay to cast: actually never more than 255
    uint16_t i;
    for (i = 0; i < 256; ++i)
      bms_[i] = n;
    lcp_ = 0;
    lcs_ = 1;
    for (i = 0; i < n; ++i)
    {
      uint8_t pch = static_cast<uint8_t>(chr_[i]);
      bms_[pch] = static_cast<uint8_t>(n - i - 1);
      if (i > 0)
      {
        uint8_t freqpch = frequency(pch);
        uint8_t lcpch = static_cast<uint8_t>(chr_[lcp_]);
        uint8_t lcsch = static_cast<uint8_t>(chr_[lcs_]);
        if (frequency(lcpch) > freqpch)
        {
          lcs_ = lcp_;
          lcp_ = i;
        }
        else if (frequency(lcsch) > freqpch ||
            (frequency(lcsch) == freqpch &&
             abs(static_cast<int>(lcp_) - static_cast<int>(lcs_)) < abs(static_cast<int>(lcp_) - static_cast<int>(i))))
        {
          lcs_ = i;
        }
      }
    }
    uint16_t j;
    for (i = n - 1, j = i; j > 0; --j)
      if (chr_[j - 1] == chr_[i])
        break;
    bmd_ = i - j + 1;
#if defined(HAVE_AVX512BW) || defined(HAVE_AVX2) || defined(HAVE_SSE2) || defined(__SSE2__) || defined(__x86_64__) || _M_IX86_FP == 2 || !defined(HAVE_NEON)
    size_t score = 0;
    for (i = 0; i < n; ++i)
      score += bms_[static_cast<uint8_t>(chr_[i])];
    score /= n;
    uint8_t fch = frequency(static_cast<uint8_t>(chr_[lcp_]));
#if defined(HAVE_AVX512BW) || defined(HAVE_AVX2) || defined(HAVE_SSE2)
    if (!have_HW_SSE2() && !have_HW_AVX2() && !have_HW_AVX512BW())
    {
      // SSE2/AVX2 not available: if B-M scoring is high and freq is high, then use our improved Boyer-Moore
      if (score > 1 && fch > 35 && (score > 4 || fch > 50) && fch + score > 52)
        lcs_ = 0xffff; // use B-M
    }
#elif defined(__SSE2__) || defined(__x86_64__) || _M_IX86_FP == 2
    // SSE2 is available: only if B-M scoring is high and freq is high, then use our improved Boyer-Moore
    if (score > 1 && fch > 35 && (score > 4 || fch > 50) && fch + score > 52)
      lcs_ = 0xffff; // use B-M
#elif !defined(HAVE_NEON)
    // no SIMD available: if B-M scoring is high and freq is high, then use our improved Boyer-Moore
    if (score > 1 && fch > 35 && (score > 3 || fch > 50) && fch + score > 52)
      lcs_ = 0xffff; // use B-M
#endif
#endif
    if (lcs_ < 0xffff)
    {
      // do not use B-M
      bmd_ = 0;
      // spread lcp and lcs apart if lcp and lcs are adjacent (chars are possibly correlated)
      if (len_ == 3 && (lcp_ == 1 || lcs_ == 1))
      {
        lcp_ = 0;
        lcs_ = 2;
      }
      else if (len_ > 3 && (lcp_ + 1 == lcs_ || lcs_ + 1 == lcp_))
      {
        uint8_t freqlcs = 255;
        for (i = 0; i < n; ++i)
        {
          if (i > lcp_ + 1 || i + 1 < lcp_)
          {
            uint8_t pch = static_cast<uint8_t>(chr_[i]);
            uint8_t freqpch = frequency(pch);
            if (freqlcs > freqpch)
            {
              lcs_ = i;
              freqlcs = freqpch;
            }
          }
        }
      }
    }
    DBGLOG("len=%zu bmd=%zu lcp=%hu(%hu)", len_, bmd_, lcp_, lcs_);
  }
}

void Pattern::init_options(const char *options)
{
  opt_.b = false;
  opt_.h = false;
  opt_.g = 0;
  opt_.i = false;
  opt_.m = false;
  opt_.o = false;
  opt_.p = false;
  opt_.q = false;
  opt_.r = false;
  opt_.s = false;
  opt_.w = false;
  opt_.x = false;
  opt_.e = '\\';
  if (options != NULL)
  {
    for (const char *s = options; *s != '\0'; ++s)
    {
      switch (*s)
      {
        case 'b':
          opt_.b = true;
          break;
        case 'e':
          opt_.e = (*(s += (s[1] == '=') + 1) == ';' || *s == '\0' ? 256 : *s++);
          --s;
          break;
        case 'g':
          ++opt_.g;
          break;
        case 'h':
          opt_.h = true;
          break;
        case 'i':
          opt_.i = true;
          break;
        case 'm':
          opt_.m = true;
          break;
        case 'o':
          opt_.o = true;
          break;
        case 'p':
          opt_.p = true;
          break;
        case 'q':
          opt_.q = true;
          break;
        case 'r':
          opt_.r = true;
          break;
        case 's':
          opt_.s = true;
          break;
        case 'w':
          opt_.w = true;
          break;
        case 'x':
          opt_.x = true;
          break;
        case 'z':
          for (const char *t = s += (s[1] == '='); *s != ';' && *s != '\0'; ++t)
          {
            if (std::isspace(static_cast<unsigned char>(*t)) || *t == ';' || *t == '\0')
            {
              if (t > s + 1)
                opt_.z = std::string(s + 1, t - s - 1);
              s = t;
            }
          }
          --s;
          break;
        case 'f':
        case 'n':
          for (const char *t = s += (s[1] == '='); *s != ';' && *s != '\0'; ++t)
          {
            if (*t == ',' || *t == ';' || *t == '\0')
            {
              if (t > s + 1)
              {
                std::string name(s + 1, t - s - 1);
                if (name.find('.') == std::string::npos)
                  opt_.n = name;
                else
                  opt_.f.push_back(name);
              }
              s = t;
            }
          }
          --s;
          break;
      }
    }
  }
}

void Pattern::parse(
    Positions& startpos,
    Follow&    followpos,
    Lazypos&   lazypos,
    Mods       modifiers,
    Map&       lookahead)
{
  DBGLOG("BEGIN parse()");
  if (rex_.size() > Position::MAXLOC)
    throw regex_error(regex_error::exceeds_length, rex_, Position::MAXLOC);
  Location   len = static_cast<Location>(rex_.size());
  Location   loc = 0;
  Accept     choice = 1;
  Lazy       lazyidx = 0;
  Positions  firstpos;
  Positions  lastpos;
  bool       nullable;
  Iter       iter;
#ifdef WITH_TREE_DFA
  DFA::State *last_state = NULL;
#endif
  timer_type t;
  timer_start(t);
  if (at(0) == '(' && at(1) == '?')
  {
    loc = 2;
    while (at(loc) == '-' || std::isalnum(at(loc)))
      ++loc;
    if (at(loc) == ')')
    {
      bool active = true;
      loc = 2;
      Char c;
      while ((c = at(loc)) != ')')
      {
        c = at(loc);
        if (c == '-')
          active = false;
        else if (c == 'i')
          opt_.i = active;
        else if (c == 'm')
          opt_.m = active;
        else if (c == 'q')
          opt_.q = active;
        else if (c == 's')
          opt_.s = active;
        else if (c == 'x')
          opt_.x = active;
        else
          error(regex_error::invalid_modifier, loc);
        ++loc;
      }
      ++loc;
    }
    else
    {
      loc = 0;
    }
  }
  bol_ = at(loc) == '^';
  do
  {
    Location end = loc;
    if (!opt_.q && !opt_.x)
    {
      while (true)
      {
        Char c = at(end);
        if (c == '\0' || c == '|')
          break;
        if (c == '.' || c == '^' || c == '$' ||
            c == '(' || c == '[' || c == '{' ||
            c == '?' || c == '*' || c == '+' ||
            c == ')')
        {
          end = loc;
          break;
        }
        if (c == opt_.e)
        {
          c = at(++end);
          if (c == '\0' || std::strchr("0123456789<>ABDHLNPSUWXbcdehijklpsuwxz", c) != NULL)
          {
            end = loc;
            break;
          }
          if (c == 'Q')
          {
            while ((c = at(++end)) != '\0')
              if (c == opt_.e && at(end + 1) == 'E')
                break;
          }
        }
        ++end;
      }
    }
    if (loc < end)
    {
      // string pattern found w/o regex metas: merge string into the tree DFA
      bool quote = false;
#ifdef WITH_TREE_DFA
      DFA::State *t = tfa_.start();
#else
      Tree::Node *t = tfa_.root();
#endif
      while (loc < end)
      {
        Char c = at(loc++);
        if (c == opt_.e)
        {
          if (at(loc) == 'E')
          {
            quote = false;
            ++loc;
            continue;
          }
          if (!quote)
          {
            if (at(loc) == 'Q')
            {
              quote = true;
              ++loc;
              continue;
            }
            static const char abtnvfr[] = "abtnvfr";
            c = at(loc++);
            const char *s = std::strchr(abtnvfr, c);
            if (s != NULL)
              c = static_cast<Char>(s - abtnvfr + '\a');
          }
        }
        else if (c >= 'A' && c <= 'Z' && opt_.i)
        {
          c = lowercase(c);
        }
#ifdef WITH_TREE_DFA
        DFA::State::Edges::iterator i = t->edges.find(c);
        if (i == t->edges.end())
        {
          if (last_state == NULL)
            last_state = t; // t points to the tree DFA start state
          DFA::State *target_state = last_state = last_state->next = tfa_.state();
          t->edges[c] = DFA::State::Edge(c, target_state);
          t = target_state;
          ++eno_;
          ++vno_;
        }
        else
        {
          t = i->second.second;
        }
#else
        t = tfa_.edge(t, c);
#endif
      }
      if (t->accept == 0)
        t->accept = choice;
#ifdef WITH_TREE_DFA
      acc_.resize(choice, false);
      acc_[choice - 1] = true;
#endif
    }
    else
    {
      if (at(loc) != '^')
        bol_ = false;
      parse2(
          true,
          loc,
          firstpos,
          lastpos,
          nullable,
          followpos,
          lazyidx,
          lazypos,
          modifiers,
          lookahead[choice],
          iter);
      pos_insert(startpos, firstpos);
      if (nullable)
        pos_add(startpos, Position(choice).accept(true));
      if (lazypos.empty())
      {
        for (Positions::const_iterator p = lastpos.begin(); p != lastpos.end(); ++p)
          pos_add(followpos[p->pos()], Position(choice).accept(true));
      }
      else
      {
        for (Positions::const_iterator p = lastpos.begin(); p != lastpos.end(); ++p)
          for (Lazypos::const_iterator l = lazypos.begin(); l != lazypos.end(); ++l)
            pos_add(followpos[p->pos()], Position(choice).accept(true).lazy(l->lazy()));
      }
    }
    if (++choice == 0)
      error(regex_error::exceeds_limits, loc); // overflow: too many top-level alternations (should never happen)
    end_.push_back(loc);
  } while (at(loc++) == '|');
  --loc;
  if (at(loc) == ')')
    error(regex_error::mismatched_parens, loc);
  else if (at(loc) != 0)
    error(regex_error::invalid_syntax, loc);
  if (opt_.i)
    update_modified(ModConst::i, modifiers, 0, len);
  if (opt_.m)
    update_modified(ModConst::m, modifiers, 0, len);
  if (opt_.s)
    update_modified(ModConst::s, modifiers, 0, len);
  pms_ = timer_elapsed(t);
#ifdef DEBUG
  DBGLOGN("startpos = {");
  for (Positions::const_iterator p = startpos.begin(); p != startpos.end(); ++p)
    DBGLOGPOS(*p);
  DBGLOGA(" }");
  for (Follow::const_iterator fp = followpos.begin(); fp != followpos.end(); ++fp)
  {
    DBGLOGN("followpos(");
    DBGLOGPOS(fp->first);
    DBGLOGA(" ) = {");
    for (Positions::const_iterator p = fp->second.begin(); p != fp->second.end(); ++p)
      DBGLOGPOS(*p);
    DBGLOGA(" }");
  }
#endif
  DBGLOG("END parse()");
}

void Pattern::parse1(
    bool       begin,
    Location&  loc,
    Positions& firstpos,
    Positions& lastpos,
    bool&      nullable,
    Follow&    followpos,
    Lazy&      lazyidx,
    Lazypos&   lazypos,
    Mods       modifiers,
    Locations& lookahead,
    Iter&      iter)
{
  DBGLOG("BEGIN parse1(%u)", loc);
  parse2(
      begin,
      loc,
      firstpos,
      lastpos,
      nullable,
      followpos,
      lazyidx,
      lazypos,
      modifiers,
      lookahead,
      iter);
  Positions firstpos1;
  Positions lastpos1;
  bool      nullable1;
  Lazypos   lazypos1;
  Iter      iter1;
  while (at(loc) == '|')
  {
    ++loc;
    parse2(
        begin,
        loc,
        firstpos1,
        lastpos1,
        nullable1,
        followpos,
        lazyidx,
        lazypos1,
        modifiers,
        lookahead,
        iter1);
    pos_insert(firstpos, firstpos1);
    pos_insert(lastpos, lastpos1);
    lazy_insert(lazypos, lazypos1);
    if (nullable1)
      nullable = true;
    if (iter1 > iter)
      iter = iter1;
  }
  DBGLOG("END parse1()");
}

void Pattern::parse2(
    bool       begin,
    Location&  loc,
    Positions& firstpos,
    Positions& lastpos,
    bool&      nullable,
    Follow&    followpos,
    Lazy&      lazyidx,
    Lazypos&   lazypos,
    Mods       modifiers,
    Locations& lookahead,
    Iter&      iter)
{
  DBGLOG("BEGIN parse2(%u)", loc);
  Positions a_pos;
  Char      c;
  if (begin)
  {
    while (true)
    {
      if (opt_.x)
        while (std::isspace(at(loc)))
          ++loc;
      if (at(loc) == '^')
      {
        pos_add(a_pos, Position(loc++));
        begin = false;
      }
      else if (escapes_at(loc, "ABb<>"))
      {
        pos_add(a_pos, Position(loc));
        loc += 2;
        begin = false;
      }
      else
      {
        if (escapes_at(loc, "ij"))
          begin = false;
        break;
      }
    }
  }
  if (begin || ((c = at(loc)) != '\0' && c != '|' && c != ')'))
  {
    parse3(
        begin,
        loc,
        firstpos,
        lastpos,
        nullable,
        followpos,
        lazyidx,
        lazypos,
        modifiers,
        lookahead,
        iter);
    Positions firstpos1;
    Positions lastpos1;
    bool      nullable1;
    Lazypos   lazypos1;
    Iter      iter1;
    while ((c = at(loc)) != '\0' && c != '|' && c != ')')
    {
      parse3(
          false,
          loc,
          firstpos1,
          lastpos1,
          nullable1,
          followpos,
          lazyidx,
          lazypos1,
          modifiers,
          lookahead,
          iter1);
      if (nullable)
        pos_insert(firstpos, firstpos1);
      for (Positions::const_iterator p = lastpos.begin(); p != lastpos.end(); ++p)
        pos_insert(followpos[p->pos()], firstpos1);
      if (nullable1)
      {
        pos_insert(lastpos, lastpos1);
      }
      else
      {
        lastpos.swap(lastpos1);
        nullable = false;
      }
      lazy_insert(lazypos, lazypos1);
      if (iter1 > iter)
        iter = iter1;
    }
  }
  for (Positions::iterator p = a_pos.begin(); p != a_pos.end(); ++p)
  {
    for (Positions::const_iterator k = lastpos.begin(); k != lastpos.end(); ++k)
      if (at(k->loc()) == ')' && lookahead.find(k->loc()) != lookahead.end())
        pos_add(followpos[p->pos()], *k);
    if (lazypos.empty())
    {
      for (Positions::const_iterator k = lastpos.begin(); k != lastpos.end(); ++k)
        pos_add(followpos[k->pos()], p->anchor(!nullable || k->pos() != p->pos()));
    }
    else
    {
      // make the starting anchors at positions a_pos lazy
      for (Lazypos::const_iterator l = lazypos.begin(); l != lazypos.end(); ++l)
        for (Positions::const_iterator k = lastpos.begin(); k != lastpos.end(); ++k)
          pos_add(followpos[k->pos()], p->lazy(l->lazy()).anchor(!nullable || k->pos() != p->pos()));
    }
    lastpos.clear();
    pos_add(lastpos, *p);
    if (nullable || firstpos.empty())
    {
      pos_add(firstpos, *p);
      nullable = false;
    }
  }
  DBGLOG("END parse2()");
}

void Pattern::parse3(
    bool       begin,
    Location&  loc,
    Positions& firstpos,
    Positions& lastpos,
    bool&      nullable,
    Follow&    followpos,
    Lazy&      lazyidx,
    Lazypos&   lazypos,
    Mods       modifiers,
    Locations& lookahead,
    Iter&      iter)
{
  DBGLOG("BEGIN parse3(%u)", loc);
  Position b_pos(loc);
  parse4(
      begin,
      loc,
      firstpos,
      lastpos,
      nullable,
      followpos,
      lazyidx,
      lazypos,
      modifiers,
      lookahead,
      iter);
  Char c = at(loc);
  if (opt_.x)
    while (std::isspace(c))
      c = at(++loc);
  while (true)
  {
    if (c == '*' || c == '+' || c == '?')
    {
      if (c == '*' || c == '?')
        nullable = true;
      if (at(++loc) == '?')
      {
        if (++lazyidx == 0)
          error(regex_error::exceeds_limits, loc); // overflow: exceeds max 255 lazy quantifiers
        lazy_add(lazypos, lazyidx, loc);
        lazy(lazypos, firstpos);
        ++loc;
      }
      else if (c != '?' && !lazypos.empty())
      {
        greedy(firstpos);
      }
      if (c != '?')
        for (Positions::const_iterator p = lastpos.begin(); p != lastpos.end(); ++p)
          pos_insert(followpos[p->pos()], firstpos);
    }
    else if (c == '{') // {n,m} repeat min n times to max m
    {
      size_t k = 0;
      for (Location i = 0; i < 7 && std::isdigit(c = at(++loc)); ++i)
        k = 10 * k + (c - '0');
      if (k > Position::MAXITER)
        error(regex_error::exceeds_limits, loc);
      Iter n = static_cast<Iter>(k);
      Iter m = n;
      bool unlimited = false;
      if (at(loc) == ',')
      {
        if (std::isdigit(at(loc + 1)))
        {
          m = 0;
          for (Location i = 0; i < 7 && std::isdigit(c = at(++loc)); ++i)
            m = 10 * m + (c - '0');
        }
        else
        {
          unlimited = true;
          ++loc;
        }
      }
      if (at(loc) == '}')
      {
        bool nullable1 = nullable;
        if (n == 0)
          nullable = true;
        if (n > m)
          error(regex_error::invalid_repeat, loc);
        if (at(++loc) == '?')
        {
          if (++lazyidx == 0)
            error(regex_error::exceeds_limits, loc); // overflow: exceeds max 255 lazy quantifiers
          lazy_add(lazypos, lazyidx, loc);
          lazy(lazypos, firstpos);
          ++loc;
        }
        if (nullable && unlimited) // {0,} == *
        {
          for (Positions::const_iterator p = lastpos.begin(); p != lastpos.end(); ++p)
            pos_insert(followpos[p->pos()], firstpos);
        }
        else if (m > 0)
        {
          if (iter * m > Position::MAXITER)
            error(regex_error::exceeds_limits, loc);
          // update followpos by virtually repeating sub-regex m-1 times
          Follow followpos1;
          for (Follow::const_iterator fp = followpos.begin(); fp != followpos.end(); ++fp)
            if (fp->first.loc() >= b_pos)
              for (Iter i = 0; i < m - 1; ++i)
                for (Positions::const_iterator p = fp->second.begin(); p != fp->second.end(); ++p)
                  pos_add(followpos1[fp->first.iter(iter * (i + 1))], p->iter(iter * (i + 1)));
          for (Follow::const_iterator fp = followpos1.begin(); fp != followpos1.end(); ++fp)
            pos_insert(followpos[fp->first], fp->second);
          // add m-1 times virtual concatenation (by indexed positions k.i)
          for (Iter i = 0; i < m - 1; ++i)
            for (Positions::const_iterator k = lastpos.begin(); k != lastpos.end(); ++k)
              for (Positions::const_iterator j = firstpos.begin(); j != firstpos.end(); ++j)
                pos_add(followpos[k->pos().iter(iter * i)], j->iter(iter * i + iter));
          if (unlimited)
            for (Positions::const_iterator k = lastpos.begin(); k != lastpos.end(); ++k)
              for (Positions::const_iterator j = firstpos.begin(); j != firstpos.end(); ++j)
                pos_add(followpos[k->pos().iter(iter * (m - 1))], j->iter(iter * (m - 1)));
          if (nullable1)
          {
            // extend firstpos when sub-regex is nullable
            Positions firstpos1 = firstpos;
            firstpos.reserve(m * firstpos1.size());
            for (Iter i = 1; i <= m - 1; ++i)
              for (Positions::const_iterator k = firstpos1.begin(); k != firstpos1.end(); ++k)
                pos_add(firstpos, k->iter(iter * i));
          }
          // n to m-1 are optional with all 0 to m-1 are optional when nullable
          Positions lastpos1;
          for (Iter i = (nullable ? 0 : n - 1); i <= m - 1; ++i)
            for (Positions::const_iterator k = lastpos.begin(); k != lastpos.end(); ++k)
              pos_add(lastpos1, k->iter(iter * i));
          lastpos.swap(lastpos1);
          iter *= m;
        }
        else // zero range {0}
        {
          firstpos.clear();
          lastpos.clear();
          lazypos.clear();
        }
      }
      else if (at(loc) == '\0')
      {
        error(regex_error::mismatched_braces, loc);
      }
      else
      {
        error(regex_error::invalid_repeat, loc);
      }
    }
    else
    {
      break;
    }
    c = at(loc);
  }
  DBGLOG("END parse3()");
}

void Pattern::parse4(
    bool       begin,
    Location&  loc,
    Positions& firstpos,
    Positions& lastpos,
    bool&      nullable,
    Follow&    followpos,
    Lazy&      lazyidx,
    Lazypos&   lazypos,
    Mods       modifiers,
    Locations& lookahead,
    Iter&      iter)
{
  DBGLOG("BEGIN parse4(%u)", loc);
  firstpos.clear();
  lastpos.clear();
  nullable = true;
  lazypos.clear();
  iter = 1;
  Char c = at(loc);
  if (c == '(')
  {
    if (at(++loc) == '?')
    {
      c = at(++loc);
      if (c == '#') // (?# comment
      {
        while ((c = at(++loc)) != '\0' && c != ')')
          continue;
        if (c == ')')
          ++loc;
      }
      else if (c == '^') // (?^ negative pattern to be ignored (new mode), producing a redo match
      {
        Positions firstpos1;
        ++loc;
        parse1(
            begin,
            loc,
            firstpos1,
            lastpos,
            nullable,
            followpos,
            lazyidx,
            lazypos,
            modifiers,
            lookahead,
            iter);
        for (Positions::iterator p = firstpos1.begin(); p != firstpos1.end(); ++p)
          pos_add(firstpos, p->negate(true));
      }
      else if (c == '=') // (?= lookahead
      {
        Position l_pos(loc++ - 2); // lookahead at (
        parse1(
            begin,
            loc,
            firstpos,
            lastpos,
            nullable,
            followpos,
            lazyidx,
            lazypos,
            modifiers,
            lookahead,
            iter);
        pos_add(firstpos, l_pos);
        if (nullable)
          pos_add(lastpos, l_pos);
        if (lookahead.find(l_pos.loc(), loc) == lookahead.end()) // do not permit nested lookaheads
          lookahead.insert(l_pos.loc(), loc); // lookstop at )
        for (Positions::const_iterator p = lastpos.begin(); p != lastpos.end(); ++p)
          pos_add(followpos[p->pos()], Position(loc).ticked(true));
        pos_add(lastpos, Position(loc).ticked(true));
        if (nullable)
        {
          pos_add(firstpos, Position(loc).ticked(true));
          pos_add(lastpos, l_pos);
        }
      }
      else if (c == ':')
      {
        ++loc;
        parse1(
            begin,
            loc,
            firstpos,
            lastpos,
            nullable,
            followpos,
            lazyidx,
            lazypos,
            modifiers,
            lookahead,
            iter);
      }
      else
      {
        Location m_loc = loc;
        bool negative = false;
        bool opt_q = opt_.q;
        bool opt_x = opt_.x;
        do
        {
          if (c == '-')
            negative = true;
          else if (c == 'q')
            opt_.q = !negative;
          else if (c == 'x')
            opt_.x = !negative;
          else if (c != 'i' && c != 'm' && c != 's')
            error(regex_error::invalid_modifier, loc);
          c = at(++loc);
        } while (c != '\0' && c != ':' && c != ')');
        if (c != '\0')
          ++loc;
        // enforce (?imqsux) modes
        parse1(
            begin,
            loc,
            firstpos,
            lastpos,
            nullable,
            followpos,
            lazyidx,
            lazypos,
            modifiers,
            lookahead,
            iter);
        negative = false;
        do
        {
          c = at(m_loc++);
          switch (c)
          {
            case '-': 
              negative = true;
              break;
            case 'i':
              update_modified(ModConst::i ^ static_cast<Mod>(negative), modifiers, m_loc, loc);
              break;
            case 'm':
              update_modified(ModConst::m ^ static_cast<Mod>(negative), modifiers, m_loc, loc);
              break;
            case 's':
              update_modified(ModConst::s ^ static_cast<Mod>(negative), modifiers, m_loc, loc);
              break;
            case 'u':
              update_modified(ModConst::u ^ static_cast<Mod>(negative), modifiers, m_loc, loc);
              break;
          }
        } while (c != '\0' && c != ':' && c != ')');
        opt_.q = opt_q;
        opt_.x = opt_x;
      }
    }
    else
    {
      parse1(
          begin,
          loc,
          firstpos,
          lastpos,
          nullable,
          followpos,
          lazyidx,
          lazypos,
          modifiers,
          lookahead,
          iter);
    }
    if (c != ')')
    {
      if (at(loc) == ')')
        ++loc;
      else
        error(regex_error::mismatched_parens, loc);
    }
  }
  else if (c == '[')
  {
    pos_add(firstpos, loc);
    pos_add(lastpos, loc);
    nullable = false;
    if ((c = at(++loc)) == '^')
      c = at(++loc);
    while (c != '\0')
    {
      if (c == '[' && (at(loc + 1) == ':' || at(loc + 1) == '.' || at(loc + 1) == '='))
      {
        size_t c_loc = find_at(loc + 2, static_cast<char>(at(loc + 1)));
        if (c_loc != std::string::npos && at(static_cast<Location>(c_loc + 1)) == ']')
          loc = static_cast<Location>(c_loc + 1);
      }
      else if (c == opt_.e && !opt_.b)
      {
        ++loc;
      }
      if ((c = at(++loc)) == ']')
        break;
    }
    if (c == '\0')
      error(regex_error::mismatched_brackets, loc);
    ++loc;
  }
  else if ((c == '"' && opt_.q) || escape_at(loc) == 'Q')
  {
    bool quoted = (c == '"');
    if (!quoted)
      ++loc;
    Location q_loc = ++loc;
    c = at(loc);
    if (c != '\0' && (quoted ? c != '"' : c != opt_.e || at(loc + 1) != 'E'))
    {
      pos_add(firstpos, loc);
      Position p;
      do
      {
        if (quoted && c == opt_.e && at(loc + 1) == '"')
          ++loc;
        if (p != Position::NPOS)
          pos_add(followpos[p.pos()], loc);
        p = loc++;
        c = at(loc);
      } while (c != '\0' && (!quoted || c != '"') && (quoted || c != opt_.e || at(loc + 1) != 'E'));
      pos_add(lastpos, p);
      nullable = false;
      modifiers[ModConst::q].insert(q_loc, loc - 1);
    }
    if (!quoted && at(loc) != '\0')
      ++loc;
    if (at(loc) != '\0')
      ++loc;
    else
      error(regex_error::mismatched_quotation, loc);
  }
  else if (c == '#' && opt_.x)
  {
    ++loc;
    while ((c = at(loc)) != '\0' && c != '\n')
      ++loc;
    if (c == '\n')
      ++loc;
  }
  else if (std::isspace(c) && opt_.x)
  {
    ++loc;
  }
  else if (c == ')')
  {
    error(begin ? regex_error::empty_expression : regex_error::mismatched_parens, loc++);
  }
  else if (c != '\0' && c != '|' && c != '?' && c != '*' && c != '+')
  {
    pos_add(firstpos, loc);
    pos_add(lastpos, loc);
    nullable = false;
    if (c == opt_.e)
      c = parse_esc(loc);
    else
      ++loc;
  }
  else if (begin && c != '\0') // permits empty regex pattern but not empty subpatterns
  {
    error(regex_error::empty_expression, loc);
  }
  DBGLOG("END parse4()");
}

Pattern::Char Pattern::parse_esc(Location& loc, Chars *chars) const
{
  Char c = at(++loc);
  if (c == '0')
  {
    c = 0;
    int d = at(++loc);
    if (d >= '0' && d <= '7')
    {
      c = d - '0';
      d = at(++loc);
      if (d >= '0' && d <= '7')
      {
        c = (c << 3) + d - '0';
        d = at(++loc);
        if (c < 32 && d >= '0' && d <= '7')
        {
          c = (c << 3) + d - '0';
          ++loc;
        }
      }
    }
  }
  else if ((c == 'x' || c == 'u') && at(loc + 1) == '{')
  {
    c = 0;
    loc += 2;
    int d = at(loc);
    if (std::isxdigit(d))
    {
      c = (d > '9' ? (d | 0x20) - ('a' - 10) : d - '0');
      d = at(++loc);
      if (std::isxdigit(d))
      {
        c = (c << 4) + (d > '9' ? (d | 0x20) - ('a' - 10) : d - '0');
        ++loc;
      }
    }
    if (at(loc) == '}')
      ++loc;
    else
      error(regex_error::invalid_escape, loc);
  }
  else if (c == 'x' && std::isxdigit(at(loc + 1)))
  {
    int d = at(++loc);
    c = (d > '9' ? (d | 0x20) - ('a' - 10) : d - '0');
    d = at(++loc);
    if (std::isxdigit(d))
    {
      c = (c << 4) + (d > '9' ? (d | 0x20) - ('a' - 10) : d - '0');
      ++loc;
    }
  }
  else if (c == 'c')
  {
    c = at(++loc) % 32;
    ++loc;
  }
  else if (c == 'e')
  {
    c = 0x1B;
    ++loc;
  }
  else if (c == 'N')
  {
    if (chars != NULL)
    {
      chars->add(0, 9);
      chars->add(11, 255);
    }
    ++loc;
    c = META_EOL;
  }
  else if ((c == 'p' || c == 'P') && at(loc + 1) == '{')
  {
    loc += 2;
    if (chars != NULL)
    {
      size_t i;
      for (i = 0; i < 14; ++i)
        if (eq_at(loc, posix_class[i]))
          break;
      if (i < 14)
        posix(i, *chars);
      else
        error(regex_error::invalid_class, loc);
      if (c == 'P')
        flip(*chars);
      loc += static_cast<Location>(strlen(posix_class[i]));
      if (at(loc) == '}')
        ++loc;
      else
        error(regex_error::invalid_escape, loc);
    }
    else
    {
      while ((c = at(++loc)) != '\0' && c != '}')
        continue;
      if (c == '}')
        ++loc;
      else
        error(regex_error::invalid_escape, loc);
    }
    c = META_EOL;
  }
  else if (c != '_')
  {
    static const char abtnvfr[] = "abtnvfr";
    const char *s = std::strchr(abtnvfr, c);
    if (s != NULL)
    {
      c = static_cast<Char>(s - abtnvfr + '\a');
    }
    else
    {
      static const char escapes[] = "__sSxX________hHdD__lL__uUwW";
      s = std::strchr(escapes, c);
      if (s != NULL)
      {
        if (chars != NULL)
        {
          posix((s - escapes) / 2, *chars);
          if ((s - escapes) % 2)
            flip(*chars);
        }
        c = META_EOL;
      }
    }
    ++loc;
  }
  if (c <= 0xFF && chars != NULL)
    chars->add(c);
  return c;
}

void Pattern::compile(
    DFA::State    *start,
    Follow&        followpos,
    const Lazypos& lazypos,
    const Mods     modifiers,
    const Map&     lookahead)
{
  DBGLOG("BEGIN compile()");
  // init timers
  timer_type vt, et;
  timer_start(vt);
  // construct the DFA
  acc_.resize(end_.size(), false);
  trim_lazy(start, lazypos);
  // hash table with 64K pointer entries uint16_t indexed
  DFA::State **table = new DFA::State*[65536];
  for (int i = 0; i < 65536; ++i)
    table[i] = NULL;
  // start state should only be discoverable (to possibly cycle back to) if no tree DFA was constructed
  if (start->tnode == NULL)
    table[hash_pos(start)] = start;
  // last added state
  DFA::State *last_state = start;
  for (DFA::State *state = start; state != NULL; state = state->next)
  {
    Moves moves;
    timer_start(et);
    // use the tree DFA accept state, if present
    if (state->tnode != NULL && state->tnode->accept > 0)
      state->accept = state->tnode->accept;
    compile_transition(
        state,
        followpos,
        lazypos,
        modifiers,
        lookahead,
        moves);
    if (state->tnode != NULL)
    {
#ifdef WITH_TREE_DFA
      // merge tree DFA transitions into the final DFA transitions to target states
      if (moves.empty())
      {
        // no DFA transitions: the final DFA transitions are the tree DFA transitions to target states
        if (opt_.i)
        {
          for (DFA::State::Edges::iterator t = state->tnode->edges.begin(); t != state->tnode->edges.end(); ++t)
          {
            Char c = t->first;
            DFA::State *target_state = last_state = last_state->next = dfa_.state(t->second.second);
            state->edges[c] = DFA::State::Edge(c, target_state);
            if (c >= 'a' && c <= 'z')
            {
              state->edges[uppercase(c)] = DFA::State::Edge(uppercase(c), target_state);
              ++eno_;
            }
            ++eno_;
          }
        }
        else
        {
          for (DFA::State::Edges::iterator t = state->tnode->edges.begin(); t != state->tnode->edges.end(); ++t)
          {
            Char c = t->first;
            DFA::State *target_state = last_state = last_state->next = dfa_.state(t->second.second);
            state->edges[c] = DFA::State::Edge(c, target_state);
            ++eno_;
          }
        }
      }
      else
      {
        // combine the tree DFA transitions with the regex DFA transition moves
        Chars chars;
        for (DFA::State::Edges::iterator t = state->tnode->edges.begin(); t != state->tnode->edges.end(); ++t)
          chars.add(t->first);
        if (opt_.i)
        {
          for (DFA::State::Edges::iterator t = state->tnode->edges.find('a'); t != state->tnode->edges.end(); ++t)
          {
            Char c = t->first;
            if (c > 'z')
              break;
            chars.add(uppercase(c));
          }
        }
        Moves::iterator i = moves.begin();
        Positions pos;
        while (i != moves.end())
        {
          if (chars.intersects(i->first))
          {
            // tree DFA transitions intersect with this DFA transition move
            Chars common = chars & i->first;
            chars -= common;
            Char lo = common.lo();
            Char hi = common.hi();
            if (opt_.i)
            {
              for (Char c = lo; c <= hi; ++c)
              {
                if (common.contains(c))
                {
                  if (std::isalpha(c))
                  {
                    if (c >= 'a' && c <= 'z')
                    {
                      pos = i->second;
                      DFA::State *target_state = last_state = last_state->next = dfa_.state(state->tnode->edges[c].second, pos);
                      state->edges[c] = DFA::State::Edge(c, target_state);
                      state->edges[uppercase(c)] = DFA::State::Edge(uppercase(c), target_state);
                      eno_ += 2;
                    }
                  }
                  else
                  {
                    pos = i->second;
                    DFA::State *target_state = last_state = last_state->next = dfa_.state(state->tnode->edges[c].second, pos);
                    state->edges[c] = DFA::State::Edge(c, target_state);
                    ++eno_;
                  }
                }
              }
            }
            else
            {
              for (Char c = lo; c <= hi; ++c)
              {
                if (common.contains(c))
                {
                  pos = i->second;
                  DFA::State *target_state = last_state = last_state->next = dfa_.state(state->tnode->edges[c].second, pos);
                  state->edges[c] = DFA::State::Edge(c, target_state);
                  ++eno_;
                }
              }
            }
            i->first -= common;
            if (i->first.any())
              ++i;
            else
              moves.erase(i++);
          }
          else
          {
            ++i;
          }
        }
        if (opt_.i)
        {
          // normalize by removing upper case if option i (case insensitivem matching) is enabled
          static const uint64_t upper[5] = { 0x0000000000000000ULL, 0x0000000007FFFFFEULL, 0ULL, 0ULL, 0ULL };
          chars -= Chars(upper);
        }
        if (chars.any())
        {
          Char lo = chars.lo();
          Char hi = chars.hi();
          if (opt_.i)
          {
            for (Char c = lo; c <= hi; ++c)
            {
              if (chars.contains(c))
              {
                DFA::State *target_state = last_state = last_state->next = dfa_.state(state->tnode->edges[c].second);
                if (std::isalpha(c))
                {
                  state->edges[lowercase(c)] = DFA::State::Edge(lowercase(c), target_state);
                  state->edges[uppercase(c)] = DFA::State::Edge(uppercase(c), target_state);
                  eno_ += 2;
                }
                else
                {
                  state->edges[c] = DFA::State::Edge(c, target_state);
                  ++eno_;
                }
              }
            }
          }
          else
          {
            for (Char c = lo; c <= hi; ++c)
            {
              if (chars.contains(c))
              {
                DFA::State *target_state = last_state = last_state->next = dfa_.state(state->tnode->edges[c].second);
                state->edges[c] = DFA::State::Edge(c, target_state);
                ++eno_;
              }
            }
          }
        }
      }
#else
#ifdef WITH_TREE_MAP
      // merge tree DFA transitions into the final DFA transitions to target states
      if (moves.empty())
      {
        // no DFA transitions: the final DFA transitions are the tree DFA transitions to target states
        for (std::map<Char,Tree::Node>::iterator t = state->tnode->edges.begin(); t != state->tnode->edges.end(); ++t)
        {
          Char c = t->first;
          DFA::State *target_state = last_state = last_state->next = dfa_.state(&t->second);
          state->edges[c] = DFA::State::Edge(c, target_state);
          ++eno_;
          if (opt_.i && c >= 'a' && c <= 'z')
          {
            state->edges[uppercase(c)] = DFA::State::Edge(uppercase(c), target_state);
            ++eno_;
          }
        }
      }
      else
      {
        // combine the tree DFA transitions with the regex DFA transition moves
        Chars chars;
        for (std::map<Char,Tree::Node>::iterator t = state->tnode->edges.begin(); t != state->tnode->edges.end(); ++t)
          chars.add(t->first);
        if (opt_.i)
        {
          for (std::map<Char,Tree::Node>::iterator t = state->tnode->edges.find('a'); t != state->tnode->edges.end(); ++t)
          {
            Char c = t->first;
            if (c > 'z')
              break;
            chars.add(uppercase(c));
          }
        }
        Moves::iterator i = moves.begin();
        Positions pos;
        while (i != moves.end())
        {
          if (chars.intersects(i->first))
          {
            // tree DFA transitions intersect with this DFA transition move
            Chars common = chars & i->first;
            chars -= common;
            Char lo = common.lo();
            Char hi = common.hi();
            if (opt_.i)
            {
              for (Char c = lo; c <= hi; ++c)
              {
                if (common.contains(c))
                {
                  if (std::isalpha(c))
                  {
                    if (c >= 'a' && c <= 'z')
                    {
                      pos = i->second;
                      DFA::State *target_state = last_state = last_state->next = dfa_.state(&state->tnode->edges[c], pos);
                      state->edges[c] = DFA::State::Edge(c, target_state);
                      state->edges[uppercase(c)] = DFA::State::Edge(uppercase(c), target_state);
                      eno_ += 2;
                    }
                  }
                  else
                  {
                    pos = i->second;
                    DFA::State *target_state = last_state = last_state->next = dfa_.state(&state->tnode->edges[c], pos);
                    state->edges[c] = DFA::State::Edge(c, target_state);
                    ++eno_;
                  }
                }
              }
            }
            else
            {
              for (Char c = lo; c <= hi; ++c)
              {
                if (common.contains(c))
                {
                  pos = i->second;
                  DFA::State *target_state = last_state = last_state->next = dfa_.state(&state->tnode->edges[c], pos);
                  state->edges[c] = DFA::State::Edge(c, target_state);
                  ++eno_;
                }
              }
            }
            i->first -= common;
            if (i->first.any())
              ++i;
            else
              moves.erase(i++);
          }
          else
          {
            ++i;
          }
        }
        if (opt_.i)
        {
          // normalize by removing upper case if option i (case insensitive matching) is enabled
          static const uint64_t upper[5] = { 0x0000000000000000ULL, 0x0000000007FFFFFEULL, 0ULL, 0ULL, 0ULL };
          chars -= Chars(upper);
        }
        if (chars.any())
        {
          Char lo = chars.lo();
          Char hi = chars.hi();
          for (Char c = lo; c <= hi; ++c)
          {
            if (chars.contains(c))
            {
              DFA::State *target_state = last_state = last_state->next = dfa_.state(&state->tnode->edges[c]);
              if (opt_.i && std::isalpha(c))
              {
                state->edges[lowercase(c)] = DFA::State::Edge(lowercase(c), target_state);
                state->edges[uppercase(c)] = DFA::State::Edge(uppercase(c), target_state);
                eno_ += 2;
              }
              else
              {
                state->edges[c] = DFA::State::Edge(c, target_state);
                ++eno_;
              }
            }
          }
        }
      }
#else
      // merge tree DFA transitions into the final DFA transitions to target states
      if (moves.empty())
      {
        // no DFA transitions: the final DFA transitions are the tree DFA transitions to target states
        for (Char i = 0; i < 16; ++i)
        {
          Tree::Node **p = state->tnode->edge[i];
          if (p != NULL)
          {
            for (Char j = 0; j < 16; ++j)
            {
              if (p[j] != NULL)
              {
                Char c = (i << 4) + j;
                DFA::State *target_state = last_state = last_state->next = dfa_.state(p[j]);
                if (opt_.i && std::isalpha(c))
                {
                  state->edges[lowercase(c)] = DFA::State::Edge(lowercase(c), target_state);
                  state->edges[uppercase(c)] = DFA::State::Edge(uppercase(c), target_state);
                  eno_ += 2;
                }
                else
                {
                  state->edges[c] = DFA::State::Edge(c, target_state);
                  ++eno_;
                }
              }
            }
          }
        }
      }
      else
      {
        // combine the tree DFA transitions with the regex DFA transition moves
        Chars chars;
        for (Char i = 0; i < 16; ++i)
        {
          Tree::Node **p = state->tnode->edge[i];
          if (p != NULL)
          {
            for (Char j = 0; j < 16; ++j)
            {
              if (p[j] != NULL)
              {
                Char c = (i << 4) + j;
                chars.add(c);
              }
            }
          }
        }
        if (opt_.i)
          for (Char c = 'a'; c <= 'z'; ++c)
            if (state->tnode->edge[c >> 4] != NULL && state->tnode->edge[c >> 4][c & 0xf] != NULL)
              chars.add(uppercase(c));
        Moves::iterator i = moves.begin();
        Positions pos;
        while (i != moves.end())
        {
          if (chars.intersects(i->first))
          {
            // tree DFA transitions intersect with this DFA transition move
            Chars common = chars & i->first;
            chars -= common;
            Char lo = common.lo();
            Char hi = common.hi();
            if (opt_.i)
            {
              for (Char c = lo; c <= hi; ++c)
              {
                if (common.contains(c))
                {
                  if (std::isalpha(c))
                  {
                    if (c >= 'a' && c <= 'z')
                    {
                      pos = i->second;
                      DFA::State *target_state = last_state = last_state->next = dfa_.state(state->tnode->edge[c >> 4][c & 0xf], pos);
                      state->edges[c] = DFA::State::Edge(c, target_state);
                      state->edges[uppercase(c)] = DFA::State::Edge(uppercase(c), target_state);
                      eno_ += 2;
                    }
                  }
                  else
                  {
                    pos = i->second;
                    DFA::State *target_state = last_state = last_state->next = dfa_.state(state->tnode->edge[c >> 4][c & 0xf], pos);
                    state->edges[c] = DFA::State::Edge(c, target_state);
                    ++eno_;
                  }
                }
              }
            }
            else
            {
              for (Char c = lo; c <= hi; ++c)
              {
                if (common.contains(c))
                {
                  pos = i->second;
                  DFA::State *target_state = last_state = last_state->next = dfa_.state(state->tnode->edge[c >> 4][c & 0xf], pos);
                  state->edges[c] = DFA::State::Edge(c, target_state);
                  ++eno_;
                }
              }
            }
            i->first -= common;
            if (i->first.any())
              ++i;
            else
              moves.erase(i++);
          }
          else
          {
            ++i;
          }
        }
        if (opt_.i)
        {
          // normalize by removing upper case if option i (case insensitive matching) is enabled
          static const uint64_t upper[5] = { 0x0000000000000000ULL, 0x0000000007FFFFFEULL, 0ULL, 0ULL, 0ULL };
          chars -= Chars(upper);
        }
        if (chars.any())
        {
          Char lo = chars.lo();
          Char hi = chars.hi();
          for (Char c = lo; c <= hi; ++c)
          {
            if (chars.contains(c))
            {
              DFA::State *target_state = last_state = last_state->next = dfa_.state(state->tnode->edge[c >> 4][c & 0xf]);
              if (opt_.i && std::isalpha(c))
              {
                state->edges[lowercase(c)] = DFA::State::Edge(lowercase(c), target_state);
                state->edges[uppercase(c)] = DFA::State::Edge(uppercase(c), target_state);
                eno_ += 2;
              }
              else
              {
                state->edges[c] = DFA::State::Edge(c, target_state);
                ++eno_;
              }
            }
          }
        }
      }
#endif
#endif
    }
    ems_ += timer_elapsed(et);
    Moves::iterator end = moves.end();
    for (Moves::iterator i = moves.begin(); i != end; ++i)
    {
      Positions& pos = i->second;
      uint16_t h = hash_pos(&pos);
      DFA::State **branch_ptr = &table[h];
      DFA::State *target_state = *branch_ptr;
      // binary search the target state for a possible matching state in the hash table overflow tree
      while (target_state != NULL)
      {
        if (pos < *target_state)
          target_state = *(branch_ptr = &target_state->left);
        else if (pos > *target_state)
          target_state = *(branch_ptr = &target_state->right);
        else
          break;
      }
      if (target_state == NULL)
        *branch_ptr = target_state = last_state = last_state->next = dfa_.state(NULL, pos);
      Char lo = i->first.lo();
      Char max = i->first.hi();
#ifdef DEBUG
      DBGLOGN("from state %p on %02x-%02x move to {", state, lo, max);
      for (Positions::const_iterator p = pos.begin(); p != pos.end(); ++p)
        DBGLOGPOS(*p);
      DBGLOGN(" } = state %p", target_state);
#endif
      while (lo <= max)
      {
        if (i->first.contains(lo))
        {
          Char hi = lo + 1;
          while (hi <= max && i->first.contains(hi))
            ++hi;
          --hi;
#if WITH_COMPACT_DFA == -1
          state->edges[lo] = DFA::State::Edge(hi, target_state);
#else
          state->edges[hi] = DFA::State::Edge(lo, target_state);
#endif
          eno_ += hi - lo + 1;
          lo = hi + 1;
        }
        ++lo;
      }
    }
    if (state->accept > 0 && state->accept <= end_.size())
      acc_[state->accept - 1] = true;
    ++vno_;
  }
  delete[] table;
  vms_ = timer_elapsed(vt) - ems_;
  DBGLOG("END compile()");
}

void Pattern::lazy(
    const Lazypos& lazypos,
    Positions&     pos) const
{
  for (Positions::iterator p = pos.begin(); p != pos.end(); ++p)
    for (Lazypos::const_iterator l = lazypos.begin(); l != lazypos.end(); ++l)
      *p = p->lazy(l->lazy());
}

void Pattern::lazy(
    const Lazypos&   lazypos,
    const Positions& pos,
    Positions&       pos1) const
{
  pos1.reserve(lazypos.size() * pos.size());
  for (Positions::const_iterator p = pos.begin(); p != pos.end(); ++p)
    for (Lazypos::const_iterator l = lazypos.begin(); l != lazypos.end(); ++l)
      pos_add(pos1, p->lazy(l->lazy()));
}

void Pattern::greedy(Positions& pos) const
{
  for (Positions::iterator p = pos.begin(); p != pos.end(); ++p)
    *p = p->lazy(0);
}

void Pattern::trim_anchors(Positions& follow, const Position p) const
{
#ifdef DEBUG
  DBGLOG("trim_anchors({");
  for (Positions::const_iterator q = follow.begin(); q != follow.end(); ++q)
    DBGLOGPOS(*q);
  DBGLOGA(" }, %u)", p.loc());
#endif
  Positions::iterator q = follow.begin();
  Positions::iterator end = follow.end();
  // check if we follow into an accepting state, if so trim follow state to remove back edges and cyclic anchors e.g. (^$)*
  while (q != end && !q->accept())
    ++q;
  if (q != end)
  {
    q = follow.begin();
    if (p.anchor())
    {
      while (q != follow.end())
      {
        // erase if not accepting and not a begin anchor and not a ) lookahead tail
        if (!q->accept() && !q->anchor() && at(q->loc()) != ')')
          q = follow.erase(q);
        else
          ++q;
      }
    }
    else
    {
      Location loc = p.loc();
      while (q != follow.end())
      {
        // erase if not accepting and not a begin anchor and back edge
        if (!q->accept() && !q->anchor() && q->loc() <= loc)
          q = follow.erase(q);
        else
          ++q;
      }
    }
  }
#ifdef DEBUG
  DBGLOGA(" = {");
  for (Positions::const_iterator q = follow.begin(); q != follow.end(); ++q)
    DBGLOGPOS(*q);
  DBGLOG(" }");
#endif
}

void Pattern::trim_lazy(Positions *pos, const Lazypos& lazypos) const
{
#ifdef DEBUG
  DBGLOG("BEGIN trim_lazy({");
  for (Positions::const_iterator q = pos->begin(); q != pos->end(); ++q)
    DBGLOGPOS(*q);
  DBGLOGA(" })");
#endif
  for (Positions::iterator p = pos->begin(); p != pos->end(); ++p)
  {
    Lazy l = p->lazy();
    // if lazy accept state, then remove matching lazy positions to cut lazy edges
    if (l > 0 && (p->accept() || p->anchor()))
    {
      *p = p->lazy(0);
      // remove lazy positions matching lazy index l
      Positions::iterator q = pos->begin();
      Positions::iterator r = q;
      size_t i = 0;
      while (q != pos->end())
      {
        if (q->lazy() != l)
        {
          if (q != r)
            *r = *q;
          ++r;
          if (q < p)
            ++i;
        }
        ++q;
      }
      // if anything was removed, then update the position vector and reassign iterator p
      if (r != pos->end())
      {
        pos->erase(r, pos->end());
        p = pos->begin() + i;
      }
    }
  }
  // sort the positions and remove duplicates to make the state unique and comparable
  std::sort(pos->begin(), pos->end());
  pos->erase(unique(pos->begin(), pos->end()), pos->end());
  // if all positions are lazy with the same lazy index, then make the after positions non-lazy
  if (!pos->empty() && pos->begin()->lazy())
  {
    Location max = 0;
    for (Lazypos::const_iterator l = lazypos.begin(); l != lazypos.end(); ++l)
      for (Positions::const_iterator p = pos->begin(); p != pos->end(); ++p)
        if (p->lazy() == l->lazy())
          if (max < l->loc())
            max = l->loc();
    if (max > 0)
      for (Positions::iterator p = pos->begin(); p != pos->end(); ++p)
        if (p->loc() > max)
          *p = p->lazy(0);
  }
#ifdef DEBUG
  DBGLOG("END trim_lazy({");
  for (Positions::const_iterator q = pos->begin(); q != pos->end(); ++q)
    DBGLOGPOS(*q);
  DBGLOGA(" })");
#endif
}

void Pattern::compile_transition(
    DFA::State    *state,
    Follow&        followpos,
    const Lazypos& lazypos,
    const Mods     modifiers,
    const Map&     lookahead,
    Moves&         moves) const
{
  DBGLOG("BEGIN compile_transition()");
  Positions::const_iterator end = state->end();
  for (Positions::const_iterator k = state->begin(); k != end; ++k)
  {
    if (k->accept())
    {
      Accept accept = k->accepts();
      if (state->accept == 0 || accept < state->accept)
        state->accept = accept;
      if (k->negate())
        state->redo = true;
      DBGLOG("ACCEPT %u STATE %u REDO %d", accept, state->accept, state->redo);
    }
  }
  for (Positions::const_iterator k = state->begin(); k != end; ++k)
  {
    if (!k->accept())
    {
      Location loc = k->loc();
      Char c = at(loc);
      DBGLOGN("At %u: %c", loc, c);
      bool literal = is_modified(ModConst::q, modifiers, loc);
      if (c == '(' && !literal)
      {
        Lookahead n = 0;
        DBGLOG("LOOKAHEAD HEAD");
        for (Map::const_iterator i = lookahead.begin(); i != lookahead.end(); ++i)
        {
          Locations::const_iterator j = i->second.find(loc);
          DBGLOGN("%d %d (%d) %u", state->accept, i->first, j != i->second.end(), n);
          if (j != i->second.end())
          {
            Lookahead l = n + static_cast<Lookahead>(std::distance(i->second.begin(), j));
            if (l < n)
              error(regex_error::exceeds_limits, loc);
            state->heads.insert(l);
          }
          Lookahead k = n;
          n += static_cast<Lookahead>(i->second.size());
          if (n < k)
            error(regex_error::exceeds_limits, loc);
        }
      }
      else if (c == ')' && !literal)
      {
        if (state->accept > 0)
        {
          Lookahead n = 0;
          DBGLOG("LOOKAHEAD TAIL");
          for (Map::const_iterator i = lookahead.begin(); i != lookahead.end(); ++i)
          {
            Locations::const_iterator j = i->second.find(loc);
            DBGLOGN("%d %d (%d) %u", state->accept, i->first, j != i->second.end(), n);
            // only add lookstop when part of the proper accept state
            if (j != i->second.end() && static_cast<int>(state->accept) == i->first)
            {
              Lookahead l = n + static_cast<Lookahead>(std::distance(i->second.begin(), j));
              if (l < n)
                error(regex_error::exceeds_limits, loc);
              state->tails.insert(l);
            }
            Lookahead k = n;
            n += static_cast<Lookahead>(i->second.size());
            if (n < k)
              error(regex_error::exceeds_limits, loc);
          }
        }
      }
      else
      {
        Follow::iterator i = followpos.find(k->pos());
        if (i != followpos.end())
        {
          if (k->negate())
          {
            Positions::iterator b = i->second.begin();
            if (b != i->second.end() && !b->negate())
              for (Positions::iterator p = b; p != i->second.end(); ++p)
                *p = p->negate(true);
          }
          Lazy l = k->lazy();
          if (l)
          {
            // propagage lazy property along the path
            Follow::iterator j = followpos.find(*k);
            if (j == followpos.end())
            {
              j = followpos.insert(std::pair<Position,Positions>(*k, Positions())).first;
              j->second.reserve(i->second.size());
              for (Positions::iterator p = i->second.begin(); p != i->second.end(); ++p)
                pos_add(j->second, p->ticked() ? *p : p->lazy(l));
#ifdef DEBUG
              DBGLOGN("lazy followpos(");
              DBGLOGPOS(*k);
              DBGLOGA(" ) = {");
              for (Positions::const_iterator q = j->second.begin(); q != j->second.end(); ++q)
                DBGLOGPOS(*q);
              DBGLOGA(" }");
#endif
            }
            i = j;
          }
          Positions &follow = i->second;
          Chars chars;
          if (literal)
          {
            if (std::isalpha(c) && is_modified(ModConst::i, modifiers, loc))
            {
              chars.add(uppercase(c));
              chars.add(lowercase(c));
            }
            else
            {
              chars.add(c);
            }
          }
          else
          {
            switch (c)
            {
              case '.':
                if (is_modified(ModConst::s, modifiers, loc))
                {
                  static const uint64_t dot[5] = { 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL, 0ULL };
                  chars |= Chars(dot);
                }
                else
                {
                  static const uint64_t dot[5] = { 0xFFFFFFFFFFFFFBFFULL, 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL, 0ULL };
                  chars |= Chars(dot);
                }
                break;
              case '^':
                chars.add(is_modified(ModConst::m, modifiers, loc) ? META_BOL : META_BOB);
                trim_anchors(follow, *k);
                break;
              case '$':
                chars.add(is_modified(ModConst::m, modifiers, loc) ? META_EOL : META_EOB);
                trim_anchors(follow, *k);
                break;
              default:
                if (c == '[')
                {
                  compile_list(loc + 1, chars, modifiers);
                }
                else
                {
                  switch (escape_at(loc))
                  {
                    case '\0': // no escape at current loc
                      if (std::isalpha(c) && is_modified(ModConst::i, modifiers, loc))
                      {
                        chars.add(uppercase(c));
                        chars.add(lowercase(c));
                      }
                      else
                      {
                        chars.add(c);
                      }
                      break;
                    case 'i':
                      chars.add(META_IND);
                      break;
                    case 'j':
                      chars.add(META_DED);
                      break;
                    case 'k':
                      chars.add(META_UND);
                      break;
                    case 'A':
                      chars.add(META_BOB);
                      trim_anchors(follow, *k);
                      break;
                    case 'z':
                      chars.add(META_EOB);
                      trim_anchors(follow, *k);
                      break;
                    case 'B':
                      chars.add(k->anchor() ? META_NWB : META_NWE);
                      trim_anchors(follow, *k);
                      break;
                    case 'b':
                      chars.add(k->anchor() ? META_WBB : META_WBE);
                      trim_anchors(follow, *k);
                      break;
                    case '<':
                      chars.add(k->anchor() ? META_BWB : META_BWE);
                      trim_anchors(follow, *k);
                      break;
                    case '>':
                      chars.add(k->anchor() ? META_EWB : META_EWE);
                      trim_anchors(follow, *k);
                      break;
                    default:
                      c = parse_esc(loc, &chars);
                      if (c <= 'z' && std::isalpha(c) && is_modified(ModConst::i, modifiers, loc))
                      {
                        chars.add(uppercase(c));
                        chars.add(lowercase(c));
                      }
                  }
                }
            }
          }
          transition(moves, chars, follow);
        }
      }
    }
  }
  Moves::iterator i = moves.begin();
  Moves::const_iterator e = moves.end();
  while (i != e)
  {
    trim_lazy(&i->second, lazypos);
    if (i->second.empty())
      moves.erase(i++);
    else
      ++i;
  }
  DBGLOG("END compile_transition()");
}

void Pattern::transition(
    Moves&           moves,
    Chars&           chars,
    const Positions& follow) const
{
  Moves::iterator i = moves.begin();
  Moves::iterator end = moves.end();
  while (i != end)
  {
    if (i->second == follow)
    {
      chars += i->first;
      moves.erase(i++);
    }
    else
    {
      ++i;
    }
  }
  Chars common;
  for (i = moves.begin(); i != end; ++i)
  {
    common = chars & i->first;
    if (common.any())
    {
      if (common == i->first)
      {
        chars -= common;
        pos_insert(i->second, follow);
      }
      else
      {
        moves.push_back(Move(common, i->second));
        Move& back = moves.back();
        pos_insert(back.second, follow);
        chars -= back.first;
        i->first -= back.first;
      }
      if (!chars.any())
        return;
    }
  }
  if (chars.any())
    moves.push_back(Move(chars, follow));
}

void Pattern::compile_list(Location loc, Chars& chars, const Mods modifiers) const
{
  bool complement = (at(loc) == '^');
  if (complement)
    ++loc;
  Char prev = META_BOL;
  Char lo = META_EOL;
  for (Char c = at(loc); c != '\0' && (c != ']' || prev == META_BOL); c = at(++loc))
  {
    if (c == '-' && !is_meta(prev) && is_meta(lo))
    {
      lo = prev;
    }
    else
    {
      size_t c_loc;
      if (c == '[' && at(loc + 1) == ':' && (c_loc = find_at(loc + 2, ':')) != std::string::npos && at(static_cast<Location>(c_loc + 1)) == ']')
      {
        if (c_loc == loc + 3)
        {
          ++loc;
          c = parse_esc(loc, &chars);
        }
        else
        {
          size_t i;
          for (i = 0; i < 14; ++i)
            if (eq_at(loc + 4, posix_class[i] + 2)) // ignore first two letters (upper/lower) when matching
              break;
          if (i < 14)
            posix(i, chars);
          else
            error(regex_error::invalid_class, loc);
          c = META_EOL;
        }
        loc = static_cast<Location>(c_loc + 1);
      }
      else if (c == '[' && (at(loc + 1) == '.' || at(loc + 1) == '='))
      {
        c = at(loc + 2);
        if (c == '\0' || at(loc + 3) != at(loc + 1) || at(loc + 4) != ']')
          error(regex_error::invalid_collating, loc);
        loc += 4;
      }
      else if (c == opt_.e && !opt_.b)
      {
        c = parse_esc(loc, &chars);
        --loc;
      }
      if (!is_meta(c))
      {
        if (!is_meta(lo))
        {
          if (lo <= c)
            chars.add(lo, c);
          else
            error(regex_error::invalid_class_range, loc);
          if (is_modified(ModConst::i, modifiers, loc))
          {
            for (Char a = lo; a <= c; ++a)
            {
              if (a >= 'A' && a <= 'Z')
                chars.add(lowercase(a));
              else if (a >= 'a' && a <= 'z')
                chars.add(uppercase(a));
            }
          }
          c = META_EOL;
        }
        else
        {
          if (std::isalpha(c) && is_modified(ModConst::i, modifiers, loc))
          {
            chars.add(uppercase(c));
            chars.add(lowercase(c));
          }
          else
          {
            chars.add(c);
          }
        }
      }
      prev = c;
      lo = META_EOL;
    }
  }
  if (!is_meta(lo))
    chars.add('-');
  if (complement)
    flip(chars);
}

void Pattern::posix(size_t index, Chars& chars) const
{
  DBGLOG("posix(%lu)", index);
  static const uint64_t posix_chars[14][5] = {
    { 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL, 0ULL, 0ULL, 0ULL }, // ASCII
    { 0x0000000100003E00ULL, 0x0000000000000000ULL, 0ULL, 0ULL, 0ULL }, // Space: \t-\r, ' '
    { 0x03FF000000000000ULL, 0x0000007E0000007EULL, 0ULL, 0ULL, 0ULL }, // XDigit: 0-9, A-F, a-f
    { 0x00000000FFFFFFFFULL, 0x8000000000000000ULL, 0ULL, 0ULL, 0ULL }, // Cntrl: \x00-0x1F, \0x7F
    { 0xFFFFFFFF00000000ULL, 0x7FFFFFFFFFFFFFFFULL, 0ULL, 0ULL, 0ULL }, // Print: ' '-'~'
    { 0x03FF000000000000ULL, 0x07FFFFFE07FFFFFEULL, 0ULL, 0ULL, 0ULL }, // Alnum: 0-9, A-Z, a-z
    { 0x0000000000000000ULL, 0x07FFFFFE07FFFFFEULL, 0ULL, 0ULL, 0ULL }, // Alpha: A-Z, a-z
    { 0x0000000100000200ULL, 0x0000000000000000ULL, 0ULL, 0ULL, 0ULL }, // Blank: \t, ' '
    { 0x03FF000000000000ULL, 0x0000000000000000ULL, 0ULL, 0ULL, 0ULL }, // Digit: 0-9
    { 0xFFFFFFFE00000000ULL, 0x7FFFFFFFFFFFFFFFULL, 0ULL, 0ULL, 0ULL }, // Graph: '!'-'~'
    { 0x0000000000000000ULL, 0x07FFFFFE00000000ULL, 0ULL, 0ULL, 0ULL }, // Lower: a-z
    { 0xFC00FFFE00000000ULL, 0x78000001F8000001ULL, 0ULL, 0ULL, 0ULL }, // Punct: '!'-'/', ':'-'@', '['-'`', '{'-'~'
    { 0x0000000000000000ULL, 0x0000000007FFFFFEULL, 0ULL, 0ULL, 0ULL }, // Upper: A-Z
    { 0x03FF000000000000ULL, 0x07FFFFFE87FFFFFEULL, 0ULL, 0ULL, 0ULL }, // Word: 0-9, A-Z, a-z, _
  };
  chars |= Chars(posix_chars[index]);
}

void Pattern::flip(Chars& chars) const
{
  chars.flip256();
}

void Pattern::assemble(DFA::State *start)
{
  DBGLOG("BEGIN assemble()");
  timer_type t;
  timer_start(t);
  if (opt_.h)
    gen_match_hfa(start);
  analyze_dfa(start);
  ams_ = timer_elapsed(t);
  graph_dfa(start);
  compact_dfa(start);
  encode_dfa(start);
  wms_ = timer_elapsed(t);
  if (!opt_.f.empty())
  {
    if (opt_.o)
      gencode_dfa(start);
    else
      export_code();
  }
  DBGLOG("END assemble()");
}

void Pattern::compact_dfa(DFA::State *start)
{
#if WITH_COMPACT_DFA == -1
  // edge compaction in reverse order
  for (DFA::State *state = start; state != NULL; state = state->next)
  {
    for (DFA::State::Edges::iterator i = state->edges.begin(); i != state->edges.end(); ++i)
    {
      Char hi = i->second.first;
      if (hi >= 0xFF)
        break;
      DFA::State::Edges::iterator j = i;
      ++j;
      while (j != state->edges.end() && j->first <= hi + 1)
      {
        hi = j->second.first;
        if (j->second.second == i->second.second)
        {
          i->second.first = hi;
          state->edges.erase(j++);
        }
        else
        {
          ++j;
        }
      }
    }
  }
#elif WITH_COMPACT_DFA == 1
  // edge compaction
  for (DFA::State *state = start; state != NULL; state = state->next)
  {
    for (DFA::State::Edges::reverse_iterator i = state->edges.rbegin(); i != state->edges.rend(); ++i)
    {
      Char lo = i->second.first;
      if (lo <= 0x00)
        break;
      DFA::State::Edges::reverse_iterator j = i;
      ++j;
      while (j != state->edges.rend() && j->first >= lo - 1)
      {
        lo = j->second.first;
        if (j->second.second == i->second.second)
        {
          i->second.first = lo;
          state->edges.erase(--j.base());
        }
        else
        {
          ++j;
        }
      }
    }
  }
#else
  (void)start;
#endif
}

void Pattern::encode_dfa(DFA::State *start)
{
  nop_ = 0;
  for (DFA::State *state = start; state != NULL; state = state->next)
  {
    // clamp max accept
    if (state->accept > Const::AMAX)
      state->accept = Const::AMAX;
    state->first = state->index = nop_;
#if WITH_COMPACT_DFA == -1
    Char hi = 0x00;
    for (DFA::State::Edges::const_iterator i = state->edges.begin(); i != state->edges.end(); ++i)
    {
      Char lo = i->first;
      if (lo == hi)
        hi = i->second.first + 1;
      ++nop_;
      if (is_meta(lo))
        nop_ += i->second.first - lo;
    }
    // add final dead state (HALT opcode) only when needed, i.e. skip dead state if all chars 0-255 are already covered
    if (hi <= 0xFF)
    {
      state->edges[hi] = DFA::State::Edge(0xFF, static_cast<DFA::State*>(NULL)); // cast to appease MSVC 2010
      ++nop_;
    }
#else
    Char lo = 0xFF;
    bool covered = false;
    for (DFA::State::Edges::const_reverse_iterator i = state->edges.rbegin(); i != state->edges.rend(); ++i)
    {
      Char hi = i->first;
      if (lo == hi)
      {
        if (i->second.first == 0x00)
          covered = true;
        else
          lo = i->second.first - 1;
      }
      ++nop_;
      if (is_meta(lo))
        nop_ += hi - i->second.first;
    }
    // add final dead state (HALT opcode) only when needed, i.e. skip dead state if all chars 0-255 are already covered
    if (!covered)
    {
      state->edges[lo] = DFA::State::Edge(0x00, static_cast<DFA::State*>(NULL)); // cast to appease MSVC 2010
      ++nop_;
    }
#endif
    nop_ += static_cast<Index>(state->heads.size() + state->tails.size() + (state->accept > 0 || state->redo));
    if (!valid_goto_index(nop_))
      throw regex_error(regex_error::exceeds_limits, rex_, rex_.size());
  }
  if (nop_ > Const::LONG)
  {
    // over 64K opcodes: use 64-bit GOTO LONG opcodes
    nop_ = 0;
    for (DFA::State *state = start; state != NULL; state = state->next)
    {
      state->index = nop_;
#if WITH_COMPACT_DFA == -1
      Char hi = 0x00;
      for (DFA::State::Edges::const_iterator i = state->edges.begin(); i != state->edges.end(); ++i)
      {
        Char lo = i->first;
        if (lo == hi)
          hi = i->second.first + 1;
        // use 64-bit jump opcode if forward jump determined by previous loop is beyond 32K or backward jump is beyond 64K
        if (i->second.second != NULL &&
            ((i->second.second->first > state->first && i->second.second->first >= Const::LONG / 2) ||
             i->second.second->index >= Const::LONG))
          nop_ += 2;
        else
          ++nop_;
        if (is_meta(lo))
        {
          // use 64-bit jump opcode if forward jump determined by previous loop is beyond 32K or backward jump is beyond 64K
          if (i->second.second != NULL &&
            ((i->second.second->first > state->first && i->second.second->first >= Const::LONG / 2) ||
             i->second.second->index >= Const::LONG))
            nop_ += 2 * (i->second.first - lo);
          else
            nop_ += i->second.first - lo;
        }
      }
#else
      Char lo = 0xFF;
      for (DFA::State::Edges::const_reverse_iterator i = state->edges.rbegin(); i != state->edges.rend(); ++i)
      {
        Char hi = i->first;
        if (lo == hi)
          lo = i->second.first - 1;
        // use 64-bit jump opcode if forward jump determined by previous loop is beyond 32K or backward jump is beyond 64K
        if (i->second.second != NULL &&
            ((i->second.second->first > state->first && i->second.second->first >= Const::LONG / 2) ||
             i->second.second->index >= Const::LONG))
          nop_ += 2;
        else
          ++nop_;
        if (is_meta(lo))
        {
          // use 64-bit jump opcode if forward jump determined by previous loop is beyond 32K or backward jump is beyond 64K
          if (i->second.second != NULL &&
            ((i->second.second->first > state->first && i->second.second->first >= Const::LONG / 2) ||
             i->second.second->index >= Const::LONG))
            nop_ += 2 * (hi - i->second.first);
          else
            nop_ += hi - i->second.first;
        }
      }
#endif
      nop_ += static_cast<Index>(state->heads.size() + state->tails.size() + (state->accept > 0 || state->redo));
      if (!valid_goto_index(nop_))
        throw regex_error(regex_error::exceeds_limits, rex_, rex_.size());
    }
  }
  Opcode *opcode = new Opcode[nop_];
  opc_ = opcode;
  Index pc = 0;
  for (const DFA::State *state = start; state != NULL; state = state->next)
  {
    if (state->redo)
    {
      opcode[pc++] = opcode_redo();
    }
    else if (state->accept > 0)
    {
      opcode[pc++] = opcode_take(state->accept);
    }
    for (Lookaheads::const_iterator i = state->tails.begin(); i != state->tails.end(); ++i)
    {
      if (!valid_lookahead_index(static_cast<Index>(*i)))
        throw regex_error(regex_error::exceeds_limits, rex_, rex_.size());
      opcode[pc++] = opcode_tail(static_cast<Index>(*i));
    }
    for (Lookaheads::const_iterator i = state->heads.begin(); i != state->heads.end(); ++i)
    {
      if (!valid_lookahead_index(static_cast<Index>(*i)))
        throw regex_error(regex_error::exceeds_limits, rex_, rex_.size());
      opcode[pc++] = opcode_head(static_cast<Index>(*i));
    }
#if WITH_COMPACT_DFA == -1
    for (DFA::State::Edges::const_reverse_iterator i = state->edges.rbegin(); i != state->edges.rend(); ++i)
    {
      Char lo = i->first;
      Char hi = i->second.first;
      Index target_first = i->second.second != NULL ? i->second.second->first : Const::IMAX;
      Index target_index = i->second.second != NULL ? i->second.second->index : Const::IMAX;
      if (is_meta(lo))
      {
        do
        {
          if (target_index == Const::IMAX)
          {
            opcode[pc++] = opcode_goto(lo, lo, Const::HALT);
          }
          else if (nop_ > Const::LONG && ((target_first > state->first && target_first >= Const::LONG / 2) || target_index >= Const::LONG))
          {
            opcode[pc++] = opcode_goto(lo, lo, Const::LONG);
            opcode[pc++] = opcode_long(target_index);
          }
          else
          {
            opcode[pc++] = opcode_goto(lo, lo, target_index);
          }
        } while (++lo <= hi);
      }
      else
      {
        if (target_index == Const::IMAX)
        {
          opcode[pc++] = opcode_goto(lo, hi, Const::HALT);
        }
        else if (nop_ > Const::LONG && ((target_first > state->first && target_first >= Const::LONG / 2) || target_index >= Const::LONG))
        {
          opcode[pc++] = opcode_goto(lo, hi, Const::LONG);
          opcode[pc++] = opcode_long(target_index);
        }
        else
        {
          opcode[pc++] = opcode_goto(lo, hi, target_index);
        }
      }
    }
#else
    for (DFA::State::Edges::const_reverse_iterator i = state->edges.rbegin(); i != state->edges.rend(); ++i)
    {
      Char hi = i->first;
      Char lo = i->second.first;
      if (is_meta(lo))
      {
        Index target_first = i->second.second != NULL ? i->second.second->first : Const::IMAX;
        Index target_index = i->second.second != NULL ? i->second.second->index : Const::IMAX;
        do
        {
          if (target_index == Const::IMAX)
          {
            opcode[pc++] = opcode_goto(lo, lo, Const::HALT);
          }
          else if (nop_ > Const::LONG && ((target_first > state->first && target_first >= Const::LONG / 2) || target_index >= Const::LONG))
          {
            opcode[pc++] = opcode_goto(lo, lo, Const::LONG);
            opcode[pc++] = opcode_long(target_index);
          }
          else
          {
            opcode[pc++] = opcode_goto(lo, lo, target_index);
          }
        } while (++lo <= hi);
      }
    }
    for (DFA::State::Edges::const_iterator i = state->edges.begin(); i != state->edges.end(); ++i)
    {
      Char lo = i->second.first;
      if (!is_meta(lo))
      {
        Char hi = i->first;
        if (i->second.second != NULL)
        {
          Index target_first = i->second.second->first;
          Index target_index = i->second.second->index;
          if (nop_ > Const::LONG && ((target_first > state->first && target_first >= Const::LONG / 2) || target_index >= Const::LONG))
          {
            opcode[pc++] = opcode_goto(lo, hi, Const::LONG);
            opcode[pc++] = opcode_long(target_index);
          }
          else
          {
            opcode[pc++] = opcode_goto(lo, hi, target_index);
          }
        }
        else
        {
          opcode[pc++] = opcode_goto(lo, hi, Const::HALT);
        }
      }
    }
#endif
  }
}

void Pattern::gencode_dfa(const DFA::State *start) const
{
#ifndef WITH_NO_CODEGEN
  for (std::vector<std::string>::const_iterator i = opt_.f.begin(); i != opt_.f.end(); ++i)
  {
    const std::string& filename = *i;
    size_t len = filename.length();
    if ((len > 2 && filename.compare(len - 2, 2, ".h"  ) == 0)
     || (len > 3 && filename.compare(len - 3, 3, ".hh" ) == 0)
     || (len > 4 && filename.compare(len - 4, 4, ".hpp") == 0)
     || (len > 4 && filename.compare(len - 4, 4, ".hxx") == 0)
     || (len > 3 && filename.compare(len - 3, 3, ".cc" ) == 0)
     || (len > 4 && filename.compare(len - 4, 4, ".cpp") == 0)
     || (len > 4 && filename.compare(len - 4, 4, ".cxx") == 0))
    {
      FILE *file = NULL;
      int err = 0;
      if (filename.compare(0, 7, "stdout.") == 0)
        file = stdout;
      else if (filename.at(0) == '+')
        err = reflex::fopen_s(&file, filename.c_str() + 1, "a");
      else
        err = reflex::fopen_s(&file, filename.c_str(), "w");
      if (err || file == NULL)
        throw regex_error(regex_error::cannot_save_tables, filename);
      ::fprintf(file,
          "#include <reflex/matcher.h>\n\n"
          "#if defined(OS_WIN)\n"
          "#pragma warning(disable:4101 4102)\n"
          "#elif defined(__GNUC__)\n"
          "#pragma GCC diagnostic ignored \"-Wunused-variable\"\n"
          "#pragma GCC diagnostic ignored \"-Wunused-label\"\n"
          "#elif defined(__clang__)\n"
          "#pragma clang diagnostic ignored \"-Wunused-variable\"\n"
          "#pragma clang diagnostic ignored \"-Wunused-label\"\n"
          "#endif\n\n");
      write_namespace_open(file);
      ::fprintf(file,
          "void reflex_code_%s(reflex::Matcher& m)\n"
          "{\n"
          "  int c = 0;\n"
          "  m.FSM_INIT(c);\n", opt_.n.empty() ? "FSM" : opt_.n.c_str());
      for (const DFA::State *state = start; state != NULL; state = state->next)
      {
        ::fprintf(file, "\nS%u:\n", state->index);
        if (state == start)
          ::fprintf(file, "  m.FSM_FIND();\n");
        if (state->redo)
          ::fprintf(file, "  m.FSM_REDO();\n");
        else if (state->accept > 0)
          ::fprintf(file, "  m.FSM_TAKE(%u);\n", state->accept);
        for (Lookaheads::const_iterator i = state->tails.begin(); i != state->tails.end(); ++i)
          ::fprintf(file, "  m.FSM_TAIL(%u);\n", *i);
        for (Lookaheads::const_iterator i = state->heads.begin(); i != state->heads.end(); ++i)
          ::fprintf(file, "  m.FSM_HEAD(%u);\n", *i);
        if (state->edges.rbegin() != state->edges.rend() && state->edges.rbegin()->first == META_DED)
          ::fprintf(file, "  if (m.FSM_DENT()) goto S%u;\n", state->edges.rbegin()->second.second->index);
        bool peek = false; // if we need to read a character into c
        for (DFA::State::Edges::const_reverse_iterator i = state->edges.rbegin(); i != state->edges.rend(); ++i)
        {
#if WITH_COMPACT_DFA == -1
          Char lo = i->first;
          Char hi = i->second.first;
#else
          Char hi = i->first;
          Char lo = i->second.first;
#endif
          if (is_meta(lo))
          {
            do
            {
              if (lo == META_EOB || lo == META_EOL || lo == META_EWE || lo == META_BWE || lo == META_NWE || lo == META_WBE)
              {
                peek = true;
                break;
              }
              check_dfa_closure(i->second.second, 1, peek);
            } while (++lo <= hi);
          }
          else
          {
            Index target_index = Const::IMAX;
            if (i->second.second != NULL)
              target_index = i->second.second->index;
            DFA::State::Edges::const_reverse_iterator j = i;
            if (target_index == Const::IMAX && (++j == state->edges.rend() || is_meta(j->second.first)))
              break;
            peek = true;
          }
        }
        bool read = peek;
        bool elif = false;
#if WITH_COMPACT_DFA == -1
        for (DFA::State::Edges::const_reverse_iterator i = state->edges.rbegin(); i != state->edges.rend(); ++i)
        {
          Char lo = i->first;
          Char hi = i->second.first;
          Index target_index = Const::IMAX;
          if (i->second.second != NULL)
            target_index = i->second.second->index;
          if (read)
          {
            ::fprintf(file, "  c = m.FSM_CHAR();\n");
            read = false;
          }
          if (is_meta(lo))
          {
            do
            {
              switch (lo)
              {
                case META_EOB:
                case META_EOL:
                case META_EWE:
                case META_BWE:
                case META_NWE:
                case META_WBE:
                  ::fprintf(file, "  ");
                  if (elif)
                    ::fprintf(file, "else ");
                  ::fprintf(file, "if (m.FSM_META_%s(c)) {\n", meta_label[lo - META_MIN]);
                  gencode_dfa_closure(file, i->second.second, 2, peek);
                  ::fprintf(file, "  }\n");
                  elif = true;
                  break;
                default:
                  ::fprintf(file, "  ");
                  if (elif)
                    ::fprintf(file, "else ");
                  ::fprintf(file, "if (m.FSM_META_%s()) {\n", meta_label[lo - META_MIN]);
                  gencode_dfa_closure(file, i->second.second, 2, peek);
                  ::fprintf(file, "  }\n");
                  elif = true;
              }
            } while (++lo <= hi);
          }
          else
          {
            DFA::State::Edges::const_reverse_iterator j = i;
            if (target_index == Const::IMAX && (++j == state->edges.rend() || is_meta(j->second.first)))
              break;
            if (lo == hi)
            {
              ::fprintf(file, "  if (c == ");
              print_char(file, lo);
              ::fprintf(file, ")");
            }
            else if (hi == 0xFF)
            {
              ::fprintf(file, "  if (");
              print_char(file, lo);
              ::fprintf(file, " <= c)");
            }
            else
            {
              ::fprintf(file, "  if (");
              print_char(file, lo);
              ::fprintf(file, " <= c && c <= ");
              print_char(file, hi);
              ::fprintf(file, ")");
            }
            if (target_index == Const::IMAX)
            {
              if (peek)
                ::fprintf(file, " return m.FSM_HALT(c);\n");
              else
                ::fprintf(file, " return m.FSM_HALT();\n");
            }
            else
            {
              ::fprintf(file, " goto S%u;\n", target_index);
            }
          }
        }
#else
        for (DFA::State::Edges::const_reverse_iterator i = state->edges.rbegin(); i != state->edges.rend(); ++i)
        {
          Char hi = i->first;
          Char lo = i->second.first;
          if (is_meta(lo))
          {
            if (read)
            {
              ::fprintf(file, "  c = m.FSM_CHAR();\n");
              read = false;
            }
            do
            {
              switch (lo)
              {
                case META_EOB:
                case META_EOL:
                case META_EWE:
                case META_BWE:
                case META_NWE:
                case META_BWE:
                  ::fprintf(file, "  ");
                  if (elif)
                    ::fprintf(file, "else ");
                  ::fprintf(file, "if (m.FSM_META_%s(c)) {\n", meta_label[lo - META_MIN]);
                  gencode_dfa_closure(file, i->second.second, 2, peek);
                  ::fprintf(file, "  }\n");
                  elif = true;
                  break;
                default:
                  ::fprintf(file, "  ");
                  if (elif)
                    ::fprintf(file, "else ");
                  ::fprintf(file, "if (m.FSM_META_%s()) {\n", meta_label[lo - META_MIN]);
                  gencode_dfa_closure(file, i->second.second, 2, peek);
                  ::fprintf(file, "  }\n");
                  elif = true;
              }
            } while (++lo <= hi);
          }
        }
        for (DFA::State::Edges::const_iterator i = state->edges.begin(); i != state->edges.end(); ++i)
        {
          Char hi = i->first;
          Char lo = i->second.first;
          Index target_index = Const::IMAX;
          if (i->second.second != NULL)
            target_index = i->second.second->index;
          if (read)
          {
            ::fprintf(file, "  c = m.FSM_CHAR();\n");
            read = false;
          }
          if (!is_meta(lo))
          {
            DFA::State::Edges::const_iterator j = i;
            if (target_index == Const::IMAX && (++j == state->edges.end() || is_meta(j->second.first)))
              break;
            if (lo == hi)
            {
              ::fprintf(file, "  if (c == ");
              print_char(file, lo);
              ::fprintf(file, ")");
            }
            else if (hi == 0xFF)
            {
              ::fprintf(file, "  if (");
              print_char(file, lo);
              ::fprintf(file, " <= c)");
            }
            else
            {
              ::fprintf(file, "  if (");
              print_char(file, lo);
              ::fprintf(file, " <= c && c <= ");
              print_char(file, hi);
              ::fprintf(file, ")");
            }
            if (target_index == Const::IMAX)
            {
              if (peek)
                ::fprintf(file, " return m.FSM_HALT(c);\n");
              else
                ::fprintf(file, " return m.FSM_HALT();\n");
            }
            else
            {
              ::fprintf(file, " goto S%u;\n", target_index);
            }
          }
        }
#endif
        if (peek)
          ::fprintf(file, "  return m.FSM_HALT(c);\n");
        else
          ::fprintf(file, "  return m.FSM_HALT();\n");
      }
      ::fprintf(file, "}\n\n");
      if (opt_.p)
        write_predictor(file);
      write_namespace_close(file);
      if (file != stdout)
        ::fclose(file);
    }
  }
#else
  (void)start;
#endif
}

#ifndef WITH_NO_CODEGEN
void Pattern::check_dfa_closure(const DFA::State *state, int nest, bool& peek) const
{
  if (nest > 5)
    return;
  for (DFA::State::Edges::const_reverse_iterator i = state->edges.rbegin(); i != state->edges.rend(); ++i)
  {
#if WITH_COMPACT_DFA == -1
    Char lo = i->first;
    Char hi = i->second.first;
#else
    Char hi = i->first;
    Char lo = i->second.first;
#endif
    if (is_meta(lo))
    {
      do
      {
        if (lo == META_EOB || lo == META_EOL || lo == META_EWE || lo == META_BWE || lo == META_NWE || lo == META_WBE)
        {
          peek = true;
          break;
        }
        check_dfa_closure(i->second.second, nest + 1, peek);
      } while (++lo <= hi);
    }
  }
}
#endif

#ifndef WITH_NO_CODEGEN
void Pattern::gencode_dfa_closure(FILE *file, const DFA::State *state, int nest, bool peek) const
{
  bool elif = false;
  if (state->redo)
  {
    if (peek)
      ::fprintf(file, "%*sm.FSM_REDO(c);\n", 2*nest, "");
    else
      ::fprintf(file, "%*sm.FSM_REDO();\n", 2*nest, "");
  }
  else if (state->accept > 0)
  {
    if (peek)
      ::fprintf(file, "%*sm.FSM_TAKE(%u, c);\n", 2*nest, "", state->accept);
    else
      ::fprintf(file, "%*sm.FSM_TAKE(%u);\n", 2*nest, "", state->accept);
  }
  for (Lookaheads::const_iterator i = state->tails.begin(); i != state->tails.end(); ++i)
    ::fprintf(file, "%*sm.FSM_TAIL(%u);\n", 2*nest, "", *i);
  if (nest > 5)
    return;
  for (DFA::State::Edges::const_reverse_iterator i = state->edges.rbegin(); i != state->edges.rend(); ++i)
  {
#if WITH_COMPACT_DFA == -1
    Char lo = i->first;
    Char hi = i->second.first;
#else
    Char hi = i->first;
    Char lo = i->second.first;
#endif
    if (is_meta(lo))
    {
      do
      {
        switch (lo)
        {
          case META_EOB:
          case META_EOL:
          case META_EWE:
          case META_BWE:
          case META_NWE:
          case META_WBE:
            ::fprintf(file, "%*s", 2*nest, "");
            if (elif)
              ::fprintf(file, "else ");
            ::fprintf(file, "if (m.FSM_META_%s(c)) {\n", meta_label[lo - META_MIN]);
            gencode_dfa_closure(file, i->second.second, nest + 1, peek);
            ::fprintf(file, "%*s}\n", 2*nest, "");
            elif = true;
            break;
          default:
            ::fprintf(file, "%*s", 2*nest, "");
            if (elif)
              ::fprintf(file, "else ");
            ::fprintf(file, "if (m.FSM_META_%s()) {\n", meta_label[lo - META_MIN]);
            gencode_dfa_closure(file, i->second.second, nest + 1, peek);
            ::fprintf(file, "%*s}\n", 2*nest, "");
            elif = true;
        }
      } while (++lo <= hi);
    }
#if WITH_COMPACT_DFA == -1
    else
    {
      Index target_index = Const::IMAX;
      if (i->second.second != NULL)
        target_index = i->second.second->index;
      DFA::State::Edges::const_reverse_iterator j = i;
      if (target_index == Const::IMAX && (++j == state->edges.rend() || is_meta(j->second.first)))
        break;
      ::fprintf(file, "%*s", 2*nest, "");
      if (lo == hi)
      {
        ::fprintf(file, "if (c == ");
        print_char(file, lo);
        ::fprintf(file, ")");
      }
      else if (hi == 0xFF)
      {
        ::fprintf(file, "if (");
        print_char(file, lo);
        ::fprintf(file, " <= c)");
      }
      else
      {
        ::fprintf(file, "if (");
        print_char(file, lo);
        ::fprintf(file, " <= c && c <= ");
        print_char(file, hi);
        ::fprintf(file, ")");
      }
      if (target_index == Const::IMAX)
      {
        if (peek)
          ::fprintf(file, " return m.FSM_HALT(c);\n");
        else
          ::fprintf(file, " return m.FSM_HALT();\n");
      }
      else
      {
        ::fprintf(file, " goto S%u;\n", target_index);
      }
    }
  }
#else
  }
  for (DFA::State::Edges::const_iterator i = state->edges.begin(); i != state->edges.end(); ++i)
  {
    Char lo = i->first;
    Char hi = i->second.first;
    if (!is_meta(lo))
    {
      Index target_index = Const::IMAX;
      if (i->second.second != NULL)
        target_index = i->second.second->index;
      DFA::State::Edges::const_iterator j = i;
      if (target_index == Const::IMAX && (++j == state->edges.end() || is_meta(j->second.first)))
        break;
      ::fprintf(file, "%*s", 2*nest, "");
      if (lo == hi)
      {
        ::fprintf(file, "if (c == ");
        print_char(file, lo);
        ::fprintf(file, ")");
      }
      else if (hi == 0xFF)
      {
        ::fprintf(file, "if (");
        print_char(file, lo);
        ::fprintf(file, " <= c)");
      }
      else
      {
        ::fprintf(file, "if (");
        print_char(file, lo);
        ::fprintf(file, " <= c && c <= ");
        print_char(file, hi);
        ::fprintf(file, ")");
      }
      if (target_index == Const::IMAX)
      {
        if (peek)
          ::fprintf(file, " return m.FSM_HALT(c);\n");
        else
          ::fprintf(file, " return m.FSM_HALT();\n");
      }
      else
      {
        ::fprintf(file, " goto S%u;\n", target_index);
      }
    }
  }
#endif
}
#endif

void Pattern::graph_dfa(const DFA::State *start) const
{
#ifndef WITH_NO_CODEGEN
  for (std::vector<std::string>::const_iterator i = opt_.f.begin(); i != opt_.f.end(); ++i)
  {
    const std::string& filename = *i;
    size_t len = filename.length();
    if ((len > 3 && filename.compare(len - 3, 3, ".gv") == 0)
     || (len > 4 && filename.compare(len - 4, 4, ".dot") == 0))
    {
      FILE *file = NULL;
      int err = 0;
      if (filename.compare(0, 7, "stdout.") == 0)
        file = stdout;
      else if (filename.at(0) == '+')
        err = reflex::fopen_s(&file, filename.c_str() + 1, "a");
      else
        err = reflex::fopen_s(&file, filename.c_str(), "w");
      if (!err && file)
      {
        ::fprintf(file, "digraph %s {\n\t\trankdir=LR;\n\t\tconcentrate=true;\n\t\tnode [fontname=\"ArialNarrow\"];\n\t\tedge [fontname=\"Courier\"];\n\n\t\tinit [root=true,peripheries=0,label=\"%s\",fontname=\"Courier\"];\n\t\tinit -> N%p;\n", opt_.n.empty() ? "FSM" : opt_.n.c_str(), opt_.n.c_str(), (void*)start);
        for (const DFA::State *state = start; state != NULL; state = state->next)
        {
          if (opt_.g > 1 && state != start && state->first != 0 && state->first < cut_)
            continue;
          if (state == start)
            ::fprintf(file, "\n/*START*/\t");
          if (state->redo)
            ::fprintf(file, "\n/*REDO*/\t");
          else if (state->accept)
            ::fprintf(file, "\n/*ACCEPT %u*/\t", state->accept);
          for (Lookaheads::const_iterator i = state->heads.begin(); i != state->heads.end(); ++i)
            ::fprintf(file, "\n/*HEAD %u*/\t", *i);
          for (Lookaheads::const_iterator i = state->tails.begin(); i != state->tails.end(); ++i)
            ::fprintf(file, "\n/*TAIL %u*/\t", *i);
          if (state != start && !state->accept && state->heads.empty() && state->tails.empty())
            ::fprintf(file, "\n/*STATE*/\t");
          ::fprintf(file, "N%p [label=\"", (void*)state);
#ifdef DEBUG
          size_t k = 1;
          size_t n = std::sqrt(state->size()) + 0.5;
          const char *sep = "";
          for (Positions::const_iterator i = state->begin(); i != state->end(); ++i)
          {
            ::fprintf(file, "%s", sep);
            if (i->accept())
            {
              ::fprintf(file, "(%u)", i->accepts());
            }
            else
            {
              if (i->iter())
                ::fprintf(file, "%u.", i->iter());
              ::fprintf(file, "%u", i->loc());
            }
            if (i->lazy())
              ::fprintf(file, "?%u", i->lazy());
            if (i->anchor())
              ::fprintf(file, "^");
            if (i->ticked())
              ::fprintf(file, "'");
            if (i->negate())
              ::fprintf(file, "-");
            if (k++ % n)
              sep = " ";
            else
              sep = "\\n";
          }
          if ((state->accept && !state->redo) || !state->heads.empty() || !state->tails.empty())
            ::fprintf(file, "\\n");
#endif
          if (opt_.g && lbk_ > 0)
          {
            if (state->first == DFA::KEEP_PATH)
              ::fprintf(file, "{keep}");
            else if (state->first == DFA::LOOP_PATH)
              ::fprintf(file, "{loop}");
            else if (state->first > 0)
              ::fprintf(file, "{%u}", state->first);
            if (state->index > 0)
              ::fprintf(file, "<%u>", state->index);
          }
          if (state->accept > 0 && !state->redo)
            ::fprintf(file, "[%u]", state->accept);
          for (Lookaheads::const_iterator i = state->tails.begin(); i != state->tails.end(); ++i)
            ::fprintf(file, "%u>", *i);
          for (Lookaheads::const_iterator i = state->heads.begin(); i != state->heads.end(); ++i)
            ::fprintf(file, "<%u", *i);
          if (opt_.g && lbk_ > 0 && state->first > 0 && state->first <= cut_)
            ::fprintf(file, "\",style=dotted];\n");
          else if (state->redo)
            ::fprintf(file, "\",style=dashed,peripheries=1];\n");
          else if (state->accept > 0)
            ::fprintf(file, "\",peripheries=2];\n");
          else if (!state->heads.empty())
            ::fprintf(file, "\",style=dashed,peripheries=2];\n");
          else
            ::fprintf(file, "\"];\n");
          if (opt_.g > 1 && lbk_ > 0 && state->accept > 0)
            continue;
          for (DFA::State::Edges::const_iterator i = state->edges.begin(); i != state->edges.end(); ++i)
          {
            if (i->second.second == NULL)
              continue;
            if (opt_.g > 1 && lbk_ > 0 && i->second.second->first != 0 && i->second.second->first <= cut_)
              continue;
#if WITH_COMPACT_DFA == -1
            Char lo = i->first;
            Char hi = i->second.first;
#else
            Char hi = i->first;
            Char lo = i->second.first;
#endif
            if (!is_meta(lo))
            {
              ::fprintf(file, "\t\tN%p -> N%p [label=\"", (void*)state, (void*)i->second.second);
              if (lo >= '\a' && lo <= '\r')
                ::fprintf(file, "\\\\%c", "abtnvfr"[lo - '\a']);
              else if (lo == '"')
                ::fprintf(file, "\\\"");
              else if (lo == '\\')
                ::fprintf(file, "\\\\");
              else if (std::isgraph(lo))
                ::fprintf(file, "%c", lo);
              else if (lo < 8)
                ::fprintf(file, "\\\\%u", lo);
              else
                ::fprintf(file, "\\\\x%02x", lo);
              if (lo != hi)
              {
                ::fprintf(file, "-");
                if (hi >= '\a' && hi <= '\r')
                  ::fprintf(file, "\\\\%c", "abtnvfr"[hi - '\a']);
                else if (hi == '"')
                  ::fprintf(file, "\\\"");
                else if (hi == '\\')
                  ::fprintf(file, "\\\\");
                else if (std::isgraph(hi))
                  ::fprintf(file, "%c", hi);
                else if (hi < 8)
                  ::fprintf(file, "\\\\%u", hi);
                else
                  ::fprintf(file, "\\\\x%02x", hi);
              }
              ::fprintf(file, "\"");
              if (opt_.g && lbk_ > 0 && i->second.second->first > 0 && i->second.second->first <= cut_)
                ::fprintf(file, ",style=dotted");
              ::fprintf(file, "];\n");
            }
            else
            {
              do
              {
                ::fprintf(file, "\t\tN%p -> N%p [label=\"%s\",style=\"dashed\"];\n", (void*)state, (void*)i->second.second, meta_label[lo - META_MIN]);
              } while (++lo <= hi);
            }
          }
          if (state->redo)
            ::fprintf(file, "\t\tN%p -> R%p;\n\t\tR%p [peripheries=0,label=\"redo\"];\n", (void*)state, (void*)state, (void*)state);
        }
        ::fprintf(file, "}\n");
        if (file != stdout)
          ::fclose(file);
      }
    }
  }
#else
  (void)start;
#endif
}

void Pattern::export_code() const
{
#ifndef WITH_NO_CODEGEN
  if (nop_ == 0)
    return;
  for (std::vector<std::string>::const_iterator i = opt_.f.begin(); i != opt_.f.end(); ++i)
  {
    const std::string& filename = *i;
    size_t len = filename.length();
    if ((len > 2 && filename.compare(len - 2, 2, ".h"  ) == 0)
     || (len > 3 && filename.compare(len - 3, 3, ".hh" ) == 0)
     || (len > 4 && filename.compare(len - 4, 4, ".hpp") == 0)
     || (len > 4 && filename.compare(len - 4, 4, ".hxx") == 0)
     || (len > 3 && filename.compare(len - 3, 3, ".cc" ) == 0)
     || (len > 4 && filename.compare(len - 4, 4, ".cpp") == 0)
     || (len > 4 && filename.compare(len - 4, 4, ".cxx") == 0))
    {
      FILE *file = NULL;
      int err = 0;
      if (filename.compare(0, 7, "stdout.") == 0)
        file = stdout;
      else if (filename.at(0) == '+')
        err = reflex::fopen_s(&file, filename.c_str() + 1, "a");
      else
        err = reflex::fopen_s(&file, filename.c_str(), "w");
      if (!err && file)
      {
        ::fprintf(file, "#ifndef REFLEX_CODE_DECL\n#include <reflex/pattern.h>\n#define REFLEX_CODE_DECL const reflex::Pattern::Opcode\n#endif\n\n");
        write_namespace_open(file);
        ::fprintf(file, "REFLEX_CODE_DECL reflex_code_%s[%u] =\n{\n", opt_.n.empty() ? "FSM" : opt_.n.c_str(), nop_);
        for (Index i = 0; i < nop_; ++i)
        {
          Opcode opcode = opc_[i];
          Char lo = lo_of(opcode);
          Char hi = hi_of(opcode);
          ::fprintf(file, "  0x%08X, // %u: ", opcode, i);
          if (is_opcode_redo(opcode))
          {
            ::fprintf(file, "REDO\n");
          }
          else if (is_opcode_take(opcode))
          {
            ::fprintf(file, "TAKE %u\n", long_index_of(opcode));
          }
          else if (is_opcode_tail(opcode))
          {
            ::fprintf(file, "TAIL %u\n", long_index_of(opcode));
          }
          else if (is_opcode_head(opcode))
          {
            ::fprintf(file, "HEAD %u\n", long_index_of(opcode));
          }
          else if (is_opcode_halt(opcode))
          {
            ::fprintf(file, "HALT\n");
          }
          else
          {
            Index index = index_of(opcode);
            if (index == Const::HALT)
            {
              ::fprintf(file, "HALT ON ");
            }
            else
            {
              if (index == Const::LONG)
              {
                opcode = opc_[++i];
                index = long_index_of(opcode);
                ::fprintf(file, "GOTO\n  0x%08X, // %u:  FAR %u ON ", opcode, i, index);
              }
              else
              {
                ::fprintf(file, "GOTO %u ON ", index);
              }
            }
            if (!is_meta(lo))
            {
              print_char(file, lo, true);
              if (lo != hi)
              {
                ::fprintf(file, "-");
                print_char(file, hi, true);
              }
            }
            else
            {
              ::fprintf(file, "%s", meta_label[lo - META_MIN]);
            }
            ::fprintf(file, "\n");
          }
        }
        ::fprintf(file, "};\n\n");
        if (opt_.p)
          write_predictor(file);
        write_namespace_close(file);
        if (file != stdout)
          ::fclose(file);
      }
    }
  }
#endif
}

void Pattern::analyze_dfa(DFA::State *start)
{
  DBGLOG("BEGIN Pattern::analyze_dfa()");
  cut_ = 0;
  lbk_ = 0;
  lbm_ = 0;
  cbk_.reset();
  fst_.reset();
  std::set<DFA::State*> start_states;
  if (start->accept == 0)
  {
    // Analyze DFA with a breadth-first search to produce a set of new starting states for more accurate match prediction.
    // A good starting state is one with few edges (chars); we want select a new set of starting states with few edges.
    // We make a DFA graph s-t cut with few edges from which to predict matches.
    // We also cut away edges to states that precede the new starting states, because these repetitions can be ignored.
    // Characters on removed edges are recorded so we can look back to find a full match with the regex matcher.
    // We name an edge a "backedge" when it points to a state before the new starting states, i.e. a loop, not necessarily a cycle.
    bool backedge = false;       // if we found a backedge during breadth-first search
    bool has_backedge = false;   // if we found a backedge after the last cut to a state after the cut, not before the cut
    uint16_t fin_depth = 0xffff; // shortest distance to a final state
    uint16_t fin_count = 0;      // number of characters to the final states cut off that are not included in the current cut
    std::set<DFA::State*> states;     // current set of breadth-first-search states
    std::set<DFA::State*> fin_states; // set of states to final states not included in the current cut
    reflex::ORanges<Char> chars;      // set of characters on edges before the current cut, the lookback set
    // current cut
    bool cut_backedge = false;   // if we cound a backedge for the current cut
    uint16_t cut_depth = 0;      // breadth-first search depth of the current cut
    uint16_t cut_fin_depth = 0;  // shortest distance to a final state for the current cut
    uint16_t cut_span = 0;       // length of the current cut, from the cut to the last state searched
    uint16_t cut_count = 0xffff; // number of characters at the start of the current cut
    uint16_t min_count = 0xffff; // min count of characters over the span of the current cut
    uint16_t max_count = 0;      // max of number of characters over the span of the current cut
    uint8_t max_freq = 0;        // max character frequency over the span of the current cut
    std::set<DFA::State*> cut_states;     // set of states positioned on the left of the cut
    std::set<DFA::State*> cut_fin_states; // set of states to final states not included in the current cut
    reflex::ORanges<Char> cut_chars;      // set of characters on edges before the current cut, the lookback set
    // best cut saved
    bool best_cut_backedge = false;
    uint16_t best_cut_depth = 0;
    uint16_t best_cut_fin_depth = 0xffff;
    uint16_t best_cut_span = 0;
    uint16_t best_cut_count = 0xffff;
    uint16_t best_min_count = 0xffff;
    std::set<DFA::State*> best_cut_states;
    std::set<DFA::State*> best_cut_fin_states;
    reflex::ORanges<Char> best_cut_chars;
    // start analyzing the DFA from the start state using a breadth-first search following forward edges to non-visited states
    start->first = 1;
    states.insert(states.begin(), start);
    std::set<DFA::State*> next_states;
    reflex::ORanges<uint16_t> next_chars;
    bool searching = false;
    for (uint16_t depth = 0; depth < DFA::MAX_DEPTH; ++depth)
    {
      next_states.clear();
      next_chars.clear();
      bool is_more = fin_count == 0;
      for (std::set<DFA::State*>::iterator it = states.begin(); it != states.end(); ++it)
      {
        DFA::State *state = *it;
        DFA::State::Edges::iterator edge, end = state->edges.end();
        for (edge = state->edges.begin(); edge != end; ++edge)
        {
          DFA::State *next_state = edge->second.second;
          if (next_state == NULL)
            continue;
          Char lo = edge->first;
          Char hi = edge->second.first;
          if (depth == 0 && !is_meta(lo))
            for (Char ch = lo; ch <= hi; ++ch)
              fst_.set(ch);
          if ((lo <= '\n' && hi >= '\n') || next_state->accept > 0 || next_state->edges.empty() || is_meta(lo) || is_meta(next_state->edges.rbegin()->first))
          {
            // reset the next state BFS depth to zero, to prevent edges to next states to be marked as backedges
            next_state->first = lo <= '\n' && hi >= '\n' ? DFA::KEEP_PATH : 0;
            // separate states that reach a final state or a state with a meta edge
            fin_states.insert(state);
            // depth
            if (fin_depth == 0xffff)
              fin_depth = depth;
            // count number of edges from this state to the "final" state
            fin_count += hi - lo + 1;
            continue;
          }
          if (next_state->first == 0 || next_state->first > cut_depth + 1U)
            next_chars.insert(lo, hi);
          if (next_state->first == 0)
          {
            next_state->first = depth + 2;
          }
          else if (next_state->first <= state->first)
          {
            chars.insert(lo, hi);
            if (next_state->first > cut_depth + 1U) // has a backedge to a state after the new cut?
              has_backedge = true;
            backedge = true; // has a backedge to a previous state
            continue;
          }
          next_states.insert(next_state);
        }
      }
      uint16_t count = static_cast<uint16_t>(next_chars.count()); // never more than 256
      for (reflex::ORanges<uint16_t>::const_iterator range = next_chars.begin(); range != next_chars.end(); ++range)
        for (Char ch = range->first; ch < range->second; ++ch)
          max_freq = std::max(max_freq, frequency(static_cast<uint8_t>(ch)));
      uint16_t prev_min_count = min_count;
      if (count + fin_count > max_count)
        max_count = count + fin_count;
      if (count + fin_count < min_count)
        min_count = count + fin_count;
      if (is_more)
        cut_span = depth - cut_depth;
      DBGLOGN("depth %hu has-backedge=%d cut-len=%hu count=%zu fin-count=%hu min-count=%hu max-count=%hu max-freq=%hu", depth, has_backedge, cut_span, count, fin_count, min_count, max_count, max_freq);
      if (searching)
      {
        // make a cut?
        bool make_cut = false;
        if (has_backedge)
        {
          make_cut = (max_count > 4 || max_freq > 251);
        }
        else if (cut_span >= 3 && prev_min_count < 0xffff)
        {
          make_cut = (prev_min_count > 2 * min_count ||
              (prev_min_count > 16 && min_count <= 16) ||
              (prev_min_count >  8 && min_count <=  8) ||
              (prev_min_count >  1 && min_count <=  1));
        }
        if (make_cut)
        {
          // determine if this is a better cut than the last
          bool better = false;
          if (cut_span <= 2)
            better = (cut_span > best_cut_span);
          else
            better = (best_min_count >= prev_min_count && cut_span >= best_cut_span);
          if (better)
          {
            DBGLOGN("new improved cut-depth=%hu %hu/%hu %hu/%hu %hu/%hu", cut_depth, cut_span, best_cut_span, min_count, best_min_count, cut_count, best_cut_count);
            best_cut_states = cut_states;
            best_cut_fin_states = cut_fin_states;
            best_cut_count = cut_count;
            best_cut_chars = cut_chars;
            best_cut_backedge = cut_backedge;
            best_cut_depth = cut_depth;
            best_cut_fin_depth = cut_fin_depth;
            best_cut_span = cut_span;
            best_min_count = prev_min_count;
            searching = false;
          }
        }
      }
      // are we done?
      if (count <= fin_count || !is_more)
      {
        if (is_more)
          ++cut_span;
        break;
      }
      if (!searching)
      {
        if (depth > 0)
        {
          // recalculate count at the cut, now without the self-edges that will be ignored in the cut-off sub-pattern DFA
          next_chars.clear();
          for (std::set<DFA::State*>::iterator it = states.begin(); it != states.end(); ++it)
          {
            DFA::State *state = *it;
            DFA::State::Edges::iterator edge, end = state->edges.end();
            for (edge = state->edges.begin(); edge != end; ++edge)
            {
              DFA::State *next_state = edge->second.second;
              if (next_state == NULL)
                continue;
              Char lo = edge->first;
              Char hi = edge->second.first;
              if ((lo <= '\n' && hi >= '\n') || next_state->accept > 0 || next_state->edges.empty() || is_meta(lo) || is_meta(next_state->edges.rbegin()->first))
                continue;
              if (next_state->first == 0 || next_state->first > depth + 1U)
                next_chars.insert(lo, hi);
            }
          }
          count = static_cast<uint16_t>(next_chars.count()); // never more than 256
        }
        // save the previous metrics
        cut_states = states;
        cut_fin_states = fin_states;
        cut_count = count + fin_count;
        cut_chars += chars;
        cut_backedge = backedge;
        cut_depth = depth;
        cut_fin_depth = fin_depth == 0xffff ? depth : fin_depth;
        // prepare to find another cut
        chars.clear();
        has_backedge = false;
        max_freq = 0;
        max_count = count;
        min_count = count;
        searching = true;
      }
      chars += next_chars;
      states.swap(next_states);
    }
    // did we find more than one cut?
    if (best_cut_depth > 0 || best_cut_backedge || best_cut_span > 0)
    {
      // if the current cut is not a better cut, then use the last best cut
      bool better = false;
      if (best_cut_count > cut_count || (best_cut_span == 1 && best_cut_count == cut_count))
      {
        if (cut_span > best_cut_span)
          better = true;
        else if (cut_span >= 2 || cut_span == best_cut_span)
          better = ((best_min_count > 16 && min_count <= 16) ||
                    (best_min_count >  8 && min_count <=  8));
      }
      if (better)
      {
        // the current cut is best
        DBGLOGN("cut: %hu/%hu %hu/%hu %hu/%hu", cut_span, best_cut_span, cut_count, best_cut_count, min_count, best_min_count);
      }
      else
      {
        // the last best cut is best
        DBGLOGN("best: %hu/%hu %hu/%hu %hu/%hu", cut_span, best_cut_span, cut_count, best_cut_count, min_count, best_min_count);
        cut_states = best_cut_states;
        cut_fin_states = best_cut_fin_states;
        cut_count = best_cut_count;
        cut_chars = best_cut_chars;
        cut_backedge = best_cut_backedge;
        cut_depth = best_cut_depth;
        cut_fin_depth = best_cut_fin_depth;
      }
    }
    // did we find a suitable cut?
    if (cut_depth > 0 || cut_backedge)
    {
      cut_ = cut_depth + 1;
      std::set<DFA::State*> sweep[HFA::MAX_DEPTH];
      // include final states in the cut states
      cut_states.insert(cut_fin_states.begin(), cut_fin_states.end());
      // new start states
      std::set<DFA::State*>::iterator fin = cut_states.end();
      for (std::set<DFA::State*>::iterator it = cut_states.begin(); it != fin; ++it)
      {
        DFA::State *state = *it;
        start = dfa_.state();
        start->first = 1;
        DFA::State::Edges::iterator end = state->edges.end();
        // add edges to the new start state, ignore backedges to states <= cut
        for (DFA::State::Edges::iterator edge = state->edges.begin(); edge != end; ++edge)
        {
          DFA::State *next_state = edge->second.second;
          if (next_state == NULL)
            continue;
          if (next_state->first == 0 || next_state->first > cut_)
          {
            sweep[0].insert(next_state);
            start->edges[edge->first] = edge->second;
          }
        }
        if (!start->edges.empty())
          start_states.insert(start);
      }
      // sweep forward over states up to 8 levels (max bitap length) or 16 levels (HFA::MAX_DEPTH) to mark states
      size_t depth;
      for (depth = 0; depth < 7 && !sweep[depth].empty(); ++depth)
      {
        fin = sweep[depth].end();
        for (std::set<DFA::State*>::iterator it = sweep[depth].begin(); it != fin; ++it)
        {
          DFA::State *state = *it;
          if (state->accept > 0 || state->edges.empty() || is_meta(state->edges.rbegin()->first))
            continue;
          // add edges from the new start state, ignore backedges to states <= cut
          bool can = false, any = false;
          DFA::State::Edges::iterator end = state->edges.end();
          for (DFA::State::Edges::iterator edge = state->edges.begin(); edge != end; ++edge)
          {
            DFA::State *next_state = edge->second.second;
            if (next_state == NULL)
              continue;
            Char lo = edge->first;
            Char hi = edge->second.first;
            if ((lo <= '\n' && hi >= '\n') || state->first == DFA::KEEP_PATH)
            {
              any = true;
              if (next_state->first != DFA::KEEP_PATH)
              {
                next_state->first = DFA::KEEP_PATH;
                sweep[depth + 1].insert(next_state);
              }
            }
            else if (next_state->first == 0 || next_state->first > cut_)
            {
              any = true;
              if (next_state->first != DFA::LOOP_PATH)
                sweep[depth + 1].insert(next_state);
            }
            else
            {
              can = true; // can reach a backedge
              cut_backedge = true; // loops are present
              cut_chars.insert(lo, hi);
            }
          }
          // mark state as to can reach a backedge (in a loop) / only reaches backedges (dead path)
          if (can && state->first != DFA::KEEP_PATH)
            state->first = any ? DFA::LOOP_PATH : DFA::DEAD_PATH;
        }
      }
      // sweep backward to mark states loopy or dead when all out paths lead to a dead state
      for (; depth > 0; --depth)
      {
        fin = sweep[depth - 1].end();
        for (std::set<DFA::State*>::iterator it = sweep[depth - 1].begin(); it != fin; ++it)
        {
          DFA::State *state = *it;
          if (state->first == DFA::KEEP_PATH || state->accept > 0 || state->edges.empty() || is_meta(state->edges.rbegin()->first))
            continue;
          bool all = true;
          DFA::State::Edges::iterator end = state->edges.end();
          for (DFA::State::Edges::iterator edge = state->edges.begin(); edge != end; ++edge)
          {
            DFA::State *next_state = edge->second.second;
            if (next_state == NULL)
              continue;
            Char lo = edge->first;
            Char hi = edge->second.first;
            if (next_state->first == DFA::DEAD_PATH)
            {
              cut_chars.insert(lo, hi);
            }
            else if (next_state->first == DFA::LOOP_PATH)
            {
              all = false; // not all are marked dead
              state->first = DFA::LOOP_PATH;
              cut_chars.insert(lo, hi);
            }
            else
            {
              all = false; // not all are marked dead
            }
          }
          if (all)
            state->first = DFA::DEAD_PATH; // all edges only reach backedges (dead path)
        }
      }
      // record lookback chars and remove start states with edges that are all dead paths
      DBGLOGN("new start state with edges:");
      next_chars.clear();
      fin = start_states.end();
      std::set<DFA::State*>::iterator it = start_states.begin();
      while (it != fin)
      {
        bool all = true;
        DFA::State *state = *it;
        DFA::State::Edges::iterator end = state->edges.end();
        for (DFA::State::Edges::iterator edge = state->edges.begin(); edge != end; ++edge)
        {
          DFA::State *next_state = edge->second.second;
          if (next_state == NULL)
            continue;
          if (next_state->first == DFA::DEAD_PATH)
          {
            cut_chars.insert(edge->first, edge->second.first);
          }
          else if (next_state->first == DFA::LOOP_PATH)
          {
            all = false; // not all are marked dead
            cut_chars.insert(edge->first, edge->second.first);
            next_chars.insert(edge->first, edge->second.first);
          }
          else
          {
            all = false; // not all are marked dead
            next_chars.insert(edge->first, edge->second.first);
          }
#ifdef DEBUG
          if (next_state->first != DFA::DEAD_PATH)
            DBGLOGN("%u: %d..%d -> %u", state->first, edge->first, edge->second.first, next_state->first);
#endif
        }
        if (all)
          start_states.erase(it++);
        else
          ++it;
      }
      // set the pattern's lookback distance lbk, lookback min distance lbm, and lookback characters cbk for the pattern matcher
      lbk_ = cut_backedge ? 0xffff : cut_depth;
      lbm_ = cut_fin_depth;
      for (reflex::ORanges<Char>::iterator range = cut_chars.begin(); range != cut_chars.end(); ++range)
        for (Char ch = range->first; ch < range->second; ++ch)
          cbk_.set(ch, 1);
#ifdef DEBUG
      DBGLOGN("backedge=%d cut-depth=%hu cut-fin-depth=%hu cut=%u lookbacks:", cut_backedge, cut_depth, cut_fin_depth, cut_);
      for (reflex::ORanges<Char>::iterator range = cut_chars.begin(); range != cut_chars.end(); ++range)
      {
        Char lo = range->first;
        Char hi = range->second - 1;
        if (lo > 32 && lo < 127)
          DBGLOGA(" %c", lo);
        else
          DBGLOGA(" %02x", lo);
        if (lo < hi)
        {
          DBGLOGA("..");
          if (hi > 32 && hi < 127)
            DBGLOGA("%c", hi);
          else
            DBGLOGA("%02x", hi);
        }
      }
#endif
    }
  }
  if (lbk_ == 0)
  {
    DFA::State *state = start;
    one_ = true;
    while (state->accept == 0)
    {
      if (state->edges.size() != 1 || !state->heads.empty())
      {
        one_ = false;
        break;
      }
      Char lo = state->edges.begin()->first;
      if (!is_meta(lo) && lo == state->edges.begin()->second.first)
      {
        if (len_ >= 255)
        {
          one_ = false;
          break;
        }
        chr_[len_++] = static_cast<uint8_t>(lo);
      }
      else
      {
        one_ = false;
        break;
      }
      DFA::State *next = state->edges.begin()->second.second;
      if (next == NULL)
      {
        one_ = false;
        break;
      }
      state = next;
    }
    if (state != NULL && ((state->accept > 0 && !state->edges.empty()) || state->redo))
      one_ = false;
    if (state != NULL && (len_ == 0 || state->accept == 0))
      start_states.insert(state);
  }
  min_ = 0;
  std::memset(bit_, 0xFF, sizeof(bit_));
  std::memset(pmh_, 0xFF, sizeof(pmh_));
  std::memset(pma_, 0xFF, sizeof(pma_));
  if (!start_states.empty())
  {
    gen_predict_match(start_states);
#ifdef DEBUG
    for (Char i = 0; i < 256; ++i)
    {
      if (bit_[i] != 0xFF)
      {
        if (isprint(i))
          DBGLOGN("bit['%c'] = %02x", i, bit_[i]);
        else
          DBGLOGN("bit[%3d] = %02x", i, bit_[i]);
      }
    }
    for (Hash i = 0; i < Const::HASH; ++i)
    {
      if (pmh_[i] != 0xFF)
      {
        if (isprint(pmh_[i]))
          DBGLOGN("pmh['%c'] = %02x", i, pmh_[i]);
        else
          DBGLOGN("pmh[%3d] = %02x", i, pmh_[i]);
      }
    }
    for (Hash i = 0; i < Const::HASH; ++i)
    {
      if (pma_[i] != 0xFF)
      {
        if (isprint(pma_[i]))
          DBGLOGN("pma['%c'] = %02x", i, pma_[i]);
        else
          DBGLOGN("pma[%3d] = %02x", i, pma_[i]);
      }
    }
#endif
  }
  DBGLOG("min = %zu len = %zu", min_, len_);
  DBGLOG("END Pattern::analyze_dfa()");
}

void Pattern::gen_predict_match(std::set<DFA::State*> states)
{
  min_ = 8;
  std::map<DFA::State*,ORanges<Hash> > hashes[8];
  gen_predict_match_start(states, hashes[0]);
  for (int level = 1; level < 8; ++level)
    for (std::map<DFA::State*,ORanges<Hash> >::iterator from = hashes[level - 1].begin(); from != hashes[level - 1].end(); ++from)
      gen_predict_match_transitions(level, from->first, from->second, hashes[level]);
}

void Pattern::gen_predict_match_start(std::set<DFA::State*> states, std::map<DFA::State*,ORanges<Hash> >& hashes)
{
  bool empty = true;
  for (std::set<DFA::State*>::iterator it = states.begin(); it != states.end(); ++it)
  {
    DFA::State *state = *it;
    if (!state->edges.empty())
      empty = false;
    for (DFA::State::Edges::iterator edge = state->edges.begin(); edge != state->edges.end(); ++edge)
    {
      Char lo = edge->first;
      if (is_meta(lo))
      {
        min_ = 0;
        break;
      }
      DFA::State *next_state = edge->second.second;
      // ignore edges from a state to a state with breadth-first depth <= cut
      if (next_state != NULL && lbk_ > 0)
        if (next_state->first > 0 && next_state->first <= cut_)
          continue;
      if (next_state != NULL && (next_state->edges.empty() || is_meta(next_state->edges.begin()->first)))
        next_state = NULL;
      bool accept = next_state == NULL || next_state->accept > 0 || is_meta(next_state->edges.rbegin()->first);
      Char hi = edge->second.first;
      if (next_state != NULL)
        hashes[next_state].insert(lo, hi);
      uint8_t mask = ~(1 << 6);
      if (accept)
      {
        mask &= ~(1 << 7);
        min_ = 1;
      }
      for (Char ch = lo; ch <= hi; ++ch)
      {
        bit_[ch] &= ~1;
        pmh_[ch] &= ~1;
        pma_[ch] &= mask;
      }
      DBGLOG("0 bitap %u..%u -> %p", lo, hi, next_state);
    }
  }
  if (empty)
    min_ = 0;
}

void Pattern::gen_predict_match_transitions(size_t level, DFA::State *state, const ORanges<Hash>& previous, std::map<DFA::State*,ORanges<Hash> >& hashes)
{
  for (DFA::State::Edges::iterator edge = state->edges.begin(); edge != state->edges.end(); ++edge)
  {
    Char lo = edge->first;
    if (is_meta(lo))
    {
      if (min_ > level)
        min_ = level;
      break;
    }
    DFA::State *next_state = level < 7 ? edge->second.second : NULL;
    // ignore edges from a state to a state with breadth-first depth <= cut
    if (next_state != NULL && lbk_ > 0)
      if (next_state->first > 0 && next_state->first <= cut_)
        continue;
    if (next_state != NULL && (next_state->edges.empty() || is_meta(next_state->edges.begin()->first)))
      next_state = NULL;
    bool accept = next_state == NULL || next_state->accept > 0 || is_meta(next_state->edges.rbegin()->first);
    ORanges<Hash> *next_hashes = next_state != NULL ? &hashes[next_state] : NULL;
    if (accept && min_ > level)
      min_ = level + 1;
    if (level < 4)
    {
      uint8_t pmh_mask = ~(1 << level);
      uint8_t pma_mask = ~(1 << (6 - 2 * level));
      if (level == 3 || accept)
        pma_mask &= ~(1 << (7 - 2 * level));
      Char hi = edge->second.first;
      if (level <= min_)
        for (Char ch = lo; ch <= hi; ++ch)
          bit_[ch] &= pmh_mask;
      DBGLOG("%zu bitap %p: %u..%u -> %p", level, state, lo, hi, next_state);
      if (next_hashes != NULL)
      {
        for (ORanges<Hash>::iterator prev_range = previous.begin(); prev_range != previous.end(); ++prev_range)
        {
          Hash prev_lo = prev_range->first;
          Hash prev_hi = prev_range->second;
          for (Hash prev = prev_lo; prev < prev_hi; ++prev)
          {
            for (Char ch = lo; ch <= hi; ++ch)
            {
              Hash h = hash(prev, static_cast<uint8_t>(ch));
              pmh_[h] &= pmh_mask;
              pma_[h] &= pma_mask;
              next_hashes->insert(h);
            }
          }
        }
      }
      else
      {
        for (ORanges<Hash>::iterator prev_range = previous.begin(); prev_range != previous.end(); ++prev_range)
        {
          Hash prev_lo = prev_range->first;
          Hash prev_hi = prev_range->second;
          for (Hash prev = prev_lo; prev < prev_hi; ++prev)
          {
            for (Char ch = lo; ch <= hi; ++ch)
            {
              Hash h = hash(prev, static_cast<uint8_t>(ch));
              pmh_[h] &= pmh_mask;
              pma_[h] &= pma_mask;
            }
          }
        }
      }
    }
    else if (level <= min_)
    {
      uint8_t pmh_mask = ~(1 << level);
      Char hi = edge->second.first;
      for (Char ch = lo; ch <= hi; ++ch)
        bit_[ch] &= pmh_mask;
      DBGLOG("%zu bitap %p: %u..%u -> %p", level, state, lo, hi, next_state);
      if (next_hashes != NULL)
      {
        for (ORanges<Hash>::iterator prev_range = previous.begin(); prev_range != previous.end(); ++prev_range)
        {
          Hash prev_lo = prev_range->first;
          Hash prev_hi = prev_range->second;
          for (Hash prev = prev_lo; prev < prev_hi; ++prev)
          {
            for (Char ch = lo; ch <= hi; ++ch)
            {
              Hash h = hash(prev, static_cast<uint8_t>(ch));
              pmh_[h] &= pmh_mask;
              next_hashes->insert(h);
            }
          }
        }
      }
      else
      {
        for (ORanges<Hash>::iterator prev_range = previous.begin(); prev_range != previous.end(); ++prev_range)
        {
          Hash prev_lo = prev_range->first;
          Hash prev_hi = prev_range->second;
          for (Hash prev = prev_lo; prev < prev_hi; ++prev)
          {
            for (Char ch = lo; ch <= hi; ++ch)
            {
              Hash h = hash(prev, static_cast<uint8_t>(ch));
              pmh_[h] &= pmh_mask;
            }
          }
        }
      }
    }
  }
}

void Pattern::gen_match_hfa(DFA::State* start)
{
  size_t max_level = HFA::MAX_DEPTH - 1; // max level from start state(s) is reduced when hashes exponentially increase
  HFA::State index = 1; // DFA states are enumarated for breadth-first matching with the state visit set in match_hfa()
  HFA::StateHashes hashes[HFA::MAX_DEPTH]; // up to MAX_DEPTH states deep into the DFA are hashed from the start state(s)
  gen_match_hfa_start(start, index, hashes[0]);
  for (size_t level = 1; level <= max_level; ++level)
    for (HFA::StateHashes::iterator from = hashes[level - 1].begin(); from != hashes[level - 1].end(); ++from)
      if (!gen_match_hfa_transitions(level, max_level, from->first, from->second, index, hashes[level]))
        break;
  // move the HFA to a new HFA with enumerated states for breadth-first matching with a bitset in match_hfa()
  for (size_t level = 0; level <= max_level; ++level)
  {
    HFA::StateHashes::iterator hashes_end = hashes[level].end();
    for (HFA::StateHashes::iterator next = hashes[level].begin(); next != hashes_end; ++next)
    {
      HFA::HashRanges& set_ranges = hfa_.hashes[level][next->first->index];
      HFA::HashRanges& get_ranges = next->second;
      for (size_t offset = std::max(HFA::MAX_CHAIN - 1, level) + 1 - HFA::MAX_CHAIN; offset <= level; ++offset)
        set_ranges[offset].swap(get_ranges[offset]);
    }
  }
}

void Pattern::gen_match_hfa_start(DFA::State *start, HFA::State& index, HFA::StateHashes& hashes)
{
  if (start->accept == 0 && !start->edges.empty() && !is_meta(start->edges.rbegin()->first))
  {
    start->index = index++;
    for (DFA::State::Edges::const_iterator edge = start->edges.begin(); edge != start->edges.end(); ++edge)
    {
      DFA::State *next_state = edge->second.second;
      if (next_state == NULL)
        continue;
      // ignore edges from a state to a state with breadth-first depth <= cut
      if (lbk_ > 0 && next_state->first > 0 && next_state->first <= cut_)
        continue;
      if (next_state->index == 0)
        next_state->index = index++; // cannot overflow max states if HFA::MAX_STATES >= 256
      hfa_.states[start->index].insert(next_state->index);
      Char lo = edge->first;
      Char hi = edge->second.first;
      DBGLOG("0 HFA %p: %u..%u -> %p", start, lo, hi, next_state);
      hashes[next_state][0].insert(lo, hi);
    }
  }
}

bool Pattern::gen_match_hfa_transitions(size_t level, size_t& max_level, DFA::State *state, const HFA::HashRanges& previous, HFA::State& index, HFA::StateHashes& hashes)
{
  if (state->accept > 0 || state->edges.empty() || is_meta(state->edges.rbegin()->first))
    return true;
  size_t ranges = 0; // total number of hash ranges at this depth level from the DFA/HFA start state
  for (DFA::State::Edges::const_iterator edge = state->edges.begin(); edge != state->edges.end(); ++edge)
  {
    DFA::State *next_state = edge->second.second;
    if (next_state == NULL)
      continue;
    // ignore edges from a state to a state with breadth-first depth <= cut
    if (lbk_ > 0 && next_state->first > 0 && next_state->first <= cut_)
      continue;
    if (next_state->index == 0)
    {
      if (index >= HFA::MAX_STATES)
      {
        max_level = level; // too many HFA states, truncate HFA depth to the current level minus one
        hfa_.states[state->index].clear(); // make this state accepting (dead)
        DBGLOG("Too many HFA states at level %zu", level);
        return false; // stop generating HFA states and hashes
      }
      next_state->index = index++; // enumerate the next state
    }
    hfa_.states[state->index].insert(next_state->index);
    Char lo = edge->first;
    Char hi = edge->second.first;
    DBGLOG("%zu HFA %p: %u..%u -> %p", level, state, lo, hi, next_state);
    for (size_t offset = std::max(HFA::MAX_CHAIN - 1, level) + 1 - HFA::MAX_CHAIN; offset < level; ++offset)
    {
      DBGLOGN("   offset%3zu", offset);
      HFA::HashRange& next_hashes = hashes[next_state][offset];
      const HFA::HashRange::const_iterator prev_range_end = previous[offset].end();
      for (HFA::HashRange::const_iterator prev_range = previous[offset].begin(); prev_range != prev_range_end; ++prev_range)
      {
        Hash prev_lo = prev_range->first;
        Hash prev_hi = prev_range->second - 1; // if prev_hi == 0 it overflowed from 65535, -1 takes care of this
#ifdef DEBUG
        if (prev_lo == prev_hi)
          DBGLOGA(" %.4x", prev_lo);
        else
          DBGLOGA(" %.4x..%.4x", prev_lo, prev_hi);
#endif
        for (uint32_t prev = prev_lo; prev <= prev_hi; ++prev)
        {
          // important: assume index hashing is additive, i.e. indexhash(x,b+1) = indexhash(x,b)+1 modulo 2^16
          Hash hash_lo = indexhash(static_cast<Hash>(prev), static_cast<uint8_t>(lo));
          Hash hash_hi = indexhash(static_cast<Hash>(prev), static_cast<uint8_t>(hi));
          if (hash_lo <= hash_hi && hash_hi < 65535)
          {
            next_hashes.insert(hash_lo, hash_hi);
          }
          else
          {
            if (hash_lo < 65535)
              next_hashes.insert(hash_lo, 65534); // 65534 max, 65535 overflows
            if (hash_hi < 65535)
              next_hashes.insert(0, hash_hi); // 65534 max, 65535 overflows
            if (next_hashes.find(65535) == next_hashes.end())
              next_hashes.insert(65535); // overflow value 65535 is unordered in ORange<Hash> 
          }
        }
      }
      ranges += next_hashes.size();
    }
    hashes[next_state][level].insert(lo, hi); // at offset == level
    hno_ += ranges;
  }
  if (ranges > HFA::MAX_RANGES)
  {
    max_level = level; // too many hashes causing significant slow down, truncate HFA to the current level
    hfa_.states[state->index].clear(); // make this state accepting (dead)
    DBGLOG("too many HFA hashes at level %zu state %u ranges %zu", level, state->index, ranges);
  }
  return true;
}

bool Pattern::match_hfa(const uint8_t *indexed, size_t size) const
{
  if (!has_hfa())
    return false;
  HFA::VisitSet visit[2]; // we alternate two state visit bitsets to produce a new one from the previous for breadth-first matching with the HFA (as an NFA)
  bool accept = false; // a flag to indicate that we reached an accept (= dead) state, i.e. a possible match is found
  for (size_t level = 0; level < HFA::MAX_DEPTH && !accept; ++level)
    if (!match_hfa_transitions(level, hfa_.hashes[level], indexed, size, visit[level & 1], visit[~level & 1], accept))
      return false;
  return true;
}

bool Pattern::match_hfa_transitions(size_t level, const HFA::Hashes& hashes, const uint8_t *indexed, size_t size, HFA::VisitSet& visit, HFA::VisitSet& next_visit, bool& accept) const
{
  bool any = false;
  for (HFA::Hashes::const_iterator next = hashes.begin(); next != hashes.end(); ++next)
  {
    if (level == 0 || visit.test(next->first))
    {
      bool all = true;
      for (size_t offset = std::max(static_cast<size_t>(7), level) - 7; offset <= level; ++offset)
      {
        uint8_t mask = 1 << (level - offset);
        bool flag = false;
        const HFA::HashRange::const_iterator range_end = next->second[offset].end();
        for (HFA::HashRange::const_iterator range = next->second[offset].begin(); range != range_end; ++range)
        {
          Hash lo = range->first;
          Hash hi = range->second - 1; // if hi == 0 it overflowed from 65535, -1 takes care of this
          uint32_t h;
          for (h = lo; h <= hi && (indexed[h & (size - 1)] & mask) != 0; ++h)
            continue;
          if (h <= hi)
          {
            flag = true;
            break;
          }
        }
        if (flag)
        {
          HFA::States::const_iterator state = hfa_.states.find(next->first);
          if (state == hfa_.states.end() || state->second.empty())
            return accept = true; // reached an accepting (= dead) state (dead means accept in HFA)
          const HFA::StateSet::const_iterator index_end = state->second.end();
          for (HFA::StateSet::const_iterator index = state->second.begin(); index != index_end; ++index)
            next_visit.set(*index, true);
        }
        else
        {
          all = false;
          break;
        }
      }
      if (all)
        any = true;
    }
  }
  return any;
}

void Pattern::write_predictor(FILE *file) const
{
  ::fprintf(file, "extern const reflex::Pattern::Pred reflex_pred_%s[%zu] = {", opt_.n.empty() ? "FSM" : opt_.n.c_str(), 2 + len_ + (len_ == 0) * 256 + Const::HASH + (lbk_ > 0) * 68);
  ::fprintf(file, "\n  %3hhu,%3hhu,", static_cast<uint8_t>(len_), (static_cast<uint8_t>(min_ | (one_ << 4) | ((lbk_ > 0) << 5) | (bol_ << 6))));
  // save match characters chr_[0..len_-1]
  for (size_t i = 0; i < len_; ++i)
    ::fprintf(file, "%s%3hhu,", ((i + 2) & 0xF) ? "" : "\n  ", static_cast<uint8_t>(chr_[i]));
  if (len_ == 0)
  {
    // save bitap bit_[] parameters
    for (Char i = 0; i < 256; ++i)
      ::fprintf(file, "%s%3hhu,", (i & 0xF) ? "" : "\n  ", static_cast<uint8_t>(~bit_[i]));
  }
  if (min_ < 4)
  {
    // save predict match PM4 pma_[] parameters
    for (Hash i = 0; i < Const::HASH; ++i)
      ::fprintf(file, "%s%3hhu,", (i & 0xF) ? "" : "\n  ", static_cast<uint8_t>(~pma_[i]));
  }
  else
  {
    // save predict match hash pmh_[] parameters
    for (Hash i = 0; i < Const::HASH; ++i)
      ::fprintf(file, "%s%3hhu,", (i & 0xF) ? "" : "\n  ", static_cast<uint8_t>(~pmh_[i]));
  }
  if (lbk_ > 0)
  {
    // save lookback parameters lbk_ lbm_ cbk_[] after s-t cut and first s-t cut pattern characters fst_[]
    ::fprintf(file, "\n  %3hhu,%3hhu,%3hhu,%3hhu,", static_cast<uint8_t>(lbk_ & 0xff), static_cast<uint8_t>(lbk_ >> 8), static_cast<uint8_t>(lbm_ & 0xff), static_cast<uint8_t>(lbm_ >> 8));
    for (size_t i = 0; i < 256; i += 8)
    {
      uint8_t b = 0;
      for (size_t j = 0; j < 8; ++j)
        b |= cbk_.test(i + j) << j;
      ::fprintf(file, "%s%3hhu,", (i & 0x7F) ? "" : "\n  ", b);
    }
    for (size_t i = 0; i < 256; i += 8)
    {
      uint8_t b = 0;
      for (size_t j = 0; j < 8; ++j)
        b |= fst_.test(i + j) << j;
      ::fprintf(file, "%s%3hhu,", (i & 0x7F) ? "" : "\n  ", b);
    }
  }
  ::fprintf(file, "\n};\n\n");
}

void Pattern::write_namespace_open(FILE *file) const
{
  if (opt_.z.empty())
    return;

  const std::string& s = opt_.z;
  size_t i = 0, j;
  while ((j = s.find("::", i)) != std::string::npos)
  {
    ::fprintf(file, "namespace %s {\n", s.substr(i, j - i).c_str());
    i = j + 2;
  }
  ::fprintf(file, "namespace %s {\n\n", s.substr(i).c_str());
}

void Pattern::write_namespace_close(FILE *file) const
{
  if (opt_.z.empty())
    return;

  const std::string& s = opt_.z;
  size_t i = 0, j;
  while ((j = s.find("::", i)) != std::string::npos)
  {
    ::fprintf(file, "} // namespace %s\n\n", s.substr(i, j - i).c_str());
    i = j + 2;
  }
  ::fprintf(file, "} // namespace %s\n\n", s.substr(i).c_str());
}

} // namespace reflex
