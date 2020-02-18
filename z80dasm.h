#ifndef z80dasm_h
#define z80dasm_h

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

typedef enum {
  op_ADC,
  op_ADD,
  op_AND,
  op_BIT,
  op_CALL,
  op_CCF,
  op_CP,
  op_CPD,
  op_CPDR,
  op_CPI,
  op_CPIR,
  op_CPL,
  op_DAA,
  op_DEC,
  op_DI,
  op_DJNZ,
  op_EI,
  op_EX,
  op_EXX,
  op_HALT,
  op_IM,
  op_IN,
  op_INC,
  op_IND,
  op_INDR,
  op_INI,
  op_INIR,
  op_JP,
  op_JR,
  op_LD,
  op_LDD,
  op_LDDR,
  op_LDI,
  op_LDIR,
  op_NEG,
  op_NOP,
  op_OR,
  op_OUT,
  op_OUTD,
  op_OTDR,
  op_OUTI,
  op_OTIR,
  op_POP,
  op_PUSH,
  op_RES,
  op_RET,
  op_RETI,
  op_RETN,
  op_RL,
  op_RLA,
  op_RLC,
  op_RLD,
  op_RLCA,
  op_RST,
  op_RR,
  op_RRA,
  op_RRC,
  op_RRD,
  op_RRCA,
  op_SBC,
  op_SCF,
  op_SET,
  op_SLA,
  op_SRA,
  op_SRL,
  op_SUB,
  op_XOR
} Z80_Operation;

typedef enum {
  reg_A,
  reg_B,
  reg_C,
  reg_D,
  reg_E,
  reg_H,
  reg_L,
  reg_AF,
  reg_BC,
  reg_DE,
  reg_HL,
  reg_SP,
  reg_IX,
  reg_IY,
  reg_AAF,
  reg_I,
  reg_R,
  fl_NZ,
  fl_Z,
  fl_NC,
  fl_C,
  fl_PO,
  fl_PE,
  fl_P,
  fl_M
} Z80_Operand;

typedef enum {
  am_Register = 0x1,
  am_Immediate = 0x2,
  am_Extended = 0x4,
  am_Indirect = 0x8,
  am_Indexed = 0x10,
  am_Flag = 0x20,
  am_Implicit = 0x40,
  am_Displacement = 0x80
} Z80_AddressingMode;

typedef struct {
  uint8_t flags;
  uint16_t v;
  uint8_t displacement; 
} Z80_Arg;

typedef struct {
  uint8_t len;
  Z80_Operation operation;
  int argc;
  Z80_Arg args[2];
} Z80_OpCode;


#if defined(__cplusplus)
extern "C" {
#endif

Z80_OpCode* z80_disassemble(uint8_t* mem);

const char* z80_to_string(Z80_OpCode* opcode);

void z80_free(Z80_OpCode* opcode);

#if defined(__cplusplus)
}
#endif

#endif /* z80dasm_h */
