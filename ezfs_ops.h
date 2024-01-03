#ifndef __EZFS_OPS_H__
#define __EZFS_OPS_H__

#include <linux/time.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include "ezfs.h"

int ezfs_readdir(struct file *file, struct dir_context *ctx);


/*define the functions of super operations*/


// struct inode *ezfs_alloc_inode(struct super_block *sb);

// void ezfs_free_inode(struct inode *inode);



int ezfs_write_inode(struct inode *inode, struct writeback_control *wbc);

void ezfs_evict_inode(struct inode *inode);

void ezfs_put_super(struct super_block *s);

int ezfs_statfs(struct dentry *dentry, struct kstatfs *buf);

int ezfs_create(struct user_namespace *mnt_userns, struct inode *dir,
		      struct dentry *dentry, umode_t mode, bool excl);

int ezfs_mknod(struct user_namespace *mnt_userns, struct inode *dir,
	    struct dentry *dentry, umode_t mode, dev_t dev);


struct dentry *ezfs_lookup(struct inode *dir, struct dentry *dentry,
						unsigned int flags);


int ezfs_link(struct dentry *old, struct inode *dir,
						struct dentry *new);
					

int ezfs_unlink(struct inode *dir, struct dentry *dentry);


static int ezfs_rename(struct user_namespace *mnt_userns, struct inode *old_dir,
		      struct dentry *old_dentry, struct inode *new_dir,
		      struct dentry *new_dentry, unsigned int flags);


static int ezfs_read_folio(struct file *file, struct folio *folio);


static int ezfs_writepage(struct page *page, struct writeback_control *wbc);

static void ezfs_write_failed(struct address_space *mapping, loff_t to);

static int ezfs_write_begin(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len,
			struct page **pagep, void **fsdata);

int ezfs_write_end(struct file *file, struct address_space *mapping,
		loff_t pos, unsigned len, unsigned copied,
		struct page *page, void *fsdata);


static sector_t ezfs_bmap(struct address_space *mapping, sector_t block);

int ezfs_mkdir(struct user_namespace *mnt_userns, struct inode *dir,
		       struct dentry *dentry, umode_t mode);

int ezfs_rmdir(struct inode *dir, struct dentry *dentry);

int ezfs_setattr(struct user_namespace *mnt_userns, struct dentry *dentry,
		   struct iattr *iattr);


const struct super_operations ezfs_sops = { // all needed to be initialized
	// .alloc_inode	= ezfs_alloc_inode,
	// .free_inode	= ezfs_free_inode,
	.write_inode	= ezfs_write_inode,
	.evict_inode	= ezfs_evict_inode,
	.put_super	= ezfs_put_super,
	.statfs		= ezfs_statfs,
};

const struct inode_operations ezfs_dir_inops = {
	.create			= ezfs_create, // Create a file
	.lookup			= ezfs_lookup,
	.link			= ezfs_link,
	.unlink			= ezfs_unlink,
	.rename			= ezfs_rename,
	.mkdir			= ezfs_mkdir, // for creating a directory
	.rmdir			= ezfs_rmdir, // for removing a directory
	//.mknod			= ezfs_mknod, // for creating a inode (helper function)
};

const struct file_operations ezfs_dir_operations = {
	.read		= generic_read_dir,
	.iterate_shared	= ezfs_readdir,
	.fsync		= generic_file_fsync,
	.llseek		= generic_file_llseek, // you need this function for vim open
	.open		= generic_file_open,
};

// Should this be in the .c file or .h file?
const struct file_operations ezfs_file_operations = {
	.llseek 	= generic_file_llseek,
	.read_iter	= generic_file_read_iter,
	.write_iter	= generic_file_write_iter,
	.mmap		= generic_file_mmap,
	.fsync		= generic_file_fsync, // you need this function for vim open
	.llseek		= generic_file_llseek,
	.splice_read	= generic_file_splice_read,
};

// for reading/writing to buffer head, etc, write end you need to update some inode metadata.
// You must update inode metadata. appending to a file. data block we use. change the indirect block
// You need to know all the block we have. and reserve the blocks. touch the file. block will be zero
// 
const struct address_space_operations ezfs_aops = {
	.dirty_folio	= block_dirty_folio,
	.invalidate_folio = block_invalidate_folio,
	.read_folio	= ezfs_read_folio,
	.writepage	= ezfs_writepage,
	.write_begin	= ezfs_write_begin,
	.write_end	= ezfs_write_end,
	.bmap		= ezfs_bmap,
};

const struct inode_operations ezfs_file_inops = {
	.setattr	= ezfs_setattr,
	//.getattr	= simple_getattr,
};

#endif /* ifndef __EZFS_OPS_H__ */

/*we encounter permission problem during vim wq*/
