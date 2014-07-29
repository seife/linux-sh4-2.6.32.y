/*
 * Copyright (C) 2012 STMicroelectronics Limited
 *
 * Author: Francesco Virlinzi <francesco.virlinzist.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/ahci_platform.h>
#include <linux/stm/clk.h>
#include <linux/stm/platform.h>
#include <linux/stm/device.h>

#define AHCI_OOBR	0xbc

struct ahci_stm_drv_data {
	struct stm_miphy *miphy_dev;
	struct clk *clk;
	struct stm_device_state *device_state;
	struct platform_device *pdev;
	struct platform_device *ahci;
};

static int stm_ahci_init(struct device *dev, void __iomem *mmio)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ahci_stm_drv_data *drv_data = dev_get_drvdata(dev->parent);

	writel(0x80000000, mmio + AHCI_OOBR);
	writel(0x8204080C, mmio + AHCI_OOBR);
	writel(0x0204080C, mmio + AHCI_OOBR);

	if (drv_data->miphy_dev)
		stm_miphy_start(drv_data->miphy_dev);
	else
		drv_data->miphy_dev = stm_miphy_claim(pdev->id, SATA_MODE, dev);

	return 0;
};

static struct ahci_platform_data default_ahci_platform_data = {
	.init = stm_ahci_init,
};

static struct platform_device
__init *prepare_ahci(struct platform_device *pdev)
{
	struct stm_plat_ahci_data *pdata = dev_get_platdata(&pdev->dev);
	struct platform_device *ahci;
	struct ahci_platform_data *ahci_pdata;

	ahci = platform_device_alloc("ahci", pdev->id);

	platform_device_add_resources(ahci, pdev->resource, 2);

	ahci_pdata = (pdata->ahci_pdata ? : &default_ahci_platform_data);
	platform_device_add_data(ahci, ahci_pdata, sizeof(*ahci_pdata));

	ahci->dev.parent = &pdev->dev;
	ahci->dev.dma_mask = pdev->dev.dma_mask;
	ahci->dev.coherent_dma_mask = pdev->dev.coherent_dma_mask;

	return ahci;
}

static int __devinit ahci_stm_driver_probe(struct platform_device *pdev)
{
	struct stm_plat_ahci_data *pdata = dev_get_platdata(&pdev->dev);
	struct ahci_stm_drv_data *drv_data;
	struct device *dev = &pdev->dev;
	int ret = 0;

	drv_data = kzalloc(sizeof(*drv_data), GFP_KERNEL);
	if (!drv_data)
		return -ENOMEM;

	drv_data->pdev = pdev;
	drv_data->clk = clk_get(dev, "ahci_clk");
	if (IS_ERR(drv_data->clk)) {
		ret = -EINVAL;
		goto err_0;
	}

	clk_enable(drv_data->clk);

	drv_data->device_state =
		devm_stm_device_init(&pdev->dev, pdata->device_config);
	if (!drv_data->device_state) {
		ret = -EBUSY;
		goto err_1;
	}

	platform_set_drvdata(pdev, drv_data);

	drv_data->ahci = prepare_ahci(pdev);
	if (!drv_data->ahci) {
		ret = -ENODEV;
		goto err_1;
	}

	ret = platform_device_add(drv_data->ahci);
	if (ret)
		goto err_1;

	return 0;
err_1:
	clk_disable(drv_data->clk);
	clk_put(drv_data->clk);
err_0:
	kfree(drv_data);
	pr_err("Error: %s: %d\n", __func__, ret);
	return ret;
}

#ifdef CONFIG_PM
static int ahci_stm_suspend(struct device *dev)
{
	struct ahci_stm_drv_data *data = dev_get_drvdata(dev);

	clk_disable(data->clk);

	return 0;
}

static int ahci_stm_resume(struct device *dev)
{
	struct ahci_stm_drv_data *data = dev_get_drvdata(dev);

	clk_enable(data->clk);

	return 0;
}

static int ahci_stm_freeze(struct device *dev)
{
	struct ahci_stm_drv_data *data = dev_get_drvdata(dev);

	stm_miphy_freeze(data->miphy_dev);
	return 0;
}

static int ahci_stm_restore(struct device *dev)
{
	struct ahci_stm_drv_data *data = dev_get_drvdata(dev);

	stm_miphy_thaw(data->miphy_dev);

	return 0;
}
#endif

static struct dev_pm_ops ahci_stm_pm_ops = {
	.suspend = ahci_stm_suspend,
	.resume = ahci_stm_resume,
	.freeze = ahci_stm_freeze,
	.thaw = ahci_stm_restore,
	.restore = ahci_stm_restore,
};

static int ahci_stm_remove(struct platform_device *pdev)
{
	struct ahci_stm_drv_data *data = dev_get_drvdata(&pdev->dev);

	clk_disable(data->clk);
	clk_put(data->clk);

	return 0;
}

static struct platform_driver ahci_stm_driver = {
	.driver.name = "ahci_stm",
	.driver.owner = THIS_MODULE,
	.driver.pm = &ahci_stm_pm_ops,
	.probe = ahci_stm_driver_probe,
	.remove = ahci_stm_remove,
};

static int __init ahci_stm_init(void)
{
	return platform_driver_register(&ahci_stm_driver);
}
module_init(ahci_stm_init);
