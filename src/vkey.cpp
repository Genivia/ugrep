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
@file      vkey.cpp
@brief     Virtual terminal keyboard input API - static, not thread safe
@author    Robert van Engelen - engelen@genivia.com
@copyright (c) 2019-2022, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt
*/

#include "vkey.hpp"
#include <reflex/utf8.h>

#ifdef VKEY_DOS
extern "C" int _getch(void);
#endif

// expand vkey support for non-portable META-PGUP/PGDN/HOME/END and META-F1/F2/F3/F4
// #define WITH_VKEY_ENABLE_EXPANDED

// fix ALT-LEFT and ALT-RIGHT e.g. with Mac OS Terminal, but this replaces ALT-b and ALT-f
// #define VKEY_FIX_META_LEFT
// #define VKEY_FIX_META_RIGHT

// use ALT-/ to enter Unicode character code followed by / or any non-hexdigit key, e.g. ALT-/3c0/ gives π
#ifndef VKEY_META_UNICODE
# define VKEY_META_UNICODE '/'
#endif

// rotate the key buffer
int VKey::rot_keybuf()
{
  unsigned char ch;

  ch = keybuf[0];
  keybuf[0] = keybuf[1];
  keybuf[1] = keybuf[2];
  keybuf[2] = 0;

  return ch;
}

#ifdef VKEY_DOS

// see https://docs.microsoft.com/en-us/previous-versions/visualstudio/visual-studio-6.0/aa299374(v=vs.60)

// wait until key press and return raw VKey::xxx code
int VKey::raw_get()
{
  int ch = _getch();

  if (ch == 0 || ch == 0xE0)
    ch = _getch();

  switch (ch)
  {
    case 10:  return LF;
    case 13:  return CR;
    case 59:  return FN(1);
    case 60:  return FN(2);
    case 61:  return FN(3);
    case 62:  return FN(4);
    case 63:  return FN(5);
    case 64:  return FN(6);
    case 65:  return FN(7);
    case 66:  return FN(8);
    case 67:  return FN(9);
    case 68:  return FN(10);
    case 71:  return HOME;
    case 72:  return UP;
    case 73:  return PGUP;
    case 75:  return LEFT;
    case 77:  return RIGHT;
    case 79:  return END;
    case 80:  return DOWN;
    case 81:  return PGDN;
    case 82:  keybuf[0] = DEL; return META; // INS
    case 83:  return DEL;
    case 115: return HOME; // CTRL-LEFT -> HOME
    case 116: return END; // CTRL-RIGHT -> END
    case 133: return FN(11);
    case 134: return FN(12);
    case 141: return PGUP; // CTRL-UP   -> PGUP
    case 145: return PGDN; // CTRL-DOWN -> PGDN
    case 147: keybuf[0] = DEL;   return META; // CTRL-DEL -> META-DEL
    case 148: keybuf[0] = TAB;   return META; // CTRL-TAB -> META-TAB
    case 152: keybuf[0] = UP;    return META;
    case 155: keybuf[0] = LEFT;  return META;
    case 157: keybuf[0] = RIGHT; return META;
    case 160: keybuf[0] = DOWN;  return META;
    case 163: keybuf[0] = DEL;   return META;
#ifdef WITH_VKEY_ENABLE_EXPANDED
    case 117: keybuf[0] = END;  return META; // CTRL-END
    case 118: keybuf[0] = PGDN; return META; // CTRL-PGDN
    case 119: keybuf[0] = HOME; return META; // CTRL-HOME
    case 134: keybuf[0] = PGUP; return META; // CTRL-PGUP
    case 151: keybuf[0] = HOME; return META; // META-HOME
    case 153: keybuf[0] = PGUP; return META; // META-PGUP
    case 159: keybuf[0] = END;  return META; // META-END
    case 161: keybuf[0] = PGDN; return META; // META-PGDN
    case 162: keybuf[0] = DEL;  return META; // META-DEL
#endif
    default: if (ch >= 1 && ch <= 126) return ch;
  }

  return 0;
}

// wait until key press and return raw VKey::xxx code, time out after timeout ms
int VKey::raw_in(int timeout)
{
  if (keybuf[0])
    return rot_keybuf();

  switch (WaitForSingleObject(hConIn, timeout))
  {
    case WAIT_OBJECT_0:
      if (!_kbhit())
      {
        FlushConsoleInputBuffer(hConIn);
        return 0;
      }
      return raw_get();

    case WAIT_TIMEOUT:
      return 0;

    default:
      return EOF;
  }
}

// wait until key press and return VKey::xxx code
int VKey::get()
{
  int ch;

  if (keybuf[0])
    return rot_keybuf();

  while ((ch = raw_get()) == 0)
    continue;

  return fn_key(ch);
}

// wait until key press and return VKey::xxx code, time out after timeout ms
int VKey::in(int timeout)
{
  if (keybuf[0])
    return rot_keybuf();

  return fn_key(raw_in(timeout));
}

#else

#ifdef VKEY_WIN

// wait until key press and return raw VKey::xxx code
int VKey::raw_get()
{
  DWORD nread = 0;
  INPUT_RECORD rec;

  while (true)
  {
    if (ReadConsoleInputW(hConIn, &rec, 1, &nread) == 0)
      return EOF;

    if (nread == 1 && rec.EventType == KEY_EVENT && rec.Event.KeyEvent.bKeyDown)
    {
      int wc = rec.Event.KeyEvent.uChar.UnicodeChar;

      while (wc == 0)
      {
        if (ReadConsoleInputW(hConIn, &rec, 1, &nread) == 0)
          return EOF;

        if (nread == 1 && rec.EventType == KEY_EVENT)
        {
          wc = rec.Event.KeyEvent.uChar.UnicodeChar;
          if (!rec.Event.KeyEvent.bKeyDown)
            break;
        }
      }

      // convert non-ASCII to UTF-8
      if (wc >= 0x80)
      {	
        // convert UTF-16LE surrogate pair (wc,ws)
        if (wc >= 0xD800 && wc < 0xE000)
        {
          if (ReadConsoleInputW(hConIn, &rec, 1, &nread) == 0)
            return EOF;

          if (nread == 1 && rec.EventType == KEY_EVENT && rec.Event.KeyEvent.bKeyDown)
          {
            int ws = rec.Event.KeyEvent.uChar.UnicodeChar;

            while (ws == 0)
            {
              if (ReadConsoleInputW(hConIn, &rec, 1, &nread) == 0)
                return EOF;

              if (nread == 1 && rec.EventType == KEY_EVENT)
              {
                ws = rec.Event.KeyEvent.uChar.UnicodeChar;
                if (rec.Event.KeyEvent.bKeyDown)
                  break;
              }
            }

            wc = 0x010000 - 0xDC00 + ((wc - 0xD800) << 10) + ws;
          }
        }

        // convert to UTF-8 and save result to keybuf
        char utf8[4];
        size_t n = reflex::utf8(wc, utf8);
        if (n > 0 && n <= 4)
        {
          wc = utf8[0];
          keybuf[0] = 0;
          keybuf[1] = 0;
          keybuf[2] = 0;
          memcpy(keybuf, &utf8[1], n - 1);
        }
      }

      return static_cast<unsigned char>(wc);
    }
  }
}

// wait until key press and return raw VKey::xxx code, time out after timeout ms
int VKey::raw_in(int timeout)
{
  DWORD nread = 0;
  INPUT_RECORD rec;

  switch (WaitForSingleObject(hConIn, timeout))
  {
    case WAIT_OBJECT_0:
      if (PeekConsoleInputW(hConIn, &rec, 1, &nread) != 0 &&
          nread == 1 &&
          rec.EventType == KEY_EVENT &&
          rec.Event.KeyEvent.bKeyDown)
        return raw_get();
  
      // discard event
      if (nread == 1)
        ReadConsoleInputW(hConIn, &rec, 1, &nread);
      return 0;

    case WAIT_TIMEOUT:
      return 0;

    default:
      return EOF;
  }
}

// poll the keys for timeout ms, return true if a key is pressed and is available to read
bool VKey::poll(int timeout)
{
  DWORD nread = 0;
  INPUT_RECORD rec;

  switch (WaitForSingleObject(hConIn, timeout))
  {
    case WAIT_OBJECT_0:
      if (PeekConsoleInputW(hConIn, &rec, 1, &nread) != 0 &&
          nread == 1 &&
          rec.EventType == KEY_EVENT &&
          rec.Event.KeyEvent.bKeyDown)
        return true;
  
      // discard event
      if (nread == 1)
        ReadConsoleInputW(hConIn, &rec, 1, &nread);
      return 0;

    case WAIT_TIMEOUT:
      return false;

    default:
      return true;
  }
}

#else

// wait until key press and return raw VKey::xxx code
int VKey::raw_get()
{
  char ch;

  if (read(tty, &ch, 1) <= 0)
    return EOF;

  return static_cast<unsigned char>(ch);
}

// wait until key press and return raw VKey::xxx code, time out after timeout ms
int VKey::raw_in(int timeout)
{
  char ch = 0;
  struct timeval tv;

  fd_set readfds;
  FD_ZERO(&readfds);
  FD_SET(tty, &readfds);
  tv.tv_sec = timeout / 1000;
  tv.tv_usec = (1000 * timeout) % 1000000;

  switch (select(tty + 1, &readfds, NULL, NULL, &tv))
  {
    case 0:
      return 0;

    case 1:
      if (read(tty, &ch, 1) == 1)
        return ch ? static_cast<unsigned char>(ch) : 32;
  }

  return EOF;
}

// poll the keys for timeout ms, return true if a key is pressed and is available to read
bool VKey::poll(int timeout)
{
  struct timeval tv;

  fd_set readfds;
  FD_ZERO(&readfds);
  FD_SET(tty, &readfds);
  tv.tv_sec = timeout / 1000;
  tv.tv_usec = (1000 * timeout) % 1000000;

  return select(tty + 1, &readfds, NULL, NULL, &tv) == 1;
}

#endif

// wait for next raw key press within VKey::TIMEOUT ms
int VKey::key()
{
  return VKey::raw_in(VKey::TIMEOUT);
}

// return VKey::META key press or META+/hex/ Unicode code point
int VKey::alt_key(int ch)
{
#ifdef VKEY_META_UNICODE

  if (ch == VKEY_META_UNICODE)
  {
    unsigned char *chp;
    int code = 0;

    do
    {
      ch = raw_get();

      if (ch >= 'A' && ch <= 'Z')
        ch -= 7;
      else if (ch >= 'a' && ch <= 'z')
        ch -= 39;

      if (ch < '0' || ch > '0' + 15)
      {
        (void)vkey(ch);
        break;
      }

      code = 16 * code + ch - '0';
    } while (code <= 0x10FFF);

    if (code < 0x80)
      return code;

    chp = keybuf;

    if (code < 0x0800)
    {
      ch = (0xC0 | ((code >> 6) & 0x1F));
    }
    else
    {
      if (code < 0x010000)
      {
        ch = (0xE0 | ((code >> 12) & 0x0F));
      }
      else
      {
        ch = (0xF0 | ((code >> 18) & 0x07));
        *chp++ = static_cast<unsigned char>(0x80 | ((code >> 12) & 0x3F));
      }
      *chp++ = static_cast<unsigned char>(0x80 | ((code >> 6) & 0x3F));
    }
    *chp++ = static_cast<unsigned char>(0x80 | (code & 0x3F));

    return ch;
  }

#endif

  if (ch >= 32 && ch <= 126 && alt[ch - 32][0] != 0)
  {
    keybuf[0] = alt[ch - 32][1];
    keybuf[1] = alt[ch - 32][2];
    keybuf[2] = alt[ch - 32][3];

    return alt[ch - 32][0];
  }

  keybuf[0] = ch;

  return META;
}

// return VKey::FN or VKey::META key press or META+/hex/ Unicode code point
int VKey::fn_key(int ch)
{
  if (ch >= 256 + '@' && ch < 256 + '@' + 12 && fn[ch - 256 - '@'][0] != '\0')
  {
    if (fn[ch - 256 - '@'][0] == META)
      return alt_key(fn[ch - 256 - '@'][1]);

    keybuf[0] = fn[ch - 256 - '@'][1];
    keybuf[1] = fn[ch - 256 - '@'][2];
    keybuf[2] = fn[ch - 256 - '@'][3];

    return fn[ch - 256 - '@'][0];
  }

  return ch;
}

// translate ANSI escape sequence starting with ch, return VKey::xxx key code
int VKey::ansi_esc(int ch)
{
  int ch2;

  switch (ch)
  {
    case 0:
      return VKey::ESC;
    case 13:
      return 10; // META-ENTER -> LF
    case 27:
      if (keybuf[0]) // nonzero when holding down ESC
      {
        flush();
        return 0;
      }
      keybuf[0] = ESC;
      ch = ansi_esc(key());
      if (ch == 0)
        return 0;
      keybuf[0] = ch;
      return META;
    case 'O':
      switch (ch = key())
      {
        case 0:
          return alt_key('O');
#ifdef WITH_VKEY_ENABLE_EXPANDED
        case '1':
          if (key() == ';')
            key(); // ignore '2'..'8'
          switch (key())
          {
            case 'P': return FN(9);  // META-FN1 -> FN9
            case 'Q': return FN(10); // META-FN2 -> FN10
            case 'R': return FN(11); // META-FN3 -> FN11
            case 'S': return FN(12); // META-FN4 -> FN12
          }
          return 0;
#endif
        case 'A': return UP;
        case 'B': return DOWN;
        case 'C': return RIGHT;
        case 'D': return LEFT;
        case 'F': return END;
        case 'H': return HOME;
        case 'P': return FN(1);
        case 'Q': return FN(2);
        case 'R': return FN(3);
        case 'S': return FN(4);
        case 'T': return FN(5);
        case 'U': return FN(6);
        case 'V': return FN(7);
        case 'W': return FN(8);
        case 'X': return FN(9);
        case 'Y': return FN(10);
        case 'Z': return FN(11);
        case '[': return FN(12);
      }
      return 0;
    case '[':
      switch (ch = key())
      {
        case 0:
          return alt_key('[');
        case '1':
          if ((ch = key()) == '~')
            return HOME;
          if ((ch2 = key()) == ';')
            ch2 = key();
          switch (ch2)
          {
            case '2': // SHIFT
            case '3': // META
            case '4': // META-SHIFT
              switch (key())
              {
                case 'A': keybuf[0] = UP;    return META; // META-UP
                case 'B': keybuf[0] = DOWN;  return META; // META-DOWN
                case 'C': keybuf[0] = RIGHT; return META; // META-RIGHT
                case 'D': keybuf[0] = LEFT;  return META; // META-LEFT
#ifdef WITH_VKEY_ENABLE_EXPANDED
                case 'F': keybuf[0] = END;   return META; // META-END
                case 'H': keybuf[0] = HOME;  return META; // META-HOME
#endif
              }
              return 0;
            case '5': // CTRL
            case '6': // CTRL-SHIFT
              switch (key())
              {
                case 'A': return PGUP; // CTRL-UP    -> PGUP
                case 'B': return PGDN; // CTRL-DOWN  -> PGDN
                case 'C': return END;  // CTRL-RIGHT -> END
                case 'D': return HOME; // CTRL-LEFT  -> HOME
#ifdef WITH_VKEY_ENABLE_EXPANDED
                case 'F': keybuf[0] = END;   return META; // CTRL-END  -> META-END
                case 'H': keybuf[0] = HOME;  return META; // CTRL-HOME -> META-HOME
#endif
              }
              return 0;
#ifdef WITH_VKEY_ENABLE_EXPANDED
            case '7': // META-CTRL
            case '8': // META-CTRL-SHIFT
              switch (key())
              {
                case 'A': keybuf[0] = PGUP; return META; // META-PGUP
                case 'B': keybuf[0] = PGDN; return META; // META-PGDN
                case 'C': keybuf[0] = END;  return META; // META-END
                case 'D': keybuf[0] = HOME; return META; // META-HOME
                case 'F': keybuf[0] = END;  return META; // META-END
                case 'H': keybuf[0] = HOME; return META; // META-HOME
              }
              return 0;
#endif
            case '~':
              switch (ch)
              {
                case '1': return FN(1);
                case '2': return FN(2);
                case '3': return FN(3);
                case '4': return FN(4);
                case '5': return FN(5);
                case '7': return FN(6);
                case '8': return FN(7);
                case '9': return FN(8);
              }
          }
          return 0;
        case '2':
          if ((ch = key()) == '~')
          {
            keybuf[0] = DEL;
            return META; // INS -> META-DEL
          }
          if (ch == ';' || (ch2 = key()) == ';')
          {
            key(); // ignore '2'..'8'
	    ch2 = key();
          }
          if (ch2 == '~')
          {
            switch (ch)
            {
              case '0': return FN(9);
              case '1': return FN(10);
              case '3': return FN(11);
              case '4': return FN(12);
#ifdef WITH_VKEY_ENABLE_EXPANDED
              case ';': return DEL; // META-INS -> DEL
#endif
            }
          }
          return 0;
        case '3':
          switch (key())
          {
            case ';':
              key(); // ignore '2'..'8'
              if (key() != '~')
		 return 0;
              keybuf[0] = DEL;
              return META;
            case '~': return DEL;
          }
          return 0;
        case '4':
          switch (key())
          {
#ifdef WITH_VKEY_ENABLE_EXPANDED
            case ';':
              key(); // ignore '2'..'8'
              if (key() != '~')
		 return 0;
              keybuf[0] = END;
              return META;
#endif
            case '~': return END;
          }
          return 0;
        case '5':
          switch (key())
          {
#ifdef WITH_VKEY_ENABLE_EXPANDED
            case ';':
              key(); // ignore '2'..'8'
              if (key() != '~')
		 return 0;
              keybuf[0] = PGUP;
              return META;
#endif
            case '~': return PGUP;
          }
          return 0;
        case '6':
          switch (key())
          {
#ifdef WITH_VKEY_ENABLE_EXPANDED
            case ';':
              key(); // ignore '2'..'8'
              if (key() != '~')
		 return 0;
	      keybuf[0] = PGDN;
	      return META;
#endif
            case '~': return PGDN;
          }
          return 0;
        case 'A': return UP;
        case 'B': return DOWN;
        case 'C': return RIGHT;
        case 'D': return LEFT;
        case 'F': return END;
        case 'H': return HOME;
        case 'Z': keybuf[0] = TAB; return META;
        case '[':
          switch (key())
          {
            case 'A': return FN(1);
            case 'B': return FN(2);
            case 'C': return FN(3);
            case 'D': return FN(4);
            case 'E': return FN(5);
          }
      }
      return 0;
  }

  return alt_key(ch);
}

// translate ANSI escape sequences, FN, and DEL keys
int VKey::vkey(int ch)
{
  switch (ch)
  {
    case 27:  return fn_key(ansi_esc(key()));
    case 127: return 8;
  }

  return ch;
}

// wait until key press and return VKey::xxx code
int VKey::get()
{
  int ch;

  if (keybuf[0])
    return rot_keybuf();

  while ((ch = vkey(raw_get())) == 0)
    continue;

  return ch;
}

// wait until key press and return VKey::xxx code, time out after timeout ms
int VKey::in(int timeout)
{
  if (keybuf[0])
    return rot_keybuf();

  return vkey(raw_in(timeout));
}

#endif

// flush the key buffer and remove pending key presses until no key is pressed
void VKey::flush()
{
  keybuf[0] = 0;
  keybuf[1] = 0;
  keybuf[2] = 0;

#ifdef OS_WIN

  FlushConsoleInputBuffer(hConIn);

#else

  while (key() > 0)
    continue;

#endif
}

// setup vkey in VKey::NORMAL tty or VKey::TTYRAW raw tty mode (cfmakeraw), returns 0 on success <0 on failure
bool VKey::setup(int mode)
{
#ifdef OS_WIN

  hConIn = CreateFileA("CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

  if (hConIn == INVALID_HANDLE_VALUE)
    return false;

  if (!GetConsoleMode(hConIn, &oldInMode))
    return false;

  DWORD inMode = oldInMode & ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT);

  if (mode == VKey::TTYRAW)
    inMode &= ~ENABLE_PROCESSED_INPUT;

  // get event when window is resized
  inMode |= ENABLE_WINDOW_INPUT;

#ifdef VKEY_WIN
  inMode |= ENABLE_VIRTUAL_TERMINAL_INPUT;
#endif

  if (!SetConsoleMode(hConIn, inMode))
    return false;

#ifdef CP_UTF8
  SetConsoleCP(CP_UTF8);
#endif

#else

  struct termios newterm;

  tty = open("/dev/tty", O_RDWR);

  if (tty < 0)
  {
    if (isatty(STDIN_FILENO) == 0)
      return false;
    tty = STDIN_FILENO;
  }

  fcntl(tty, F_SETFL, fcntl(tty, F_GETFL) & ~O_NONBLOCK);
  tcgetattr(tty, &oldterm);
  tcgetattr(tty, &newterm);

  if (mode == VKey::TTYRAW)
  {
#ifdef __sun // no cfmakeraw on SunOS/Solaris https://www.perkin.org.uk/posts/solaris-portability-cfmakeraw.html
    newterm.c_iflag &= ~(IMAXBEL|IGNBRK|BRKINT|PARMRK|ISTRIP|INLCR|IGNCR|ICRNL|IXON);
    newterm.c_oflag &= ~OPOST;
    newterm.c_lflag &= ~(ECHO|ECHONL|ICANON|ISIG|IEXTEN);
    newterm.c_cflag &= ~(CSIZE|PARENB);
    newterm.c_cflag |= CS8;
#else
    cfmakeraw(&newterm);
#endif
  }
  else
  {
    newterm.c_lflag &= ~(ECHO | ICANON);
  }

  tcsetattr(tty, TCSADRAIN, &newterm);

#endif

  flush();

  return true;
}

// release vkey resources and restore tty
void VKey::cleanup()
{
#ifdef OS_WIN

  if (hConIn != INVALID_HANDLE_VALUE)
  {
    SetConsoleMode(hConIn, oldInMode);
    CloseHandle(hConIn);
  }

#else

  tcsetattr(tty, TCSAFLUSH, &oldterm);

#endif
}

// assign ALT + key code 32..126 one or more keys stored in string
void VKey::map_alt_key(int key, const char *keys)
{
  if (key >= 32 && key <= 126)
  {
    if (keys == NULL)
    {
      alt[key - 32][0] = 0;
    }
    else
    {
      for (size_t i = 0; i < sizeof(*alt) && *keys; ++i)
      {
        alt[key - 32][i] = *keys;
        if (*keys++ == '\0')
          break;
      }
    }
  }
}

// assign FN 1..12 one or more keys stored in string
void VKey::map_fn_key(unsigned num, const char *keys)
{
  if (num >= 1 && num <= 12)
  {
    if (keys == NULL)
    {
      fn[num - 1][0] = 0;
    }
    else
    {
      for (size_t i = 0; i < sizeof(*fn); ++i)
      {
        fn[num - 1][i] = *keys;
        if (*keys++ == '\0')
          break;
      }
    }
  }
}

#ifdef OS_WIN

// Windows console state
HANDLE VKey::hConIn;
DWORD  VKey::oldInMode;

#else

// Unix/Linux terminal state
int            VKey::tty;
struct termios VKey::oldterm;

#endif

// key buffer to store up to three more keys after ALT/OPTION/META key was pressed
unsigned char VKey::keybuf[3];

// META/ALT/OPTION key mapping table, up to 4 UTF-8 bytes, not required to be \0-terminated
unsigned char VKey::alt[127 - 32][4] = {
  { 0xc2, 0xa0, 0x00 }, // ALT-SPACE -> NBSP
  { 0xe2, 0x81, 0x84 }, // ALT-! -> ⁄
  { 0xc3, 0x86, 0x00 }, // ALT-" -> Æ
  { 0xe2, 0x80, 0xb9 }, // ALT-# -> ‹
  { 0xe2, 0x80, 0xba }, // ALT-$ -> ›
  { 0xef, 0xac, 0x81 }, // ALT-% -> ﬁ
  { 0xe2, 0x80, 0xa1 }, // ALT-& -> ‡
  { 0xc3, 0xa6, 0x00 }, // ALT-' -> æ
  { 0xc2, 0xb7, 0x00 }, // ALT-( -> ·
  { 0xe2, 0x80, 0x9a }, // ALT-) -> ‚
  { 0xc2, 0xb0, 0x00 }, // ALT-* -> °
  { 0xc2, 0xb1, 0x00 }, // ALT-+ -> ±
  { 0xe2, 0x89, 0xa4 }, // ALT-, -> ≤
  { 0xe2, 0x80, 0x93 }, // ALT-- -> –
  { 0xe2, 0x89, 0xa5 }, // ALT-. -> ≥
  { 0xc3, 0xb7, 0x00 }, // ALT-/ -> ÷
  { 0xc2, 0xba, 0x00 }, // ALT-0 -> º
  { 0xc2, 0xa1, 0x00 }, // ALT-1 -> ¡
  { 0xe2, 0x84, 0xa2 }, // ALT-2 -> ™
  { 0xc2, 0xa3, 0x00 }, // ALT-3 -> £
  { 0xc2, 0xa2, 0x00 }, // ALT-4 -> ¢
  { 0xe2, 0x88, 0x9e }, // ALT-5 -> ∞
  { 0xc2, 0xa7, 0x00 }, // ALT-6 -> §
  { 0xc2, 0xb6, 0x00 }, // ALT-7 -> ¶
  { 0xe2, 0x80, 0xa2 }, // ALT-8 -> •
  { 0xc2, 0xaa, 0x00 }, // ALT-9 -> ª
  { 0xc3, 0x9a, 0x00 }, // ALT-: -> Ú
  { 0xe2, 0x80, 0xa6 }, // ALT-; -> …
  { 0xc2, 0xaf, 0x00 }, // ALT-< -> ¯
  { 0xe2, 0x89, 0xa0 }, // ALT-= -> ≠
  { 0xcb, 0x98, 0x00 }, // ALT-> -> ˘
  { 0xc2, 0xbf, 0x00 }, // ALT-? -> ¿
  { 0xe2, 0x82, 0xac }, // ALT-@ -> €
  { 0xc3, 0x85, 0x00 }, // ALT-A -> Å
  { 0xc4, 0xb1, 0x00 }, // ALT-B -> ı
  { 0xc3, 0x87, 0x00 }, // ALT-C -> Ç
  { 0xc3, 0x8e, 0x00 }, // ALT-D -> Î
  { 0xcb, 0x8a, 0x00 }, // acute accent ALT-E -> ´
  { 0xc3, 0x8f, 0x00 }, // ALT-F -> Ï
  { 0xcb, 0x9d, 0x00 }, // ALT-G -> ˝
  { 0xc3, 0x93, 0x00 }, // ALT-H -> Ó
  { 0xcb, 0x86, 0x00 }, // circumflex accent ALT-I -> ˆ
  { 0xc3, 0x94, 0x00 }, // ALT-J -> Ô
  { 0xef, 0xa3, 0xbf }, // ALT-K -> 
  { 0xc3, 0x92, 0x00 }, // ALT-L -> Ò
  { 0xc3, 0x82, 0x00 }, // ALT-M -> Â
  { 0xcb, 0x9c, 0x00 }, // tilde accent ALT-N -> ˜
  { 0xc3, 0x98, 0x00 }, // ALT-O -> Ø
  { 0xe2, 0x88, 0x8f }, // ALT-P -> ∏
  { 0xc5, 0x92, 0x00 }, // ALT-Q -> Œ
  { 0xe2, 0x80, 0xb0 }, // ALT-R -> ‰
  { 0xc3, 0x8d, 0x00 }, // ALT-S -> Í
  { 0xcb, 0x87, 0x00 }, // ALT-T -> ˇ
  { 0xc2, 0xa8, 0x00 }, // diaeresis accent ALT-U -> ¨
  { 0xe2, 0x97, 0x8a }, // ALT-V -> ◊
  { 0xe2, 0x80, 0x9e }, // ALT-W -> „
  { 0xcb, 0x9b, 0x00 }, // ALT-X -> ˛
  { 0xc3, 0x81, 0x00 }, // ALT-Y -> Á
  { 0xc2, 0xb8, 0x00 }, // ALT-Z -> ¸
  { 0xe2, 0x80, 0x9c }, // ALT-[ -> “
  { 0xc2, 0xab, 0x00 }, // ALT-\ -> «
  { 0xe2, 0x80, 0x98 }, // ALT-] -> ‘
  { 0xef, 0xac, 0x82 }, // ALT-^ -> ﬂ
  { 0xe2, 0x80, 0x94 }, // ALT-_ -> —
  { 0xcb, 0x8b, 0x00 }, // grave accent modifier ALT-` -> ˋ
  { 0xc3, 0xa5, 0x00 }, // ALT-a -> å
#ifdef VKEY_FIX_META_LEFT
  { 0x1F, 0x02, 0x00 }, // MacOS terminal: ALT-b = META-LEFT
#else
  { 0xe2, 0x88, 0xab }, // ALT-b -> ∫
#endif
  { 0xc3, 0xa7, 0x00 }, // ALT-c -> ç
  { 0xe2, 0x88, 0x82 }, // ALT-d -> ∂
  { 0xcb, 0x8a, 0x00 }, // acute accent modifier ALT-e -> ´
#ifdef VKEY_FIX_META_RIGHT
  { 0x1F, 0x06, 0x00 }, // MacOS terminal: ALT-f = META-RIGHT
#else
  { 0xc6, 0x92, 0x00 }, // ALT-f -> ƒ
#endif
  { 0xc2, 0xa9, 0x00 }, // ALT-g -> ©
  { 0xcb, 0x99, 0x00 }, // ALT-h -> ˙
  { 0xcb, 0x86, 0x00 }, // circumflex accent modifier ALT-i -> ˆ
  { 0xe2, 0x88, 0x86 }, // ALT-j -> ∆
  { 0xcb, 0x9a, 0x00 }, // ALT-k -> ˚
  { 0xc2, 0xac, 0x00 }, // ALT-l -> ¬
  { 0xc2, 0xb5, 0x00 }, // ALT-m -> µ
  { 0xcb, 0x9c, 0x00 }, // tilde accent modifier ALT-n -> ˜
  { 0xc3, 0xb8, 0x00 }, // ALT-o -> ø
  { 0xcf, 0x80, 0x00 }, // ALT-p -> π
  { 0xc5, 0x93, 0x00 }, // ALT-q -> œ
  { 0xc2, 0xae, 0x00 }, // ALT-r -> ®
  { 0xc3, 0x9f, 0x00 }, // ALT-s -> ß
  { 0xe2, 0x80, 0xa0 }, // ALT-t -> †
  { 0xc2, 0xa8, 0x00 }, // diaeresis accent modifier ALT-u -> ¨
  { 0xe2, 0x88, 0x9a }, // ALT-v -> √
  { 0xe2, 0x88, 0x91 }, // ALT-w -> ∑
  { 0xe2, 0x89, 0x88 }, // ALT-x -> ≈
  { 0xc2, 0xa5, 0x00 }, // ALT-y -> ¥
  { 0xce, 0xa9, 0x00 }, // ALT-z -> Ω
  { 0xe2, 0x80, 0x9d }, // ALT-{ -> ”
  { 0xc2, 0xbb, 0x00 }, // ALT-| -> »
  { 0xe2, 0x80, 0x99 }, // ALT-} -> ’
  { 0xcb, 0x9c, 0x00 }, // tilde accent ALT-~ -> ˜
};

// customizable FN key mapping table, up to 4 UTF-8 bytes, when entries are zero then 256+'A'..256+'L' are returned for FN1..FN12
unsigned char VKey::fn[12][4] = {
  { 0 }, // FN1  -> 321
  { 0 }, // FN2  -> 322
  { 0 }, // FN3  -> 323
  { 0 }, // FN4  -> 324
  { 0 }, // FN5  -> 325
  { 0 }, // FN6  -> 326
  { 0 }, // FN7  -> 327
  { 0 }, // FN8  -> 328
  { 0 }, // FN9  -> 329
  { 0 }, // FN10 -> 330
  { 0 }, // FN11 -> 331
  { 0 }, // FN12 -> 332
};
