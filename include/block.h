#ifndef _MOS_RISCV_BLOCK_H_
#define _MOS_RISCV_BLOCK_H_

#include <types.h>

#define BLOCK_SECTOR_SIZE 512UL

void block_init(void);
int block_available(void);
u32 block_irq(void);
int block_interrupts_enabled(void);
void block_enable_irq_wait(void);
void block_handle_irq(void);
int block_read_sector(u64 sector, void *buf);
int block_write_sector(u64 sector, const void *buf);

#endif
