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
@file      abslexer.h
@brief     RE/flex abstract lexer base class for reflex-generated scanners
@author    Robert van Engelen - engelen@genivia.com
@copyright (c) 2016-2020, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt
*/

#ifndef REFLEX_ABSLEXER_H
#define REFLEX_ABSLEXER_H

#include <reflex/input.h>
#include <reflex/absmatcher.h>
#include <sstream>
#include <stack>

namespace reflex {

/// The abstract lexer class template that is the abstract root class of all reflex-generated scanners.
template<typename M> /// @tparam <M> matcher class derived from reflex::AbstractMatcher
class AbstractLexer {
 public:
  /// Extend matcher class M with a member pointing to the instantiating lexer class.
  class Matcher : public M {
   public:
    /// Construct a lexer matcher from a matcher's pattern type.
    Matcher(
        const typename M::Pattern& pattern,    ///< regex pattern to instantiate matcher class M(pattern, input)
        const Input&               input,      ///< the reflex::Input to instantiate matcher class M(pattern, input)
        AbstractLexer             *lexer,      ///< points to the instantiating lexer class
        const char                *opt = NULL) ///< option string of the form `(A|N|T(=[[:digit:]])?|;)*`
      :
        M(pattern, input, opt),
        lexer_(lexer)
    { }
    /// Construct a lexer matcher from a string pattern.
    Matcher(
        const char    *pattern,    ///< regex pattern to instantiate matcher class M(pattern, input)
        const Input&   input,      ///< the reflex::Input to instantiate matcher class M(pattern, input)
        AbstractLexer *lexer,      ///< points to the instantiating lexer class
        const char    *opt = NULL) ///< option string of the form `(A|N|T(=[[:digit:]])?|;)*`
      :
        M(pattern, input, opt),
        lexer_(lexer)
    { }
    /// Assign a matcher, the underlying pattern object is shared (not deep copied).
    virtual Matcher& operator=(const Matcher& matcher)
    {
      M::operator=(matcher);
      lexer_ = matcher.lexer_;
      return *this;
    }
   protected:
    /// Returns true if matcher should wrap input after EOF (lexer wrap() should return 0 to wrap input after EOF).
    virtual bool wrap()
      /// @returns true if reflex::AbstractLexer::wrap() == 0 indicating that input is wrapped after EOF
    {
      return lexer_->wrap() == 0;
    }
    /// Points to the lexer class that instantiated this Matcher.
    AbstractLexer *lexer_;
  };
  /// Construct abstract lexer to scan an input character sequence and echo the text matches to output.
  AbstractLexer(
      const Input&  input, ///< reflex::Input character sequence to read from
      std::ostream& os)    ///< echo the text matches to this std::ostream or to std::cout
    :
      in_(input),
      os_(&os),
      base_(NULL),
      size_(0),
      matcher_(NULL),
      start_(0),
      debug_(0),
      stack_(),
      state_()
  { }
  /// Delete lexer and its current matcher with its associated input.
  virtual ~AbstractLexer()
  {
    DBGLOG("AbstractLexer::~AbstractLexer()");
    if (matcher_ != NULL)
      delete matcher_;
  }
  /// Set debug flag value.
  virtual void set_debug(int flag) ///< 0 or 1 (false or true)
  {
    debug_ = flag;
  }
  /// Get debug flag value.
  virtual int debug() const
    /// @returns debug flag value
  {
    return debug_;
  }
  /// Dummy performance reporter, to prevent link errors when reflex option -p is omitted.
  void perf_report()
  { }
  /// The default wrap operation at EOF: do not wrap input.
  virtual int wrap()
    /// @returns 1 (override to return 0 to indicate that new input is available after this invocation so that wrap after EOF is OK)
  {
    return 1;
  }
  /// Reset the matcher and start scanning from the given input character sequence I.
  template<typename I>
  inline AbstractLexer& in(const I& input) ///< a character sequence to scan, e.g. reflex::Input, char*, wchar_t*, std::string, std::wstring, FILE*, std::istream
    /// @returns reference to *this
  {
    in_ = input;
    if (has_matcher())
      matcher().input(in_); // reset and assign new input
    return *this;
  }
  /// Reset the matcher and start scanning from the given byte sequence.
  inline AbstractLexer& in(
      const char *b, ///< the byte sequence to scan
      size_t      n) ///< length of the byte sequence to scan
    /// @returns reference to *this
  {
    return in(Input(b, n));
  }
  /// Returns the current input character sequence that is being scanned.
  inline Input& in()
    /// @returns reference to the current reflex::Input object
  {
    if (has_matcher())
      return matcher().in;
    return in_;
  }
  /// Returns the current input character sequence that is being scanned, if none assign stdin.
  inline Input& stdinit()
    /// @returns reference to the current reflex::Input object with input assigned
  {
    if (!in_.assigned() && base_ == NULL)
      in_ = stdin;
    return in_;
  }
  /// Returns the current input character sequence that is being scanned, if none assign std::cin.
  inline Input& nostdinit()
    /// @returns reference to the current reflex::Input object with input assigned
  {
    if (!in_.assigned() && base_ == NULL)
      in_ = std::cin;
    return in_;
  }
  /// Reset the matcher and start scanning the given buffer containing 0-terminated character data (data may be modified).
  inline AbstractLexer& buffer(
      char  *base, ///< base of the buffer containing 0-terminated character data
      size_t size) ///< nonzero size of the buffer
    /// @returns reference to *this
  {
    if (has_matcher())
    {
      matcher().buffer(base, size); // reset and assign new buffer
    }
    else
    {
      base_ = base;
      size_ = size;
    }
    return *this;
  }
  /// Set the current output to the given output stream to echo text matches to.
  inline AbstractLexer& out(std::ostream& os) ///< output stream to echo text matches to
    /// @returns reference to *this
  {
    os_ = &os;
    return *this;
  }
  /// Returns the current output stream used to echo text matches to.
  inline std::ostream& out() const
    /// @returns reference to the current std::ostream object
  {
    return os_ ? *os_ : std::cout;
  }
  /// Returns pointer to the current output stream used to echo text matches to.
  inline std::ostream*& os()
    /// @returns pointer to the current std::ostream object
  {
    return os_;
  }
  /// Returns true if a matcher was assigned to this lexer for scanning.
  inline bool has_matcher() const
    /// @returns true if a matcher was assigned
  {
    return matcher_ != NULL;
  }
  /// Set the matcher (and its current state) for scanning.
  inline AbstractLexer& matcher(Matcher *matcher) ///< points to a matcher object
    /// @returns reference to *this
  {
    matcher_ = matcher;
    if (matcher_ != NULL && base_ != NULL)
    {
      matcher_->buffer(base_, size_);
      base_ = NULL;
      size_ = 0;
    }
    return *this;
  }
  /// Returns a reference to the current matcher.
  inline Matcher& matcher() const
    /// @returns reference to the current matcher
  {
    ASSERT(has_matcher());
    return *matcher_;
  }
  /// Returns a pointer to the current matcher, NULL if none was set.
  inline Matcher *ptr_matcher() const
    /// @returns pointer to the current matcher or NULL if no matcher was set
  {
    return matcher_;
  }
  /// Returns a new copy of the matcher for the given input.
  virtual Matcher *new_matcher(
      const Input& input = Input(), ///< reflex::Input character sequence to match
      const char *opt    = NULL)    ///< options, if any
    /// @returns pointer to new reflex::AbstractLexer::Matcher
  {
    char tabs[4] = "T=n";
    return new Matcher(matcher().pattern(), input, this, opt ? opt : (tabs[2] = matcher().tabs() + '0', tabs));
  }
  /// Delete a matcher.
  void del_matcher(Matcher *matcher)
  {
    if (matcher != NULL)
      delete matcher;
    if (matcher_ == matcher)
      matcher_ = NULL;
  }
  /// Push the current matcher on the stack and use the given matcher for scanning.
  void push_matcher(Matcher *matcher) ///< points to a matcher object
  {
    stack_.push(matcher_);
    matcher_ = matcher;
  }
  /// Pop matcher from the stack and continue scanning where it left off, delete the current matcher.
  bool pop_matcher()
  {
    if (matcher_ != NULL)
      delete matcher_;
    if (!stack_.empty())
    {
      matcher_ = stack_.top();
      stack_.pop();
      return true;
    }
    matcher_ = NULL;
    return false;
  }
  /// Echo the matched text to the current output.
  inline void echo() const
  {
    out().write(matcher().begin(), matcher().size());
  }
  /// Returns 0-terminated pattern match as a char pointer, does not include matched \0s, this is a constant-time operation.
  inline const char *text() const
    /// @returns matched text
  {
    return matcher().text();
  }
#if __cplusplus >= 201703L
  /// Returns the pattern match as a string_view (zero copy), does not include a terminating \0, this is a constant-time operation.
  inline const std::string_view strview() const
    /// @returns string_view with text matched
  {
    return matcher().strview();
  }
#endif
  /// Returns the pattern match as a string, a copy of text(), may include pattern-matched \0s.
  inline std::string str() const
    /// @returns matched text
  {
    return matcher().str();
  }
  /// Returns the pattern match as a wide string, converted from UTF-8 text(), may include pattern-matched \0s.
  inline std::wstring wstr() const
    /// @returns matched text
  {
    return matcher().wstr();
  }
  /// Returns the first 8-bit character of the text matched.
  inline int chr() const
    /// @returns 8-bit char
  {
    return matcher().chr();
  }
  /// Returns the first wide character of the text matched.
  inline int wchr() const
    /// @returns wide char (UTF-8 converted to Unicode)
  {
    return matcher().wchr();
  }
  /// Returns the matched text size in number of bytes.
  inline size_t size() const
    /// @returns size of the matched text
  {
    return matcher().size();
  }
  /// Returns the matched text size in number of (wide) characters.
  inline size_t wsize() const
    /// @returns number of (wide) characters matched
  {
    return matcher().wsize();
  }
  /// Returns the line number of matched text.
  inline size_t lineno() const
    /// @returns line number
  {
    return matcher().lineno();
  }
  /// Set or change the starting line number of the last match.
  inline void lineno(size_t n)
  {
    matcher().lineno(n);
  }
  /// Returns the number of lines that the match spans.
  inline size_t lines() const
    /// @returns number of lines
  {
    return matcher().lines();
  }
  /// Returns the ending line number of matched text.
  inline size_t lineno_end() const
    /// @returns line number
  {
    return matcher().lineno_end();
  }
  /// Returns the starting column number of matched text, taking tab spacing into account and counting wide characters as one character each.
  inline size_t columno() const
    /// @returns column number
  {
    return matcher().columno();
  }
#if WITH_SPAN
  /// Returns the number of bytes from the begin of line of the match.
  inline size_t border() const
    /// @returns border offset
  {
    return matcher().border();
  }
#endif
  /// Returns the number of columns of the last line (or the single line of matched text) in the matched text, taking tab spacing into account and counting wide characters as one character each.
  inline size_t columns() const
    /// @returns number of columns
  {
    return matcher().columns();
  }
  /// Returns the ending column number of matched text, taking tab spacing into account and counting wide characters as one character each.
  inline size_t columno_end() const
    /// @returns column number
  {
    return matcher().columno_end();
  }
  /// Transition to the given start condition state.
  inline AbstractLexer& start(int state) ///< start condition state to transition to
    /// @returns reference to *this
  {
    start_ = state;
    return *this;
  }
  /// Returns the current start condition state.
  inline int start() const
    /// @returns start condition (integer)
  {
    return start_;
  }
  /// Push the current start condition state on the stack and transition to the given start condition state.
  inline void push_state(int state) ///< start condition state to transition to
  {
     state_.push(start_);
     start_ = state;
  }
  /// Pop the stack start condition state and transition to that state.
  inline void pop_state()
  {
    if (!state_.empty())
    {
      start_ = state_.top();
      state_.pop();
    }
  }
  /// Returns the stack top start condition state or 0 (INITIAL) if the stack is empty.
  inline int top_state() const
    /// @returns start condition (integer)
  {
    if (!state_.empty())
      return state_.top();
    return 0;
  }
  /// Returns true if the condition state stack is empty.
  inline bool states_empty() const
    /// @returns true if the stack is empty, false otherwise
  {
    return state_.empty();
  }
  /// Lexer exceptions.
  virtual void lexer_error(const char *message = NULL)
  {
    std::stringstream stream_message;
    stream_message << (message != NULL ? message : "lexer error") << " at " << lineno() << ":" << columno();
    throw std::runtime_error(stream_message.str());
  }
 protected:
  Input                in_;      ///< the input character sequence to scan
  std::ostream        *os_;      ///< the output stream to echo text matches to
  char                *base_;    ///< the buffer to scan in place, if non-NULL
  size_t               size_;    ///< the size of the buffer to scan in place, if nonzero
  Matcher             *matcher_; ///< the matcher used for scanning
  int                  start_;   ///< the current start condition state
  int                  debug_;   ///< 1 if -d (--debug) 0 otherwise:
  std::stack<Matcher*> stack_;   ///< a stack of pointers to matchers
  std::stack<int>      state_;   ///< a stack of start condition states
};

} // namespace reflex

#endif
