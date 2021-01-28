// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2016 MediaTek Inc.
 */

#include <linux/kconfig.h>
#include <linux/module.h>
#include <linux/string.h>
#include "mrdump_private.h"

#include <mrdump.h>
#if IS_ENABLED(CONFIG_MTK_DFD_INTERNAL_DUMP)
#include <mtk_platform_debug.h>
#endif

#if IS_ENABLED(CONFIG_MTK_WATCHDOG)
#include <mtk_wd_api.h>

static void mrdump_wd_dram_reserved_mode(bool enabled)
{
	int res;
	struct wd_api *wd_api;

	res = get_wd_api(&wd_api);
	if (res < 0) {
		pr_notice("%s: get wd api error (%d)\n", __func__, res);
	} else {
		if (mrdump_ddr_reserve_ready) {
			res = wd_api->wd_dram_reserved_mode(enabled);
			if (!res) {
				if (enabled) {
					pr_notice("%s: DDR reserved mode enabled\n",
							__func__);
#if IS_ENABLED(CONFIG_MTK_DFD_INTERNAL_DUMP)
					res = dfd_setup();
					if (res == -1)
						pr_notice("%s: DFD disabled\n",
								__func__);
					else
						pr_notice("%s: DFD enabled\n",
								__func__);
#else
			pr_notice("%s: config is not enabled yet\n",
					__func__);
#endif
				} else {
					pr_notice("%s: DDR reserved mode disabled(%d)\n",
							__func__, res);
				}
			}
		}
	}
}

int __init mrdump_hw_init(void)
{
	mrdump_wd_dram_reserved_mode(true);
	pr_info("%s: init_done.\n", __func__);
	return 0;
}

#else

int __init mrdump_hw_init(void)
{

#if IS_ENABLED(CONFIG_MTK_DFD_INTERNAL_DUMP)
	int res;
#endif

#ifdef MODULE
	mrdump_module_param_ddrsv();
#endif
#if IS_ENABLED(CONFIG_MTK_DFD_INTERNAL_DUMP)
	res = dfd_setup();
	if (res == -1)
		pr_notice("%s: DFD disabled\n", __func__);
	else
		pr_notice("%s: DFD enabled\n", __func__);
#endif
	return 0;
}

#endif /* CONFIG_MTK_WATCHDOG */

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("MediaTek AEE module");
MODULE_AUTHOR("MediaTek Inc.");
