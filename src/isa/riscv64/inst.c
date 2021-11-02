#include "local-include/reg.h"
#include "local-include/intr.h"
#include <cpu/cpu.h>
#include <cpu/ifetch.h>
#include <cpu/decode.h>
#include <cpu/difftest.h>

#define R(i) gpr(i)
#define Mr(addr, len)       ({ word_t tmp = vaddr_read(s, addr, len, MMU_DYNAMIC); check_ex(); tmp; })
#define Mw(addr, len, data) vaddr_write(s, addr, len, data, MMU_DYNAMIC); check_ex()
#define check_ex() do { \
  if (MUXDEF(CONFIG_PERF_OPT, false, g_ex_cause != NEMU_EXEC_RUNNING)) { \
    cpu.pc = isa_raise_intr(g_ex_cause, s->pc); \
    g_ex_cause = NEMU_EXEC_RUNNING; \
    return 0; \
  } \
} while (0)

enum {
  TYPE_R, TYPE_I, TYPE_J,
  TYPE_U, TYPE_S, TYPE_B,
  TYPE_N, // none

  TYPE_CIW, TYPE_CFLD, TYPE_CLW, TYPE_CLD, TYPE_CFSD, TYPE_CSW, TYPE_CSD,
  TYPE_CI, TYPE_CASP, TYPE_CLUI, TYPE_CSHIFT,
  TYPE_CANDI, TYPE_CS, TYPE_CJ, TYPE_CB, TYPE_CIU, TYPE_CR,
  TYPE_CFLDSP, TYPE_CLWSP, TYPE_CLDSP,
  TYPE_CFSDSP, TYPE_CSWSP, TYPE_CSDSP,
};

void csrrw(word_t *dest, word_t src, uint32_t csrid);
void csrrs(word_t *dest, word_t src, uint32_t csrid);
word_t priv_instr(uint32_t op, const word_t *src);

static word_t immI(uint32_t i) { return SEXT(BITS(i, 31, 20), 12); }
static word_t immU(uint32_t i) { return SEXT(BITS(i, 31, 12), 20) << 12; }
static word_t immS(uint32_t i) { return (SEXT(BITS(i, 31, 25), 7) << 5) | BITS(i, 11, 7); }
static word_t immJ(uint32_t i) { return (SEXT(BITS(i, 31, 31), 1) << 20) |
  (BITS(i, 19, 12) << 12) | (BITS(i, 20, 20) << 11) | (BITS(i, 30, 21) << 1);
}
static word_t immB(uint32_t i) { return (SEXT(BITS(i, 31, 31), 1) << 12) |
  (BITS(i, 7, 7) << 11) | (BITS(i, 30, 25) << 5) | (BITS(i, 11, 8) << 1);
}

#ifdef CONFIG_PERF_OPT
#include <isa-all-instr.h>
static word_t zero_null = 0;
#define src1R(n) do { id_src1->preg = &R(n); } while (0)
#define src2R(n) do { id_src2->preg = &R(n); } while (0)
#define destR(n) do { id_dest->preg = (n == 0 ? &zero_null : &R(n)); } while (0)
#define src1I(i) do { id_src1->imm = i; } while (0)
#define src2I(i) do { id_src2->imm = i; } while (0)
#define destI(i) do { id_dest->imm = i; } while (0)
#else
#define src1R(n) do { *src1 = R(n); } while (0)
#define src2R(n) do { *src2 = R(n); } while (0)
#define destR(n) do { *dest = n; } while (0)
#define src1I(i) do { *src1 = i; } while (0)
#define src2I(i) do { *src2 = i; } while (0)
#define destI(i) do { *dest = i; } while (0)
#endif

static void decode_operand(Decode *s, word_t *dest, word_t *src1, word_t *src2, int type) {
  uint32_t i = s->isa.instr.val;
  int rd  = BITS(i, 11, 7);
  int rs1 = BITS(i, 19, 15);
  int rs2 = BITS(i, 24, 20);
  destR(rd);
  switch (type) {
    case TYPE_I: src1R(rs1);             src2I(immI(i)); break;
    case TYPE_U: src1I(immU(i));         src2I(immU(i) + s->pc); break;
    case TYPE_J: src1I(s->pc + immJ(i)); src2I(s->snpc); break;
    case TYPE_S: destI(immS(i));         goto R;
    case TYPE_B: destI(immB(i) + s->pc); R: // fall through
    case TYPE_R: src1R(rs1); src2R(rs2); break;
  }
}

static void jcond(bool cond, vaddr_t target) { if (cond) cpu.pc = target; }
static word_t sextw(word_t x) { return (int64_t)(int32_t)x; }

static int decode_exec(Decode *s) {
  word_t dest = 0, src1 = 0, src2 = 0;
  cpu.pc = s->snpc;

#define INSTPAT_INST(s) ((s)->isa.instr.val)
#define INSTPAT_MATCH(s, name, type, ... /* body */ ) { \
  decode_operand(s, &dest, &src1, &src2, concat(TYPE_, type)); \
  IFDEF(CONFIG_PERF_OPT, return concat(EXEC_ID_, name)); \
  __VA_ARGS__ ; \
}

  INSTPAT_START();
#ifdef CONFIG_PERF_OPT
  INSTPAT("0000000 00000 00001 ??? 00000 11001 11", p_ret  , I);
  INSTPAT("0000000 00000 ????? ??? 00000 11001 11", c_jr   , I);
  INSTPAT("??????? ????? ????? ??? 00000 11011 11", c_j    , J);
  INSTPAT("??????? ????? ????? ??? 00001 11011 11", p_jal  , J);
  INSTPAT("??????? 00000 ????? 000 ????? 11000 11", c_beqz , B);
  INSTPAT("??????? 00000 ????? 001 ????? 11000 11", c_bnez , B);
  INSTPAT("??????? 00000 ????? 100 ????? 11000 11", p_bltz , B);
  INSTPAT("??????? 00000 ????? 101 ????? 11000 11", p_bgez , B);
  INSTPAT("??????? ????? 00000 100 ????? 11000 11", p_bgtz , B);
  INSTPAT("??????? ????? 00000 101 ????? 11000 11", p_blez , B);
  INSTPAT("0000000 00000 00000 000 ????? 00100 11", p_li_0 , I);
  INSTPAT("0000000 00001 00000 000 ????? 00100 11", p_li_1 , I);
  INSTPAT("??????? ????? 00000 000 ????? 00100 11", c_li   , I);
  INSTPAT("0000000 00000 ????? 000 ????? 00100 11", p_mv_src1, I);
  INSTPAT("0000000 00000 ????? 000 ????? 00110 11", p_sext_w, I);
  int rs1 = BITS(s->isa.instr.val, 19, 15);
  int rd  = BITS(s->isa.instr.val, 11, 7);
if (rd == rs1) {
  INSTPAT("0000000 ????? ????? 000 ????? 01100 11", c_add  , R);
  INSTPAT("0100000 ????? ????? 000 ????? 01100 11", c_sub  , R);
  INSTPAT("0000000 ????? ????? 100 ????? 01100 11", c_xor  , R);
  INSTPAT("0000000 ????? ????? 110 ????? 01100 11", c_or   , R);
  INSTPAT("0000000 ????? ????? 111 ????? 01100 11", c_and  , R);
  INSTPAT("0000000 00001 ????? 000 ????? 00100 11", p_inc  , I);
  INSTPAT("1111111 11111 ????? 000 ????? 00100 11", p_dec  , I);
  INSTPAT("??????? ????? ????? 000 ????? 00100 11", c_addi , I);
  INSTPAT("??????? ????? ????? 111 ????? 00100 11", c_andi , I);
  INSTPAT("0000000 ????? ????? 001 ????? 00100 11", c_slli , I);
  INSTPAT("0100000 ????? ????? 101 ????? 00100 11", c_srai , I);
  INSTPAT("0000000 ????? ????? 101 ????? 00100 11", c_srli , I);
  INSTPAT("??????? ????? ????? 000 ????? 00110 11", c_addiw, I);
  INSTPAT("0000000 ????? ????? 000 ????? 01110 11", c_addw , R);
  INSTPAT("0100000 ????? ????? 000 ????? 01110 11", c_subw , R);
}
int mmu_mode = isa_mmu_state();
if (mmu_mode == MMU_TRANSLATE) {
  INSTPAT("??????? ????? ????? 000 ????? 00000 11", lb_mmu , I);
  INSTPAT("??????? ????? ????? 001 ????? 00000 11", lh_mmu , I);
  INSTPAT("??????? ????? ????? 010 ????? 00000 11", lw_mmu , I);
  INSTPAT("??????? ????? ????? 011 ????? 00000 11", ld_mmu , I);
  INSTPAT("??????? ????? ????? 100 ????? 00000 11", lbu_mmu, I);
  INSTPAT("??????? ????? ????? 101 ????? 00000 11", lhu_mmu, I);
  INSTPAT("??????? ????? ????? 110 ????? 00000 11", lwu_mmu, I);
  INSTPAT("??????? ????? ????? 000 ????? 01000 11", sb_mmu , S);
  INSTPAT("??????? ????? ????? 001 ????? 01000 11", sh_mmu , S);
  INSTPAT("??????? ????? ????? 010 ????? 01000 11", sw_mmu , S);
  INSTPAT("??????? ????? ????? 011 ????? 01000 11", sd_mmu , S);
}
#endif
  // rv32i
  INSTPAT("??????? ????? ????? ??? ????? 01101 11", lui    , U, R(dest) = src1);
  INSTPAT("??????? ????? ????? ??? ????? 00101 11", auipc  , U, R(dest) = src2);
  INSTPAT("??????? ????? ????? ??? ????? 11011 11", jal    , J, jcond(true, src1); R(dest) = src2);
  INSTPAT("??????? ????? ????? ??? ????? 11001 11", jalr   , I, jcond(true, src1 + src2); R(dest) = s->snpc);
  INSTPAT("??????? ????? ????? 000 ????? 11000 11", beq    , B, jcond(src1 == src2, dest));
  INSTPAT("??????? ????? ????? 001 ????? 11000 11", bne    , B, jcond(src1 != src2, dest));
  INSTPAT("??????? ????? ????? 100 ????? 11000 11", blt    , B, jcond((sword_t)src1 <  (sword_t)src2, dest));
  INSTPAT("??????? ????? ????? 101 ????? 11000 11", bge    , B, jcond((sword_t)src1 >= (sword_t)src2, dest));
  INSTPAT("??????? ????? ????? 110 ????? 11000 11", bltu   , B, jcond(src1 <  src2, dest));
  INSTPAT("??????? ????? ????? 111 ????? 11000 11", bgeu   , B, jcond(src1 >= src2, dest));
  INSTPAT("??????? ????? ????? 000 ????? 00000 11", lb     , I, R(dest) = SEXT(Mr(src1 + src2, 1), 8));
  INSTPAT("??????? ????? ????? 001 ????? 00000 11", lh     , I, R(dest) = SEXT(Mr(src1 + src2, 2), 16));
  INSTPAT("??????? ????? ????? 010 ????? 00000 11", lw     , I, R(dest) = SEXT(Mr(src1 + src2, 4), 32));
  INSTPAT("??????? ????? ????? 100 ????? 00000 11", lbu    , I, R(dest) = Mr(src1 + src2, 1));
  INSTPAT("??????? ????? ????? 101 ????? 00000 11", lhu    , I, R(dest) = Mr(src1 + src2, 2));
  INSTPAT("??????? ????? ????? 000 ????? 01000 11", sb     , S, Mw(src1 + dest, 1, src2));
  INSTPAT("??????? ????? ????? 001 ????? 01000 11", sh     , S, Mw(src1 + dest, 2, src2));
  INSTPAT("??????? ????? ????? 010 ????? 01000 11", sw     , S, Mw(src1 + dest, 4, src2));
  INSTPAT("??????? ????? ????? 000 ????? 00100 11", addi   , I, R(dest) = src1 + src2);
  INSTPAT("??????? ????? ????? 010 ????? 00100 11", slti   , I, R(dest) = (sword_t)src1 < (sword_t)src2);
  INSTPAT("??????? ????? ????? 011 ????? 00100 11", sltiu  , I, R(dest) = src1 <  src2);
  INSTPAT("??????? ????? ????? 100 ????? 00100 11", xori   , I, R(dest) = src1 ^  src2);
  INSTPAT("??????? ????? ????? 110 ????? 00100 11", ori    , I, R(dest) = src1 |  src2);
  INSTPAT("??????? ????? ????? 111 ????? 00100 11", andi   , I, R(dest) = src1 &  src2);
  INSTPAT("000000? ????? ????? 001 ????? 00100 11", slli   , I, R(dest) = src1 << src2);
  INSTPAT("000000? ????? ????? 101 ????? 00100 11", srli   , I, R(dest) = src1 >> src2);
  INSTPAT("010000? ????? ????? 101 ????? 00100 11", srai   , I, R(dest) = (sword_t)src1 >> (src2 & 0x3f));
  INSTPAT("0000000 ????? ????? 000 ????? 01100 11", add    , R, R(dest) = src1 +  src2);
  INSTPAT("0100000 ????? ????? 000 ????? 01100 11", sub    , R, R(dest) = src1 -  src2);
  INSTPAT("0000000 ????? ????? 001 ????? 01100 11", sll    , R, R(dest) = src1 << src2);
  INSTPAT("0000000 ????? ????? 010 ????? 01100 11", slt    , R, R(dest) = (sword_t)src1 <  (sword_t)src2);
  INSTPAT("0000000 ????? ????? 011 ????? 01100 11", sltu   , R, R(dest) = src1 <  src2);
  INSTPAT("0000000 ????? ????? 100 ????? 01100 11", xor    , R, R(dest) = src1 ^  src2);
  INSTPAT("0000000 ????? ????? 101 ????? 01100 11", srl    , R, R(dest) = src1 >> src2);
  INSTPAT("0100000 ????? ????? 101 ????? 01100 11", sra    , R, R(dest) = (sword_t)src1 >> src2);
  INSTPAT("0000000 ????? ????? 110 ????? 01100 11", or     , R, R(dest) = src1 |  src2);
  INSTPAT("0000000 ????? ????? 111 ????? 01100 11", and    , R, R(dest) = src1 &  src2);
  INSTPAT("??????? ????? ????? 000 ????? 00011 11", fence  , I); // do nothing in non-perf mode
  INSTPAT("??????? ????? ????? 001 ????? 00011 11", fence_i, I); // do nothing in non-perf mode

  // rv64i
  INSTPAT("??????? ????? ????? 110 ????? 00000 11", lwu    , I, R(dest) = Mr(src1 + src2, 4));
  INSTPAT("??????? ????? ????? 011 ????? 00000 11", ld     , I, R(dest) = Mr(src1 + src2, 8));
  INSTPAT("??????? ????? ????? 011 ????? 01000 11", sd     , S, Mw(src1 + dest, 8, src2));
  INSTPAT("??????? ????? ????? 000 ????? 00110 11", addiw  , I, R(dest) = sextw(src1 + src2));
  INSTPAT("0000000 ????? ????? 001 ????? 00110 11", slliw  , I, R(dest) = sextw((uint32_t)src1 << (src2 & 0x1f)));
  INSTPAT("0000000 ????? ????? 101 ????? 00110 11", srliw  , I, R(dest) = sextw((uint32_t)src1 >> (src2 & 0x1f)));
  INSTPAT("0100000 ????? ????? 101 ????? 00110 11", sraiw  , I, R(dest) = sextw(( int32_t)src1 >> (src2 & 0x1f)));
  INSTPAT("0000000 ????? ????? 000 ????? 01110 11", addw   , R, R(dest) = sextw(src1 + src2));
  INSTPAT("0100000 ????? ????? 000 ????? 01110 11", subw   , R, R(dest) = sextw(src1 - src2));
  INSTPAT("0000000 ????? ????? 001 ????? 01110 11", sllw   , R, R(dest) = sextw((uint32_t)src1 << (src2 & 0x1f)));
  INSTPAT("0000000 ????? ????? 101 ????? 01110 11", srlw   , R, R(dest) = sextw((uint32_t)src1 >> (src2 & 0x1f)));
  INSTPAT("0100000 ????? ????? 101 ????? 01110 11", sraw   , R, R(dest) = sextw(( int32_t)src1 >> (src2 & 0x1f)));

  // rv32m
  INSTPAT("0000001 ????? ????? 000 ????? 01100 11", mul    , R, R(dest) = src1 *  src2);
  INSTPAT("0000001 ????? ????? 001 ????? 01100 11", mulh   , R,
      R(dest) = ((__int128_t)(sword_t)src1 *  (__int128_t)(sword_t)src2) >> 64);
  INSTPAT("0000001 ????? ????? 010 ????? 01100 11", mulhsu , R,
      word_t hi = ((__uint128_t)src1 *  (__uint128_t)src2) >> 64;
      R(dest) = hi - ((sword_t)src1  < 0 ? src2 : 0));
  INSTPAT("0000001 ????? ????? 011 ????? 01100 11", mulhu  , R,
      R(dest) = ((__uint128_t)src1 *  (__uint128_t)src2) >> 64);
  INSTPAT("0000001 ????? ????? 100 ????? 01100 11", div    , R, R(dest) = (sword_t)src1 /  (sword_t)src2);
  INSTPAT("0000001 ????? ????? 101 ????? 01100 11", divu   , R, R(dest) = src1 /  src2);
  INSTPAT("0000001 ????? ????? 110 ????? 01100 11", rem    , R, R(dest) = (sword_t)src1 %  (sword_t)src2);
  INSTPAT("0000001 ????? ????? 111 ????? 01100 11", remu   , R, R(dest) = src1 %  src2);

  // rv64m
  INSTPAT("0000001 ????? ????? 000 ????? 01110 11", mulw   , R, R(dest) = sextw(src1 * src2));
  INSTPAT("0000001 ????? ????? 100 ????? 01110 11", divw   , R, R(dest) = sextw(( int32_t)src1 / ( int32_t)src2));
  INSTPAT("0000001 ????? ????? 101 ????? 01110 11", divuw  , R, R(dest) = sextw((uint32_t)src1 / (uint32_t)src2));
  INSTPAT("0000001 ????? ????? 110 ????? 01110 11", remw   , R, R(dest) = sextw(( int32_t)src1 % ( int32_t)src2));
  INSTPAT("0000001 ????? ????? 111 ????? 01110 11", remuw  , R, R(dest) = sextw((uint32_t)src1 % (uint32_t)src2));

  // privileged
#ifdef CONFIG_PERF_OPT
  INSTPAT("??????? ????? ????? ??? ????? 11100 11", system , I);
#else
  INSTPAT("0000000 00000 00000 000 00000 11100 11", ecall  , N, cpu.pc = isa_raise_intr(cpu.mode + 8, s->pc));
  INSTPAT("0011000 00010 00000 000 00000 11100 11", mret   , I, cpu.pc = priv_instr(src2, NULL));
  INSTPAT("0001001 ????? ????? 000 00000 11100 11", sfence_vma,I); // do nothing in non-perf mode
  INSTPAT("??????? ????? ????? 001 ????? 11100 11", csrrw  , I, csrrw(&R(dest), src1, src2));
  INSTPAT("??????? ????? ????? 010 ????? 11100 11", csrrs  , I, csrrs(&R(dest), src1, src2));
#endif

  INSTPAT("??????? ????? ????? ??? ????? 11010 11", nemu_trap, N, NEMUTRAP(s->pc, R(10))); // R(10) is $a0
  INSTPAT("??????? ????? ????? ??? ????? ????? ??", inv    , N, INV(s->pc));
  INSTPAT_END();

  R(0) = 0; // reset $zero to 0

  return 0;
}

__attribute__((always_inline))
static word_t rvc_imm_internal(uint32_t instr, const char *str, int len, bool sign) {
  word_t imm = 0;
  uint32_t msb_mask = 0;
  uint32_t instr_sll16 = instr << 16;
  assert(len == 16);
#define macro(i) do { \
    char c = str[i]; \
    if (c != '.') { \
      Assert((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'), \
          "invalid character '%c' in pattern string", c); \
      int pos = (c >= '0' && c <= '9') ? c - '0' : c - 'a' + 10; \
      int nr_srl = 16 + (15 - (i)) - pos; \
      uint32_t mask = 1u << pos ; \
      imm |= (instr_sll16 >> nr_srl) & mask; \
      if (mask > msb_mask) { msb_mask = mask;} \
    } \
  } while (0)

  macro16(0);
#undef macro

  if (sign && (msb_mask & imm)) {
    imm |= (-imm) & ~((word_t)msb_mask - 1);
  }
  return imm;
}

#define rvc_simm(i, str) rvc_imm_internal(i, str, STRLEN(str), true)
#define rvc_uimm(i, str) rvc_imm_internal(i, str, STRLEN(str), false)
#define creg2reg(creg) (creg + 8)

static void decode_operand_rvc(Decode *s, word_t *dest, word_t *src1, word_t *src2, int type) {
  uint32_t i = s->isa.instr.val;
  int r9_7 = creg2reg(BITS(i, 9, 7));
  int r4_2 = creg2reg(BITS(i, 4, 2));
  int r11_7 = BITS(i, 11, 7);
  int r6_2 = BITS(i, 6, 2);
#define rdrs1(r0, r1) do { destR(r0); src1R(r1); } while (0)
#define rdrs1_same(r) rdrs1(r, r)
#define rs1rs2(r1, r2) do { src1R(r1); src2R(r2); } while (0)
  switch (type) {
    case TYPE_CIW:    src2I(rvc_uimm(i, "...54987623....."));         rdrs1(r4_2, 2); break;
    case TYPE_CLW:    src2I(rvc_uimm(i, "...543...26.....")); goto CL;
    case TYPE_CLD:    src2I(rvc_uimm(i, "...543...76.....")); CL:     rdrs1(r4_2, r9_7); break;
    case TYPE_CSW:    destI(rvc_uimm(i, "...543...26.....")); goto CW;
    case TYPE_CSD:    destI(rvc_uimm(i, "...543...76.....")); CW:     rs1rs2(r9_7, r4_2); break;
    case TYPE_CI:     src2I(rvc_simm(i, "...5.....43210.."));         rdrs1_same(r11_7); break;
    case TYPE_CASP:   src2I(rvc_simm(i, "...9.....46875.."));         rdrs1_same(2); break;
    case TYPE_CLUI:   src1I(rvc_simm(i, "...5.....43210..") << 12);   destR(r11_7); break;
    case TYPE_CSHIFT: src2I(rvc_uimm(i, "...5.....43210.."));         rdrs1_same(r9_7); break;
    case TYPE_CANDI:  src2I(rvc_simm(i, "...5.....43210.."));         rdrs1_same(r9_7); break;
    case TYPE_CJ:     src1I(rvc_simm(i, "...b498a673215..") + s->pc); break;
    case TYPE_CB:     destI(rvc_simm(i, "...843...76215..") + s->pc); rs1rs2(r9_7, 0); break;
    case TYPE_CIU:    src2I(rvc_uimm(i, "...5.....43210.."));         rdrs1_same(r11_7); break;
    case TYPE_CFLDSP: src2I(rvc_uimm(i, "...5.....43876..")); goto CI_ld;
    case TYPE_CLWSP:  src2I(rvc_uimm(i, "...5.....43276..")); goto CI_ld;
    case TYPE_CLDSP:  src2I(rvc_uimm(i, "...5.....43876..")); CI_ld:  rdrs1(r11_7, 2); break;
    case TYPE_CFSDSP: destI(rvc_uimm(i, "...543876.......")); goto CSS;
    case TYPE_CSWSP:  destI(rvc_uimm(i, "...543276.......")); goto CSS;
    case TYPE_CSDSP:  destI(rvc_uimm(i, "...543876.......")); CSS:    rs1rs2(2, r6_2); break;
    case TYPE_CS: rdrs1_same(r9_7);  src2R(r4_2); break;
    case TYPE_CR: rdrs1_same(r11_7); src2R(r6_2); break;
  }
}

static int decode_exec_rvc(Decode *s) {
  word_t dest = 0, src1 = 0, src2 = 0;
  cpu.pc = s->snpc;

#undef INSTPAT_MATCH
#define INSTPAT_MATCH(s, name, type, ... /* body */ ) { \
  decode_operand_rvc(s, &dest, &src1, &src2, concat(TYPE_, type)); \
  IFDEF(CONFIG_PERF_OPT, return concat(EXEC_ID_, name)); \
  __VA_ARGS__ ; \
}

  INSTPAT_START();
#ifdef CONFIG_PERF_OPT
  INSTPAT("000 0 ????? 00001 01", p_inc   , CI);
  INSTPAT("000 1 ????? 11111 01", p_dec   , CI);
  INSTPAT("010 0 ????? 00000 01", p_li_0  , CI);
  INSTPAT("010 0 ????? 00001 01", p_li_1  , CI);
  INSTPAT("100 0 00001 00000 10", p_ret   , CR);
int mmu_mode = isa_mmu_state();
if (mmu_mode == MMU_TRANSLATE) {
  INSTPAT("001 ??? ??? ?? ??? 00", fld_mmu, CFLD);
  INSTPAT("010 ??? ??? ?? ??? 00", lw_mmu , CLW);
  INSTPAT("011 ??? ??? ?? ??? 00", ld_mmu , CLD);
  INSTPAT("101 ??? ??? ?? ??? 00", fsd_mmu, CFSD);
  INSTPAT("110 ??? ??? ?? ??? 00", sw_mmu , CSW);
  INSTPAT("111 ??? ??? ?? ??? 00", sd_mmu , CSD);
  INSTPAT("001 ? ????? ????? 10" , fld_mmu, CFLDSP);
  INSTPAT("010 ? ????? ????? 10" , lw_mmu , CLWSP);
  INSTPAT("011 ? ????? ????? 10" , ld_mmu , CLDSP);
  INSTPAT("101 ? ????? ????? 10" , fsd_mmu, CFSDSP);
  INSTPAT("110 ? ????? ????? 10" , sw_mmu , CSWSP);
  INSTPAT("111 ? ????? ????? 10" , sd_mmu , CSDSP);
}
  // c_jalr can not handle correctly when rs1 == ra, fall back to general jalr
  INSTPAT("100 1 00001 00000 10", jalr    , CR);
#endif
  // Q0
  INSTPAT("000  00000000  000 00", inv    , N   , INV(s->pc));
  INSTPAT("000  ????????  ??? 00", addi   , CIW , R(dest) = src1 + src2); // C.ADDI2SPN
  INSTPAT("001 ??? ??? ?? ??? 00", fld    , CFLD, assert(0));
  INSTPAT("010 ??? ??? ?? ??? 00", lw     , CLW , R(dest) = SEXT(Mr(src1 + src2, 4), 32));
  INSTPAT("011 ??? ??? ?? ??? 00", ld     , CLD , R(dest) = Mr(src1 + src2, 8));
  INSTPAT("101 ??? ??? ?? ??? 00", fsd    , CFSD, assert(0));
  INSTPAT("110 ??? ??? ?? ??? 00", sw     , CSW , Mw(src1 + dest, 4, src2));
  INSTPAT("111 ??? ??? ?? ??? 00", sd     , CSD , Mw(src1 + dest, 8, src2));

  // Q1
  INSTPAT("000 ? ????? ????? 01", c_addi  , CI , R(dest) = src1 + src2);
  INSTPAT("001 ? ????? ????? 01", c_addiw , CI , R(dest) = sextw(src1 + src2));
  INSTPAT("010 ? ????? ????? 01", c_li    , CI , R(dest) = src2);
  INSTPAT("011 ? 00010 ????? 01", c_addi  , CASP,R(dest) = src1 + src2); // C.ADDI16SP
  INSTPAT("011 ? ????? ????? 01", lui     , CLUI,R(dest) = src1);
  INSTPAT("100 ? 00??? ????? 01", c_srli  , CSHIFT, R(dest) = src1 >> src2);
  INSTPAT("100 ? 01??? ????? 01", c_srai  , CSHIFT, R(dest) = (sword_t)src1 >> src2);
  INSTPAT("100 ? 10??? ????? 01", c_andi  , CANDI , R(dest) = src1 & src2);
  INSTPAT("100 0 11??? 00??? 01", c_sub   , CS, R(dest) = src1 - src2);
  INSTPAT("100 0 11??? 01??? 01", c_xor   , CS, R(dest) = src1 ^ src2);
  INSTPAT("100 0 11??? 10??? 01", c_or    , CS, R(dest) = src1 | src2);
  INSTPAT("100 0 11??? 11??? 01", c_and   , CS, R(dest) = src1 & src2);
  INSTPAT("100 1 11??? 00??? 01", c_subw  , CS, R(dest) = sextw(src1 - src2));
  INSTPAT("100 1 11??? 01??? 01", c_addw  , CS, R(dest) = sextw(src1 + src2));
  INSTPAT("101  ???????????  01", c_j     , CJ, jcond(true, src1));
  INSTPAT("110 ??? ??? ????? 01", c_beqz  , CB, jcond(src1 == 0, dest));
  INSTPAT("111 ??? ??? ????? 01", c_bnez  , CB, jcond(src1 != 0, dest));

  // Q2
  INSTPAT("000 ? ????? ????? 10", slli    , CIU, R(dest) = src1 << src2);
  INSTPAT("001 ? ????? ????? 10", fld     , CFLDSP, assert(0));
  INSTPAT("010 ? ????? ????? 10", lw      , CLWSP , R(dest) = SEXT(Mr(src1 + src2, 4), 32));
  INSTPAT("011 ? ????? ????? 10", ld      , CLDSP , R(dest) = Mr(src1 + src2, 8));

  INSTPAT("100 0 ????? 00000 10", c_jr    , CR, jcond(true, src1));
  INSTPAT("100 0 ????? ????? 10", c_mv    , CR, R(dest) = src2);
  INSTPAT("100 1 00000 00000 10", inv     , N);  // ebreak
  INSTPAT("100 1 ????? 00000 10", c_jalr  , CR, jcond(true, src1); R(1) = s->snpc);
  INSTPAT("100 1 ????? ????? 10", c_add   , CR, R(dest) = src1 + src2);

  INSTPAT("101 ? ????? ????? 10", fsd     , CFSDSP, assert(0));
  INSTPAT("110 ? ????? ????? 10", sw      , CSWSP , Mw(src1 + dest, 4, src2));
  INSTPAT("111 ? ????? ????? 10", sd      , CSDSP , Mw(src1 + dest, 8, src2));

  INSTPAT("??? ??? ??? ?? ??? ??", inv    , N  , INV(s->pc));
  INSTPAT_END();

  R(0) = 0; // reset $zero to 0

  return 0;
}

int isa_fetch_decode(Decode *s) {
  int idx = 0;
  s->isa.instr.val = instr_fetch(&s->snpc, 2);
  if (BITS(s->isa.instr.val, 1, 0) != 0x3) {
    // this is an RVC instruction
    idx = decode_exec_rvc(s);
  } else {
    // this is a 4-byte instruction, should fetch the MSB part
    // NOTE: The fetch here may cause IPF.
    // If it is the case, we should have mepc = xxxffe and mtval = yyy000.
    // Refer to `mtval` in the privileged manual for more details.
    uint32_t hi = instr_fetch(&s->snpc, 2);
    s->isa.instr.val |= (hi << 16);
    idx = decode_exec(s);
  }
#ifdef CONFIG_PERF_OPT
  s->type = INSTR_TYPE_N;
  switch (idx) {
    case EXEC_ID_c_j: case EXEC_ID_p_jal: case EXEC_ID_jal:
      s->jnpc = id_src1->imm; s->type = INSTR_TYPE_J; break;

    case EXEC_ID_beq: case EXEC_ID_bne: case EXEC_ID_blt: case EXEC_ID_bge:
    case EXEC_ID_bltu: case EXEC_ID_bgeu:
    case EXEC_ID_c_beqz: case EXEC_ID_c_bnez:
    case EXEC_ID_p_bltz: case EXEC_ID_p_bgez: case EXEC_ID_p_blez: case EXEC_ID_p_bgtz:
      s->jnpc = id_dest->imm; s->type = INSTR_TYPE_B; break;

    case EXEC_ID_p_ret: case EXEC_ID_c_jr: case EXEC_ID_c_jalr: case EXEC_ID_jalr:
      s->type = INSTR_TYPE_I; break;

    case EXEC_ID_system:
      if (s->isa.instr.i.funct3 == 0) {
        switch (s->isa.instr.csr.csr) {
          case 0:     // ecall
          case 0x102: // sret
          case 0x302: // mret
            s->type = INSTR_TYPE_I;
        }
      }
      break;
  }
#endif
  return idx;
}
