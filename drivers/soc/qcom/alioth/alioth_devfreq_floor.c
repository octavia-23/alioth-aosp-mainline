// SPDX-License-Identifier: GPL-2.0-only
/*
 * Xiaomi POCO F3 (Alioth) Devfreq Performance Floor Driver
 *
 * Implements devfreq OPP and scaling constraints to maintain
 * minimum hardware performance floors for SM8250 GPU and buses.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pm_opp.h>
#include <linux/devfreq.h>
#include <linux/interconnect.h>
#include <linux/slab.h>

struct alioth_devfreq_floor_data {
	struct icc_path *icc_path;
	struct devfreq *df;
	struct devfreq_dev_profile profile;
	u32 *freq_table;
	u32 freq_count;
	u32 flags;
};

static int floor_target(struct device *dev, unsigned long *freq, u32 flags)
{
	struct alioth_devfreq_floor_data *data = dev_get_drvdata(dev);
	struct dev_pm_opp *opp;

	opp = devfreq_recommended_opp(dev, freq, flags);
	if (IS_ERR(opp))
		return PTR_ERR(opp);
	dev_pm_opp_put(opp);

	if (data && data->icc_path)
		icc_set_bw(data->icc_path, 0, *freq);

	return 0;
}

static void floor_remove_opps(void *data)
{
	struct device *dev = data;
	dev_pm_opp_remove_all_dynamic(dev);
}

static void floor_remove_devfreq(void *data)
{
	struct alioth_devfreq_floor_data *d = data;
	if (d && d->df)
		devfreq_remove_device(d->df);
}

static int alioth_devfreq_floor_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct alioth_devfreq_floor_data *data;
	const char *gov_name = "performance";
	const char *rename_to = NULL;
	int ret, i, count;

	if (!np)
		return -ENODEV;

	count = of_property_count_u32_elems(np, "xiaomi,freq-table");
	if (count < 2)
		return dev_err_probe(dev, -EINVAL, "invalid or missing xiaomi,freq-table\n");

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->freq_table = devm_kcalloc(dev, count, sizeof(u32), GFP_KERNEL);
	if (!data->freq_table)
		return -ENOMEM;

	data->freq_count = count;
	ret = of_property_read_u32_array(np, "xiaomi,freq-table", data->freq_table, count);
	if (ret)
		return ret;

	for (i = 1; i < count; i++) {
		if (data->freq_table[i] <= data->freq_table[i - 1])
			return dev_err_probe(dev, -EINVAL, "freq-table must be strictly ascending\n");
	}

	data->icc_path = devm_of_icc_get(dev, NULL);
	if (IS_ERR(data->icc_path))
		return dev_err_probe(dev, PTR_ERR(data->icc_path), "failed to get interconnect path\n");

	platform_set_drvdata(pdev, data);

	for (i = 0; i < count; i++) {
		ret = dev_pm_opp_add(dev, (unsigned long)data->freq_table[i], 0);
		if (ret) {
			dev_pm_opp_remove_all_dynamic(dev);
			return dev_err_probe(dev, ret, "failed to add OPP for %u Hz\n", data->freq_table[i]);
		}
	}

	ret = devm_add_action_or_reset(dev, floor_remove_opps, dev);
	if (ret)
		return ret;

	data->profile.initial_freq = data->freq_table[0];
	data->profile.polling_ms = 50;
	data->profile.target = floor_target;

	data->df = devfreq_add_device(dev, &data->profile, gov_name, NULL);
	if (IS_ERR(data->df))
		return dev_err_probe(dev, PTR_ERR(data->df), "failed to register devfreq device\n");

	ret = devm_add_action_or_reset(dev, floor_remove_devfreq, data);
	if (ret)
		return ret;

	if (!of_property_read_string(np, "xiaomi,devfreq-name", &rename_to)) {
		ret = device_rename(&data->df->dev, rename_to);
		if (ret)
			dev_warn(dev, "failed to rename devfreq device to %s\n", rename_to);
	}

	dev_info(dev, "devfreq floor registered (%u to %u Hz)\n",
		 data->freq_table[0], data->freq_table[count - 1]);
	return 0;
}

static const struct of_device_id alioth_devfreq_floor_match[] = {
	{ .compatible = "xiaomi,alioth-devfreq-floor" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, alioth_devfreq_floor_match);

static struct platform_driver alioth_devfreq_floor_driver = {
	.probe = alioth_devfreq_floor_probe,
	.driver = {
		.name = "alioth-devfreq-floor",
		.of_match_table = alioth_devfreq_floor_match,
	},
};
module_platform_driver(alioth_devfreq_floor_driver);

MODULE_DESCRIPTION("Xiaomi POCO F3 (Alioth) Devfreq Performance Floor Driver");
MODULE_LICENSE("GPL");
