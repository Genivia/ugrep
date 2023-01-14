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
@file      flexlexer.h
@brief     RE/flex Flex-compatible FlexLexer base class and Flex-compatible macros
@author    Robert van Engelen - engelen@genivia.com
@copyright (c) 2016-2020, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt
*/

#ifndef REFLEX_FLEXLEXER_H
#define REFLEX_FLEXLEXER_H

#include <reflex/abslexer.h>

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Flex-compatibility definitions (redefinable)                              //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

/// Flex-compatible user-definable macro.
#ifndef YY_USER_INIT
#define YY_USER_INIT
#endif

/// Flex-compatible user-definable macro.
#ifndef YY_USER_ACTION
#define YY_USER_ACTION
#endif

/// Flex-compatible user-definable macro.
#ifndef YY_BREAK
#define YY_BREAK                break;
#endif

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Flex-compatibility definitions (built-in)                                 //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

/// Flex and Bison-compatible `%%option bison` generates a global `YY_SCANNER` scanner object, otherwise we use *this.
#if defined(REFLEX_OPTION_reentrant) || defined(REFLEX_OPTION_bison_cc) || defined(REFLEX_OPTION_bison_bridge) || (!defined(REFLEX_OPTION_bison) && !defined(REFLEX_OPTION_bison_locations) && !defined(YY_SCANNER))
#define YY_SCANNER              (*this)
#define YY_SCANNER_DOT_
#else
#define YY_SCANNER_DOT_         YY_SCANNER.
#endif

#if (defined(REFLEX_OPTION_yywrap) || defined(REFLEX_OPTION_bison)) && !defined(REFLEX_OPTION_noyywrap)
#if defined(REFLEX_OPTION_reentrant)

/// Flex-compatible global yywrap() is enabled with `%%option yywrap` or `%%option bison`.
#if defined(YY_EXTERN_C)
YY_EXTERN_C int yywrap(yyscan_t);
#else
int yywrap(yyscan_t);
#endif

#else

/// Flex-compatible global yywrap() is enabled with `%%option yywrap` or `%%option bison`.
#if defined(YY_EXTERN_C)
YY_EXTERN_C int yywrap(void);
#else
int yywrap(void);
#endif

#endif
#endif

/// Flex-compatible macro: the token value returned upon end-of-file.
#ifndef YY_NULL
#ifdef REFLEX_OPTION_token_eof
#define YY_NULL                 REFLEX_OPTION_token_eof
#else
#ifdef REFLEX_OPTION_token_type
#define YY_NULL                 REFLEX_OPTION_token_type()
#else
#define YY_NULL                 (0)
#endif
#endif
#endif

/// Flex-compatible macro: exit error code
#ifndef YY_EXIT_FAILURE
#define YY_EXIT_FAILURE         (2)
#endif

/// Flex-compatible macro: size of default input buffer (not applicable to RE/flex, buffer is dynamic).
#ifndef YY_BUF_SIZE
#define YY_BUF_SIZE             (16384)
#endif

/// Flex-compatible macro: the type of a state variable.
#ifndef yy_state_type
#define yy_state_type           int
#endif

/// Flex-compatible macro: yy_size_t type of yyleng.
#ifndef yy_size_t
#define yy_size_t               size_t
#endif

/// Flex-compatible macro: the type of the scanner buffer.
#define yy_buffer_state         FlexLexer::AbstractBaseLexer::Matcher

/// Flex-compatible macro: a pointer to the type of the scanner buffer.
#define YY_BUFFER_STATE         yy_buffer_state*

/// Flex-compatible macro: ECHO action to output the content of yytext.
#ifndef ECHO
#define ECHO                    YY_SCANNER_DOT_ LexerOutput(yytext, yyleng)
#endif

/// Flex-compatible macro: BEGIN action to set a start condition.
#define BEGIN                   YY_SCANNER_DOT_ start_ =

/// Flex-compatible macro: the current start condition.
#define YYSTATE                 YY_SCANNER_DOT_ start()

/// Flex-compatible macro: the current start condition.
#define YY_START                YY_SCANNER_DOT_ start()

/// Flex-compatible macro: the current start condition.
#define yy_current_state        YY_SCANNER_DOT_ start()

/// Flex-compatible macro: the current matcher buffer, NULL when scanner has not started.
#define YY_CURRENT_BUFFER       YY_SCANNER_DOT_ ptr_matcher()

/// Flex-compatible macro: the buffer flush action
#define YY_FLUSH_BUFFER         (YY_SCANNER_DOT_ has_matcher() ? YY_SCANNER_DOT_ matcher().flush() : (void)0)

/// Flex-compatible macro: the matched text.
#define yytext                  const_cast<char*>(YY_SCANNER_DOT_ YYText())

/// Flex-compatible macro: the matched text length.
#define yyleng                  static_cast<yy_size_t>(YY_SCANNER_DOT_ YYLeng())

/// Flex-compatible macro: the line number of the matched text.
#define yylineno                static_cast<int>(YY_SCANNER_DOT_ matcher().lineno())

/// Flex-compatible macro: the column number of the matched text.
#define yycolumno               static_cast<int>(YY_SCANNER_DOT_ matcher().columno())

/// Flex-compatible macro: the matched action index (use only when scanner has started).
#define yy_act                  YY_SCANNER_DOT_ matcher().accept()

/// Flex-compatible macro: the next action index (use only when scanner has started).
#define yylook()                YY_SCANNER_DOT_ matcher().scan()

/// Flex-compatible macro: the current input, a reference to a reflex::Input object that can be a stream, file, or string.
#define yyin                    YY_SCANNER_DOT_ in()

/// Flex-compatible macro: the current output stream, points to std::ostream or NULL.
#define yyout                   YY_SCANNER_DOT_ os()

/// Flex-compatible macro: at-begin-of-line check (use only when scanner has started).
#define YY_AT_BOL()             YY_SCANNER_DOT_ matcher().at_bol()

/// Flex-compatible macro: a begin-of-line forced set (use only when scanner has started).
#define yy_set_bol(b)           YY_SCANNER_DOT_ matcher().set_bol(b)

/// Flex-compatible macro: interactive mode on/off (use only when scanner has started).
#define yy_set_interactive(b)   YY_SCANNER_DOT_ matcher().buffer((b) ? 1 : 0)

/// Flex-compatible macro: create and return a new buffer (new reflex::Matcher).
#if defined(REFLEX_OPTION_reentrant)
#define yy_create_buffer(i,_,s) static_cast<FlexLexer*>(s)->new_matcher(i)
#else
#define yy_create_buffer(i,_)   YY_SCANNER_DOT_ new_matcher(i)
#endif

/// Flex-compatible macro: create and return a new buffer (new reflex::Matcher).
#if defined(REFLEX_OPTION_reentrant)
#define yy_new_buffer(i,_,s)    static_cast<FlexLexer*>(s)->new_matcher(i)
#else
#define yy_new_buffer(i,_)      YY_SCANNER_DOT_ new_matcher(i)
#endif

/// Flex-compatible macro: delete a buffer.
#if defined(REFLEX_OPTION_reentrant)
#define yy_delete_buffer(b,s)   static_cast<FlexLexer*>(s)->del_matcher(b)
#else
#define yy_delete_buffer(b)     YY_SCANNER_DOT_ del_matcher(b)
#endif

/// Flex-compatible macro: flush a buffer.
#if defined(REFLEX_OPTION_reentrant)
#define yy_flush_buffer(b,s)    ((b) ? (b)->flush() : (void)0)
#else
#define yy_flush_buffer(b)      ((b) ? (b)->flush() : (void)0)
#endif

/// Flex-compatible macro: push the current buffer on the stack to use the given buffer.
#if defined(REFLEX_OPTION_reentrant)
#define yypush_buffer_state(b,s) static_cast<FlexLexer*>(s)->push_matcher(b)
#else
#define yypush_buffer_state(b)  YY_SCANNER_DOT_ push_matcher(b)
#endif

/// Flex-compatible macro: pop buffer from the stack and delete the current buffer.
#if defined(REFLEX_OPTION_reentrant)
#define yypop_buffer_state(s)   static_cast<FlexLexer*>(s)->pop_matcher()
#else
#define yypop_buffer_state()    YY_SCANNER_DOT_ pop_matcher()
#endif

/// Flex-compatible macro: switch to another buffer.
#if defined(REFLEX_OPTION_reentrant)
#define yy_switch_to_buffer(b,s) static_cast<FlexLexer*>(s)->matcher(b)
#else
#define yy_switch_to_buffer(b)  YY_SCANNER_DOT_ matcher(b)
#endif

/// Flex-compatible macro: restart from the given input source.
#if defined(REFLEX_OPTION_reentrant)
#define yyrestart(i,s)          static_cast<FlexLexer*>(s)->in(i)
#else
#define yyrestart(i)            YY_SCANNER_DOT_ in(i)
#endif

/// Flex-compatible macro: scan a string
#if defined(REFLEX_OPTION_reentrant)
#define yy_scan_string(i,s)     (static_cast<FlexLexer*>(s)->in(i), static_cast<YY_BUFFER_STATE>(NULL))
#define yy_scan_wstring(i,s)    yy_scan_string((i), (s))
#define yy_scan_bytes(b,n,s)    yy_scan_string(reflex::Input((b), static_cast<size_t>(n)), (s))
#define yy_scan_buffer(b,n,s)   (static_cast<FlexLexer*>(s)->buffer((b), (n)-1), static_cast<YY_BUFFER_STATE>(NULL))
#else
#define yy_scan_string(i)       (YY_SCANNER_DOT_ in(i), static_cast<YY_BUFFER_STATE>(NULL))
#define yy_scan_wstring(i)      yy_scan_string(i)
#define yy_scan_bytes(b,n)      yy_scan_string(reflex::Input((b), static_cast<size_t>(n)))
#define yy_scan_buffer(b,n)     (YY_SCANNER_DOT_ buffer((b), (n)-1), static_cast<YY_BUFFER_STATE>(NULL))
#endif

/// Flex-compatible macro: the terminating action.
#ifndef yyterminate
#define yyterminate()           return YY_NULL
#endif

/// Flex-compatible macro: read one 8-bit character, returns zero when EOF.
#if defined(REFLEX_OPTION_reentrant)
#define yyinput(s)              static_cast<FlexLexer*>(s)->input()
#else
#define yyinput()               YY_SCANNER_DOT_ input()
#endif

/// Flex-compatible macro: read one wide character, returns zero when EOF.
#if defined(REFLEX_OPTION_reentrant)
#define yywinput(s)             static_cast<FlexLexer*>(s)->winput()
#else
#define yywinput()              YY_SCANNER_DOT_ winput()
#endif

/// Flex-compatible macro: put one 8-bit character back onto the input stream to be read again.
#if defined(REFLEX_OPTION_reentrant)
#define yyunput(c,s)            static_cast<FlexLexer*>(s)->unput(c)
#else
#define yyunput(c)              YY_SCANNER_DOT_ unput(c)
#endif

/// Flex-compatible macro: put one wide character back onto the input stream to be read again.
#if defined(REFLEX_OPTION_reentrant)
#define yywunput(c,s)           static_cast<FlexLexer*>(s)->wunput(c)
#else
#define yywunput(c)             YY_SCANNER_DOT_ wunput(c)
#endif

/// Flex-compatible macro: output one 8-bit character.
#if defined(REFLEX_OPTION_reentrant)
#define yyoutput(c,s)           static_cast<FlexLexer*>(s)->output(c)
#else
#define yyoutput(c)             YY_SCANNER_DOT_ output(c)
#endif

/// Flex-compatible macro: woutput one wide character.
#if defined(REFLEX_OPTION_reentrant)
#define yywoutput(c,s)          static_cast<FlexLexer*>(s)->woutput(c)
#else
#define yywoutput(c)            YY_SCANNER_DOT_ woutput(c)
#endif

/// Flex-compatible macro: push the current start condition state on the stack and transition to the given state.
#if defined(REFLEX_OPTION_reentrant)
#define yy_push_state(n,s)      static_cast<FlexLexer*>(s)->push_state(n)
#else
#define yy_push_state(n)        YY_SCANNER_DOT_ push_state(n)
#endif

/// Flex-compatible macro: pop the stack start condition state and transition to that state.
#if defined(REFLEX_OPTION_reentrant)
#define yy_pop_state(s)         static_cast<FlexLexer*>(s)->pop_state()
#else
#define yy_pop_state()          YY_SCANNER_DOT_ pop_state()
#endif

/// Flex-compatible macro: returns the stack top start condition state.
#if defined(REFLEX_OPTION_reentrant)
#define yy_top_state(s)         static_cast<FlexLexer*>(s)->top_state()
#else
#define yy_top_state()          YY_SCANNER_DOT_ top_state()
#endif

/// Flex-compatible macro: append the next matched text to the currently matched text (use only when scanner has started).
#define yymore()                YY_SCANNER_DOT_ matcher().more()

/// Flex-compatible macro: truncate the yytext length of the match to n characters in length and reposition for next match (use only when scanner has started).
#define yyless(n)               YY_SCANNER_DOT_ matcher().less(n)

/// Flex-compatible macro: the debug flag.
#define yy_flex_debug           YY_SCANNER_DOT_ debug_;

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Flex-compatibility reentrant definitions                                  //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#if defined(REFLEX_OPTION_reentrant) || defined(REFLEX_OPTION_bison_bridge)

/// Flex-compatible macro: the extra type for reentrant scanner.
#if defined(REFLEX_OPTION_extra_type)
#define YY_EXTRA_TYPE           REFLEX_OPTION_extra_type
#else
#define YY_EXTRA_TYPE           void*
#endif

/// Flex-compatible macro: reentrant use of yyscanner.
#define yyscanner               this

/// Flex-compatible macro: the text accessor of a reentrant scanner.
#define yyget_text(s)           const_cast<char*>(static_cast<FlexLexer*>(s)->YYText())

/// Flex-compatible macro: the leng accessor of a reentrant scanner.
#define yyget_leng(s)           static_cast<FlexLexer*>(s)->YYLeng()

/// Flex-compatible macro: the lineno accessor of a reentrant scanner.
#define yyget_lineno(s)         static_cast<int>(static_cast<FlexLexer*>(s)->lineno())

/// Flex-compatible macro: set the lineno accessor of a reentrant scanner.
#define yyset_lineno(n,s)       static_cast<FlexLexer*>(s)->lineno(n)

/// Flex-compatible macro: set the column accessor of a reentrant scanner.
#define yyset_columno(n,s)      static_cast<FlexLexer*>(s)->columno(n)

/// Flex-compatible macro: the reflex::Input in accessor of a reentrant scanner.
#define yyget_in(s)             static_cast<FlexLexer*>(s)->in()

/// Flex-compatible macro: set the reflex::Input in accessor of a reentrant scanner.
#define yyset_in(i,s)           static_cast<FlexLexer*>(s)->in(i)

/// Flex-compatible macro: the std::ostream* out accessor of a reentrant scanner.
#define yyget_out(s)            static_cast<FlexLexer*>(s)->os()

/// Flex-compatible macro: set the std::ostream* out accessor of a reentrant scanner.
#define yyset_out(o,s)          (static_cast<FlexLexer*>(s)->os() = o)

/// Flex-compatible macro: the debug accessor of a reentrant scanner.
#define yyget_debug(s)          static_cast<FlexLexer*>(s)->debug()

/// Flex-compatible macro: set the debug accessor of a reentrant scanner.
#define yyset_debug(f,s)        static_cast<FlexLexer*>(s)->set_debug(f)

/// Flex-compatible macro: the yyextra accessor of a reentrant scanner.
#define yyget_extra(s)          static_cast<FlexLexer*>(s)->yyextra

/// Flex-compatible macro: set the yyextra accessor of a reentrant scanner.
#define yyset_extra(x,s)        (static_cast<FlexLexer*>(s)->yyextra = x)

/// Flex-compatible macro: the YY_CURRENT_BUFFER of a reentrant scanner.
#define yyget_current_buffer(s) static_cast<FlexLexer*>(s)->ptr_matcher()

#endif

namespace reflex {

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  FlexLexer abstract base class template                                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

/// Flex-compatible FlexLexer abstract base class template derived from reflex::AbstractMatcher for the reflex-generated yyFlexLexer scanner class.
template<typename M> /// @tparam <M> matcher class derived from reflex::AbstractMatcher
class FlexLexer : public AbstractLexer<M> {
 public:
  /// Typedef for the abstract base class
  typedef AbstractLexer<M> AbstractBaseLexer;
  /// Extends reflex::AbstractLexer<M>::Matcher for Flex-compatibility.
  class Matcher : public AbstractBaseLexer::Matcher {
   public:
    /// Construct a FlexLexer matcher from a matcher's pattern type.
    Matcher(
        const typename M::Pattern& pattern, ///< regex pattern to instantiate matcher class M(pattern, input)
        const Input&               input,   ///< the reflex::Input to instantiate matcher class M(pattern, input)
        FlexLexer                 *lexer,   ///< points to the instantiating lexer class
        const char                *opt = NULL) ///< option string of the form `(A|N|T(=[[:digit:]])?|;)*`
      :
        AbstractBaseLexer::Matcher(pattern, input, lexer, opt)
    { }
    /// Construct a FlexLexer matcher from a string pattern.
    Matcher(
        const char    *pattern,    ///< regex pattern to instantiate matcher class M(pattern, input)
        const Input&   input,      ///< the reflex::Input to instantiate matcher class M(pattern, input)
        FlexLexer     *lexer,      ///< points to the instantiating lexer class
        const char    *opt = NULL) ///< option string of the form `(A|N|T(=[[:digit:]])?|;)*`
      :
        AbstractBaseLexer::Matcher(pattern, input, lexer, opt)
    { }
    /// Read character sequences via a Flex-compatible virtual FlexLexer::LexerInput method.
    virtual size_t get(
        char  *s,
        size_t n)
    {
      FlexLexer *lexer = dynamic_cast<FlexLexer*>(this->lexer_); // a safe cast
      size_t k = lexer->LexerInput(s, n); // a nice little trick to get input from LexerInput()
      return k < n ? k : n; // make sure the return value is limited to n, as it should be, even when LexerInput misbehaves!
    }
   protected:
    /// Check the Flex-compatible FlexLexer::yywrap method to determine if matcher should wrap input after EOF (lexer yywrap() should return 0 to wrap input after EOF).
    virtual bool wrap()
      /// @returns true if FlexLexer::yywrap() == 0 indicating that input is wrapped after EOF
    {
      FlexLexer *lexer = dynamic_cast<FlexLexer*>(this->lexer_); // a safe cast
      return lexer->yywrap() == 0;
    }
  };
  /// Construct Flex-compatible lexer to read from a std::istream (and echo the text matches to a std::ostream).
  FlexLexer(
      const Input&  input, ///< input to read from
      std::ostream *os)    ///< echo the text matches to this std::ostream (std::cout by default)
    :
      AbstractBaseLexer(input, os ? *os : std::cout)
  { }
  /// The matched text.
  inline const char *YYText() const
    /// @returns NUL-terminated string.
  {
    return this->matcher().text();
  }
  /// The matched text length.
  inline int YYLeng() const
    /// @returns matched text length.
  {
    return static_cast<int>(this->matcher().size());
  }
  /// Read one 8-bit character, returns zero when EOF.
  inline int input()
    /// @returns the character read.
  {
    int c = this->matcher().input();
    return c == EOF ? 0 : c;
  }
  /// Read one wide character, returns zero when EOF.
  inline int winput()
    /// @returns the character read.
  {
    int c = this->matcher().winput();
    return c == EOF ? 0 : c;
  }
  /// Put back one character on the input character sequence for matching, invalidating the current match info and text.
  inline void unput(char c) ///< 8-bit character to put back
  {
    this->matcher().unput(c);
  }
  /// Put back one wide character on the input character sequence for matching, invalidating the current match info and text.
  inline void unput(int c) ///< wide character to put back
  {
    this->matcher().wunput(c);
  }
  /// Output one character.
  inline void output(char c) ///< 8-bit char to output via LexerOutput
  {
    this->LexerOutput(&c, 1);
  }
  /// Output one wide character.
  inline void woutput(int c) ///< wide char to output via LexerOutput
  {
    char buf[8];
    size_t n = utf8(c, buf);
    this->LexerOutput(buf, n);
  }
  /// Switch input and output streams.
  virtual void switch_streams(
      const Input&  input = Input(), ///< new input to read from
      std::ostream *os    = NULL)    ///< echo the text matches to this std::ostream or std::cout by default
  {
    if (input.assigned())
      this->in(input);
    if (os)
      this->out(*os);
  }
  /// Default yywrap operation at EOF: do not wrap input.
  virtual int yywrap()
    /// @returns 1 (0 indicates that new input was set and wrap after EOF is OK).
  {
#if (defined(REFLEX_OPTION_yywrap) || defined(REFLEX_OPTION_bison)) && !defined(REFLEX_OPTION_noyywrap)
#if defined(REFLEX_OPTION_reentrant)
    return ::yywrap(static_cast<yyscan_t>(this));
#else
    return ::yywrap();
#endif
#else
    return 1;
#endif
  }
  /// Pure virtual lexer (implemented by a reflex-generated yyFlexLexer).
#ifdef REFLEX_OPTION_token_type
  virtual REFLEX_OPTION_token_type yylex() = 0;
#else
  virtual int yylex() = 0;
#endif
#if defined(REFLEX_OPTION_reentrant) || defined(REFLEX_OPTION_bison_bridge)
  YY_EXTRA_TYPE yyextra;       ///< Flex-compatible reentrant YY_EXTRA_TYPE yyextra
#endif
  /// Invoked by FlexLexer::Matcher to read input character sequence.
  virtual size_t LexerInput(
      char  *s, ///< points to the string buffer to fill with input
      size_t n) ///< size of buffer pointed to by s
    /// @returns the nonzero number of (less or equal to n) 8-bit characters added to buffer s from the current input, or zero when EOF.
  {
    AbstractMatcher *matcher = this->ptr_matcher();
    if (matcher != NULL)
      return matcher->in.get(s, n);
    return 0;
  }
  /// Invoked by ECHO and FlexLexer::output.
  virtual void LexerOutput(
      const char *s, ///< points to text to output
      size_t      n) ///< length of text to output
  {
    this->out().write(s, static_cast<std::streamsize>(n));
  }
  /// Invoked by reflex-generated yyFlexLexer when an error occurs.
  virtual void LexerError(const char *s) ///< error message
  {
    std::cerr << s << std::endl;
    exit(YY_EXIT_FAILURE);
  }
};

} // namespace reflex

#endif
