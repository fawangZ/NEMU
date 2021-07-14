#include <cpu/decode.h>
#include "../local-include/rtl.h"
#include "../instr/rt.h"

#define INSTR_NULLARY(f) \
  f(inv) f(nemu_trap) f(nop) \
  f(ret) f(leave) \
  f(cwtl) f(cltd) \
  f(movs) f(stos) \
  f(rep_movs) f(rep_stos) f(repz_cmps) \
  f(pushf) f(pusha) f(popa) \
  f(clc) f(stc) f(cld) f(std) \
  f(iret) \
  f(cpuid) f(rdtsc) \
  f(fchs) f(fabs) f(fsqrt) f(fxam) f(frndint) f(fld1) f(fldz) f(fnstsw) f(fwait)

#define INSTR_UNARY(f) \
  f(call) f(jcc) f(jmp) f(setcc) f(call_E) f(jmp_E) f(ret_imm) f(jecxz) \
  f(push) f(pop) \
  f(inc) f(dec) f(neg) f(not) \
  f(mul) f(imul1) f(div) f(idiv) \
  f(lgdt) f(lidt) f(ltr) f(_int) \
  f(flds) f(fldl) f(fstl) f(fld) f(fsts) f(fstps) f(fstpl) f(fstp) \
  f(fildl) f(fildll) f(fistl) f(fistpl) f(fistpll) \
  f(fxch) f(fnstcw) f(fldcw) \
  f(fadds) f(faddl) f(fsubs) f(fsubl) f(fsubrs) f(fsubrl) \
  f(fmuls) f(fmull) f(fdivs) f(fdivl) f(fdivrs) f(fdivrl)

#define INSTR_BINARY(f) \
  f(mov) f(add) f(sub) f(adc) f(sbb) f(cmp) \
  f(and) f(or) f(test) f(xor) f(shl) f(shr) f(sar) f(rol) f(ror) \
  f(lea) f(movzb) f(movzw) f(movsb) f(movsw) \
  f(imul2) \
  f(bsr) f(bt) f(bsf) f(xchg) f(cmpxchg) \
  f(in) f(out) f(mov_cr2r) f(mov_r2cr) \
  f(cmovcc) \
  f(mov_rm2sreg) \
  f(movq_E2xmm) f(movq_xmm2E) f(movdqa_E2xmm) f(psrlq) f(movd_xmm2E) f(pxor) \
  f(xadd) \
  f(fadd) f(faddp) f(fsub) f(fsubp) f(fsubr) f(fsubrp) \
  f(fmul) f(fmulp) f(fdiv) f(fdivp) f(fdivr) f(fdivrp) \
  f(fcomi) f(fcomip) f(fucomi) f(fucomip) \
  f(fcmovb) f(fcmovbe)

#define INSTR_TERNARY(f) \
  f(imul3) f(shld) f(shrd)

def_all_EXEC_ID();
