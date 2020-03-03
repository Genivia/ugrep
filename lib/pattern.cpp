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
@copyright (c) 2016-2020, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt
*/

#include <reflex/pattern.h>
#include <reflex/timer.h>
#include <cstdlib>
#include <cerrno>
#include <cmath>

/// DFA compaction: -1 == reverse order edge compression (best); 1 == edge compression; 0 == no edge compression.
#define WITH_COMPACT_DFA -1

#ifdef DEBUG
# define DBGLOGPOS(p) \
  if ((p).accept()) \
  { \
    DBGLOGA(" (%hu)", (p).accepts()); \
    if ((p).lazy()) \
      DBGLOGA("?%zu", (p).lazy()); \
    if ((p).greedy()) \
      DBGLOGA("!"); \
  } \
  else \
  { \
    DBGLOGA(" "); \
    if ((p).iter()) \
      DBGLOGA("%hu.", (p).iter()); \
    DBGLOGA("%lu", (p).loc()); \
    if ((p).lazy()) \
      DBGLOGA("?%zu", (p).lazy()); \
    if ((p).anchor()) \
      DBGLOGA("^"); \
    if ((p).greedy()) \
      DBGLOGA("!"); \
    if ((p).ticked()) \
      DBGLOGA("'"); \
  }
#endif

namespace reflex {

#if (defined(__WIN32__) || defined(_WIN32) || defined(WIN32) || defined(_WIN64) || defined(__BORLANDC__)) && !defined(__CYGWIN__) && !defined(__MINGW32__) && !defined(__MINGW64__)
inline int fopen_s(FILE **file, const char *name, const char *mode) { return ::fopen_s(file, name, mode); }
#else
inline int fopen_s(FILE **file, const char *name, const char *mode) { return (*file = ::fopen(name, mode)) ? 0 : errno; }
#endif

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
    ::fprintf(file, "%02X", c);
  else
    ::fprintf(file, "%u", c);
}

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

static const char *meta_label[] = {
  NULL,
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

const std::string Pattern::operator[](Index choice) const
{
  if (choice == 0)
    return rex_;
  if (choice >= 1 && choice <= size())
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
  if (code == regex_error::exceeds_limits || opt_.r)
    throw err;
}

void Pattern::init(const char *opt, const uint8_t *pred)
{
  init_options(opt);
  nop_ = 0;
  len_ = 0;
  min_ = 0;
  one_ = false;
  if (opc_ || fsm_)
  {
    if (pred != NULL)
    {
      len_ = pred[0];
      min_ = pred[1] & 0x0f;
      one_ = pred[1] & 0x10;
      memcpy(pre_, pred + 2, len_);
      if (min_ > 0)
      {
        size_t n = len_ + 2;
        if (min_ > 1 && len_ == 0)
        {
          for (size_t i = 0; i < 256; ++i)
            bit_[i] = ~pred[i + n];
          n += 256;
        }
        if (min_ >= 4)
        {
          for (size_t i = 0; i < Const::HASH; ++i)
            pmh_[i] = ~pred[i + n];
        }
        else
        {
          for (size_t i = 0; i < Const::HASH; ++i)
            pma_[i] = ~pred[i + n];
        }
      }
    }
  }
  else
  {
    Positions startpos;
    Follow    followpos;
    Map       modifiers;
    Map       lookahead;
    parse(startpos, followpos, modifiers, lookahead);
    State start(startpos);
    compile(start, followpos, modifiers, lookahead);
    assemble(start);
  }
}

void Pattern::init_options(const char *opt)
{
  opt_.b = false;
  opt_.i = false;
  opt_.l = false;
  opt_.m = false;
  opt_.o = false;
  opt_.p = false;
  opt_.q = false;
  opt_.r = false;
  opt_.s = false;
  opt_.w = false;
  opt_.x = false;
  opt_.e = '\\';
  if (opt)
  {
    for (const char *s = opt; *s != '\0'; ++s)
    {
      switch (*s)
      {
        case 'b':
          opt_.b = true;
          break;
        case 'e':
          opt_.e = (*(s += (s[1] == '=') + 1) == ';' ? '\0' : *s);
          break;
        case 'p':
          opt_.p = true;
          break;
        case 'i':
          opt_.i = true;
          break;
        case 'l':
          opt_.l = true;
          break;
        case 'm':
          opt_.m = true;
          break;
        case 'o':
          opt_.o = true;
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
              if (std::isspace(*t) || *t == ';' || *t == '\0')
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
            if (*t == ',' || std::isspace(*t) || *t == ';' || *t == '\0')
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
    Map&       modifiers,
    Map&       lookahead)
{
  DBGLOG("BEGIN parse()");
  if (rex_.size() > Position::MAXLOC)
    throw regex_error(regex_error::exceeds_length, rex_, Position::MAXLOC);
  Location   loc = 0;
  Index      choice = 1;
  Positions  firstpos;
  Positions  lastpos;
  bool       nullable;
  Index      iter;
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
        else if (c == 'l')
          opt_.l = active;
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
  do
  {
    Positions lazypos;
    parse2(
        true,
        loc,
        firstpos,
        lastpos,
        nullable,
        followpos,
        lazypos,
        modifiers,
        lookahead[choice],
        iter);
    end_.push_back(loc);
    set_insert(startpos, firstpos);
    if (nullable)
    {
      if (lazypos.empty())
      {
        startpos.insert(Position(choice).accept(true));
      }
      else
      {
        for (Positions::const_iterator p = lazypos.begin(); p != lazypos.end(); ++p)
          startpos.insert(Position(choice).accept(true).lazy(p->loc()));
      }
    }
    for (Positions::const_iterator p = lastpos.begin(); p != lastpos.end(); ++p)
    {
      if (lazypos.empty())
      {
        followpos[p->pos()].insert(Position(choice).accept(true));
      }
      else
      {
        for (Positions::const_iterator q = lazypos.begin(); q != lazypos.end(); ++q)
          followpos[p->pos()].insert(Position(choice).accept(true).lazy(q->loc()));
      }
    }
    ++choice;
  } while (at(loc++) == '|');
  if (opt_.i)
    update_modified('i', modifiers, 0, rex_.size() - 1);
  if (opt_.m)
    update_modified('m', modifiers, 0, rex_.size() - 1);
  if (opt_.s)
    update_modified('s', modifiers, 0, rex_.size() - 1);
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
    Positions& lazypos,
    Map&       modifiers,
    Locations& lookahead,
    Index&     iter)
{
  DBGLOG("BEGIN parse1(%zu)", loc);
  parse2(
      begin,
      loc,
      firstpos,
      lastpos,
      nullable,
      followpos,
      lazypos,
      modifiers,
      lookahead,
      iter);
  Positions firstpos1;
  Positions lastpos1;
  bool      nullable1;
  Positions lazypos1;
  Index     iter1;
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
        lazypos1,
        modifiers,
        lookahead,
        iter1);
    set_insert(firstpos, firstpos1);
    set_insert(lastpos, lastpos1);
    set_insert(lazypos, lazypos1);
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
    Positions& lazypos,
    Map&       modifiers,
    Locations& lookahead,
    Index&     iter)
{
  DBGLOG("BEGIN parse2(%zu)", loc);
  Positions a_pos;
  if (begin)
  {
    while (true)
    {
      if (opt_.x)
        while (std::isspace(at(loc)))
          ++loc;
      if (at(loc) == '^')
      {
        a_pos.insert(Position(loc++));
        begin = false; // CHECKED algorithmic options: 7/29 but does not allow ^ as a pattern
      }
      else if (escapes_at(loc, "ABb<>"))
      {
        a_pos.insert(Position(loc));
        loc += 2;
        begin = false; // CHECKED algorithmic options: 7/29 but does not allow \b as a pattern
      }
      else
      {
        if (escapes_at(loc, "ij"))
          begin = false;
        break;
      }
    }
  }
  parse3(
      begin,
      loc,
      firstpos,
      lastpos,
      nullable,
      followpos,
      lazypos,
      modifiers,
      lookahead,
      iter);
  Positions firstpos1;
  Positions lastpos1;
  bool      nullable1;
  Positions lazypos1;
  Index     iter1;
  Position  l_pos;
  Char      c;
  while ((c = at(loc)) != '\0' && c != '|' && c != ')')
  {
    if (c == '/' && l_pos == Position::NPOS && opt_.l && (!opt_.x || at(loc + 1) != '*'))
      l_pos = loc++; // lookahead
    parse3(
        false,
        loc,
        firstpos1,
        lastpos1,
        nullable1,
        followpos,
        lazypos1,
        modifiers,
        lookahead,
        iter1);
    if (c == '/' && l_pos != Position::NPOS)
      firstpos1.insert(l_pos);
    if (!lazypos.empty()) // CHECKED this is an extra rule for + only and (may) not be needed for *
    {
      // CHECKED algorithmic options: lazy(lazypos, firstpos1); does not work for (a|b)*?a*b+, below works
      Positions firstpos2;
      lazy(lazypos, firstpos1, firstpos2);
      set_insert(firstpos1, firstpos2);
      // if (lazypos1.empty())
        // greedy(firstpos1); // CHECKED algorithmic options: 8/1 works except fails for ((a|b)*?b){2} and (a|b)??(a|b)??aa
    }
    if (nullable)
      set_insert(firstpos, firstpos1);
    for (Positions::const_iterator p = lastpos.begin(); p != lastpos.end(); ++p)
      set_insert(followpos[p->pos()], firstpos1);
    if (nullable1)
    {
      set_insert(lastpos, lastpos1);
      set_insert(lazypos, lazypos1); // CHECKED 10/21
    }
    else
    {
      lastpos.swap(lastpos1);
      lazypos.swap(lazypos1); // CHECKED 10/21
      nullable = false;
    }
    // CHECKED 10/21 set_insert(lazypos, lazypos1);
    if (iter1 > iter)
      iter = iter1;
  }
  for (Positions::const_iterator p = a_pos.begin(); p != a_pos.end(); ++p)
  {
    for (Positions::const_iterator k = lastpos.begin(); k != lastpos.end(); ++k)
      if ((at(k->loc()) == ')' || (opt_.l && at(k->loc()) == '/')) && lookahead.find(k->loc()) != lookahead.end())
        followpos[p->pos()].insert(*k);
    for (Positions::const_iterator k = lastpos.begin(); k != lastpos.end(); ++k)
      followpos[k->pos()].insert(p->anchor(!nullable || k->pos() != p->pos()));
    lastpos.clear();
    lastpos.insert(*p);
    if (nullable)
    {
      firstpos.insert(*p);
      nullable = false;
    }
  }
  if (l_pos != Position::NPOS)
  {
    for (Positions::const_iterator p = lastpos.begin(); p != lastpos.end(); ++p)
      followpos[p->pos()].insert(l_pos.ticked(true)); // ticked for lookstop
    lastpos.insert(l_pos.ticked(true));
    lookahead.insert(l_pos.loc(), l_pos.loc());
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
    Positions& lazypos,
    Map&       modifiers,
    Locations& lookahead,
    Index&     iter)
{
  DBGLOG("BEGIN parse3(%zu)", loc);
  Position b_pos(loc);
  parse4(
      begin,
      loc,
      firstpos,
      lastpos,
      nullable,
      followpos,
      lazypos,
      modifiers,
      lookahead,
      iter);
  Char c = at(loc);
  if (opt_.x)
    while (std::isspace(c))
      c = at(++loc);
  if (c == '*' || c == '+' || c == '?')
  {
    if (c == '*' || c == '?')
      nullable = true;
    if (at(++loc) == '?')
    {
      lazypos.insert(loc);
      if (nullable)
        lazy(lazypos, firstpos);
      ++loc;
    }
    else
    {
      // CHECKED algorithmic options: 7/30 if (!nullable)
      // CHECKED algorithmic options: 7/30   lazypos.clear();
      greedy(firstpos);
    }
    if (c == '+' && !nullable && !lazypos.empty())
    {
      Positions firstpos1;
      lazy(lazypos, firstpos, firstpos1);
      for (Positions::const_iterator p = lastpos.begin(); p != lastpos.end(); ++p)
        set_insert(followpos[p->pos()], firstpos1);
      set_insert(firstpos, firstpos1);
    }
    else if (c == '*' || c == '+')
    {
      for (Positions::const_iterator p = lastpos.begin(); p != lastpos.end(); ++p)
        set_insert(followpos[p->pos()], firstpos);
    }
  }
  else if (c == '{') // {n,m} repeat min n times to max m
  {
    size_t k = 0;
    for (Location i = 0; i < 7 && std::isdigit(c = at(++loc)); ++i)
      k = 10 * k + (c - '0');
    if (k > Const::IMAX)
      error(regex_error::exceeds_limits, loc);
    Index n = static_cast<Index>(k);
    Index m = n;
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
        lazypos.insert(loc);
        if (nullable)
        {
          lazy(lazypos, firstpos);
        }
        /* CHECKED algorithmic options: 8/1 else
        {
          lazy(lazypos, firstpos, firstpos1);
          set_insert(firstpos, firstpos1);
          pfirstpos = &firstpos1;
        } */
        ++loc;
      }
      else
      {
        // CHECKED algorithmic options 7/30 if (!nullable)
        // CHECKED algorithmic options 7/30   lazypos.clear();
        if (n < m && lazypos.empty())
          greedy(firstpos);
      }
      // CHECKED added pfirstpos to point to updated firstpos with lazy quants
      Positions firstpos1, *pfirstpos = &firstpos;
      if (!nullable && !lazypos.empty()) // CHECKED algorithmic options 8/1 added to make ((a|b)*?b){2} work
      {
        lazy(lazypos, firstpos, firstpos1);
        pfirstpos = &firstpos1;
      }
      if (nullable && unlimited) // {0,} == *
      {
        for (Positions::const_iterator p = lastpos.begin(); p != lastpos.end(); ++p)
          set_insert(followpos[p->pos()], *pfirstpos);
      }
      else if (m > 0)
      {
        if (iter * m >= Const::IMAX)
          error(regex_error::exceeds_limits, loc);
        // update followpos by virtually repeating sub-regex m-1 times
        Follow followpos1;
        for (Follow::const_iterator fp = followpos.begin(); fp != followpos.end(); ++fp)
          if (fp->first.loc() >= b_pos)
            for (Index i = 1; i < m; ++i)
              for (Positions::const_iterator p = fp->second.begin(); p != fp->second.end(); ++p)
                followpos1[fp->first.iter(iter * i)].insert(p->iter(iter * i));
        for (Follow::const_iterator fp = followpos1.begin(); fp != followpos1.end(); ++fp)
          set_insert(followpos[fp->first], fp->second);
        // add m-1 times virtual concatenation (by indexed positions k.i)
        for (Index i = 0; i < m - 1; ++i)
          for (Positions::const_iterator k = lastpos.begin(); k != lastpos.end(); ++k)
            for (Positions::const_iterator j = pfirstpos->begin(); j != pfirstpos->end(); ++j)
              followpos[k->pos().iter(iter * i)].insert(j->iter(iter * i + iter));
        if (unlimited)
          for (Positions::const_iterator k = lastpos.begin(); k != lastpos.end(); ++k)
            for (Positions::const_iterator j = pfirstpos->begin(); j != pfirstpos->end(); ++j)
              followpos[k->pos().iter(iter * m - iter)].insert(j->iter(iter * m - iter));
        if (nullable1)
        {
          // extend firstpos when sub-regex is nullable
          Positions firstpos1 = *pfirstpos;
          for (Index i = 1; i <= m - 1; ++i)
            for (Positions::const_iterator k = firstpos1.begin(); k != firstpos1.end(); ++k)
              firstpos.insert(k->iter(iter * i));
        }
        // n to m-1 are optional with all 0 to m-1 are optional when nullable
        Positions lastpos1;
        for (Index i = (nullable ? 0 : n - 1); i <= m - 1; ++i)
          for (Positions::const_iterator k = lastpos.begin(); k != lastpos.end(); ++k)
            lastpos1.insert(k->iter(iter * i));
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
    else
    {
      error(regex_error::invalid_repeat, loc);
    }
  }
  else if (c == '}')
  {
    error(regex_error::mismatched_braces, loc++);
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
    Positions& lazypos,
    Map&       modifiers,
    Locations& lookahead,
    Index&     iter)
{
  DBGLOG("BEGIN parse4(%zu)", loc);
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
      else if (c == '^') // (?^ negative pattern to be ignored (new mode)
      {
        ++loc;
        parse1(
            begin,
            loc,
            firstpos,
            lastpos,
            nullable,
            followpos,
            lazypos,
            modifiers,
            lookahead,
            iter);
        for (Positions::const_iterator p = lastpos.begin(); p != lastpos.end(); ++p)
          followpos[p->pos()].insert(Position(0).accept(true));
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
            lazypos,
            modifiers,
            lookahead,
            iter);
        firstpos.insert(l_pos);
        if (nullable)
          lastpos.insert(l_pos);
        if (lookahead.find(l_pos.loc(), loc) == lookahead.end()) // do not permit nested lookaheads
          lookahead.insert(l_pos.loc(), loc); // lookstop at )
        for (Positions::const_iterator p = lastpos.begin(); p != lastpos.end(); ++p)
          followpos[p->pos()].insert(Position(loc).ticked(true));
        lastpos.insert(Position(loc).ticked(true));
        if (nullable)
        {
          firstpos.insert(Position(loc).ticked(true));
          lastpos.insert(l_pos);
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
            lazypos,
            modifiers,
            lookahead,
            iter);
      }
      else
      {
        Location m_loc = loc;
        bool opt_l = opt_.l;
        bool opt_q = opt_.q;
        bool opt_x = opt_.x;
        bool active = true;
        do
        {
          if (c == '-')
            active = false;
          else if (c == 'l')
            opt_.l = active;
          else if (c == 'q')
            opt_.q = active;
          else if (c == 'x')
            opt_.x = active;
          else if (c != 'i' && c != 'm' && c != 's')
            error(regex_error::invalid_modifier, loc);
          c = at(++loc);
        } while (c != '\0' && c != ':' && c != ')');
        if (c != '\0')
          ++loc;
        // enforce (?ilmqsx) modes
        parse1(
            begin,
            loc,
            firstpos,
            lastpos,
            nullable,
            followpos,
            lazypos,
            modifiers,
            lookahead,
            iter);
        active = true;
        do
        {
          c = at(m_loc++);
          if (c == '-')
          {
            active = false;
          }
          else if (c != '\0' && c != 'l' && c != 'q' && c != 'x' && c != ':' && c != ')')
          {
            if (active)
              update_modified(c, modifiers, m_loc, loc);
            else
              update_modified(uppercase(c), modifiers, m_loc, loc);
          }
        } while (c != '\0' && c != ':' && c != ')');
        opt_.l = opt_l;
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
    firstpos.insert(loc);
    lastpos.insert(loc);
    nullable = false;
    if ((c = at(++loc)) == '^')
      c = at(++loc);
    while (c != '\0')
    {
      if (c == '[' && at(loc + 1) == ':')
      {
        Location c_loc = find_at(loc + 2, ':');
        if (c_loc != std::string::npos && at(c_loc + 1) == ']')
          loc = c_loc + 1;
      }
      else if (c == opt_.e && opt_.e != '\0' && !opt_.b)
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
    Location q_loc = loc++;
    if ((c = at(loc)) != '\0' && (!quoted || c != '"') && (quoted || c != opt_.e || at(loc + 1) != 'E'))
    {
      firstpos.insert(loc);
      Position p;
      do
      {
        if (c == '\\' && at(loc + 1) == '"' && quoted)
          ++loc;
        if (p != Position::NPOS)
          followpos[p].insert(loc);
        p = loc++;
      } while ((c = at(loc)) != '\0' && (!quoted || c != '"') && (quoted || c != opt_.e || at(loc + 1) != 'E'));
      lastpos.insert(p);
      nullable = false;
    }
    modifiers['q'].insert(q_loc, loc);
    if (c != '\0')
    {
      if (!quoted)
        ++loc;
      if (at(loc) != '\0')
        ++loc;
    }
    else
    {
      error(regex_error::mismatched_quotation, loc);
    }
  }
  else if (c == '#' && opt_.x)
  {
    ++loc;
    while ((c = at(loc)) != '\0' && c != '\n')
      ++loc;
    if (c == '\n')
      ++loc;
  }
  else if (c == '/' && opt_.l && opt_.x && at(loc + 1) == '*')
  {
    loc += 2;
    while ((c = at(loc)) != '\0' && (c != '*' || at(loc + 1) != '/'))
      ++loc;
    if (c != '\0')
      loc += 2;
    else
      error(regex_error::invalid_syntax, loc);
  }
  else if (std::isspace(c) && opt_.x)
  {
    ++loc;
  }
  else if (c != '\0' && c != '|' && c != ')' && c != '?' && c != '*' && c != '+')
  {
    firstpos.insert(loc);
    lastpos.insert(loc);
    nullable = false;
    parse_esc(loc);
  }
  else if (begin && c != '\0') // permits empty regex pattern but not empty subpatterns
  {
    error(regex_error::empty_expression, loc);
  }
  DBGLOG("END parse4()");
}

void Pattern::parse_esc(Location& loc) const
{
  Char c;
  if (at(loc++) == opt_.e && opt_.e != '\0' && (c = at(loc)) != '\0')
  {
    if (c == '0')
    {
      ++loc;
      for (int i = 0; i < 3 && std::isdigit(at(loc)); ++i)
        ++loc;
    }
    else if ((c == 'p' || c == 'P') && at(loc + 1) == '{')
    {
      ++loc;
      while (std::isalnum(at(++loc)))
        continue;
      if (at(loc) == '}')
        ++loc;
      else
        error(regex_error::invalid_escape, loc);
    }
    else if (c == 'u' && at(loc + 1) == '{')
    {
      ++loc;
      while (std::isxdigit(at(++loc)))
        continue;
      if (at(loc) == '}')
        ++loc;
      else
        error(regex_error::invalid_escape, loc);
    }
    else if (c == 'x' && at(loc + 1) == '{')
    {
      ++loc;
      while (std::isxdigit(at(++loc)))
        continue;
      if (at(loc) == '}')
        ++loc;
      else
        error(regex_error::invalid_escape, loc);
    }
    else if (c == 'x')
    {
      ++loc;
      for (int i = 0; i < 2 && std::isxdigit(at(loc)); ++i)
        ++loc;
    }
    else
    {
      if (c == 'c')
        ++loc;
      if (at(loc) != '\0')
        ++loc;
      else
        error(regex_error::invalid_escape, loc);
    }
  }
}

void Pattern::compile(
    State&     start,
    Follow&    followpos,
    const Map& modifiers,
    const Map& lookahead)
{
  DBGLOG("BEGIN compile()");
  State *back_state = &start;
  vno_ = 0;
  eno_ = 0;
  ems_ = 0.0;
  timer_type vt, et;
  timer_start(vt);
  acc_.resize(end_.size(), false);
  trim_lazy(start);
  for (State *state = &start; state; state = state->next)
  {
    Moves moves;
    timer_start(et);
    compile_transition(
        state,
        followpos,
        modifiers,
        lookahead,
        moves);
    ems_ += timer_elapsed(et);
    for (Moves::iterator i = moves.begin(); i != moves.end(); ++i)
    {
      Positions& pos = i->second;
      trim_lazy(pos);
      if (!pos.empty())
      {
        State *target_state = &start;
        State **branch_ptr = NULL;
        // binary search for a matching state
        do
        {
          if (pos < *target_state)
            target_state = *(branch_ptr = &target_state->left);
          else if (pos > *target_state)
            target_state = *(branch_ptr = &target_state->right);
          else
            break;
        } while (target_state);
        if (!target_state)
          back_state = back_state->next = *branch_ptr = target_state = new State(pos);
#if defined(WITH_BITS)
        Char lo = i->first.find_first(), j = lo, k = lo;
        while (true)
        {
          if (j != k)
          {
            Char hi = k - 1;
#if WITH_COMPACT_DFA == -1
            state->edges[lo] = std::pair<Char,State*>(hi, target_state);
#else
            state->edges[hi] = std::pair<Char,State*>(lo, target_state);
#endif
            lo = k = j;
          }
          j = i->first.find_next(j);
          ++k;
          ++eno_;
          size_t n = i->first.find_next(j);
          if (n == Bits::npos)
          {
            Char hi = k - 1;
#if WITH_COMPACT_DFA == -1
            state->edges[lo] = std::pair<Char,State*>(hi, target_state);
#else
            state->edges[hi] = std::pair<Char,State*>(lo, target_state);
#endif
            break;
          }
          j = static_cast<Char>(n);
        }
#else
        for (Chars::const_iterator j = i->first.begin(); j != i->first.end(); ++j)
        {
          Char lo = j->first;
          Char hi = j->second - 1; // -1 to adjust open ended [lo,hi)
#if WITH_COMPACT_DFA == -1
          state->edges[lo] = std::pair<Char,State*>(hi, target_state);
#else
          state->edges[hi] = std::pair<Char,State*>(lo, target_state);
#endif
          eno_ += hi - lo + 1;
        }
#endif
      }
    }
    if (state->accept > 0 && state->accept <= end_.size())
      acc_[state->accept - 1] = true;
    ++vno_;
  }
  vms_ = timer_elapsed(vt) - ems_;
  DBGLOG("END compile()");
}

void Pattern::lazy(
    const Positions& lazypos,
    Positions&       pos) const
{
  if (!lazypos.empty())
  {
    Positions pos1;
    lazy(lazypos, pos, pos1);
    pos.swap(pos1);
  }
}

void Pattern::lazy(
    const Positions& lazypos,
    const Positions& pos,
    Positions&       pos1) const
{
  for (Positions::const_iterator p = pos.begin(); p != pos.end(); ++p)
    for (Positions::const_iterator q = lazypos.begin(); q != lazypos.end(); ++q)
      // pos1.insert(p->lazy() ? *p : p->lazy(q->loc())); // CHECKED algorithmic options: only if p is not already lazy??
      pos1.insert(p->lazy(q->loc())); // overrides lazyness even when p is already lazy
}

void Pattern::greedy(Positions& pos) const
{
  Positions pos1;
  for (Positions::const_iterator p = pos.begin(); p != pos.end(); ++p)
    pos1.insert(p->lazy() ? *p : p->greedy(true)); // CHECKED algorithmic options: 7/29 guard added: p->lazy() ? *p : p->greedy(true)
    // CHECKED 10/21 pos1.insert(p->lazy(0).greedy(true));
  pos.swap(pos1);
}

void Pattern::trim_lazy(Positions& pos) const
{
#ifdef DEBUG
  DBGLOG("BEGIN trim_lazy({");
  for (Positions::const_iterator q = pos.begin(); q != pos.end(); ++q)
    DBGLOGPOS(*q);
  DBGLOGA(" })");
#endif
  Positions::reverse_iterator p = pos.rbegin();
  while (p != pos.rend() && p->lazy())
  {
    Location l = p->lazy();
    if (p->accept() || p->anchor()) // CHECKED algorithmic options: 7/28 added p->anchor()
    {
      pos.insert(p->lazy(0)); // make lazy accept/anchor a non-lazy accept/anchor
      pos.erase(--p.base());
      while (p != pos.rend() && !p->accept() && p->lazy() == l)
      {
#if 0 // CHECKED algorithmic options: set to 1 to turn lazy trimming off
        ++p;
#else
        pos.erase(--p.base());
#endif
      }
    }
    else
    {
#if 0 // CHECKED algorithmic options: 7/31
      if (p->greedy())
      {
        pos.insert(p->lazy(0).greedy(false));
        pos.erase(--p.base());
      }
      else
      {
        break; // ++p;
      }
#else
      if (!p->greedy()) // stop here, greedy bit is 0 from here on
        break;
      pos.insert(p->lazy(0));
      pos.erase(--p.base()); // CHECKED 10/21 ++p;
#endif
    }
  }
#if 0 // CHECKED algorithmic options: 7/31 but results in more states
  while (p != pos.rend() && p->greedy())
  {
    pos.insert(p->greedy(false));
    pos.erase(--p.base());
  }
#endif
  // trims accept positions keeping the first only, and keeping redo (positions with accept == 0)
  Positions::iterator q = pos.begin(), a = pos.end();
  while (q != pos.end())
  {
    if (q->accept() && q->accepts() != 0)
    {
      if (a == pos.end())
        a = q++;
      else
        pos.erase(q++);
    }
    else
    {
      ++q;
    }
  }
#ifdef DEBUG
  DBGLOG("END trim_lazy({");
  for (Positions::const_iterator q = pos.begin(); q != pos.end(); ++q)
    DBGLOGPOS(*q);
  DBGLOGA(" })");
#endif
}

void Pattern::compile_transition(
    State     *state,
    Follow&    followpos,
    const Map& modifiers,
    const Map& lookahead,
    Moves&     moves) const
{
  DBGLOG("BEGIN compile_transition()");
  Positions::const_iterator end = state->end();
  for (Positions::const_iterator k = state->begin(); k != end; ++k)
  {
    if (k->accept())
    {
      Index accept = k->accepts();
      if (state->accept == 0 || accept < state->accept)
        state->accept = accept; // pick lowest nonzero accept index
      if (accept == 0)
        state->redo = true;
    }
    else
    {
      Location loc = k->loc();
      Char c = at(loc);
      DBGLOGN("At %lu: %c", loc, c);
      bool literal = is_modified('q', modifiers, loc);
      if (c == '/' && opt_.l && !literal)
      {
        Position n(0);
        DBGLOG("LOOKAHEAD");
        for (Map::const_iterator i = lookahead.begin(); i != lookahead.end(); ++i)
        {
          Locations::const_iterator j = i->second.find(loc);
          DBGLOGN("%d %d (%d) %lu", state->accept, i->first, j != i->second.end(), n.loc());
          if (j != i->second.end())
          {
            if (!k->ticked())
              state->heads.insert(static_cast<Index>(n + std::distance(i->second.begin(), j)));
            else // CHECKED algorithmic options: 7/18 if (state->accept == i->first) no longer check for accept state, assume we are at an accept state
              state->tails.insert(static_cast<Index>(n + std::distance(i->second.begin(), j)));
          }
          n = n + i->second.size();
        }
      }
      else if (c == '(' && !literal)
      {
        Position n(0);
        DBGLOG("LOOKAHEAD HEAD");
        for (Map::const_iterator i = lookahead.begin(); i != lookahead.end(); ++i)
        {
          Locations::const_iterator j = i->second.find(loc);
          DBGLOGN("%d %d (%d) %lu", state->accept, i->first, j != i->second.end(), n.loc());
          if (j != i->second.end())
            state->heads.insert(static_cast<Index>(n + std::distance(i->second.begin(), j)));
          n = n + i->second.size();
        }
      }
      else if (c == ')' && !literal)
      {
        /* CHECKED algorithmic options: 7/18 do no longer check for accept state, assume we are at an accept state
        if (state->accept > 0)
        */
        {
          Position n(0);
          DBGLOG("LOOKAHEAD TAIL");
          for (Map::const_iterator i = lookahead.begin(); i != lookahead.end(); ++i)
          {
            Locations::const_iterator j = i->second.find(loc);
            DBGLOGN("%d %d (%d) %lu", state->accept, i->first, j != i->second.end(), n.loc());
            if (j != i->second.end() /* CHECKED algorithmic options: 7/18 && state->accept == i->first */ ) // only add lookstop when part of the proper accept state
              state->tails.insert(static_cast<Index>(n + std::distance(i->second.begin(), j)));
            n = n + i->second.size();
          }
        }
      }
      else
      {
        Follow::const_iterator i = followpos.find(k->pos());
        if (i != followpos.end())
        {
          if (k->lazy())
          {
#if 1 // CHECKED algorithmic options: 7/31 this optimization works fine when trim_lazy adds non-lazy greedy state, but may increase the total number of states:
            if (k->greedy())
              continue;
#endif
            Follow::iterator j = followpos.find(*k);
            if (j == followpos.end())
            {
              // followpos is not defined for lazy pos yet, so add lazy followpos (memoization)
              j = followpos.insert(std::pair<Position,Positions>(*k, Positions())).first;
              for (Positions::const_iterator p = i->second.begin(); p != i->second.end(); ++p)
                j->second.insert(/* p->lazy() || CHECKED algorithmic options: 7/31 */ p->ticked() ? *p : /* CHECKED algorithmic options: 7/31 adds too many states p->greedy() ? p->lazy(0).greedy(false) : */ p->lazy(k->lazy())); // CHECKED algorithmic options: 7/18 ticked() preserves lookahead tail at '/' and ')'
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
          const Positions &follow = i->second;
          Chars chars;
          if (literal)
          {
            chars.insert(c);
          }
          else
          {
            switch (c)
            {
              case '.':
                if (is_modified('s', modifiers, loc))
                {
                  chars.insert(0, 0xFF);
                }
                else
                {
                  chars.insert(0, 9);
                  chars.insert(11, 0xFF);
                }
                break;
              case '^':
                chars.insert(is_modified('m', modifiers, loc) ? META_BOL : META_BOB);
                break;
              case '$':
                chars.insert(is_modified('m', modifiers, loc) ? META_EOL : META_EOB);
                break;
              default:
                if (c == '[' && !escapes_at(loc, "AzBb<>ij"))
                {
                  compile_list(loc + 1, chars, modifiers);
                }
                else
                {
                  switch (escape_at(loc))
                  {
                    case 'i':
                      chars.insert(META_IND);
                      break;
                    case 'j':
                      chars.insert(META_DED);
                      break;
                    case 'k':
                      chars.insert(META_UND);
                      break;
                    case 'A':
                      chars.insert(META_BOB);
                      break;
                    case 'z':
                      chars.insert(META_EOB);
                      break;
                    case 'B':
                      chars.insert(k->anchor() ? META_NWB : META_NWE);
                      break;
                    case 'b':
                      if (k->anchor())
                        chars.insert(META_BWB, META_EWB);
                      else
                        chars.insert(META_BWE, META_EWE);
                      break;
                    case '<':
                      chars.insert(k->anchor() ? META_BWB : META_BWE);
                      break;
                    case '>':
                      chars.insert(k->anchor() ? META_EWB : META_EWE);
                      break;
                    case '\0': // no escape at current loc
                      if (std::isalpha(c) && is_modified('i', modifiers, loc))
                      {
                        chars.insert(uppercase(c));
                        chars.insert(lowercase(c));
                      }
                      else
                      {
                        chars.insert(c);
                      }
                      break;
                    default:
                      c = compile_esc(loc + 1, chars);
                      if (c <= 255 && std::isalpha(c) && is_modified('i', modifiers, loc))
                      {
                        chars.insert(uppercase(c));
                        chars.insert(lowercase(c));
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
  DBGLOG("END compile_transition()");
}

void Pattern::transition(
    Moves&           moves,
    const Chars&     chars,
    const Positions& follow) const
{
  Chars rest = chars;
  Moves::iterator i = moves.begin();
  Moves::const_iterator end = moves.end();
  while (i != end)
  {
    if (i->second == follow)
    {
      rest |= i->first;
      moves.erase(i++);
    }
    else if (chars.intersects(i->first))
    {
      Chars common(chars & i->first);
      if (is_subset(follow, i->second))
      {
        rest -= common;
        ++i;
      }
      else if (i->first == common)
      {
        if (is_subset(i->second, follow))
        {
          moves.erase(i++);
        }
        else
        {
          rest -= common;
          set_insert(i->second, follow);
        }
      }
      else
      {
        rest -= common;
        i->first -= common;
        Move back;
        back.first.swap(common);
        back.second = i->second;
        set_insert(back.second, follow);
        moves.push_back(back); // faster: C++11 emplace_back(Chars(), i->second); move.back().first.swap(common)
        ++i;
      }
    }
    else
    {
      ++i;
    }
  }
  if (rest.any())
    moves.push_back(Move(rest, follow)); // faster: C++11 move.emplace_back(rest, follow)
}

Pattern::Char Pattern::compile_esc(Location loc, Chars& chars) const
{
  Char c = at(loc);
  if (c == '0')
  {
    c = static_cast<Char>(std::strtoul(rex_.substr(loc + 1, 3).c_str(), NULL, 8));
  }
  else if ((c == 'x' || c == 'u') && at(loc + 1) == '{')
  {
    c = static_cast<Char>(std::strtoul(rex_.c_str() + 2, NULL, 16));
  }
  else if (c == 'x' && std::isxdigit(at(loc + 1)))
  {
    c = static_cast<Char>(std::strtoul(rex_.substr(loc + 1, 2).c_str(), NULL, 16));
  }
  else if (c == 'c')
  {
    c = at(loc + 1) % 32;
  }
  else if (c == 'e')
  {
    c = 0x1B;
  }
  else if (c == '_')
  {
    posix(6 /* alpha */, chars);
  }
  else if (c == 'N')
  {
    chars.insert(0, 9);
    chars.insert(11, 255);
  }
  else if ((c == 'p' || c == 'P') && at(loc + 1) == '{')
  {
    size_t i;
    for (i = 0; i < 14; ++i)
      if (eq_at(loc + 2, posix_class[i]))
        break;
    if (i < 14)
      posix(i, chars);
    else
      error(regex_error::invalid_class, loc);
    if (c == 'P')
      flip(chars);
    return META_EOL;
  }
  else
  {
    static const char abtnvfr[] = "abtnvfr";
    const char *s = std::strchr(abtnvfr, c);
    if (s)
    {
      c = static_cast<Char>(s - abtnvfr + '\a');
    }
    else
    {
      static const char escapes[] = "__sSxX________hHdD__lL__uUwW";
      s = std::strchr(escapes, c);
      if (s)
      {
        posix((s - escapes) / 2, chars);
        if ((s - escapes) % 2)
          flip(chars);
        return META_EOL;
      }
    }
  }
  if (c > 0xFF)
    error(regex_error::invalid_escape, loc);
  chars.insert(c);
  return c;
}

void Pattern::compile_list(Location loc, Chars& chars, const Map& modifiers) const
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
      Location c_loc;
      if (c == '[' && at(loc + 1) == ':' && (c_loc = find_at(loc + 2, ':')) != std::string::npos && at(c_loc + 1) == ']')
      {
        if (c_loc == loc + 3)
          c = compile_esc(loc + 2, chars);
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
        loc = c_loc + 1;
      }
      else if (c == opt_.e && opt_.e != '\0' && !opt_.b)
      {
        c = compile_esc(loc + 1, chars);
        parse_esc(loc);
        --loc;
      }
      if (!is_meta(c))
      {
        if (!is_meta(lo))
        {
          if (lo <= c)
            chars.insert(lo, c);
          else
            error(regex_error::invalid_class_range, loc);
          if (is_modified('i', modifiers, loc))
          {
            for (Char a = lo; a <= c; ++a)
            {
              if (std::isupper(a))
                chars.insert(lowercase(a));
              else if (std::islower(a))
                chars.insert(uppercase(a));
            }
          }
          c = META_EOL;
        }
        else
        {
          if (std::isalpha(c) && is_modified('i', modifiers, loc))
          {
            chars.insert(uppercase(c));
            chars.insert(lowercase(c));
          }
          else
          {
            chars.insert(c);
          }
        }
      }
      prev = c;
      lo = META_EOL;
    }
  }
  if (!is_meta(lo))
    chars.insert('-');
  if (complement)
    flip(chars);
}

void Pattern::posix(size_t index, Chars& chars) const
{
  DBGLOG("posix(%lu)", index);
  switch (index)
  {
    case 0:
      chars.insert(0x00, 0x7F);
      break;
    case 1:
      chars.insert('\t', '\r');
      chars.insert(' ');
      break;
    case 2:
      chars.insert('0', '9');
      chars.insert('A', 'F');
      chars.insert('a', 'f');
      break;
    case 3:
      chars.insert(0x00, 0x1F);
      chars.insert(0x7F);
      break;
    case 4:
      chars.insert(' ', '~');
      break;
    case 5:
      chars.insert('0', '9');
      chars.insert('A', 'Z');
      chars.insert('a', 'z');
      break;
    case 6:
      chars.insert('A', 'Z');
      chars.insert('a', 'z');
      break;
    case 7:
      chars.insert('\t');
      chars.insert(' ');
      break;
    case 8:
      chars.insert('0', '9');
      break;
    case 9:
      chars.insert('!', '~');
      break;
    case 10:
      chars.insert('a', 'z');
      break;
    case 11:
      chars.insert('!', '/');
      chars.insert(':', '@');
      chars.insert('[', '`');
      chars.insert('{', '~');
      break;
    case 12:
      chars.insert('A', 'Z');
      break;
    case 13:
      chars.insert('0', '9');
      chars.insert('A', 'Z');
      chars.insert('a', 'z');
      chars.insert('_');
      break;
  }
}

void Pattern::flip(Chars& chars) const
{
#if defined(WITH_BITS)
  chars.reserve(256).flip();
#else
  Chars flip;
  Char c = 0;
  for (Chars::const_iterator i = chars.begin(); i != chars.end(); ++i)
  {
    if (c < i->first)
      flip.insert(c, i->first - 1);
    c = i->second;
  }
  if (c <= 0xFF)
    flip.insert(c, 0xFF);
  chars.swap(flip);
#endif
}

void Pattern::assemble(State& start)
{
  DBGLOG("BEGIN assemble()");
  timer_type t;
  timer_start(t);
  predict_match_dfa(start);
  export_dfa(start);
  compact_dfa(start);
  encode_dfa(start);
  gencode_dfa(start);
  delete_dfa(start);
  export_code();
  wms_ = timer_elapsed(t);
  DBGLOG("END assemble()");
}

void Pattern::compact_dfa(State& start)
{
#if WITH_COMPACT_DFA == -1
  // edge compaction in reverse order
  for (State *state = &start; state; state = state->next)
  {
    for (State::Edges::iterator i = state->edges.begin(); i != state->edges.end(); ++i)
    {
      Char hi = i->second.first;
      if (hi >= 0xFF)
        break;
      State::Edges::iterator j = i;
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
  for (State *state = &start; state; state = state->next)
  {
    for (State::Edges::reverse_iterator i = state->edges.rbegin(); i != state->edges.rend(); ++i)
    {
      Char lo = i->second.first;
      if (lo <= 0x00)
        break;
      State::Edges::reverse_iterator j = i;
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

void Pattern::encode_dfa(State& start)
{
  nop_ = 0;
  for (State *state = &start; state; state = state->next)
  {
    state->index = nop_;
#if WITH_COMPACT_DFA == -1
    Char hi = 0x00;
    for (State::Edges::const_iterator i = state->edges.begin(); i != state->edges.end(); ++i)
    {
      Char lo = i->first;
      if (lo == hi)
        hi = i->second.first + 1;
      ++nop_;
      if (is_meta(lo))
        nop_ += i->second.first - lo;
    }
    // add dead state only when needed
    if (hi <= 0xFF)
    {
      state->edges[hi] = std::pair<Char,State*>(0xFF, NULL);
      ++nop_;
    }
#else
    Char lo = 0xFF;
    bool covered = false;
    for (State::Edges::const_reverse_iterator i = state->edges.rbegin(); i != state->edges.rend(); ++i)
    {
      Char hi = i->first;
      if (lo == hi)
      {
        if (i->second.first == 0x00)
          covered = true;
        else
          lo =  i->second.first - 1;
      }
      ++nop_;
      if (is_meta(lo))
        nop_ += hi - i->second.first;
    }
    // add dead state only when needed
    if (!covered)
    {
      state->edges[lo] = std::pair<Char,State*>(0x00, NULL);
      ++nop_;
    }
#endif
    nop_ += static_cast<Index>(state->heads.size() + state->tails.size() + (state->accept > 0 || state->redo));
    if (nop_ < state->index)
      throw regex_error(regex_error::exceeds_limits, rex_, rex_.size());
  }
  Opcode *opcode = new Opcode[nop_];
  opc_ = opcode;
  Index pc = 0;
  for (const State *state = &start; state; state = state->next)
  {
    if (state->redo)
      opcode[pc++] = opcode_redo();
    else if (state->accept > 0)
      opcode[pc++] = opcode_take(state->accept);
    for (Set::const_iterator i = state->tails.begin(); i != state->tails.end(); ++i)
      opcode[pc++] = opcode_tail(static_cast<Index>(*i));
    for (Set::const_iterator i = state->heads.begin(); i != state->heads.end(); ++i)
      opcode[pc++] = opcode_head(static_cast<Index>(*i));
#if WITH_COMPACT_DFA == -1
    for (State::Edges::const_reverse_iterator i = state->edges.rbegin(); i != state->edges.rend(); ++i)
    {
      Char lo = i->first;
      Char hi = i->second.first;
      Index target_index = Const::IMAX;
      if (i->second.second)
        target_index = i->second.second->index;
      if (!is_meta(lo))
      {
        opcode[pc++] = opcode_goto(lo, hi, target_index);
      }
      else
      {
        do
        {
          opcode[pc++] = opcode_goto(lo, lo, target_index);
        } while (++lo <= hi);
      }
    }
#else
    for (State::Edges::const_reverse_iterator i = state->edges.rbegin(); i != state->edges.rend(); ++i)
    {
      Char hi = i->first;
      Char lo = i->second.first;
      if (is_meta(lo))
      {
        Index target_index = Const::IMAX;
        if (i->second.second)
          target_index = i->second.second->index;
        do
        {
          opcode[pc++] = opcode_goto(lo, lo, target_index);
        } while (++lo <= hi);
      }
    }
    for (State::Edges::const_iterator i = state->edges.begin(); i != state->edges.end(); ++i)
    {
      Char lo = i->second.first;
      if (!is_meta(lo))
      {
        Char hi = i->first;
        Index target_index = Const::IMAX;
        if (i->second.second)
          target_index = i->second.second->index;
        opcode[pc++] = opcode_goto(lo, hi, target_index);
      }
    }
#endif
  }
}

void Pattern::gencode_dfa(const State& start) const
{
  if (!opt_.o)
    return;
  for (std::vector<std::string>::const_iterator i = opt_.f.begin(); i != opt_.f.end(); ++i)
  {
    const std::string& filename = *i;
    size_t len = filename.length();
    if ((len > 2 && filename.compare(len - 2, 2, ".h"  ) == 0)
     || (len > 4 && filename.compare(len - 4, 4, ".hpp") == 0)
     || (len > 4 && filename.compare(len - 4, 4, ".cpp") == 0)
     || (len > 3 && filename.compare(len - 3, 3, ".cc" ) == 0))
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
            "  int c0 = 0, c1 = 0;\n"
            "  m.FSM_INIT(c1);\n", opt_.n.empty() ? "FSM" : opt_.n.c_str());
        for (const State *state = &start; state; state = state->next)
        {
          ::fprintf(file, "\nS%u:\n", state->index);
          if (state == &start)
            ::fprintf(file, "  m.FSM_FIND();\n");
          if (state->redo)
            ::fprintf(file, "  m.FSM_REDO();\n");
          else if (state->accept > 0)
            ::fprintf(file, "  m.FSM_TAKE(%u);\n", state->accept);
          for (Set::const_iterator i = state->tails.begin(); i != state->tails.end(); ++i)
            ::fprintf(file, "  m.FSM_TAIL(%zu);\n", *i);
          for (Set::const_iterator i = state->heads.begin(); i != state->heads.end(); ++i)
            ::fprintf(file, "  m.FSM_HEAD(%zu);\n", *i);
          if (state->edges.rbegin() != state->edges.rend() && state->edges.rbegin()->first == META_DED)
            ::fprintf(file, "  if (m.FSM_DENT()) goto S%u;\n", state->edges.rbegin()->second.second->index);
          bool peek = false; // if we need to read a character into c1
          bool prev = false; // if we need to keep the previous character in c0
          for (State::Edges::const_reverse_iterator i = state->edges.rbegin(); i != state->edges.rend(); ++i)
          {
#if WITH_COMPACT_DFA == -1
            Char lo = i->first;
            Char hi = i->second.first;
#else
            Char hi = i->first;
            Char lo = i->second.first;
#endif
            if (!is_meta(lo))
            {
              Index target_index = Const::IMAX;
              if (i->second.second != NULL)
                target_index = i->second.second->index;
              State::Edges::const_reverse_iterator j = i;
              if (target_index == Const::IMAX && (++j == state->edges.rend() || is_meta(j->second.first)))
                break;
              peek = true;
            }
            else
            {
              do
              {
                if (lo == META_EOB || lo == META_EOL)
                  peek = true;
                else if (lo == META_EWE || lo == META_BWE || lo == META_NWE)
                  prev = peek = true;
                check_dfa_closure(i->second.second, 2, peek, prev);
              } while (++lo <= hi);
            }
          }
          bool read = peek;
          bool elif = false;
#if WITH_COMPACT_DFA == -1
          for (State::Edges::const_reverse_iterator i = state->edges.rbegin(); i != state->edges.rend(); ++i)
          {
            Char lo = i->first;
            Char hi = i->second.first;
            Index target_index = Const::IMAX;
            if (i->second.second != NULL)
              target_index = i->second.second->index;
            if (read)
            {
              if (prev)
                ::fprintf(file, "  c0 = c1, c1 = m.FSM_CHAR();\n");
              else
                ::fprintf(file, "  c1 = m.FSM_CHAR();\n");
              read = false;
            }
            if (!is_meta(lo))
            {
              State::Edges::const_reverse_iterator j = i;
              if (target_index == Const::IMAX && (++j == state->edges.rend() || is_meta(j->second.first)))
                break;
              if (lo == hi)
              {
                ::fprintf(file, "  if (c1 == ");
                print_char(file, lo);
                ::fprintf(file, ")");
              }
              else if (hi == 0xFF)
              {
                ::fprintf(file, "  if (");
                print_char(file, lo);
                ::fprintf(file, " <= c1)");
              }
              else
              {
                ::fprintf(file, "  if (");
                print_char(file, lo);
                ::fprintf(file, " <= c1 && c1 <= ");
                print_char(file, hi);
                ::fprintf(file, ")");
              }
              if (target_index == Const::IMAX)
              {
                if (peek)
                  ::fprintf(file, " return m.FSM_HALT(c1);\n");
                else
                  ::fprintf(file, " return m.FSM_HALT();\n");
              }
              else
              {
                ::fprintf(file, " goto S%u;\n", target_index);
              }
            }
            else
            {
              do
              {
                switch (lo)
                {
                  case META_EOB:
                  case META_EOL:
                    ::fprintf(file, "  ");
                    if (elif)
                      ::fprintf(file, "else ");
                    ::fprintf(file, "if (m.FSM_META_%s(c1)) {\n", meta_label[lo - META_MIN]);
                    gencode_dfa_closure(file, i->second.second, 2, peek);
                    ::fprintf(file, "  }\n");
                    elif = true;
                    break;
                  case META_EWE:
                  case META_BWE:
                  case META_NWE:
                    ::fprintf(file, "  ");
                    if (elif)
                      ::fprintf(file, "else ");
                    ::fprintf(file, "if (m.FSM_META_%s(c0, c1)) {\n", meta_label[lo - META_MIN]);
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
#else
          for (State::Edges::const_reverse_iterator i = state->edges.rbegin(); i != state->edges.rend(); ++i)
          {
            Char hi = i->first;
            Char lo = i->second.first;
            if (is_meta(lo))
            {
              if (read)
              {
                if (prev)
                  ::fprintf(file, "  c0 = c1, c1 = m.FSM_CHAR();\n");
                else
                  ::fprintf(file, "  c1 = m.FSM_CHAR();\n");
                read = false;
              }
              do
              {
                switch (lo)
                {
                  case META_EOB:
                  case META_EOL:
                    ::fprintf(file, "  ");
                    if (elif)
                      ::fprintf(file, "else ");
                    ::fprintf(file, "if (m.FSM_META_%s(c1)) {\n", meta_label[lo - META_MIN]);
                    gencode_dfa_closure(file, i->second.second, 2, peek);
                    ::fprintf(file, "  }\n");
                    elif = true;
                    break;
                  case META_EWE:
                  case META_BWE:
                  case META_NWE:
                    ::fprintf(file, "  ");
                    if (elif)
                      ::fprintf(file, "else ");
                    ::fprintf(file, "if (m.FSM_META_%s(c0, c1)) {\n", meta_label[lo - META_MIN]);
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
          for (State::Edges::const_iterator i = state->edges.begin(); i != state->edges.end(); ++i)
          {
            Char hi = i->first;
            Char lo = i->second.first;
            Index target_index = Const::IMAX;
            if (i->second.second != NULL)
              target_index = i->second.second->index;
            if (read)
            {
              if (prev)
                ::fprintf(file, "  c0 = c1, c1 = m.FSM_CHAR();\n");
              else
                ::fprintf(file, "  c1 = m.FSM_CHAR();\n");
              read = false;
            }
            if (!is_meta(lo))
            {
              State::Edges::const_iterator j = i;
              if (target_index == Const::IMAX && (++j == state->edges.end() || is_meta(j->second.first)))
                break;
              if (lo == hi)
              {
                ::fprintf(file, "  if (c1 == ");
                print_char(file, lo);
                ::fprintf(file, ")");
              }
              else if (hi == 0xFF)
              {
                ::fprintf(file, "  if (");
                print_char(file, lo);
                ::fprintf(file, " <= c1)");
              }
              else
              {
                ::fprintf(file, "  if (");
                print_char(file, lo);
                ::fprintf(file, " <= c1 && c1 <= ");
                print_char(file, hi);
                ::fprintf(file, ")");
              }
              if (target_index == Const::IMAX)
              {
                if (peek)
                  ::fprintf(file, " return m.FSM_HALT(c1);\n");
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
            ::fprintf(file, "  return m.FSM_HALT(c1);\n");
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
  }
}

void Pattern::check_dfa_closure(const State *state, int nest, bool& peek, bool& prev) const
{
  if (nest > 5)
    return;
  for (State::Edges::const_reverse_iterator i = state->edges.rbegin(); i != state->edges.rend(); ++i)
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
        if (lo == META_EOB || lo == META_EOL)
          peek = true;
        else if (lo == META_EWE || lo == META_BWE || lo == META_NWE)
          prev = peek = true;
        check_dfa_closure(i->second.second, 2, peek, prev);
      } while (++lo <= hi);
    }
  }
}

void Pattern::gencode_dfa_closure(FILE *file, const State *state, int nest, bool peek) const
{
  bool elif = false;
  if (state->redo)
  {
    if (peek)
      ::fprintf(file, "%*sm.FSM_REDO(c1);\n", 2*nest, "");
    else
      ::fprintf(file, "%*sm.FSM_REDO();\n", 2*nest, "");
  }
  else if (state->accept > 0)
  {
    if (peek)
      ::fprintf(file, "%*sm.FSM_TAKE(%u, c1);\n", 2*nest, "", state->accept);
    else
      ::fprintf(file, "%*sm.FSM_TAKE(%u);\n", 2*nest, "", state->accept);
  }
  for (Set::const_iterator i = state->tails.begin(); i != state->tails.end(); ++i)
    ::fprintf(file, "%*sm.FSM_TAIL(%zu);\n", 2*nest, "", *i);
  if (nest > 5)
    return;
  for (State::Edges::const_reverse_iterator i = state->edges.rbegin(); i != state->edges.rend(); ++i)
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
            ::fprintf(file, "%*s", 2*nest, "");
            if (elif)
              ::fprintf(file, "else ");
            ::fprintf(file, "if (m.FSM_META_%s(c1)) {\n", meta_label[lo - META_MIN]);
            gencode_dfa_closure(file, i->second.second, nest + 1, peek);
            ::fprintf(file, "%*s}\n", 2*nest, "");
            elif = true;
            break;
          case META_EWE:
          case META_BWE:
          case META_NWE:
            ::fprintf(file, "%*s", 2*nest, "");
            if (elif)
              ::fprintf(file, "else ");
            ::fprintf(file, "if (m.FSM_META_%s(c0, c1)) {\n", meta_label[lo - META_MIN]);
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
  }
}

void Pattern::delete_dfa(State& start)
{
  const State *state = start.next;
  while (state)
  {
    const State *next_state = state->next;
    delete state;
    state = next_state;
  }
  start.next = NULL;
}

void Pattern::export_dfa(const State& start) const
{
  for (std::vector<std::string>::const_iterator i = opt_.f.begin(); i != opt_.f.end(); ++i)
  {
    const std::string& filename = *i;
    size_t len = filename.length();
    if (len > 3 && filename.compare(len - 3, 3, ".gv") == 0)
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
        ::fprintf(file, "digraph %s {\n\t\trankdir=LR;\n\t\tconcentrate=true;\n\t\tnode [fontname=\"ArialNarrow\"];\n\t\tedge [fontname=\"Courier\"];\n\n\t\tinit [root=true,peripheries=0,label=\"%s\",fontname=\"Courier\"];\n\t\tinit -> N%p;\n", opt_.n.empty() ? "FSM" : opt_.n.c_str(), opt_.n.c_str(), (void*)&start);
        for (const State *state = &start; state; state = state->next)
        {
          if (state == &start)
            ::fprintf(file, "\n/*START*/\t");
          if (state->redo) // state->accept == Const::IMAX)
            ::fprintf(file, "\n/*REDO*/\t");
          else if (state->accept)
            ::fprintf(file, "\n/*ACCEPT %hu*/\t", state->accept);
          for (Set::const_iterator i = state->heads.begin(); i != state->heads.end(); ++i)
            ::fprintf(file, "\n/*HEAD %zu*/\t", *i);
          for (Set::const_iterator i = state->tails.begin(); i != state->tails.end(); ++i)
            ::fprintf(file, "\n/*TAIL %zu*/\t", *i);
          if (state != &start && !state->accept && state->heads.empty() && state->tails.empty())
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
              ::fprintf(file, "(%hu)", i->accepts());
            }
            else
            {
              if (i->iter())
                ::fprintf(file, "%hu.", i->iter());
              ::fprintf(file, "%zu", i->loc());
            }
            if (i->lazy())
              ::fprintf(file, "?%zu", i->lazy());
            if (i->anchor())
              ::fprintf(file, "^");
            if (i->greedy())
              ::fprintf(file, "!");
            if (i->ticked())
              ::fprintf(file, "'");
            if (k++ % n)
              sep = " ";
            else
              sep = "\\n";
          }
          if ((state->accept && !state->redo) || !state->heads.empty() || !state->tails.empty())
            ::fprintf(file, "\\n");
#endif
          if (state->accept && !state->redo) // state->accept != Const::IMAX)
            ::fprintf(file, "[%hu]", state->accept);
          for (Set::const_iterator i = state->tails.begin(); i != state->tails.end(); ++i)
            ::fprintf(file, "%zu>", *i);
          for (Set::const_iterator i = state->heads.begin(); i != state->heads.end(); ++i)
            ::fprintf(file, "<%zu", *i);
          if (state->redo) // state->accept != Const::IMAX)
            ::fprintf(file, "\",style=dashed,peripheries=1];\n");
          else if (state->accept) // state->accept != Const::IMAX)
            ::fprintf(file, "\",peripheries=2];\n");
          else if (!state->heads.empty())
            ::fprintf(file, "\",style=dashed,peripheries=2];\n");
          else
            ::fprintf(file, "\"];\n");
          for (State::Edges::const_iterator i = state->edges.begin(); i != state->edges.end(); ++i)
          {
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
                ::fprintf(file, "\\\\x%2.2x", lo);
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
                  ::fprintf(file, "\\\\x%2.2x", hi);
              }
              ::fprintf(file, "\"];\n");
            }
            else
            {
              do
              {
                ::fprintf(file, "\t\tN%p -> N%p [label=\"%s\",style=\"dashed\"];\n", (void*)state, (void*)i->second.second, meta_label[lo - META_MIN]);
              } while (++lo <= hi);
            }
          }
          if (state->redo) // state->accept == Const::IMAX)
            ::fprintf(file, "\t\tN%p -> R%p;\n\t\tR%p [peripheries=0,label=\"redo\"];\n", (void*)state, (void*)state, (void*)state);
        }
        ::fprintf(file, "}\n");
        if (file != stdout)
          ::fclose(file);
      }
    }
  }
}

void Pattern::export_code() const
{
  if (!nop_)
    return;
  if (opt_.o)
    return;
  for (std::vector<std::string>::const_iterator i = opt_.f.begin(); i != opt_.f.end(); ++i)
  {
    const std::string& filename = *i;
    size_t len = filename.length();
    if ((len > 2 && filename.compare(len - 2, 2, ".h"  ) == 0)
     || (len > 4 && filename.compare(len - 4, 4, ".hpp") == 0)
     || (len > 4 && filename.compare(len - 4, 4, ".cpp") == 0)
     || (len > 3 && filename.compare(len - 3, 3, ".cc" ) == 0))
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
        ::fprintf(file, "extern REFLEX_CODE_DECL reflex_code_%s[%hu] =\n{\n", opt_.n.empty() ? "FSM" : opt_.n.c_str(), nop_);
        for (Index i = 0; i < nop_; ++i)
        {
          Opcode opcode = opc_[i];
          ::fprintf(file, "  0x%08X, // %hu: ", opcode, i);
          Index index = index_of(opcode);
          if (is_opcode_redo(opcode))
          {
            ::fprintf(file, "REDO\n");
          }
          else if (is_opcode_take(opcode))
          {
            ::fprintf(file, "TAKE %u\n", index);
          }
          else if (is_opcode_tail(opcode))
          {
            ::fprintf(file, "TAIL %u\n", index);
          }
          else if (is_opcode_head(opcode))
          {
            ::fprintf(file, "HEAD %u\n", index);
          }
          else if (is_opcode_halt(opcode))
          {
            ::fprintf(file, "HALT\n");
          }
          else
          {
            if (index == Const::IMAX)
              ::fprintf(file, "HALT ON ");
            else
              ::fprintf(file, "GOTO %u ON ", index);
            Char lo = lo_of(opcode);
            if (!is_meta(lo))
            {
              print_char(file, lo, true);
              Char hi = hi_of(opcode);
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
}

void Pattern::predict_match_dfa(State& start)
{
  DBGLOG("BEGIN Pattern::predict_match_dfa()");
  State *state = &start;
  one_ = true;
  while (state->accept == 0)
  {
    if (state->edges.size() != 1)
    {
      one_ = false;
      break;
    }
    Char lo = state->edges.begin()->first;
    if (!is_meta(lo) && lo == state->edges.begin()->second.first)
    {
      if (lo != state->edges.begin()->second.first)
        break;
      if (len_ >= 255)
      {
        one_ = false;
        break;
      }
      pre_[len_++] = lo;
    }
    else
    {
      one_ = false;
      break;
    }
    State *next = state->edges.begin()->second.second;
    if (next == NULL)
    {
      one_ = false;
      break;
    }
    state = next;
  }
  if (state != NULL && state->accept != 0 && !state->edges.empty())
    one_ = false;
  min_ = 0;
  std::memset(bit_, 0xFF, sizeof(bit_));
  std::memset(pmh_, 0xFF, sizeof(pmh_));
  std::memset(pma_, 0xFF, sizeof(pma_));
  if (state != NULL && state->accept == 0)
  {
    gen_predict_match(state);
#ifdef DEBUG
    for (Index i = 0; i < 256; ++i)
    {
      if (bit_[i] != 0xFF)
      {
        if (isprint(i))
          DBGLOGN("bit['%c'] = %2x\n", i, bit_[i]);
        else
          DBGLOGN("bit[%3d] = %2x\n", i, bit_[i]);
      }
    }
    for (Index i = 0; i < Const::HASH; ++i)
    {
      if (pmh_[i] != 0xFF)
      {
        if (isprint(pmh_[i]))
          DBGLOGN("pmh['%c'] = %2x\n", i, pmh_[i]);
        else
          DBGLOGN("pmh[%3d] = %2x\n", i, pmh_[i]);
      }
    }
    for (Index i = 0; i < Const::HASH; ++i)
    {
      if (pma_[i] != 0xFF)
      {
        if (isprint(pma_[i]))
          DBGLOGN("pma['%c'] = %2x\n", i, pma_[i]);
        else
          DBGLOGN("pma[%3d] = %2x\n", i, pma_[i]);
      }
    }
#endif
  }
  DBGLOGN("min = %zu", min_);
  DBGLOG("END Pattern::predict_match_dfa()");
}

void Pattern::gen_predict_match(State *state)
{
  min_ = 8;
  std::map<State*,ORanges<Hash> > states[8];
  gen_predict_match_transitions(state, states[0]);
  for (Index level = 1; level < 8; ++level)
    for (std::map<State*,ORanges<Hash> >::iterator from = states[level - 1].begin(); from != states[level - 1].end(); ++from)
      gen_predict_match_transitions(level, from->first, from->second, states[level]);
  for (Char i = 0; i < 256; ++i)
    bit_[i] &= (1 << min_) - 1;
}

void Pattern::gen_predict_match_transitions(State *state, std::map<State*,ORanges<Hash> >& states)
{
  for (State::Edges::const_iterator edge = state->edges.begin(); edge != state->edges.end(); ++edge)
  {
    Char lo = edge->first;
    if (is_meta(lo))
    {
      min_ = 0;
      break;
    }
    State *next = edge->second.second;
    bool accept = (next == NULL || next->accept != 0);
    if (!accept)
    {
      for (State::Edges::const_iterator e = next->edges.begin(); e != next->edges.end(); ++e)
      {
        if (is_meta(e->first))
        {
          if (e == next->edges.begin())
            next = NULL;
          accept = true;
          break;
        }
      }
    }
    else if (next != NULL && next->edges.empty())
    {
      next = NULL;
    }
    if (accept)
      min_ = 1;
    Char hi = edge->second.first;
    while (lo <= hi)
    {
      bit_[lo] &= ~1;
      pmh_[lo] &= ~1;
      if (accept)
        pma_[lo] &= ~(1 << 7);
      pma_[lo] &= ~(1 << 6);
      if (next != NULL)
        states[next].insert(hash(lo));
      ++lo;
    }
  }
}

void Pattern::gen_predict_match_transitions(Index level, State *state, ORanges<Hash>& labels, std::map<State*,ORanges<Hash> >& states)
{
  for (State::Edges::const_iterator edge = state->edges.begin(); edge != state->edges.end(); ++edge)
  {
    Char lo = edge->first;
    if (is_meta(lo))
      break;
    State *next = level < 7 ? edge->second.second : NULL;
    bool accept = next == NULL || next->accept != 0;
    if (!accept)
    {
      for (State::Edges::const_iterator e = next->edges.begin(); e != next->edges.end(); ++e)
      {
        if (is_meta(e->first))
        {
          if (e == next->edges.begin())
            next = NULL;
          accept = true;
          break;
        }
      }
    }
    else if (next != NULL && next->edges.empty())
    {
      next = NULL;
    }
    if (accept && min_ > level)
      min_ = level + 1;
    if (level < 4 || level <= min_)
    {
      Char hi = edge->second.first;
      if (level <= min_)
        while (lo <= hi)
          bit_[lo++] &= ~(1 << level);
      for (ORanges<Hash>::const_iterator label = labels.begin(); label != labels.end(); ++label)
      {
        Hash label_hi = label->second - 1;
        for (Hash label_lo = label->first; label_lo <= label_hi; ++label_lo)
        {
          for (lo = edge->first; lo <= hi; ++lo)
          {
            Hash h = hash(label_lo, lo);
            pmh_[h] &= ~(1 << level);
            if (level < 4)
            {
              if (level == 3 || accept)
                pma_[h] &= ~(1 << (7 - 2 * level));
              pma_[h] &= ~(1 << (6 - 2 * level));
            }
            if (next != NULL)
              states[next].insert(hash(h));
          }
        }
      }
    }
  }
}

void Pattern::write_predictor(FILE *file) const
{
  ::fprintf(file, "extern const reflex::Pattern::Pred reflex_pred_%s[%zu] = {", opt_.n.empty() ? "FSM" : opt_.n.c_str(), 2 + len_ + (min_ > 1 && len_ == 0) * 256 + (min_ > 0) * Const::HASH);
  ::fprintf(file, "\n  %3hhu,%3hhu,", static_cast<uint8_t>(len_), (static_cast<uint8_t>(min_ | (one_ << 4))));
  for (size_t i = 0; i < len_; ++i)
    ::fprintf(file, "%s%3hhu,", ((i + 2) & 0xF) ? "" : "\n  ", static_cast<uint8_t>(pre_[i]));
  if (min_ > 0)
  {
    if (min_ > 1 && len_ == 0)
    {
      for (Index i = 0; i < 256; ++i)
        ::fprintf(file, "%s%3hhu,", (i & 0xF) ? "" : "\n  ", static_cast<uint8_t>(~bit_[i]));
    }
    if (min_ >= 4)
    {
      for (Index i = 0; i < Const::HASH; ++i)
        ::fprintf(file, "%s%3hhu,", (i & 0xF) ? "" : "\n  ", static_cast<uint8_t>(~pmh_[i]));
    }
    else
    {
      for (Index i = 0; i < Const::HASH; ++i)
        ::fprintf(file, "%s%3hhu,", (i & 0xF) ? "" : "\n  ", static_cast<uint8_t>(~pma_[i]));
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
