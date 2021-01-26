#include <linux/proc_fs.h>
#include <linux/errno.h> /* EFAULT */
#include <linux/fs.h>
#include <linux/printk.h> /* pr_info */
#include <linux/seq_file.h> /* seq_read, seq_lseek, single_release */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/limits.h>
#include <linux/slab.h>
#include <uapi/asm-generic/fcntl.h>

#include "khook/engine.c"
#ifndef DEBUG
  #include "rr.h"
#endif


static struct proc_dir_entry* rick_file;

static int show(struct seq_file* file, void* v) {
#ifdef DEBUG
    seq_printf(file, "ab\ncd\n");
#else
    seq_write(file, rr_mp3, rr_mp3_len);
#endif
    return 0;
}
static int open(struct inode* i, struct file* f) {
    (void)i;
    return single_open(f, show, NULL);
}

static const struct file_operations fops = {
    .llseek = seq_lseek,
    .open = open,
    .owner = THIS_MODULE,
    .read = seq_read,
    .release = single_release,
};

static int ends_with(const char* str, const char* suffix) {
    size_t lenstr, lensuffix;
    if (!str || !suffix)
        return 0;
    lenstr = strlen(str);
    lensuffix = strlen(suffix);
    if(lensuffix >  lenstr)
        return 0;
    return strncmp(str + lenstr - lensuffix, suffix, lensuffix) == 0;
}

#define PATH     "/proc/rick"
#define PATH_LEN 11
typedef struct {
    int valid;
    char user_data[PATH_LEN];
} rick_status;

static void rick_prepare(rick_status* s, unsigned long* reg_path, unsigned long* reg_flags) {
    char* kernel_path = NULL;
    void* __user user_path = (void* __user)(*reg_path);
    int res;
    {
        int flags = (int)(*reg_flags);
        const int disabled_flags = O_WRONLY | O_RDWR | O_CREAT | O_TRUNC | O_APPEND
            | O_DIRECT | O_DIRECTORY | O_TMPFILE;
        if(flags & disabled_flags) goto fail;
    }
    kernel_path = kmalloc(sizeof(char) * PATH_MAX, GFP_KERNEL);
    if(kernel_path == NULL) {
        res = -1;
        goto fail;
    }
    res = strncpy_from_user(kernel_path, user_path, PATH_MAX);
    if(res < 0) {
        goto fail;
    }
    if(ends_with(kernel_path, ".mp3")) {
        printk("opened %s, replacing with rick file\n", kernel_path);
        memcpy(&s->user_data, kernel_path, PATH_LEN);
        res = copy_to_user(user_path, PATH, PATH_LEN);
        if(res != 0) {
            printk("copy_to_user fail: %d\n", res);
            goto fail;
        }
        s->valid = 1;
    }
fail:
    kfree(kernel_path);
}

static void rick_undo(rick_status* s, unsigned long* reg) {
    if(s->valid) {
        int i = copy_to_user((void* __user)(*reg), s->user_data, PATH_LEN);
        if(i != 0) printk("rick_undo copy failed: %d", i);
    }
}

//filename:
//open   -> di
//openat -> si
//flags:
//open   -> si
//openat -> dx
KHOOK_EXT(long, __x64_sys_open, struct pt_regs *);
static long khook___x64_sys_open(struct pt_regs *regs) {
    long res;
    rick_status s = {};
    rick_prepare(&s, &regs->di, &regs->si);
    res = KHOOK_ORIGIN(__x64_sys_open, regs);
    rick_undo(&s, &regs->di);
    return res;
}
KHOOK_EXT(long, __x64_sys_openat, struct pt_regs*);
static long khook___x64_sys_openat(struct pt_regs *regs) {
    long res;
    rick_status s = {};
    rick_prepare(&s, &regs->si, &regs->dx);
    res = KHOOK_ORIGIN(__x64_sys_openat, regs);
    rick_undo(&s, &regs->si);
    return res;
}


int init_module(void)
{
    rick_file = proc_create("rick", 0, NULL, &fops);
    if(!rick_file) return -EINVAL;
	return khook_init();
}

void cleanup_module(void)
{
    proc_remove(rick_file);
	khook_cleanup();
}

MODULE_LICENSE("GPL");
