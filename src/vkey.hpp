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
@file      vkey.hpp
@brief     Virtual terminal keyboard input API - static, not thread safe
@author    Robert van Engelen - engelen@genivia.com
@copyright (c) 2019,2025, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt

Supports Unix/Linux, MacOS, DOS, and Windows.

Key codes:

  -1      interrupted or error
  0       timed out

  32..126 ASCII characters

  31      META/ALT/OPTION/CTRL-_

  8       CTRL-H/BACKSPACE/DELETE
  9       CTRL-I/TAB and SHIFT-TAB is META-TAB
  10      CTRL-J/ENTER/RETURN
  13      CTRL-M/ENTER/RETURN (raw mode)
  27      CTRL-[/ESC
  127     DEL and INS is META-DEL (31 127)

  16      CTRL-P/UP
  14      CTRL-N/DOWN
  2       CTRL-B/LEFT
  6       CTRL-F/RIGHT

  7       CTRL-G/CTRL-UP/PGUP
  4       CTRL-D/CTRL-DOWN/PGDN
  1       CTRL-A/CTRL-LEFT/HOME
  5       CTRL-E/CTRL-RIGHT/END

  3       CTRL-C (requires VKEY_RAW)
  11      CTRL-K
  12      CTRL-L
  15      CTRL-O (requires VKEY_RAW)
  17      CTRL-Q (requires VKEY_RAW, reserved for XON/XOFF flow control)
  18      CTRL-R
  19      CTRL-S (requires VKEY_RAW, reserved for XON/XOFF flow control)
  20      CTRL-T
  21      CTRL-U
  22      CTRL-V (requires VKEY_RAW)
  23      CTRL-W
  24      CTRL-X
  25      CTRL-Y
  26      CTRL-Z/PAUSE
  28      CTRL-\ (BackSlash)
  29      CTRL-] (Right Bracket)
  30      CTRL-^ (Circumflex Accent)

Function keys (if not remapped with vkey_map_fn_key):

  FN1..FN12 -> 256+'A'..256+'L'

ALT/META (if not remapped with vkey_map_alt_key):

  ALT-SPACE -> NBSP
  ALT-! -> ⁄
  ALT-" -> Æ
  ALT-# -> ‹
  ALT-$ -> ›
  ALT-% -> ﬁ
  ALT-& -> ‡
  ALT-' -> æ
  ALT-( -> ·
  ALT-) -> ‚
  ALT-* -> °
  ALT-+ -> ±
  ALT-, -> ≤
  ALT-- -> –
  ALT-. -> ≥
  ALT-/ -> ÷
  ALT-0 -> º
  ALT-1 -> ¡
  ALT-2 -> ™
  ALT-3 -> £
  ALT-4 -> ¢
  ALT-5 -> ∞
  ALT-6 -> §
  ALT-7 -> ¶
  ALT-8 -> •
  ALT-9 -> ª
  ALT-: -> Ú
  ALT-; -> …
  ALT-< -> ¯
  ALT-= -> ≠
  ALT-> -> ˘
  ALT-? -> ¿
  ALT-@ -> €
  ALT-A -> Å
  ALT-B -> ı
  ALT-C -> Ç
  ALT-D -> Î
  acute accent ALT-E -> ´
  ALT-F -> Ï
  ALT-G -> ˝
  ALT-H -> Ó
  circumflex accent ALT-I -> ˆ
  ALT-J -> Ô
  ALT-K -> 
  ALT-L -> Ò
  ALT-M -> Â
  tilde accent ALT-N -> ˜
  ALT-O -> Ø
  ALT-P -> ∏
  ALT-Q -> Œ
  ALT-R -> ‰
  ALT-S -> Í
  ALT-T -> ˇ
  diaeresis accent ALT-U -> ¨
  ALT-V -> ◊
  ALT-W -> „
  ALT-X -> ˛
  ALT-Y -> Á
  ALT-Z -> ¸
  ALT-[ -> “
  ALT-\ -> «
  ALT-] -> ‘
  ALT-^ -> ﬂ
  ALT-_ -> —
  grave accent modifier ALT-` -> ˋ
  ALT-a -> å
  ALT-b -> ∫
  ALT-c -> ç
  ALT-d -> ∂
  acute accent modifier ALT-e -> ´
  ALT-f -> ƒ
  ALT-g -> ©
  ALT-h -> ˙
  circumflex accent modifier ALT-i -> ˆ
  ALT-j -> ∆
  ALT-k -> ˚
  ALT-l -> ¬
  ALT-m -> µ
  tilde accent modifier ALT-n -> ˜
  ALT-o -> ø
  ALT-p -> π
  ALT-q -> œ
  ALT-r -> ®
  ALT-s -> ß
  ALT-t -> †
  diaeresis accent modifier ALT-u -> ¨
  ALT-v -> √
  ALT-w -> ∑
  ALT-x -> ≈
  ALT-y -> ¥
  ALT-z -> Ω
  ALT-{ -> ”
  ALT-| -> »
  ALT-} -> ’
  tilde accent ALT-~ -> ˜

Unicode code point entry:

  ALT-/ hex-digits /

*/

#ifndef VKEY_HPP
#define VKEY_HPP

#include "ugrep.hpp"

#ifdef OS_WIN

#ifdef ENABLE_VIRTUAL_TERMINAL_INPUT
# define VKEY_WIN
#else
# define VKEY_DOS
#endif

#else

#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/select.h>

#endif

class VKey {

 public:

  static const int NORMAL   = 0;   // VKey::setup in tty normal mode
  static const int TTYRAW   = 1;   // VKey::setup in tty raw mode (cfmakeraw)

  static const int META     = 31;  // META/ALT/OPTION/CTRL-_

  static const int BS       = 8;   // CTRL-H/BACKSPACE/DELETE
  static const int TAB      = 9;   // CTRL-I/TAB and SHIFT-TAB is META-TAB
  static const int LF       = 10;  // CTRL-J/ENTER/RETURN
  static const int CR       = 13;  // CTRL-M/ENTER/RETURN (raw mode)
  static const int ESC      = 27;  // CTRL-[/ESC
  static const int DEL      = 127; // DEL and INS is META-DEL

  static const int UP       = 16;  // CTRL-P/UP
  static const int DOWN     = 14;  // CTRL-N/DOWN
  static const int LEFT     = 2;   // CTRL-B/LEFT
  static const int RIGHT    = 6;   // CTRL-F/RIGHT

  static const int PGUP     = 7;   // CTRL-G/CTRL-UP/PGUP
  static const int PGDN     = 4;   // CTRL-D/CTRL-DOWN/PGDN
  static const int HOME     = 1;   // CTRL-A/CTRL-LEFT/HOME
  static const int END      = 5;   // CTRL-E/CTRL-RIGHT/END

  static const int CTRL_C   = 3;   // CTRL-C (requires VKEY_RAW)
  static const int CTRL_K   = 11;  // CTRL-K
  static const int CTRL_L   = 12;  // CTRL-L
  static const int CTRL_O   = 15;  // CTRL-O (requires VKEY_RAW)
  static const int CTRL_Q   = 17;  // CTRL-Q (requires VKEY_RAW, reserved for XON/XOFF flow control)
  static const int CTRL_R   = 18;  // CTRL-R
  static const int CTRL_S   = 19;  // CTRL-S (requires VKEY_RAW, reserved for XON/XOFF flow control)
  static const int CTRL_T   = 20;  // CTRL-T
  static const int CTRL_U   = 21;  // CTRL-U
  static const int CTRL_V   = 22;  // CTRL-V (requires VKEY_RAW)
  static const int CTRL_W   = 23;  // CTRL-W
  static const int CTRL_X   = 24;  // CTRL-X
  static const int CTRL_Y   = 25;  // CTRL-Y
  static const int CTRL_Z   = 26;  // CTRL-Z/PAUSE
  static const int CTRL_BS  = 28;  // CTRL-\ (BackSlash)
  static const int CTRL_RB  = 29;  // CTRL-] (Right Bracket)
  static const int CTRL_CA  = 30;  // CTRL-^ (Circumflex Accent)

  static constexpr int FN(int num)
  {
    return 255 + '@' + num;        // FN1..FN12 is 256+'A'..256+'L'
  }

  // setup vkey in VKey::NORMAL tty or VKey::TTYRAW raw tty mode (cfmakeraw), returns 0 on success <0 on failure
  static bool setup(int mode = NORMAL);

  // release vkey resources and restore tty
  static void cleanup();

  // wait until key press and return VKey::xxx code
  static int get();

  // wait until key press and return VKey::xxx code, time out after timeout ms
  static int in(int timeout);

  // wait until key press and return unmapped VKey::xxx code
  static int raw_get();

  // wait until key press and return unmapped VKey::xxx code, time out after timeout ms
  static int raw_in(int timeout);

  // poll the keys for timeout ms, return true if a key is pressed and is available to read
  static bool poll(int timeout);

  // flush the key buffer and remove pending key presses until no key is pressed
  static void flush();

  // assign ALT + key code 32..126 one to four keys/chars stored in string
  static void map_alt_key(int key, const char *keys);

  // assign FN 1..12 one to four keys/chars stored in string
  static void map_fn_key(unsigned num, const char *keys);

 protected:

  // default 50ms timeout to allow the key queue to be filled with ANSI escape codes
  static const int TIMEOUT = 50;

  // rotate the key buffer
  static int rot_keybuf();

  // translate ANSI escape sequences, FN, and DEL keys
  static int vkey(int ch);

  // wait for next raw key press within VKey::TIMEOUT ms
  static int key();

  // return VKey::META key press or META+/hex/ Unicode code point
  static int alt_key(int ch);

  // return VKey::FN or VKey::META key press or META+/hex/ Unicode code point
  static int fn_key(int ch);

  // translate ANSI escape sequences, FN, and DEL keys
  static int ansi_esc(int ch);

#ifdef OS_WIN

  // Windows console state
  static HANDLE hConIn;
  static DWORD  oldInMode;
  static UINT   oldOutputCP;

#else

  // Unix/Linux terminal state
  static int            tty;
  static struct termios oldterm;

#endif

  // key buffer to store up to three more keys after ALT/OPTION/META key was pressed
  static unsigned char keybuf[3];

  // META/ALT/OPTION key mapping table, up to 4 UTF-8 bytes, not required to be \0-terminated
  static unsigned char alt[127 - 32][4];

  // customizable FN key mapping table, up to 4 UTF-8 bytes, when entries are zero then 256+'A'..256+'L' are returned for FN1..FN12
  static unsigned char fn[12][4];

};

#endif
