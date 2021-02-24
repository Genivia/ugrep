/******************************************************************************\
* Copyright (c) 2019, Robert van Engelen, Genivia Inc. All rights reserved.    *
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
@file      cnf.cpp
@brief     CNF class for normalization of Boolean search queries
@author    Robert van Engelen - engelen@genivia.com
@copyright (c) 2019-2020, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt
*/

#include "cnf.hpp"

// parse a pattern into an operator tree using a recursive descent parser
void CNF::OpTree::parse(const char *& pattern)
{
  while (true)
  {
    parse1(pattern);

    if (*pattern == '\0')
      break;

    ++pattern;
  }
}

// <parse1> -> <parse2> { <space>+ [ 'AND' <space>+ ] <parse2> }*
void CNF::OpTree::parse1(const char *& pattern)
{
  skip_space(pattern);

  while (*pattern != '\0' && *pattern != ')')
  {
    list.emplace_back(OR);
    list.back().parse2(pattern);

    skip_space(pattern);

    if (is_oper(AND, pattern))
      skip_space(pattern);
  }
}

// <parse2> -> <parse3> { [ '|'+ | 'OR' <space>+ ] <parse3> }*
void CNF::OpTree::parse2(const char *& pattern)
{
  do
  {
    list.emplace_back(NONE);
    list.back().parse3(pattern);
  } while (is_alternation(pattern));
}

// <parse3> -> [ '-' <space>* | 'NOT' <space>+ ] <parse4>
// <parse4> -> '(' <parse1> ')' | <pattern>
void CNF::OpTree::parse3(const char *& pattern)
{
  if (*pattern == '-' || is_oper(NOT, pattern))
  {
    op = NOT;

    ++pattern;

    skip_space(pattern);
  }

  if (*pattern == '(')
  {
    ++pattern;

    list.emplace_back(AND);
    list.back().parse1(pattern);

    if (*pattern == ')')
      ++pattern;
  }
  else
  {
    const char *p;

    regex.clear();

    while (true)
    {
      if (*pattern == '"')
      {
        p = ++pattern;

        while (*pattern != '\0' && *pattern != '"')
          if (*pattern++ == '\\' && *pattern != '\0')
            ++pattern;

        std::string quoted(p, pattern - p);

        size_t from = 0;
        size_t to;

        // replace each \" by "
        while ((to = quoted.find("\\\"", from)) != std::string::npos)
        {
          quoted.erase(to, 1);
          from = to + 1;
        }

        // if not -x then quote the string with \Q and \E
        if (!flag_fixed_strings)
          quote(quoted);

        regex.append(quoted);

        if (*pattern == '"')
          ++pattern;
      }
      else
      {
        p = pattern;

        int n = 0;

        while (*pattern != '\0')
        {
          if (*pattern == '\\' && pattern[1] == 'Q')
            while (*pattern != '\0' && (*pattern != '\\' || pattern[1] != 'E'))
              ++pattern;
          else if (*pattern == '\\' && pattern[1] != '\0')
            ++pattern;
          else if (n == 0 && (*pattern == ')' || *pattern == '|' || *pattern == '"' || isspace(*pattern)))
            break;
          else if (*pattern == '(')
            ++n;
          else if (*pattern == ')')
            --n;

          ++pattern;
        }

        if (p == pattern)
          break;

        regex.append(p, pattern - p);
      }
    }

    if (flag_line_regexp && regex.empty())
      regex.assign("^$");
    else if (flag_fixed_strings)
      quote(regex);

    // -w and -x
    anchor(regex);
  }
}

// normalize operator tree to CNF
void CNF::OpTree::normalize(bool invert)
{
  invert ^= op == NOT;

  if (list.empty())
  {
    op = invert ? NOT : NONE;
  }
  else
  {
    // normalize list terms
    for (auto& i : list)
      i.normalize(invert);

    if (list.empty())
    {
      op = NONE;
    }
    else if (list.size() == 1)
    {
      // (P) => P
      std::list<OpTree> newlist;
      newlist.swap(list);
      *this = newlist.front();
    }
    else if (invert)
    {
      // !(P&Q) => !P|!Q
      // !(P|Q) => !P&!Q
      if (op == AND)
        op = OR;
      else if (op == OR)
        op = AND;
    }
    
    // P&(Q&R) => P&Q&R
    // P|(Q|R) => P|Q|R
    auto i = list.begin();
    auto e = list.end();
    while (i != e)
    {
      if (i->op == op)
      {
        list.insert(e, i->list.begin(), i->list.end());
        list.erase(i++);
      }
      else
      {
        ++i;
      }
    }

    if (op == OR)
    {
      // (P&Q)|R => (P|R)&(Q|R)
      // (P&Q)|(R&S) => (P|R)&(P|S)&(Q|R)&(Q|S)
      i = list.begin();
      e = list.end();
      while (i != e && i->op != AND)
        ++i;

      if (i != e)
      {
        // isolate (P&Q&...) from ...|(P&Q&...)|...
        std::list<OpTree> newlist;
        newlist.swap(i->list);
        list.erase(i);

        // construct nested lists (P|...)&(Q|...)&...
        for (auto& j : newlist)
        {
          OpTree opt(OR);
          opt.list.emplace_back(j.op);
          opt.list.back().regex.swap(j.regex);
          opt.list.back().list.swap(j.list);
          j = opt;
        }

        i = list.begin();
        while (i != e)
        {
          if (i->op == AND)
          {
            // (P&Q)|(R&S) => (P|R)&(P|S)&(Q|R)&(Q|S)
            std::list<OpTree> product;

            for (auto& j : i->list)
            {
              auto duplist = newlist;

              for (auto& k : duplist)
                k.list.emplace_back(j);

              product.insert(product.end(), duplist.begin(), duplist.end());
            }

            newlist.swap(product);
          }
          else
          {
            // (P&Q)|R => (P|R)&(Q|R)
            for (auto& j : newlist)
              j.list.emplace_back(*i);
          }

          list.erase(i++);
        }

        op = AND;
        list.swap(newlist);
      }
    }
  }
}

// convert CNF-normalized operator tree to terms, a CNF AND-list of OR-term lists
void CNF::OpTree::convert(Terms& terms)
{
  if (op == OpTree::AND)
  {
    for (const auto& i : list)
    {
      if (!terms.back().empty())
        terms.emplace_back();

      if (i.op == OpTree::OR)
      {
        auto k = i.list.begin();
        auto e = i.list.end();

        while (k != e && (k->op != NONE || !k->regex.empty()))
          ++k;

        if (k != e)
        {
          // empty pattern found, ignore all other patterns
          k->add_to(terms);
        }
        else
        {
          for (const auto& j : i.list)
            j.add_to(terms);
        }
      }
      else
      {
        i.add_to(terms);
      }

      if (terms.back().empty())
      {
        // pop unused ending '|' (or BRE '\|')
        terms.pop_back();
        if (flag_basic_regexp)
          terms.pop_back();
      }
    }
  }
  else if (op == OpTree::OR)
  {
    for (const auto& i : list)
      i.add_to(terms);
  }
  else
  {
    add_to(terms);
  }
}

// add a [NOT] term of the operator tree to terms
void CNF::OpTree::add_to(Terms& terms) const
{
  Term& term = terms.back();

  if (op == OpTree::NOT)
  {
    if (!regex.empty())
    {
      if (term.empty())
        term.emplace_back();
      else if (term.front() && term.front()->empty())
        return; // empty pattern means anything matches

      term.emplace_back(new std::string(regex));
    }
  }
  else
  {
    if (term.empty())
      term.emplace_back(new std::string(regex));
    else if (!term.front())
      term.front() = Pattern(new std::string(regex));
    else if (term.front()->empty())
      ; // empty pattern means anything matches
    else if (regex.empty())
      term.front()->clear(); // empty pattern means anything matches
    else
      term.front()->append(flag_basic_regexp ? "\\|" : "|").append(regex);

    // empty pattern means anything matches
    if (term.front()->empty())
    {
      auto i = term.begin();
      term.erase(++i, term.end());
    }
  }
}

// add an OR pattern or OR-NOT pattern, optionally negated (option -N)
void CNF::new_pattern(const char *pattern, bool neg)
{
  if (terms.empty())
    terms.emplace_back();

  if (flag_bool && !neg)
  {
    // --bool --not
    if (flag_not)
    {
      flag_not = false;

      std::string not_pattern;
      not_pattern.assign("-(").append(pattern).append(")");
      compile(not_pattern.c_str());
    }
    else
    {
      // --bool
      compile(pattern);
    }
  }
  else
  {
    Term& term = terms.back();

    // -e PATTERN, -N PATTERN, --and PATTERN, --not PATTERN
    std::string spattern(pattern);

    // -F
    if (flag_fixed_strings)
      quote(spattern);

    // -w and -x
    anchor(spattern);

    // -N PATTERN
    if (neg && !spattern.empty())
      spattern.insert(0, "(?^").append(")");

    // --not
    if (flag_not)
    {
      flag_not = false;

      if (!spattern.empty())
      {
        if (term.empty())
          term.emplace_back();
        else if (term.front() && term.front()->empty())
          return; // empty pattern means anything matches

        term.emplace_back(new std::string(spattern));
      }
    }
    else
    {
      if (term.empty())
        term.emplace_back(new std::string(spattern));
      else if (!term.front())
        term.front() = Pattern(new std::string(spattern));
      else if (term.front()->empty())
        ; // empty pattern means anything matches
      else if (spattern.empty())
        term.front()->clear(); // empty pattern means anything matches
      else
        term.front()->append(flag_basic_regexp ? "\\|" : "|").append(spattern);

      // empty pattern means anything matches
      if (term.front()->empty())
      {
        auto i = term.begin();
        term.erase(++i, term.end());
      }
    }
  }
}

// prune empty OR-terms and OR-terms with empty patterns that match anything
void CNF::prune()
{
  // -x: empty patterns match empty lines
  if (flag_line_regexp)
    return;

  auto s = terms.begin();
  auto e = terms.end();
  auto i = s;

  while (i != e)
  {
    // erase empty term and erase NULL term without NOT-OR terms, unless the first term and -f FILE is specified
    if ((i->empty() || (i->size() == 1 && (!i->front() || i->front()->empty()))) && (i != s || flag_file.empty()))
      terms.erase(i++);
    else
      ++i;
  }
}

// split the patterns at newlines, when present
void CNF::split()
{
  // --bool: spacing means AND
  if (flag_bool)
    return;

  const char *sep = flag_fixed_strings ? "\\E|\\Q" : flag_basic_regexp ? "\\|" : "|";

  for (auto& i : terms)
  {
    for (auto& j : i)
    {
      if (j)
      {
        std::string& pattern = *j;

        size_t from = 0;
        size_t to;

        // split pattern at newlines, for -F add \Q \E to each string, separate by |
        while ((to = pattern.find('\n', from)) != std::string::npos)
        {
          if (from < to)
          {
            size_t len = 1 + (pattern[to - 1] == '\r');
            pattern.replace(to + 1 - len, len, sep);
          }

          from = to + 1;
        }
      }
    }
  }
}

// report the CNF in readable form
void CNF::report(FILE *output) const
{
  if (empty())
    return;

  fprintf(output, "Lines matched if:\n  ");

  if (!flag_file.empty())
  {
    // -f FILE is combined with -e, --and, --andnot, --not

    bool or_sep = false;

    fprintf(output, "a pattern in ");
    for (const auto& filename : flag_file)
    {
      if (or_sep)
        fprintf(output, " or ");

      fprintf(output, "%s", filename.c_str()); 

      or_sep = true;
    }
    fprintf(output, " matches");

    // if the first CNF term is left empty then we match -f FILE with additional constraints, i.e. not as an alternation
    if (terms.front().empty())
      fprintf(output, ", and\n  ");
    else
      fprintf(output, " or ");
  }

  bool and_sep = false;

  for (auto i = terms.begin(); i != terms.end(); ++i)
  {
    if (and_sep)
      fprintf(output, ", and\n  ");

    bool or_sep = false;

    for (auto j = i->begin(); j != i->end(); ++j)
    {
      if (*j)
      {
        if (or_sep)
          fprintf(output, " or ");
        if ((*j)->empty())
          fprintf(output, "anything");
        else
          fprintf(output, "\"%s\"", (*j)->c_str());
        if (j != i->begin())
          fprintf(output, " does not match");
        else
          fprintf(output, " matches");

        or_sep = true;
        and_sep = true;
      }
    }
  }

  fprintf(output, "\n");
}

// return all OR-terms of the CNF joined together
std::string CNF::adjoin() const
{
  std::string adjoined;

  bool allnot = true;

  for (const auto& i : terms)
  {
    if (i.size() <= 1)
    {
      allnot = false;
      break;
    }
  }

  if (!allnot)
  {
    const char *sep = flag_basic_regexp ? "\\|" : "|";

    for (const auto& i : terms)
      if (i.front() && !i.front()->empty())
        adjoined.append(*i.front()).append(sep);

    if (!adjoined.empty())
    {
      // pop unused ending '|' (or BRE '\|')
      adjoined.pop_back();
      if (flag_basic_regexp)
        adjoined.pop_back();
    }
  }

  return adjoined;
}

// return the first OR-terms of the CNF
std::string CNF::first() const
{
  if (!terms.empty() && terms.front().front())
    return *terms.front().front();

  return "";
}
