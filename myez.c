#include <linux/string.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/vfs.h>
#include <linux/writeback.h>
#include <linux/uio.h>
#include <linux/uaccess.h>
#include <linux/printk.h>
#include <linux/fs_context.h>
#include "ezfs_ops.h"

MODULE_AUTHOR("Teng Jiang; Mingfeng Yang");
MODULE_DESCRIPTION("Our filesystem");
MODULE_LICENSE("GPL");


// static void print_ezfs_inode(struct inode* ino)
// {
// 	struct ezfs_inode *ezfs_i = (struct ezfs_inode *)ino->i_private;
// 	printf("The nlink is %ld\n", ezfs_i->nlink);
// 	printf("The direct_blk_n is %ld\n", ezfs_i->direct_blk_n);
// 	printf("The indirect_blk_n is %ld\n", ezfs_i->indirect_blk_n);
// 	printf("The file_size is %ld\n", ezfs_i->file_size);
// 	printf("The nblocks is %ld\n", ezfs_i->nblocks);
// }

static inline struct ezfs_sb_buffer_heads *MYEZ_SB(struct super_block *sb)
{
	return sb->s_fs_info;
}


#define printf(format, args...) \
	printk(KERN_ERR "EZFS-fs: %s(): " format, __func__, ## args)

// input an sb, return an ezfs_sb
static struct ezfs_super_block *sb_to_ezfs_sb(struct super_block *sb)
{
	struct ezfs_sb_buffer_heads *info = MYEZ_SB(sb);
	struct ezfs_super_block *ezfs_sb = (struct ezfs_super_block *) info->sb_bh->b_data;
	return ezfs_sb;
}

// get fs specific inode with in-memory superblock
static struct ezfs_inode *get_ezfs_inode(struct super_block *sb, uint64_t ino)
{
	struct ezfs_sb_buffer_heads *ezfs_sb = sb->s_fs_info;
	struct ezfs_inode *di;

	printf("The ino is %llu", ino);
	if ((ino < EZFS_ROOT_INODE_NUMBER) || ino > EZFS_MAX_INODES) {
		printf("Bad inode number in find inode");
		return ERR_PTR(-EIO);
	}

	di = (struct ezfs_inode *) ezfs_sb->i_store_bh->b_data + ino - EZFS_ROOT_INODE_NUMBER;
	return di;
}


// get the inode number of the next inode that's not in use.
static uint64_t ezfs_get_free_ino(struct ezfs_super_block *ezfs_sb)
{
	for (uint64_t i = 0; i < EZFS_MAX_INODES; i++) {
		if (!IS_SET(ezfs_sb->free_inodes, i)) {
			SETBIT(ezfs_sb->free_inodes, i);
			return i + EZFS_ROOT_INODE_NUMBER; // i + 1
		}
	}

	return 0; // If all inodes are used, return 0
}

// release an in-use inode.
static void ezfs_release_ino(struct super_block *sb, uint64_t ino)
{
	struct ezfs_super_block *ezfs_sb = sb_to_ezfs_sb(sb);

	CLEARBIT(ezfs_sb->free_inodes, ino - EZFS_ROOT_INODE_NUMBER);
}

// release an in-use datablock.
static void ezfs_release_data_blocks(struct super_block *sb, struct ezfs_inode *ei)
{
	struct buffer_head *bh;
	struct ezfs_super_block *ezfs_sb = sb_to_ezfs_sb(sb);
	uint64_t *addr;
	// CLEARBIT(ezfs_sb->free_data_blocks, ei->direct_blk_n);
	// CLEARBIT(ezfs_sb->free_data_blocks, ei->indirect_blk_n);

	// for (i = 0; i < ei->nblocks - 2; i++) // -2?
	// 	CLEARBIT(ezfs_sb->free_data_blocks, something);
	if (ei->nblocks == 1) {
		CLEARBIT(ezfs_sb->free_data_blocks, ei->direct_blk_n);
	} else if (ei->nblocks > 1) {
		bh = sb_bread(sb, ei->indirect_blk_n);
		if (!bh) {
			printf("when release indirect block, invalid indirect block.\n");
			return;
		}
		addr = (uint64_t *) bh->b_data;
		for (int i = 0; i < EZFS_BLOCK_SIZE/sizeof(uint64_t); i++) {
			if (*addr != 0) {
				printf("when we clean indirect block, we clean %llu.\n", *addr);
				CLEARBIT(ezfs_sb->free_data_blocks, *addr);
			}
			addr += 1;
		}
		CLEARBIT(ezfs_sb->free_data_blocks, ei->direct_blk_n);
		CLEARBIT(ezfs_sb->free_data_blocks, ei->indirect_blk_n);
		brelse(bh);
	}
}



// get an existing inode from disk, then fill the struct inode.
// if the inode is already in-memory, then return the inode immediately.
// If the inode is not in-memory, then fill it, unset I_NEW, unlock it, and return the inode.
struct inode *ezfs_iget(struct super_block *sb, unsigned long ino)
{
	struct ezfs_inode *di;
	struct inode *inode;
	struct buffer_head *bh;

	//printf("The start of ezfs_iget");

	inode = iget_locked(sb, ino); // this only gets the ezfs_inode and put it in the in-memory inode. It does not care about whether the data block is in-use or not.
	if (!inode)
		return ERR_PTR(-ENOMEM);
	if (!(inode->i_state & I_NEW))
		return inode;

	if ((ino < EZFS_ROOT_INODE_NUMBER) || ino > EZFS_MAX_INODES) {
		printf("Bad inode number %s:%08lx\n", inode->i_sb->s_id, ino);
		goto error;
	}

	bh = MYEZ_SB(sb)->i_store_bh;
	if (!bh) {
		printf("Unable to read inode %s:%08lx\n", inode->i_sb->s_id,
									ino);
		goto error;
	}

	di = (struct ezfs_inode *)bh->b_data + ino - EZFS_ROOT_INODE_NUMBER;
	// if (di->nlink == 0) {
	// 	printf("An new ezfs_inode should be added.\n");
	// 	di->nlink = 1;
	// 	di->direct_blk_n = 0;
	// 	di->indirect_blk_n = 0;
	// 	di->nblocks = 0;
	// 	di->file_size = 0;
	// 	mark_buffer_dirty(bh);
	// }
	//printf("The nlink is %d", di->nlink);

	// inode->i_mode = di->mode;
	// printf("The direct block is %llu\n", di->direct_blk_n);
	// printf("The imode is %d\n", di->mode);
	// printf("It should be %d\n", S_IFDIR | 0777);
	// inode->i_private = kzalloc(sizeof(struct ezfs_inode), GFP_KERNEL);
	// memcpy(inode->i_private, di, sizeof(struct ezfs_inode));
	inode->i_private = di;

	inode->i_mode = 0x0000FFFF & le32_to_cpu(di->mode);
	if (di->mode & S_IFDIR) {
		printf("This is a directory\n");
		inode->i_mode |= S_IFDIR;
		inode->i_op = &ezfs_dir_inops;      // we need to initialize
		inode->i_fop = &ezfs_dir_operations;        // we need to initialize
	} else if (di->mode & S_IFREG) {
		printf("This is a regular file\n");
		inode->i_mode |= S_IFREG;
		inode->i_op = &ezfs_file_inops;         // we need to initialize
		inode->i_fop = &ezfs_file_operations;         // we need to initialize
		inode->i_mapping->a_ops = &ezfs_aops;      // we need to initialize
	}

	i_uid_write(inode, le32_to_cpu(di->uid));
	i_gid_write(inode,  le32_to_cpu(di->gid));
	set_nlink(inode, le32_to_cpu(di->nlink));
	inode->i_size = di->file_size;
	inode->i_blocks = di->nblocks * 8;
	inode->i_atime.tv_sec =  le32_to_cpu(di->i_atime.tv_sec);
	inode->i_mtime.tv_sec =  le32_to_cpu(di->i_mtime.tv_sec);
	inode->i_ctime.tv_sec =  le32_to_cpu(di->i_ctime.tv_sec);
	inode->i_atime.tv_nsec = 0;
	inode->i_mtime.tv_nsec = 0;
	inode->i_ctime.tv_nsec = 0;

	unlock_new_inode(inode);
	return inode;


error:
	iget_failed(inode);
	return ERR_PTR(-EIO);
}

// This function is to find a free datablock.
static uint64_t find_free_block(struct super_block *sb)
{
	uint64_t block_num = 0;
	struct ezfs_sb_buffer_heads *ezfs_sb_bh = MYEZ_SB(sb);
	struct ezfs_super_block *ezfs_sb = (struct ezfs_super_block *) ezfs_sb_bh->sb_bh->b_data;

	for (int i = EZFS_ROOT_DATABLOCK_NUMBER; i < ezfs_sb->disk_blks; i++) {
		if (!IS_SET(ezfs_sb->free_data_blocks, i)) {
			SETBIT(ezfs_sb->free_data_blocks, i);
			mark_buffer_dirty(ezfs_sb_bh->sb_bh);
			block_num = i;
			return block_num;
		}
	}
	return block_num;
}

/*add the file to the directory*/
static int ezfs_add_entry(struct inode *dir, const struct qstr *child, int ino)
{
	struct ezfs_dir_entry *ezfs_dirent;
	struct ezfs_inode *ezfs_dir_i = (struct ezfs_inode *) dir->i_private;
	struct buffer_head *dir_bh = sb_bread(dir->i_sb, ezfs_dir_i->direct_blk_n);
	char *addr = dir_bh->b_data;

	printf("test ezfs_add_entry.\n");

	for (loff_t i = 0; i < EZFS_MAX_CHILDREN; i++) {
		ezfs_dirent = ((struct ezfs_dir_entry *) addr) + i;
		if (!ezfs_dirent->active) {
			printf("find an empty directory entry.\n");
			ezfs_dirent->inode_no = ino;
			memcpy(ezfs_dirent->filename, child->name, EZFS_FILENAME_BUF_SIZE);
			ezfs_dirent->active = 1;
			mark_buffer_dirty(dir_bh);
			brelse(dir_bh);
			return 0;
		}
	}
	brelse(dir_bh);
	return 1;

}



// Instantiate a new inode with different modes
// dir: The parent inode
struct inode *ezfs_get_inode(struct super_block *sb,
			struct inode *dir, umode_t mode, dev_t dev, struct dentry *dentry)
{
	struct inode *inode = new_inode(sb);
	struct ezfs_super_block *ezfs_sb = sb_to_ezfs_sb(sb);
	struct ezfs_inode *ezfs_i;
	int err;

	printf("start of get inode.\n");
	mutex_lock(ezfs_sb->ezfs_lock);
	if (inode) {
		inode->i_ino = ezfs_get_free_ino(ezfs_sb);
		if (!inode->i_ino) {
			mutex_unlock(ezfs_sb->ezfs_lock);
			iput(inode);
			printf("There is no available inode.\n");
			return NULL;
		}
		//printf("before get ezfs inode %llu", inode->i_ino);
		ezfs_i = get_ezfs_inode(sb, inode->i_ino);
		inode->i_private = ezfs_i;

		inode_init_owner(&init_user_ns, inode, dir, mode);
		// inode->i_uid = dir->i_uid;
		// inode->i_gid = dir->i_gid;
		inode->i_mapping->a_ops = &ezfs_aops;
		mapping_set_gfp_mask(inode->i_mapping, GFP_HIGHUSER);
		mapping_set_unevictable(inode->i_mapping);
		inode->i_atime = inode->i_mtime = inode->i_ctime = current_time(inode);
		//printf("test point 0.\n");
		switch (mode & S_IFMT) {
		default:
			printf("create a special inode?\n");
			init_special_inode(inode, mode, dev);
			break;
		case S_IFREG:
			//printf("test point 1.\n");
			inode->i_blocks = 0;
			inode->i_size = 0;
			inode->i_op = &ezfs_file_inops;
			inode->i_fop = &ezfs_file_operations;
			// All inode starts with link = 1, if we're using new_inode.
			ezfs_i->direct_blk_n = 0;
			ezfs_i->indirect_blk_n = 0;
			printf("The nlink is %d.\n", inode->i_nlink);
			printf("The __nlink is %d.\n", inode->__i_nlink);
			// insert_inode_hash(inode);
			//printf("test point 2.\n");
			err = ezfs_add_entry(dir, &dentry->d_name, inode->i_ino);
			if (err) {
				printf("line 233, error adding entry.\n");
				ezfs_release_ino(sb, inode->i_ino);
				inode_dec_link_count(inode);
				printf("The nlink is %d.\n", inode->i_nlink);
				printf("The __nlink is %d.\n", inode->__i_nlink);
				mutex_unlock(ezfs_sb->ezfs_lock);
				iput(inode);
				return NULL;
			}
			insert_inode_hash(inode);
			mark_inode_dirty(inode);
			break;
		case S_IFDIR:
			inode->i_op = &ezfs_dir_inops;
			inode->i_fop = &ezfs_dir_operations;
			inode->i_size = EZFS_BLOCK_SIZE;
			inode->i_blocks = 8;
			ezfs_i->direct_blk_n = find_free_block(sb);
			if (ezfs_i->direct_blk_n == 0) {
				printf("There is No free block.\n");
				ezfs_release_ino(sb, inode->i_ino);
				inode_dec_link_count(inode);
				mutex_unlock(ezfs_sb->ezfs_lock);
				iput(inode);
				return NULL;
			}
			err = ezfs_add_entry(dir, &dentry->d_name, inode->i_ino);
			if (err) {
				printf("Cannot add dir_entry.\n");
				ezfs_release_ino(sb, inode->i_ino);
				inode_dec_link_count(inode);
				CLEARBIT(ezfs_sb->free_data_blocks, ezfs_i->direct_blk_n);
				printf("The nlink is %d.\n", inode->i_nlink);
				printf("The __nlink is %d.\n", inode->__i_nlink);
				mutex_unlock(ezfs_sb->ezfs_lock);
				iput(inode);
				return NULL;
			}
			insert_inode_hash(inode);
			mark_inode_dirty(inode);
			/* directory inodes start off with i_nlink == 2 (for "." entry) */
			inc_nlink(inode);
			break;
		case S_IFLNK:
			printf("create a link file.\n");
			inode->i_op = &page_symlink_inode_operations;
			inode_nohighmem(inode);
			break;
		}
	}
	mutex_unlock(ezfs_sb->ezfs_lock);
	return inode;
}

int ezfs_mknod(struct user_namespace *mnt_userns, struct inode *dir,
		struct dentry *dentry, umode_t mode, dev_t dev)
{
	struct inode *inode = ezfs_get_inode(dir->i_sb, dir, mode, dev, dentry);
	int error = -ENOSPC;

	if (inode) {
		d_instantiate(dentry, inode);
		// dget(dentry);	/* Extra count - pin the dentry in core */ // cannot be used in other fs. you need to delete dentry if needed
		error = 0;
		dir->i_mtime = dir->i_ctime = current_time(dir);
	}
	return error;
}

/*define the functions of super operations*/
// static struct kmem_cache *ezfs_inode_cachep;

inline int ezfs_namecmp(int len, const unsigned char *name,
							const char *buffer)
{
	if ((len < EZFS_MAX_FILENAME_LENGTH) && buffer[len])
		return 0;
	//printf("In ezfs_namecmp, the name is %s", name);
	//printf("In ezfs_namecmp, the buffer is %s", buffer);
	return !memcmp(name, buffer, len);
}

// find an entry inside of the directory's dentry table.
// It returns the bh of the direct block of dir,
// and set res_dir to the pointer to the entry of the desired name.
struct buffer_head *ezfs_find_entry(struct inode *dir,
			const struct qstr *child,
			struct ezfs_dir_entry **res_dir)
{
	struct buffer_head *bh = NULL;
	struct ezfs_dir_entry *de;
	const unsigned char *name = child->name;
	int namelen = child->len;
	struct ezfs_inode *ezfs_i = (struct ezfs_inode *) dir->i_private;
	char *addr;

	//printf("The name is %s", name);

	*res_dir = NULL;
	if (namelen > EZFS_MAX_FILENAME_LENGTH)
		return NULL;

	bh = sb_bread(dir->i_sb, ezfs_i->direct_blk_n);
	if (!bh)
		return NULL;
	addr = (char *) bh->b_data;
	de = (struct ezfs_dir_entry *) addr;
	//printf("The name of dir_entry is %s, the active state is %ld", de->filename, de->active);

	for (int i = 0; i < EZFS_MAX_CHILDREN; i++) {
		if (de->inode_no && ezfs_namecmp(namelen, name, de->filename) && de->active) {
			*res_dir = de;
			return bh;
		}
		de += 1;
	}

	brelse(bh);
	return NULL;
}


// what does this do?
struct dentry *ezfs_lookup(struct inode *dir, struct dentry *dentry,
						unsigned int flags)
{
	struct inode *inode = NULL;
	struct buffer_head *bh;
	struct ezfs_dir_entry *de;
	struct ezfs_sb_buffer_heads *ezfs_sb_bhs = (struct ezfs_sb_buffer_heads *)dir->i_sb->s_fs_info;
	struct ezfs_super_block *ezfs_sb = (struct ezfs_super_block *) ezfs_sb_bhs->sb_bh->b_data;


	printf("lookup\n");

	if (dentry->d_name.len > EZFS_MAX_FILENAME_LENGTH)
		return ERR_PTR(-ENAMETOOLONG);

	mutex_lock(ezfs_sb->ezfs_lock);
	bh = ezfs_find_entry(dir, &dentry->d_name, &de);
	if (bh) {
		unsigned long ino = (unsigned long)le16_to_cpu(de->inode_no);
		brelse(bh);
		inode = ezfs_iget(dir->i_sb, ino);
	}
	mutex_unlock(ezfs_sb->ezfs_lock);
	return d_splice_alias(inode, dentry);
}

// this function is for ls -a
// VFS will continue to call iterate_shared until your implementation returns without calling dir_emit().
// for dir_emit_dots, if ctxpos = 0, set ctxpos to 2(emit . and ..) else do nothing
// emit means adding the entry to ls -a.
int ezfs_readdir(struct file *file, struct dir_context *ctx)
{
	struct inode *inode = file_inode(file);
	struct ezfs_inode *ez_inode = (struct ezfs_inode *) inode->i_private;
	struct buffer_head *bh;
	struct ezfs_dir_entry *ezfs_dir;
	char *addr;
	int size;

	printf("ezfs_readdir being called. \n");
	//printf("The value of cts->pos %lld\n", ctx->pos);

	printf("The ctx->pos is %lld before emit_dots\n", ctx->pos);
	if (!dir_emit_dots(file, ctx)) // if ctxpos = 0, +2
		return 0;
	printf("The ctx->pos is %lld after emit_dots\n", ctx->pos);
	//printf("The i_size is %lld\n", inode->i_size);
	//printf("The direct_block of ez_node %d\n", ez_inode->direct_blk_n);
	//printf("The nblock of ez_node %d\n", ez_inode->nblocks);

	bh = sb_bread(inode->i_sb, ez_inode->direct_blk_n);
	if (!bh)
		return -EINVAL;

	//printf("The value of cts->pos %lld\n", ctx->pos);

	//printf("The nblock of inode %d\n", inode->i_blocks);



	addr = bh->b_data;
	ezfs_dir = (struct ezfs_dir_entry *)addr;
	// ezfs_dir = (struct ezfs_dir_entry *)addr;
	// size = strnlen(ezfs_dir->filename, EZFS_FILENAME_BUF_SIZE);
	// printf("The state of ezfs_dir %d\n", ezfs_dir->active);
	// printf("The inode of ezfs_dir %d\n", ezfs_dir->inode_no);
	// printf("The name of ezfs_dir %s\n", ezfs_dir->filename);
	// if (ctx->pos < EZFS_BLOCK_SIZE)
	// 	dir_emit(ctx, ezfs_dir->filename, size,
	//  					le64_to_cpu(ezfs_dir->inode_no),
	//  					DT_UNKNOWN);
	// ctx->pos = EZFS_BLOCK_SIZE;
	// printf("Done with ezfs_readdir\n");
	// brelse(bh);
	// return 0;

	for (; ctx->pos < EZFS_BLOCK_SIZE; ctx->pos += sizeof(struct ezfs_dir_entry)) { // won't stop until we reach 4096.
		size = strnlen(ezfs_dir->filename, EZFS_MAX_FILENAME_LENGTH);
		if (ezfs_dir->active && ezfs_dir->inode_no && size > 0 && size < EZFS_MAX_FILENAME_LENGTH) {
			printf("The ctx->pos is %lld \n", ctx->pos);
			printf("The name is %s\n", ezfs_dir->filename);
			printf("The inode is %llu\n", ezfs_dir->inode_no);
			printf("The mode is %d\n", fs_umode_to_dtype(get_ezfs_inode(inode->i_sb, ezfs_dir->inode_no)->mode));
			//printf("before get ezfs inode %llu", ezfs_dir->inode_no);
			if (!dir_emit(ctx, ezfs_dir->filename, size,
						le64_to_cpu(ezfs_dir->inode_no),
						fs_umode_to_dtype(get_ezfs_inode(inode->i_sb, ezfs_dir->inode_no)->mode))) { // emit fail
					brelse(bh);
					return 0;
				}
		}
		addr += sizeof(struct ezfs_dir_entry);
		//printf("The addr is %ld\n", addr);
		ezfs_dir = (struct ezfs_dir_entry *)addr;
	}
	//printf("The end of the readdir\n");
	ctx->pos = EZFS_BLOCK_SIZE;
	brelse(bh);
	return 0;
}


// struct inode *ezfs_alloc_inode(struct super_block *sb)
// {
// 	struct ezfs_inode_info *bi;

// 	// //printf("Test alloc_inode\n");
// 	// //printf("Test alloc_inode\n");
// 	// bi = kmem_cache_alloc(ezfs_inode_cachep, GFP_KERNEL);
// 	// if (!bi)
// 	// 	return NULL;
// 	return &bi->vfs_inode;
// }

// void ezfs_free_inode(struct inode *inode)
// {
// 	//printf("Test free_inode\n");
// 	//printf("Test free_inode\n");
// 	//kmem_cache_free(ezfs_inode_cachep, inode);
// }

// struct ezfs_inode *find_inode(struct super_block *sb, uint64_t ino)
// {
// 	struct buffer_head *bh;
// 	struct ezfs_inode *ez_i;

// 	if ((ino < EZFS_ROOT_INODE_NUMBER) || ino > EZFS_MAX_INODES) {
// 		printf("Bad inode number in find inode");
// 		return ERR_PTR(-EIO);
// 	}

// 	bh = MYEZ_SB(sb)->i_store_bh;

// 	ino -= EZFS_ROOT_INODE_NUMBER;

// 	ez_i = (struct ezfs_inode *)bh->b_data + ino;

// 	return ez_i;
// }

int ezfs_write_inode(struct inode *inode, struct writeback_control *wbc)
{
	struct ezfs_inode *di;
	struct ezfs_sb_buffer_heads *ezfs_sb_bhs =
		(struct ezfs_sb_buffer_heads *)inode->i_sb->s_fs_info;
	struct ezfs_super_block *ezfs_sb = (struct ezfs_super_block *) ezfs_sb_bhs->sb_bh->b_data;
	int err = 0;
	//printf("before get ezfs inode %llu", inode->i_ino);
	di = get_ezfs_inode(inode->i_sb, inode->i_ino);

	if (IS_ERR(di))
		return PTR_ERR(di);

	mutex_lock(ezfs_sb->ezfs_lock);

	di->mode = cpu_to_le32(inode->i_mode);
	di->uid = cpu_to_le32(i_uid_read(inode));
	di->gid = cpu_to_le32(i_gid_read(inode));
	printf("The nlink is %d.\n", inode->i_nlink);
	printf("The __nlink is %d.\n", inode->__i_nlink);
	if (inode->i_nlink)
		di->nlink = cpu_to_le32(inode->i_nlink);
	else
		di->nlink = cpu_to_le32(inode->__i_nlink);
	di->i_atime = inode->i_atime;
	di->i_mtime = inode->i_mtime;
	di->i_ctime = inode->i_ctime;
	di->file_size = inode->i_size;
	printf("In write_inode: the i_size is %llu.\n", inode->i_size);
	/*We want to change the blocks*/

	di->nblocks = inode->i_blocks / 8;

	printf("In write_inode: the i_blocks is %llu.\n", inode->i_blocks);

	mark_buffer_dirty(ezfs_sb_bhs->i_store_bh);
	if (wbc->sync_mode == WB_SYNC_ALL) {
		sync_dirty_buffer(ezfs_sb_bhs->i_store_bh);
		if (buffer_req(ezfs_sb_bhs->i_store_bh) && !buffer_uptodate(ezfs_sb_bhs->i_store_bh))
			err = -EIO;
	}
	mutex_unlock(ezfs_sb->ezfs_lock);
	return err;

}

/*bfs_write_inode: sync_*/
void ezfs_evict_inode(struct inode *inode)
{
	unsigned long ino = inode->i_ino;
	struct ezfs_inode *di;
	struct super_block *s = inode->i_sb;
	struct ezfs_sb_buffer_heads *info = MYEZ_SB(s);
	struct ezfs_super_block *ezfs_sb = (struct ezfs_super_block *) info->sb_bh->b_data;
	struct buffer_head *bh, *sb_bh;

	bh = info->i_store_bh;
	sb_bh = info->sb_bh;

	printf("evict_inode_start.\n");
	truncate_inode_pages_final(&inode->i_data);
	printf("evict_inode test point 1.\n");
	invalidate_inode_buffers(inode);
	printf("evict_inode test point 2.\n");
	clear_inode(inode);
	printf("evict_inode test point 3.\n");

	if (inode->i_nlink)
		return;
	//printf("before get ezfs inode %llu", ino);
	di = get_ezfs_inode(s, ino);
	if (IS_ERR(di))
		return;

	mutex_lock(ezfs_sb->ezfs_lock);

	ezfs_release_ino(s, ino);

	ezfs_release_data_blocks(s, di);


	/* clear on-disk inode */
	memset(di, 0, sizeof(struct ezfs_inode));

	// CLEARBIT(ezfs_sb->free_inodes, ino - EZFS_ROOT_INODE_NUMBER);

	// CLEARBIT(ezfs_sb->free_data_blocks, direct and indirect - EZFS_ROOT_INODE_NUMBER);

	mark_buffer_dirty(bh);

	mark_buffer_dirty(sb_bh);

	mutex_unlock(ezfs_sb->ezfs_lock);
	printf("evict_inode test point 4.\n");
}


int ezfs_link(struct dentry *old, struct inode *dir,
						struct dentry *new)
{
	struct inode *inode = d_inode(old);
	struct ezfs_super_block *ezfs_sb = sb_to_ezfs_sb(inode->i_sb);
	int err;

	printf("ezfs_link test here.\n");
	mutex_lock(ezfs_sb->ezfs_lock);
	err = ezfs_add_entry(dir, &new->d_name, inode->i_ino);
	if (err) {
		mutex_unlock(ezfs_sb->ezfs_lock);
		return err;
	}
	inc_nlink(inode);
	inode->i_ctime = current_time(inode);
	mark_inode_dirty(inode);
	ihold(inode);
	d_instantiate(new, inode);
	printf("ezfs_link test point 2.\n");
	mutex_unlock(ezfs_sb->ezfs_lock);
	return 0;
}

int ezfs_unlink(struct inode *dir, struct dentry *dentry)
{
	int error = -ENOENT;
	struct inode *inode = d_inode(dentry);
	struct buffer_head *bh;
	struct ezfs_dir_entry *de;
	struct ezfs_super_block *ezfs_sb = sb_to_ezfs_sb(dir->i_sb);

	printf("start of unlink.\n");
	mutex_lock(ezfs_sb->ezfs_lock);
	bh = ezfs_find_entry(dir, &dentry->d_name, &de); // return bh, set de to the according de
	printf("The name of the file is %s", (&dentry->d_name)->name);
	if (!bh || (le16_to_cpu(de->inode_no) != inode->i_ino) || !de->active)
		goto out_brelse;

	if (!inode->i_nlink) { // why?
		printf("unlinking non-existent file %s:%lu (nlink=%d)\n",
					inode->i_sb->s_id, inode->i_ino,
					inode->i_nlink);
		set_nlink(inode, 1);
	}
	printf("start unlinking.\n");
	memset(de->filename, 0, EZFS_MAX_FILENAME_LENGTH);
	memset(de, 0, sizeof(struct ezfs_dir_entry));
	mark_buffer_dirty_inode(bh, dir);
	dir->i_ctime = dir->i_mtime = current_time(dir);
	mark_inode_dirty(dir);
	inode->i_ctime = dir->i_ctime;
	inode_dec_link_count(inode);
	printf("The nlink is %d.\n", inode->i_nlink);
	printf("The __nlink is %d.\n", inode->__i_nlink);
	printf("The refcount of dentry is %d.\n", dentry->d_lockref.count);
	mark_inode_dirty(inode);
	error = 0;

out_brelse:
	printf("end of unlink.\n");
	brelse(bh);
	mutex_unlock(ezfs_sb->ezfs_lock);
	return error;
}

void ezfs_put_super(struct super_block *s)
{
	struct ezfs_sb_buffer_heads *info = MYEZ_SB(s);
	struct ezfs_super_block *ezfs_sb = (struct ezfs_super_block *) info->sb_bh->b_data;

	if (!info)
		return;
	printf("umount the file system, clear everything\n");
	mutex_destroy(ezfs_sb->ezfs_lock);
	kfree(ezfs_sb->ezfs_lock);
	mark_buffer_dirty(info->sb_bh);
	mark_buffer_dirty(info->i_store_bh);
	brelse(info->sb_bh);
	brelse(info->i_store_bh);
	kfree(info);
	s->s_fs_info = NULL;
}

int ezfs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct super_block *s = dentry->d_sb;
	u64 id = huge_encode_dev(s->s_bdev->bd_dev);
	buf->f_type = EZFS_MAGIC_NUMBER;
	buf->f_bsize = s->s_blocksize;
	// buf->f_blocks = info->si_blocks;
	// buf->f_bfree = buf->f_bavail = info->si_freeb;
	// buf->f_files = info->si_lasti + 1 - BFS_ROOT_INO;
	// buf->f_ffree = info->si_freei;
	buf->f_fsid = u64_to_fsid(id);
	buf->f_namelen = EZFS_MAX_FILENAME_LENGTH;
	return 0;
}


int ezfs_create(struct user_namespace *mnt_userns, struct inode *dir,
			struct dentry *dentry, umode_t mode, bool excl)
{
	// int err;
	// struct inode *inode;
	// struct super_block *s = dir->i_sb;
	// unsigned long ino;

	// mutex_lock(ezfs_sb->ezfs_lock);
	// mutex_unlock(ezfs_sb->ezfs_lock);
	return ezfs_mknod(&init_user_ns, dir, dentry, mode | S_IFREG, 0);
	// printf("create test.\n");
	// printf("create test.\n");
	// printf("create test.\n");
	// printf("create test.\n");
	// printf("create test.\n");
	// return 0;
}

int ezfs_mkdir(struct user_namespace *mnt_userns, struct inode *dir,
			   struct dentry *dentry, umode_t mode)
{
	int retval = ezfs_mknod(&init_user_ns, dir, dentry, mode | S_IFDIR, 0);
	if (!retval)
		inc_nlink(dir);
	return retval;
}

int check_empty_dir(struct inode *dir)
{
	struct ezfs_dir_entry *ezfs_dir;
	struct ezfs_inode *ezfs_i = (struct ezfs_inode *) dir->i_private;
	struct buffer_head *bh = sb_bread(dir->i_sb, ezfs_i->direct_blk_n);

	ezfs_dir = (struct ezfs_dir_entry *) bh->b_data;

	for (loff_t i = 0; i < EZFS_MAX_CHILDREN; i++) {
		if (ezfs_dir->active) {
			printf("in check_empty_dir, the file name is %s, inode is %llu.\n", ezfs_dir->filename, ezfs_dir->inode_no);
			return 0;
		}
		ezfs_dir += 1;
	}
	return 1;
}

int ezfs_rmdir(struct inode *dir, struct dentry *dentry)
{
	struct inode *this_dir = d_inode(dentry);
	printf("is empty %d\n", simple_empty(dentry));
	printf("check_empty_dir %d\n", check_empty_dir(this_dir));
	if (!simple_empty(dentry) || !check_empty_dir(this_dir))
		return -ENOTEMPTY;

	drop_nlink(d_inode(dentry));
	ezfs_unlink(dir, dentry);
	drop_nlink(dir);
	mark_inode_dirty(dir);
	return 0;
}


static int ezfs_rename(struct user_namespace *mnt_userns, struct inode *old_dir,
			  struct dentry *old_dentry, struct inode *new_dir,
			  struct dentry *new_dentry, unsigned int flags)
{
	struct inode *old_inode;
	struct buffer_head *old_bh;
	struct ezfs_dir_entry *old_de;
	struct ezfs_super_block *ezfs_sb;
	int error = -ENOENT;

	if (flags & ~RENAME_NOREPLACE)
		return -EINVAL;
	printf("rename.\n");
	old_bh  = NULL;
	old_inode = d_inode(old_dentry);
	if (S_ISDIR(old_inode->i_mode))
		return -EINVAL;

	ezfs_sb = sb_to_ezfs_sb(old_inode->i_sb);

	mutex_lock(ezfs_sb->ezfs_lock);
	old_bh = ezfs_find_entry(old_dir, &old_dentry->d_name, &old_de);

	if (!old_bh || (le16_to_cpu(old_de->inode_no) != old_inode->i_ino))
		goto end_rename;

	error = -EPERM;

	memset(old_de->filename, 0, EZFS_MAX_FILENAME_LENGTH);
	memcpy(old_de->filename, (&new_dentry->d_name)->name, EZFS_MAX_FILENAME_LENGTH);
	old_dir->i_ctime = old_dir->i_mtime = current_time(old_dir);
	mark_inode_dirty(old_dir);
	mark_buffer_dirty(old_bh);
	error = 0;

end_rename:
	mutex_unlock(ezfs_sb->ezfs_lock);
	brelse(old_bh);
	return error;
	// printf("rename.\n");
	// return 0;
}


static int ezfs_get_block(struct inode *inode, sector_t block,
			struct buffer_head *bh_result, int create)
{
	// print_ezfs_inode(inode);
	// printf("The block size is %ld\n", block);
	// printf("The create is %d", create);
	// map_bh(bh_result, inode->i_sb, 3);
	int err = 0;
	struct super_block *sb = inode->i_sb;
	struct ezfs_inode *ezfs_i = (struct ezfs_inode *) inode->i_private;
	uint64_t *indirect_num;
	struct buffer_head *bh;
	char *addr;
	struct ezfs_sb_buffer_heads *info = MYEZ_SB(sb);
	struct ezfs_super_block *ezfs_sb = (struct ezfs_super_block *) info->sb_bh->b_data;
	uint64_t free_block_num;
	int indirect_new = 0;


	mutex_lock(ezfs_sb->ezfs_lock);
	if (block == 0) {
		if (ezfs_i->direct_blk_n < 0) {
			printf("Invalid direct_blk_n.\n");
			err = -ENOSPC;
			goto out;
		} else if (ezfs_i->direct_blk_n == 0) {
			if (!create)
				goto out;
			/*We need to assign a new direct block to the inode*/
			free_block_num = find_free_block(sb);
			printf("The new free block is %llu.\n", free_block_num);
			if (!free_block_num) {
				printf("There is no free data block.\n");
				err = -ENOSPC;
				goto out;
			}
			// inode->i_blocks++;
			ezfs_i->direct_blk_n = free_block_num;
			mark_inode_dirty(inode);
			map_bh(bh_result, sb, ezfs_i->direct_blk_n);
		} else {
			printf("We have direct block, the block is %llu.\n", ezfs_i->direct_blk_n);
			map_bh(bh_result, sb, ezfs_i->direct_blk_n);
		}
	} else if (block > 0) {
		if (block >= EZFS_BLOCK_SIZE/sizeof(uint64_t)) {
			printf("The file size has exceeded the maximum.\n");
			err = -ENOSPC;
			goto out;
		}
		if (ezfs_i->indirect_blk_n < 0) {
			printf("Invalid indirect_blk_n.\n");
			err = -ENOSPC;
			goto out;
		}

		if (ezfs_i->indirect_blk_n == 0) {
			if (!create)
				goto out;
			/*We need to first assign a indirect block*/
			free_block_num = find_free_block(sb);
			printf("The new free block for indirect block is %llu.\n", free_block_num);
			if (!free_block_num) {
				printf("There is no free data block.\n");
				err = -ENOSPC;
				goto out;
			}
			ezfs_i->indirect_blk_n = free_block_num;
			indirect_new = 1;
		}

		bh = sb_bread(sb, ezfs_i->indirect_blk_n);
		if (indirect_new)
			memset(bh->b_data, 0, EZFS_BLOCK_SIZE);
		addr = bh->b_data + sizeof(uint64_t) * (block - 1);
		indirect_num = (uint64_t *) addr;
		if (*indirect_num) {
			printf("We have indirect blocks, the block is %llu.\n", *indirect_num);
			map_bh(bh_result, sb, *indirect_num);
			brelse(bh);
		} else {
			if (!create) {
				brelse(bh);
				goto out;
			}
			free_block_num = find_free_block(sb);
			printf("The new free block is %llu.\n", free_block_num);
			if (!free_block_num) {
				brelse(bh);
				printf("There is no free data block.\n");
				err = -ENOSPC;
				goto out;
			}
			*indirect_num = free_block_num;
			// inode->i_blocks++;
			mark_buffer_dirty(bh);
			printf("We have new indirect blocks, the block is %llu.\n", *indirect_num);
			map_bh(bh_result, sb, *indirect_num);
			brelse(bh);
		}
		mark_inode_dirty(inode);
	}
out:
	mutex_unlock(ezfs_sb->ezfs_lock);
	return err;
}

int ezfs_read_folio(struct file *file, struct folio *folio)
{
	return block_read_full_folio(folio, ezfs_get_block);
}

static int ezfs_fill_super(struct super_block *s, struct fs_context *fsc)
{
	struct ezfs_sb_buffer_heads *info;
	struct ezfs_super_block *ezfs_sb;
	struct inode *inode;
	int ret = -EINVAL;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	s->s_fs_info = info;
	s->s_time_min = 0;
	s->s_time_max = U32_MAX;

	printk(KERN_INFO "test point1\n");
	sb_set_blocksize(s, EZFS_BLOCK_SIZE);
	info->sb_bh = sb_bread(s, 0);
	if (!info->sb_bh)
		goto out;
	printk(KERN_INFO "test point2\n");
	ezfs_sb = (struct ezfs_super_block *) info->sb_bh->b_data;

	ezfs_sb->ezfs_lock = kzalloc(sizeof(struct mutex), GFP_KERNEL);
	mark_buffer_dirty(info->sb_bh);

	mutex_init(ezfs_sb->ezfs_lock);
	if (ezfs_sb->magic != EZFS_MAGIC_NUMBER) {
		printf("No EZFS on the %s\n", s->s_id);
		goto out1;
	}
	printk(KERN_INFO "test point3\n");
	s->s_magic = EZFS_MAGIC_NUMBER;

	if (ezfs_sb->disk_blks > EZFS_MAX_DATA_BLKS) {
		printf("The number of data blocks exceeds the maximum");
		goto out1;
	}

	printk(KERN_INFO "test point4\n");


	info->i_store_bh = sb_bread(s, 1);

	if (!info->i_store_bh)
		goto out1;

	s->s_op = &ezfs_sops;
	//printk(KERN_INFO "test point2\n");
	inode = ezfs_iget(s, EZFS_ROOT_INODE_NUMBER);
	if (IS_ERR(inode)) {
		ret = PTR_ERR(inode);
		goto out2;
	}
	//printk(KERN_INFO "test point3\n");


	s->s_root = d_make_root(inode);
	if (!s->s_root) {
		ret = -ENOMEM;
		goto out2;
	}
	// printk(KERN_INFO "test point4\n");
	// printk(KERN_INFO "test point4\n");
	// printk(KERN_INFO "test point4\n");

	return 0;

out2:
	brelse(info->i_store_bh);
	printk(KERN_INFO "There is error 1");
out1:
	brelse(info->sb_bh);
	printk(KERN_INFO "There is error 2");
	mutex_destroy(ezfs_sb->ezfs_lock);
	kfree(ezfs_sb->ezfs_lock);
out:
	kfree(info);
	printk(KERN_INFO "There is error 3");
	s->s_fs_info = NULL;
	return ret;

}

// for write how to deal with concurrency
int ezfs_writepage(struct page *page, struct writeback_control *wbc)
{
	printf("ezfs_writepage test");
	return block_write_full_page(page, ezfs_get_block, wbc);
}

void ezfs_write_failed(struct address_space *mapping, loff_t to)
{
	struct inode *inode = mapping->host;

	printf("ezfs_write_failed test");
	if (to > inode->i_size)
		truncate_pagecache(inode, inode->i_size);
}

int ezfs_write_begin(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len,
			struct page **pagep, void **fsdata)
{
	int ret;

	printf("ezfs_write_begin test.\n");
	ret = block_write_begin(mapping, pos, len, pagep, ezfs_get_block);
	if (unlikely(ret))
		ezfs_write_failed(mapping, pos + len);

	return ret;
}

static sector_t ezfs_bmap(struct address_space *mapping, sector_t block)
{
	printf("ezfs_bmap test");
	return generic_block_bmap(mapping, block, ezfs_get_block);
}

int truncate_blocks(struct inode *inode)
{
	// struct inode *inode = d_inode(dentry);
	struct ezfs_super_block *ezfs_sb = sb_to_ezfs_sb(inode->i_sb);
	struct ezfs_sb_buffer_heads *ezfs_bhs = (struct ezfs_sb_buffer_heads *) inode->i_sb->s_fs_info;
	struct ezfs_inode *ezfs_i = (struct ezfs_inode *) inode->i_private;
	int pre_blocks = inode->i_blocks / 8;
	int cur_blocks = (inode->i_size + EZFS_BLOCK_SIZE - 1) / EZFS_BLOCK_SIZE; /*without the indirect blocks*/
	struct buffer_head *bh = NULL;
	uint64_t *addr;
	int err = 0;
	int i;


	printf("The start of truncate blocks.\n");
	printf("The size after truncate is %lld.\n", inode->i_size);
	printf("The previous number of blocks is %d.\n", pre_blocks);
	printf("The current number of blocks is %d.\n", cur_blocks);

	if (cur_blocks >= pre_blocks) {
		if (cur_blocks > pre_blocks)
				inode->i_blocks = cur_blocks * 8;
		return 0;
	}



	mutex_lock(ezfs_sb->ezfs_lock);
	if (ezfs_i->indirect_blk_n) {
		bh = sb_bread(inode->i_sb, ezfs_i->indirect_blk_n);
		addr = (uint64_t *) bh->b_data;
	}
	/*if cur_blocks == 0*/
	if (cur_blocks == 0) {
		if (ezfs_i->indirect_blk_n) {
			if (pre_blocks <= 1) {
				printf("invalid preblocks number %d\n", pre_blocks);
				err = 1;
				goto exit;
			}
			for (i = 0; i < pre_blocks - 1; i++) {
				if (*addr > 0) {
					printf("clear bit %llu.\n", *addr);
					CLEARBIT(ezfs_sb->free_data_blocks, *addr);
					memset(addr, 0, sizeof(uint64_t));
				} else {
					printf("In indirect_block, the number is not right, index is %d.\n", i);
					err = 1;
					goto exit;
				}
				addr += 1;
			}
			CLEARBIT(ezfs_sb->free_data_blocks, ezfs_i->indirect_blk_n);
			ezfs_i->indirect_blk_n = 0;
		}
		CLEARBIT(ezfs_sb->free_data_blocks, ezfs_i->direct_blk_n);
		inode->i_blocks = cur_blocks * 8;
		ezfs_i->direct_blk_n = 0;
	} else if (cur_blocks == 1) {
		if (ezfs_i->indirect_blk_n) {
			if (pre_blocks <= 1) {
				printf("invalid preblocks number %d\n", pre_blocks);
				err = 1;
				goto exit;
			}
			for (i = 0; i < pre_blocks - 1; i++) {
				if (*addr > 0) {
					printf("clear bit %llu.\n", *addr);
					CLEARBIT(ezfs_sb->free_data_blocks, *addr);
					memset(addr, 0, sizeof(uint64_t));
				} else {
					printf("In indirect_block, the number is not right, index is %d.\n", i);
					err = 1;
					goto exit;
				}
				addr += 1;
			}
			CLEARBIT(ezfs_sb->free_data_blocks, ezfs_i->indirect_blk_n);
			ezfs_i->indirect_blk_n = 0;
			inode->i_blocks = cur_blocks * 8;
		} else {
			printf("invalid indirect_block for preblocks.\n");
			err = 1;
			goto exit;
		}
	} else if (cur_blocks > 1) {
		if (ezfs_i->indirect_blk_n) {
			if (pre_blocks <= 1) {
				printf("invalid preblocks number %d\n", pre_blocks);
				err = 1;
				goto exit;
			}
			for (i = pre_blocks - 2, addr += i; i > cur_blocks - 2; i--) {
				if (*addr > 0) {
					printf("clear bit %llu.\n", *addr);
					CLEARBIT(ezfs_sb->free_data_blocks, *addr);
					memset(addr, 0, sizeof(uint64_t));
				} else {
					printf("In indirect_block, the number is not right, index is %d.\n", i);
					err = 1;
					goto exit;
				}
				addr -= 1;
			}
			inode->i_blocks = cur_blocks * 8;
		} else {
			printf("invalid indirect_block for preblocks.\n");
			err = 1;
			goto exit;
		}
	}

	printf("successfully truncate blocks.\n");
	printf("The current number of blocks is %llu.\n", inode->i_blocks);
exit:
	mutex_unlock(ezfs_sb->ezfs_lock);
	if (bh)
		brelse(bh);
	mark_inode_dirty(inode);
	mark_buffer_dirty(ezfs_bhs->sb_bh);
	mark_buffer_dirty(ezfs_bhs->i_store_bh);
	return err;
}


int ezfs_write_end(struct file *file, struct address_space *mapping,
		loff_t pos, unsigned len, unsigned copied,
		struct page *page, void *fsdata)
{
	int err = 0;
	loff_t size;
	int blocks;

	printf("The start of write end.\n");
	err = generic_write_end(file, mapping, pos, len, copied, page, fsdata);
	// printf("The ino is %llu, and the blocks is %llu, the size is %llu.\n", mapping->host->i_ino, mapping->host->i_blocks, mapping->host->i_size);
	size = mapping->host->i_size;
	blocks = (size + EZFS_BLOCK_SIZE - 1) / EZFS_BLOCK_SIZE;

	mapping->host->i_blocks = blocks * 8;

	//printf("The ino is %llu, and the blocks is %llu, the size is %llu.\n", mapping->host->i_ino, mapping->host->i_blocks, mapping->host->i_size);
	return err;
}

int ezfs_setattr(struct user_namespace *mnt_userns, struct dentry *dentry,
		   struct iattr *iattr)
{
	int err = 0;

	printf("start of setattr.\n");
	err = simple_setattr(mnt_userns, dentry, iattr);
	if (err)
		return err;

	err = truncate_blocks(d_inode(dentry));
	if (err)
		return err;
	return 0;
}


static int ezfs_get_tree(struct fs_context *fc)
{
	return get_tree_bdev(fc, ezfs_fill_super);
}

static void ezfs_free_fc(struct fs_context *fc)
{
	return;
}

static const struct fs_context_operations ezfs_context_ops = {
	.free		= ezfs_free_fc,
	// .parse_param	= ezfs_parse_param,
	.get_tree	= ezfs_get_tree,
};

// static void init_once(void *foo)
// {
// 	struct ezfs_inode_info *bi = foo;

// 	inode_init_once(&bi->vfs_inode);
// }

// static int __init init_inodecache(void)
// {
// 	ezfs_inode_cachep = kmem_cache_create("ezfs_inode_cache",
// 					     sizeof(struct ezfs_inode_info),
// 					     0, (SLAB_RECLAIM_ACCOUNT|
// 						SLAB_MEM_SPREAD|SLAB_ACCOUNT),
// 					     init_once);
// 	if (ezfs_inode_cachep == NULL)
// 		return -ENOMEM;
// 	return 0;
// }

// static void destroy_inodecache(void)
// {
// 	/*
// 	 * Make sure all delayed rcu free inodes are flushed before we
// 	 * destroy cache.
// 	 */
// 	rcu_barrier();
// 	kmem_cache_destroy(ezfs_inode_cachep);
// }

static int ezfs_init_fs_context(struct fs_context *fc)
{
	fc->ops = &ezfs_context_ops;
	return 0;
}


// static struct dentry *ezfs_mount(struct file_system_type *fs_type,
// 	int flags, const char *dev_name, void *data)
// {
// 	return mount_bdev(fs_type, flags, dev_name, data, ezfs_fill_super);
// }

static struct file_system_type myezfs = {
	.owner		= THIS_MODULE,
	.name		= "myezfs",
	// .parameters	= ezfs_fs_parameters,
	.init_fs_context = ezfs_init_fs_context, // to be defined
	//.mount		= ezfs_mount, //
	.kill_sb	= kill_block_super, // or ezfs_kill_sb
	// .fs_flags	= FS_REQUIRES_DEV,
	// here is a flag to disble inode cache
};
MODULE_ALIAS_FS("myezfs");

static int __init init_myez_fs(void)
{
// 	int err = init_inodecache();   // The class specific function
// 	if (err)
// 		goto out1;
// 	err = register_filesystem(&myezfs);
// 	if (err)
// 		goto out;
// 	return 0;
// out:
// 	destroy_inodecache(); // The class specific function
// out1:
// 	return err;
	int err = register_filesystem(&myezfs);
	if (err)
		return err;
	return 0;
}

static void __exit exit_myez_fs(void)
{
	unregister_filesystem(&myezfs);
	//destroy_inodecache();   // The class specific function
}

module_init(init_myez_fs)
module_exit(exit_myez_fs)