#include <linux/module.h>
#define INCLUDE_VERMAGIC
#include <linux/build-salt.h>
#include <linux/elfnote-lto.h>
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
	{ 0xe28fa882, "module_layout" },
	{ 0x48106fee, "class_destroy" },
	{ 0x3d598bde, "device_destroy" },
	{ 0xa33eb387, "unregister_filesystem" },
	{ 0xbad0336a, "register_filesystem" },
	{ 0xb10dd8d4, "device_create" },
	{ 0x6091b333, "unregister_chrdev_region" },
	{ 0x75328114, "cdev_del" },
	{ 0xcee0cc87, "__class_create" },
	{ 0x37d92150, "cdev_add" },
	{ 0x65f84319, "cdev_init" },
	{ 0xe3ec2f2b, "alloc_chrdev_region" },
	{ 0xbcab6ee6, "sscanf" },
	{ 0xd0da656b, "__stack_chk_fail" },
	{ 0x349cba85, "strchr" },
	{ 0x37a0cba, "kfree" },
	{ 0x5a921311, "strncmp" },
	{ 0x41ed3709, "get_random_bytes" },
	{ 0x13c49cc2, "_copy_from_user" },
	{ 0xeb233a45, "__kmalloc" },
	{ 0x92997ed8, "_printk" },
	{ 0x6b10bee1, "_copy_to_user" },
	{ 0x88db9f48, "__check_object_size" },
	{ 0x754d539c, "strlen" },
	{ 0x5b8239ca, "__x86_return_thunk" },
	{ 0xbdfb6dbb, "__fentry__" },
};

MODULE_INFO(depends, "");

