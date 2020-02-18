#include <string.h>
#include <sys/types.h>

#include "z80dasm.h"

static const char* s_mnemonics[] = {
  "ADC", "ADD", "AND", "BIT", "CALL", "CCF", "CP", "CPD",
  "CPDR", "CPI", "CPIR", "CPL", "DAA", "DEC", "DI", "DJNZ",
  "EI", "EX", "EXX", "HALT", "IM", "IN",
  "INC", "IND", "INDR", "INI", "INIR", "JP", "JR", "LD",
  "LDD", "LDDR", "LDI", "LDIR", "NEG", "NOP", "OR", "OUT",
  "OUTD", "OTDR", "OUTI", "OTIR", "POP", "PUSH", "RES", "RET",
  "RETI", "RETN", "RL", "RLA", "RLC", "RLD", "RLCA", "RST",
  "RR", "RRA", "RRC", "RRD", "RRCA", "SBC", "SCF", "SET",
  "SLA", "SRA", "SRL", "SUB", "XOR" 
};

static const char* s_operands[] = {
  "A", "B", "C", "D", "E", "H", "L", "AF", "BC", "DE", "HL",
  "SP", "IX", "IY", "AF'", "I", "R", "NZ", "Z", "NC", "C", "PO", "PE", 
  "P", "M"
};

static uint8_t reg_r[] = { 
  reg_B, reg_C, reg_D, reg_E, reg_H, reg_L, 0xff, reg_A 
};

static uint8_t reg_ss[] = { reg_BC, reg_DE, reg_HL, reg_SP };
static uint8_t reg_qq[] = { reg_BC, reg_DE, reg_HL, reg_AF };
static uint8_t flags[] = { fl_NZ, fl_Z, fl_NC, fl_C, fl_PO, fl_PE, fl_P, fl_M };

static int argLength(Z80_Arg arg) {
  if ((arg.flags & (am_Indexed | am_Indirect)) == (am_Indexed | am_Indirect)) return 1;
  if ((arg.flags & am_Immediate) != 0) return (arg.flags & am_Extended) == 0 ? 1 : 2;
  return 0; 
}

static Z80_OpCode* opCode0(int level, Z80_Operation op) {
  Z80_OpCode* opcode = (Z80_OpCode *) malloc(sizeof(Z80_OpCode));
  opcode->len = level + 1;
  opcode->argc = 0;
  opcode->operation = op;
  return opcode;
}

static Z80_OpCode* opCode1(int level, Z80_Operation op, Z80_Arg arg) {
  Z80_OpCode* opcode = opCode0(level, op);
  opcode->len += argLength(arg);
  opcode->argc = 1;
  opcode->args[0] = arg;
  return opcode;
}

static Z80_OpCode* opCode2(int level, Z80_Operation op, 
    Z80_Arg arg_a, Z80_Arg arg_b) {
  Z80_OpCode* opcode = opCode0(level, op);
  opcode->len += argLength(arg_a) + argLength(arg_b);
  opcode->argc = 2;
  opcode->args[0] = arg_a;
  opcode->args[1] = arg_b;
  return opcode;
}

static Z80_Arg flag_f(uint8_t f) {
  Z80_Arg arg;
  arg.flags = am_Flag;
  arg.v = flags[f & 0x7];  
  return arg;
}

static Z80_Arg register_explicit(Z80_Operand reg) {
  Z80_Arg arg;
  arg.flags = am_Register;
  arg.v = reg;
  return arg;
}

static Z80_Arg register_indirect(Z80_Operand reg) {
  Z80_Arg arg;
  arg.flags = am_Register | am_Indirect;
  arg.v = reg;
  return arg;
}

static Z80_Arg register_r(Z80_Operand ireg, uint8_t displacement, uint8_t r) {
  Z80_Arg arg;
  arg.flags = am_Register;
  if ((r & 0x7) == 6) {
    if (ireg != reg_HL) {
      arg.flags |= am_Indexed;
    }
    arg.flags |= am_Indirect;
    arg.v = ireg;
    arg.displacement = displacement;
  }
  else {
    arg.v = reg_r[r & 0x7];
  }
  return arg;
}

static Z80_Arg register_ss(Z80_Operand ireg, uint8_t ss) {
  Z80_Arg arg;
  arg.flags = am_Register;
  arg.v = ((ss & 0x3) == 2) ? ireg : reg_ss[ss & 0x3];
  return arg;
}

static Z80_Arg register_qq(Z80_Operand ireg, uint8_t qq) {
  Z80_Arg arg;
  arg.flags = am_Register;
  arg.v = ((qq & 0x3) == 2) ? ireg : reg_qq[qq & 0x3];
  return arg;
}

static Z80_Arg relative_address(uint8_t rel) {
  Z80_Arg arg;
  arg.flags = am_Immediate | am_Displacement;
  arg.v = rel;
  return arg;
}

static Z80_Arg absolute_address(uint8_t lsb, uint8_t msb) {
  Z80_Arg arg;
  arg.flags = am_Immediate | am_Extended;
  arg.v = (msb<<8) | lsb;
  return arg;  
}

static Z80_Arg indirect_address(uint8_t lsb, uint8_t msb) {
  Z80_Arg arg;
  arg.flags = am_Immediate | am_Extended | am_Indirect;
  arg.v = (msb<<8) | lsb;
  return arg;  
}

static Z80_Arg immediate(uint8_t b) {
  Z80_Arg arg;
  arg.flags = am_Immediate;
  arg.v = b;
  return arg;
}

static Z80_Arg zero_page_address(uint8_t b) {
  Z80_Arg arg;
  arg.flags = am_Implicit | am_Flag;
  arg.v = b;
  return arg;  
}

static Z80_Arg literal(uint8_t b) {
  Z80_Arg arg;
  arg.flags = am_Implicit;
  arg.v = b;
  return arg;  
}

static Z80_OpCode* disassemblePageXX(int level, Z80_Operand ireg, uint8_t* mem);
static Z80_OpCode* disassemblePageCB(int level, Z80_Operand ireg, uint8_t* mem);
static Z80_OpCode* disassemblePageED(int level, Z80_Operand ireg, uint8_t* mem);

static Z80_OpCode* disassembleSection0(int level, Z80_Operand ireg, uint8_t* mem) {
  uint8_t op = mem[0];
  switch (op & 0x7) {
    case 0:
      switch ((op>>3) & 0x7) {
        case 0:
          return opCode0(level, op_NOP);
        case 1:
          return opCode2(level, op_EX, register_explicit(reg_AF), register_explicit(reg_AAF));
        case 2:
          return opCode1(level, op_DJNZ, relative_address(mem[1]));
        case 3:
          return opCode1(level, op_JR, relative_address(mem[1]));
        default:
          return opCode2(level, op_JR, flag_f((op>>3) & 0x3), relative_address(mem[1]));
      }
    case 1:
      if (((op>>3) & 0x1) == 0) {
        return opCode2(level, op_LD, register_ss(ireg, (op>>4) & 0x3), absolute_address(mem[1], mem[2]));
      }
      else {
        return opCode2(level, op_ADD, register_explicit(ireg), register_ss(ireg, (op>>4) & 0x3));
      }
    case 2:
      switch ((op>>3) & 0x7) {
        case 0:
          return opCode2(level, op_LD, register_indirect(reg_BC), register_explicit(reg_A));
        case 1:
          return opCode2(level, op_LD, register_explicit(reg_A), register_indirect(reg_BC));
        case 2:
          return opCode2(level, op_LD, register_indirect(reg_DE), register_explicit(reg_A));
        case 3:
          return opCode2(level, op_LD, register_explicit(reg_A), register_indirect(reg_DE));
        case 4:
          return opCode2(level, op_LD, indirect_address(mem[1], mem[2]), register_explicit(ireg));
        case 5:
          return opCode2(level, op_LD, register_explicit(ireg), indirect_address(mem[1], mem[2]));
        case 6:
          return opCode2(level, op_LD, indirect_address(mem[1], mem[2]), register_explicit(reg_A));
        case 7:
          return opCode2(level, op_LD, register_explicit(reg_A), indirect_address(mem[1], mem[2]));
      }
    case 3:
      return opCode1(level, ((op>>3) & 0x1) == 0 ? op_INC : op_DEC, register_ss(ireg, (op>>4) & 3));
    case 4:
      return opCode1(level, op_INC, register_r(ireg, mem[1], (op>>3) & 0x7));
    case 5:
      return opCode1(level, op_DEC, register_r(ireg, mem[1], (op>>3) & 0x7));
    case 6:
      return opCode2(level, op_LD, register_r(ireg, mem[1], (op>>3) & 0x7), immediate(mem[1]));
    case 7:
      switch ((op>>3) & 0x7) {
        case 0:
          return opCode0(level, op_RLCA);
        case 1:
          return opCode0(level, op_RRCA);
        case 2:
          return opCode0(level, op_RLA);
        case 3:
          return opCode0(level, op_RRA);
        case 4:
          return opCode0(level, op_DAA);
        case 5:
          return opCode0(level, op_CPL);
        case 6:
          return opCode0(level, op_SCF);
        case 7:
          return opCode0(level, op_CCF);
      }
  }
  return NULL;
}

static Z80_OpCode* disassembleSection1(int level, Z80_Operand ireg, uint8_t* mem) {
  uint8_t op = mem[0];
  if (op == 0x76) {
    return opCode0(level, op_HALT);
  }
  return opCode2(level, op_LD, register_r(ireg, mem[1], (op>>3) & 0x7), 
      register_r(ireg, mem[1], op & 0x7));
}

static Z80_OpCode* disassembleSection2(int level, Z80_Operand ireg, uint8_t* mem) {
  uint8_t op = mem[0];
  switch ((op>>3) & 0x7) {
    case 0:
      return opCode2(level, op_ADD, register_explicit(reg_A), register_r(ireg, mem[1], op & 0x7));
    case 1:
      return opCode2(level, op_ADC, register_explicit(reg_A), register_r(ireg, mem[1], op & 0x7));
    case 2:
      return opCode1(level, op_SUB, register_r(ireg, mem[1], op & 0x7));
    case 3:
      return opCode2(level, op_SBC, register_explicit(reg_A), register_r(ireg, mem[1], op & 0x7));
    case 4:
      return opCode1(level, op_AND, register_r(ireg, mem[1], op & 0x7));
    case 5:
      return opCode1(level, op_XOR, register_r(ireg, mem[1], op & 0x7));
    case 6:
      return opCode1(level, op_OR, register_r(ireg, mem[1], op & 0x7));
    case 7:
      return opCode1(level, op_CP, register_r(ireg, mem[1], op & 0x7));
  }
  return NULL;
}

static Z80_OpCode* disassembleSection3(int level, Z80_Operand ireg, uint8_t* mem) {
  uint8_t op = mem[0];
  switch (op & 0x7) {
    case 0:
      return opCode1(level, op_RET, flag_f((op>>3) & 0x7));
    case 1:
      if (((op>>3) & 0x1) == 0) {
        return opCode1(level, op_POP, register_qq(ireg, (op>>4) & 0x3));
      }
      else {
        switch ((op>>4) & 0x3) {
          case 0:
            return opCode0(level, op_RET);
          case 1:
            return opCode0(level, op_EXX);
          case 2:
            return opCode1(level, op_JP, register_indirect(ireg));
          case 3:
            return opCode2(level, op_LD, register_explicit(reg_SP), register_explicit(ireg));
        }
      }
    case 2:
      return opCode2(level, op_JP, flag_f((op>>3) & 0x7), absolute_address(mem[1], mem[2]));      
    case 3:
      switch ((op>>3) & 0x7) {
        case 0:
          return opCode1(level, op_JP, absolute_address(mem[1], mem[2]));
        case 1:
          return disassemblePageCB(level + 1, ireg, mem + 1);
        case 2:
          return opCode2(level, op_OUT, immediate(mem[1]), register_explicit(reg_A));
        case 3:
          return opCode2(level, op_IN, register_explicit(reg_A), immediate(mem[1]));
        case 4:
          return opCode2(level, op_EX, register_indirect(reg_SP), register_explicit(ireg));
        case 5:
          return opCode2(level, op_EX, register_explicit(reg_DE), register_explicit(reg_HL));
        case 6:
          return opCode0(level, op_DI);
        case 7:
          return opCode0(level, op_EI);
      }
    case 4:
      return opCode2(level, op_CALL, flag_f((op>>3) & 0x7), absolute_address(mem[1], mem[2]));      
    case 5:
      if (((op>>3) & 0x1) == 0) {
        return opCode1(level, op_PUSH, register_qq(ireg, (op>>4) & 0x3));
      }
      else {
        switch ((op>>4) & 0x3) {
          case 0:
            return opCode1(level, op_CALL, absolute_address(mem[1], mem[2]));
          case 1:
            return disassemblePageXX(level + 1, reg_IX, mem + 1);
          case 2:
            return disassemblePageED(level + 1, ireg, mem + 1);
          case 3:
            return disassemblePageXX(level + 1, reg_IY, mem + 1);
        }
      }
    case 6:
      switch ((op>>3) & 0x7) {
        case 0:
          return opCode2(level, op_ADD, register_explicit(reg_A), immediate(mem[1]));
        case 1:
          return opCode2(level, op_ADC, register_explicit(reg_A), immediate(mem[1]));
        case 2:
          return opCode1(level, op_SUB, immediate(mem[1]));
        case 3:
          return opCode2(level, op_SBC, register_explicit(reg_A), immediate(mem[1]));
        case 4:
          return opCode1(level, op_AND, immediate(mem[1]));
        case 5:
          return opCode1(level, op_XOR, immediate(mem[1]));
        case 6:
          return opCode1(level, op_OR, immediate(mem[1]));
        case 7:
          return opCode1(level, op_CP, immediate(mem[1]));
      }
    case 7:
      return opCode1(level, op_RST, zero_page_address(8 * ((op>>3) & 0x7)));
  }
  return NULL;
}

static Z80_OpCode* disassemblePageXX(int level, Z80_Operand ireg, uint8_t* mem) {
  uint8_t op = mem[0];
  if ((op & 0xc0) == 0) {
    return disassembleSection0(level, ireg, mem);
  }
  else if ((op & 0xc0) == 0x40) {
    return disassembleSection1(level, ireg, mem);    
  }
  else if ((op & 0xc0) == 0x80) {
    return disassembleSection2(level, ireg, mem);        
  }
  else {
    return disassembleSection3(level, ireg, mem);        
  }  
}

static Z80_OpCode* disassemblePageCB(int level, Z80_Operand ireg, uint8_t* mem) {
  int indexed = ireg != reg_HL;
  uint8_t op = indexed ? mem[1] : mem[0];
  switch ((op >> 6) & 0x3) {
    case 0:
      switch ((op>>3) & 0x7) {
        case 0:
          return opCode1(level, op_RLC, register_r(ireg, mem[0], op & 0x7));
        case 1:
          return opCode1(level, op_RRC, register_r(ireg, mem[0], op & 0x7));
        case 2:
          return opCode1(level, op_RL, register_r(ireg, mem[0], op & 0x7));
        case 3:
          return opCode1(level, op_RR, register_r(ireg, mem[0], op & 0x7));
        case 4:
          return opCode1(level, op_SLA, register_r(ireg, mem[0], op & 0x7));
        case 5:
          return opCode1(level, op_SRA, register_r(ireg, mem[0], op & 0x7));
        case 6:
          return NULL;
        case 7:  
          return opCode1(level, op_SRL, register_r(ireg, mem[0], op & 0x7));
      }
    case 1:
      return opCode2(level, op_BIT, literal((op>>3) & 0x7), register_r(ireg, mem[0], op & 0x7));
    case 2:
      return opCode2(level, op_RES, literal((op>>3) & 0x7), register_r(ireg, mem[0], op & 0x7));
    case 3:
      return opCode2(level, op_SET, literal((op>>3) & 0x7), register_r(ireg, mem[0], op & 0x7));
  }

  return NULL;
}
 
static Z80_OpCode* disassemblePageED(int level, Z80_Operand ireg, uint8_t* mem) {
  uint8_t op = mem[0];
  switch ((op>>6) & 0x3) {
    case 1:
      switch (op & 0x7) {
        case 0:
          if (((op>>3) & 0x7) == 6) return NULL;
          return opCode2(level, op_IN, register_r(ireg, 0, (op>>3) & 0x7), register_indirect(reg_C));
        case 1:
          if (((op>>3) & 0x7) == 6) return NULL;
          return opCode2(level, op_OUT, register_indirect(reg_C), register_r(ireg, 0, (op>>3) & 0x7));
        case 2:
          if (((op>>3) & 0x1) == 0) {
            return opCode2(level, op_SBC, register_explicit(reg_HL), register_ss(ireg, (op>>4) & 0x3));
          }
          else {
            return opCode2(level, op_ADC, register_explicit(reg_HL), register_ss(ireg, (op>>4) & 0x3));        
          }
        case 3:
          if (((op>>3) & 0x1) == 0) {
            return opCode2(level, op_LD, indirect_address(mem[1], mem[2]), register_ss(ireg, (op>>4) & 0x3));
          }
          else {
            return opCode2(level, op_LD, register_ss(ireg, (op>>4) & 0x3), indirect_address(mem[1], mem[2]));        
          }
        case 4:
          return ((op>>3) & 0x7) == 0 ? opCode0(level, op_NEG) : NULL;
        case 5:
          if (((op>>3) & 0x7) > 1) return NULL;
          return ((op>>3) & 0x7) ? opCode0(level, op_RETI) : opCode0(level, op_RETN);
        case 6:
          switch ((op>>3) & 0x7) {
            case 0:
              return opCode1(level, op_IM, literal(0));
            case 2:
              return opCode1(level, op_IM, literal(1));
            case 3:
              return opCode1(level, op_IM, literal(2));
            default:
              return NULL;
          }
        case 7:
          switch ((op>>3) & 0x7) {
            case 0:
              return opCode2(level, op_LD, register_explicit(reg_I), register_explicit(reg_A));
            case 1:
              return opCode2(level, op_LD, register_explicit(reg_R), register_explicit(reg_A));
            case 2:
              return opCode2(level, op_LD, register_explicit(reg_A), register_explicit(reg_I));
            case 3:
              return opCode2(level, op_LD, register_explicit(reg_A), register_explicit(reg_R));
            case 4:
              return opCode0(level, op_RRD);          
            case 5:
              return opCode0(level, op_RLD);          
            default:
              return NULL;
          }
      }
    case 2:
      switch (op & 0x7) {
        case 0:
          switch ((op>>3) & 0x7) {
            case 4:
              return opCode0(level, op_LDI);
            case 5:
              return opCode0(level, op_LDD);
            case 6:
              return opCode0(level, op_LDIR);
            case 7:
              return opCode0(level, op_LDDR);
            default:
              return NULL;
          }                    
        case 1:
          switch ((op>>3) & 0x7) {
            case 4:
              return opCode0(level, op_CPI);
            case 5:
              return opCode0(level, op_CPD);
            case 6:
              return opCode0(level, op_CPIR);
            case 7:
              return opCode0(level, op_CPDR);
            default:
              return NULL;
          }
        case 2:
          switch ((op>>3) & 0x7) {
            case 4:
              return opCode0(level, op_INI);
            case 5:
              return opCode0(level, op_IND);
            case 6:
              return opCode0(level, op_INIR);
            case 7:
              return opCode0(level, op_INDR);
            default:
              return NULL;
          }
        case 3:
          switch ((op>>3) & 0x7) {
            case 4:
              return opCode0(level, op_OUTI);
            case 5:
              return opCode0(level, op_OUTD);
            case 6:
              return opCode0(level, op_OTIR);
            case 7:
              return opCode0(level, op_OTDR);
            default:
              return NULL;
          }
        default:
          return NULL;
      }
    default:
      return NULL; 
  }

  return NULL;
}

Z80_OpCode* z80_disassemble(uint8_t* mem) {
  Z80_OpCode *opcode = disassemblePageXX(0, reg_HL, mem);
  return opcode;
}

static const char* arg_to_string(Z80_Arg arg) {
  static char buf[16];
  memset(buf, 0, sizeof(buf));
  if ((arg.flags & am_Register) != 0) {
    if ((arg.flags & am_Indirect) != 0) {
      if ((arg.flags & am_Indexed) != 0) {
        snprintf(buf, sizeof(buf) -1, "(%s%+d)", s_operands[arg.v], 
            (signed char) arg.displacement);
      }
      else {
        snprintf(buf, sizeof(buf) -1, "(%s)", s_operands[arg.v]);
      }
    }
    else {
      strncpy(buf, s_operands[arg.v], sizeof(buf) - 1);
    }
  }
  else if ((arg.flags & am_Immediate) != 0) {
    if ((arg.flags & am_Extended) != 0) {
      snprintf(buf, sizeof(buf) - 1, "0x%X", arg.v);
    }
    else if ((arg.flags & am_Displacement) != 0) {
      snprintf(buf, sizeof(buf) - 1, "%+d", (signed char) arg.v);
    }
    else {
      snprintf(buf, sizeof(buf) - 1, "0x%X", arg.v);
    }
  }
  else if ((arg.flags & am_Flag) != 0) {
    if ((arg.flags & am_Implicit) != 0) {
      snprintf(buf, sizeof(buf) - 1, "0x%X", arg.v);
    }
    else {
      strncpy(buf, s_operands[arg.v], sizeof(buf) - 1);
    }
  }
  else if ((arg.flags & am_Implicit) != 0) {
    snprintf(buf, sizeof(buf) - 1, "%d", arg.v);
  }

  return buf;
}

const char* z80_to_string(Z80_OpCode* opcode) {
  static char buf[80];
  memset(buf, 0, sizeof(buf));
  if (opcode == NULL) return buf;

  strncat(buf, s_mnemonics[opcode->operation], sizeof(buf) - 1);
  strncat(buf, " ", sizeof(buf) - 1);
  for (int i = 0; i < opcode->argc; i++) {
    strncat(buf, arg_to_string(opcode->args[i]), sizeof(buf) - 1);
    if (i < opcode->argc - 1) {
      strncat(buf, ",", sizeof(buf) - 1);
    }          
  }
  return buf;  
}

void z80_free(Z80_OpCode *opcode) {
  free((void*) opcode);
}
