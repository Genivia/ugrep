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
@file      unicode.cpp
@brief     Get Unicode character class ranges and UTF-8 regex translations
@author    Robert van Engelen - engelen@genivia.com
@copyright (c) 2016-2020, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt
*/

#include <reflex/unicode.h>
#include <reflex/utf8.h>

namespace reflex {

namespace Unicode {

Tables::Tables()
{
  block_scripts();
  language_scripts();
  letter_scripts();

  range["Other"]                  = range["C"];
  range["Letter"]                 = range["L"];
  range["Mark"]                   = range["M"];
  range["Number"]                 = range["N"];
  range["Punctuation"]            = range["P"];
  range["Symbol"]                 = range["S"];
  range["Separator"]              = range["Z"];
  range["Lowercase_Letter"]       = range["Ll"];
  range["Uppercase_Letter"]       = range["Lu"];
  range["Titlecase_Letter"]       = range["Lt"];
  range["Modifier_Letter"]        = range["Lm"];
  range["Other_Letter"]           = range["Lo"];
  range["Non_Spacing_Mark"]       = range["Mn"];
  range["Spacing_Combining_Mark"] = range["Mc"];
  range["Enclosing_Mark"]         = range["Me"];
  range["Space_Separator"]        = range["Zs"];
  range["Line_Separator"]         = range["Zl"];
  range["Paragraph_Separator"]    = range["Zp"];
  range["Math_Symbol"]            = range["Sm"];
  range["Currency_Symbol"]        = range["Sc"];
  range["Modifier_Symbol"]        = range["Sk"];
  range["Other_Symbol"]           = range["So"];
  range["Decimal_Digit_Number"]   = range["Nd"];
  range["Letter_Number"]          = range["Nl"];
  range["Other_Number"]           = range["No"];
  range["Dash_Punctuation"]       = range["Pd"];
  range["Open_Punctuation"]       = range["Ps"];
  range["Close_Punctuation"]      = range["Pe"];
  range["Initial_Punctuation"]    = range["Pi"];
  range["Final_Punctuation"]      = range["Pf"];
  range["Connector_Punctuation"]  = range["Pc"];
  range["Other_Punctuation"]      = range["Po"];
  range["Control"]                = range["Cc"];
  range["Format"]                 = range["Cf"];

  range["d"] = range["Decimal_Digit_Number"];
  range["l"] = range["Lowercase_Letter"];
  range["u"] = range["Uppercase_Letter"];
  range["s"] = range["Space"];
  range["w"] = range["Word"];
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
