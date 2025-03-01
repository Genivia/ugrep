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
@file      pattern.h
@brief     RE/flex regular expression pattern compiler
@author    Robert van Engelen - engelen@genivia.com
@copyright (c) 2016-2025, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt
*/

#ifndef REFLEX_PATTERN_H
#define REFLEX_PATTERN_H

#include <reflex/bits.h>
#include <reflex/debug.h>
#include <reflex/error.h>
#include <reflex/input.h>
#include <reflex/ranges.h>
#include <reflex/setop.h>
#include <cstdint>
#include <cctype>
#include <cstring>
#include <iostream>
#include <string>
#include <list>
#include <map>
#include <set>
#include <array>
#include <bitset>
#include <vector>
#include <stack>

// ugrep 7.0: use vectorized bitap (hashed) with AVX2, but it is not faster (in our extensive emperical testing)
// #define WITH_BITAP_AVX2

// ugrep 3.7.0a: use a map to construct fixed string pattern trees
// #define WITH_TREE_MAP
// ugrep 3.7.0b: use a DFA as a tree to bypass DFA construction step when possible
#define WITH_TREE_DFA

#if (defined(__WIN32__) || defined(_WIN32) || defined(WIN32) || defined(_WIN64) || defined(__BORLANDC__)) && !defined(__CYGWIN__) && !defined(__MINGW32__) && !defined(__MINGW64__)
# pragma warning( disable : 4290 )
#endif

namespace reflex {

/// Pattern class holds a regex pattern and its compiled FSM opcode table or code for the reflex::Matcher engine.
class Pattern {
  friend class Matcher;      ///< permit access by the reflex::Matcher engine
  friend class FuzzyMatcher; ///< permit access by the reflex::FuzzyMatcher engine
 public:
  typedef uint8_t  Bitap;  ///< bitap bitmask, may change in a future update
  typedef uint8_t  Pred;   ///< predict match bits
  typedef uint16_t Hash;   ///< hash value type, max value is Const::HASH
  typedef uint32_t Index;  ///< index into opcodes array Pattern::opc_ and subpattern indexing
  typedef uint32_t Accept; ///< group capture index
  typedef uint32_t Opcode; ///< 32 bit opcode word
  typedef void (*FSM)(class Matcher&); ///< function pointer to FSM code
  /// Common constants.
  struct Const {
    static const Index  IMAX = 0xffffffff; ///< max index, also serves as a marker
    static const Index  GMAX = 0xfeffff;   ///< max goto index
    static const Accept AMAX = 0xfdffff;   ///< max accept
    static const Index  LMAX = 0xfaffff;   ///< max lookahead index
    static const Index  LONG = 0xfffe;     ///< LONG marker for 64 bit opcodes, must be HALT-1
    static const Index  HALT = 0xffff;     ///< HALT marker for GOTO opcodes, must be 16 bit max
    static const Hash   HASH = 0x1000;     ///< size of the predict match array
    static const Hash   BTAP = 0x0800;     ///< size of the bitap hashed character pairs array
    static const Bitap  BITS = 8;          ///< number of bitap bits, may change in a future update
  };
  /// Construct an unset pattern.
  Pattern()
    :
      opc_(NULL),
      fsm_(NULL),
      nop_(0)
  {
    init(NULL);
  }
  /// Construct a pattern object given a regex string.
  explicit Pattern(
      const char *regex,
      const char *options = NULL)
    :
      rex_(regex),
      opc_(NULL),
      fsm_(NULL)
  {
    init(options);
  }
  /// Construct a pattern object given a regex string.
  Pattern(
      const char        *regex,
      const std::string& options)
    :
      rex_(regex),
      opc_(NULL),
      fsm_(NULL)
  {
    init(options.c_str());
  }
  /// Construct a pattern object given a regex string.
  explicit Pattern(
      const std::string& regex,
      const char        *options = NULL)
    :
      rex_(regex),
      opc_(NULL),
      fsm_(NULL)
  {
    init(options);
  }
  /// Construct a pattern object given a regex string.
  Pattern(
      const std::string& regex,
      const std::string& options)
    :
      rex_(regex),
      opc_(NULL),
      fsm_(NULL)
  {
    init(options.c_str());
  }
  /// Construct a pattern object given an opcode table.
  explicit Pattern(
      const Opcode  *code,
      const uint8_t *pred = NULL)
    :
      opc_(code),
      fsm_(NULL)
  {
    init(NULL, pred);
  }
  /// Construct a pattern object given a function pointer to FSM code.
  explicit Pattern(
      FSM            fsm,
      const uint8_t *pred = NULL)
    :
      opc_(NULL),
      fsm_(fsm)
  {
    init(NULL, pred);
  }
  /// Copy constructor.
  Pattern(const Pattern& pattern) ///< pattern to copy
  {
    operator=(pattern);
  }
  /// Destructor, deletes internal code array when owned and allocated.
  virtual ~Pattern()
  {
    clear();
  }
  /// Clear and delete pattern data.
  void clear()
  {
    rex_.clear();
    if (nop_ > 0 && opc_ != NULL)
      delete[] opc_;
    opc_ = NULL;
    nop_ = 0;
    fsm_ = NULL;
  }
  /// Assign a (new) pattern.
  Pattern& assign(
      const char *regex,
      const char *options = NULL)
  {
    clear();
    rex_ = regex;
    init(options);
    return *this;
  }
  /// Assign a (new) pattern.
  Pattern& assign(
      const char        *regex,
      const std::string& options)
  {
    return assign(regex, options.c_str());
  }
  /// Assign a (new) pattern.
  Pattern& assign(
      const std::string& regex,
      const char        *options = NULL)
  {
    return assign(regex.c_str(), options);
  }
  /// Assign a (new) pattern.
  Pattern& assign(
      const std::string& regex,
      const std::string& options)
  {
    return assign(regex.c_str(), options.c_str());
  }
  /// Assign a (new) pattern.
  Pattern& assign(
      const Opcode  *code,
      const uint8_t *pred = NULL)
  {
    clear();
    opc_ = code;
    init(NULL, pred);
    return *this;
  }
  /// Assign a (new) pattern.
  Pattern& assign(
      FSM            fsm,
      const uint8_t *pred = NULL)
  {
    clear();
    fsm_ = fsm;
    init(NULL, pred);
    return *this;
  }
  /// Assign a (new) pattern.
  Pattern& operator=(const Pattern& pattern)
  {
    clear();
    opt_ = pattern.opt_;
    rex_ = pattern.rex_;
    end_ = pattern.end_;
    acc_ = pattern.acc_;
    vno_ = pattern.vno_;
    eno_ = pattern.eno_;
    pms_ = pattern.pms_;
    vms_ = pattern.vms_;
    ems_ = pattern.ems_;
    wms_ = pattern.wms_;
    if (pattern.nop_ > 0 && pattern.opc_ != NULL)
    {
      nop_ = pattern.nop_;
      Opcode *code = new Opcode[nop_];
      for (size_t i = 0; i < nop_; ++i)
        code[i] = pattern.opc_[i];
      opc_ = code;
    }
    else
    {
      fsm_ = pattern.fsm_;
    }
    return *this;
  }
  /// Assign a (new) pattern.
  Pattern& operator=(const char *regex)
  {
    return assign(regex);
  }
  /// Assign a (new) pattern.
  Pattern& operator=(const std::string& regex)
  {
    return assign(regex);
  }
  /// Assign a (new) pattern.
  Pattern& operator=(const Opcode *code)
  {
    return assign(code);
  }
  /// Assign a (new) pattern.
  Pattern& operator=(FSM fsm)
  {
    return assign(fsm);
  }
  /// Get the number of subpatterns of this pattern object.
  Accept size() const
    /// @returns number of subpatterns
  {
    return static_cast<Accept>(end_.size());
  }
  /// Return true if this pattern is not assigned.
  bool empty() const
    /// @return true if this pattern is not assigned
  {
    return opc_ == NULL && fsm_ == NULL;
  }
  /// Get subpattern regex of this pattern object or the whole regex with index 0.
  const std::string operator[](Accept choice) const
    /// @returns subpattern string or "" when not set
    ;
  /// Check if subpattern is reachable by a match.
  bool reachable(Accept choice) const
    /// @returns true if subpattern is reachable
  {
    return choice >= 1 && choice <= size() && acc_.at(choice - 1);
  }
  /// Get the number of finite state machine nodes (vertices).
  size_t nodes() const
    /// @returns number of nodes or 0 when no finite state machine was constructed by this pattern
  {
    return nop_ > 0 ? vno_ : 0;
  }
  /// Get the number of finite state machine edges (transitions on input characters).
  size_t edges() const
    /// @returns number of edges or 0 when no finite state machine was constructed by this pattern
  {
    return nop_ > 0 ? eno_ : 0;
  }
  /// Get the code size in number of words.
  size_t words() const
    /// @returns number of words or 0 when no code was generated by this pattern
  {
    return nop_;
  }
  /// Get the total number of indexing hash tables constructed for the optional HFA.
  size_t hashes() const
    /// @returns number of HFA hashes total for all HFA edges
  {
    return hno_;
  }
  /// Get elapsed regex parsing and analysis time.
  float parse_time() const
    /// @returns time in ms
  {
    return pms_;
  }
  /// Get elapsed DFA vertices construction time.
  float nodes_time() const
    /// @returns time in ms
  {
    return vms_;
  }
  /// Get elapsed DFA edges construction time.
  float edges_time() const
    /// @returns time in ms
  {
    return ems_;
  }
  /// Get elapsed code words assembly time.
  float words_time() const
    /// @returns time in ms
  {
    return wms_;
  }
  /// Get elapsed time of DFA analysis to predict matches and construct an optional HFA.
  float analysis_time() const
    /// @returns time in ms
  {
    return ams_;
  }
  /// Returns true when match is predicted, based on s[0..3..e-1] (e >= s + 4).
  inline bool predict_match(const char *s, size_t n) const
  {
    uint32_t h = static_cast<uint8_t>(*s);
    uint32_t f = pmh_[h] & 1;
    h = hash(h, static_cast<uint8_t>(*++s));
    f |= pmh_[h] & 2;
    h = hash(h, static_cast<uint8_t>(*++s));
    f |= pmh_[h] & 4;
    h = hash(h, static_cast<uint8_t>(*++s));
    f |= pmh_[h] & 8;
    if (f != 0)
      return false;
    const char *e = s + n - 3;
    uint32_t m = 16;
    while (++s < e)
    {
      h = hash(h, static_cast<uint8_t>(*s));
      f |= pmh_[h] & m;
      m <<= 1;
    }
    return f == 0;
  }
  /// Returns true when match is predicted using my PM4 logic.
  inline bool predict_match(const char *s) const
  {
    uint8_t c0 = static_cast<uint8_t>(s[0]);
    uint8_t c1 = static_cast<uint8_t>(s[1]);
    uint8_t c2 = static_cast<uint8_t>(s[2]);
    uint8_t c3 = static_cast<uint8_t>(s[3]);
    uint32_t h1 = hash(c0, c1);
    uint32_t h2 = hash(h1, c2);
    uint32_t h3 = hash(h2, c3);
    Pred p = (pma_[c0] & 0xc0) | (pma_[h1] & 0x30) | (pma_[h2] & 0x0c) | (pma_[h3] & 0x03);
    Pred m = ((((((p >> 2) | p) >> 2) | p) >> 1) | p);
    return m != 0xff;
  }
  /// Relative frequency of English letters with upper/lower-case ratio = 0.0563, punctuation and UTF-8 bytes.
  static uint8_t frequency(uint8_t c)
  {
    static unsigned char freq[256] =
      // x64 binary frequencies combined with ASCII TAB/LF/CR control code frequencies
      "\377\101\14\22\15\21\10\10\24\73\41\10\11\41\6\51"
      "\16\4\3\3\3\3\3\3\6\3\3\2\3\4\4\12"
      // TAB/LF/CR control code frequencies in text
      // "\0\0\0\0\0\0\0\0\0\73\41\0\0\41\0\0"
      // "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
      // ASCII frequencies
      "\377\0\1\1\0\0\16\33\6\6\7\0\27\11\27\14"
      "\13\14\10\5\4\5\4\4\4\7\12\21\10\14\10\0"
      "\0\11\2\3\5\16\2\2\7\10\0\1\4\3\7\10"
      "\2\0\6\7\12\3\1\3\0\2\0\70\1\70\0\1"
      "\0\237\35\64\133\373\53\47\170\205\3\20\115\64\202\227"
      "\45\2\162\170\272\64\23\56\3\47\2\3\15\3\0\0"
      // upper half with UTF-8 multibyte frequencies (synthesized from Unicode tables)
      "\47\47\47\47\47\47\47\47\47\47\47\47\47\47\47\47"
      "\45\45\45\45\45\45\45\45\45\45\45\45\45\45\45\45"
      "\45\45\45\45\45\45\45\45\45\45\45\45\45\45\45\45"
      "\42\44\42\44\44\44\44\44\44\44\44\44\42\44\44\44"
      "\0\0\5\5\5\5\0\5\5\5\5\5\5\5\0\5"
      "\0\5\5\5\0\5\5\5\5\5\5\5\5\5\5\5"
      "\40\72\76\100\100\100\100\100\100\100\76\100\100\40\100\77"
      "\73\0\0\0\0\0\0\0\0\0\0\0\0\0\0";
    return freq[c];
  }
 protected:
  /// Throw an error.
  virtual void error(
      regex_error_type code,    ///< error code
      size_t           pos = 0) ///< optional location of the error in regex string Pattern::rex_
    const;
 private:
  typedef uint8_t                 Mod;
  typedef uint16_t                Char; // 8 bit char and meta chars up to META_MAX-1
  typedef uint8_t                 Lazy;
  typedef uint16_t                Iter;
  typedef uint16_t                Lookahead;
  typedef std::set<Lookahead>     Lookaheads;
  typedef uint32_t                Location;
  typedef ORanges<Location>       Locations;
  typedef std::map<int,Locations> Map;
  typedef Locations               Mods[10];
  /// Modifiers 'i', 'm', 'q', 's', 'u' (enable) 'I', 'M', 'Q', 'S', 'U' (disable)
  struct ModConst {
    static const Mod i = 0;
    static const Mod I = 1;
    static const Mod m = 2;
    static const Mod M = 3;
    static const Mod q = 4;
    static const Mod Q = 5;
    static const Mod s = 6;
    static const Mod S = 7;
    static const Mod u = 8;
    static const Mod U = 9;
  };
  /// Set of chars and meta chars
  struct Chars {
    Chars()                                 { clear(); }
    Chars(const Chars& c)                   { b[0] = c.b[0]; b[1] = c.b[1]; b[2] = c.b[2]; b[3] = c.b[3]; b[4] = c.b[4]; }
    Chars(const uint64_t c[5])              { b[0] = c[0]; b[1] = c[1]; b[2] = c[2]; b[3] = c[3]; b[4] = c[4]; }
    void   clear()                          { b[0] = b[1] = b[2] = b[3] = b[4] = 0ULL; }
    bool   any()                      const { return b[0] | b[1] | b[2] | b[3] | b[4]; }
    bool   intersects(const Chars& c) const { return (b[0] & c.b[0]) | (b[1] & c.b[1]) | (b[2] & c.b[2]) | (b[3] & c.b[3]) | (b[4] & c.b[4]); }
    bool   contains(const Chars& c)   const { return !(c - *this).any(); }
    bool   contains(Char c)           const { return b[c >> 6] & (1ULL << (c & 0x3f)); }
    Chars& add(Char c)                      { b[c >> 6] |= 1ULL << (c & 0x3f); return *this; }
    Chars& add(Char lo, Char hi)            { while (lo <= hi) add(lo++); return *this; }
    Chars& flip()                           { b[0] = ~b[0]; b[1] = ~b[1]; b[2] = ~b[2]; b[3] = ~b[3]; b[4] = ~b[4]; return *this; }
    Chars& flip256()                        { b[0] = ~b[0]; b[1] = ~b[1]; b[2] = ~b[2]; b[3] = ~b[3]; return *this; }
    Chars& swap(Chars& c)                   { Chars t = c; c = *this; return *this = t; }
    Chars& operator+=(const Chars& c)       { return operator|=(c); }
    Chars& operator-=(const Chars& c)       { b[0] &=~c.b[0]; b[1] &=~c.b[1]; b[2] &=~c.b[2]; b[3] &=~c.b[3]; b[4] &=~c.b[4]; return *this; }
    Chars& operator|=(const Chars& c)       { b[0] |= c.b[0]; b[1] |= c.b[1]; b[2] |= c.b[2]; b[3] |= c.b[3]; b[4] |= c.b[4]; return *this; }
    Chars& operator&=(const Chars& c)       { b[0] &= c.b[0]; b[1] &= c.b[1]; b[2] &= c.b[2]; b[3] &= c.b[3]; b[4] &= c.b[4]; return *this; }
    Chars& operator^=(const Chars& c)       { b[0] ^= c.b[0]; b[1] ^= c.b[1]; b[2] ^= c.b[2]; b[3] ^= c.b[3]; b[4] ^= c.b[4]; return *this; }
    Chars  operator+(const Chars& c)  const { return Chars(*this) += c; }
    Chars  operator-(const Chars& c)  const { return Chars(*this) -= c; }
    Chars  operator|(const Chars& c)  const { return Chars(*this) |= c; }
    Chars  operator&(const Chars& c)  const { return Chars(*this) &= c; }
    Chars  operator^(const Chars& c)  const { return Chars(*this) ^= c; }
    Chars  operator~()                const { return Chars(*this).flip(); }
           operator bool()            const { return any(); }
    Chars& operator=(const Chars& c)        { b[0] = c.b[0]; b[1] = c.b[1]; b[2] = c.b[2]; b[3] = c.b[3]; b[4] = c.b[4]; return *this; }
    bool   operator!=(const Chars& c) const { return (b[0] ^ c.b[0]) | (b[1] ^ c.b[1]) | (b[2] ^ c.b[2]) | (b[3] ^ c.b[3]) | (b[4] ^ c.b[4]); }
    bool   operator==(const Chars& c) const { return !(c != *this); }
    bool   operator<(const Chars& c)  const { return b[0] < c.b[0] || (b[0] == c.b[0] && (b[1] < c.b[1] || (b[1] == c.b[1] && (b[2] < c.b[2] || (b[2] == c.b[2] && (b[3] < c.b[3] || (b[3] == c.b[3] && b[4] < c.b[4]))))))); }
    bool   operator>(const Chars& c)  const { return c < *this; }
    bool   operator<=(const Chars& c) const { return !(c < *this); }
    bool   operator>=(const Chars& c) const { return !(*this < c); }
    Char   lo()                       const { for (Char i = 0; i < 5; ++i) if (b[i]) for (Char j = 0; j < 64; ++j) if (b[i] & (1ULL << j)) return (i << 6) + j; return 0; }
    Char   hi()                       const { for (Char i = 0; i < 5; ++i) if (b[4-i]) for (Char j = 0; j < 64; ++j) if (b[4-i] & (1ULL << (63-j))) return ((4-i) << 6) + (63-j); return 0; }
    uint64_t b[5]; ///< 256 bits to store a set of 8-bit chars + extra bits for meta
  };
  /// Finite state machine construction position information.
  struct Position {
    typedef uint64_t        value_type;
    static const Iter       MAXITER = 0xffff;
    static const Location   MAXLOC  = 0xffffffffUL;
    static const value_type NPOS    = 0xffffffffffffffffULL;
    static const value_type RES1    = 1ULL << 48; ///< reserved
    static const value_type RES2    = 1ULL << 49; ///< reserved
    static const value_type RES3    = 1ULL << 50; ///< reserved
    static const value_type NEGATE  = 1ULL << 51; ///< marks negative patterns
    static const value_type TICKED  = 1ULL << 52; ///< marks lookahead ending ) in (?=X)
    static const value_type RES4    = 1ULL << 53; ///< reserved
    static const value_type ANCHOR  = 1ULL << 54; ///< marks begin of word (\b,\<,\>) and buffer (\A,^) anchors
    static const value_type ACCEPT  = 1ULL << 55; ///< accept, not a regex position
    Position()                   : k(NPOS) { }
    Position(value_type k)       : k(k)    { }
    Position(const Position& p)  : k(p.k)  { }
    Position& operator=(const Position& p) { k = p.k; return *this; }
    operator value_type()            const { return k; }
    Position iter(Iter i)            const { return Position(k + (static_cast<value_type>(i) << 32)); }
    Position negate(bool b)          const { return b ? Position(k | NEGATE) : Position(k & ~NEGATE); }
    Position ticked(bool b)          const { return b ? Position(k | TICKED) : Position(k & ~TICKED); }
    Position anchor(bool b)          const { return b ? Position(k | ANCHOR) : Position(k & ~ANCHOR); }
    Position accept(bool b)          const { return b ? Position(k | ACCEPT) : Position(k & ~ACCEPT); }
    Position lazy(Lazy l)            const { return Position((k & 0x00ffffffffffffffULL) | static_cast<value_type>(l) << 56); }
    Position pos()                   const { return Position(k & 0x0000ffffffffffffULL); }
    Location loc()                   const { return static_cast<Location>(k); }
    Accept   accepts()               const { return static_cast<Accept>(k); }
    Iter     iter()                  const { return static_cast<Index>((k >> 32) & 0xffff); }
    bool     negate()                const { return (k & NEGATE) != 0; }
    bool     ticked()                const { return (k & TICKED) != 0; }
    bool     anchor()                const { return (k & ANCHOR) != 0; }
    bool     accept()                const { return (k & ACCEPT) != 0; }
    Lazy     lazy()                  const { return static_cast<Lazy>(k >> 56); }
    value_type k;
  };
  typedef std::vector<Position>        Lazypos;
  typedef std::vector<Position>        Positions;
  typedef std::map<Position,Positions> Follow;
  typedef std::pair<Chars,Positions>   Move;
  typedef std::list<Move>              Moves;
  static inline void pos_insert(Positions& s1, const Positions& s2) { s1.insert(s1.end(), s2.begin(), s2.end()); }
  static inline void pos_add(Positions& s, const Position& e) { s.insert(s.end(), e); }
  static inline void lazy_insert(Lazypos& s1, const Lazypos& s2) { s1.insert(s1.end(), s2.begin(), s2.end()); }
  static inline void lazy_add(Lazypos& s, const Lazy i, Location p) { s.insert(s.end(), Position(p).lazy(i)); }
#ifndef WITH_TREE_DFA
  /// Tree DFA constructed from string patterns.
  struct Tree {
#ifdef WITH_TREE_MAP
    struct Node {
      Node()
        :
          accept(0)
      { }
      std::map<Char,Node> edges;  ///< edges to next tree nodes
      Accept              accept; ///< nonzero if final state, the index of an accepted/captured subpattern
    };
    Tree()
      :
        tree(NULL)
    { }
    ~Tree()
    {
      clear();
    }
    /// delete the tree and all subnodes.
    void clear()
    {
      if (tree != NULL)
        delete tree;
      tree = NULL;
    }
    /// return the root of the tree.
    Node *root()
    {
      return tree != NULL ? tree : (tree = new Node);
    }
    /// create an edge from a tree node to a target tree node, return the target tree node.
    Node *edge(Node *node, Char c)
    {
      return &node->edges[c];
    }
    Node *tree; ///< root of the tree or NULL
#else
    struct Node {
      Node()
        :
          accept(0)
      {
        for (int i = 0; i < 16; ++i)
          edge[i] = NULL;
      }
      ~Node()
      {
        for (int i = 0; i < 16; ++i)
          if (edge[i] != NULL)
            delete[] edge[i];
      }
      Node **edge[16]; ///< 16x16 edges, one per 8-bit char
      Accept accept;   ///< nonzero if final state, the index of an accepted/captured subpattern
    };
    typedef std::list<Node*> List;
    static const uint16_t ALLOC = 1024; ///< allocate 1024 nodes at a time, to improve performance
    Tree()
      :
        tree(NULL),
        next(ALLOC)
    { }
    ~Tree()
    {
      clear();
    }
    /// delete the tree DFA and reset to the intial state.
    void clear()
    {
      for (List::iterator i = list.begin(); i != list.end(); ++i)
        delete[] *i;
      list.clear();
      tree = NULL;
      next = ALLOC;
    }
    /// return the root of the tree.
    Node *root()
    {
      return tree != NULL ? tree : (tree = leaf());
    }
    /// create an edge from a tree node to a target tree node, return the target tree node.
    Node *edge(Node *node, Char c)
    {
      Node **p = node->edge[c >> 4];
      if (p != NULL)
        return p[c & 0xf] != NULL ?  p[c & 0xf] : (p[c & 0xf]  = leaf());
      p = node->edge[c >> 4] = new Node*[16];
      for (int i = 0; i < 16; ++i)
        p[i] = NULL;
      return p[c & 0xf] = leaf();
    }
    /// create a new leaf node.
    Node *leaf()
    {
      if (next >= ALLOC)
      {
        list.push_back(new Node[ALLOC]);
        next = 0;
      }
      return &list.back()[next++];
    }
    Node    *tree; ///< root of the tree or NULL
    List     list; ///< block allocation list
    uint16_t next; ///< block allocation, next available slot in last block
#endif
  };
#endif
  /// DFA created by subset construction from regex patterns.
  struct DFA {
    struct State : Positions {
      typedef std::pair<Char,State*> Edge;  ///< hi of char range [lo,hi] and state to transition to
      typedef std::map<Char,Edge>    Edges; ///< maps lo to hi and state to transition to on char in range [lo,hi]
      State()
        :
          next(NULL),
          left(NULL),
          right(NULL),
          tnode(NULL),
          first(0),
          index(0),
          accept(0),
          redo(false)
      { }
#ifndef WITH_TREE_DFA
      State *assign(Tree::Node *node)
      {
        tnode = node;
        return this;
      }
      State *assign(Tree::Node *node, Positions& pos)
      {
        tnode = node;
        this->swap(pos);
        return this;
      }
#endif
      State      *next;   ///< points to next state in the list of states allocated depth-first by subset construction
      State      *left;   ///< left pointer for O(log N) node insertion in the hash table overflow tree
      State      *right;  ///< right pointer for O(log N) node insertion in the hash table overflow tree
#ifdef WITH_TREE_DFA
      State      *tnode;  ///< the corresponding tree DFA node, when applicable
#else
      Tree::Node *tnode;  ///< the corresponding tree DFA node, when applicable
#endif
      Edges       edges;  ///< state transitions
      Lookaheads  heads;  ///< lookahead head set
      Lookaheads  tails;  ///< lookahead tail set
      Index       first;  ///< index of this state in the opcode table in the first assembly pass, also used in breadth-first search to cut DFA for predict match
      Index       index;  ///< index of this state in the opcode table, also used in HFA construction
      Accept      accept; ///< nonzero if final state, the index of an accepted/captured subpattern
      bool        redo;   ///< true if this is a final state of a negative pattern
    };
    // transitive closure of DFA meta edges; no follow metas to accepting states and no follow cycles
    struct MetaEdgesClosure {
      MetaEdgesClosure(State& state)
      {
        edge = state.edges.begin();
        end = state.edges.end();
        accept = state.accept > 0 || state.edges.empty();
        walk();
      }
      MetaEdgesClosure(State *state)
      {
        edge = state->edges.begin();
        end = state->edges.end();
        accept = state->accept > 0 || state->edges.empty();
        walk();
      }
      ~MetaEdgesClosure()
      {
        // clean up markers
        while (!done())
          ++edge;
      }
      MetaEdgesClosure& operator++()
      {
        ++edge;
        walk();
        return *this;
      }
      Char lo() const
      {
        return edge->first;
      }
      Char hi() const
      {
        return edge->second.first;
      }
      State *state() const
      {
        return edge->second.second; // target state is non-NULL by walk()
      }
      bool accepting() const
      {
        return accept;
      }
      bool next_accepting() const
      {
        if (state() == NULL || state()->accept > 0 || state()->edges.empty())
          return true;
        return is_meta(state()->edges.rbegin()->first) && MetaEdgesClosure(state()).find_accepting();
      }
      bool find_accepting()
      {
        while (!done())
          ++edge;
        return accepting();
      }
      bool done()
      {
        while (edge == end)
        {
          if (stack.empty())
            return true;
          // restore previous iterators
          edge = stack.top().first;
          end = stack.top().second;
          stack.pop();
          // unmark state visited
          state()->index = 0;
          ++edge;
        }
        return false;
      }
      void walk()
      {
        if (done())
          return;
        // walk the DFA graph acyclicly until non-meta edge to a non-NULL state
        while (is_meta(lo()) || state() == NULL)
        {
          // find non-empty non-visited non-accepting state on a meta edge
          State *next_state;
          do
          {
            next_state = state();
            if (next_state == NULL || next_state->accept > 0 || next_state->edges.empty())
              accept = true;
            else if (next_state->index != 1)
              break;
            ++edge;
            if (done())
              return;
          } while (is_meta(lo()) || next_state == NULL);
          // save current iterators
          stack.push(std::pair<State::Edges::const_iterator,State::Edges::const_iterator>(edge,end));
          // mark state as visited
          next_state->index = 1;
          // new iterators
          edge = next_state->edges.begin();
          end = next_state->edges.end();
        }
      }
      std::stack<std::pair<State::Edges::const_iterator,State::Edges::const_iterator> > stack;
      State::Edges::const_iterator edge;
      State::Edges::const_iterator end;
      bool accept;
    };
    typedef std::list<State*> List;
    static const uint16_t ALLOC = 1024;           ///< allocate 1024 DFA states at a time, to improve performance
    static const uint16_t MAX_DEPTH = 256;        ///< analyze DFA up to states this deep to improve predict match
    static const Index MAX_STATES = Const::GMAX;  ///< maximum number of states
    static const Index DEAD_PATH = 1;             ///< state marker "path always and only reaches backedges" (a dead end)
    static const Index KEEP_PATH = MAX_DEPTH;     ///< state marker "required path" (from a newline edge)
    static const Index LOOP_PATH = MAX_DEPTH + 1; ///< state marker "path reaches a backedge" (collect lookback chars)
    DFA()
      :
        next(ALLOC)
    { }
    ~DFA()
    {
      clear();
    }
    /// delete DFA and reset to initial state.
    void clear()
    {
      for (List::iterator i = list.begin(); i != list.end(); ++i)
        delete[] *i;
      list.clear();
      next = ALLOC;
    }
#ifdef WITH_TREE_DFA
    /// new DFA state.
    State *state()
    {
      if (next >= ALLOC)
      {
        list.push_back(new State[ALLOC]);
        next = 0;
      }
      return &list.back()[next++];
    }
    /// new DFA state with positions, destroys pos.
    State *state(Positions& pos)
    {
      State *s = state();
      s->swap(pos);
      return s;
    }
    /// new DFA state with optional tree DFA node and positions, destroys pos.
    State *state(State *tnode)
    {
      State *s = state();
      s->tnode = tnode;
      return s;
    }
    /// new DFA state with optional tree DFA node and positions, destroys pos.
    State *state(State *tnode, Positions& pos)
    {
      State *s = state(tnode);
      s->swap(pos);
      return s;
    }
    /// root of the DFA is the first state created or NULL.
    State *root()
    {
      return list.empty() ? NULL : list.front();
    }
    /// start state the DFA is the first state created.
    State *start()
    {
      return list.empty() ? state() : list.front();
    }
#else
    /// new DFA state with optional tree DFA node.
    State *state(Tree::Node *node)
    {
      if (next >= ALLOC)
      {
        list.push_back(new State[ALLOC]);
        next = 0;
      }
      return list.back()[next++].assign(node);
    }
    /// new DFA state with optional tree DFA node and positions, destroys pos.
    State *state(Tree::Node *node, Positions& pos)
    {
      if (next >= ALLOC)
      {
        list.push_back(new State[ALLOC]);
        next = 0;
      }
      return list.back()[next++].assign(node, pos);
    }
#endif
    List     list; ///< block allocation list
    uint16_t next; ///< block allocation, next available slot in last block
  };
  /// Indexing hash finite state automaton for indexed file search.
  struct HFA {
    static const size_t MAX_DEPTH  =     16; ///< max hashed pattern length must be between 3 and 16, long is accurate
    static const size_t MAX_CHAIN  =      8; ///< max length of hashed chars chain must be between 2 and 8 (8 is optimal)
    static const size_t MAX_STATES =   1024; ///< max number of states must be 256 or greater
    static const size_t MAX_RANGES = 262144; ///< max number of hashes ranges on an edge to the next state
    typedef ORanges<Hash>                    HashRange;
    typedef std::array<HashRange,MAX_DEPTH>  HashRanges;
    typedef std::map<DFA::State*,HashRanges> StateHashes;
    typedef Index                            State;
    typedef std::map<State,HashRanges>       Hashes;
    typedef std::set<State>                  StateSet;
    typedef std::map<State,StateSet>         States;
    typedef std::bitset<MAX_STATES>          VisitSet;
    Hashes hashes[MAX_DEPTH];
    States states;
  };
  /// Global modifier modes, syntax flags, and compiler options.
  struct Option {
    Option() : b(), h(), e(), f(), g(0), i(), m(), n(), o(), p(), q(), r(), s(), w(), x(), z() { }
    bool                     b; ///< disable escapes in bracket lists
    bool                     h; ///< construct indexing hash finite state automaton
    Char                     e; ///< escape character, or > 255 for none, a backslash by default
    std::vector<std::string> f; ///< output the patterns and/or DFA to files(s)
    int                      g; ///< debug level 0,1,2: output a cut DFA graphviz file with option f, predict match and HFA states
    bool                     i; ///< case insensitive mode, also `(?i:X)`
    bool                     m; ///< multi-line mode, also `(?m:X)`
    std::string              n; ///< pattern name (for use in generated code)
    bool                     o; ///< generate optimized FSM code with option f
    bool                     p; ///< with option f also output predict match array for fast search with find()
    bool                     q; ///< enable "X" quotation of verbatim content, also `(?q:X)`
    bool                     r; ///< raise syntax errors as exceptions
    bool                     s; ///< single-line mode (dotall mode), also `(?s:X)`
    bool                     w; ///< write error message to stderr
    bool                     x; ///< free-spacing mode, also `(?x:X)`
    std::string              z; ///< namespace (NAME1.NAME2.NAME3)
  };
  /// Meta characters.
  enum Meta {
    META_MIN = 0x100,
    // word boundaries
    META_WBB = 0x101, ///< word boundary at begin     `\bx`
    META_WBE = 0x102, ///< word boundary at end       `x\b`
    META_NWB = 0x103, ///< non-word boundary at begin `\Bx`
    META_NWE = 0x104, ///< non-word boundary at end   `x\B`
    META_BWB = 0x105, ///< begin of word at begin     `\<x`
    META_EWB = 0x106, ///< end of word at begin       `\>x`
    META_BWE = 0x107, ///< begin of word at end       `x\<`
    META_EWE = 0x108, ///< end of word at end         `x\>`
    // line and buffer boundaries
    META_BOL = 0x109, ///< begin of line              `^`
    META_EOL = 0x10a, ///< end of line                `$`
    META_BOB = 0x10b, ///< begin of buffer            `\A`
    META_EOB = 0x10c, ///< end of buffer              `\Z`
    // indent boundaries
    META_UND = 0x10d, ///< undent boundary            `\k`
    META_IND = 0x10e, ///< indent boundary            `\i` (must be one but the largest META code)
    META_DED = 0x10f, ///< dedent boundary            `\j` (must be the largest META code)
    // end of boundaries
    META_MAX          ///< max meta characters
  };
  /// Initialize the pattern at construction.
  void init(
      const char    *options,
      const uint8_t *pred = NULL);
  void init_options(const char *options);
  void parse(
      Positions& startpos,
      Follow&    followpos,
      Lazypos&   lazypos,
      Mods       modifiers,
      Map&       lookahead);
  void parse1(
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
      Iter&      iter);
  void parse2(
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
      Iter&      iter);
  void parse3(
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
      Iter&      iter);
  void parse4(
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
      Iter&      iter);
  Char parse_esc(
      Location& loc,
      Chars    *chars = NULL) const;
  void compile(
      DFA::State *start,
      Follow&     followpos,
      const Lazypos& lazypos,
      const Mods  modifiers,
      const Map&  lookahead);
  void lazy(
      const Lazypos& lazypos,
      Positions&     pos) const;
  void lazy(
      const Lazypos&   lazypos,
      const Positions& pos,
      Positions&       pos1) const;
  void greedy(Positions& pos) const;
  void trim_anchors(Positions& follow) const;
  void trim_lazy(Positions *pos, const Lazypos& lazypos) const;
  void compile_transition(
      DFA::State *state,
      Follow&     followpos,
      const Lazypos& lazypos,
      const Mods  modifiers,
      const Map&  lookahead,
      Moves&      moves) const;
  void transition(
      Moves&           moves,
      Chars&           chars,
      const Positions& follow) const;
  void compile_list(
      Location    loc,
      Chars&      chars,
      const Mods  modifiers) const;
  void posix(
      size_t index,
      Chars& chars) const;
  void flip(Chars& chars) const;
  void assemble(DFA::State *start);
  void compact_dfa(DFA::State *start);
  void encode_dfa(DFA::State *start);
  void gencode_dfa(const DFA::State *start) const;
  void check_dfa_closure(
      const DFA::State *state,
      int               nest,
      bool&             peek) const;
  void gencode_dfa_closure(
      FILE             *fd,
      const DFA::State *start,
      int               nest,
      bool              peek) const;
  void graph_dfa(const DFA::State *start) const;
  void export_code() const;
  void analyze_dfa(DFA::State *start);
  void gen_min(std::set<DFA::State*>& states);
  void gen_predict_match(std::set<DFA::State*>& states);
  void gen_predict_match_start(std::set<DFA::State*>& states, std::map<DFA::State*,std::pair<ORanges<Hash>,ORanges<Char> > >& first_hashes);
  void gen_predict_match_transitions(size_t level, DFA::State *state, const std::pair<ORanges<Hash>,ORanges<Char> >& previous, std::map<DFA::State*,std::pair<ORanges<Hash>,ORanges<Char> > >& level_hashes);
  void gen_match_hfa(DFA::State *start);
  void gen_match_hfa_start(DFA::State *start, HFA::State& index, HFA::StateHashes& hashes);
  bool gen_match_hfa_transitions(size_t level, size_t& max_level, DFA::State *state, const HFA::HashRanges& previous, HFA::State& index, HFA::StateHashes& hashes);
 public:
  bool has_hfa() const
  {
    return !hfa_.states.empty();
  }
  bool match_hfa(const uint8_t *indexed, size_t size) const;
 private:
  bool match_hfa_transitions(size_t level, const HFA::Hashes& hashes, const uint8_t *indexed, size_t size, HFA::VisitSet& visit, HFA::VisitSet& next_visit, bool& accept) const;
  void write_predictor(FILE *fd) const;
  void write_namespace_open(FILE *fd) const;
  void write_namespace_close(FILE *fd) const;
  size_t find_at(
      Location loc,
      char     c) const
  {
    return rex_.find_first_of(c, loc);
  }
  Char at(Location k) const
  {
    return static_cast<unsigned char>(rex_[k]);
  }
  bool eq_at(
      Location    loc,
      const char *s) const
  {
    return rex_.compare(loc, strlen(s), s) == 0;
  }
  Char escape_at(Location loc) const
  {
    if (at(loc) == opt_.e)
      return at(loc + 1);
    return '\0';
  }
  Char escapes_at(
      Location    loc,
      const char *escapes) const
  {
    if (at(loc) == opt_.e && std::strchr(escapes, at(loc + 1)))
      return at(loc + 1);
    return '\0';
  }
  static inline bool is_modified(
      Mod        mod,
      const Mods modifiers,
      Location   loc)
  {
    return modifiers[mod].find(loc) != modifiers[mod].end();
  }
  static inline void update_modified(
      Mod      mod,
      Mods     modifiers,
      Location from,
      Location to)
  {
    // modifiers i, m, s, u
    Locations modified(from, to);
    modified -= modifiers[mod ^ 1];
    modifiers[mod] += modified;
  }
  static inline uint16_t hash_pos(const Positions *pos)
  {
    uint16_t h = 0;
    for (Positions::const_iterator i = pos->begin(); i != pos->end(); ++i)
      h += h + static_cast<uint16_t>(*i ^ (*i >> 24));
    return h;
  }
  static inline bool valid_goto_index(Index index)
  {
    return index <= Const::GMAX;
  }
  static inline bool valid_take_index(Index index)
  {
    return index <= Const::AMAX;
  }
  static inline bool valid_lookahead_index(Index index)
  {
    return index <= Const::LMAX;
  }
  static inline bool is_meta(Char c)
  {
    return c > META_MIN;
  }
  static inline Opcode opcode_long(Index index)
  {
    return 0xff000000 | (index & 0xffffff); // index <= Const::GMAX (0xfeffff max)
  }
  static inline Opcode opcode_take(Index index)
  {
    return 0xfe000000 | (index & 0xffffff); // index <= Const::AMAX (0xfdffff max)
  }
  static inline Opcode opcode_redo()
  {
    return 0xfd000000;
  }
  static inline Opcode opcode_tail(Index index)
  {
    return 0xfc000000 | (index & 0xffffff); // index <= Const::LMAX (0xfaffff max)
  }
  static inline Opcode opcode_head(Index index)
  {
    return 0xfb000000 | (index & 0xffffff); // index <= Const::LMAX (0xfaffff max)
  }
  static inline Opcode opcode_goto(
      Char  lo,
      Char  hi,
      Index index)
  {
    return is_meta(lo) ? (static_cast<Opcode>(lo) << 24) | index : (static_cast<Opcode>(lo) << 24) | (hi << 16) | index;
  }
  static inline Opcode opcode_halt()
  {
    return 0x00ffffff;
  }
  static inline bool is_opcode_long(Opcode opcode)
  {
    return (opcode & 0xff000000) == 0xff000000;
  }
  static inline bool is_opcode_take(Opcode opcode)
  {
    return (opcode & 0xfe000000) == 0xfe000000;
  }
  static inline bool is_opcode_redo(Opcode opcode)
  {
    return opcode == 0xfd000000;
  }
  static inline bool is_opcode_tail(Opcode opcode)
  {
    return (opcode & 0xff000000) == 0xfc000000;
  }
  static inline bool is_opcode_head(Opcode opcode)
  {
    return (opcode & 0xff000000) == 0xfb000000;
  }
  static inline bool is_opcode_halt(Opcode opcode)
  {
    return opcode == 0x00ffffff;
  }
  static inline bool is_opcode_goto(Opcode opcode)
  {
    return (opcode << 8) >= (opcode & 0xff000000);
  }
  static inline bool is_opcode_meta(Opcode opcode)
  {
    return (opcode & 0x00ff0000) == 0x00000000 && (opcode >> 24) > 0;
  }
  static inline bool is_opcode_goto(
      Opcode        opcode,
      unsigned char c)
  {
    return c >= (opcode >> 24) && c <= ((opcode >> 16) & 0xff);
  }
  static inline Char meta_of(Opcode opcode)
  {
    return META_MIN + (opcode >> 24);
  }
  static inline Char lo_of(Opcode opcode)
  {
    return is_opcode_meta(opcode) ? meta_of(opcode) : opcode >> 24;
  }
  static inline Char hi_of(Opcode opcode)
  {
    return is_opcode_meta(opcode) ? meta_of(opcode) : (opcode >> 16) & 0xff;
  }
  static inline Index index_of(Opcode opcode)
  {
    return opcode & 0xffff;
  }
  static inline Index long_index_of(Opcode opcode)
  {
    return opcode & 0xffffff;
  }
  static inline Lookahead lookahead_of(Opcode opcode)
  {
    return opcode & 0xffff;
  }
  /// convert to lower case if c is a letter a-z, A-Z.
  static inline Char lowercase(Char c)
  {
    return static_cast<unsigned char>(c | 0x20);
  }
  /// convert to upper case if c is a letter a-z, A-Z.
  static inline Char uppercase(Char c)
  {
    return static_cast<unsigned char>(c & ~0x20);
  }
  /// predict match hash 0 <= hash() < Const::HASH.
  static inline uint32_t hash(uint32_t h, uint8_t b)
  {
    return ((h << 3) ^ b) & (Const::HASH - 1);
  }
  /// bitap character pairs hash
  static inline uint32_t bihash(uint8_t a, uint8_t b)
  {
    return (a ^ (b << 6)) & (Const::BTAP - 1);
  }
  /// file indexing hash 0 <= indexhash() < 65536, must be additive: indexhash(x,b+1) = indexhash(x,b)+1 modulo 2^16.
  static inline uint32_t indexhash(Hash h, uint8_t b)
  {
    return static_cast<uint16_t>((h << 6) - h - h - h + b);
  }
  Option                opt_; ///< pattern compiler options
  HFA                   hfa_; ///< indexing hash finite state automaton
#ifdef WITH_TREE_DFA
  DFA                   tfa_; ///< tree DFA constructed from strings
#else
  Tree                  tfa_; ///< tree DFA constructed from strings
#endif
  DFA                   dfa_; ///< DFA constructed from regex with subset construction using firstpos/lastpos/followpos
  std::string           rex_; ///< regular expression string
  std::vector<Location> end_; ///< entries point to the subpattern's ending '|' or '\0'
  std::vector<bool>     acc_; ///< true if subpattern n is accepting (state is reachable)
  size_t                vno_; ///< number of finite state machine vertices |V| (nodes)
  size_t                eno_; ///< number of finite state machine edges |E| (arrows)
  size_t                hno_; ///< number of indexing hash tables (HFA edges)
  const Opcode         *opc_; ///< points to the table with compiled finite state machine opcodes
  FSM                   fsm_; ///< function pointer to FSM code
  Index                 nop_; ///< number of opcodes generated
  Index                 cut_; ///< DFA s-t cut to improve predict match and HFA accuracy with lbk_ and cbk_
  size_t                len_; ///< length of chr_[], less or equal to 255
  size_t                min_; ///< patterns after the prefix are at least this long but no more than 8
  size_t                pin_; ///< number of needles, 0 to 16
  std::bitset<256>      cbk_; ///< characters to look back over when lbk_ > 0, never includes \n
  std::bitset<256>      fst_; ///< the beginning characters of the pattern
  char                  chr_[256]; ///< pattern prefix string or character needles for needle-based search
  Bitap                 bit_[256]; ///< bitsets of characters for the first positions (one position per bit)
  Bitap                 tap_[Const::BTAP]; ///< bitap hashed character pairs array
#ifdef WITH_BITAP_AVX2 // in case vectorized bitap (hashed) is faster than serial version (typically not!)
#if defined(HAVE_AVX512BW) || defined(HAVE_AVX2) || defined(HAVE_SSE2)
  uint16_t              vtp_[Const::BTAP * 4]; ///< AVX2 vectorized bitap hashed character pairs array
#endif
#endif
  Pred                  pmh_[Const::HASH]; ///< predict-match bloom filter hash up to first 8 positions
  Pred                  pma_[Const::HASH]; ///< predict-match 4 (PM4) array
  uint16_t              lbk_; ///< lookback distance or 0xffff unlimited lookback or 0 for no lookback (empty cbk_)
  uint16_t              lbm_; ///< loopback minimum distance when lbk_ > 0
  uint16_t              lcp_; ///< primary least common character position in the pattern or 0xffff
  uint16_t              lcs_; ///< secondary least common character position in the pattern or 0xffff
  size_t                bmd_; ///< Boyer-Moore jump distance on mismatch, B-M is enabled when bmd_ > 0 (<= 255)
  uint8_t               bms_[256]; ///< Boyer-Moore skip array
  float                 pms_; ///< ms elapsed time to parse regex
  float                 vms_; ///< ms elapsed time to compile DFA vertices
  float                 ems_; ///< ms elapsed time to compile DFA edges
  float                 wms_; ///< ms elapsed time to assemble code words
  float                 ams_; ///< ms elapsed time to analyze DFA for predict match and HFA
  uint16_t              npy_; ///< entropy derived from the bitap array bit_[]
  bool                  one_; ///< true if matching one string stored in chr_[] without meta/anchors
  bool                  bol_; ///< true if matching all patterns at the begin of a line with anchor ^
};

} // namespace reflex

#endif
