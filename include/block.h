#ifndef _MOS_RISCV_BLOCK_H_
#define _MOS_RISCV_BLOCK_H_

#include <types.h>

#define BLOCK_SECTOR_SIZE 512UL

void block_init(void);
int block_available(void);
int block_read_sector(u64 sector, void *buf);

#endif
