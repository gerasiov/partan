#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <stdint.h>
#include <unistd.h>
#include <stdio.h>

void usage(const char *name)
{
	printf("Usage:\n\t%s <dev|file>\n\t%s --help", name, name);
}

void help()
{
	printf("partan - partition analizer\n");
	printf("Copyright (c) 2014 Alexander Gerasiov <gq@cs.msu.su>\n");
	printf("Simple and stupid analizer for MS-DOS partion table.\n");
	printf("Supports only LBA, ignores (but prints) CHS addressing.\n");
}

#define BLOCK_SIZE 512

struct part_entry {
	unsigned char status;
	unsigned char chs_begin[3];
	unsigned char type;
	unsigned char chs_end[3];
	uint32_t lba_begin;
	uint32_t lba_size;
} __attribute__ ((__packed__));

#define MAGIC1 0x55
#define MAGIC2 0xAA
struct disk_block {
	char bootstrap[446];
	struct part_entry entry[4];
	unsigned char magic1;
	unsigned char magic2;
} __attribute__ ((__packed__));

uint32_t analize_entry(struct part_entry *entry, int e_num, int p_num, uint32_t offset)
{
	if (p_num)
		printf("------ Entry %d (partition %d) ------\n", e_num, p_num);
	else
		printf("------ Entry %d (next EBR) ------\n", e_num);

	printf("Status: 0x%.2x\t\t", entry->status);
	printf("Type: 0x%.2x\n", entry->type);

	printf("CHS begin: 0x%.2x%.2x%.2x\t", entry->chs_begin[0], entry->chs_begin[1], entry->chs_begin[2]); 
	printf("end: 0x%.2x%.2x%.2x\n", entry->chs_end[0], entry->chs_end[1], entry->chs_end[2]); 

	printf("LBA begin: 0x%.8x (%u)\tsize: 0x%.8x (%u)\tend*: %.8x (%u)\n",
			entry->lba_begin + offset, entry->lba_begin + offset,
			entry->lba_size, entry->lba_size,
			entry->lba_size?entry->lba_begin + entry->lba_size - 1 + offset:0,
			entry->lba_size?entry->lba_begin + entry->lba_size - 1 + offset:0
			);

	if (entry->type == 0x05 || entry->type == 0x0f) {
		if (entry->lba_begin == 0)
			printf("WARNING: type == 0x05 | 0x0f (Extended), but LBA begin = 0\n");
		return entry->lba_begin;
	}
	return 0;
}

uint32_t analize_block(struct disk_block *block, uint32_t ebr, uint32_t f_ebr, int *p_num)
{
	if (!ebr)
		printf("======= MBR =======\n");
	else
		printf("======= EBR (%u) =======\n", ebr);

	uint32_t next_ebr = 0;
	if (!ebr) {
		for (unsigned int i = 0; i < sizeof(block->entry) / sizeof(block->entry[0]); i++) {
			size_t n = analize_entry(&block->entry[i], i+1, (*p_num)++, ebr);
			if (next_ebr && n)
				printf("WARNING: Two or more extended partitions found.\n");
			if (n)
				next_ebr = n;
		}
	} else {
		analize_entry(&block->entry[0], 1, *p_num, ebr);
		next_ebr = analize_entry(&block->entry[1], 2, 0, f_ebr);
	}
	
	if (block->magic1 != MAGIC1 || block->magic2 != MAGIC2) {
		printf("WARNING:");
		if (block->magic1 != MAGIC1)
			printf("\tmagic1=0x%.2x", block->magic1);
		if (block->magic2 != MAGIC2)
			printf("\tmagic2=0x%.2x\t", block->magic2);
		printf("\n");
	}

	return next_ebr;
}

int main(int argc, const char** argv)
{
	if (argc != 2) {
		usage(argv[0]);
		return -1;
	}

	if (strcmp(argv[1], "--help")==0) {
		help();
		return 0;
	}


	const char *file = argv[1];

	long page_size = sysconf(_SC_PAGE_SIZE);

	void * page = NULL;

	int p_num = 1;

	int fd = open(file, O_RDONLY);
	if (fd < 0) {
		printf("Failed to open %s: %s", file, strerror(errno));
		return -1;
	}

	if (BLOCK_SIZE != sizeof(struct disk_block)) {
		printf("Internal error: BLOCK_SIZE != sizeof(struct disk_block)\n");
		return -1;
	}

	page = mmap(NULL, BLOCK_SIZE, PROT_READ, MAP_SHARED, fd, 0);
	
	if (page == MAP_FAILED) {
		printf("mmap failed: %s\n", strerror(errno));
		return -1;
	}

	struct disk_block *block = (struct disk_block *)page;
	

	uint32_t ebr_off = analize_block(block, 0, 0, &p_num);
	uint32_t f_ebr = ebr_off;
	uint32_t ebr = f_ebr;

	munmap(page, BLOCK_SIZE);

	while (ebr_off) {
		page = mmap(NULL, page_size, PROT_READ, MAP_SHARED, fd, ((ebr * BLOCK_SIZE) / page_size) * page_size);
		if (page == MAP_FAILED) {
			printf("mmap failed: %s\n", strerror(errno));
			return -1;
		}

		block = (struct disk_block *)((char *)page + ((ebr * BLOCK_SIZE) % page_size));
		
		ebr_off = analize_block(block, ebr, f_ebr, &p_num);
		ebr = f_ebr + ebr_off;
		munmap(page, page_size);
	}

	return 0;
}
