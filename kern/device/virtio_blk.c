#include <arch/csr.h>
#include <arch/vm.h>
#include <block.h>
#include <plic.h>
#include <printk.h>
#include <string.h>
#include <types.h>

#define VIRTIO_MMIO_MAGIC_VALUE      0x000
#define VIRTIO_MMIO_VERSION          0x004
#define VIRTIO_MMIO_DEVICE_ID        0x008
#define VIRTIO_MMIO_DEVICE_FEATURES  0x010
#define VIRTIO_MMIO_DRIVER_FEATURES  0x020
#define VIRTIO_MMIO_GUEST_PAGE_SIZE  0x028
#define VIRTIO_MMIO_QUEUE_SEL        0x030
#define VIRTIO_MMIO_QUEUE_NUM_MAX    0x034
#define VIRTIO_MMIO_QUEUE_NUM        0x038
#define VIRTIO_MMIO_QUEUE_ALIGN      0x03c
#define VIRTIO_MMIO_QUEUE_PFN        0x040
#define VIRTIO_MMIO_QUEUE_READY      0x044
#define VIRTIO_MMIO_QUEUE_NOTIFY     0x050
#define VIRTIO_MMIO_INTERRUPT_STATUS 0x060
#define VIRTIO_MMIO_INTERRUPT_ACK    0x064
#define VIRTIO_MMIO_STATUS           0x070
#define VIRTIO_MMIO_QUEUE_DESC_LOW   0x080
#define VIRTIO_MMIO_QUEUE_DESC_HIGH  0x084
#define VIRTIO_MMIO_QUEUE_AVAIL_LOW  0x090
#define VIRTIO_MMIO_QUEUE_AVAIL_HIGH 0x094
#define VIRTIO_MMIO_QUEUE_USED_LOW   0x0a0
#define VIRTIO_MMIO_QUEUE_USED_HIGH  0x0a4

#define VIRTIO_STATUS_ACKNOWLEDGE 1
#define VIRTIO_STATUS_DRIVER      2
#define VIRTIO_STATUS_DRIVER_OK   4
#define VIRTIO_STATUS_FEATURES_OK 8
#define VIRTIO_STATUS_FAILED      128

#define VIRTQ_DESC_F_NEXT  1
#define VIRTQ_DESC_F_WRITE 2

#define VIRTIO_BLK_T_IN 0
#define VIRTIO_BLK_T_OUT 1
#define VIRTQ_SIZE      8
#define VIRTIO_MAGIC    0x74726976
#define VIRTIO_BLK_ID   2
#define VIRTIO_MMIO_STRIDE 0x1000UL
#define VIRTIO_MMIO_SLOTS  8
#define VIRTIO_IRQ_WAIT_TICKS 1000000UL

struct VirtqDesc {
	u64 addr;
	u32 len;
	u16 flags;
	u16 next;
};

struct VirtqAvail {
	u16 flags;
	u16 idx;
	u16 ring[VIRTQ_SIZE];
};

struct VirtqUsedElem {
	u32 id;
	u32 len;
};

struct VirtqUsed {
	u16 flags;
	u16 idx;
	struct VirtqUsedElem ring[VIRTQ_SIZE];
};

struct VirtioBlkReq {
	u32 type;
	u32 reserved;
	u64 sector;
};

static u8 queue_mem[8192] __attribute__((aligned(PAGE_SIZE)));
static volatile struct VirtqDesc *virtq_desc;
static volatile struct VirtqAvail *virtq_avail;
static volatile struct VirtqUsed *virtq_used;
static struct VirtioBlkReq blk_req __attribute__((aligned(16)));
static volatile u8 blk_status __attribute__((aligned(1)));
static uintptr_t virtio_base;
static u32 virtio_irq;
static int blk_ready;
static int blk_irq_enabled;
static int blk_irq_wait_enabled;
static volatile int blk_done;
static volatile u32 blk_interrupt_count;
static u16 used_seen;

static volatile u32 *mmio_reg(u32 offset) {
	return (volatile u32 *)(virtio_base + offset);
}

static u32 mmio_read(u32 offset) {
	return *mmio_reg(offset);
}

static void mmio_write(u32 offset, u32 value) {
	*mmio_reg(offset) = value;
}

static void mem_barrier(void) {
	__asm__ volatile("fence rw, rw" ::: "memory");
}

void block_init(void) {
	u32 magic = 0;
	u32 version = 0;
	u32 device = 0;
	u32 qmax;
	uintptr_t addr;

	blk_ready = 0;
	for (int i = 0; i < VIRTIO_MMIO_SLOTS; i++) {
		virtio_base = VIRTIO0_BASE + (uintptr_t)i * VIRTIO_MMIO_STRIDE;
		magic = mmio_read(VIRTIO_MMIO_MAGIC_VALUE);
		version = mmio_read(VIRTIO_MMIO_VERSION);
		device = mmio_read(VIRTIO_MMIO_DEVICE_ID);
		if (magic == VIRTIO_MAGIC && device == VIRTIO_BLK_ID) {
			virtio_irq = PLIC_IRQ_VIRTIO0 + (u32)i;
			break;
		}
	}
	if (magic != VIRTIO_MAGIC || (version != 1 && version != 2) || device != VIRTIO_BLK_ID) {
		printk("  virtio-blk absent magic=%x version=%u device=%u\n", magic, version, device);
		return;
	}

	mmio_write(VIRTIO_MMIO_STATUS, 0);
	mmio_write(VIRTIO_MMIO_STATUS, VIRTIO_STATUS_ACKNOWLEDGE);
	mmio_write(VIRTIO_MMIO_STATUS, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);

	(void)mmio_read(VIRTIO_MMIO_DEVICE_FEATURES);
	mmio_write(VIRTIO_MMIO_DRIVER_FEATURES, 0);
	if (version == 2) {
		mmio_write(VIRTIO_MMIO_STATUS, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER |
		                                    VIRTIO_STATUS_FEATURES_OK);
		if ((mmio_read(VIRTIO_MMIO_STATUS) & VIRTIO_STATUS_FEATURES_OK) == 0) {
			mmio_write(VIRTIO_MMIO_STATUS, VIRTIO_STATUS_FAILED);
			printk("  virtio-blk feature negotiation failed\n");
			return;
		}
	}

	mmio_write(VIRTIO_MMIO_QUEUE_SEL, 0);
	qmax = mmio_read(VIRTIO_MMIO_QUEUE_NUM_MAX);
	if (qmax < VIRTQ_SIZE) {
		printk("  virtio-blk queue too small max=%u\n", qmax);
		return;
	}

	memset(queue_mem, 0, sizeof(queue_mem));
	virtq_desc = (struct VirtqDesc *)queue_mem;
	virtq_avail = (struct VirtqAvail *)(queue_mem + sizeof(struct VirtqDesc) * VIRTQ_SIZE);
	virtq_used = (struct VirtqUsed *)(queue_mem + PAGE_SIZE);

	mmio_write(VIRTIO_MMIO_QUEUE_NUM, VIRTQ_SIZE);
	if (version == 1) {
		mmio_write(VIRTIO_MMIO_GUEST_PAGE_SIZE, PAGE_SIZE);
		mmio_write(VIRTIO_MMIO_QUEUE_ALIGN, PAGE_SIZE);
		mmio_write(VIRTIO_MMIO_QUEUE_PFN, (u32)((uintptr_t)queue_mem >> PAGE_SHIFT));
	} else {
		addr = (uintptr_t)virtq_desc;
		mmio_write(VIRTIO_MMIO_QUEUE_DESC_LOW, (u32)addr);
		mmio_write(VIRTIO_MMIO_QUEUE_DESC_HIGH, (u32)(addr >> 32));
		addr = (uintptr_t)virtq_avail;
		mmio_write(VIRTIO_MMIO_QUEUE_AVAIL_LOW, (u32)addr);
		mmio_write(VIRTIO_MMIO_QUEUE_AVAIL_HIGH, (u32)(addr >> 32));
		addr = (uintptr_t)virtq_used;
		mmio_write(VIRTIO_MMIO_QUEUE_USED_LOW, (u32)addr);
		mmio_write(VIRTIO_MMIO_QUEUE_USED_HIGH, (u32)(addr >> 32));
		mmio_write(VIRTIO_MMIO_QUEUE_READY, 1);
	}

	used_seen = 0;
	mmio_write(VIRTIO_MMIO_STATUS, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER |
	                                VIRTIO_STATUS_FEATURES_OK | VIRTIO_STATUS_DRIVER_OK);
	blk_ready = 1;
	if (virtio_irq != 0) {
		plic_enable_irq(virtio_irq);
		blk_irq_enabled = 1;
	}
	printk("  virtio-blk ready base=0x%lx irq=%u version=%u\n", virtio_base,
	       virtio_irq, version);
}

int block_available(void) {
	return blk_ready;
}

u32 block_irq(void) {
	return virtio_irq;
}

int block_interrupts_enabled(void) {
	return blk_ready && blk_irq_enabled;
}

void block_enable_irq_wait(void) {
	if (block_interrupts_enabled()) {
		blk_irq_wait_enabled = 1;
	}
}

void block_handle_irq(void) {
	u32 status;

	if (!blk_ready) {
		return;
	}
	status = mmio_read(VIRTIO_MMIO_INTERRUPT_STATUS);
	if (status != 0) {
		mmio_write(VIRTIO_MMIO_INTERRUPT_ACK, status);
		blk_interrupt_count++;
		blk_done = 1;
	}
}

static int block_rw_sector(u32 type, u64 sector, void *buf) {
	reg_t sstatus;
	reg_t sie;
	reg_t deadline;
	int ret;
	int use_irq;

	if (!blk_ready || buf == NULL) {
		return -1;
	}
	sstatus = csr_read_sstatus();
	sie = csr_read_sie();
	(void)blk_irq_wait_enabled;
	use_irq = 0;
	if (!use_irq) {
		(void)csr_read_clear_sstatus(SSTATUS_SIE);
	}

	blk_req.type = type;
	blk_req.reserved = 0;
	blk_req.sector = sector;
	blk_status = 0xff;
	blk_done = 0;

	virtq_desc[0].addr = (u64)(uintptr_t)&blk_req;
	virtq_desc[0].len = sizeof(blk_req);
	virtq_desc[0].flags = VIRTQ_DESC_F_NEXT;
	virtq_desc[0].next = 1;

	virtq_desc[1].addr = (u64)(uintptr_t)buf;
	virtq_desc[1].len = BLOCK_SECTOR_SIZE;
	virtq_desc[1].flags = VIRTQ_DESC_F_NEXT;
	if (type == VIRTIO_BLK_T_IN) {
		virtq_desc[1].flags |= VIRTQ_DESC_F_WRITE;
	}
	virtq_desc[1].next = 2;

	virtq_desc[2].addr = (u64)(uintptr_t)&blk_status;
	virtq_desc[2].len = sizeof(blk_status);
	virtq_desc[2].flags = VIRTQ_DESC_F_WRITE;
	virtq_desc[2].next = 0;

	virtq_avail->ring[virtq_avail->idx % VIRTQ_SIZE] = 0;
	mem_barrier();
	virtq_avail->idx++;
	mem_barrier();

	mmio_write(VIRTIO_MMIO_QUEUE_NOTIFY, 0);
	if (use_irq) {
		deadline = csr_read_time() + VIRTIO_IRQ_WAIT_TICKS;
		csr_write_sie(SIE_SEIE);
		csr_set_sstatus(SSTATUS_SIE);
		while (!blk_done && used_seen == virtq_used->idx &&
		       csr_read_time() < deadline) {
			mem_barrier();
		}
		csr_write_sie(sie);
		if ((sstatus & SSTATUS_SIE) == 0) {
			csr_clear_sstatus(SSTATUS_SIE);
		}
	}
	while (used_seen == virtq_used->idx) {
		mem_barrier();
	}
	mem_barrier();
	used_seen++;

	block_handle_irq();
	ret = blk_status == 0 ? 0 : -1;
	if (!use_irq && (sstatus & SSTATUS_SIE) != 0) {
		csr_set_sstatus(SSTATUS_SIE);
	}
	return ret;
}

int block_read_sector(u64 sector, void *buf) {
	return block_rw_sector(VIRTIO_BLK_T_IN, sector, buf);
}

int block_write_sector(u64 sector, const void *buf) {
	return block_rw_sector(VIRTIO_BLK_T_OUT, sector, (void *)buf);
}
