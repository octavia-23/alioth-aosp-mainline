// SPDX-License-Identifier: GPL-2.0-only
/*
 * Xiaomi Alioth CPU Runqueue Statistics Driver
 *
 * Exposes /sys/devices/system/cpu/cpu0/rq-stats for load tracking
 * and dynamic governor tuning.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/cpu.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/timer.h>
#include <linux/jiffies.h>

static struct kobject *alioth_rq_kobj;
static struct timer_list alioth_rq_timer;
static unsigned int alioth_def_timer_ms = 100;

static ssize_t def_timer_ms_show(struct kobject *kobj,
				  struct kobj_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%u\n", alioth_def_timer_ms);
}

static ssize_t def_timer_ms_store(struct kobject *kobj,
				   struct kobj_attribute *attr,
				   const char *buf, size_t count)
{
	unsigned int val;
	int ret;

	ret = kstrtouint(buf, 0, &val);
	if (ret)
		return ret;

	alioth_def_timer_ms = val;
	if (val)
		mod_timer(&alioth_rq_timer, jiffies + msecs_to_jiffies(val));
	else
		timer_delete(&alioth_rq_timer);

	return count;
}
static struct kobj_attribute def_timer_ms_attr = __ATTR_RW(def_timer_ms);

static struct attribute *alioth_rq_attrs[] = {
	&def_timer_ms_attr.attr,
	NULL,
};

static const struct attribute_group alioth_rq_group = {
	.attrs = alioth_rq_attrs,
};

static void alioth_rq_timer_fn(struct timer_list *t)
{
	if (alioth_def_timer_ms)
		mod_timer(&alioth_rq_timer,
			  jiffies + msecs_to_jiffies(alioth_def_timer_ms));
}

static int __init alioth_rq_stats_init(void)
{
	struct device *cpu_dev = get_cpu_device(0);
	int ret;

	if (!cpu_dev)
		return -ENODEV;

	timer_setup(&alioth_rq_timer, alioth_rq_timer_fn, 0);

	alioth_rq_kobj = kobject_create_and_add("rq-stats", &cpu_dev->kobj);
	if (!alioth_rq_kobj)
		return -ENOMEM;

	ret = sysfs_create_group(alioth_rq_kobj, &alioth_rq_group);
	if (ret) {
		kobject_put(alioth_rq_kobj);
		return ret;
	}

	if (alioth_def_timer_ms)
		mod_timer(&alioth_rq_timer,
			  jiffies + msecs_to_jiffies(alioth_def_timer_ms));

	return 0;
}
late_initcall(alioth_rq_stats_init);

MODULE_DESCRIPTION("Xiaomi Alioth CPU Runqueue Statistics Driver");
MODULE_LICENSE("GPL");
