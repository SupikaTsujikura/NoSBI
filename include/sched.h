#ifndef _MOS_RISCV_SCHED_H_
#define _MOS_RISCV_SCHED_H_

#include <env.h>

void schedule(int yield);
void sched_tick(void);
void sched_yield(void);
void sched_init(void);

#endif
