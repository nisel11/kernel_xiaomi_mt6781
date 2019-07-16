// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#define pr_fmt(fmt) "[eas_ctrl]"fmt

#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include <linux/uaccess.h>

#include "boost_ctrl.h"
#include "eas_ctrl_plat.h"
#include <mt-plat/eas_ctrl.h>
#include "mtk_perfmgr_internal.h"
//#include <mt-plat/mtk_sched.h>
#include <linux/sched.h>

#ifdef CONFIG_TRACING
#include <linux/kallsyms.h>
#include <linux/trace_events.h>
#endif

/* boost value */
static struct mutex boost_eas;

/* uclamp */
#if defined(CONFIG_UCLAMP_TASK_GROUP) && defined(CONFIG_SCHED_TUNE)
static int cur_uclamp_min[NR_CGROUP];
static unsigned long uclamp_policy_mask[NR_CGROUP];
#endif
static int uclamp_min[NR_CGROUP][EAS_MAX_KIR];
static int debug_uclamp_min[NR_CGROUP];

/* log */
static int log_enable;


#define MAX_UCLAMP_VALUE		(100)
#define MIN_UCLAMP_VALUE		(0)
#define MIN_DEBUG_UCLAMP_VALUE	(-1)

/************************/

/************************/
static int check_uclamp_value(int value)
{
	return clamp(value, MIN_UCLAMP_VALUE, MAX_UCLAMP_VALUE);
}

static int check_debug_uclamp_value(int value)
{
	return clamp(value, MIN_DEBUG_UCLAMP_VALUE, MAX_UCLAMP_VALUE);
}


#if defined(CONFIG_UCLAMP_TASK_GROUP) && defined(CONFIG_SCHED_TUNE)
int update_eas_uclamp_min(int kicker, int cgroup_idx, int value)
{
	int final_uclamp = 0;
	int i, len = 0, len1 = 0;

	char msg[LOG_BUF_SIZE];
	char msg1[LOG_BUF_SIZE];

	mutex_lock(&boost_eas);

	if (cgroup_idx >= NR_CGROUP) {
		mutex_unlock(&boost_eas);
		pr_debug(" cgroup_idx >= NR_CGROUP, error\n");
		perfmgr_trace_printk("uclamp_min", "cgroup_idx >= NR_CGROUP\n");
		return -1;
	}

	uclamp_min[cgroup_idx][kicker] = value;
	len += snprintf(msg + len, sizeof(msg) - len, "[%d] [%d] [%d]",
			kicker, cgroup_idx, value);

	/* ptr return error EIO:I/O error */
	if (len < 0) {
		perfmgr_trace_printk("uclamp_min", "return -EIO 1\n");
		mutex_unlock(&boost_eas);
		return -EIO;
	}

	for (i = 0; i < EAS_UCLAMP_MAX_KIR; i++) {
		if (uclamp_min[cgroup_idx][i] == 0) {
			clear_bit(i, &uclamp_policy_mask[cgroup_idx]);
			continue;
		}

		final_uclamp = MAX(final_uclamp,
			uclamp_min[cgroup_idx][i]);

		set_bit(i, &uclamp_policy_mask[cgroup_idx]);
	}

	cur_uclamp_min[cgroup_idx] = check_uclamp_value(final_uclamp);

	len += snprintf(msg + len, sizeof(msg) - len, "{%d} ", final_uclamp);

	/*ptr return error EIO:I/O error */
	if (len < 0) {
		perfmgr_trace_printk("uclamp_min", "return -EIO 2\n");
		mutex_unlock(&boost_eas);
		return -EIO;
	}

	len1 += snprintf(msg1 + len1, sizeof(msg1) - len1, "[0x %lx] ",
			uclamp_policy_mask[cgroup_idx]);

	if (len1 < 0) {
		perfmgr_trace_printk("uclamp_min", "return -EIO 3\n");
		mutex_unlock(&boost_eas);
		return -EIO;
	}
	if (debug_uclamp_min[cgroup_idx] == -1)
		uclamp_min_pct_for_perf_idx(cgroup_idx,
				cur_uclamp_min[cgroup_idx]);

	strncat(msg, msg1, LOG_BUF_SIZE);
	if (log_enable)
		pr_debug("%s\n", msg);

#ifdef CONFIG_TRACING
	perfmgr_trace_printk("eas_ctrl (uclamp)", msg);
#endif
	mutex_unlock(&boost_eas);

	return cur_uclamp_min[cgroup_idx];
}
#else
int update_eas_uclamp_min(int kicker, int cgroup_idx, int value)
{
	return -1;
}
#endif
EXPORT_SYMBOL(update_eas_uclamp_min);


/************************************************/
static ssize_t perfmgr_boot_boost_proc_write(
		struct file *filp, const char *ubuf,
		size_t cnt, loff_t *pos)
{
	int cgroup = 0, data = 0;

	int rv = check_boot_boost_proc_write(&cgroup, &data, ubuf, cnt);

	if (rv != 0)
		return rv;

	data = check_uclamp_value(data);

	if (cgroup >= 0 && cgroup < NR_CGROUP)
		update_eas_uclamp_min(EAS_KIR_BOOT, cgroup, data);

	return cnt;
}

static int perfmgr_boot_boost_proc_show(struct seq_file *m, void *v)
{
	int i;

	for (i = 0; i < NR_CGROUP; i++)
		seq_printf(m, "%d\n", uclamp_min[i][EAS_KIR_BOOT]);

	return 0;
}

/****************/
/* uclamp min   */
/****************/
static ssize_t perfmgr_perfserv_uclamp_min_proc_write(struct file *filp
		, const char *ubuf, size_t cnt, loff_t *pos)
{
	int data = 0;

	int rv = check_proc_write(&data, ubuf, cnt);

	if (rv != 0)
		return rv;

	data = check_uclamp_value(data);

	update_eas_uclamp_min(EAS_UCLAMP_KIR_PERF, CGROUP_ROOT, data);

	return cnt;
}

static int perfmgr_perfserv_uclamp_min_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", uclamp_min[CGROUP_ROOT][EAS_UCLAMP_KIR_PERF]);

	return 0;
}

/************************************************/
static int perfmgr_current_uclamp_min_proc_show(struct seq_file *m, void *v)
{
#if defined(CONFIG_UCLAMP_TASK_GROUP) && defined(CONFIG_SCHED_TUNE)
	seq_printf(m, "%d\n", cur_uclamp_min[CGROUP_ROOT]);
#else
	seq_printf(m, "%d\n", -1);
#endif

	return 0;
}

/************************************************************/
static ssize_t perfmgr_debug_uclamp_min_proc_write(
		struct file *filp, const char *ubuf,
		size_t cnt, loff_t *pos)
{
	int data = 0;

	int rv = check_proc_write(&data, ubuf, cnt);

	if (rv != 0)
		return rv;

	debug_uclamp_min[CGROUP_ROOT] = check_debug_uclamp_value(data);

#if defined(CONFIG_UCLAMP_TASK_GROUP) && defined(CONFIG_SCHED_TUNE)
	if (debug_uclamp_min[CGROUP_ROOT] >= 0)
		uclamp_min_pct_for_perf_idx(CGROUP_ROOT,
			debug_uclamp_min[CGROUP_ROOT]);
	else
		uclamp_min_pct_for_perf_idx(CGROUP_ROOT,
			cur_uclamp_min[CGROUP_ROOT]);
#endif
	return cnt;
}

static int perfmgr_debug_uclamp_min_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", debug_uclamp_min[CGROUP_ROOT]);

	return 0;
}

static ssize_t perfmgr_perfserv_fg_uclamp_min_proc_write(struct file *filp
		, const char *ubuf, size_t cnt, loff_t *pos)
{
	int data = 0;

	int rv = check_proc_write(&data, ubuf, cnt);

	if (rv != 0)
		return rv;

	data = check_uclamp_value(data);

	update_eas_uclamp_min(EAS_UCLAMP_KIR_PERF, CGROUP_FG, data);

	return cnt;
}

static int perfmgr_perfserv_fg_uclamp_min_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", uclamp_min[CGROUP_FG][EAS_UCLAMP_KIR_PERF]);

	return 0;
}

/************************************************/
static int perfmgr_current_fg_uclamp_min_proc_show(struct seq_file *m, void *v)
{
#if defined(CONFIG_UCLAMP_TASK_GROUP) && defined(CONFIG_SCHED_TUNE)
	seq_printf(m, "%d\n", cur_uclamp_min[CGROUP_FG]);
#else
	seq_printf(m, "%d\n", -1);
#endif

	return 0;
}

/************************************************************/
static ssize_t perfmgr_debug_fg_uclamp_min_proc_write(
		struct file *filp, const char *ubuf,
		size_t cnt, loff_t *pos)
{
	int data = 0;

	int rv = check_proc_write(&data, ubuf, cnt);

	if (rv != 0)
		return rv;

	debug_uclamp_min[CGROUP_FG] = check_debug_uclamp_value(data);

#if defined(CONFIG_UCLAMP_TASK_GROUP) && defined(CONFIG_SCHED_TUNE)
	if (debug_uclamp_min[CGROUP_FG] >= 0)
		uclamp_min_pct_for_perf_idx(CGROUP_FG,
			debug_uclamp_min[CGROUP_FG]);
	else
		uclamp_min_pct_for_perf_idx(CGROUP_FG,
			cur_uclamp_min[CGROUP_FG]);
#endif
	return cnt;
}

static int perfmgr_debug_fg_uclamp_min_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", debug_uclamp_min[CGROUP_FG]);

	return 0;
}

static ssize_t perfmgr_perfserv_bg_uclamp_min_proc_write(struct file *filp
		, const char *ubuf, size_t cnt, loff_t *pos)
{
	int data = 0;

	int rv = check_proc_write(&data, ubuf, cnt);

	if (rv != 0)
		return rv;

	data = check_uclamp_value(data);

	update_eas_uclamp_min(EAS_UCLAMP_KIR_PERF, CGROUP_BG, data);

	return cnt;
}

static int perfmgr_perfserv_bg_uclamp_min_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", uclamp_min[CGROUP_BG][EAS_UCLAMP_KIR_PERF]);

	return 0;
}

/************************************************/
static int perfmgr_current_bg_uclamp_min_proc_show(struct seq_file *m, void *v)
{
#if defined(CONFIG_UCLAMP_TASK_GROUP) && defined(CONFIG_SCHED_TUNE)
	seq_printf(m, "%d\n", cur_uclamp_min[CGROUP_BG]);
#else
	seq_printf(m, "%d\n", -1);
#endif

	return 0;
}

/************************************************************/
static ssize_t perfmgr_debug_bg_uclamp_min_proc_write(
		struct file *filp, const char *ubuf,
		size_t cnt, loff_t *pos)
{
	int data = 0;

	int rv = check_proc_write(&data, ubuf, cnt);

	if (rv != 0)
		return rv;

	debug_uclamp_min[CGROUP_BG] = check_debug_uclamp_value(data);

#if defined(CONFIG_UCLAMP_TASK_GROUP) && defined(CONFIG_SCHED_TUNE)
	if (debug_uclamp_min[CGROUP_BG] >= 0)
		uclamp_min_pct_for_perf_idx(CGROUP_BG,
			debug_uclamp_min[CGROUP_BG]);
	else
		uclamp_min_pct_for_perf_idx(CGROUP_BG,
			cur_uclamp_min[CGROUP_BG]);
#endif
	return cnt;
}

static int perfmgr_debug_bg_uclamp_min_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", debug_uclamp_min[CGROUP_BG]);

	return 0;
}

static ssize_t perfmgr_perfserv_ta_uclamp_min_proc_write(struct file *filp
		, const char *ubuf, size_t cnt, loff_t *pos)
{
	int data = 0;

	int rv = check_proc_write(&data, ubuf, cnt);

	if (rv != 0)
		return rv;

	data = check_uclamp_value(data);

	update_eas_uclamp_min(EAS_UCLAMP_KIR_PERF, CGROUP_TA, data);

	return cnt;
}

static int perfmgr_perfserv_ta_uclamp_min_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", uclamp_min[CGROUP_TA][EAS_UCLAMP_KIR_PERF]);

	return 0;
}

/************************************************/
static int perfmgr_current_ta_uclamp_min_proc_show(struct seq_file *m, void *v)
{
#if defined(CONFIG_UCLAMP_TASK_GROUP) && defined(CONFIG_SCHED_TUNE)
	seq_printf(m, "%d\n", cur_uclamp_min[CGROUP_TA]);
#else
	seq_printf(m, "%d\n", -1);
#endif

	return 0;
}

/************************************************************/
static ssize_t perfmgr_debug_ta_uclamp_min_proc_write(
		struct file *filp, const char *ubuf,
		size_t cnt, loff_t *pos)
{
	int data = 0;

	int rv = check_proc_write(&data, ubuf, cnt);

	if (rv != 0)
		return rv;

	debug_uclamp_min[CGROUP_TA] = check_debug_uclamp_value(data);

#if defined(CONFIG_UCLAMP_TASK_GROUP) && defined(CONFIG_SCHED_TUNE)
	if (debug_uclamp_min[CGROUP_TA] >= 0)
		uclamp_min_pct_for_perf_idx(CGROUP_TA,
			debug_uclamp_min[CGROUP_TA]);
	else
		uclamp_min_pct_for_perf_idx(CGROUP_TA,
			cur_uclamp_min[CGROUP_TA]);
#endif
	return cnt;
}

static int perfmgr_debug_ta_uclamp_min_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", debug_uclamp_min[CGROUP_TA]);

	return 0;
}

static ssize_t perfmgr_perfmgr_log_proc_write(
		struct file *filp, const char __user *ubuf,
		size_t cnt, loff_t *pos)
{
	int data = 0;

	int rv = check_proc_write(&data, ubuf, cnt);

	if (rv != 0)
		return rv;

	log_enable = data > 0 ? 1 : 0;

	return cnt;
}

static int perfmgr_perfmgr_log_proc_show(struct seq_file *m, void *v)
{
	if (m)
		seq_printf(m, "%d\n", log_enable);
	return 0;
}

/* uclamp */
PROC_FOPS_RW(boot_boost);
PROC_FOPS_RW(perfserv_uclamp_min);
PROC_FOPS_RW(debug_uclamp_min);
PROC_FOPS_RO(current_uclamp_min);
PROC_FOPS_RW(perfserv_fg_uclamp_min);
PROC_FOPS_RW(debug_fg_uclamp_min);
PROC_FOPS_RO(current_fg_uclamp_min);
PROC_FOPS_RW(perfserv_bg_uclamp_min);
PROC_FOPS_RW(debug_bg_uclamp_min);
PROC_FOPS_RO(current_bg_uclamp_min);
PROC_FOPS_RW(perfserv_ta_uclamp_min);
PROC_FOPS_RW(debug_ta_uclamp_min);
PROC_FOPS_RO(current_ta_uclamp_min);

/* others */
PROC_FOPS_RW(perfmgr_log);

/*******************************************/
int uclamp_ctrl_init(struct proc_dir_entry *parent)
{
	int i, ret = 0;
#if defined(CONFIG_UCLAMP_TASK_GROUP) && defined(CONFIG_SCHED_TUNE)
	int j;
#endif
	struct pentry {
		const char *name;
		const struct file_operations *fops;
	};

	const struct pentry entries[] = {
		/* uclamp */
		PROC_ENTRY(boot_boost),
		PROC_ENTRY(perfserv_uclamp_min),
		PROC_ENTRY(debug_uclamp_min),
		PROC_ENTRY(current_uclamp_min),
		PROC_ENTRY(perfserv_fg_uclamp_min),
		PROC_ENTRY(debug_fg_uclamp_min),
		PROC_ENTRY(current_fg_uclamp_min),
		PROC_ENTRY(perfserv_bg_uclamp_min),
		PROC_ENTRY(debug_bg_uclamp_min),
		PROC_ENTRY(current_bg_uclamp_min),
		PROC_ENTRY(perfserv_ta_uclamp_min),
		PROC_ENTRY(debug_ta_uclamp_min),
		PROC_ENTRY(current_ta_uclamp_min),

		/* log */
		PROC_ENTRY(perfmgr_log),
	};
	mutex_init(&boost_eas);

	/* create procfs */
	for (i = 0; i < ARRAY_SIZE(entries); i++) {
		if (!proc_create(entries[i].name, 0644,
					parent, entries[i].fops)) {
			pr_debug("%s(), create /eas_ctrl%s failed\n",
					__func__, entries[i].name);
			ret = -EINVAL;
			goto out;
		}
	}

#if defined(CONFIG_UCLAMP_TASK_GROUP) && defined(CONFIG_SCHED_TUNE)
	/* uclamp */
	for (i = 0; i < NR_CGROUP; i++) {
		cur_uclamp_min[i] = 0;
		debug_uclamp_min[i] = -1;
		for (j = 0; j < EAS_UCLAMP_MAX_KIR; j++)
			uclamp_min[i][j] = 0;
	}
#endif

out:
	return ret;
}
