// SPDX-License-Identifier: GPL-2.0

#include <linux/capability.h>
#include <linux/compat.h>
#include <linux/dcache.h>
#include <linux/fcntl.h>
#include <linux/fs.h>
#include <linux/ioctl.h>
#include <linux/mman.h>
#include <linux/memfd.h>
#include <linux/shmem_fs.h>
#include <linux/string.h>
#include <linux/uaccess.h>

#define ASHMEM_NAME_LEN		256
#define __ASHMEMIOC		0x77

struct ashmem_pin {
	__u32 offset;
	__u32 len;
};

#define ASHMEM_SET_NAME		_IOW(__ASHMEMIOC, 1, char[ASHMEM_NAME_LEN])
#define ASHMEM_GET_NAME		_IOR(__ASHMEMIOC, 2, char[ASHMEM_NAME_LEN])
#define ASHMEM_SET_SIZE		_IOW(__ASHMEMIOC, 3, size_t)
#define ASHMEM_GET_SIZE		_IO(__ASHMEMIOC, 4)
#define ASHMEM_SET_PROT_MASK	_IOW(__ASHMEMIOC, 5, unsigned long)
#define ASHMEM_GET_PROT_MASK	_IO(__ASHMEMIOC, 6)
#define ASHMEM_PIN		_IOW(__ASHMEMIOC, 7, struct ashmem_pin)
#define ASHMEM_UNPIN		_IOW(__ASHMEMIOC, 8, struct ashmem_pin)
#define ASHMEM_GET_PIN_STATUS	_IO(__ASHMEMIOC, 9)
#define ASHMEM_PURGE_ALL_CACHES	_IO(__ASHMEMIOC, 10)
#define ASHMEM_GET_FILE_ID	_IOR(__ASHMEMIOC, 11, unsigned long)

#define COMPAT_ASHMEM_SET_SIZE		_IOW(__ASHMEMIOC, 3, compat_size_t)
#define COMPAT_ASHMEM_SET_PROT_MASK	_IOW(__ASHMEMIOC, 5, unsigned int)
#define COMPAT_ASHMEM_GET_FILE_ID	_IOR(__ASHMEMIOC, 11, compat_uptr_t)

#define ASHMEM_NOT_PURGED	0
#define ASHMEM_IS_PINNED	1

#define WRITE_SEALS_MASK	(F_SEAL_WRITE | F_SEAL_FUTURE_WRITE)

static long memfd_ashmem_get_name(struct file *file, void __user *ubuf)
{
	struct name_snapshot snap;
	char name[ASHMEM_NAME_LEN] = {};
	const char *s;
	size_t len;
	long ret = 0;

	take_dentry_name_snapshot(&snap, file->f_path.dentry);
	s = snap.name.name;
	if (!strncmp(s, "memfd:", 6))
		s += 6;
	len = min(strlen(s), (size_t)ASHMEM_NAME_LEN - 1);
	memcpy(name, s, len);
	release_dentry_name_snapshot(&snap);

	if (copy_to_user(ubuf, name, len + 1))
		ret = -EFAULT;
	return ret;
}

static long memfd_ashmem_set_prot(struct file *file, unsigned long prot)
{
	int seals = memfd_get_seals(file);
	bool writable;

	if (seals < 0)
		return seals;

	prot |= PROT_READ | PROT_EXEC;
	writable = !(seals & WRITE_SEALS_MASK);

	if (!writable && (prot & PROT_WRITE))
		return -EINVAL;
	if (writable && !(prot & PROT_WRITE))
		return memfd_fcntl(file, F_ADD_SEALS, F_SEAL_FUTURE_WRITE);
	return 0;
}

static long memfd_ashmem_get_prot(struct file *file)
{
	int seals = memfd_get_seals(file);
	long prot = PROT_READ | PROT_EXEC;

	if (seals < 0)
		return seals;
	if (!(seals & WRITE_SEALS_MASK))
		prot |= PROT_WRITE;
	return prot;
}

long memfd_ashmem_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	unsigned long ino = file_inode(file)->i_ino;

	switch (cmd) {
	case ASHMEM_GET_NAME:
		return memfd_ashmem_get_name(file, (void __user *)arg);
	case ASHMEM_GET_SIZE:
		return i_size_read(file_inode(file));
	case ASHMEM_SET_PROT_MASK:
		return memfd_ashmem_set_prot(file, arg);
	case ASHMEM_GET_PROT_MASK:
		return memfd_ashmem_get_prot(file);
	case ASHMEM_GET_FILE_ID:
		if (copy_to_user((void __user *)arg, &ino, sizeof(ino)))
			return -EFAULT;
		return 0;
	case ASHMEM_PIN:
		return ASHMEM_NOT_PURGED;
	case ASHMEM_UNPIN:
		return 0;
	case ASHMEM_GET_PIN_STATUS:
		return ASHMEM_IS_PINNED;
	case ASHMEM_PURGE_ALL_CACHES:
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		return 0;
	case ASHMEM_SET_NAME:
	case ASHMEM_SET_SIZE:
		return -EINVAL;
	default:
		return -ENOTTY;
	}
}

#ifdef CONFIG_COMPAT
long memfd_ashmem_compat_ioctl(struct file *file, unsigned int cmd,
			       unsigned long arg)
{
	u32 ino;

	switch (cmd) {
	case COMPAT_ASHMEM_SET_SIZE:
		cmd = ASHMEM_SET_SIZE;
		break;
	case COMPAT_ASHMEM_SET_PROT_MASK:
		cmd = ASHMEM_SET_PROT_MASK;
		break;
	case COMPAT_ASHMEM_GET_FILE_ID:
		ino = file_inode(file)->i_ino;
		if (put_user(ino, (u32 __user *)compat_ptr(arg)))
			return -EFAULT;
		return 0;
	case ASHMEM_GET_NAME:
		arg = (unsigned long)compat_ptr(arg);
		break;
	}
	return memfd_ashmem_ioctl(file, cmd, arg);
}
#endif
