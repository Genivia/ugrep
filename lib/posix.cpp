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
@file      posix.cpp
@brief     Get POSIX character class ranges and regex translations
@author    Robert van Engelen - engelen@genivia.com
@copyright (c) 2016-2020, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt
*/

#include <reflex/posix.h>

namespace reflex {

namespace Posix {

Tables::Tables()
{
  static const int Alnum[]  = { '0', '9', 'A', 'Z', 'a', 'z', 0, 0 };
  static const int Alpha[]  = { 'A', 'Z', 'a', 'z', 0, 0 };
  static const int ASCII[]  = { 0, 127, 0, 0 };
  static const int Blank[]  = { 9, 9, 32, 32, 0, 0 };
  static const int Cntrl[]  = { 0, 31, 127, 127, 0, 0 };
  static const int Digit[]  = { '0', '9', 0, 0 };
  static const int Graph[]  = { '!', '~', 0, 0 };
  static const int Lower[]  = { 'a', 'z', 0, 0 };
  static const int Print[]  = { ' ', '~', 0, 0 };
  static const int Punct[]  = { '!', '/', ':', '@', '[', '`', '{', '~', 0, 0 };
  static const int Space[]  = { 9, 13, 32, 32, 0, 0 };
  static const int Upper[]  = { 'A', 'Z', 0, 0 };
  static const int Word[]   = { '0', '9', 'A', 'Z', '_', '_', 'a', 'z', 0, 0 };
  static const int XDigit[] = { '0', '9', 'A', 'F', 'a', 'f', 0, 0 };

  range["Alnum"]               = Alnum;
  range["Alpha"]               = Alpha;
  range["ASCII"]               = ASCII;
  range["Blank"]  = range["h"] = Blank;
  range["Cntrl"]               = Cntrl;
  range["Digit"]  = range["d"] = Digit;
  range["Graph"]               = Graph;
  range["Lower"]  = range["l"] = Lower;
  range["Print"]               = Print;
  range["Punct"]               = Punct;
  range["Space"]  = range["s"] = Space;
  range["Upper"]  = range["u"] = Upper;
  range["Word"]   = range["w"] = Word;
  range["XDigit"] = range["x"] = XDigit;
}

static const Tables tables;

const int * range(const char *s)
{
  Tables::Range::const_iterator i = tables.range.find(s);
  if (i != tables.range.end())
    return i->second;
  return NULL;
}

}

}
