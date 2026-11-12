// SPDX-License-Identifier: GPL-2.0
/*
 * In-kernel ION Compatibility Driver for Android Userspace
 * Bridges legacy ION ioctl allocations to modern DMA-BUF heaps (system / cma).
 */

#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/of.h>

#define ION_IOC_MAGIC 'I'

struct ion_allocation_data {
	__u64 len;
	__u64 align;
	__u32 heap_id_mask;
	__u32 flags;
	__u32 handle;
};

#define ION_IOC_ALLOC _IOWR(ION_IOC_MAGIC, 0, struct ion_allocation_data)
#define ION_IOC_FREE _IOWR(ION_IOC_MAGIC, 1, struct ion_allocation_data)
#define ION_IOC_MAP _IOWR(ION_IOC_MAGIC, 2, struct ion_allocation_data)
#define ION_IOC_SHARE _IOWR(ION_IOC_MAGIC, 4, struct ion_allocation_data)
#define ION_IOC_IMPORT _IOWR(ION_IOC_MAGIC, 5, struct ion_allocation_data)
#define ION_IOC_SYNC _IOWR(ION_IOC_MAGIC, 7, struct ion_allocation_data)

static int ion_compat_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int ion_compat_release(struct inode *inode, struct file *file)
{
	return 0;
}

static long ion_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case ION_IOC_ALLOC: {
		struct ion_allocation_data data;
		struct dma_heap *heap;
		int fd;

		if (copy_from_user(&data, (void __user *)arg, sizeof(data)))
			return -EFAULT;

		if (!data.len)
			return -EINVAL;

		/* Prioritize system heap, fallback to CMA */
		heap = dma_heap_find("system");
		if (!heap)
			heap = dma_heap_find("linux,cma");

		if (!heap) {
			pr_err_ratelimited("ion-compat: no DMA-BUF heap available\n");
			return -ENODEV;
		}

		fd = dma_heap_buffer_alloc_fd(heap, data.len, O_RDWR | O_CLOEXEC, 0);
		if (fd < 0) {
			pr_err_ratelimited("ion-compat: dma_heap alloc failed for size %llu: %d\n", data.len, fd);
			return fd;
		}

		data.handle = fd;

		if (copy_to_user((void __user *)arg, &data, sizeof(data))) {
			close_fd(fd);
			return -EFAULT;
		}
		return 0;
	}
	case ION_IOC_FREE:
	case ION_IOC_SYNC:
		return 0;
	case ION_IOC_SHARE:
	case ION_IOC_IMPORT:
	case ION_IOC_MAP: {
		struct ion_allocation_data data;
		if (copy_from_user(&data, (void __user *)arg, sizeof(data)))
			return -EFAULT;
		/* In modern Android, handle is already an exported fd */
		return 0;
	}
	default:
		if (_IOC_TYPE(cmd) == ION_IOC_MAGIC)
			return 0;
		return -ENOTTY;
	}
}

#ifdef CONFIG_COMPAT
static long ion_compat_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	return ion_compat_ioctl(file, cmd, arg);
}
#endif

static const struct file_operations ion_compat_fops = {
	.owner = THIS_MODULE,
	.open = ion_compat_open,
	.release = ion_compat_release,
	.unlocked_ioctl = ion_compat_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = ion_compat_compat_ioctl,
#endif
};

static struct miscdevice ion_misc_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "ion",
	.fops = &ion_compat_fops,
	.mode = 0666,
};

static int ion_compat_probe(struct platform_device *pdev)
{
	int ret = misc_register(&ion_misc_device);
	if (ret && ret != -EBUSY) {
		pr_err("ion-compat: failed to register /dev/ion: %d\n", ret);
		return ret;
	}
	pr_info("ion-compat: /dev/ion registered successfully for Android compatibility\n");
	return 0;
}

static void ion_compat_remove(struct platform_device *pdev)
{
	misc_deregister(&ion_misc_device);
}

static const struct of_device_id ion_compat_of_match[] = {
	{ .compatible = "android,ion-compat" },
	{ },
};
MODULE_DEVICE_TABLE(of, ion_compat_of_match);

static struct platform_driver ion_compat_driver = {
	.probe = ion_compat_probe,
	.remove = ion_compat_remove,
	.driver = {
		.name = "ion-compat",
		.of_match_table = ion_compat_of_match,
	},
};

static int __init ion_compat_init(void)
{
	/* Register early so /dev/ion exists even if DT probe is delayed */
	misc_register(&ion_misc_device);
	return platform_driver_register(&ion_compat_driver);
}
device_initcall(ion_compat_init);

MODULE_AUTHOR("Alioth Mainline Team");
MODULE_DESCRIPTION("Android ION to DMA-BUF Heap Compatibility Layer");
MODULE_LICENSE("GPL");
