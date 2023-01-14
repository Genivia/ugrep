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
@file      absmatcher.h
@brief     RE/flex abstract matcher base class and pattern matcher class
@author    Robert van Engelen - engelen@genivia.com
@copyright (c) 2016-2022, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt
*/

#ifndef REFLEX_ABSMATCHER_H
#define REFLEX_ABSMATCHER_H

/// This compile-time option may speed up buffer reallocation with realloc() instead of new and delete.
#ifndef WITH_REALLOC
#define WITH_REALLOC 1
#endif

/// This compile-time option speeds up matching, but slows input().
#ifndef WITH_FAST_GET
#define WITH_FAST_GET 1
#endif

/// This compile-time option adds span(), line(), wline(), bol(), eol()
#ifndef WITH_SPAN
#define WITH_SPAN 1
#endif

#include <reflex/convert.h>
#include <reflex/debug.h>
#include <reflex/input.h>
#include <reflex/traits.h>
#include <reflex/simd.h>
#include <cstdlib>
#include <cctype>
#include <iterator>

namespace reflex {

/// Check ASCII word-like character `[A-Za-z0-9_]`, permitting the character range 0..303 (0x12F) and EOF.
inline int isword(int c) ///< Character to check
  /// @returns nonzero if argument c is in `[A-Za-z0-9_]`, zero otherwise
{
  return std::isalnum(static_cast<unsigned char>(c)) | (c == '_');
}

/// The abstract matcher base class template defines an interface for all pattern matcher engines.
/**
The buffer expands when matches do not fit.  The buffer size is initially BUFSZ.

```
      _________________
     |  |    |    |    |
buf_=|  |text|rest|free|
     |__|____|____|____|
        ^    ^    ^    ^
        cur_ pos_ end_ max_

buf_ // points to buffered input, buffer may grow to fit long matches
cur_ // current position in buf_ while matching text, cur_ = pos_ afterwards, can be changed by more()
pos_ // position in buf_ to start the next match
end_ // position in buf_ that is free to fill with more input
max_ // allocated size of buf_, must ensure that max_ > end_ for text() to add a final \0
txt_ // points to the match, will be 0-terminated when text() or rest() are called
len_ // length of the match
chr_ // char located at txt_[len_] when txt_[len_] is set to \0 by text(), is \0 otherwise
got_ // buf_[cur_-1] or txt_[-1] character before this match (assigned before each match), initially Const::BOB
eof_ // true if no more data can/should be fetched to fill the buffer
```
*/
class AbstractMatcher {
 protected:
  typedef int Method; ///< a method is one of Const::SCAN, Const::FIND, Const::SPLIT, Const::MATCH
 public:
  /// AbstractMatcher::Const common constants.
  struct Const {
    static const Method SCAN  = 0;          ///< AbstractMatcher::match method is to scan input (tokenizer)
    static const Method FIND  = 1;          ///< AbstractMatcher::match method is to find pattern in input
    static const Method SPLIT = 2;          ///< AbstractMatcher::match method is to split input at pattern matches
    static const Method MATCH = 3;          ///< AbstractMatcher::match method is to match the entire input
    static const int NUL      = '\0';       ///< NUL string terminator
    static const int UNK      = 256;        ///< unknown/undefined character meta-char marker
    static const int BOB      = 257;        ///< begin of buffer meta-char marker
    static const int EOB      = EOF;        ///< end of buffer meta-char marker
    static const size_t BLOCK = 4096;       ///< minimum remaining unused space in the buffer, to prevent excessive shifting
#ifndef REFLEX_BUFSZ
    static const size_t BUFSZ = (64*1024);  ///< initial buffer size, at least 4096 bytes
#else
    static const size_t BUFSZ = REFLEX_BUFSZ;
#endif
#ifndef REFLEX_BOLSZ
    static const size_t BOLSZ = (3*BUFSZ);  ///< max begin of line size till match to retain in memory by growing the buffer
#else
    static const size_t BOLSZ = REFLEX_BOLSZ;
#endif
    static const size_t REDO  = 0x7FFFFFFF; ///< reflex::Matcher::accept() returns "redo" with reflex::Matcher option "A"
    static const size_t EMPTY = 0xFFFFFFFF; ///< accept() returns "empty" last split at end of input
  };
  /// Context returned by before() and after()
  struct Context {
    Context()
      :
        buf(NULL),
        len(0),
        num(0)
    { }
    Context(const char *buf, size_t len, size_t num)
      :
        buf(buf),
        len(len),
        num(num)
    { }
    const char *buf; ///< pointer to buffer
    size_t      len; ///< length of buffered context
    size_t      num; ///< number of bytes shifted out so far, when buffer shifted
  };
  /// Event handler functor base class to invoke when the buffer contents are shifted out, e.g. for logging the data searched.
  struct Handler {
    virtual void operator()(AbstractMatcher&, const char*, size_t, size_t) = 0;
    virtual ~Handler() { };
  };
 protected:
  /// AbstractMatcher::Options for matcher engines.
  struct Option {
    Option()
      :
        A(false),
        N(false),
        W(false),
        T(8)
    { }
    bool A; ///< accept any/all (?^X) negative patterns as Const::REDO accept index codes
    bool N; ///< nullable, find may return empty match (N/A to scan, split, matches)
    bool W; ///< half-check for "whole words", check only left of \< and right of \> for non-word character
    char T; ///< tab size, must be a power of 2, default is 8, for column count and indent \i, \j, and \k
  };
  /// AbstractMatcher::Iterator class for scanning, searching, and splitting input character sequences.
  template<typename T> /// @tparam <T> AbstractMatcher or const AbstractMatcher
  class Iterator {
    friend class AbstractMatcher;
    friend class Iterator<typename reflex::TypeOp<T>::ConstType>;
    friend class Iterator<typename reflex::TypeOp<T>::NonConstType>;
   public:
    /// Non-const AbstractMatcher type
    typedef typename reflex::TypeOp<T>::NonConstType NonConstT;
    /// Const AbstractMatcher type
    typedef typename reflex::TypeOp<T>::ConstType ConstT;
    /// Iterator iterator_category trait.
    typedef std::input_iterator_tag iterator_category;
    /// Iterator value_type trait.
    typedef T value_type;
    /// Iterator difference_type trait.
    typedef std::ptrdiff_t difference_type;
    /// Iterator pointer trait.
    typedef T* pointer;
    /// Iterator reference trait.
    typedef T& reference;
    /// Construct an AbstractMatcher::Iterator such that Iterator() == AbstractMatcher::Operation(*this, method).end().
    Iterator()
      :
        matcher_(NULL),
        method_()
    { }
    /// Copy constructor.
    Iterator(const Iterator<NonConstT>& it)
      :
        matcher_(it.matcher_),
        method_(it.method_)
    { }
    /// AbstractMatcher::Iterator dereference.
    T& operator*() const
      /// @returns (const) reference to the iterator's matcher
    {
      return *matcher_;
    }
    /// AbstractMatcher::Iterator pointer.
    T* operator->() const
      /// @returns (const) pointer to the iterator's matcher
    {
      return matcher_;
    }
    /// AbstractMatcher::Iterator equality.
    bool operator==(const Iterator<ConstT>& rhs) const
      /// @returns true if iterator equals RHS
    {
      return matcher_ == rhs.matcher_;
    }
    /// AbstractMatcher::Iterator inequality.
    bool operator!=(const Iterator<ConstT>& rhs) const
      /// @returns true if iterator does not equal RHS
    {
      return matcher_ != rhs.matcher_;
    }
    /// AbstractMatcher::Iterator preincrement.
    Iterator& operator++()
      /// @returns reference to this iterator
    {
      if (matcher_->match(method_) == 0)
        matcher_ = NULL;
      return *this;
    }
    /// AbstractMatcher::Iterator postincrement.
    Iterator operator++(int)
      /// @returns iterator to current match
    {
      Iterator it = *this;
      operator++();
      return it;
    }
    /// Construct an AbstractMatcher::Iterator to scan, search, or split an input character sequence.
    Iterator(
        NonConstT *matcher, ///< iterate over pattern matches with this matcher
        Method     method)  ///< match using method Const::SCAN, Const::FIND, or Const::SPLIT
      :
        matcher_(matcher),
        method_(method)
    {
      if (matcher_ && matcher_->match(method_) == 0)
        matcher_ = NULL;
    }
   private:
    NonConstT *matcher_; ///< the matcher used by this iterator
    Method     method_;  ///< the method for pattern matching by this iterator's matcher
  };
 public:
  typedef AbstractMatcher::Iterator<AbstractMatcher>       iterator;       ///< std::input_iterator for scanning, searching, and splitting input character sequences
  typedef AbstractMatcher::Iterator<const AbstractMatcher> const_iterator; ///< std::input_iterator for scanning, searching, and splitting input character sequences
  /// AbstractMatcher::Operation functor to match input to a pattern, also provides a (const) AbstractMatcher::iterator to iterate over matches.
  class Operation {
   public:
    /// Construct an AbstractMatcher::Operation functor to scan, search, or split an input character sequence.
    Operation(
        AbstractMatcher *matcher, ///< use this matcher for this functor
        Method           method)  ///< match using method Const::SCAN, Const::FIND, or Const::SPLIT
      :
        matcher_(matcher),
        method_(method)
    { }
    void init(
        AbstractMatcher *matcher, ///< use this matcher for this functor
        Method           method)  ///< match using method Const::SCAN, Const::FIND, or Const::SPLIT
    {
      matcher_ = matcher;
      method_ = method;
    }
    /// AbstractMatcher::Operation() matches input to a pattern using method Const::SCAN, Const::FIND, or Const::SPLIT.
    size_t operator()() const
      /// @returns value of accept() >= 1 for match or 0 for end of matches
    {
      return matcher_->match(method_);
    }
    /// AbstractMatcher::Operation.begin() returns a std::input_iterator to the start of the matches.
    iterator begin() const
      /// @returns input iterator
    {
      return iterator(matcher_, method_);
    }
    /// AbstractMatcher::Operation.end() returns a std::input_iterator to the end of matches.
    iterator end() const
      /// @returns input iterator
    {
      return iterator();
    }
    /// AbstractMatcher::Operation.cbegin() returns a const std::input_iterator to the start of the matches.
    const_iterator cbegin() const
      /// @returns input const_iterator
    {
      return const_iterator(matcher_, method_);
    }
    /// AbstractMatcher::Operation.cend() returns a const std::input_iterator to the end of matches.
    const_iterator cend() const
      /// @returns input const_iterator
    {
      return const_iterator();
    }
   private:
    AbstractMatcher *matcher_; ///< the matcher used by this functor
    Method           method_;  ///< the method for pattern matching by this functor's matcher
  };
  /// Construct a base abstract matcher.
  AbstractMatcher(
      const Input& input, ///< input character sequence for this matcher
      const char  *opt)   ///< option string of the form `(A|N|T(=[[:digit:]])?|;)*`
    :
      scan(this, Const::SCAN),
      find(this, Const::FIND),
      split(this, Const::SPLIT)
  {
    in = input;
    init(opt);
  }
  /// Construct a base abstract matcher.
  AbstractMatcher(
      const Input&  input, ///< input character sequence for this matcher
      const Option& opt)   ///< options
    :
      scan(this, Const::SCAN),
      find(this, Const::FIND),
      split(this, Const::SPLIT)
  {
    in = input;
    init();
    opt_ = opt;
  }
  /// Delete abstract matcher, deletes this matcher's internal buffer.
  virtual ~AbstractMatcher()
  {
    DBGLOG("AbstractMatcher::~AbstractMatcher()");
    if (own_)
    {
#if WITH_REALLOC
#if defined(__WIN32__) || defined(_WIN32) || defined(WIN32) || defined(_WIN64) || defined(__BORLANDC__)
      _aligned_free(static_cast<void*>(buf_));
#else
      std::free(static_cast<void*>(buf_));
#endif
#else
      delete[] buf_;
#endif
    }
  }
  /// Polymorphic cloning.
  virtual AbstractMatcher *clone() = 0;
  /// Reset this matcher's state to the initial state and set options (when provided).
  virtual void reset(const char *opt = NULL)
  {
    DBGLOG("AbstractMatcher::reset(%s)", opt ? opt : "(null)");
    if (opt)
    {
      opt_.A = false; // when true: accept any/all (?^X) negative patterns as Const::REDO accept index codes
      opt_.N = false; // when true: find may return empty match (N/A to scan, split, matches)
      opt_.W = false; // when true: half-check for "whole words", check only left of \< and right of \> for non-word character
      opt_.T = 8;     // tab size 1, 2, 4, or 8
      if (opt)
      {
        for (const char *s = opt; *s != '\0'; ++s)
        {
          switch (*s)
          {
            case 'A':
              opt_.A = true;
              break;
            case 'N':
              opt_.N = true;
              break;
            case 'W':
              opt_.W = true;
              break;
            case 'T':
              opt_.T = isdigit(*(s += (s[1] == '=') + 1)) ? static_cast<char>(*s - '0') : 0;
              break;
          }
        }
      }
    }
    if (!own_)
    {
      max_ = Const::BUFSZ;
#if WITH_REALLOC
#if defined(__WIN32__) || defined(_WIN32) || defined(WIN32) || defined(_WIN64) || defined(__BORLANDC__)
      buf_ = static_cast<char*>(_aligned_malloc(max_, 4096));
      if (buf_ == NULL)
        throw std::bad_alloc();
#else
      buf_ = NULL;
      if (posix_memalign(reinterpret_cast<void**>(&buf_), 4096, max_) != 0)
        throw std::bad_alloc();
#endif
#else
      buf_ = new char[max_];
#endif
    }
    buf_[0] = '\0';
    txt_ = buf_;
    len_ = 0;
    cap_ = 0;
    cur_ = 0;
    pos_ = 0;
    end_ = 0;
    ind_ = 0;
    blk_ = 0;
    got_ = Const::BOB;
    chr_ = '\0';
#if WITH_SPAN
    bol_ = buf_;
    evh_ = NULL;
#endif
    lpb_ = buf_;
    lno_ = 1;
#if WITH_SPAN
    cpb_ = buf_;
#endif
    cno_ = 0;
    num_ = 0;
    own_ = true;
    eof_ = false;
    mat_ = false;
  }
  /// Set buffer block size for reading: use 0 (or omit argument) to buffer all input in which case returns true if all the data could be read and false if a read error occurred.
  bool buffer(size_t blk = 0) ///< new block size between 1 and Const::BLOCK, or 0 to buffer all input (default)
    /// @returns true when successful to buffer all input when n=0
  {
    if (blk > Const::BLOCK)
      blk = Const::BLOCK;
    DBGLOG("AbstractMatcher::buffer(%zu)", blk);
    blk_ = blk;
    if (blk > 0 || eof_ || in.eof())
      return true;
    size_t n = in.size(); // get the (rest of the) data size, which is 0 if unknown (e.g. reading input from a TTY or a pipe)
    if (n > 0)
    {
      (void)grow(n + 1); // now attempt to fetch all (remaining) data to store in the buffer, +1 for a final \0
      end_ += get(buf_, n);
    }
    while (in.good()) // there is more to get while good(), e.g. via wrap()
    {
      (void)grow();
      size_t len = get(buf_ + end_, max_ - end_);
      if (len == 0)
        break;
      end_ += len;
    }
    if (end_ == max_)
      (void)grow(1); // make sure we have room for a final \0
    eof_ = in.eof();
    return eof_;
  }
#if WITH_SPAN
  /// Set event handler functor to invoke when the buffer contents are shifted out, e.g. for logging the data searched.
  void set_handler(Handler *handler)
  {
    evh_ = handler;
  }
  /// Get the buffered context before the matching line.
  inline Context before()
  {
    (void)lineno();
    return Context(buf_, bol_ - buf_, num_);
  }
  /// Get the buffered context after EOF is reached.
  inline Context after()
  {
    if (hit_end())
    {
      (void)lineno();
      // if there is no \n at the end of input: increase line count by one to compensate
      if (bol_ < txt_)
        ++lno_;
      return Context(buf_, end_, num_);
    }
    return Context(buf_, 0, num_);
  }
#endif
  /// Set interactive input with buffer size of 1 to read data bytewise which is very slow.
  void interactive()
    /// @note Use this method before any matching is done and before any input is read since the last time input was (re)set.
  {
    DBGLOG("AbstractMatcher::interactive()");
    (void)buffer(1);
  }
  /// Flush the buffer's remaining content.
  void flush()
  {
    DBGLOG("AbstractMatcher::flush()");
    pos_ = end_;
  }
  /// Returns more input data directly from the source (method can be overriden, as by reflex::FlexLexer::get(s, n) for example that invokes reflex::FlexLexer::LexerInput(s, n)).
  virtual size_t get(
      /// @returns the nonzero number of (less or equal to n) 8-bit characters added to buffer s from the current input, or zero when EOF
      char  *s, ///< points to the string buffer to fill with input
      size_t n) ///< size of buffer pointed to by s
  {
    return in.get(s, n);
  }
  /// Returns true if wrapping of input after EOF is supported.
  virtual bool wrap()
    /// @returns true if input was succesfully wrapped
  {
    return false;
  }
  /// Set the input character sequence for this matcher and reset/restart the matcher.
  virtual AbstractMatcher& input(const Input& input) ///< input character sequence for this matcher
    /// @returns this matcher
  {
    DBGLOG("AbstractMatcher::input()");
    in = input;
    reset();
    return *this;
  }
  /// Set the buffer base containing 0-terminated character data to scan in place (data may be modified), reset/restart the matcher.
  AbstractMatcher& buffer(
      char *base,  ///< base of the buffer containing 0-terminated character data
      size_t size) ///< nonzero size of the buffer
    /// @returns this matcher
  {
    if (size > 0)
    {
      if (own_)
      {
#if WITH_REALLOC
#if defined(__WIN32__) || defined(_WIN32) || defined(WIN32) || defined(_WIN64) || defined(__BORLANDC__)
        _aligned_free(static_cast<void*>(buf_));
#else
        std::free(static_cast<void*>(buf_));
#endif
#else
        delete[] buf_;
#endif
      }
      buf_ = base;
      txt_ = buf_;
      len_ = 0;
      cap_ = 0;
      cur_ = 0;
      pos_ = 0;
      end_ = size - 1;
      max_ = size;
      ind_ = 0;
      blk_ = 0;
      got_ = Const::BOB;
      chr_ = '\0';
#if WITH_SPAN
      bol_ = buf_;
      evh_ = NULL;
#endif
      lpb_ = buf_;
      lno_ = 1;
#if WITH_SPAN
      cpb_ = buf_;
#endif
      cno_ = 0;
      num_ = 0;
      own_ = false;
      eof_ = true;
      mat_ = false;
    }
    return *this;
  }
  
  /// Returns nonzero capture index (i.e. true) if the entire input matches this matcher's pattern (and internally caches the true/false result to permit repeat invocations).
  inline size_t matches()
    /// @returns nonzero capture index if the entire input matched this matcher's pattern, zero (i.e. false) otherwise
  {
    if (!mat_ && at_bob())
    {
      mat_ = match(Const::MATCH);
      if (!at_end())
        mat_ = 0;
    }
    return mat_;
  }
  /// Returns a positive integer (true) indicating the capture index of the matched text in the pattern or zero (false) for a mismatch.
  inline size_t accept() const
    /// @returns nonzero capture index of the match in the pattern, which may be matcher dependent, or zero for a mismatch, or Const::EMPTY for the empty last split
  {
    return cap_;
  }
  /// Returns pointer to the begin of the matched text (non-0-terminated), a constant-time operation, use with end() or use size() for text end/length.
  inline const char *begin() const
    /// @returns const char* pointer to the matched text in the buffer
  {
    return txt_;
  }
  /// Returns pointer to the exclusive end of the matched text, a constant-time operation.
  inline const char *end() const
    /// @returns const char* pointer to the exclusive end of the matched text in the buffer
  {
    return txt_ + len_;
  }
  /// Returns 0-terminated string of the text matched, does not include matched \0s, this is a constant-time operation.
  inline const char *text()
    /// @returns 0-terminated const char* string with text matched
  {
    if (chr_ == '\0')
    {
      chr_ = txt_[len_];
      txt_[len_] = '\0';
    }
    return txt_;
  }
  /// Returns the text matched as a string, a copy of text(), may include matched \0s.
  inline std::string str() const
    /// @returns string with text matched
  {
    return std::string(txt_, len_);
  }
  /// Returns the match as a wide string, converted from UTF-8 text(), may include matched \0s.
  inline std::wstring wstr() const
    /// @returns wide string with text matched
  {
    return wcs(txt_, len_);
  }
  /// Returns the length of the matched text in number of bytes, including matched \0s, a constant-time operation.
  inline size_t size() const
    /// @returns match size in bytes
  {
    return len_;
  }
  /// Returns the length of the matched text in number of wide characters.
  inline size_t wsize() const
    /// @returns the length of the match in number of wide (multibyte UTF-8) characters
  {
    size_t n = 0;
    const char *e = txt_ + len_;
    for (const char *s = txt_; s < e; ++s)
      n += (*s & 0xC0) != 0x80;
    return n;
  }
  /// Returns the first 8-bit character of the text matched.
  inline int chr() const
    /// @returns 8-bit char
  {
    return *txt_;
  }
  /// Returns the first wide character of the text matched.
  inline int wchr() const
    /// @returns wide char (UTF-8 converted to Unicode)
  {
    return utf8(txt_);
  }
  /// Set or change the starting line number of the last match.
  inline void lineno(size_t n) ///< new line number
  {
    (void)lineno(); // update lno_ and bol_ (or cno_) before overriding lno_
    lno_ = n;
  }
  /// Updates and returns the starting line number of the match in the input character sequence.
  inline size_t lineno()
    /// @returns line number
  {
#if WITH_SPAN
    if (lpb_ < txt_)
    {
      const char *s = lpb_;
      const char *t = txt_;
      size_t n = 0;
#if defined(HAVE_AVX512BW) && (!defined(_MSC_VER) || defined(_WIN64))
      if (have_HW_AVX512BW())
      {
        n += simd_nlcount_avx512bw(s, t);
      }
      else if (have_HW_AVX2())
      {
        n += simd_nlcount_avx2(s, t);
      }
      else
      {
        __m128i vlcn = _mm_set1_epi8('\n');
        while (s + 16 <= t)
        {
          __m128i vlcm = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s));
          __m128i vlceq = _mm_cmpeq_epi8(vlcm, vlcn);
          uint32_t mask = _mm_movemask_epi8(vlceq);
          n += popcount(mask);
          s += 16;
        }
      }
#elif defined(HAVE_AVX2)
      if (have_HW_AVX2())
      {
        n += simd_nlcount_avx2(s, t);
      }
      else
      {
        __m128i vlcn = _mm_set1_epi8('\n');
        while (s + 16 <= t)
        {
          __m128i vlcm = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s));
          __m128i vlceq = _mm_cmpeq_epi8(vlcm, vlcn);
          uint32_t mask = _mm_movemask_epi8(vlceq);
          n += popcount(mask);
          s += 16;
        }
      }
#elif defined(HAVE_SSE2)
      __m128i vlcn = _mm_set1_epi8('\n');
      while (s + 16 <= t)
      {
        __m128i vlcm = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s));
        __m128i vlceq = _mm_cmpeq_epi8(vlcm, vlcn);
        uint32_t mask = _mm_movemask_epi8(vlceq);
        n += popcount(mask);
        s += 16;
      }
#elif defined(HAVE_NEON)
      {
        // ARM AArch64/NEON SIMD optimized loop? - no code that runs faster than the code below?
      }
#endif
      uint32_t n0 = 0, n1 = 0, n2 = 0, n3 = 0;
      // clang/gcc 4-way auto-vectorizable loop
      while (s + 3 < t)
      {
        n0 += s[0] == '\n';
        n1 += s[1] == '\n';
        n2 += s[2] == '\n';
        n3 += s[3] == '\n';
        s += 4;
      }
      n += n0 + n1 + n2 + n3;
      // epilogue
      if (s < t)
      {
        n += *s == '\n';
        if (++s < t)
        {
          n += *s == '\n';
          if (++s < t)
            n += *s == '\n';
        }
      }
      // if newlines are detected, then find begin of the last line to adjust bol
      if (n > 0)
      {
        lno_ += n;
        s = lpb_;
        // clang/gcc 4-way auto-vectorizable loop
        while (t - 4 >= s)
        {
          if ((t[-1] == '\n') | (t[-2] == '\n') | (t[-3] == '\n') | (t[-4] == '\n'))
            break;
          t -= 4;
        }
        // epilogue
        if (--t >= s && *t != '\n')
          if (--t >= s && *t != '\n')
            if (--t >= s && *t != '\n')
              --t;
        bol_ = t + 1;
        cpb_ = bol_;
        cno_ = 0;
      }
      lpb_ = txt_;
    }
#else
    size_t n = lno_;
    size_t k = cno_;
    const char *s = lpb_;
    const char *e = txt_;
    while (s < e)
    {
      if (*s == '\n')
      {
        ++n;
        k = 0;
      }
      else if (*s == '\t')
      {
        // count tab spacing
        k += 1 + (~k & (opt_.T - 1));
      }
      else
      {
        // count column offset in UTF-8 chars
        k += ((*s & 0xC0) != 0x80);
      }
      ++s;
    }
    lpb_ = e;
    lno_ = n;
    cno_ = k;
#endif
    return lno_;
  }
  /// Returns the number of lines that the match spans.
  inline size_t lines()
    /// @returns number of lines
  {
    size_t n = 1;
    const char *e = txt_ + len_;
    for (const char *s = txt_; s < e; ++s)
      n += (*s == '\n');
    return n;
  }
  /// Returns the inclusive ending line number of the match in the input character sequence.
  inline size_t lineno_end()
    /// @returns line number
  {
    return lineno() + lines() - 1;
  }
  /// Set or change the starting column number of the last match.
  inline void columno(size_t n) ///< new column number
  {
    (void)lineno(); // update lno_ and bol_ (or cno_) before overriding lno_
#if WITH_SPAN
    cpb_ = txt_;
#else
    lpb_ = txt_;
#endif
    cno_ = n;
  }
  /// Updates and returns the starting column number of the matched text, taking tab spacing into account and counting wide characters as one character each
  inline size_t columno()
    /// @returns column number
  {
    (void)lineno();
#if WITH_SPAN
    const char *s = cpb_;
    const char *e = txt_;
    size_t k = cno_;
    size_t m = opt_.T - 1;
    while (s < e)
    {
      if (*s == '\t')
        k += 1 + (~k & m); // count tab spacing
      else
        k += ((*s & 0xC0) != 0x80); // count column offset in UTF-8 chars
      ++s;
    }
    cpb_ = txt_;
    cno_ = k;
#endif
    return cno_;
  }
  /// Returns the number of columns of the matched text, taking tab spacing into account and counting wide characters as one character each.
  inline size_t columns()
    /// @returns number of columns
  {
    // count columns in tabs and UTF-8 chars
#if WITH_SPAN
    const char *s = txt_;
    const char *e = txt_ + len_;
    size_t n = columno();
    size_t k = n;
    while (s < e)
    {
      if (*s == '\t')
        k += 1 + (~k & (opt_.T - 1)); // count tab spacing
      else if (*s != '\r' && *s != '\n')
        k += ((*s & 0xC0) != 0x80); // count column offset in UTF-8 chars
      ++s;
    }
    return k - n;
#else
    size_t n = cno_;
    size_t m = 0;
    const char *s;
    const char *t = buf_;
    for (s = txt_ + len_ - 1; s >= t; --s)
    {
      if (*s == '\n')
      {
        n = 0;
        break;
      }
    }
    t = txt_;
    const char *e = txt_ + len_;
    for (++s; s < e; ++s)
    {
      if (s == t)
        m = n;
      if (*s == '\t')
        n += 1 + (~n & (opt_.T - 1));
      else
        n += (*s & 0xC0) != 0x80;
    }
    return n - m;
#endif
  }
#if WITH_SPAN
  /// Returns the inclusive ending column number of the matched text on the ending matching line, taking tab spacing into account and counting wide characters as one character each
  inline size_t columno_end()
    /// @returns column number
  {
    if (len_ == 0)
      return columno();
    (void)lineno();
    const char *e = txt_ + len_;
    const char *s = e;
    const char *b = bol_;
    while (--s >= b)
      if (*s == '\n')
        break;
    size_t k = 0;
    while (++s < e)
    {
      if (*s == '\t')
        k += 1 + (~k & (opt_.T - 1));
      else
        k += (*s & 0xC0) != 0x80;
    }
    return k > 0 ? k - 1 : 0;
  }
#endif
  /// Returns std::pair<size_t,std::string>(accept(), str()), useful for tokenizing input into containers of pairs.
  inline std::pair<size_t,std::string> pair() const
    /// @returns std::pair<size_t,std::string>(accept(), str())
  {
    return std::pair<size_t,std::string>(accept(), str());
  }
  /// Returns std::pair<size_t,std::wstring>(accept(), wstr()), useful for tokenizing input into containers of pairs.
  inline std::pair<size_t,std::wstring> wpair() const
    /// @returns std::pair<size_t,std::wstring>(accept(), wstr())
  {
    return std::pair<size_t,std::wstring>(accept(), wstr());
  }
  /// Returns the position of the first character of the match in the input character sequence, a constant-time operation.
  inline size_t first() const
    /// @returns position in the input character sequence
  {
    return num_ + txt_ - buf_;
  }
  /// Returns the exclusive position of the last character of the match in the input character sequence, a constant-time operation.
  inline size_t last() const
    /// @returns position in the input character sequence
  {
    return first() + size();
  }
  /// Returns true if this matcher is at the start of a buffer to read an input character sequence. Use reset() to restart reading new input.
  inline bool at_bob() const
    /// @returns true if at the begin of an input sequence
  {
    return got_ == Const::BOB;
  }
  /// Set/reset the begin of a buffer state.
  inline void set_bob(bool bob) ///< if true: set begin of buffer state
  {
    if (bob)
      got_ = Const::BOB;
    else if (got_ == Const::BOB)
      got_ = Const::UNK;
  }
  /// Returns true if this matcher has no more input to read from the input character sequence.
  inline bool at_end()
    /// @returns true if at end of input and a read attempt will produce EOF
  {
    return pos_ >= end_ && (eof_ || peek() == EOF);
  }
  /// Returns true if this matcher hit the end of the input character sequence.
  inline bool hit_end() const
    /// @returns true if EOF was hit (and possibly more input would have changed the result), false otherwise (but next read attempt may return EOF immediately)
  {
    return pos_ >= end_ && eof_;
  }
  /// Set and force the end of input state.
  inline void set_end(bool eof)
  {
    if (eof)
      flush();
    if (own_)
      eof_ = eof;
  }
  /// Returns true if this matcher reached the begin of a new line.
  inline bool at_bol() const
    /// @returns true if at begin of a new line
  {
    return got_ == Const::BOB || got_ == '\n';
  }
  /// Set/reset the begin of a new line state.
  inline void set_bol(bool bol) ///< if true: set begin of a new line state
  {
    if (bol)
      got_ = '\n';
    else if (got_ == '\n')
      got_ = Const::UNK;
  }
  /// Returns true if this matcher matched text that begins a word.
  inline bool at_bow()
    /// @returns true if this matcher matched text that begins a word
  {
    return !isword(got_) && isword(txt_ < buf_ + end_ ? static_cast<unsigned char>(*txt_) : peek_more());
  }
  /// Returns true if this matcher matched text that ends a word.
  inline bool at_eow()
    /// @returns true if this matcher matched text that ends a word
  {
    return isword(got_) && !isword(txt_ < buf_ + end_ ? static_cast<unsigned char>(*txt_) : peek_more());
  }
  /// Returns the next 8-bit character (unsigned char 0..255 or EOF) from the input character sequence, while preserving the current text() match (but pointer returned by text() may change; warning: does not preserve the yytext string pointer when options --flex and --bison are used).
  int input()
    /// @returns the next character (unsigned char 0..255) from input or EOF (-1)
  {
    DBGLOG("AbstractMatcher::input() pos = %zu end = %zu", pos_, end_);
    if (pos_ < end_)
    {
      if (chr_ != '\0' && buf_ + pos_ == txt_ + len_)
        got_ = chr_;
      else
        got_ = static_cast<unsigned char>(buf_[pos_]);
      ++pos_;
    }
    else
    {
#if WITH_FAST_GET
      got_ = get_more();
#else
      got_ = get();
#endif
    }
    cur_ = pos_;
    return got_;
  }
  /// Returns the next wide character (unsigned 0..U+10FFFF or EOF) from the input character sequence, while preserving the current text() match (but pointer returned by text() may change; warning: does not preserve the yytext string pointer when options --flex and --bison are used).
  int winput()
    /// @returns the next wide character (unsigned 0..U+10FFFF) or EOF (-1)
  {
    DBGLOG("AbstractMatcher::winput()");
    char tmp[8] = { 0 }, *s = tmp;
    int c;
    if ((c = input()) == EOF)
      return EOF;
    if (static_cast<unsigned char>(*s++ = c) >= 0x80)
    {
      while (((++*s = get()) & 0xC0) == 0x80)
        continue;
      got_ = static_cast<unsigned char>(buf_[cur_ = --pos_]);
    }
    return utf8(tmp);
  }
  /// Put back one character (8-bit) on the input character sequence for matching, DANGER: invalidates the previous text() pointer and match info, unput is not honored when matching in-place using buffer(base, size) and nothing has been read yet.
  void unput(char c) ///< 8-bit character to put back
  {
    DBGLOG("AbstractMatcher::unput()");
    reset_text();
    if (pos_ > 0)
    {
      --pos_;
    }
    else if (own_)
    {
      txt_ = buf_;
      len_ = 0;
      if (end_ + 1 >= max_)
        (void)grow();
      std::memmove(buf_ + 1, buf_, end_);
      ++end_;
    }
    buf_[pos_] = c;
    cur_ = pos_;
  }
  /// Put back one (wide) character on the input character sequence for matching, DANGER: invalidates the previous text() pointer and match info, unput is not honored when matching in-place using buffer(base, size) and nothing has been read yet.
  void wunput(int c) ///< character to put back
  {
    DBGLOG("AbstractMatcher::wunput()");
    char tmp[8];
    size_t n = utf8(c, tmp);
    if (pos_ >= n)
    {
      pos_ -= n;
    }
    else if (own_)
    {
      txt_ = buf_;
      len_ = 0;
      if (end_ + n >= max_)
        (void)grow();
      std::memmove(buf_ + n, buf_, end_);
      end_ += n;
    }
    std::memcpy(&buf_[pos_], tmp, n);
    cur_ = pos_;
  }
  /// Peek at the next character available for reading from the current input source.
  inline int peek()
    /// @returns the character (unsigned char 0..255) or EOF (-1)
  {
    DBGLOG("AbstractMatcher::peek()");
#if WITH_FAST_GET
    return pos_ < end_ ? static_cast<unsigned char>(buf_[pos_]) : peek_more();
#else
    if (pos_ < end_)
      return static_cast<unsigned char>(buf_[pos_]);
    if (eof_)
      return EOF;
    while (true)
    {
      if (end_ + blk_ + 1 >= max_)
        (void)grow();
      end_ += get(buf_ + end_, blk_ > 0 ? blk_ : max_ - end_ - 1);
      if (pos_ < end_)
        return static_cast<unsigned char>(buf_[pos_]);
      DBGLOGN("peek(): EOF");
      if (!wrap())
      {
        eof_ = true;
        return EOF;
      }
    }
#endif
  }
#if WITH_SPAN
  /// Returns pointer to the begin of the line in the buffer containing the matched text.
  inline const char *bol()
    /// @returns pointer to the begin of line
  {
    (void)lineno();
    return bol_;
  }
  /// Returns pointer to the end of the line (last char + 1) in the buffer containing the matched text, DANGER: invalidates previous bol() and text() pointers, use eol() before bol(), text(), begin(), and end() when those are used.
  inline const char *eol(bool inclusive = false) ///< true if inclusive, i.e. point after \n instead of at \n
    /// @returns pointer to the end of line
  {
    if (chr_ == '\n' || (txt_ + len_ < buf_ + end_ && txt_[len_] == '\n'))
      return txt_ + len_ + inclusive;
    size_t loc = pos_;
    while (true)
    {
      if (loc < end_)
      {
        char *s = static_cast<char*>(std::memchr(buf_ + loc, '\n', end_ - loc));
        if (s != NULL)
          return s + inclusive;
      }
      if (eof_)
        break;
      (void)grow();
      loc = end_;
      end_ += get(buf_ + end_, blk_ > 0 ? blk_ : max_ - end_ - 1);
      if (loc >= end_ && !wrap())
      {
        eof_ = true;
        break;
      }
    }
    return buf_ + end_;
  }
  /// Returns the number of bytes in the buffer available to search from the current begin()/text() position.
  size_t avail()
  {
    if (peek() == EOF)
      return 0;
    return end_ - (txt_ - buf_);
  }
  /// Returns the byte offset of the match from the start of the line.
  size_t border()
    /// @returns border offset
  {
    return txt_ - bol();
  }
  /// Enlarge the match to span the entire line of input (excluding \n), return text().
  const char *span()
    /// @returns const char* span of text for the entire line
  {
    DBGLOG("AbstractMatcher::span()");
    (void)lineno();
    len_ += txt_ - bol_;
    txt_ = const_cast<char*>(bol_); // requires ugly cast
    if (chr_ == '\n')
      return txt_;
    reset_text();
    const char *e = eol();
    set_current(e - buf_);
    len_ = e - bol_;
    return text();
  }
  /// Returns the line of input (excluding \n) as a string containing the matched text as a substring.
  std::string line()
    /// @returns matching line as a string
  {
    DBGLOG("AbstractMatcher::line()");
    reset_text();
    const char *e = eol(); // warning: must call eol() before bol()
    const char *b = bol();
    return std::string(b, e - b);
  }
  /// Returns the line of input (excluding \n) as a wide string containing the matched text as a substring.
  std::wstring wline()
    /// @returns matching line as a wide string
  {
    DBGLOG("AbstractMatcher::wline()");
    reset_text();
    const char *e = eol(); // warning: must call eol() before bol()
    const char *b = bol();
    while (b < e && (*b & 0xC0) == 0x80) // make sure we advance forward to valid UTF-8
      ++b;
    return wcs(b, e - b);
  }
#endif
  /// Skip input until the specified ASCII character is consumed and return true, or EOF is reached and return false.
  bool skip(char c) ///< ASCII character to skip to
    /// @returns true if skipped to c, false if EOF is reached
  {
    DBGLOG("AbstractMatcher::skip()");
    reset_text();
    len_ = 0;
    while (true)
    {
      txt_ = static_cast<char*>(std::memchr(buf_ + pos_, c, end_ - pos_));
      if (txt_ != NULL)
      {
        ++txt_;
        set_current(txt_ - buf_);
        return true;
      }
      if (eof_)
        break;
      pos_ = cur_ = end_;
      txt_ = buf_ + end_;
      (void)grow();
      end_ += get(buf_ + end_, blk_ > 0 ? blk_ : max_ - end_ - 1);
      if (pos_ >= end_ && !wrap())
      {
        eof_ = true;
        break;
      }
    }
    set_current(end_);
    return false;
  }
  /// Skip input until the specified Unicode character is consumed and return true, or EOF is reached and return false.
  bool skip(wchar_t c) ///< Unicode character to skip to
    /// @returns true if skipped to c, false if EOF is reached
  {
    char s[8];
    size_t n = utf8(c, s);
    s[n] = '\0';
    return skip(s);
  }
  /// Skip input until the specified literal UTF-8 string is consumed and return true, or EOF is reached and return false.
  bool skip(const char *s) ///< literal UTF-8 string to skip to
    /// @returns true if skipped to c, false if EOF is reached
  {
    if (s == NULL || s[0] == '\0')
      return true;
    if (s[1] == '\0')
      return skip(s[0]);
    while (skip(s[0]))
    {
      const char *t = s + 1;
      while (true)
      {
        if (*t == '\0')
        {
          set_current(pos_);
          return true;
        }
        int c = get();
        if (c == EOF)
          return false;
        if (c != static_cast<unsigned char>(*t))
          break;
        ++t;
      }
      pos_ = txt_ - buf_;
    }
    return false;
  }
  /// Fetch the rest of the input as text, useful for searching/splitting up to n times after which the rest is needed.
  const char *rest()
    /// @returns const char* string of the remaining input (wrapped with more input when AbstractMatcher::wrap is defined)
  {
    DBGLOG("AbstractMatcher::rest()");
    reset_text();
    cur_ = pos_;
    txt_ = buf_ + cur_;
    while (!eof_)
    {
      (void)grow();
      pos_ = end_;
      end_ += get(buf_ + end_, blk_ > 0 ? blk_ : max_ - end_ - 1);
      if (pos_ >= end_ && !wrap())
        eof_ = true;
    }
    len_ = end_ - cur_;
    pos_ = cur_ = end_;
    DBGLOGN("rest() length = %zu", len_);
    return text();
  }
  /// Append the next match to the currently matched text returned by AbstractMatcher::text, when the next match found is adjacent to the current match.
  void more()
  {
    cur_ = txt_ - buf_;
  }
  /// Truncate the AbstractMatcher::text length of the match to n characters in length and reposition for next match.
  void less(size_t n) ///< truncated string length
  {
    if (n < len_)
    {
      DBGCHK(pos_ < max_);
      reset_text();
      pos_ = txt_ - buf_ + n;
      DBGCHK(pos_ < max_);
      len_ = n;
      cur_ = pos_;
    }
  }
  /// Cast this matcher to positive integer indicating the nonzero capture index of the matched text in the pattern, same as AbstractMatcher::accept.
  operator size_t() const
    /// @returns nonzero capture index of a match, which may be matcher dependent, or zero for a mismatch
  {
    return accept();
  }
  /// Cast this matcher to a std::string of the text matched by this matcher.
  operator std::string() const
    /// @returns std::string with matched text
  {
    return str();
  }
  /// Cast this matcher to a std::wstring of the text matched by this matcher.
  operator std::wstring() const
    /// @returns std::wstring converted to UCS from the 0-terminated matched UTF-8 text
  {
    return wstr();
  }
  /// Cast the match to std::pair<size_t,std::wstring>(accept(), wstr()), useful for tokenization into containers.
  operator std::pair<size_t,std::string>() const
    /// @returns std::pair<size_t,std::wstring>(accept(), wstr())
  {
    return pair();
  }
  /// Returns true if matched text is equal to a string, useful for std::algorithm.
  bool operator==(const char *rhs) ///< rhs string to compare to
    /// @returns true if matched text is equal to rhs string
    const
  {
    return std::strncmp(rhs, txt_, len_) == 0 && rhs[len_] == '\0';
  }
  /// Returns true if matched text is equalt to a string, useful for std::algorithm.
  bool operator==(const std::string& rhs) ///< rhs string to compare to
    /// @returns true if matched text is equal to rhs string
    const
  {
    return rhs.size() == len_ && rhs.compare(0, std::string::npos, txt_, len_) == 0;
  }
  /// Returns true if capture index is equal to a given size_t value, useful for std::algorithm.
  bool operator==(size_t rhs) ///< capture index to compare accept() to
    /// @returns true if capture index is equal to rhs
    const
  {
    return accept() == rhs;
  }
  /// Returns true if capture index is equal to a given int value, useful for std::algorithm.
  bool operator==(int rhs) ///< capture index to compare accept() to
    /// @returns true if capture index is equal to rhs
    const
  {
    return static_cast<int>(accept()) == rhs;
  }
  /// Returns true if matched text is not equal to a string, useful for std::algorithm.
  bool operator!=(const char *rhs) ///< rhs string to compare to
    /// @returns true if matched text is not equal to rhs string
    const
  {
    return std::strncmp(rhs, txt_, len_) != 0 || rhs[len_] != '\0'; // if static checkers complain here, they are wrong
  }
  /// Returns true if matched text is not equal to a string, useful for std::algorithm.
  bool operator!=(const std::string& rhs) ///< rhs string to compare to
    /// @returns true if matched text is not equal to rhs string
    const
  {
    return rhs.size() > len_ || rhs.compare(0, std::string::npos, txt_, len_) != 0;
  }
  /// Returns true if capture index is not equal to a given size_t value, useful for std::algorithm.
  bool operator!=(size_t rhs) ///< capture index to compare accept() to
    /// @returns true if capture index is not equal to rhs
    const
  {
    return accept() != rhs;
  }
  /// Returns true if capture index is not equal to a given int value, useful for std::algorithm.
  bool operator!=(int rhs) ///< capture index to compare accept() to
    /// @returns true if capture index is not equal to rhs
    const
  {
    return static_cast<int>(accept()) != rhs;
  }
  /// Returns captured text as a std::pair<const char*,size_t> with string pointer (non-0-terminated) and length.
  virtual std::pair<const char*,size_t> operator[](size_t n)
    /// @returns std::pair of string pointer and length in the captured text, where [0] returns std::pair(begin(), size())
    const = 0;
  /// Returns the group capture identifier containing the group capture index >0 and name (or NULL) of a named group capture, or (1,NULL) by default
  virtual std::pair<size_t,const char*> group_id()
    /// @returns a pair of size_t and string
    = 0;
  /// Returns the next group capture identifier containing the group capture index >0 and name (or NULL) of a named group capture, or (0,NULL) when no more groups matched
  virtual std::pair<size_t,const char*> group_next_id()
    /// @returns a pair of size_t and string
    = 0;
  /// Set tab size 1, 2, 4, or 8
  void tabs(char n) ///< tab size 1, 2, 4, or 8
  {
    opt_.T = n & 0xf;
  }
  /// Returns current tab size 1, 2, 4, or 8.
  char tabs()
  {
    return opt_.T;
  }
  Operation scan;  ///< functor to scan input (to tokenize input)
  Operation find;  ///< functor to search input
  Operation split; ///< functor to split input
  Input in;        ///< input character sequence being matched by this matcher
 protected:
  /// Initialize the base abstract matcher at construction.
  virtual void init(const char *opt = NULL) ///< options
  {
    DBGLOG("AbstractMatcher::init(%s)", opt ? opt : "");
    own_ = false; // require allocation of a buffer
    reset(opt);
  }
  /// The abstract match operation implemented by pattern matching engines derived from AbstractMatcher.
  virtual size_t match(Method method)
    /// @returns nonzero when input matched the pattern using method Const::SCAN, Const::FIND, Const::SPLIT, or Const::MATCH
    = 0;
  /// Shift or expand the internal buffer when it is too small to accommodate more input, where the buffer size is doubled when needed, change cur_, pos_, end_, max_, ind_, buf_, bol_, lpb_, and txt_.
  inline bool grow(size_t need = Const::BLOCK) ///< optional needed space = Const::BLOCK size by default
    /// @returns true if buffer was shifted or enlarged
  {
    if (max_ - end_ >= need + 1)
      return false;
#if WITH_SPAN
    (void)lineno();
    cno_ = 0;
    if (bol_ + Const::BOLSZ - buf_ < txt_ - bol_ && evh_ == NULL)
    {
      // this line is very long, so shift all the way to the match instead of to the begin of the last line
      DBGLOG("Line in buffer is too long to shift, moving bol position to text match position");
      (void)columno();
      bol_ = txt_;
    }
    size_t gap = bol_ - buf_;
    if (gap > 0)
    {
      if (evh_ != NULL)
        (*evh_)(*this, buf_, gap, num_);
      cur_ -= gap;
      ind_ -= gap;
      pos_ -= gap;
      end_ -= gap;
      txt_ -= gap;
      bol_ -= gap;
      lpb_ -= gap;
      num_ += gap;
      std::memmove(buf_, buf_ + gap, end_);
    }
    if (max_ - end_ >= need)
    {
      DBGLOG("Shift buffer to close gap of %zu bytes", gap);
    }
    else
    {
      size_t newmax = end_ + need;
      while (max_ < newmax)
        max_ *= 2;
      DBGLOG("Expand buffer to %zu bytes", max_);
#if WITH_REALLOC
#if defined(__WIN32__) || defined(_WIN32) || defined(WIN32) || defined(_WIN64) || defined(__BORLANDC__)
      char *newbuf = static_cast<char*>(_aligned_realloc(static_cast<void*>(buf_), max_, 4096));
#else
      char *newbuf = static_cast<char*>(std::realloc(static_cast<void*>(buf_), max_));
#endif
      if (newbuf == NULL)
        throw std::bad_alloc();
#else
      char *newbuf = new char[max_];
      std::memcpy(newbuf, buf_, end_);
      delete[] buf_;
#endif
      txt_ = newbuf + (txt_ - buf_);
      lpb_ = newbuf + (lpb_ - buf_);
      buf_ = newbuf;
    }
    bol_ = buf_;
    cpb_ = buf_;
#else
    size_t gap = txt_ - buf_;
    if (max_ - end_ + gap >= need)
    {
      DBGLOG("Shift buffer to close gap of %zu bytes", gap);
      (void)lineno();
      cur_ -= gap;
      ind_ -= gap;
      pos_ -= gap;
      end_ -= gap;
      num_ += gap;
      if (end_ > 0)
        std::memmove(buf_, txt_, end_);
      txt_ = buf_;
      lpb_ = buf_;
    }
    else
    {
      size_t newmax = end_ - gap + need;
      size_t oldmax = max_;
      while (max_ < newmax)
        max_ *= 2;
      if (oldmax < max_)
      {
        DBGLOG("Expand buffer from %zu to %zu bytes", oldmax, max_);
        (void)lineno();
        cur_ -= gap;
        ind_ -= gap;
        pos_ -= gap;
        end_ -= gap;
        num_ += gap;
#if WITH_REALLOC
        std::memmove(buf_, txt_, end_);
#if defined(__WIN32__) || defined(_WIN32) || defined(WIN32) || defined(_WIN64) || defined(__BORLANDC__)
        char *newbuf = static_cast<char*>(_aligned_realloc(static_cast<void*>(buf_), max_, 4096));
#else
        char *newbuf = static_cast<char*>(std::realloc(static_cast<void*>(buf_), max_));
#endif
        if (newbuf == NULL)
          throw std::bad_alloc();
#else
        char *newbuf = new char[max_];
        std::memcpy(newbuf, txt_, end_);
        delete[] buf_;
#endif
        buf_ = newbuf;
        txt_ = buf_;
        lpb_ = buf_;
      }
    }
#endif
    return true;
  }
  /// Returns the next character read from the current input source.
  inline int get()
    /// @returns the character read (unsigned char 0..255) or EOF (-1)
  {
    DBGLOG("AbstractMatcher::get()");
#if WITH_FAST_GET
    return pos_ < end_ ? static_cast<unsigned char>(buf_[pos_++]) : get_more();
#else
    if (pos_ < end_)
      return static_cast<unsigned char>(buf_[pos_++]);
    if (eof_)
      return EOF;
    while (true)
    {
      if (end_ + blk_ + 1 >= max_)
        (void)grow();
      end_ += get(buf_ + end_, blk_ > 0 ? blk_ : max_ - end_ - 1);
      if (pos_ < end_)
        return static_cast<unsigned char>(buf_[pos_++]);
      DBGLOGN("get(): EOF");
      if (!wrap())
      {
        eof_ = true;
        return EOF;
      }
    }
#endif
  }
  /// Reset the matched text by removing the terminating \0, which is needed to search for a new match.
  inline void reset_text()
  {
    if (chr_ != '\0')
    {
      txt_[len_] = chr_;
      chr_ = '\0';
    }
  }
  /// Set the current position in the buffer for the next match.
  inline void set_current(size_t loc) ///< new location in buffer
  {
    DBGCHK(loc <= end_);
    pos_ = cur_ = loc;
#if WITH_SPAN
    got_ = loc > 0 ? static_cast<unsigned char>(buf_[loc - 1]) : '\n';
#else
    got_ = loc > 0 ? static_cast<unsigned char>(buf_[loc - 1]) : Const::UNK;
#endif
  }
  /// Set the current match position in the buffer.
  inline void set_current_match(size_t loc) ///< new location in buffer
  {
    set_current(loc);
    txt_ = buf_ + cur_;
  }
  /// Get the next character and grow the buffer to make more room if necessary.
  inline int get_more()
    /// @returns the character read (unsigned char 0..255) or EOF (-1)
  {
    DBGLOG("AbstractMatcher::get_more()");
    if (eof_)
      return EOF;
    while (true)
    {
      if (end_ + blk_ + 1 >= max_)
        (void)grow();
      end_ += get(buf_ + end_, blk_ > 0 ? blk_ : max_ - end_ - 1);
      if (pos_ < end_)
        return static_cast<unsigned char>(buf_[pos_++]);
      DBGLOGN("get_more(): EOF");
      if (!wrap())
      {
        eof_ = true;
        return EOF;
      }
    }
  }
  /// Peek at the next character and grow the buffer to make more room if necessary.
  inline int peek_more()
    /// @returns the character (unsigned char 0..255) or EOF (-1)
  {
    DBGLOG("AbstractMatcher::peek_more()");
    if (eof_)
      return EOF;
    while (true)
    {
      if (end_ + blk_ + 1 >= max_)
        (void)grow();
      end_ += get(buf_ + end_, blk_ > 0 ? blk_ : max_ - end_ - 1);
      if (pos_ < end_)
        return static_cast<unsigned char>(buf_[pos_]);
      DBGLOGN("peek_more(): EOF");
      if (!wrap())
      {
        eof_ = true;
        return EOF;
      }
    }
  }
  Option      opt_; ///< options for matcher engines
  char       *buf_; ///< input character sequence buffer
  char       *txt_; ///< points to the matched text in buffer AbstractMatcher::buf_
  size_t      len_; ///< size of the matched text
  size_t      cap_; ///< nonzero capture index of an accepted match or zero
  size_t      cur_; ///< next position in AbstractMatcher::buf_ to assign to AbstractMatcher::txt_
  size_t      pos_; ///< position in AbstractMatcher::buf_ after AbstractMatcher::txt_
  size_t      end_; ///< ending position of the input buffered in AbstractMatcher::buf_
  size_t      max_; ///< total buffer size and max position + 1 to fill
  size_t      ind_; ///< current indent position
  size_t      blk_; ///< block size for block-based input reading, as set by AbstractMatcher::buffer
  int         got_; ///< last unsigned character we looked at (to determine anchors and boundaries)
  int         chr_; ///< the character located at AbstractMatcher::txt_[AbstractMatcher::len_]
#if WITH_SPAN
  const char *bol_; ///< begin of line pointer in buffer
  Handler    *evh_; ///< event handler functor to invoke when buffer contents are shifted out
#endif
  const char *lpb_; ///< line pointer in buffer, updated when counting line numbers with lineno()
  size_t      lno_; ///< line number count (cached)
#if WITH_SPAN
  const char *cpb_; ///< column pointer in buffer, updated when counting column numbers with columno()
#endif
  size_t      cno_; ///< column number count (cached)
  size_t      num_; ///< character count of the input till bol_
  bool        own_; ///< true if AbstractMatcher::buf_ was allocated and should be deleted
  bool        eof_; ///< input has reached EOF
  bool        mat_; ///< true if AbstractMatcher::matches() was successful
};

/// The pattern matcher class template extends abstract matcher base class.
template<typename P> /// @tparam <P> pattern class to instantiate a matcher
class PatternMatcher : public AbstractMatcher {
 public:
  typedef P Pattern; ///< pattern class of this matcher, a typedef of the PatternMatcher template parameter
  /// Copy constructor, the underlying pattern object is shared (not deep copied).
  PatternMatcher(const PatternMatcher& matcher) ///< matcher with pattern to use (pattern may be shared)
    :
      AbstractMatcher(matcher.in, matcher.opt_),
      pat_(matcher.pat_),
      own_(false)
  {
    DBGLOG("PatternMatcher::PatternMatcher(matcher)");
  }
  /// Delete matcher, deletes pattern when owned
  virtual ~PatternMatcher()
  {
    DBGLOG("PatternMatcher::~PatternMatcher()");
    if (own_ && pat_ != NULL)
      delete pat_;
  }
  /// Assign a matcher, the underlying pattern object is shared (not deep copied).
  PatternMatcher& operator=(const PatternMatcher& matcher) ///< matcher with pattern to use (pattern may be shared)
  {
    scan.init(this, Const::SCAN);
    find.init(this, Const::FIND);
    split.init(this, Const::SPLIT);
    in = matcher.in;
    reset();
    opt_ = matcher.opt_;
    pat_ = matcher.pat_,
    own_ = false;
    return *this;
  }
  /// Set the pattern to use with this matcher as a shared pointer to another matcher pattern.
  virtual PatternMatcher& pattern(const PatternMatcher& matcher) ///< the other matcher
    /// @returns this matcher
  {
    opt_ = matcher.opt_;
    return this->pattern(matcher.pattern());
  }
  /// Set the pattern to use with this matcher (the given pattern is shared and must be persistent).
  virtual PatternMatcher& pattern(const Pattern& pattern) ///< pattern object for this matcher
    /// @returns this matcher
  {
    DBGLOG("PatternMatcher::pattern()");
    if (pat_ != &pattern)
    {
      if (own_ && pat_ != NULL)
        delete pat_;
      pat_ = &pattern;
      own_ = false;
    }
    return *this;
  }
  /// Set the pattern to use with this matcher (the given pattern is shared and must be persistent).
  virtual PatternMatcher& pattern(const Pattern *pattern) ///< pattern object for this matcher
    /// @returns this matcher
  {
    DBGLOG("PatternMatcher::pattern()");
    if (pat_ != pattern)
    {
      if (own_ && pat_ != NULL)
        delete pat_;
      pat_ = pattern;
      own_ = false;
    }
    return *this;
  }
  /// Set the pattern from a regex string to use with this matcher.
  virtual PatternMatcher& pattern(const char *pattern) ///< regex string to instantiate internal pattern object
    /// @returns this matcher
  {
    DBGLOG("PatternMatcher::pattern(\"%s\")", pattern);
    if (own_ && pat_ != NULL)
      delete pat_;
    pat_ = new Pattern(pattern);
    own_ = true;
    return *this;
  }
  /// Set the pattern from a regex string to use with this matcher.
  virtual PatternMatcher& pattern(const std::string& pattern) ///< regex string to instantiate internal pattern object
    /// @returns this matcher
  {
    DBGLOG("PatternMatcher::pattern(\"%s\")", pattern.c_str());
    if (own_ && pat_ != NULL)
      delete pat_;
    pat_ = new Pattern(pattern);
    own_ = true;
    return *this;
  }
  /// Returns true if this matcher has a pattern.
  bool has_pattern() const
    /// @returns true if this matcher has a pattern
  {
    return pat_ != NULL;
  }
  /// Returns true if this matcher has its own pattern not received from another matcher (responsible to delete).
  bool own_pattern() const
    /// @returns true if this matcher has its own pattern
  {
    return own_ && pat_ != NULL;
  }
  /// Returns a reference to the pattern object associated with this matcher.
  const Pattern& pattern() const
    /// @returns reference to pattern object
  {
    ASSERT(pat_ != NULL);
    return *pat_;
  }
 protected:
  /// Construct a base abstract matcher from a pointer to a persistent pattern object (that is shared with this class) and an input character sequence.
  PatternMatcher(
      const Pattern *pattern = NULL,  ///< points to pattern object for this matcher
      const Input&   input = Input(), ///< input character sequence for this matcher
      const char    *opt = NULL)      ///< option string of the form `(A|N|T(=[[:digit:]])?|;)*`
    :
      AbstractMatcher(input, opt),
      pat_(pattern),
      own_(false)
  { }
  /// Construct a base abstract matcher from a persistent pattern object (that is shared with this class) and an input character sequence.
  PatternMatcher(
      const Pattern& pattern,         ///< pattern object for this matcher
      const Input&   input = Input(), ///< input character sequence for this matcher
      const char    *opt = NULL)      ///< option string of the form `(A|N|T(=[[:digit:]])?|;)*`
    :
      AbstractMatcher(input, opt),
      pat_(&pattern),
      own_(false)
  { }
  /// Construct a base abstract matcher from a regex pattern string and an input character sequence.
  PatternMatcher(
      const char  *pattern,         ///< regex string instantiates pattern object for this matcher
      const Input& input = Input(), ///< input character sequence for this matcher
      const char  *opt = NULL)      ///< option string of the form `(A|N|T(=[[:digit:]])?|;)*`
    :
      AbstractMatcher(input, opt),
      pat_(new Pattern(pattern)),
      own_(true)
  { }
  /// Construct a base abstract matcher from a regex pattern string and an input character sequence.
  PatternMatcher(
      const std::string& pattern,         ///< regex string instantiates pattern object for this matcher
      const Input&       input = Input(), ///< input character sequence for this matcher
      const char        *opt = NULL)      ///< option string of the form `(A|N|T(=[[:digit:]])?|;)*`
    :
      AbstractMatcher(input, opt),
      pat_(new Pattern(pattern)),
      own_(true)
  { }
  const Pattern *pat_; ///< points to the pattern object used by the matcher
  bool           own_; ///< true if PatternMatcher::pat_ was allocated and should be deleted
};

/// A specialization of the pattern matcher class template for std::string, extends abstract matcher base class.
template<>
class PatternMatcher<std::string> : public AbstractMatcher {
 public:
  typedef std::string Pattern; ///< pattern class of this matcher
  /// Copy constructor, the underlying pattern string is copied.
  PatternMatcher(const PatternMatcher& matcher) ///< matcher with pattern to copy and use
    :
      AbstractMatcher(matcher.in, matcher.opt_),
      pat_(matcher.pat_ != NULL ? new Pattern(*matcher.pat_) : NULL),
      own_(matcher.pat_ != NULL)
  { }
  /// Delete matcher, deletes pattern when owned
  virtual ~PatternMatcher()
  {
    DBGLOG("PatternMatcher::~PatternMatcher()");
    if (own_ && pat_ != NULL)
      delete pat_;
  }
  /// Assign a matcher, the underlying pattern string is shared (not deep copied).
  PatternMatcher& operator=(const PatternMatcher& matcher) ///< matcher with pattern to use (pattern may be shared)
  {
    scan.init(this, Const::SCAN);
    find.init(this, Const::FIND);
    split.init(this, Const::SPLIT);
    in = matcher.in;
    reset();
    opt_ = matcher.opt_;
    pat_ = matcher.pat_,
    own_ = false;
    return *this;
  }
  /// Set the pattern to use with this matcher as a shared pointer to another matcher pattern.
  virtual PatternMatcher& pattern(const PatternMatcher& matcher) ///< the other matcher
    /// @returns this matcher
  {
    opt_ = matcher.opt_;
    return this->pattern(matcher.pattern());
  }
  /// Set the pattern to use with this matcher (the given pattern is shared and must be persistent).
  virtual PatternMatcher& pattern(const Pattern *pattern) ///< pattern string for this matcher
    /// @returns this matcher
  {
    DBGLOG("Patternatcher::pattern()");
    if (pat_ != pattern)
    {
      if (own_ && pat_ != NULL)
        delete pat_;
      pat_ = pattern;
      own_ = false;
    }
    return *this;
  }
  /// Set the pattern from a regex string to use with this matcher.
  virtual PatternMatcher& pattern(const char *pattern) ///< regex string to instantiate internal pattern string
    /// @returns this matcher
  {
    DBGLOG("Patternatcher::pattern(\"%s\")", pattern);
    if (own_ && pat_ != NULL)
      delete pat_;
    pat_ = new Pattern(pattern);
    own_ = true;
    return *this;
  }
  /// Set the pattern from a regex string to use with this matcher.
  virtual PatternMatcher& pattern(const std::string& pattern) ///< regex string to instantiate internal pattern string
    /// @returns this matcher
  {
    DBGLOG("Patternatcher::pattern(\"%s\")", pattern.c_str());
    if (own_ && pat_ != NULL)
      delete pat_;
    pat_ = new Pattern(pattern);
    own_ = true;
    return *this;
  }
  /// Returns true if this matcher has a pattern.
  bool has_pattern() const
    /// @returns true if this matcher has a pattern
  {
    return pat_ != NULL;
  }
  /// Returns true if this matcher has its own pattern not received from another matcher (responsible to delete).
  bool own_pattern() const
    /// @returns true if this matcher has its own pattern
  {
    return own_ && pat_ != NULL;
  }
  /// Returns a reference to the pattern string associated with this matcher.
  const Pattern& pattern() const
    /// @returns reference to pattern string
  {
    ASSERT(pat_ != NULL);
    return *pat_;
  }
 protected:
  /// Construct a base abstract matcher from a pointer to a persistent pattern string (that is shared with this class) and an input character sequence.
  PatternMatcher(
      const Pattern *pattern = NULL,  ///< points to pattern string for this matcher
      const Input&   input = Input(), ///< input character sequence for this matcher
      const char    *opt = NULL)      ///< option string of the form `(A|N|T(=[[:digit:]])?|;)*`
    :
      AbstractMatcher(input, opt),
      pat_(pattern),
      own_(false)
  { }
  /// Construct a base abstract matcher from a regex pattern string and an input character sequence.
  PatternMatcher(
      const char  *pattern,         ///< regex string instantiates pattern string for this matcher
      const Input& input = Input(), ///< input character sequence for this matcher
      const char  *opt = NULL)      ///< option string of the form `(A|N|T(=[[:digit:]])?|;)*`
    :
      AbstractMatcher(input, opt),
      pat_(new Pattern(pattern)),
      own_(true)
  { }
  /// Construct a base abstract matcher from a regex pattern string and an input character sequence.
  PatternMatcher(
      const std::string& pattern,         ///< regex string instantiates pattern string for this matcher
      const Input&       input = Input(), ///< input character sequence for this matcher
      const char        *opt = NULL)      ///< option string of the form `(A|N|T(=[[:digit:]])?|;)*`
    :
      AbstractMatcher(input, opt),
      pat_(new Pattern(pattern)),
      own_(true)
  { }
  const Pattern *pat_; ///< points to the pattern string used by the matcher
  bool           own_; ///< true if PatternMatcher::pat_ was allocated and should be deleted
};

} // namespace reflex

/// Write matched text to a stream.
inline std::ostream& operator<<(std::ostream& os, const reflex::AbstractMatcher& matcher)
{
  os.write(matcher.begin(), matcher.size());
  return os;
}

/// Read stream and store all content in the matcher's buffer.
inline std::istream& operator>>(std::istream& is, reflex::AbstractMatcher& matcher)
{
  matcher.input(is).buffer();
  return is;
}

#endif
