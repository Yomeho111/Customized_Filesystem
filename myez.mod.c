#include <linux/module.h>
#define INCLUDE_VERMAGIC
#include <linux/build-salt.h>
#include <linux/elfnote-lto.h>
#include <linux/export-internal.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

BUILD_SALT;
BUILD_LTO_INFO;

MODULE_INFO(vermagic, VERMAGIC_STRING);
MODULE_INFO(name, KBUILD_MODNAME);

__visible struct module __this_module
__section(".gnu.linkonce.this_module") = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};

#ifdef CONFIG_RETPOLINE
MODULE_INFO(retpoline, "Y");
#endif


static const struct modversion_info ____versions[]
__used __section("__versions") = {
	{ 0xbdfb6dbb, "__fentry__" },
	{ 0x5b8239ca, "__x86_return_thunk" },
	{ 0x92997ed8, "_printk" },
	{ 0x57859d3c, "generic_block_bmap" },
	{ 0x6d0ada49, "__bread_gfp" },
	{ 0x707a3489, "mark_buffer_dirty" },
	{ 0x23d449d4, "__brelse" },
	{ 0x9a9f0ec1, "block_read_full_folio" },
	{ 0x59b30b9b, "block_write_full_page" },
	{ 0xaf6c1502, "drop_nlink" },
	{ 0x91de7ee9, "__mark_inode_dirty" },
	{ 0x4dfa8d4b, "mutex_lock" },
	{ 0x65c35ceb, "inc_nlink" },
	{ 0xd1bea17f, "current_time" },
	{ 0xb85df81b, "ihold" },
	{ 0xbc9b9c21, "d_instantiate" },
	{ 0x3213f038, "mutex_unlock" },
	{ 0x37a0cba, "kfree" },
	{ 0x246c2e26, "generic_write_end" },
	{ 0x9871c7b1, "register_filesystem" },
	{ 0x69bbf387, "get_tree_bdev" },
	{ 0xe7f520c6, "unregister_filesystem" },
	{ 0xdb5ad9c3, "block_write_begin" },
	{ 0x5106d8c3, "truncate_pagecache" },
	{ 0xdf3b4923, "truncate_inode_pages_final" },
	{ 0x9fed2866, "invalidate_inode_buffers" },
	{ 0xf211b44b, "clear_inode" },
	{ 0xaf9a58f7, "from_kuid" },
	{ 0x7b6f3fa3, "from_kgid" },
	{ 0x48a557f7, "sync_dirty_buffer" },
	{ 0x65487097, "__x86_indirect_thunk_rax" },
	{ 0xba8fbd64, "_raw_spin_lock" },
	{ 0xb5b54b34, "_raw_spin_unlock" },
	{ 0x4c236f6f, "__x86_indirect_thunk_r15" },
	{ 0x8f786bee, "fs_umode_to_dtype" },
	{ 0x31549b2a, "__x86_indirect_thunk_r10" },
	{ 0xa916b694, "strnlen" },
	{ 0xcbd4898c, "fortify_panic" },
	{ 0x90e53e50, "iget_locked" },
	{ 0xb9725eac, "make_kuid" },
	{ 0xc95f48d5, "make_kgid" },
	{ 0x5fbe5226, "set_nlink" },
	{ 0xebc4032e, "unlock_new_inode" },
	{ 0x65304f38, "iget_failed" },
	{ 0xd8e51716, "kmalloc_caches" },
	{ 0xbaadbc97, "kmalloc_trace" },
	{ 0x417c158d, "sb_set_blocksize" },
	{ 0xcefb0c9f, "__mutex_init" },
	{ 0x36dc0f6, "d_make_root" },
	{ 0xb3f2614d, "new_inode" },
	{ 0xf3f7d717, "iput" },
	{ 0xe3e666c8, "init_user_ns" },
	{ 0x28569c5a, "inode_init_owner" },
	{ 0x620524f9, "page_symlink_inode_operations" },
	{ 0xe3cd90eb, "inode_nohighmem" },
	{ 0x92e27c99, "init_special_inode" },
	{ 0xeb37fbb6, "__insert_inode_hash" },
	{ 0x449ad0a7, "memcmp" },
	{ 0xd0da656b, "__stack_chk_fail" },
	{ 0x8c1a0d0e, "d_splice_alias" },
	{ 0xfc8e5492, "mark_buffer_dirty_inode" },
	{ 0x282810d2, "simple_empty" },
	{ 0xe71e81a1, "simple_setattr" },
	{ 0x8873bbb0, "kill_block_super" },
	{ 0xc8a9b96a, "block_dirty_folio" },
	{ 0x700f9e88, "block_invalidate_folio" },
	{ 0x64359f36, "generic_file_llseek" },
	{ 0x1b6f193d, "generic_file_read_iter" },
	{ 0xe07aabbb, "generic_file_write_iter" },
	{ 0x642ee2, "generic_file_mmap" },
	{ 0x62486cb2, "generic_file_fsync" },
	{ 0x2d26570f, "generic_file_splice_read" },
	{ 0x3e80e20, "generic_read_dir" },
	{ 0xb465335e, "generic_file_open" },
	{ 0x94a9c4ab, "module_layout" },
};

MODULE_INFO(depends, "");

