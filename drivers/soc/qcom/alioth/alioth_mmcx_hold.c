// SPDX-License-Identifier: GPL-2.0-only
/*
 * Xiaomi POCO F3 (Alioth) MMCX & CX Power Rail Voltage Lock Driver
 *
 * Holds the MMCX and CX genpd performance states at a high voltage level
 * during early boot, then lowers to a stable floor level in late boot.
 * Prevents sleep-death and display power collapse on SM8250.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pm_runtime.h>
#include <linux/pm_domain.h>
#include <linux/clk.h>

#define ALIOTH_MMCX_MAX_ENTRIES 4

struct alioth_mmcx_entry {
	struct device *dev;
	u32 floor_level;
};

static struct alioth_mmcx_entry alioth_mmcx_entries[ALIOTH_MMCX_MAX_ENTRIES];
static int alioth_mmcx_count;

static int alioth_mmcx_hold_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	u32 boot_level = 0x180;
	u32 floor_level = 0x100;
	struct clk *clk;
	int ret;

	if (alioth_mmcx_count >= ALIOTH_MMCX_MAX_ENTRIES)
		return dev_err_probe(dev, -EINVAL, "too many MMCX hold entries\n");

	if (!dev->of_node)
		return dev_err_probe(dev, -ENODEV, "missing device tree node\n");

	of_property_read_u32(dev->of_node, "xiaomi,boot-level", &boot_level);
	of_property_read_u32(dev->of_node, "xiaomi,floor-level", &floor_level);

	clk = devm_clk_get_optional_enabled(dev, NULL);
	if (IS_ERR(clk))
		return dev_err_probe(dev, PTR_ERR(clk), "failed to get/enable optional clock\n");

	ret = devm_pm_runtime_enable(dev);
	if (ret)
		return ret;

	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0)
		return dev_err_probe(dev, ret, "failed to resume runtime PM\n");

	ret = dev_pm_genpd_set_performance_state(dev, boot_level);
	if (ret) {
		pm_runtime_put_sync(dev);
		return dev_err_probe(dev, ret, "failed to vote MMCX level %u\n", boot_level);
	}

	alioth_mmcx_entries[alioth_mmcx_count].dev = dev;
	alioth_mmcx_entries[alioth_mmcx_count].floor_level = floor_level;
	alioth_mmcx_count++;

	dev_info(dev, "holding the rail at level %u until late boot, then %u\n", boot_level, floor_level);
	return 0;
}

static int __init alioth_mmcx_hold_late(void)
{
	int i;

	for (i = 0; i < alioth_mmcx_count; i++) {
		struct device *dev = alioth_mmcx_entries[i].dev;
		u32 floor = alioth_mmcx_entries[i].floor_level;
		int ret = dev_pm_genpd_set_performance_state(dev, floor);

		if (ret == 0)
			dev_info(dev, "dropped MMCX hold rail to floor level %u\n", floor);
		else
			dev_warn(dev, "failed to drop MMCX hold rail to floor level\n");
	}
	return 0;
}
late_initcall_sync(alioth_mmcx_hold_late);

static const struct of_device_id alioth_mmcx_hold_match[] = {
	{ .compatible = "xiaomi,alioth-mmcx-hold" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, alioth_mmcx_hold_match);

static struct platform_driver alioth_mmcx_hold_driver = {
	.probe = alioth_mmcx_hold_probe,
	.driver = {
		.name = "alioth-mmcx-hold",
		.of_match_table = alioth_mmcx_hold_match,
	},
};

static int __init alioth_mmcx_hold_init(void)
{
	return platform_driver_register(&alioth_mmcx_hold_driver);
}
subsys_initcall(alioth_mmcx_hold_init);

MODULE_DESCRIPTION("Xiaomi POCO F3 (Alioth) MMCX & CX Power Rail Voltage Lock Driver");
MODULE_LICENSE("GPL");
