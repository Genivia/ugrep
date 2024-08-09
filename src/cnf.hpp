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
@copyright (c) 2019,2024, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt
*/

#ifndef CNF_HPP
#define CNF_HPP

#include "flag.hpp"
#include <cstring>
#include <cctype>
#include <list>
#include <memory>

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

// normalize Boolean search queries to CNF
class CNF {

 public:

  // a pattern in the CNF is a string or undefined (a NULL smart pointer)
  typedef std::unique_ptr<std::string> Pattern;

  // a term in the CNF is a string/NULL, where the first is an OR pattern (with alternations) or NULL and the rest are ALT-NOT alternate patterns
  typedef std::list<Pattern> Term;

  // a CNF is a collection of terms, an AND-list of ALT-term lists of (NOT-)patterns
  typedef std::list<Term> Terms;

  // pattern mask values to indicate the type of regex pattern argument to populate CNF
  struct PATTERN {
    enum {
      ALT = 0, // -e PATTERN
      NEG = 1, // -N PATTERN
      NOT = 2, // --not [-e] PATTERN
      AND = 4, // to create a new AND-term with empty ALT-list in the CNF
    };
    PATTERN(int mask) : mask(mask) { }
    operator int() const { return mask; }
    int mask;
  };

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

  // return true if CNF is undefined or a singleton with just one pattern w/o ALT-NOT patterns, after prune()
  bool singleton_or_undefined() const
  {
    return terms.empty() || (terms.size() == 1 && terms.front().size() == 1 && terms.front().front());
  }

  // return true if the first ALT-list term is an empty pattern
  bool first_empty() const
  {
    return !terms.empty() && !terms.front().empty() && terms.front().front() && terms.front().front()->empty();
  }

  // add a new ALT-list term to CNF AND-list
  void new_term()
  {
    if (terms.empty())
      terms.emplace_back();

    terms.emplace_back();
  }

  // add an ALT pattern or ALT-NOT pattern, optionally negated (option -N)
  void new_pattern(PATTERN mask, const char *pattern);

  // compile --bool search query into operator tree, normalize to CNF, and populate CNF AND-list of ALT-term lists
  void compile(const char *pattern)
  {
    OpTree(pattern, terms);
  }

  // return the CNF AND-list of ALT-term lists
  const Terms& lists() const
  {
    return terms;
  }

  // prune empty ALT-terms and ALT-terms with empty patterns that match anything
  void prune();

  // split the patterns at newlines, when present
  void split();

  // report the CNF in readable form
  void report(FILE *output) const;

  // return all ALT-terms of the CNF adjoined
  std::string adjoin() const;

  // return the first ALT-terms of the CNF
  std::string first() const;

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
    // patterns that start with ^ or end with $ are already anchored
    if (pattern.empty())
    {
      // -x: empty regex matches empty lines with ^$
      if (flag_line_regexp)
        pattern.assign("^$");
    }
    else if (flag_line_regexp)
    {
      // -x: make the regex line-anchored
      // -G requires \( \) instead of ( )
      const char *xleft = flag_basic_regexp ? "^\\(" : "^(?:";
      const char *xright = flag_basic_regexp ? "\\)$" : ")$";
      pattern.insert(0, xleft).append(xright);
    }
    else if (flag_word_regexp)
    {
      // -w: make the regex word-anchored (or implicitly done with matcher option W instead of \< and \>)
      if (flag_perl_regexp)
      {
        // -P requires (?<!\w) (?!\w) instead of \< and \>
#if defined(HAVE_PCRE2)
        // PCRE2_EXTRA_MATCH_WORD does not work and \b(?:regex)\b is not correct anyway, so we roll out our own
        const char *wleft = pattern.front() != '^' ? "(?<!\\w)(?:" : "(?:";
        const char *wright = pattern.back() != '$' ? ")(?!\\w)" : ")";
#else // Boost.Regex
        const char *wleft = pattern.front() != '^' ? "(?<![[:word:]])(?:" : "(?:";
        const char *wright = pattern.back() != '$' ? ")(?![[:word:]])" : ")";
#endif
        pattern.insert(0, wleft).append(wright);
      }
    }
    else if (pattern.front() == '^' || pattern.back() == '$')
    {
      // enable -Y to match empty
      flag_empty = true;
    }
  }

 protected:

  struct OpTree {

    // a node is either NONE (a leaf node with a regex pattern) or an AND, OR, NOT operation with a list of OpTree operands
    enum Op { NONE, AND, OR, NOT } op;

    OpTree(Op op) : op(op) { }

    // parse a pattern, normalize to CNF, and convert to a CNF AND-list of ALT-term lists
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

    // convert CNF-normalized operator tree to terms, a CNF AND-list of ALT-term lists
    void convert(Terms& terms);

    // add a [NOT] term of the operator tree to terms
    void add_to(Terms& terms) const;

    // skip space
    static void skip_space(const char *& pattern)
    {
      while (*pattern != '\n' && isspace(static_cast<unsigned char>(*pattern)))
        ++pattern;
    }

    // return true if the pattern pointer starts with an AND, OR, or NOT operator and skip over it
    static bool is_oper(Op op, const char *& pattern)
    {
      switch (op)
      {
        case AND:
          if (strncmp(pattern, "AND", 3) != 0 || !isspace(static_cast<unsigned char>(pattern[3])))
            break;
          pattern += 3;
          return true;

        case OR:
          if (strncmp(pattern, "OR", 2) != 0 || !isspace(static_cast<unsigned char>(pattern[2])))
            break;
          pattern += 2;
          return true;

        case NOT:
          if (strncmp(pattern, "NOT", 3) != 0 || !isspace(static_cast<unsigned char>(pattern[3])))
            break;
          pattern += 3;
          return true;

        default:
          break;
      }

      return false;
    }

    // return true if at a |, \n or OR then return true when found and skip over it, otherwise return false
    static bool is_alternation(const char *& pattern)
    {
      const char *lookahead = pattern;

      skip_space(lookahead);

      if (*lookahead == '\n')
        ++lookahead;
      else if (*lookahead == '|')
        while (*++lookahead == '|')
          continue;
      else if (!is_oper(OR, lookahead))
        return false;

      skip_space(lookahead);

      pattern = lookahead;

      return true;
    }

    std::string       regex; // lead node
    std::list<OpTree> list;  // list of OpTree operands

  };

  // CNF terms, an AND-list of ALT-term lists of string/NULL patterns
  Terms terms;

};

#endif
