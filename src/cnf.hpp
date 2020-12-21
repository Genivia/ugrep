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
@file      cnf.hpp
@brief     CNF class for normalization of Boolean search queries
@author    Robert van Engelen - engelen@genivia.com
@copyright (c) 2019-2020, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt
*/

#ifndef CNF_HPP
#define CNF_HPP

#include "flag.hpp"
#include <cstring>
#include <cctype>
#include <list>
#include <memory>

// normalize Boolean search queries to CNF
class CNF {

 public:

  // a pattern in the CNF is a string or undefined (a NULL smart pointer)
  typedef std::unique_ptr<std::string> Pattern;

  // a term in the CNF is a string/NULL, where the first is an OR pattern (with alternations) or NULL and the rest are OR-NOT alternate patterns
  typedef std::list<Pattern> Term;

  // a CNF is a collection of terms, an AND-list of OR-term lists of (NOT-)patterns
  typedef std::list<Term> Terms;

  // clear the CNF
  void clear()
  {
    terms.clear();
  }

  // return true if CNF has any patterns defined with new_pattern()
  bool defined() const
  {
    return !terms.empty();
  }

  // return true if CNF only defines one empty pattern, after normalization and prune()
  bool empty() const
  {
    return terms.empty() || (terms.size() == 1 && terms.front().empty());
  }

  // return true if CNF is undefined or a singleton with just one pattern w/o OR-NOT patterns, after prune()
  bool singleton_or_undefined() const
  {
    return terms.empty() || (terms.size() == 1 && terms.front().size() == 1 && terms.front().front());
  }

  // return true if the first OR-list term is an empty pattern
  bool first_empty() const
  {
    return !terms.empty() && !terms.front().empty() && terms.front().front() && terms.front().front()->empty();
  }

  // add a new OR-list term to CNF AND-list
  void new_term()
  {
    if (terms.empty())
      terms.emplace_back();

    terms.emplace_back();
  }

  // add an OR pattern or OR-NOT pattern, optionally negated (option -N)
  void new_pattern(const char *pattern, bool neg = false);

  // compile --bool search query into operator tree, normalize to CNF, and populate CNF AND-list of OR-term lists
  void compile(const char *pattern)
  {
    OpTree(pattern, terms);
  }

  // return the CNF AND-list of OR-term lists
  const Terms& lists() const
  {
    return terms;
  }

  // prune empty OR-terms and OR-terms with empty patterns that match anything
  void prune();

  // split the patterns at newlines, when present
  void split();

  // report the CNF in readable form
  void report(FILE *output) const;

  // return all OR-terms of the CNF adjoined
  std::string adjoin() const;

  // return the first OR-terms of the CNF
  std::string first() const;

 protected:

  struct OpTree {

    // a node is either NONE (a leaf node with a regex pattern) or an AND, OR, NOT operation with a list of OpTree operands
    enum Op { NONE, AND, OR, NOT } op;

    OpTree(Op op) : op(op) { }

    // parse a pattern, normalize to CNF, and convert to a CNF AND-list of OR-term lists
    OpTree(const char *pattern, Terms& terms)
    {
      op = AND;
      parse(pattern);
      normalize();
      convert(terms);
    }

    // parse a pattern into an operator tree using a recursive descent parser
    void parse(const char *& pattern);

    // <parse1> -> <parse2> { <space>+ [ 'AND' <space>+ ] <parse2> }*
    void parse1(const char *& pattern);

    // <parse2> -> <parse3> { [ '|'+ | 'OR' <space>+ ] <parse3> }*
    void parse2(const char *& pattern);

    // <parse3> -> [ '-' <space>* | 'NOT' <space>+ ] <parse4>
    // <parse4> -> '(' <parse1> ')' | <pattern>
    void parse3(const char *& pattern);

    // normalize operator tree to CNF
    void normalize(bool invert = false);

    // convert CNF-normalized operator tree to terms, a CNF AND-list of OR-term lists
    void convert(Terms& terms);

    // add a [NOT] term of the operator tree to terms
    void add_to(Terms& terms) const;

    // skip space
    static void skip_space(const char *& pattern)
    {
      while (isspace(*pattern))
        ++pattern;
    }

    // return true if the pattern pointer starts with an AND, OR, or NOT operator and skip over it
    static bool is_oper(Op op, const char *& pattern)
    {
      switch (op)
      {
        case AND:
          if (strncmp(pattern, "AND", 3) != 0 || !isspace(pattern[3]))
            break;
          pattern += 3;
          return true;

        case OR:
          if (strncmp(pattern, "OR", 2) != 0 || !isspace(pattern[2]))
            break;
          pattern += 2;
          return true;

        case NOT:
          if (strncmp(pattern, "NOT", 3) != 0 || !isspace(pattern[3]))
            break;
          pattern += 3;
          return true;

        default:
          break;
      }

      return false;
    }

    // return true if look ahead for a | or OR then return true when found and skip over it, otherwise return false
    static bool is_alternation(const char *& pattern)
    {
      const char *p = pattern;

      skip_space(p);

      if (*p != '|' && !is_oper(OR, p))
        return false;

      while (*p == '|')
        ++p;

      skip_space(p);

      pattern = p;

      return true;
    }

    std::string       regex; // lead node
    std::list<OpTree> list;  // list of OpTree operands

  };

  // quote a pattern with \Q and \E
  static void quote(std::string& pattern)
  {
    // when empty then nothing to quote
    if (pattern.empty())
      return;

    size_t from = 0;
    size_t to;

    // replace each \E in the pattern with \E\\E\Q
    while ((to = pattern.find("\\E", from)) != std::string::npos)
    {
      pattern.insert(to + 2, "\\\\E\\Q");
      from = to + 7;
    }

    // enclose in \Q and \E
    pattern.insert(0, "\\Q").append("\\E");
  }

  // anchor a pattern, when specified with -w or -x
  static void anchor(std::string& pattern)
  {
    // -G requires \( \) instead of ( )
    const char *xleft = flag_basic_regexp ? "^\\(" : "^(";
    const char *xright = flag_basic_regexp ? "\\)$" : ")$";
    const char *wleft = flag_basic_regexp ? "\\<\\(" : "\\<(";
    const char *wright = flag_basic_regexp ? "\\)\\>" : ")\\>";

    // patterns that start with ^ or end with $ are already anchored
    if (!pattern.empty() && (pattern.front() == '^' || pattern.back() == '$'))
    {
      if (!flag_line_regexp && flag_word_regexp)
      {
        if (pattern.front() != '^')
          pattern.insert(0, wleft);
        else if (pattern.back() != '$')
          pattern.append(wright);
      }

      // enable -Y to match empty
      flag_empty = true;
    }
    else if (flag_line_regexp)
    {
      if (!pattern.empty())
        pattern.insert(0, xleft).append(xright);
      else
        pattern.assign("^$");
    }
    else if (flag_word_regexp)
    {
      if (!pattern.empty())
        pattern.insert(0, wleft).append(wright);
    }
  }

  // CNF terms, an AND-list of OR-term lists of string/NULL patterns
  Terms terms;

};

#endif
