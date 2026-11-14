// SPDX-License-Identifier: GPL-2.0-only
/*
 * Xiaomi POCO F3 (Alioth) ADSP/CDSP/SLPI Subsystem Boot & SSR Driver
 *
 * Coordinates booting of Qualcomm SM8250 remoteprocs (adsp, cdsp, slpi),
 * monitors subsystem crashes via SSR, and prevents crash-loop reboots.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/remoteproc.h>
#include <linux/remoteproc/qcom_rproc.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>

#define ALIOTH_DSP_COUNT 3

struct alioth_dsp {
	const char *name;
	const char *ssr_name;
	struct device_node *node;
	struct rproc *rproc;
	struct miscdevice misc;
	struct notifier_block nb;
	void *ssr_handle;
	struct delayed_work work;
	struct mutex lock;
	struct kobject *kobj;
	u32 boot_delay_ms;
	u32 state;
	u32 crash_count;
	unsigned long last_crash_jiffies;
	bool auto_boot;
};

static struct alioth_dsp alioth_dsps[ALIOTH_DSP_COUNT] = {
	{ .name = "adsp", .ssr_name = "adsp", .boot_delay_ms = 100, .auto_boot = true },
	{ .name = "cdsp", .ssr_name = "cdsp", .boot_delay_ms = 200, .auto_boot = true },
	{ .name = "slpi", .ssr_name = "slpi", .boot_delay_ms = 300, .auto_boot = true },
};

static unsigned int alioth_dsp_max_crashes = 5;
static unsigned int alioth_dsp_crash_window = 60;

static void alioth_dsp_work(struct work_struct *work)
{
	struct alioth_dsp *dsp = container_of(to_delayed_work(work), struct alioth_dsp, work);
	int ret;

	mutex_lock(&dsp->lock);
	if (!dsp->rproc && dsp->node) {
		dsp->rproc = rproc_get_by_phandle(dsp->node->phandle);
		if (!dsp->rproc) {
			pr_warn("alioth-dsp-boot: waiting for %s rproc...\n", dsp->name);
			schedule_delayed_work(&dsp->work, msecs_to_jiffies(1000));
			mutex_unlock(&dsp->lock);
			return;
		}
	}

	if (dsp->rproc && dsp->auto_boot && dsp->state == 0) {
		ret = rproc_boot(dsp->rproc);
		if (ret) {
			pr_warn("alioth-dsp-boot: rproc_boot failed for %s: %d, retrying...\n", dsp->name, ret);
			schedule_delayed_work(&dsp->work, msecs_to_jiffies(2000));
		} else {
			pr_info("alioth-dsp-boot: %s booted successfully\n", dsp->name);
			dsp->state = 1;
		}
	}
	mutex_unlock(&dsp->lock);
}

static int alioth_dsp_ssr_notify(struct notifier_block *nb,
				 unsigned long action, void *data)
{
	struct alioth_dsp *dsp = container_of(nb, struct alioth_dsp, nb);
	unsigned long now = jiffies;

	if (action == 2) { /* QCOM_SSR_BEFORE_SHUTDOWN */
		if (time_after(now, dsp->last_crash_jiffies + msecs_to_jiffies(alioth_dsp_crash_window * 1000)))
			dsp->crash_count = 0;

		dsp->last_crash_jiffies = now;
		dsp->crash_count++;

		pr_warn("alioth-dsp-boot: %s crashed (%u in %u s)\n",
			dsp->name, dsp->crash_count, alioth_dsp_crash_window);

		if (dsp->crash_count >= alioth_dsp_max_crashes) {
			pr_emerg("alioth-dsp-boot: %s crashed %u times within %u s, recovery disabled\n",
				 dsp->name, dsp->crash_count, alioth_dsp_crash_window);
			if (dsp->rproc)
				dsp->rproc->recovery_disabled = true;
		}
	}
	return NOTIFY_DONE;
}

static int alioth_dsp_boot_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	int i;

	if (!np)
		return -ENODEV;

	for (i = 0; i < ALIOTH_DSP_COUNT; i++) {
		struct alioth_dsp *dsp = &alioth_dsps[i];
		char prop_name[32];

		mutex_init(&dsp->lock);
		INIT_DELAYED_WORK(&dsp->work, alioth_dsp_work);

		snprintf(prop_name, sizeof(prop_name), "qcom,%s", dsp->name);
		dsp->node = of_parse_phandle(np, prop_name, 0);
		if (!dsp->node)
			continue;

		dsp->nb.notifier_call = alioth_dsp_ssr_notify;
		dsp->ssr_handle = qcom_register_ssr_notifier(dsp->ssr_name, &dsp->nb);
		if (IS_ERR_OR_NULL(dsp->ssr_handle))
			dev_warn(dev, "failed to register SSR notifier for %s\n", dsp->ssr_name);

		schedule_delayed_work(&dsp->work, msecs_to_jiffies(dsp->boot_delay_ms));
	}

	return 0;
}

static void alioth_dsp_boot_remove(struct platform_device *pdev)
{
	int i;

	for (i = 0; i < ALIOTH_DSP_COUNT; i++) {
		struct alioth_dsp *dsp = &alioth_dsps[i];

		if (dsp->ssr_handle) {
			qcom_unregister_ssr_notifier(dsp->ssr_handle, &dsp->nb);
			dsp->ssr_handle = NULL;
		}
		cancel_delayed_work_sync(&dsp->work);
		if (dsp->rproc) {
			rproc_put(dsp->rproc);
			dsp->rproc = NULL;
		}
	}
}

static const struct of_device_id alioth_dsp_boot_of_match[] = {
	{ .compatible = "xiaomi,alioth-dsp-boot" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, alioth_dsp_boot_of_match);

static struct platform_driver alioth_dsp_boot_driver = {
	.probe = alioth_dsp_boot_probe,
	.remove = alioth_dsp_boot_remove,
	.driver = {
		.name = "alioth-dsp-boot",
		.of_match_table = alioth_dsp_boot_of_match,
	},
};
module_platform_driver(alioth_dsp_boot_driver);

MODULE_DESCRIPTION("Xiaomi POCO F3 (Alioth) ADSP/CDSP/SLPI Subsystem Boot & SSR Driver");
MODULE_LICENSE("GPL");
