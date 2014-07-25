/*
    partan - Partition Analizer
    Copyright (c) 2014 Alexander Gerasiov <gq@cs.msu.su>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
*/
#define _LARGEFILE64_SOURCE     /* See feature_test_macros(7) */

#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

void usage(const char *name)
{
	printf("Usage:\n\t%s <dev|file>\n\t%s --help\n", name, name);
}

void help()
{
	printf("partan - partition analizer\n");
	printf("Copyright (c) 2014 Alexander Gerasiov <gq@cs.msu.su>\n");
	printf("Simple and stupid analizer for MS-DOS partion table.\n");
	printf("Supports only LBA, ignores (but prints) CHS addressing.\n");
	printf("\n");
	usage("partan");
}

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


int dev_fd;

struct disk_block read_block(unsigned int num)
{
	struct disk_block _block;
	if (lseek64(dev_fd, (off64_t)num * sizeof(_block), SEEK_SET) == -1) {
		fprintf(stderr, "lseek failed: %s\n", strerror(errno));
		exit(1);
	}

	if (read(dev_fd, &_block, sizeof(_block)) != sizeof(_block)) {
		fprintf(stderr, "read failed: %s\n", strerror(errno));
		exit(1);
	}
	return _block;
}

int write_block(unsigned int num, struct disk_block *block)
{
	if (lseek64(dev_fd, (off64_t)num * sizeof(*block), SEEK_SET) == -1) {
		fprintf(stderr, "lseek failed: %s\n", strerror(errno));
		exit(1);
	}

	if (write(dev_fd, block, sizeof(*block)) != sizeof(*block)) {
		fprintf(stderr, "write failed: %s\n", strerror(errno));
		exit(1);
	}
	return sizeof(*block);
}

uint32_t analize_entry(struct part_entry *entry, int e_num, int p_num, uint32_t offset)
//e_num - number of entry in MBR/EBR
//p_num - number of partition
//offset - 0 for MBR, EBR offset for extented partition, first EBR offset for next EBR link.
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
			entry->lba_begin?entry->lba_begin + offset:0,
			entry->lba_begin?entry->lba_begin + offset:0,
			entry->lba_size, entry->lba_size,
			entry->lba_size?entry->lba_begin + entry->lba_size - 1 + offset:0,
			entry->lba_size?entry->lba_begin + entry->lba_size - 1 + offset:0
			);

	if (entry->type == 0x05 || entry->type == 0x0f) {
		if (entry->lba_begin == 0)
			printf("WARNING: type == 0x05 | 0x0f (Extended), but LBA begin = 0\n");
		return entry->lba_begin; // return offset of (next) EBR
	}
	return 0;
}

uint32_t analize_block(struct disk_block *block, uint32_t ebr, uint32_t f_ebr, int *p_num)
// ebr - 0 for MBR, this EBR offset for EBR.
// f_ebr - 0 for MBR, first EBR offset for EBR
// p_num - counter for partition
{
	if (!ebr)
		printf("======= MBR =======\n");
	else
		printf("======= EBR (%u) =======\n", ebr);

	uint32_t next_ebr = 0;
	if (!ebr) { // MBR: all 4 entries contains partition.
		for (unsigned int i = 0; i < sizeof(block->entry) / sizeof(block->entry[0]); i++) {
			size_t n = analize_entry(&block->entry[i], i+1, (*p_num)++, ebr);
			if (next_ebr && n)
				printf("WARNING: Two or more extended partitions found.\n");
			if (n)
				next_ebr = n;
		}
	} else { // EBR
		// First entry contain partition with offset from current EBR
		analize_entry(&block->entry[0], 1, (*p_num)++, ebr);
		// Second entry contains next EBR with offset from the first EBR
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

	int p_num = 1;

	dev_fd = open(file, O_RDONLY);
	if (dev_fd < 0) {
		printf("Failed to open %s: %s", file, strerror(errno));
		return -1;
	}

	if (sizeof(struct disk_block) != 512) {
		printf("Internal error: sizeof(struct disk_block) != 512\n");
		return -1;
	}

	uint32_t current_record = 0;
	uint32_t next_record = 0;
	uint32_t first_ebr = 0;

	do {
		struct disk_block block = read_block(current_record);
		next_record = analize_block(&block, current_record, first_ebr, &p_num);
		current_record = first_ebr + next_record;
		if (first_ebr == 0)
			first_ebr = next_record;
	} while (next_record);

	return 0;
}
