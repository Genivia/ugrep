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
@copyright (c) 2016-2020, Robert van Engelen, Genivia Inc. All rights reserved.
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
#include <cctype>
#include <cstring>
#include <iostream>
#include <string>
#include <list>
#include <map>
#include <set>
#include <vector>

#if (defined(__WIN32__) || defined(_WIN32) || defined(WIN32) || defined(_WIN64) || defined(__BORLANDC__)) && !defined(__CYGWIN__) && !defined(__MINGW32__) && !defined(__MINGW64__)
# pragma warning( disable : 4290 )
#endif

namespace reflex {

/// Pattern class holds a regex pattern and its compiled FSM opcode table or code for the reflex::Matcher engine.
/** More info TODO */
class Pattern {
  friend class Matcher; ///< permit access by the reflex::Matcher engine
 public:
  typedef uint8_t  Pred;   ///< predict match bits
  typedef uint16_t Hash;   ///< hash type (uint16_t), max value is Const::HASH
  typedef uint16_t Index;  ///< index into opcodes array Pattern::opc_ and subpattern indexing
  typedef uint32_t Opcode; ///< 32 bit opcode word
  typedef void (*FSM)(class Matcher&); ///< function pointer to FSM code
  /// Common constants.
  struct Const {
    static const Index IMAX = 0xFFFF; ///< max index, also serves as a marker
    static const Index HASH = 0x1000; ///< size of the predict match array
  };
  /// Construct an unset pattern.
  explicit Pattern()
    :
      opc_(NULL),
      nop_(0),
      fsm_(NULL)
  { }
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
  explicit Pattern(
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
  explicit Pattern(
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
      nop_(0),
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
      nop_(0),
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
    if (nop_ && opc_)
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
    if (pattern.nop_ && pattern.opc_)
    {
      nop_ = pattern.nop_;
      Opcode *code = new Opcode[nop_];
      for (Index i = 0; i < nop_; ++i)
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
  Index size() const
    /// @returns number of subpatterns
  {
    return static_cast<Index>(end_.size());
  }
  /// Return true if this pattern is not assigned.
  bool empty() const
    /// @return true if this pattern is not assigned
  {
    return opc_ == NULL && fsm_ == NULL;
  }
  /// Get subpattern regex of this pattern object or the whole regex with index 0.
  const std::string operator[](Index choice) const
    /// @returns subpattern string or "" when not set
    ;
  /// Check if subpattern is reachable by a match.
  bool reachable(Index choice) const
    /// @returns true if subpattern is reachable
  {
    return choice >= 1 && choice <= size() && acc_.at(choice - 1);
  }
  /// Get the number of finite state machine nodes (vertices).
  size_t nodes() const
    /// @returns number of nodes or 0 when no finite state machine was constructed by this pattern
  {
    return nop_ ? vno_ : 0;
  }
  /// Get the number of finite state machine edges (transitions on input characters).
  size_t edges() const
    /// @returns number of edges or 0 when no finite state machine was constructed by this pattern
  {
    return nop_ ? eno_ : 0;
  }
  /// Get the code size in number of words.
  size_t words() const
    /// @returns number of words or 0 when no code was generated by this pattern
  {
    return nop_;
  }
  /// Get elapsed regex parsing and analysis time.
  float parse_time() const
  {
    return pms_;
  }
  /// Get elapsed DFA vertices construction time.
  float nodes_time() const
  {
    return vms_;
  }
  /// Get elapsed DFA edges construction time.
  float edges_time() const
  {
    return ems_;
  }
  /// Get elapsed code words assembly time.
  float words_time() const
  {
    return wms_;
  }
  /// Returns true when match is predicted, based on s[0..3..e-1] (e >= s + 4).
  static inline bool predict_match(const Pred pmh[], const char *s, size_t n)
  {
    Hash h = static_cast<uint8_t>(*s);
    if (pmh[h] & 1)
      return false;
    h = hash(h, static_cast<uint8_t>(*++s));
    if (pmh[h] & 2)
      return false;
    h = hash(h, static_cast<uint8_t>(*++s));
    if (pmh[h] & 4)
      return false;
    h = hash(h, static_cast<uint8_t>(*++s));
    if (pmh[h] & 8)
      return false;
    Pred m = 16;
    const char *e = s + n - 3;
    while (++s < e)
    {
      h = hash(h, static_cast<uint8_t>(*s));
      if (pmh[h] & m)
        return false;
      m <<= 1;
    }
    return true;
  }
  /// Returns zero when match is predicted or nonzero shift value, based on s[0..3].
  static inline size_t predict_match(const Pred pma[], const char *s)
  {
    uint8_t b0 = s[0];
    uint8_t b1 = s[1];
    uint8_t b2 = s[2];
    uint8_t b3 = s[3];
    Hash h1 = hash(b0, b1);
    Hash h2 = hash(h1, b2);
    Hash h3 = hash(h2, b3);
    Pred a0 = pma[b0];
    Pred a1 = pma[h1];
    Pred a2 = pma[h2];
    Pred a3 = pma[h3];
    Pred p = (a0 & 0xc0) | (a1 & 0x30) | (a2 & 0x0c) | (a3 & 0x03);
    Pred m = ((((((p >> 2) | p) >> 2) | p) >> 1) | p);
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
 protected:
  /// Throw an error.
  virtual void error(
      regex_error_type code,    ///< error code
      size_t           pos = 0) ///< optional location of the error in regex string Pattern::rex_
    const;
 private:
  typedef unsigned int            Char;
#if defined(WITH_BITS)
  typedef Bits                    Chars; ///< represent char (0-255 + meta chars) set as a bitvector
#else
  typedef ORanges<Char>           Chars; ///< represent char (0-255 + meta chars) set as a set of ranges
#endif
  typedef size_t                  Location;
  typedef ORanges<Location>       Locations;
  typedef std::set<Location>      Set;
  typedef std::map<int,Locations> Map;
  /// Finite state machine construction position information.
  struct Position {
    typedef uint64_t        value_type;
    static const value_type MAXLOC = (1 << 24) - 1;
    static const value_type NPOS   = static_cast<value_type>(~0ULL);
    static const value_type TICKED = 1LL << 44;
    static const value_type GREEDY = 1LL << 45;
    static const value_type ANCHOR = 1LL << 46;
    static const value_type ACCEPT = 1LL << 47;
    Position()                   : k(NPOS) { }
    Position(value_type k)       : k(k)    { }
    Position(const Position& p)  : k(p.k)  { }
    Position& operator=(const Position& p) { k = p.k; return *this; }
    operator value_type()            const { return k; }
    Position iter(Index i)           const { return Position(k + (static_cast<value_type>(i) << 24)); }
    Position ticked(bool b)          const { return b ? Position(k | TICKED) : Position(k & ~TICKED); }
    Position greedy(bool b)          const { return b ? Position(k | GREEDY) : Position(k & ~GREEDY); }
    Position anchor(bool b)          const { return b ? Position(k | ANCHOR) : Position(k & ~ANCHOR); }
    Position accept(bool b)          const { return b ? Position(k | ACCEPT) : Position(k & ~ACCEPT); }
    Position lazy(Location l)        const { return Position((k & 0x0000FFFFFFFFFFFFLL) | static_cast<value_type>(l) << 48); }
    Position pos()                   const { return Position(k & 0x000000FFFFFFFFFFLL); }
    Location loc()                   const { return static_cast<Location>(k & 0xFFFFFF); }
    Index    accepts()               const { return static_cast<Index>(k & 0xFFFF); }
    Index    iter()                  const { return static_cast<Index>((k >> 24) & 0xFFFF); }
    bool     ticked()                const { return (k & TICKED) != 0; }
    bool     greedy()                const { return (k & GREEDY) != 0; }
    bool     anchor()                const { return (k & ANCHOR) != 0; }
    bool     accept()                const { return (k & ACCEPT) != 0; }
    Location lazy()                  const { return static_cast<Location>((k >> 48) & 0xFFFF); }
    value_type k;
  };
  typedef std::set<Position>           Positions;
  typedef std::map<Position,Positions> Follow;
  typedef std::pair<Chars,Positions>   Move;
  typedef std::list<Move>              Moves;
  /// Finite state machine.
  struct State : Positions {
    typedef std::map<Char,std::pair<Char,State*> > Edges;
    State(const Positions& p)
      :
        Positions(p),
        next(NULL),
        left(NULL),
        right(NULL),
        index(0),
        accept(0),
        redo(false)
    { }
    State *next;   ///< points to sibling state allocated depth-first by subset construction
    State *left;   ///< left pointer for O(log N) node insertion in the state graph
    State *right;  ///< right pointer for O(log N) node insertion in the state graph
    Edges  edges;  ///< state transitions
    Index  index;  ///< index of this state
    Index  accept; ///< nonzero if final state, the index of an accepted/captured subpattern
    Set    heads;  ///< lookahead head set
    Set    tails;  ///< lookahead tail set
    bool   redo;   ///< true if this is an ignorable final state
  };
  /// Global modifier modes, syntax flags, and compiler options.
  struct Option {
    Option() : b(), e(), f(), i(), l(), m(), n(), o(), p(), q(), r(), s(), w(), x(), z() { }
    bool                     b; ///< disable escapes in bracket lists
    Char                     e; ///< escape character, or '\0' for none, '\\' default
    std::vector<std::string> f; ///< output to files
    bool                     i; ///< case insensitive mode, also `(?i:X)`
    bool                     l; ///< lex mode
    bool                     m; ///< multi-line mode, also `(?m:X)`
    std::string              n; ///< pattern name (for use in generated code)
    bool                     o; ///< generate optimized FSM code for option f
    bool                     p; ///< with option f also output predict match array for fast search with find()
    bool                     q; ///< enable "X" quotation of verbatim content, also `(?q:X)`
    bool                     r; ///< raise syntax errors
    bool                     s; ///< single-line mode (dotall mode), also `(?s:X)`
    bool                     w; ///< write error message to stderr
    bool                     x; ///< free-spacing mode, also `(?x:X)`
    std::string              z; ///< namespace (NAME1.NAME2.NAME3)
  };
  /// Meta characters.
  enum Meta {
    META_MIN = 0x100,
    META_NWB = 0x101, ///< non-word boundary at begin `\Bx`
    META_NWE = 0x102, ///< non-word boundary at end   `x\B`
    META_BWB = 0x103, ///< begin of word at begin     `\<x` where \bx=(\<|\>)x
    META_EWB = 0x104, ///< end of word at begin       `\>x`
    META_BWE = 0x105, ///< begin of word at end       `x\<` where x\b=x(\<|\>)
    META_EWE = 0x106, ///< end of word at end         `x\>`
    META_BOL = 0x107, ///< begin of line              `^`
    META_EOL = 0x108, ///< end of line                `$`
    META_BOB = 0x109, ///< begin of buffer            `\A`
    META_EOB = 0x10A, ///< end of buffer              `\Z`
    META_UND = 0x10B, ///< undent boundary            `\k`
    META_IND = 0x10C, ///< indent boundary            `\i` (must be one but the largest META code)
    META_DED = 0x10D, ///< dedent boundary            `\j` (must be the largest META code)
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
      Map&       modifiers,
      Map&       lookahead);
  void parse1(
      bool       begin,
      Location&  loc,
      Positions& firstpos,
      Positions& lastpos,
      bool&      nullable,
      Follow&    followpos,
      Positions& lazypos,
      Map&       modifiers,
      Locations& lookahead,
      Index&     iter);
  void parse2(
      bool       begin,
      Location&  loc,
      Positions& firstpos,
      Positions& lastpos,
      bool&      nullable,
      Follow&    followpos,
      Positions& lazypos,
      Map&       modifiers,
      Locations& lookahead,
      Index&     iter);
  void parse3(
      bool       begin,
      Location&  loc,
      Positions& firstpos,
      Positions& lastpos,
      bool&      nullable,
      Follow&    followpos,
      Positions& lazypos,
      Map&       modifiers,
      Locations& lookahead,
      Index&     iter);
  void parse4(
      bool       begin,
      Location&  loc,
      Positions& firstpos,
      Positions& lastpos,
      bool&      nullable,
      Follow&    followpos,
      Positions& lazypos,
      Map&       modifiers,
      Locations& lookahead,
      Index&     iter);
  void parse_esc(Location& loc) const;
  void compile(
      State&     start,
      Follow&    followpos,
      const Map& modifiers,
      const Map& lookahead);
  void lazy(
      const Positions& lazypos,
      Positions&       pos) const;
  void lazy(
      const Positions& lazypos,
      const Positions& pos,
      Positions&       pos1) const;
  void greedy(Positions& pos) const;
  void trim_lazy(Positions& pos) const;
  void compile_transition(
      State     *state,
      Follow&    followpos,
      const Map& modifiers,
      const Map& lookahead,
      Moves&     moves) const;
  void transition(
      Moves&           moves,
      const Chars&     chars,
      const Positions& follow) const;
  Char compile_esc(
      Location loc,
      Chars&   chars) const;
  void compile_list(
      Location   loc,
      Chars&     chars,
      const Map& modifiers) const;
  void posix(
      size_t index,
      Chars& chars) const;
  void flip(Chars& chars) const;
  void assemble(State& start);
  void compact_dfa(State& start);
  void encode_dfa(State& start);
  void gencode_dfa(const State& start) const;
  void check_dfa_closure(
      const State *state,
      int nest,
      bool& peek,
      bool& prev) const;
  void gencode_dfa_closure(
      FILE *fd,
      const State *start,
      int nest,
      bool peek) const;
  void delete_dfa(State& start);
  void export_dfa(const State& start) const;
  void export_code() const;
  void predict_match_dfa(State& start);
  void gen_predict_match(State *state);
  void gen_predict_match_transitions(State *state, std::map<State*,ORanges<Hash> >& states);
  void gen_predict_match_transitions(Index level, State *state, ORanges<Hash>& labels, std::map<State*,ORanges<Hash> >& states);
  void write_predictor(FILE *fd) const;
  void write_namespace_open(FILE* fd) const;
  void write_namespace_close(FILE* fd) const;
  Location find_at(
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
    if (opt_.e != '\0' && at(loc) == opt_.e)
      return at(loc + 1);
    if (at(loc) == '[' && at(loc + 1) == '[' && at(loc + 2) == ':' && at(loc + 4) == ':' && at(loc + 5) == ']' && at(loc + 6) == ']')
      return at(loc + 3);
    return '\0';
  }
  Char escapes_at(
      Location    loc,
      const char *escapes) const
  {
    if (opt_.e != '\0' && at(loc) == opt_.e && std::strchr(escapes, at(loc + 1)))
      return at(loc + 1);
    if (at(loc) == '[' && at(loc + 1) == '[' && at(loc + 2) == ':' && std::strchr(escapes, at(loc + 3)) && at(loc + 4) == ':' && at(loc + 5) == ']' && at(loc + 6) == ']')
      return at(loc + 3);
    return '\0';
  }
  static bool is_modified(
      Char       mode,
      const Map& modifiers,
      Location   loc)
  {
    Map::const_iterator i = modifiers.find(mode);
    return i != modifiers.end() && i->second.find(loc) != i->second.end();
  }
  static void update_modified(
      Char     mode,
      Map&     modifiers,
      Location from,
      Location to)
  {
    // mode modifiers i, m, s (enabled) I, M, S (disabled)
    if (modifiers.find(reversecase(mode)) != modifiers.end())
    {
      Locations modified(from, to);
      modified -= modifiers[reversecase(mode)];
      modifiers[mode] += modified;
    }
    else
    {
      modifiers[mode].insert(from, to);
    }
  }
  static inline bool is_meta(Char c)
  {
    return c > 0x100;
  }
  static inline Opcode opcode_take(Index index)
  {
    return 0xFF000000 | index;
  }
  static inline Opcode opcode_redo()
  {
    return 0xFF000000 | Const::IMAX;
  }
  static inline Opcode opcode_tail(Index index)
  {
    return 0xFF7E0000 | index;
  }
  static inline Opcode opcode_head(Index index)
  {
    return 0xFF7F0000 | index;
  }
  static inline Opcode opcode_goto(
      Char  lo,
      Char  hi,
      Index index)
  {
    if (!is_meta(lo)) return lo << 24 | hi << 16 | index;
    return 0xFF000000 | (lo - META_MIN) << 16 | index;
  }
  static inline Opcode opcode_halt()
  {
    return 0x00FF0000 | Const::IMAX;
  }
  static inline bool is_opcode_redo(Opcode opcode)
  {
    return opcode == (0xFF000000 | Const::IMAX);
  }
  static inline bool is_opcode_take(Opcode opcode)
  {
    return (opcode & 0xFFFF0000) == 0xFF000000;
  }
  static inline bool is_opcode_tail(Opcode opcode)
  {
    return (opcode & 0xFFFF0000) == 0xFF7E0000;
  }
  static inline bool is_opcode_head(Opcode opcode)
  {
    return (opcode & 0xFFFF0000) == 0xFF7F0000;
  }
  static inline bool is_opcode_halt(Opcode opcode)
  {
    return opcode == (0x00FF0000 | Const::IMAX);
  }
  static inline bool is_opcode_meta(Opcode opcode)
  {
    return (opcode & 0xFF800000) == 0xFF000000;
  }
  static inline bool is_opcode_meta(Opcode opcode, Char a)
  {
    return (opcode & 0xFFFF0000) == (0xFF000000 | (a - META_MIN) << 16);
  }
  static inline bool is_opcode_match(
      Opcode        opcode,
      unsigned char c)
  {
    return c >= (opcode >> 24) && c <= (opcode >> 16 & 0xFF);
  }
  static inline Char meta_of(Opcode opcode)
  {
    return META_MIN + (opcode >> 16 & 0xFF);
  }
  static inline Char lo_of(Opcode opcode)
  {
    return is_opcode_meta(opcode) ? meta_of(opcode) : opcode >> 24;
  }
  static inline Char hi_of(Opcode opcode)
  {
    return is_opcode_meta(opcode) ? meta_of(opcode) : opcode >> 16 & 0xFF;
  }
  static inline Index index_of(Opcode opcode)
  {
    return opcode & 0xFFFF;
  }
  static inline Char lowercase(Char c)
  {
    return static_cast<unsigned char>(c | 0x20);
  }
  static inline Char uppercase(Char c)
  {
    return static_cast<unsigned char>(c & ~0x20);
  }
  static inline Char reversecase(Char c)
  {
    return static_cast<unsigned char>(c ^ 0x20);
  }
  static inline Hash hash(Hash h, uint8_t b)
  {
    return ((h << 3) ^ b) & (Const::HASH - 1);
  }
  static inline Hash hash(Hash h)
  {
    return h & ((Const::HASH - 1) >> 3);
  }
  Option                opt_; ///< pattern compiler options
  std::string           rex_; ///< regular expression string
  std::vector<Location> end_; ///< entries point to the subpattern's ending '|' or '\0'
  std::vector<bool>     acc_; ///< true if subpattern n is accepting (state is reachable)
  size_t                vno_; ///< number of finite state machine vertices |V|
  size_t                eno_; ///< number of finite state machine edges |E|
  const Opcode         *opc_; ///< points to the opcode table
  Index                 nop_; ///< number of opcodes generated
  FSM                   fsm_; ///< function pointer to FSM code
  size_t                len_; ///< prefix length of pre_[], less or equal to 255
  size_t                min_; ///< patterns after the prefix are at least this long but no more than 8
  char                  pre_[256];         ///< pattern prefix, shorter or equal to 255 bytes
  Pred                  bit_[256];         ///< bitap array
  Pred                  pmh_[Const::HASH]; ///< predict-match hash array
  Pred                  pma_[Const::HASH]; ///< predict-match array
  float                 pms_; ///< ms elapsed time to parse regex
  float                 vms_; ///< ms elapsed time to compile DFA vertices
  float                 ems_; ///< ms elapsed time to compile DFA edges
  float                 wms_; ///< ms elapsed time to assemble code words
  bool                  one_; ///< true if matching one string in pre_[] without meta/anchors
};

} // namespace reflex

#endif
