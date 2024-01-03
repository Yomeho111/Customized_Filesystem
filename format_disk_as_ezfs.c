#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>

/* These are the same on a 64-bit architecture */
#define timespec64 timespec

#include "ezfs.h"


void passert(int condition, char *message)
{
	printf("[%s] %s\n", condition ? " OK " : "FAIL", message);
	if (!condition)
		exit(1);
}

void inode_reset(struct ezfs_inode *inode)
{
	struct timespec current_time;

	/* In case inode is uninitialized/previously used */
	memset(inode, 0, sizeof(*inode));
	memset(&current_time, 0, sizeof(current_time));

	/* These sample files will be owned by the first user and group on the system */
	inode->uid = 1000;
	inode->gid = 1000;

	/* Current time UTC */
	clock_gettime(CLOCK_REALTIME, &current_time);
	inode->i_atime = inode->i_mtime = inode->i_ctime = current_time;
}

void dentry_reset(struct ezfs_dir_entry *dentry)
{
	memset(dentry, 0, sizeof(*dentry));
}

char *read_file(const char *filename, size_t *length)
{
	FILE *file = fopen(filename, "rb");
	long filesize;
	char *buffer;

	if (file == NULL) {
		perror("Error opening file");
		return NULL;
	}

	// Go to the end of the file.
	if (fseek(file, 0, SEEK_END) != 0) {
		perror("Error seeking file");
		fclose(file);
		return NULL;
	}

	// Get the size of the file.
	filesize = ftell(file);
	printf("The filesize is %ld", filesize);
	if (filesize == -1) {
		perror("Error getting file size");
		fclose(file);
		return NULL;
	}

	// Allocate memory for the file content + null terminator
	buffer = (char *)malloc(filesize);
	if (buffer == NULL) {
		perror("Error allocating memory");
		fclose(file);
		return NULL;
	}

	// Go back to the start of the file and read it into the buffer
	fseek(file, 0, SEEK_SET);
	size_t read_size = fread(buffer, 1, filesize, file);
	if (read_size != filesize) {
		perror("Error reading file");
		free(buffer);
		fclose(file);
		return NULL;
	}

	if (length != NULL) {
		*length = filesize;
	}

	//printf("The size is %ld, the length is %ld", filesize, *length);
	// Close the file and return the buffer
	fclose(file);
	return buffer;
}




int main(int argc, char *argv[])
{
	int fd;
	int disk_blks;
	ssize_t ret, len;
	struct ezfs_super_block sb;
	struct ezfs_inode inode;
	struct ezfs_dir_entry dentry;
	size_t length_img;
	size_t length_txt;

	char *hello_contents = "Hello world!\n";
	char *name_contents = "Mingfeng Yang\nTeng Jiang\n";
	const char *big_img_dir = "./big_files/big_img.jpeg";
	const char *big_txt_dir = "./big_files/big_txt.txt";
	char *big_img_contents = read_file(big_img_dir, &length_img);
	char *big_txt_contents = read_file(big_txt_dir, &length_txt);
	char buf[EZFS_BLOCK_SIZE];
	const char zeroes[EZFS_BLOCK_SIZE] = { 0 };
	ssize_t big_len;
	uint64_t indirect_num;
	size_t indirect_length = 0;
	long big_img_start_blk = 6;
	long big_txt_start_blk = 14;

	if (argc != 3) {
		printf("Usage: ./format_disk_as_ezfs DEVICE_NAME DISK_BLKS.\n");
		return -1;
	}
	disk_blks = atoi(argv[2]);
	if (disk_blks <= 0) {
		printf("Invalid DISK_BLKS.\n");
		return -1;
	}

	fd = open(argv[1], O_RDWR);
	if (fd == -1) {
		perror("Error opening the device");
		return -1;
	}
	memset(&sb, 0, sizeof(sb));

	sb.version = 1;
	sb.magic = EZFS_MAGIC_NUMBER;
	sb.disk_blks = disk_blks;

	/* The first two inodes and datablocks are taken by the root and
	 * hello.txt file, respectively. Mark them as such.
	 */
	SETBIT(sb.free_inodes, 0);
	SETBIT(sb.free_inodes, 1);
	SETBIT(sb.free_inodes, 2);
	SETBIT(sb.free_inodes, 3);
	SETBIT(sb.free_inodes, 4);
	SETBIT(sb.free_inodes, 5);

	for (int i = 0; i < 18; i++)
		SETBIT(sb.free_data_blocks, i);

	/* Write the superblock to the first block of the filesystem. */
	ret = write(fd, (char *)&sb, sizeof(sb));
	passert(ret == EZFS_BLOCK_SIZE, "Write superblock");

	inode_reset(&inode);
	inode.mode = S_IFDIR | 0777;
	inode.nlink = 3; // 3 because of subdir
	inode.direct_blk_n = EZFS_ROOT_DATABLOCK_NUMBER;
	inode.indirect_blk_n = 0;
	inode.file_size = EZFS_BLOCK_SIZE;
	inode.nblocks = 1;

	/* Write the root inode starting in the second block. */
	ret = write(fd, (char *)&inode, sizeof(inode));
	passert(ret == sizeof(inode), "Write root inode");

	/* The hello.txt file will take inode num following root inode num. */
	inode_reset(&inode);
	inode.nlink = 1;
	inode.mode = S_IFREG | 0666;
	inode.direct_blk_n = EZFS_ROOT_DATABLOCK_NUMBER + 1;
	inode.indirect_blk_n = 0;
	inode.file_size = strlen(hello_contents);
	inode.nblocks = 1;

	ret = write(fd, (char *) &inode, sizeof(inode));
	passert(ret == sizeof(inode), "Write hello.txt inode");


	/* Write subdir inode in second block*/
	inode_reset(&inode);
	inode.mode = S_IFDIR | 0777;
	inode.nlink = 2; // add 1 to 2 because add another directory
	inode.direct_blk_n = EZFS_ROOT_DATABLOCK_NUMBER + 2;
	inode.indirect_blk_n = 0;
	inode.file_size = EZFS_BLOCK_SIZE;
	inode.nblocks = 1;

	/* Write the root inode starting in the second block. */
	ret = write(fd, (char *)&inode, sizeof(inode));
	passert(ret == sizeof(inode), "Write subdir inode");


	/*Write inode for names.txt*/
	inode_reset(&inode);
	inode.nlink = 1;
	inode.mode = S_IFREG | 0666;
	inode.direct_blk_n = 5;
	inode.indirect_blk_n = 0;
	inode.file_size = strlen(name_contents);
	inode.nblocks = 1;

	ret = write(fd, (char *) &inode, sizeof(inode));
	passert(ret == sizeof(inode), "Write names.txt inode");

	/*Write inode for big_img.jpeg*/
	inode_reset(&inode);
	inode.nlink = 1;
	inode.mode = S_IFREG | 0666;
	inode.indirect_blk_n = 16;
	inode.direct_blk_n = 6;
	inode.file_size = length_img;
	inode.nblocks = 8;

	ret = write(fd, (char *) &inode, sizeof(inode));
	passert(ret == sizeof(inode), "Write big_img.jpeg inode");

	/*Write inode for big_txt.txt*/
	inode_reset(&inode);
	inode.nlink = 1;
	inode.mode = S_IFREG | 0666;
	inode.indirect_blk_n = 17;
	inode.direct_blk_n = 14;
	inode.file_size = length_txt; //strlen(big_txt_dir);
	inode.nblocks = 2; //3

	ret = write(fd, (char *) &inode, sizeof(inode));
	passert(ret == sizeof(inode), "Write big_txt.txt inode");


	/* lseek to the next data block */
	ret = lseek(fd, EZFS_BLOCK_SIZE - 6 * sizeof(struct ezfs_inode),
		SEEK_CUR);
	passert(ret >= 0, "Seek past inode table");

	/* dentry for hello.txt */
	dentry_reset(&dentry);
	strncpy(dentry.filename, "hello.txt", sizeof(dentry.filename));
	dentry.active = 1;
	dentry.inode_no = EZFS_ROOT_INODE_NUMBER + 1;

	ret = write(fd, (char *) &dentry, sizeof(dentry));
	passert(ret == sizeof(dentry), "Write dentry for hello.txt");

	/* dentry for subdir */
	dentry_reset(&dentry);
	strncpy(dentry.filename, "subdir", sizeof(dentry.filename));
	dentry.active = 1;
	dentry.inode_no = EZFS_ROOT_INODE_NUMBER + 2;

	ret = write(fd, (char *) &dentry, sizeof(dentry));
	passert(ret == sizeof(dentry), "Write dentry for subdir");


	/* lseek to the next data block */
	len = EZFS_BLOCK_SIZE - 2 * sizeof(struct ezfs_dir_entry);
	ret = write(fd, zeroes, len);
	passert(ret == len, "Pad to end of root dentries");

	/* hello.txt contents */
	len = strlen(hello_contents);
	strncpy(buf, hello_contents, len);
	ret = write(fd, buf, len);
	passert(ret == len, "Write hello.txt contents");

	/* lseek to the next data block */
	len = EZFS_BLOCK_SIZE - len;
	ret = write(fd, zeroes, len);
	passert(ret == len, "Pad to end of hello txt");

	/*dentry for names.txt*/
	dentry_reset(&dentry);
	strncpy(dentry.filename, "names.txt", sizeof(dentry.filename));
	dentry.active = 1;
	dentry.inode_no = EZFS_ROOT_INODE_NUMBER + 3;

	ret = write(fd, (char *) &dentry, sizeof(dentry));
	passert(ret == sizeof(dentry), "Write dentry for names.txt");

	/*dentry for big_img.jpeg*/
	dentry_reset(&dentry);
	strncpy(dentry.filename, "big_img.jpeg", sizeof(dentry.filename));
	dentry.active = 1;
	dentry.inode_no = EZFS_ROOT_INODE_NUMBER + 4;

	ret = write(fd, (char *) &dentry, sizeof(dentry));
	passert(ret == sizeof(dentry), "Write dentry for big_img.jpeg");

	/*dentry for big_txt.txt*/
	dentry_reset(&dentry);
	strncpy(dentry.filename, "big_txt.txt", sizeof(dentry.filename));
	dentry.active = 1;
	dentry.inode_no = EZFS_ROOT_INODE_NUMBER + 5;

	ret = write(fd, (char *) &dentry, sizeof(dentry));
	passert(ret == sizeof(dentry), "Write dentry for big_txt.txt");

	/* lseek to the next data block */
	len = EZFS_BLOCK_SIZE - 3 * sizeof(struct ezfs_dir_entry);
	ret = write(fd, zeroes, len);
	passert(ret == len, "Pad to end of subdir dentries");

	/* names.txt contents */
	len = strlen(name_contents);
	strncpy(buf, name_contents, len);
	ret = write(fd, buf, len);
	passert(ret == len, "Write names.txt contents");

	/* lseek to the next data block */
	len = EZFS_BLOCK_SIZE - len;
	ret = write(fd, zeroes, len);
	passert(ret == len, "Pad to end of name txt");

	/* big_img.jpeg contents */
	big_len = length_img;


	ret = write(fd, big_img_contents, big_len);
	passert(ret == big_len, "write to big_img.jpeg");

	/* lseek to the next data block */
	len = EZFS_BLOCK_SIZE - big_len % EZFS_BLOCK_SIZE;
	ret = write(fd, zeroes, len);
	passert(ret == len, "Pad to end of big_img.jpeg");

	/* big_txt.txt contents */
	big_len = length_txt;
	ret = write(fd, big_txt_contents, big_len);

	/* lseek to the next data block */
	len = EZFS_BLOCK_SIZE - big_len % EZFS_BLOCK_SIZE;
	ret = write(fd, zeroes, len);
	passert(ret == len, "Pad to end of big_txt.txt");

	/* indirect block of big_img.jpeg */
	indirect_length = 0;
	for (int i = 0; i < 7; i++) {
		indirect_num = big_img_start_blk + i + 1;
		printf("direct table number %lu\n", indirect_num);
		indirect_length += sizeof(indirect_num);
		ret = write(fd, &indirect_num, sizeof(indirect_num));
		passert(ret == sizeof(indirect_num), "Write big_img.jpeg indirect block");
	}

	/* lseek to the next data block */
	len = EZFS_BLOCK_SIZE - indirect_length;
	ret = write(fd, zeroes, len);
	passert(ret == len, "Pad to end of big_img.jpeg indirect block");

	/* indirect block of big_txt.txt */
	indirect_length = 0;
	for (int i = 0; i < 1; i++) {
		indirect_num = big_txt_start_blk + i + 1;
		printf("direct table number %lu\n", indirect_num);
		indirect_length += sizeof(indirect_num);
		ret = write(fd, &indirect_num, sizeof(indirect_num));
		passert(ret == sizeof(indirect_num), "Write big_txt.txt indirect block");
	}

	/* lseek to the next data block */
	len = EZFS_BLOCK_SIZE - indirect_length;
	ret = write(fd, zeroes, len);
	passert(ret == len, "Pad to end of big_txt.txt indirect block");


	ret = fsync(fd);
	passert(ret == 0, "Flush writes to disk");

	close(fd);
	printf("Device [%s] formatted successfully.\n", argv[1]);

	free(big_img_contents);
	free(big_txt_contents);

	return 0;
}
