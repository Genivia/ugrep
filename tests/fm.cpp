#ifndef REFLEX_CODE_DECL
#include <reflex/pattern.h>
#define REFLEX_CODE_DECL const reflex::Pattern::Opcode
#endif

extern REFLEX_CODE_DECL reflex_code_FSM[11] =
{
  0x61610002, // 0: GOTO 2 ON 'a'
  0x00FFFFFF, // 1: HALT
  0x64640004, // 2: GOTO 4 ON 'd'
  0x00FFFFFF, // 3: HALT
  0x69690006, // 4: GOTO 6 ON 'i'
  0x00FFFFFF, // 5: HALT
  0x617A0008, // 6: GOTO 8 ON 'a'-'z'
  0x00FFFFFF, // 7: HALT
  0xFF000001, // 8: TAKE 1
  0x617A0008, // 9: GOTO 8 ON 'a'-'z'
  0x00FFFFFF, // 10: HALT
};

