#include <isa.h>
#include <monitor/difftest.h>
#include <cpu/exec.h>

void cpu_exec(uint64_t);

void difftest_memcpy_from_dut(paddr_t dest, void *src, size_t n) {
  memcpy(guest_to_host(dest), src, n);
}

void difftest_getregs(void *r) {
  isa_difftest_getregs(r);
//  isa_difftest_getregs_hook();
//  memcpy(r, &cpu, DIFFTEST_REG_SIZE);
}

void difftest_setregs(const void *r) {
  isa_difftest_setregs(r);
//  memcpy(&cpu, r, DIFFTEST_REG_SIZE);
//  isa_difftest_setregs_hook();
}

void difftest_exec(uint64_t n) {
  cpu_exec(n);
}

void difftest_raise_intr(word_t NO) {
  DecodeExecState s;
  s.is_jmp = 0;
  s.isa = (struct ISADecodeInfo) { 0 };

  void raise_intr(DecodeExecState *s, word_t NO, vaddr_t epc);
  raise_intr(&s, NO, cpu.pc);
  update_pc(&s);
}

void difftest_init(void) {
  /* Perform ISA dependent initialization. */
  void init_isa();
  init_isa();
}
