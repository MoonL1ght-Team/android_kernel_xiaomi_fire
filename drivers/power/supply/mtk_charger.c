// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

/*
 *
 * Filename:
 * ---------
 *    mtk_charger.c
 *
 * Project:
 * --------
 *   Android_Software
 *
 * Description:
 * ------------
 *   This Module defines functions of Battery charging
 *
 * Author:
 * -------
 * Wy Chuang
 *
 */
#include <linux/init.h>		/* For init/exit macros */
#include "adapter_class.h"
#include <linux/module.h>	/* For MODULE_ marcros  */
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/poll.h>
#include <linux/power_supply.h>
#include <linux/pm_wakeup.h>
#include <linux/rtc.h>
#include <linux/time.h>
#include <linux/mutex.h>
#include <linux/kthread.h>
#include <linux/proc_fs.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>
#include <linux/scatterlist.h>
#include <linux/suspend.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/reboot.h>

#include <asm/setup.h>

#include "mtk_charger.h"
#include "mtk_battery.h"

/* TN Begin modified by hao.jia/809321 20240729 CR/EKLAMU-202 */
#if IS_ENABLED(CONFIG_OEM_TINNO_CHARGER)
#include "../../../oem/tinno_charger/tinno_charger.h"
#endif /* CONFIG_OEM_TINNO_CHARGER */
/* TN End modified by hao.jia/809321 20240729 CR/EKLAMU-202 */

/* TN Begin modified by hao.jia/809321 20240717 CR/EKLAMU-202 */
#if IS_ENABLED(CONFIG_OEM_DEVINFO)
#include "../../../oem/devinfo/dev_info.h"
#endif /* CONFIG_OEM_DEVINFO */
/* TN End modified by hao.jia/809321 20240717 CR/EKLAMU-202 */

/*TN Begin modified by hao.jia/809321 20240628 CR/EKLAMU-202 */
#if IS_ENABLED(CONFIG_OEM_TURBO_CHARGER)
bool qc_logic_probe_done = 0;
EXPORT_SYMBOL(qc_logic_probe_done);

bool mtk_can_charging = true;
EXPORT_SYMBOL(mtk_can_charging);

int qc3p_charger_ready = 0;
EXPORT_SYMBOL(qc3p_charger_ready);

int g_thermal_charging_current_limit = -1;
EXPORT_SYMBOL(g_thermal_charging_current_limit);

bool is_qc3_charger_ready = false;
EXPORT_SYMBOL(is_qc3_charger_ready);

extern bool turbo_charger_active;
extern int ffc_reduce_count;
extern bool is_turbo_charger_ready;
extern bool ffc_batt_full;
static unsigned int turbo_power_mode = 0;
static unsigned int turbo_test_mode = 0;
#endif /* CONFIG_OEM_TURBO_CHARGER */
/*TN End modified by hao.jia/809321 20240628 CR/EKLAMU-202 */

#if IS_ENABLED(CONFIG_OEM_TINNO_CHARGER)
#define HVDCP_IGNORE_TIME_DIFF_NS 800000000
#endif

struct tag_bootmode {
	u32 size;
	u32 tag;
	u32 bootmode;
	u32 boottype;
};

/* TN Begin modified by xinjun.lu/860715 20240909 CR/EKLAMU-202 */
#if IS_ENABLED(CONFIG_PE50_FFC_SUPPORT)
static struct mtk_charger *pe50_info;
#define CHG_SHOW_MAX_SIZE 50
#define MIN_TEMP_C -20
#define MAX_TEMP_C 60
#define MIN_MAX_TEMP_C 47
#define HYSTERISIS_DEGC 2

static char *stepchg_str[] = {
	[STEP_MAX_PE50]		= "MAX",
	[STEP_NORM_PE50]		= "NORMAL",
	[STEP_FULL_PE50]		= "FULL",
	[STEP_FLOAT_PE50]		= "FLOAT",
	[STEP_DEMO_PE50]		= "DEMO",
	[STEP_STOP_PE50]		= "STOP",
	[STEP_NONE_PE50]		= "NONE",
};
#endif
/* TN End modified by xinjun.lu/860715 20240909 CR/EKLAMU-202 */

/* TN Begin modified by xinjun.lu/860715 20240808 CR/EKLAMU-202 */
#if IS_ENABLED(CONFIG_OEM_HVDCP_ALGO)
#define HVDCP_TARGE_VOLT  6600 //mV
#define HVDCP_MAX_VOLT    (HVDCP_TARGE_VOLT + 200) //mV
static bool first_insert = true;
#endif
/* TN End modified by xinjun.lu/860715 20240808 CR/EKLAMU-202 */
/* TN Begin modified by jirui.li/860702 20240904 CR/EKLAMU-1339 */
#if IS_ENABLED(CONFIG_OEM_TINNO_CHARGER) || IS_ENABLED(CONFIG_OEM_TURBO_CHARGER)
#define BATTERY_PROTECT_MAX_SOC		80
#define BATTERY_PROTECT_MIN_SOC		20
#define BATTERY_CHARGING_FULL_SOC	100
#define BATTERY_CV_GAP			40
#define SW_JEITA_TEMP_10		10
#define SW_JEITA_CV1		4250000
#define SW_JEITA_CV2		4500000
#define SW_JEITA_CV1_CURRENT_LIMIT		1550
#define SW_JEITA_CV1_CURRENT_LIMIT_GAP		50
static bool sw_jeita_enter_1A = false;
static bool sw_jeita_enter_cv2 = false;
#define DEMO_MODE_LIMIT_SOC_DEFAULT	70
#define IGNORE_CURRENT_CHECK_TIME_MAX 5
#endif /* CONFIG_OEM_TINNO_CHARGER */
/* TN End modified by jirui.li/860702 20240904 CR/EKLAMU-1339 */
#ifdef MODULE
static char __chg_cmdline[COMMAND_LINE_SIZE];
static char *chg_cmdline = __chg_cmdline;

const char *chg_get_cmd(void)
{
	struct device_node *of_chosen = NULL;
	char *bootargs = NULL;

	if (__chg_cmdline[0] != 0)
		return chg_cmdline;

	of_chosen = of_find_node_by_path("/chosen");
	if (of_chosen) {
		bootargs = (char *)of_get_property(
					of_chosen, "bootargs", NULL);
		if (!bootargs)
			chr_err("%s: failed to get bootargs\n", __func__);
		else {
			strcpy(__chg_cmdline, bootargs);
			chr_err("%s: bootargs: %s\n", __func__, bootargs);
		}
	} else
		chr_err("%s: failed to get /chosen\n", __func__);

	return chg_cmdline;
}

#else
const char *chg_get_cmd(void)
{
	return saved_command_line;
}
#endif

int chr_get_debug_level(void)
{
	struct power_supply *psy;
	static struct mtk_charger *info;
	int ret;

	if (info == NULL) {
		psy = power_supply_get_by_name("mtk-master-charger");
		if (psy == NULL)
			ret = CHRLOG_DEBUG_LEVEL;
		else {
			info =
			(struct mtk_charger *)power_supply_get_drvdata(psy);
			if (info == NULL)
				ret = CHRLOG_DEBUG_LEVEL;
			else
				ret = info->log_level;
		}
	} else
		ret = info->log_level;

	return ret;
}
EXPORT_SYMBOL(chr_get_debug_level);

void _wake_up_charger(struct mtk_charger *info)
{
	unsigned long flags;

	if (info == NULL)
		return;
	spin_lock_irqsave(&info->slock, flags);
	if (!info->charger_wakelock->active)
		__pm_stay_awake(info->charger_wakelock);
	spin_unlock_irqrestore(&info->slock, flags);
	info->charger_thread_timeout = true;
	wake_up_interruptible(&info->wait_que);
}

bool is_disable_charger(struct mtk_charger *info)
{
	if (info == NULL)
		return true;

	if (info->disable_charger == true || IS_ENABLED(CONFIG_POWER_EXT))
		return true;
	else
		return false;
}

int _mtk_enable_charging(struct mtk_charger *info,
	bool en)
{
	chr_debug("%s en:%d\n", __func__, en);
	if (info->algo.enable_charging != NULL)
		return info->algo.enable_charging(info, en);
	return false;
}

int mtk_charger_notifier(struct mtk_charger *info, int event)
{
	return srcu_notifier_call_chain(&info->evt_nh, event, NULL);
}

static void mtk_charger_parse_dt(struct mtk_charger *info,
				struct device *dev)
{
	struct device_node *np = dev->of_node;
	u32 val = 0;
	struct device_node *boot_node = NULL;
	struct tag_bootmode *tag = NULL;
/* TN Begin modified by hao.jia/809321 20240628 CR/EKLAMU-202 */
#if IS_ENABLED(CONFIG_OEM_TURBO_CHARGER)
	int byte_len, i, rc;
#endif
/* TN End modified by hao.jia/809321 20240628 CR/EKLAMU-202 */

	boot_node = of_parse_phandle(dev->of_node, "bootmode", 0);
	if (!boot_node)
		chr_err("%s: failed to get boot mode phandle\n", __func__);
	else {
		tag = (struct tag_bootmode *)of_get_property(boot_node,
							"atag,boot", NULL);
		if (!tag)
			chr_err("%s: failed to get atag,boot\n", __func__);
		else {
			chr_err("%s: size:0x%x tag:0x%x bootmode:0x%x boottype:0x%x\n",
				__func__, tag->size, tag->tag,
				tag->bootmode, tag->boottype);
			info->bootmode = tag->bootmode;
			info->boottype = tag->boottype;
		}
	}

	if (of_property_read_string(np, "algorithm-name",
		&info->algorithm_name) < 0) {
		if (of_property_read_string(np, "algorithm_name",
			&info->algorithm_name) < 0) {
			chr_err("%s: no algorithm_name, use Basic\n", __func__);
			info->algorithm_name = "Basic";
		}
	}

	if (strcmp(info->algorithm_name, "Basic") == 0) {
		chr_err("found Basic\n");
		mtk_basic_charger_init(info);
	} else if (strcmp(info->algorithm_name, "Pulse") == 0) {
		chr_err("found Pulse\n");
		mtk_pulse_charger_init(info);
	}

	info->disable_charger = of_property_read_bool(np, "disable_charger")
		|| of_property_read_bool(np, "disable-charger");
	info->charger_unlimited = of_property_read_bool(np, "charger_unlimited")
		|| of_property_read_bool(np, "charger-unlimited");
	info->atm_enabled = of_property_read_bool(np, "atm_is_enabled")
		|| of_property_read_bool(np, "atm-is-enabled");
	info->enable_sw_safety_timer =
			of_property_read_bool(np, "enable_sw_safety_timer")
			|| of_property_read_bool(np, "enable-sw-safety-timer");
	info->sw_safety_timer_setting = info->enable_sw_safety_timer;
	info->disable_aicl = of_property_read_bool(np, "disable_aicl")
		|| of_property_read_bool(np, "disable-aicl");
	info->alg_new_arbitration = of_property_read_bool(np, "alg_new_arbitration")
		|| of_property_read_bool(np, "alg-new-arbitration");
	info->alg_unchangeable = of_property_read_bool(np, "alg_unchangeable")
		|| of_property_read_bool(np, "alg-unchangeable");

	/* common */

	if (of_property_read_u32(np, "charger_configuration", &val) >= 0)
		info->config = val;
	else if (of_property_read_u32(np, "charger-configuration", &val) >= 0)
		info->config = val;
	else {
		chr_err("use default charger_configuration:%d\n",
			SINGLE_CHARGER);
		info->config = SINGLE_CHARGER;
	}
/* TN Begin modified by xinjun.lu/860715 20240725 CR/EKLAMU-202 */
#if IS_ENABLED(CONFIG_OEM_TINNO_CHARGER) && IS_ENABLED(CONFIG_OEM_DEVINFO)
	if (oem_pcba_charge_power() == CHARGE_POWER_33W)
		info->config = DIVIDER_CHARGER;
#endif
/* TN End modified by xinjun.lu/860715 20240725 CR/EKLAMU-202 */

	if (of_property_read_u32(np, "battery_cv", &val) >= 0)
		info->data.battery_cv = val;
	else if (of_property_read_u32(np, "battery-cv", &val) >= 0)
		info->data.battery_cv = val;
	else {
		chr_err("use default BATTERY_CV:%d\n", BATTERY_CV);
		info->data.battery_cv = BATTERY_CV;
	}


	info->enable_boot_volt =
		of_property_read_bool(np, "enable_boot_volt")
		|| of_property_read_bool(np, "enable-boot-volt");

	if (of_property_read_u32(np, "max_charger_voltage", &val) >= 0)
		info->data.max_charger_voltage = val;
	else if (of_property_read_u32(np, "max-charger-voltage", &val) >= 0)
		info->data.max_charger_voltage = val;
	else {
		chr_err("use default V_CHARGER_MAX:%d\n", V_CHARGER_MAX);
		info->data.max_charger_voltage = V_CHARGER_MAX;
	}
	info->data.max_charger_voltage_setting = info->data.max_charger_voltage;

	if (of_property_read_u32(np, "vbus_sw_ovp_voltage", &val) >= 0)
		info->data.vbus_sw_ovp_voltage = val;
	else if (of_property_read_u32(np, "vbus-sw-ovp-voltage", &val) >= 0)
		info->data.vbus_sw_ovp_voltage = val;
	else {
		chr_err("use default V_CHARGER_MAX:%d\n", V_CHARGER_MAX);
		info->data.vbus_sw_ovp_voltage = VBUS_OVP_VOLTAGE;
	}

	if (of_property_read_u32(np, "min_charger_voltage", &val) >= 0)
		info->data.min_charger_voltage = val;
	else if (of_property_read_u32(np, "min-charger-voltage", &val) >= 0)
		info->data.min_charger_voltage = val;
	else {
		chr_err("use default V_CHARGER_MIN:%d\n", V_CHARGER_MIN);
		info->data.min_charger_voltage = V_CHARGER_MIN;
	}

	if (of_property_read_u32(np, "enable_vbat_mon", &val) >= 0) {
		info->enable_vbat_mon = val;
		info->enable_vbat_mon_bak = val;
	} else if (of_property_read_u32(np, "enable-vbat-mon", &val) >= 0) {
		info->enable_vbat_mon = val;
		info->enable_vbat_mon_bak = val;
	} else {
		chr_err("use default enable 6pin\n");
		info->enable_vbat_mon = 0;
		info->enable_vbat_mon_bak = 0;
	}
	chr_err("enable_vbat_mon:%d\n", info->enable_vbat_mon);

	/* sw jeita */
	info->enable_sw_jeita = of_property_read_bool(np, "enable_sw_jeita")
		|| of_property_read_bool(np, "enable-sw-jeita");

	if (of_property_read_u32(np, "jeita_temp_above_t4_cv", &val) >= 0)
		info->data.jeita_temp_above_t4_cv = val;
	else if (of_property_read_u32(np, "jeita-temp-above-t4-cv", &val) >= 0)
		info->data.jeita_temp_above_t4_cv = val;
	else {
		chr_err("use default JEITA_TEMP_ABOVE_T4_CV:%d\n",
			JEITA_TEMP_ABOVE_T4_CV);
		info->data.jeita_temp_above_t4_cv = JEITA_TEMP_ABOVE_T4_CV;
	}

	if (of_property_read_u32(np, "jeita_temp_t3_to_t4_cv", &val) >= 0)
		info->data.jeita_temp_t3_to_t4_cv = val;
	else if (of_property_read_u32(np, "jeita-temp-t3-to-t4-cv", &val) >= 0)
		info->data.jeita_temp_t3_to_t4_cv = val;
	else {
		chr_err("use default JEITA_TEMP_T3_TO_T4_CV:%d\n",
			JEITA_TEMP_T3_TO_T4_CV);
		info->data.jeita_temp_t3_to_t4_cv = JEITA_TEMP_T3_TO_T4_CV;
	}

	if (of_property_read_u32(np, "jeita_temp_t2_to_t3_cv", &val) >= 0)
		info->data.jeita_temp_t2_to_t3_cv = val;
	else if (of_property_read_u32(np, "jeita-temp-t2-to-t3-cv", &val) >= 0)
		info->data.jeita_temp_t2_to_t3_cv = val;
	else {
		chr_err("use default JEITA_TEMP_T2_TO_T3_CV:%d\n",
			JEITA_TEMP_T2_TO_T3_CV);
		info->data.jeita_temp_t2_to_t3_cv = JEITA_TEMP_T2_TO_T3_CV;
	}

	if (of_property_read_u32(np, "jeita_temp_t1_to_t2_cv", &val) >= 0)
		info->data.jeita_temp_t1_to_t2_cv = val;
	else if (of_property_read_u32(np, "jeita-temp-t1-to-t2-cv", &val) >= 0)
		info->data.jeita_temp_t1_to_t2_cv = val;
	else {
		chr_err("use default JEITA_TEMP_T1_TO_T2_CV:%d\n",
			JEITA_TEMP_T1_TO_T2_CV);
		info->data.jeita_temp_t1_to_t2_cv = JEITA_TEMP_T1_TO_T2_CV;
	}

	if (of_property_read_u32(np, "jeita_temp_t0_to_t1_cv", &val) >= 0)
		info->data.jeita_temp_t0_to_t1_cv = val;
	else if (of_property_read_u32(np, "jeita-temp-t0-to-t1-cv", &val) >= 0)
		info->data.jeita_temp_t0_to_t1_cv = val;
	else {
		chr_err("use default JEITA_TEMP_T0_TO_T1_CV:%d\n",
			JEITA_TEMP_T0_TO_T1_CV);
		info->data.jeita_temp_t0_to_t1_cv = JEITA_TEMP_T0_TO_T1_CV;
	}

	if (of_property_read_u32(np, "jeita_temp_below_t0_cv", &val) >= 0)
		info->data.jeita_temp_below_t0_cv = val;
	if (of_property_read_u32(np, "jeita-temp-below-t0-cv", &val) >= 0)
		info->data.jeita_temp_below_t0_cv = val;
	else {
		chr_err("use default JEITA_TEMP_BELOW_T0_CV:%d\n",
			JEITA_TEMP_BELOW_T0_CV);
		info->data.jeita_temp_below_t0_cv = JEITA_TEMP_BELOW_T0_CV;
	}

	if (of_property_read_u32(np, "temp_t4_thres", &val) >= 0)
		info->data.temp_t4_thres = val;
	else if (of_property_read_u32(np, "temp-t4-thres", &val) >= 0)
		info->data.temp_t4_thres = val;
	else {
		chr_err("use default TEMP_T4_THRES:%d\n",
			TEMP_T4_THRES);
		info->data.temp_t4_thres = TEMP_T4_THRES;
	}

	if (of_property_read_u32(np, "temp_t4_thres_minus_x_degree", &val) >= 0)
		info->data.temp_t4_thres_minus_x_degree = val;
	else if (of_property_read_u32(np, "temp-t4-thres-minus-x-degree", &val) >= 0)
		info->data.temp_t4_thres_minus_x_degree = val;
	else {
		chr_err("use default TEMP_T4_THRES_MINUS_X_DEGREE:%d\n",
			TEMP_T4_THRES_MINUS_X_DEGREE);
		info->data.temp_t4_thres_minus_x_degree =
					TEMP_T4_THRES_MINUS_X_DEGREE;
	}

	if (of_property_read_u32(np, "temp_t3_thres", &val) >= 0)
		info->data.temp_t3_thres = val;
	else if (of_property_read_u32(np, "temp-t3-thres", &val) >= 0)
		info->data.temp_t3_thres = val;
	else {
		chr_err("use default TEMP_T3_THRES:%d\n",
			TEMP_T3_THRES);
		info->data.temp_t3_thres = TEMP_T3_THRES;
	}

	if (of_property_read_u32(np, "temp_t3_thres_minus_x_degree", &val) >= 0)
		info->data.temp_t3_thres_minus_x_degree = val;
	else if (of_property_read_u32(np, "temp-t3-thres-minus-x-degree", &val) >= 0)
		info->data.temp_t3_thres_minus_x_degree = val;
	else {
		chr_err("use default TEMP_T3_THRES_MINUS_X_DEGREE:%d\n",
			TEMP_T3_THRES_MINUS_X_DEGREE);
		info->data.temp_t3_thres_minus_x_degree =
					TEMP_T3_THRES_MINUS_X_DEGREE;
	}

	if (of_property_read_u32(np, "temp_t2_thres", &val) >= 0)
		info->data.temp_t2_thres = val;
	else if (of_property_read_u32(np, "temp-t2-thres", &val) >= 0)
		info->data.temp_t2_thres = val;
	else {
		chr_err("use default TEMP_T2_THRES:%d\n",
			TEMP_T2_THRES);
		info->data.temp_t2_thres = TEMP_T2_THRES;
	}

	if (of_property_read_u32(np, "temp_t2_thres_plus_x_degree", &val) >= 0)
		info->data.temp_t2_thres_plus_x_degree = val;
	else if (of_property_read_u32(np, "temp-t2-thres-plus-x-degree", &val) >= 0)
		info->data.temp_t2_thres_plus_x_degree = val;
	else {
		chr_err("use default TEMP_T2_THRES_PLUS_X_DEGREE:%d\n",
			TEMP_T2_THRES_PLUS_X_DEGREE);
		info->data.temp_t2_thres_plus_x_degree =
					TEMP_T2_THRES_PLUS_X_DEGREE;
	}

	if (of_property_read_u32(np, "temp_t1_thres", &val) >= 0)
		info->data.temp_t1_thres = val;
	else if (of_property_read_u32(np, "temp-t1-thres", &val) >= 0)
		info->data.temp_t1_thres = val;
	else {
		chr_err("use default TEMP_T1_THRES:%d\n",
			TEMP_T1_THRES);
		info->data.temp_t1_thres = TEMP_T1_THRES;
	}

	if (of_property_read_u32(np, "temp_t1_thres_plus_x_degree", &val) >= 0)
		info->data.temp_t1_thres_plus_x_degree = val;
	else if (of_property_read_u32(np, "temp-t1-thres-plus-x-degree", &val) >= 0)
		info->data.temp_t1_thres_plus_x_degree = val;
	else {
		chr_err("use default TEMP_T1_THRES_PLUS_X_DEGREE:%d\n",
			TEMP_T1_THRES_PLUS_X_DEGREE);
		info->data.temp_t1_thres_plus_x_degree =
					TEMP_T1_THRES_PLUS_X_DEGREE;
	}

	if (of_property_read_u32(np, "temp_t0_thres", &val) >= 0)
		info->data.temp_t0_thres = val;
	else if (of_property_read_u32(np, "temp-t0-thres", &val) >= 0)
		info->data.temp_t0_thres = val;
	else {
		chr_err("use default TEMP_T0_THRES:%d\n",
			TEMP_T0_THRES);
		info->data.temp_t0_thres = TEMP_T0_THRES;
	}

	if (of_property_read_u32(np, "temp_t0_thres_plus_x_degree", &val) >= 0)
		info->data.temp_t0_thres_plus_x_degree = val;
	else if (of_property_read_u32(np, "temp-t0-thres-plus-x-degree", &val) >= 0)
		info->data.temp_t0_thres_plus_x_degree = val;
	else {
		chr_err("use default TEMP_T0_THRES_PLUS_X_DEGREE:%d\n",
			TEMP_T0_THRES_PLUS_X_DEGREE);
		info->data.temp_t0_thres_plus_x_degree =
					TEMP_T0_THRES_PLUS_X_DEGREE;
	}

	if (of_property_read_u32(np, "temp_neg_10_thres", &val) >= 0)
		info->data.temp_neg_10_thres = val;
	else if (of_property_read_u32(np, "temp-neg-10-thres", &val) >= 0)
		info->data.temp_neg_10_thres = val;
	else {
		chr_err("use default TEMP_NEG_10_THRES:%d\n",
			TEMP_NEG_10_THRES);
		info->data.temp_neg_10_thres = TEMP_NEG_10_THRES;
	}

	/* battery temperature protection */
	info->thermal.sm = BAT_TEMP_NORMAL;
	info->thermal.enable_min_charge_temp =
		of_property_read_bool(np, "enable_min_charge_temp")
		|| of_property_read_bool(np, "enable-min-charge-temp");

	if (of_property_read_u32(np, "min_charge_temp", &val) >= 0)
		info->thermal.min_charge_temp = val;
	else if (of_property_read_u32(np, "min-charge-temp", &val) >= 0)
		info->thermal.min_charge_temp = val;
	else {
		chr_err("use default MIN_CHARGE_TEMP:%d\n",
			MIN_CHARGE_TEMP);
		info->thermal.min_charge_temp = MIN_CHARGE_TEMP;
	}

	if (of_property_read_u32(np, "min_charge_temp_plus_x_degree", &val)
		>= 0) {
		info->thermal.min_charge_temp_plus_x_degree = val;
	} else if (of_property_read_u32(np, "min-charge-temp-plus-x-degree", &val)
		>= 0) {
		info->thermal.min_charge_temp_plus_x_degree = val;
	} else {
		chr_err("use default MIN_CHARGE_TEMP_PLUS_X_DEGREE:%d\n",
			MIN_CHARGE_TEMP_PLUS_X_DEGREE);
		info->thermal.min_charge_temp_plus_x_degree =
					MIN_CHARGE_TEMP_PLUS_X_DEGREE;
	}

	if (of_property_read_u32(np, "max_charge_temp", &val) >= 0)
		info->thermal.max_charge_temp = val;
	else if (of_property_read_u32(np, "max-charge-temp", &val) >= 0)
		info->thermal.max_charge_temp = val;
	else {
		chr_err("use default MAX_CHARGE_TEMP:%d\n",
			MAX_CHARGE_TEMP);
		info->thermal.max_charge_temp = MAX_CHARGE_TEMP;
	}

	if (of_property_read_u32(np, "max_charge_temp_minus_x_degree", &val)
		>= 0) {
		info->thermal.max_charge_temp_minus_x_degree = val;
	} else if (of_property_read_u32(np, "max-charge-temp-minus-x-degree", &val)
		>= 0) {
		info->thermal.max_charge_temp_minus_x_degree = val;
	} else {
		chr_err("use default MAX_CHARGE_TEMP_MINUS_X_DEGREE:%d\n",
			MAX_CHARGE_TEMP_MINUS_X_DEGREE);
		info->thermal.max_charge_temp_minus_x_degree =
					MAX_CHARGE_TEMP_MINUS_X_DEGREE;
	}

	/* charging current */
	if (of_property_read_u32(np, "usb_charger_current", &val) >= 0)
		info->data.usb_charger_current = val;
	else if (of_property_read_u32(np, "usb-charger-current", &val) >= 0)
		info->data.usb_charger_current = val;
	else {
		chr_err("use default USB_CHARGER_CURRENT:%d\n",
			USB_CHARGER_CURRENT);
		info->data.usb_charger_current = USB_CHARGER_CURRENT;
	}

	if (of_property_read_u32(np, "ac_charger_current", &val) >= 0)
		info->data.ac_charger_current = val;
	if (of_property_read_u32(np, "ac-charger-current", &val) >= 0)
		info->data.ac_charger_current = val;
	else {
		chr_err("use default AC_CHARGER_CURRENT:%d\n",
			AC_CHARGER_CURRENT);
		info->data.ac_charger_current = AC_CHARGER_CURRENT;
	}

	if (of_property_read_u32(np, "ac_charger_input_current", &val) >= 0)
		info->data.ac_charger_input_current = val;
	else if (of_property_read_u32(np, "ac-charger-input-current", &val) >= 0)
		info->data.ac_charger_input_current = val;
	else {
		chr_err("use default AC_CHARGER_INPUT_CURRENT:%d\n",
			AC_CHARGER_INPUT_CURRENT);
		info->data.ac_charger_input_current = AC_CHARGER_INPUT_CURRENT;
	}

	if (of_property_read_u32(np, "charging_host_charger_current", &val)
		>= 0) {
		info->data.charging_host_charger_current = val;
	} else if (of_property_read_u32(np, "charging-host-charger-current", &val)
		>= 0) {
		info->data.charging_host_charger_current = val;
	} else {
		chr_err("use default CHARGING_HOST_CHARGER_CURRENT:%d\n",
			CHARGING_HOST_CHARGER_CURRENT);
		info->data.charging_host_charger_current =
					CHARGING_HOST_CHARGER_CURRENT;
	}

	/* dynamic mivr */
	info->enable_dynamic_mivr =
			of_property_read_bool(np, "enable_dynamic_mivr")
			|| of_property_read_bool(np, "enable-dynamic-mivr");

	if (of_property_read_u32(np, "min_charger_voltage_1", &val) >= 0)
		info->data.min_charger_voltage_1 = val;
	else if (of_property_read_u32(np, "min-charger-voltage-1", &val) >= 0)
		info->data.min_charger_voltage_1 = val;
	else {
		chr_err("use default V_CHARGER_MIN_1: %d\n", V_CHARGER_MIN_1);
		info->data.min_charger_voltage_1 = V_CHARGER_MIN_1;
	}

	if (of_property_read_u32(np, "min_charger_voltage_2", &val) >= 0)
		info->data.min_charger_voltage_2 = val;
	else if (of_property_read_u32(np, "min-charger-voltage-2", &val) >= 0)
		info->data.min_charger_voltage_2 = val;
	else {
		chr_err("use default V_CHARGER_MIN_2: %d\n", V_CHARGER_MIN_2);
		info->data.min_charger_voltage_2 = V_CHARGER_MIN_2;
	}

	if (of_property_read_u32(np, "max_dmivr_charger_current", &val) >= 0)
		info->data.max_dmivr_charger_current = val;
	else if (of_property_read_u32(np, "max-dmivr-charger-current", &val) >= 0)
		info->data.max_dmivr_charger_current = val;
	else {
		chr_err("use default MAX_DMIVR_CHARGER_CURRENT: %d\n",
			MAX_DMIVR_CHARGER_CURRENT);
		info->data.max_dmivr_charger_current =
					MAX_DMIVR_CHARGER_CURRENT;
	}
	/* fast charging algo support indicator */
	info->enable_fast_charging_indicator =
			of_property_read_bool(np, "enable_fast_charging_indicator")
			|| of_property_read_bool(np, "enable-fast-charging-indicator");

	/*	adapter priority */
	if (of_property_read_u32(np, "adapter-priority", &val)>= 0)
		info->setting.adapter_priority = val;

/* TN Begin modified by xinjun.lu/860715 20240809 CR/EKLAMU-202 */
	if (of_property_read_u32(np, "jeita_temp_above_t4_icurrent", &val) >= 0)
		info->data.jeita_temp_above_t4_icurrent = val;
	else {
		chr_err("use default jeita_temp_above_t4_icurrent:0\n");
		info->data.jeita_temp_above_t4_icurrent = JEITA_TEMP_ABOVE_T4_CURRENT;
	}
	if (of_property_read_u32(np, "jeita_temp_t3_to_t4_icurrent", &val) >= 0)
		info->data.jeita_temp_t3_to_t4_icurrent = val;
	else {
		chr_err("use default jeita_temp_t3_to_t4_icurrent:%d\n",
			JEITA_TEMP_T3_TO_T4_CURRENT);
		info->data.jeita_temp_t3_to_t4_icurrent = JEITA_TEMP_T3_TO_T4_CURRENT;
	}
	if (of_property_read_u32(np, "jeita_temp_t2_to_t3_icurrent", &val) >= 0)
		info->data.jeita_temp_t2_to_t3_icurrent = val;
	else {
		chr_err("use default jeita_temp_t2_to_t3_icurrent:%d\n",
			JEITA_TEMP_T2_TO_T3_CURRENT);
		info->data.jeita_temp_t2_to_t3_icurrent = JEITA_TEMP_T2_TO_T3_CURRENT;
	}
	if (of_property_read_u32(np, "jeita_temp_t1_to_t2_icurrent", &val) >= 0)
		info->data.jeita_temp_t1_to_t2_icurrent = val;
	else {
		chr_err("use default jeita_temp_t1_to_t2_icurrent:%d\n",
			JEITA_TEMP_T1_TO_T2_CURRENT);
		info->data.jeita_temp_t1_to_t2_icurrent = JEITA_TEMP_T1_TO_T2_CURRENT;
	}
	if (of_property_read_u32(np, "jeita_temp_t0_to_t1_icurrent", &val) >= 0)
		info->data.jeita_temp_t0_to_t1_icurrent = val;
	else {
		chr_err("use default jeita_temp_t0_to_t1_icurrent:%d\n",
			JEITA_TEMP_T0_TO_T1_CURRENT);
		info->data.jeita_temp_t0_to_t1_icurrent = JEITA_TEMP_T0_TO_T1_CURRENT;
	}
	if (of_property_read_u32(np, "jeita_temp_below_t0_icurrent", &val) >= 0)
		info->data.jeita_temp_below_t0_icurrent = val;
	else {
		chr_err("use default jeita_temp_below_t0_icurrent:0\n");
		info->data.jeita_temp_below_t0_icurrent = JEITA_TEMP_BELOW_T0_CURRENT;
	}

#if IS_ENABLED(CONFIG_OEM_HVDCP_ALGO)
	if (of_property_read_u32(np, "hvdcp_input_current_limit", &val) >= 0)
		info->data.hvdcp_input_current_limit = val;
	else {
		chr_err("use default hvdcp_input_current_limit:%d\n",
			HVDCP_CHARGER_INPUT_CURRENT);
		info->data.hvdcp_input_current_limit = HVDCP_CHARGER_INPUT_CURRENT;
	}
	if (of_property_read_u32(np, "hvdcp_charging_current_limit", &val) >= 0)
		info->data.hvdcp_charging_current_limit = val;
	else {
		chr_err("use default hvdcp_charging_current_limit:%d\n",
			HVDCP_CHARGER_CURRENT);
		info->data.hvdcp_charging_current_limit = HVDCP_CHARGER_CURRENT;
	}
#endif /* CONFIG_OEM_HVDCP_ALGO */
#if IS_ENABLED(CONFIG_OEM_TINNO_CHARGER)
	if (of_property_read_u32(np, "pdc_input_current_limit", &val) >= 0)
		info->data.pdc_input_current_limit = val;
	else {
		chr_err("use default pdc_input_current_limit:%d\n",
			PDC_CHARGER_INPUT_CURRENT);
		info->data.pdc_input_current_limit = PDC_CHARGER_INPUT_CURRENT;
	}
	if (of_property_read_u32(np, "pdc_charging_current_limit", &val) >= 0)
		info->data.pdc_charging_current_limit = val;
	else {
		chr_err("use default pdc_charging_current_limit:%d\n",
			PDC_CHARGER_CURRENT);
		info->data.pdc_charging_current_limit = PDC_CHARGER_CURRENT;
	}
	if (of_property_read_u32(np, "eoc_current", &val) >= 0)
		info->data.eoc_current = val;
	else if (of_property_read_u32(np, "eoc-current", &val) >= 0)
		info->data.eoc_current = val;
	else {
		chr_err("use default EOC_CURRENT:%d\n", EOC_CURRENT);
		info->data.eoc_current = EOC_CURRENT;
	}
	chr_err("%s:eoc_current:%d\n", __func__, info->data.eoc_current);
#endif
/* TN End modified by xinjun.lu/860715 20240809 CR/EKLAMU-202 */

/* TN Begin modified by hao.jia/809321 20240628 CR/EKLAMU-202 */
#if IS_ENABLED(CONFIG_OEM_TURBO_CHARGER)
	if (of_find_property(np, "ffc-batt-zone", &byte_len)) {
		if ((byte_len / sizeof(u32)) % 3) {
			chr_err("%s: DT error wrong ffc bat zones\n", __func__);
		}

		info->ffc_zones = (struct ffc_bat_zone *)devm_kzalloc(dev, byte_len, GFP_KERNEL);

		info->num_ffc_zones = byte_len / sizeof(struct ffc_bat_zone);

		if (IS_ERR_OR_NULL(info->ffc_zones))
			chr_err("%s: invalid num_ffc_zones!\n", __func__);

		rc = of_property_read_u32_array(np, "ffc-batt-zone",
					(u32 *)info->ffc_zones, byte_len / sizeof(u32));
		if (rc < 0) {
			chr_err("%s: Couldn't read ffc zones rc(%d)\n", __func__, rc);
		}

		for (i = 0; i < info->num_ffc_zones; i++) {
			chr_info("%s: FFC Zone:%d, Temp:%d, Volt:%d, Ich:%d", __func__, i,
					info->ffc_zones[i].temp,
					info->ffc_zones[i].ffc_max_mv,
					info->ffc_zones[i].ffc_chg_iterm);
		}
	} else
		info->ffc_zones = NULL;
#endif /* CONFIG_OEM_TURBO_CHARGER */
/* TN End modified by hao.jia/809321 20240628 CR/EKLAMU-202 */
	/*	PDtest */
	if (of_property_read_u32(np, "enable-pdtest-mode", &val)>= 0)
		info->en_cts_mode = val;

	/*	dual parallel battery*/
	np = of_parse_phandle(dev->of_node, "current-selector", 0);
	if (np) {
		info->cs_gpio_index = of_get_named_gpio(dev->of_node, "cs-gpios", 0);
		if (of_property_read_string(np, "cs-name",
			&info->curr_select_name) < 0) {
			chr_err("%s: no cs-name\n", __func__);
			info->curr_select_name = "NULL";
		}
		info->cs_with_gauge =
			of_property_read_bool(np, "cs-gauge");
		chr_err("%s: %d\n", __func__, info->cs_with_gauge);
		if (of_property_read_u32(np, "comp-resist", &val) >= 0)
			info->comp_resist = val;
		else
			info->comp_resist = 25;
	} else {
		chr_err("%s: failed to get current_selector\n", __func__);
		info->cs_hw_disable = true;
		info->curr_select_name = "NULL";
	}
}

static void mtk_charger_start_timer(struct mtk_charger *info)
{
	struct timespec64 end_time, time_now;
	ktime_t ktime, ktime_now;
	int ret = 0;

	/* If the timer was already set, cancel it */
	ret = alarm_try_to_cancel(&info->charger_timer);
	if (ret < 0) {
		chr_err("%s: callback was running, skip timer\n", __func__);
		return;
	}

	ktime_now = ktime_get_boottime();
	time_now = ktime_to_timespec64(ktime_now);
	end_time.tv_sec = time_now.tv_sec + info->polling_interval;
	end_time.tv_nsec = time_now.tv_nsec + 0;
	info->endtime = end_time;
	ktime = ktime_set(info->endtime.tv_sec, info->endtime.tv_nsec);

	chr_err("%s: alarm timer start:%d, %lld %ld\n", __func__, ret,
		info->endtime.tv_sec, info->endtime.tv_nsec);
	alarm_start(&info->charger_timer, ktime);
}

static void check_battery_exist(struct mtk_charger *info)
{
	unsigned int i = 0;
	int count = 0;
	//int boot_mode = get_boot_mode();

	if (is_disable_charger(info))
		return;

	for (i = 0; i < 3; i++) {
		if (is_battery_exist(info) == false) {
			count++;
			chr_debug("%s: %d\n", __func__, count);
		}
	}

#ifdef FIXME
	if (count >= 3) {
		if (boot_mode == META_BOOT || boot_mode == ADVMETA_BOOT ||
		    boot_mode == ATE_FACTORY_BOOT)
			chr_info("boot_mode = %d, bypass battery check\n",
				boot_mode);
		else {
			chr_err("battery doesn't exist, shutdown\n");
			orderly_poweroff(true);
		}
	}
#endif
}

static void check_dynamic_mivr(struct mtk_charger *info)
{
	int i = 0, ret = 0;
	int vbat = 0;
	bool is_fast_charge = false;
	struct chg_alg_device *alg = NULL;

	if (!info->enable_dynamic_mivr)
		return;

	for (i = 0; i < MAX_ALG_NO; i++) {
		alg = info->alg[i];
		if (alg == NULL)
			continue;

		ret = chg_alg_is_algo_ready(alg);
		if (ret == ALG_RUNNING) {
			is_fast_charge = true;
			break;
		}
	}

	if (!is_fast_charge) {
		vbat = get_battery_voltage(info);
		if (vbat < info->data.min_charger_voltage_2 / 1000 - 200)
			charger_dev_set_mivr(info->chg1_dev,
				info->data.min_charger_voltage_2);
		else if (vbat < info->data.min_charger_voltage_1 / 1000 - 200)
			charger_dev_set_mivr(info->chg1_dev,
				info->data.min_charger_voltage_1);
		else
			charger_dev_set_mivr(info->chg1_dev,
				info->data.min_charger_voltage);
	}
}

/* sw jeita */
void do_sw_jeita_state_machine(struct mtk_charger *info)
{
	struct sw_jeita_data *sw_jeita;

	sw_jeita = &info->sw_jeita;
	sw_jeita->pre_sm = sw_jeita->sm;
	sw_jeita->charging = true;

/* TN Begin modified by xinjun.lu/860715 20240904 CR/EKLAMU-202 */
#if IS_ENABLED(CONFIG_OEM_TINNO_CHARGER)
	struct charger_data *pdata;
	pdata = &info->chg_data[CHG1_SETTING];
	int vbat = get_battery_voltage(info);
	int ibat = get_battery_current(info);
#endif /* CONFIG_OEM_TINNO_CHARGER */
/* TN End modified by xinjun.lu/860715 20240904 CR/EKLAMU-202 */

	/* JEITA battery temp Standard */
	if (info->battery_temp >= info->data.temp_t4_thres) {
		chr_err("[SW_JEITA] Battery Over high Temperature(%d) !!\n",
			info->data.temp_t4_thres);

		sw_jeita->sm = TEMP_ABOVE_T4;
		sw_jeita->charging = false;
	} else if (info->battery_temp > info->data.temp_t3_thres) {
		/* control 45 degree to normal behavior */
		if ((sw_jeita->sm == TEMP_ABOVE_T4)
		    && (info->battery_temp
			>= info->data.temp_t4_thres_minus_x_degree)) {
			chr_err("[SW_JEITA] Battery Temperature between %d and %d,not allow charging yet!!\n",
				info->data.temp_t4_thres_minus_x_degree,
				info->data.temp_t4_thres);

			sw_jeita->charging = false;
		} else {
			chr_err("[SW_JEITA] Battery Temperature between %d and %d !!\n",
				info->data.temp_t3_thres,
				info->data.temp_t4_thres);

			sw_jeita->sm = TEMP_T3_TO_T4;
		}
	} else if (info->battery_temp >= info->data.temp_t2_thres) {
		if (((sw_jeita->sm == TEMP_T3_TO_T4)
		     && (info->battery_temp
			 >= info->data.temp_t3_thres_minus_x_degree))
		    || ((sw_jeita->sm == TEMP_T1_TO_T2)
			&& (info->battery_temp
			    <= info->data.temp_t2_thres_plus_x_degree))) {
			chr_err("[SW_JEITA] Battery Temperature not recovery to normal temperature charging mode yet!!\n");
		} else {
			chr_err("[SW_JEITA] Battery Normal Temperature between %d and %d !!\n",
				info->data.temp_t2_thres,
				info->data.temp_t3_thres);
			sw_jeita->sm = TEMP_T2_TO_T3;
		}
	} else if (info->battery_temp >= info->data.temp_t1_thres) {
		if ((sw_jeita->sm == TEMP_T0_TO_T1
		     || sw_jeita->sm == TEMP_BELOW_T0)
		    && (info->battery_temp
			<= info->data.temp_t1_thres_plus_x_degree)) {
			if (sw_jeita->sm == TEMP_T0_TO_T1) {
				chr_err("[SW_JEITA] Battery Temperature between %d and %d !!\n",
					info->data.temp_t1_thres_plus_x_degree,
					info->data.temp_t2_thres);
			}
			if (sw_jeita->sm == TEMP_BELOW_T0) {
				chr_err("[SW_JEITA] Battery Temperature between %d and %d,not allow charging yet!!\n",
					info->data.temp_t1_thres,
					info->data.temp_t1_thres_plus_x_degree);
				sw_jeita->charging = false;
			}
		} else {
			chr_err("[SW_JEITA] Battery Temperature between %d and %d !!\n",
				info->data.temp_t1_thres,
				info->data.temp_t2_thres);

			sw_jeita->sm = TEMP_T1_TO_T2;
		}
	} else if (info->battery_temp >= info->data.temp_t0_thres) {
		if ((sw_jeita->sm == TEMP_BELOW_T0)
		    && (info->battery_temp
			<= info->data.temp_t0_thres_plus_x_degree)) {
			chr_err("[SW_JEITA] Battery Temperature between %d and %d,not allow charging yet!!\n",
				info->data.temp_t0_thres,
				info->data.temp_t0_thres_plus_x_degree);

			sw_jeita->charging = false;
		} else {
			chr_err("[SW_JEITA] Battery Temperature between %d and %d !!\n",
				info->data.temp_t0_thres,
				info->data.temp_t1_thres);

			sw_jeita->sm = TEMP_T0_TO_T1;
		}
	} else {
		chr_err("[SW_JEITA] Battery below low Temperature(%d) !!\n",
			info->data.temp_t0_thres);
		sw_jeita->sm = TEMP_BELOW_T0;
		sw_jeita->charging = false;
	}

	/* set CV after temperature changed */
	/* In normal range, we adjust CV dynamically */
	if (sw_jeita->sm != TEMP_T2_TO_T3) {
		if (sw_jeita->sm == TEMP_ABOVE_T4)
			sw_jeita->cv = info->data.jeita_temp_above_t4_cv;
		else if (sw_jeita->sm == TEMP_T3_TO_T4)
			sw_jeita->cv = info->data.jeita_temp_t3_to_t4_cv;
		else if (sw_jeita->sm == TEMP_T2_TO_T3)
			sw_jeita->cv = 0;
		else if (sw_jeita->sm == TEMP_T1_TO_T2)
			sw_jeita->cv = info->data.jeita_temp_t1_to_t2_cv;
		else if (sw_jeita->sm == TEMP_T0_TO_T1)
			sw_jeita->cv = info->data.jeita_temp_t0_to_t1_cv;
		else if (sw_jeita->sm == TEMP_BELOW_T0)
			sw_jeita->cv = info->data.jeita_temp_below_t0_cv;
		else
			sw_jeita->cv = info->data.battery_cv;
	} else {
		sw_jeita->cv = 0;
	}

/* TN Begin modified by jirui.li/860702 20240904 CR/EKLAMU-834 */
#if IS_ENABLED(CONFIG_OEM_TINNO_CHARGER)
	if (sw_jeita->sm == TEMP_ABOVE_T4)
		pdata->temp_charging_current_limit = info->data.jeita_temp_above_t4_icurrent;
	else if (sw_jeita->sm == TEMP_T3_TO_T4)
		pdata->temp_charging_current_limit = info->data.jeita_temp_t3_to_t4_icurrent;
	else if (sw_jeita->sm == TEMP_T2_TO_T3)
		pdata->temp_charging_current_limit = info->data.jeita_temp_t2_to_t3_icurrent;
	else if (sw_jeita->sm == TEMP_T1_TO_T2)
		pdata->temp_charging_current_limit = info->data.jeita_temp_t1_to_t2_icurrent;
	else if (sw_jeita->sm == TEMP_T0_TO_T1)
		pdata->temp_charging_current_limit = info->data.jeita_temp_t0_to_t1_icurrent;
	else if (sw_jeita->sm == TEMP_BELOW_T0)
		pdata->temp_charging_current_limit = info->data.jeita_temp_below_t0_icurrent;
	else
		pdata->temp_charging_current_limit = 0;

	if (sw_jeita->sm == TEMP_T1_TO_T2 && info->battery_temp < SW_JEITA_TEMP_10) {
		sw_jeita->cv = SW_JEITA_CV1;

		if (((ibat <= SW_JEITA_CV1_CURRENT_LIMIT + SW_JEITA_CV1_CURRENT_LIMIT_GAP)
			&& (vbat >= SW_JEITA_CV1 / 1000 - 50))
			|| (vbat >= SW_JEITA_CV1 / 1000 + 200)) {
			sw_jeita_enter_1A = true;
		}

		if (sw_jeita_enter_1A) {
			sw_jeita_enter_cv2 = true;
			pdata->temp_charging_current_limit = SW_JEITA_CV1_CURRENT_LIMIT * 1000;
		}

		if (sw_jeita_enter_cv2)
			sw_jeita->cv = SW_JEITA_CV2;
	}

	chr_err("[SW_JEITA] temp_curr:%d\n", pdata->temp_charging_current_limit);
#endif /* CONFIG_OEM_TINNO_CHARGER */
/* TN End modified by jirui.li/860702 20240904 CR/EKLAMU-834 */

	chr_err("[SW_JEITA]preState:%d newState:%d tmp:%d cv:%d\n",
		sw_jeita->pre_sm, sw_jeita->sm, info->battery_temp,
		sw_jeita->cv);
}

static int mtk_chgstat_notify(struct mtk_charger *info)
{
	int ret = 0;
	char *env[2] = { "CHGSTAT=1", NULL };

	chr_err("%s: 0x%x\n", __func__, info->notify_code);
	ret = kobject_uevent_env(&info->pdev->dev.kobj, KOBJ_CHANGE, env);
	if (ret)
		chr_err("%s: kobject_uevent_fail, ret=%d", __func__, ret);

	return ret;
}

static void mtk_charger_set_algo_log_level(struct mtk_charger *info, int level)
{
	struct chg_alg_device *alg;
	int i = 0, ret = 0;

	for (i = 0; i < MAX_ALG_NO; i++) {
		alg = info->alg[i];
		if (alg == NULL)
			continue;

		ret = chg_alg_set_prop(alg, ALG_LOG_LEVEL, level);
		if (ret < 0)
			chr_err("%s: set ALG_LOG_LEVEL fail, ret =%d", __func__, ret);
	}
}

static ssize_t sw_jeita_show(struct device *dev, struct device_attribute *attr,
					       char *buf)
{
	struct mtk_charger *pinfo = dev->driver_data;

	chr_err("%s: %d\n", __func__, pinfo->enable_sw_jeita);
	return sprintf(buf, "%d\n", pinfo->enable_sw_jeita);
}

static ssize_t sw_jeita_store(struct device *dev, struct device_attribute *attr,
						const char *buf, size_t size)
{
	struct mtk_charger *pinfo = dev->driver_data;
	signed int temp;

	if (kstrtoint(buf, 10, &temp) == 0) {
		if (temp == 0)
			pinfo->enable_sw_jeita = false;
		else
			pinfo->enable_sw_jeita = true;

	} else {
		chr_err("%s: format error!\n", __func__);
	}
	return size;
}

static DEVICE_ATTR_RW(sw_jeita);
/* sw jeita end*/

static ssize_t sw_ovp_threshold_show(struct device *dev, struct device_attribute *attr,
					       char *buf)
{
	struct mtk_charger *pinfo = dev->driver_data;

	chr_err("%s: %d\n", __func__, pinfo->data.max_charger_voltage);
	return sprintf(buf, "%d\n", pinfo->data.max_charger_voltage);
}

static ssize_t sw_ovp_threshold_store(struct device *dev, struct device_attribute *attr,
						const char *buf, size_t size)
{
	struct mtk_charger *pinfo = dev->driver_data;
	signed int temp;

	if (kstrtoint(buf, 10, &temp) == 0) {
		if (temp < 0)
			pinfo->data.max_charger_voltage = pinfo->data.vbus_sw_ovp_voltage;
		else
			pinfo->data.max_charger_voltage = temp;
		chr_err("%s: %d\n", __func__, pinfo->data.max_charger_voltage);

	} else {
		chr_err("%s: format error!\n", __func__);
	}
	return size;
}

static DEVICE_ATTR_RW(sw_ovp_threshold);

static ssize_t chr_type_show(struct device *dev, struct device_attribute *attr,
					       char *buf)
{
	struct mtk_charger *pinfo = dev->driver_data;

	chr_err("%s: %d\n", __func__, pinfo->chr_type);
	return sprintf(buf, "%d\n", pinfo->chr_type);
}

static ssize_t chr_type_store(struct device *dev, struct device_attribute *attr,
						const char *buf, size_t size)
{
	struct mtk_charger *pinfo = dev->driver_data;
	signed int temp;

	if (kstrtoint(buf, 10, &temp) == 0)
		pinfo->chr_type = temp;
	else
		chr_err("%s: format error!\n", __func__);

	return size;
}

static DEVICE_ATTR_RW(chr_type);

static ssize_t ta_type_show(struct device *dev, struct device_attribute *attr,
					       char *buf)
{
	struct mtk_charger *pinfo = dev->driver_data;
	char *ta_type_name = "None";
	int ta_type = MTK_CAP_TYPE_UNKNOWN;

	ta_type = adapter_dev_get_property(pinfo->select_adapter, CAP_TYPE);
	switch (ta_type) {
	case MTK_CAP_TYPE_UNKNOWN:
		ta_type_name = "None";
		break;
	case MTK_PD:
		ta_type_name = "PD";
		break;
	case MTK_UFCS:
		ta_type_name = "UFCS";
		break;
	case MTK_PD_APDO:
		ta_type_name = "PD with PPS";
		break;
	}
	chr_err("%s: %d\n", __func__, ta_type);
	return sprintf(buf, "%s\n", ta_type_name);
}

static DEVICE_ATTR_RO(ta_type);


static ssize_t Pump_Express_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	int ret = 0, i = 0;
	bool is_ta_detected = false;
	struct mtk_charger *pinfo = dev->driver_data;
	struct chg_alg_device *alg = NULL;

	if (!pinfo) {
		chr_err("%s: pinfo is null\n", __func__);
		return sprintf(buf, "%d\n", is_ta_detected);
	}

	for (i = 0; i < MAX_ALG_NO; i++) {
		alg = pinfo->alg[i];
		if (alg == NULL)
			continue;
		ret = chg_alg_is_algo_ready(alg);
		if (ret == ALG_RUNNING) {
			is_ta_detected = true;
			break;
		}
	}
	chr_err("%s: idx = %d, detect = %d\n", __func__, i, is_ta_detected);
	return sprintf(buf, "%d\n", is_ta_detected);
}

static DEVICE_ATTR_RO(Pump_Express);

static ssize_t Charging_mode_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	int ret = 0, i = 0;
	char *alg_name = "normal";
	bool is_ta_detected = false;
	struct mtk_charger *pinfo = dev->driver_data;
	struct chg_alg_device *alg = NULL;

	if (!pinfo) {
		chr_err("%s: pinfo is null\n", __func__);
		return sprintf(buf, "%d\n", is_ta_detected);
	}

	for (i = 0; i < MAX_ALG_NO; i++) {
		alg = pinfo->alg[i];
		if (alg == NULL)
			continue;
		ret = chg_alg_is_algo_ready(alg);
		if (ret == ALG_RUNNING) {
			is_ta_detected = true;
			break;
		}
	}
	if (alg == NULL)
		return sprintf(buf, "%s\n", alg_name);

	switch (alg->alg_id) {
	case PE_ID:
		alg_name = "PE";
		break;
	case PE2_ID:
		alg_name = "PE2";
		break;
	case PDC_ID:
		alg_name = "PDC";
		break;
	case PE4_ID:
		alg_name = "PE4";
		break;
	case PE5_ID:
		alg_name = "P5";
		break;
	case PE5P_ID:
		alg_name = "P5P";
		break;
	}
	chr_err("%s: charging_mode: %s\n", __func__, alg_name);
	return sprintf(buf, "%s\n", alg_name);
}

static DEVICE_ATTR_RO(Charging_mode);

static ssize_t High_voltage_chg_enable_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct mtk_charger *pinfo = dev->driver_data;

	chr_err("%s: hv_charging = %d\n", __func__, pinfo->enable_hv_charging);
	return sprintf(buf, "%d\n", pinfo->enable_hv_charging);
}

static DEVICE_ATTR_RO(High_voltage_chg_enable);

static ssize_t Rust_detect_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct mtk_charger *pinfo = dev->driver_data;

	chr_err("%s: Rust detect = %d\n", __func__, pinfo->record_water_detected);
	return sprintf(buf, "%d\n", pinfo->record_water_detected);
}

static DEVICE_ATTR_RO(Rust_detect);

static ssize_t Thermal_throttle_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct mtk_charger *pinfo = dev->driver_data;
	struct charger_data *chg_data = &(pinfo->chg_data[CHG1_SETTING]);

	return sprintf(buf, "%d\n", chg_data->thermal_throttle_record);
}

static DEVICE_ATTR_RO(Thermal_throttle);

static ssize_t fast_chg_indicator_show(struct device *dev, struct device_attribute *attr,
					       char *buf)
{
	struct mtk_charger *pinfo = dev->driver_data;

	chr_debug("%s: %d\n", __func__, pinfo->fast_charging_indicator);
	return sprintf(buf, "%d\n", pinfo->fast_charging_indicator);
}

static ssize_t fast_chg_indicator_store(struct device *dev, struct device_attribute *attr,
						const char *buf, size_t size)
{
	struct mtk_charger *pinfo = dev->driver_data;
	unsigned int temp;

	if (kstrtouint(buf, 10, &temp) == 0)
		pinfo->fast_charging_indicator = temp;
	else
		chr_err("%s: format error!\n", __func__);

	if ((pinfo->fast_charging_indicator > 0) &&
	    (pinfo->bootmode == 8 || pinfo->bootmode == 9)) {
		pinfo->log_level = CHRLOG_DEBUG_LEVEL;
		mtk_charger_set_algo_log_level(pinfo, pinfo->log_level);
	}

	_wake_up_charger(pinfo);
	return size;
}

static DEVICE_ATTR_RW(fast_chg_indicator);

static ssize_t alg_new_arbitration_show(struct device *dev, struct device_attribute *attr,
						char *buf)
{
	struct mtk_charger *pinfo = dev->driver_data;

	chr_debug("%s: %d\n", __func__, pinfo->alg_new_arbitration);
	return sprintf(buf, "%d\n", pinfo->alg_new_arbitration);
}

static ssize_t alg_new_arbitration_store(struct device *dev, struct device_attribute *attr,
						const char *buf, size_t size)
{
	struct mtk_charger *pinfo = dev->driver_data;
	unsigned int temp;

	if (kstrtouint(buf, 10, &temp) == 0)
		pinfo->alg_new_arbitration = temp;
	else
		chr_err("%s: format error!\n", __func__);

	_wake_up_charger(pinfo);
	return size;
}

static DEVICE_ATTR_RW(alg_new_arbitration);

static ssize_t alg_unchangeable_show(struct device *dev, struct device_attribute *attr,
					       char *buf)
{
	struct mtk_charger *pinfo = dev->driver_data;

	chr_debug("%s: %d\n", __func__, pinfo->alg_unchangeable);
	return sprintf(buf, "%d\n", pinfo->alg_unchangeable);
}

static ssize_t alg_unchangeable_store(struct device *dev, struct device_attribute *attr,
						const char *buf, size_t size)
{
	struct mtk_charger *pinfo = dev->driver_data;
	unsigned int temp;

	if (kstrtouint(buf, 10, &temp) == 0)
		pinfo->alg_unchangeable = temp;
	else
		chr_err("%s: format error!\n", __func__);

	_wake_up_charger(pinfo);
	return size;
}

static DEVICE_ATTR_RW(alg_unchangeable);

static ssize_t enable_meta_current_limit_show(struct device *dev, struct device_attribute *attr,
					       char *buf)
{
	struct mtk_charger *pinfo = dev->driver_data;

	chr_debug("%s: %d\n", __func__, pinfo->enable_meta_current_limit);
	return sprintf(buf, "%d\n", pinfo->enable_meta_current_limit);
}

static ssize_t enable_meta_current_limit_store(struct device *dev, struct device_attribute *attr,
						const char *buf, size_t size)
{
	struct mtk_charger *pinfo = dev->driver_data;
	unsigned int temp;

	if (kstrtouint(buf, 10, &temp) == 0)
		pinfo->enable_meta_current_limit = temp;
	else
		chr_err("%s: format error!\n", __func__);

	if (pinfo->enable_meta_current_limit > 0) {
		pinfo->log_level = CHRLOG_DEBUG_LEVEL;
		mtk_charger_set_algo_log_level(pinfo, pinfo->log_level);
	}

	_wake_up_charger(pinfo);
	return size;
}

static DEVICE_ATTR_RW(enable_meta_current_limit);

static ssize_t cs_heatlim_show(struct device *dev, struct device_attribute *attr,
					       char *buf)
{
	struct mtk_charger *pinfo = dev->driver_data;

	chr_debug("%s: %d\n", __func__, pinfo->cs_heatlim);
	return sprintf(buf, "%d\n", pinfo->cs_heatlim);
}

static ssize_t cs_heatlim_store(struct device *dev, struct device_attribute *attr,
						const char *buf, size_t size)
{
	struct mtk_charger *pinfo = dev->driver_data;
	unsigned int temp;

	if (kstrtouint(buf, 10, &temp) == 0)
		pinfo->cs_heatlim = temp;
	else
		chr_err("%s: format error!\n", __func__);

	if (pinfo->cs_heatlim > 0) {
		pinfo->log_level = CHRLOG_DEBUG_LEVEL;
		mtk_charger_set_algo_log_level(pinfo, pinfo->log_level);
	}

	_wake_up_charger(pinfo);
	return size;
}

static DEVICE_ATTR_RW(cs_heatlim);

static ssize_t cs_para_mode_show(struct device *dev, struct device_attribute *attr,
					       char *buf)
{
	struct mtk_charger *pinfo = dev->driver_data;

	chr_debug("%s: %d\n", __func__, pinfo->cs_para_mode);
	return sprintf(buf, "%d\n", pinfo->cs_para_mode);
}

static ssize_t cs_para_mode_store(struct device *dev, struct device_attribute *attr,
						const char *buf, size_t size)
{
	struct mtk_charger *pinfo = dev->driver_data;
	unsigned int temp;

	if (kstrtouint(buf, 10, &temp) == 0)
		pinfo->cs_para_mode = temp;
	else
		chr_err("%s: format error!\n", __func__);

	if (pinfo->cs_para_mode > 0) {
		pinfo->log_level = CHRLOG_DEBUG_LEVEL;
		mtk_charger_set_algo_log_level(pinfo, pinfo->log_level);
	}

	_wake_up_charger(pinfo);
	return size;
}

static DEVICE_ATTR_RW(cs_para_mode);

static ssize_t vbat_mon_show(struct device *dev, struct device_attribute *attr,
					       char *buf)
{
	struct mtk_charger *pinfo = dev->driver_data;

	chr_debug("%s: %d\n", __func__, pinfo->enable_vbat_mon);
	return sprintf(buf, "%d\n", pinfo->enable_vbat_mon);
}

static ssize_t vbat_mon_store(struct device *dev, struct device_attribute *attr,
						const char *buf, size_t size)
{
	struct mtk_charger *pinfo = dev->driver_data;
	unsigned int temp;

	if (kstrtouint(buf, 10, &temp) == 0) {
		if (temp == 0)
			pinfo->enable_vbat_mon = false;
		else
			pinfo->enable_vbat_mon = true;
	} else {
		chr_err("%s: format error!\n", __func__);
	}

	_wake_up_charger(pinfo);
	return size;
}

static DEVICE_ATTR_RW(vbat_mon);

static ssize_t ADC_Charger_Voltage_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct mtk_charger *pinfo = dev->driver_data;
	int vbus = get_vbus(pinfo); /* mV */

	chr_err("%s: %d\n", __func__, vbus);
	return sprintf(buf, "%d\n", vbus);
}

static DEVICE_ATTR_RO(ADC_Charger_Voltage);

static ssize_t ADC_Charging_Current_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct mtk_charger *pinfo = dev->driver_data;
	int ibat = get_battery_current(pinfo); /* mA */

	chr_err("%s: %d\n", __func__, ibat);
	return sprintf(buf, "%d\n", ibat);
}

static DEVICE_ATTR_RO(ADC_Charging_Current);

static ssize_t input_current_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct mtk_charger *pinfo = dev->driver_data;
	int aicr = 0;

	aicr = pinfo->chg_data[CHG1_SETTING].thermal_input_current_limit;
	chr_err("%s: %d\n", __func__, aicr);
	return sprintf(buf, "%d\n", aicr);
}

static ssize_t input_current_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t size)
{
	struct mtk_charger *pinfo = dev->driver_data;
	struct charger_data *chg_data;
	signed int temp;

	chg_data = &pinfo->chg_data[CHG1_SETTING];
	if (kstrtoint(buf, 10, &temp) == 0) {
		if (temp < 0)
			chg_data->thermal_input_current_limit = 0;
		else
			chg_data->thermal_input_current_limit = temp;
	} else {
		chr_err("%s: format error!\n", __func__);
	}
	return size;
}

static DEVICE_ATTR_RW(input_current);

static ssize_t charger_log_level_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct mtk_charger *pinfo = dev->driver_data;

	chr_err("%s: %d\n", __func__, pinfo->log_level);
	return sprintf(buf, "%d\n", pinfo->log_level);
}

static ssize_t charger_log_level_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t size)
{
	struct mtk_charger *pinfo = dev->driver_data;
	int temp;

	if (kstrtoint(buf, 10, &temp) == 0) {
		if (temp < 0) {
			chr_err("%s: val is invalid: %d\n", __func__, temp);
			temp = 0;
		}
		pinfo->log_level = temp;
		chr_err("%s: log_level=%d\n", __func__, pinfo->log_level);

		mtk_charger_set_algo_log_level(pinfo, pinfo->log_level);
		_wake_up_charger(pinfo);

	} else {
		chr_err("%s: format error!\n", __func__);
	}
	return size;
}

static DEVICE_ATTR_RW(charger_log_level);

static ssize_t BatteryNotify_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mtk_charger *pinfo = dev->driver_data;

	chr_info("%s: 0x%x\n", __func__, pinfo->notify_code);

	return sprintf(buf, "%u\n", pinfo->notify_code);
}

static ssize_t BatteryNotify_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct mtk_charger *pinfo = dev->driver_data;
	unsigned int reg = 0;
	int ret = 0;

	if (buf != NULL && size != 0) {
		ret = kstrtouint(buf, 16, &reg);
		if (ret < 0) {
			chr_err("%s: failed, ret = %d\n", __func__, ret);
			return ret;
		}
		pinfo->notify_code = reg;
		chr_info("%s: store code=0x%x\n", __func__, pinfo->notify_code);
		mtk_chgstat_notify(pinfo);
	}
	return size;
}

static DEVICE_ATTR_RW(BatteryNotify);

/* TN Begin modified by hao.jia/809321 20240718 CR/EKLAMU-202 */
#if IS_ENABLED(CONFIG_OEM_TINNO_CHARGER) && IS_ENABLED(CONFIG_FACTORY_BUILD)
static ssize_t factory_enable_switch_charger_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct mtk_charger *pinfo = dev->driver_data;

	chr_err("%s: %d\n", __func__, pinfo->factory_enable_switch_charger);
	return sprintf(buf, "%d\n", pinfo->factory_enable_switch_charger);
}

static ssize_t factory_enable_switch_charger_store(struct device *dev,
				  struct device_attribute *attr, const char *buf, size_t size)
{
	struct mtk_charger *pinfo = dev->driver_data;
	signed int temp;
	if (kstrtoint(buf, 10, &temp) == 0) {
		if (temp == 1) {
			pinfo->enable_factory_charging_test = true;
			charger_dev_enable(pinfo->dvchg1_dev, false);
			msleep(100);
			charger_dev_set_input_current(pinfo->chg1_dev, 2000000);
			charger_dev_set_charging_current(pinfo->chg1_dev, 1500000);
			charger_dev_enable(pinfo->chg1_dev, true);
		} else {
			pinfo->enable_factory_charging_test = false;
			charger_dev_enable(pinfo->chg1_dev, false);
		}
		pinfo->factory_enable_switch_charger = temp;
	}
	chr_err("%s: %d\n", __func__, pinfo->factory_enable_switch_charger);
	return size;
}

static DEVICE_ATTR_RW(factory_enable_switch_charger);

static ssize_t factory_enable_pump_charger_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct mtk_charger *pinfo = dev->driver_data;

	chr_err("%s: %d\n", __func__, pinfo->factory_enable_pump_charger);
	return sprintf(buf, "%d\n", pinfo->factory_enable_pump_charger);
}

static ssize_t factory_enable_pump_charger_store(struct device *dev,
				  struct device_attribute *attr, const char *buf, size_t size)
{
	struct mtk_charger *pinfo = dev->driver_data;
	signed int temp;
	if (kstrtoint(buf, 10, &temp) == 0) {
		if (temp == 1) {
			pinfo->enable_factory_charging_test = true;
			charger_dev_enable(pinfo->chg1_dev, false);
			charger_dev_enable_adc(pinfo->dvchg1_dev, true);
			msleep(100);
			charger_dev_enable(pinfo->dvchg1_dev, true);
		} else {
			pinfo->enable_factory_charging_test = false;
			charger_dev_enable(pinfo->dvchg1_dev, false);
			charger_dev_enable_adc(pinfo->dvchg1_dev, false);
		}
		pinfo->factory_enable_pump_charger = temp;
	}
	chr_err("%s: %d\n", __func__, pinfo->factory_enable_pump_charger);
	return size;
}

static DEVICE_ATTR_RW(factory_enable_pump_charger);

/* TN Begin modified by jirui.li/860702 20240724 CR/EKLAMU-620 */
#define FACTORY_CHARGING_LIMIT_SOC_DEFAULT 65
static ssize_t factory_charging_limit_soc_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct mtk_charger *pinfo = dev->driver_data;
	chr_err("%s: %d\n", __func__, pinfo->factory_charging_limit_soc);
	return sprintf(buf, "%d\n", pinfo->factory_charging_limit_soc);
}
static ssize_t factory_charging_limit_soc_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t size)
{
	struct mtk_charger *pinfo = dev->driver_data;
	signed int temp;
	if (kstrtoint(buf, 10, &temp) == 0) {
		if (temp <= 100 && temp >= 0)
			pinfo->factory_charging_limit_soc = temp;
		else
			pinfo->factory_charging_limit_soc = FACTORY_CHARGING_LIMIT_SOC_DEFAULT;
		chr_err("%s: %d\n", __func__, temp);
	} else
		chr_err("%s: format error!\n", __func__);
	return size;
}
static DEVICE_ATTR_RW(factory_charging_limit_soc);
/* TN Begin modified by jirui.li/860702 20240724 CR/EKLAMU-620 */
#endif /* CONFIG_OEM_TINNO_CHARGER && CONFIG_FACTORY_BUILD */

#if IS_ENABLED(CONFIG_OEM_TINNO_CHARGER)
static ssize_t enable_hiz_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct mtk_charger *pinfo = dev->driver_data;
	bool en_hiz = false;

	en_hiz = pinfo->enable_hiz;

	chr_err("%s: %d\n", __func__, en_hiz);
	return sprintf(buf, "%d\n", en_hiz);
}

static ssize_t enable_hiz_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t size)
{
	struct mtk_charger *pinfo = dev->driver_data;
	signed int temp;

	if (kstrtoint(buf, 10, &temp) == 0) {
		if (temp == 1) {
			pinfo->enable_hiz = true;
		} else {
			pinfo->enable_hiz = false;
		}
		charger_dev_enable_hz(pinfo->chg1_dev, pinfo->enable_hiz);
		_wake_up_charger(pinfo);
	} else {
		chr_err("%s: format error!\n", __func__);
	}
	return size;
}

static DEVICE_ATTR_RW(enable_hiz);

static ssize_t enable_charger_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct mtk_charger *pinfo = dev->driver_data;
	bool en_chg = true;

	en_chg = pinfo->enable_charger;

	chr_err("%s: %d\n", __func__, en_chg);
	return sprintf(buf, "%d\n", en_chg);
}

static ssize_t enable_charger_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t size)
{
	struct mtk_charger *pinfo = dev->driver_data;
	signed int temp;

	if (kstrtoint(buf, 10, &temp) == 0) {
		if (temp == 1) {
			pinfo->enable_charger = true;
		} else {
			pinfo->enable_charger = false;
		}
		charger_dev_enable(pinfo->chg1_dev, pinfo->enable_charger);
		_wake_up_charger(pinfo);
	} else {
		chr_err("%s: format error!\n", __func__);
	}
	return size;
}

static DEVICE_ATTR_RW(enable_charger);

/* TN Begin modified by xinjun.lu/860715 20240719 CR/EKLAMU-202 */
static ssize_t disable_thermal_current_limit_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct mtk_charger *pinfo = dev->driver_data;

	chr_err("%s: %d\n", __func__, pinfo->disable_thermal_current_limit);
	return sprintf(buf, "%d\n", pinfo->disable_thermal_current_limit);
}

static ssize_t disable_thermal_current_limit_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t size)
{
	struct mtk_charger *pinfo = dev->driver_data;
	int temp;

	if (kstrtoint(buf, 10, &temp) == 0) {
		if (temp < 0) {
			chr_err("%s: val is invalid: %d\n", __func__, temp);
			temp = 0;
		}
		pinfo->disable_thermal_current_limit = temp;
		chr_err("%s: %d\n", __func__, pinfo->disable_thermal_current_limit);
	} else {
		chr_err("%s: format error!\n", __func__);
	}
	return size;
}

static DEVICE_ATTR_RW(disable_thermal_current_limit);
/* TN End modified by xinjun.lu/860715 20240719 CR/EKLAMU-202 */

/* TN Begin modified by jirui.li/860702 20240814 CR/EKLAMU-1339 */
static ssize_t battery_protection_mode_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct mtk_charger *pinfo = dev->driver_data;

	chr_info("%s:%d\n", __func__, pinfo->battery_protection_mode);
	return sprintf(buf, "%d\n", pinfo->battery_protection_mode);
}
static ssize_t battery_protection_mode_store(struct device *dev , struct device_attribute *attr , const char *buf, size_t size)
{
	struct mtk_charger *pinfo = dev->driver_data;
	int temp;

	if (kstrtoint(buf, 10, &temp) == 0) {
		chr_info("%s %s battery_protection\n", __func__, temp ? "enable" : "disable");
		if (temp == 0){
			pinfo->battery_protection_mode = false;
		} else if (temp == 1) {
			pinfo->battery_protection_mode = true;
		}
	} else {
		chr_err("%s: format error!\n", __func__);
	}
        return size;
}
static DEVICE_ATTR_RW(battery_protection_mode);
/* TN End modified by jirui.li/860702 20240814 CR/EKLAMU-1339 */

/* TN Begin modified by jirui.li/860702 20240814 CR/EKLAMU-30 */
#if IS_ENABLED(CONFIG_OEM_TURBO_CHARGER) || IS_ENABLED(CONFIG_PE50_FFC_SUPPORT)
static ssize_t turbo_power_mode_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct mtk_charger *pinfo = dev->driver_data;
	int value = 0;
	int chr_type = get_charger_type(pinfo);

#if IS_ENABLED(CONFIG_OEM_TURBO_CHARGER)
#if IS_ENABLED(CONFIG_PE50_FFC_SUPPORT)
	if ((chr_type == POWER_SUPPLY_TYPE_USB_QC3 || chr_type == POWER_SUPPLY_TYPE_USB_QC3P || chr_type == POWER_SUPPLY_TYPE_USB_PDC) ||
	    (pinfo->pe50.apdo_cap.pdp > 15 && chr_type == POWER_SUPPLY_TYPE_USB_DCP)) {
#else
	if (chr_type == POWER_SUPPLY_TYPE_USB_QC3 || chr_type == POWER_SUPPLY_TYPE_USB_QC3P ||
	    chr_type == POWER_SUPPLY_TYPE_USB_PDC) {
#endif
		turbo_power_mode = 1;
	} else {
		turbo_power_mode = 0;
	}
#endif

#if IS_ENABLED(CONFIG_PE50_FFC_SUPPORT)
	if (!IS_ERR_OR_NULL(pinfo->current_alg) && pinfo->current_alg->alg_id == PE5_ID) {
		value = 1;
	}
#endif

#if IS_ENABLED(CONFIG_OEM_TURBO_CHARGER)
	value = value || turbo_power_mode || turbo_test_mode;
#endif
	chr_info("%s value %d\n", __func__, value);
	return sprintf(buf, "%d\n", value);
}
static ssize_t turbo_power_mode_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t size)
{
	signed int temp;
	if (kstrtoint(buf, 10, &temp) == 0) {
		chr_info("%s %s turbo_power_mode\n", __func__, temp ? "enable" : "disable");
#if IS_ENABLED(CONFIG_OEM_TURBO_CHARGER)
		if (temp == 1)
			turbo_test_mode = 1;
		else
			turbo_test_mode = 0;
#endif
	} else
		chr_err("%s: format error!\n", __func__);
	return size;
}
static DEVICE_ATTR_RW(turbo_power_mode);
#endif
/* TN End modified by jirui.li/860702 20240814 CR/EKLAMU-30 */
/* TN Begin modified by jirui.li/860702 20240814 CR/EKLAMU-202 */
static ssize_t demo_mode_limit_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct mtk_charger *pinfo = dev->driver_data;
	chr_err("%s: %d\n", __func__, pinfo->demo_mode_limit);
	return sprintf(buf, "%d\n", pinfo->demo_mode_limit);
}
static ssize_t demo_mode_limit_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t size)
{
	struct mtk_charger *pinfo = dev->driver_data;
	signed int temp;
	if (kstrtoint(buf, 10, &temp) == 0) {
		chr_info("%s %s demo_mode_limit\n", __func__, temp ? "enable" : "disable");
		if (temp == 1)
			pinfo->demo_mode_limit = true;
		else
			pinfo->demo_mode_limit = false;
	} else
		chr_err("%s: format error!\n", __func__);
	return size;
}
static DEVICE_ATTR_RW(demo_mode_limit);
/* TN Begin modified by jirui.li/860702 20240814 CR/EKLAMU-202 */

#endif /* CONFIG_OEM_TINNO_CHARGER */
/* TN End modified by hao.jia/809321 20240718 CR/EKLAMU-202 */

/* procfs */
static int mtk_chg_set_cv_show(struct seq_file *m, void *data)
{
	struct mtk_charger *pinfo = m->private;

	seq_printf(m, "%d\n", pinfo->data.battery_cv);
	return 0;
}

static int mtk_chg_set_cv_open(struct inode *node, struct file *file)
{
	return single_open(file, mtk_chg_set_cv_show, pde_data(node));
}

static ssize_t mtk_chg_set_cv_write(struct file *file,
		const char *buffer, size_t count, loff_t *data)
{
	int len = 0, ret = 0;
	char desc[32] = {0};
	unsigned int cv = 0;
	struct mtk_charger *info = pde_data(file_inode(file));
	struct power_supply *psy = NULL;
	union  power_supply_propval dynamic_cv;

	if (!info)
		return -EINVAL;
	if (count <= 0)
		return -EINVAL;

	len = (count < (sizeof(desc) - 1)) ? count : (sizeof(desc) - 1);
	if (copy_from_user(desc, buffer, len))
		return -EFAULT;

	desc[len] = '\0';

	ret = kstrtou32(desc, 10, &cv);
	if (ret == 0) {
		if (cv >= BATTERY_CV) {
			info->data.battery_cv = BATTERY_CV;
			chr_info("%s: adjust charge voltage %dV too high, use default cv\n",
				  __func__, cv);
		} else {
			info->data.battery_cv = cv;
			chr_info("%s: adjust charge voltage = %dV\n", __func__, cv);
		}
		psy = power_supply_get_by_name("battery");
		if (!IS_ERR_OR_NULL(psy)) {
			dynamic_cv.intval = info->data.battery_cv;
			ret = power_supply_set_property(psy,
				POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE, &dynamic_cv);
			if (ret < 0)
				chr_err("set gauge cv fail\n");
		}
		return count;
	}

	chr_err("%s: bad argument\n", __func__);
	return count;
}

static const struct proc_ops mtk_chg_set_cv_fops = {
	.proc_open = mtk_chg_set_cv_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
	.proc_write = mtk_chg_set_cv_write,
};

static int mtk_chg_current_cmd_show(struct seq_file *m, void *data)
{
	struct mtk_charger *pinfo = m->private;

	seq_printf(m, "%d %d\n", pinfo->usb_unlimited, pinfo->cmd_discharging);
	return 0;
}

static int mtk_chg_current_cmd_open(struct inode *node, struct file *file)
{
	return single_open(file, mtk_chg_current_cmd_show, pde_data(node));
}

static ssize_t mtk_chg_current_cmd_write(struct file *file,
		const char *buffer, size_t count, loff_t *data)
{
	int len = 0;
	char desc[32] = {0};
	int current_unlimited = 0;
	int cmd_discharging = 0;
	struct mtk_charger *info = pde_data(file_inode(file));

	if (!info)
		return -EINVAL;
	if (count <= 0)
		return -EINVAL;

	len = (count < (sizeof(desc) - 1)) ? count : (sizeof(desc) - 1);
	if (copy_from_user(desc, buffer, len))
		return -EFAULT;

	desc[len] = '\0';

	if (sscanf(desc, "%d %d", &current_unlimited, &cmd_discharging) == 2) {
		info->usb_unlimited = current_unlimited;
		if (cmd_discharging == 1) {
			info->cmd_discharging = true;
			charger_dev_enable(info->chg1_dev, false);
			charger_dev_do_event(info->chg1_dev,
					EVENT_DISCHARGE, 0);
		} else if (cmd_discharging == 0) {
			info->cmd_discharging = false;
			charger_dev_enable(info->chg1_dev, true);
			charger_dev_do_event(info->chg1_dev,
					EVENT_RECHARGE, 0);
		}

		chr_info("%s: current_unlimited=%d, cmd_discharging=%d\n",
			__func__, current_unlimited, cmd_discharging);
		return count;
	}

	chr_err("bad argument, echo [usb_unlimited] [disable] > current_cmd\n");
	return count;
}

static const struct proc_ops mtk_chg_current_cmd_fops = {
	.proc_open = mtk_chg_current_cmd_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
	.proc_write = mtk_chg_current_cmd_write,
};

static int mtk_chg_en_power_path_show(struct seq_file *m, void *data)
{
	struct mtk_charger *pinfo = m->private;
	bool power_path_en = true;

	charger_dev_is_powerpath_enabled(pinfo->chg1_dev, &power_path_en);
	seq_printf(m, "%d\n", power_path_en);

	return 0;
}

static int mtk_chg_en_power_path_open(struct inode *node, struct file *file)
{
	return single_open(file, mtk_chg_en_power_path_show, pde_data(node));
}

static ssize_t mtk_chg_en_power_path_write(struct file *file,
		const char *buffer, size_t count, loff_t *data)
{
	int len = 0, ret = 0;
	char desc[32] = {0};
	unsigned int enable = 0;
	struct mtk_charger *info = pde_data(file_inode(file));

	if (!info)
		return -EINVAL;
	if (count <= 0)
		return -EINVAL;

	len = (count < (sizeof(desc) - 1)) ? count : (sizeof(desc) - 1);
	if (copy_from_user(desc, buffer, len))
		return -EFAULT;

	desc[len] = '\0';

	ret = kstrtou32(desc, 10, &enable);
	if (ret == 0) {
		charger_dev_enable_powerpath(info->chg1_dev, enable);
		chr_info("%s: enable power path = %d\n", __func__, enable);
		return count;
	}

	chr_err("bad argument, echo [enable] > en_power_path\n");
	return count;
}

static const struct proc_ops mtk_chg_en_power_path_fops = {
	.proc_open = mtk_chg_en_power_path_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
	.proc_write = mtk_chg_en_power_path_write,
};

static int mtk_chg_en_safety_timer_show(struct seq_file *m, void *data)
{
	struct mtk_charger *pinfo = m->private;
	bool safety_timer_en = false;

	charger_dev_is_safety_timer_enabled(pinfo->chg1_dev, &safety_timer_en);
	seq_printf(m, "%d\n", safety_timer_en);

	return 0;
}

static int mtk_chg_en_safety_timer_open(struct inode *node, struct file *file)
{
	return single_open(file, mtk_chg_en_safety_timer_show, pde_data(node));
}

static ssize_t mtk_chg_en_safety_timer_write(struct file *file,
	const char *buffer, size_t count, loff_t *data)
{
	int len = 0, ret = 0;
	char desc[32] = {0};
	unsigned int enable = 0;
	struct mtk_charger *info = pde_data(file_inode(file));

	if (!info)
		return -EINVAL;
	if (count <= 0)
		return -EINVAL;

	len = (count < (sizeof(desc) - 1)) ? count : (sizeof(desc) - 1);
	if (copy_from_user(desc, buffer, len))
		return -EFAULT;

	desc[len] = '\0';

	ret = kstrtou32(desc, 10, &enable);
	if (ret == 0) {
		charger_dev_enable_safety_timer(info->chg1_dev, enable);
		info->safety_timer_cmd = (int)enable;
		chr_info("%s: enable safety timer = %d\n", __func__, enable);

		/* SW safety timer */
		if (info->sw_safety_timer_setting == true) {
			if (enable)
				info->enable_sw_safety_timer = true;
			else
				info->enable_sw_safety_timer = false;
		}

		return count;
	}

	chr_err("bad argument, echo [enable] > en_safety_timer\n");
	return count;
}

static const struct proc_ops mtk_chg_en_safety_timer_fops = {
	.proc_open = mtk_chg_en_safety_timer_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
	.proc_write = mtk_chg_en_safety_timer_write,
};

int sc_get_sys_time(void)
{
	struct rtc_time tm_android = {0};
	struct timespec64 tv_android = {0};
	int timep = 0;

	ktime_get_real_ts64(&tv_android);
	rtc_time64_to_tm(tv_android.tv_sec, &tm_android);
	tv_android.tv_sec -= (uint64_t)sys_tz.tz_minuteswest * 60;
	rtc_time64_to_tm(tv_android.tv_sec, &tm_android);
	timep = tm_android.tm_sec + tm_android.tm_min * 60 + tm_android.tm_hour * 3600;

	return timep;
}

int sc_get_left_time(int s, int e, int now)
{
	if (e >= s) {
		if (now >= s && now < e)
			return e-now;
	} else {
		if (now >= s)
			return 86400 - now + e;
		else if (now < e)
			return e-now;
	}
	return 0;
}

char *sc_solToStr(int s)
{
	switch (s) {
	case SC_IGNORE:
		return "ignore";
	case SC_KEEP:
		return "keep";
	case SC_DISABLE:
		return "disable";
	case SC_REDUCE:
		return "reduce";
	default:
		return "none";
	}
}

int smart_charging(struct mtk_charger *info)
{
	int time_to_target = 0;
	int time_to_full_default_current = -1;
	int time_to_full_default_current_limit = -1;
	int ret_value = SC_KEEP;
	int sc_real_time = sc_get_sys_time();
	int sc_left_time = sc_get_left_time(info->sc.start_time, info->sc.end_time, sc_real_time);
	int sc_battery_percentage = get_uisoc(info) * 100;
	int sc_charger_current = get_battery_current(info);

	time_to_target = sc_left_time - info->sc.left_time_for_cv;

	if (info->sc.enable == false || sc_left_time <= 0
		|| sc_left_time < info->sc.left_time_for_cv
		|| (sc_charger_current <= 0 && info->sc.last_solution != SC_DISABLE))
		ret_value = SC_IGNORE;
	else {
		if (sc_battery_percentage > info->sc.target_percentage * 100) {
			if (time_to_target > 0)
				ret_value = SC_DISABLE;
		} else {
			if (sc_charger_current != 0)
				time_to_full_default_current =
					info->sc.battery_size * 3600 / 10000 *
					(10000 - sc_battery_percentage)
						/ sc_charger_current;
			else
				time_to_full_default_current =
					info->sc.battery_size * 3600 / 10000 *
					(10000 - sc_battery_percentage);
			chr_err("sc1: %d %d %d %d %d\n",
				time_to_full_default_current,
				info->sc.battery_size,
				sc_battery_percentage,
				sc_charger_current,
				info->sc.current_limit);

			if (time_to_full_default_current < time_to_target &&
				info->sc.current_limit != -1 &&
				sc_charger_current > info->sc.current_limit) {
				time_to_full_default_current_limit =
					info->sc.battery_size / 10000 *
					(10000 - sc_battery_percentage)
					/ info->sc.current_limit;

				chr_err("sc2: %d %d %d %d\n",
					time_to_full_default_current_limit,
					info->sc.battery_size,
					sc_battery_percentage,
					info->sc.current_limit);

				if (time_to_full_default_current_limit < time_to_target &&
					sc_charger_current > info->sc.current_limit)
					ret_value = SC_REDUCE;
			}
		}
	}
	info->sc.last_solution = ret_value;
	if (info->sc.last_solution == SC_DISABLE)
		info->sc.disable_charger = true;
	else
		info->sc.disable_charger = false;
	chr_err("[sc]disable_charger: %d\n", info->sc.disable_charger);
	chr_err("[sc1]en:%d t:%d,%d,%d,%d t:%d,%d,%d,%d c:%d,%d ibus:%d uisoc: %d,%d s:%d ans:%s\n",
		info->sc.enable, info->sc.start_time, info->sc.end_time,
		sc_real_time, sc_left_time, info->sc.left_time_for_cv,
		time_to_target, time_to_full_default_current, time_to_full_default_current_limit,
		sc_charger_current, info->sc.current_limit,
		get_ibus(info), get_uisoc(info), info->sc.target_percentage,
		info->sc.battery_size, sc_solToStr(info->sc.last_solution));

	return ret_value;
}

void sc_select_charging_current(struct mtk_charger *info, struct charger_data *pdata)
{
	if (info->bootmode == 4 || info->bootmode == 1
		|| info->bootmode == 8 || info->bootmode == 9) {
		info->sc.sc_ibat = -1;	/* not normal boot */
		return;
	}
	info->sc.solution = info->sc.last_solution;
	chr_debug("debug: %d, %d, %d\n", info->bootmode,
		info->sc.disable_in_this_plug, info->sc.solution);
	if (info->sc.disable_in_this_plug == false) {
		chr_debug("sck: %d %d %d %d %d\n",
			info->sc.pre_ibat,
			info->sc.sc_ibat,
			pdata->charging_current_limit,
			pdata->thermal_charging_current_limit,
			info->sc.solution);
		if (info->sc.pre_ibat == -1 || info->sc.solution == SC_IGNORE
			|| info->sc.solution == SC_DISABLE) {
			info->sc.sc_ibat = -1;
		} else {
			if (info->sc.pre_ibat == pdata->charging_current_limit
				&& info->sc.solution == SC_REDUCE
				&& ((pdata->charging_current_limit - 100000) >= 500000)) {
				if (info->sc.sc_ibat == -1)
					info->sc.sc_ibat = pdata->charging_current_limit - 100000;

				else {
					if (info->sc.sc_ibat - 100000 >= 500000)
						info->sc.sc_ibat = info->sc.sc_ibat - 100000;
					else
						info->sc.sc_ibat = 500000;
				}
			}
		}
	}
	info->sc.pre_ibat = pdata->charging_current_limit;

	if (pdata->thermal_charging_current_limit != -1) {
		if (pdata->thermal_charging_current_limit <
		    pdata->charging_current_limit)
			pdata->charging_current_limit =
					pdata->thermal_charging_current_limit;
		info->sc.disable_in_this_plug = true;
	} else if ((info->sc.solution == SC_REDUCE || info->sc.solution == SC_KEEP)
		&& info->sc.sc_ibat <
		pdata->charging_current_limit &&
		info->sc.disable_in_this_plug == false) {
		pdata->charging_current_limit = info->sc.sc_ibat;
	}
}

void sc_init(struct smartcharging *sc)
{
	sc->enable = false;
	sc->battery_size = 3000;
	sc->start_time = 0;
	sc->end_time = 80000;
	sc->current_limit = 2000;
	sc->target_percentage = 80;
	sc->left_time_for_cv = 3600;
	sc->pre_ibat = -1;
}

static ssize_t enable_sc_show(
	struct device *dev, struct device_attribute *attr,
					char *buf)
{
	struct power_supply *chg_psy = NULL;
	struct mtk_charger *info = NULL;

	chg_psy = power_supply_get_by_name("mtk-master-charger");
	if (chg_psy == NULL || IS_ERR(chg_psy)) {
		chr_err("%s Couldn't get chg_psy\n", __func__);
		return -EINVAL;
	}
	info = (struct mtk_charger *)power_supply_get_drvdata(chg_psy);
	if (info == NULL)
		return -EINVAL;

	chr_err(
	"[enable smartcharging] : %d\n",
	info->sc.enable);

	return sprintf(buf, "%d\n", info->sc.enable);
}

static ssize_t enable_sc_store(
	struct device *dev, struct device_attribute *attr,
					 const char *buf, size_t size)
{
	unsigned long val = 0;
	int ret;
	struct power_supply *chg_psy = NULL;
	struct mtk_charger *info = NULL;

	chg_psy = power_supply_get_by_name("mtk-master-charger");
	if (chg_psy == NULL || IS_ERR(chg_psy)) {
		chr_err("%s Couldn't get chg_psy\n", __func__);
		return -EINVAL;
	}
	info = (struct mtk_charger *)power_supply_get_drvdata(chg_psy);
	if (info == NULL)
		return -EINVAL;

	if (buf != NULL && size != 0) {
		chr_err("[enable smartcharging] buf is %s\n", buf);
		ret = kstrtoul(buf, 10, &val);
		if (ret == -ERANGE || ret == -EINVAL)
			return -EINVAL;
		if (val == 0)
			info->sc.enable = false;
		else
			info->sc.enable = true;

		chr_err(
			"[enable smartcharging]enable smartcharging=%d\n",
			info->sc.enable);
	}
	return size;
}
static DEVICE_ATTR_RW(enable_sc);

static ssize_t sc_stime_show(
	struct device *dev, struct device_attribute *attr,
					char *buf)
{
	struct power_supply *chg_psy = NULL;
	struct mtk_charger *info = NULL;

	chg_psy = power_supply_get_by_name("mtk-master-charger");
	if (chg_psy == NULL || IS_ERR(chg_psy)) {
		chr_err("%s Couldn't get chg_psy\n", __func__);
		return -EINVAL;
	}
	info = (struct mtk_charger *)power_supply_get_drvdata(chg_psy);
	if (info == NULL)
		return -EINVAL;

	chr_err(
	"[smartcharging stime] : %d\n",
	info->sc.start_time);

	return sprintf(buf, "%d\n", info->sc.start_time);
}

static ssize_t sc_stime_store(
	struct device *dev, struct device_attribute *attr,
					 const char *buf, size_t size)
{
	long val = 0;
	int ret;
	struct power_supply *chg_psy = NULL;
	struct mtk_charger *info = NULL;

	chg_psy = power_supply_get_by_name("mtk-master-charger");
	if (chg_psy == NULL || IS_ERR(chg_psy)) {
		chr_err("%s Couldn't get chg_psy\n", __func__);
		return -EINVAL;
	}
	info = (struct mtk_charger *)power_supply_get_drvdata(chg_psy);
	if (info == NULL)
		return -EINVAL;

	if (buf != NULL && size != 0) {
		chr_err("[smartcharging stime] buf is %s\n", buf);
		ret = kstrtol(buf, 10, &val);
		if (ret == -ERANGE || ret == -EINVAL)
			return -EINVAL;
		if (val < 0) {
			chr_err(
				"[smartcharging stime] val is %ld ??\n",
				val);
			val = 0;
		}

		if (val >= 0)
			info->sc.start_time = (int)val;

		chr_err(
			"[smartcharging stime]enable smartcharging=%d\n",
			info->sc.start_time);
	}
	return size;
}
static DEVICE_ATTR_RW(sc_stime);

static ssize_t sc_etime_show(
	struct device *dev, struct device_attribute *attr,
					char *buf)
{
	struct power_supply *chg_psy = NULL;
	struct mtk_charger *info = NULL;

	chg_psy = power_supply_get_by_name("mtk-master-charger");
	if (chg_psy == NULL || IS_ERR(chg_psy)) {
		chr_err("%s Couldn't get chg_psy\n", __func__);
		return -EINVAL;
	}
	info = (struct mtk_charger *)power_supply_get_drvdata(chg_psy);
	if (info == NULL)
		return -EINVAL;

	chr_err(
	"[smartcharging etime] : %d\n",
	info->sc.end_time);

	return sprintf(buf, "%d\n", info->sc.end_time);
}

static ssize_t sc_etime_store(
	struct device *dev, struct device_attribute *attr,
					 const char *buf, size_t size)
{
	long val = 0;
	int ret;
	struct power_supply *chg_psy = NULL;
	struct mtk_charger *info = NULL;

	chg_psy = power_supply_get_by_name("mtk-master-charger");
	if (chg_psy == NULL || IS_ERR(chg_psy)) {
		chr_err("%s Couldn't get chg_psy\n", __func__);
		return -EINVAL;
	}
	info = (struct mtk_charger *)power_supply_get_drvdata(chg_psy);
	if (info == NULL)
		return -EINVAL;

	if (buf != NULL && size != 0) {
		chr_err("[smartcharging etime] buf is %s\n", buf);
		ret = kstrtol(buf, 10, &val);
		if (ret == -ERANGE || ret == -EINVAL)
			return -EINVAL;
		if (val < 0) {
			chr_err(
				"[smartcharging etime] val is %ld ??\n",
				val);
			val = 0;
		}

		if (val >= 0)
			info->sc.end_time = (int)val;

		chr_err(
			"[smartcharging stime]enable smartcharging=%d\n",
			info->sc.end_time);
	}
	return size;
}
static DEVICE_ATTR_RW(sc_etime);

static ssize_t sc_tuisoc_show(
	struct device *dev, struct device_attribute *attr,
					char *buf)
{
	struct power_supply *chg_psy = NULL;
	struct mtk_charger *info = NULL;

	chg_psy = power_supply_get_by_name("mtk-master-charger");
	if (chg_psy == NULL || IS_ERR(chg_psy)) {
		chr_err("%s Couldn't get chg_psy\n", __func__);
		return -EINVAL;
	}
	info = (struct mtk_charger *)power_supply_get_drvdata(chg_psy);
	if (info == NULL)
		return -EINVAL;

	chr_err(
	"[smartcharging target uisoc] : %d\n",
	info->sc.target_percentage);

	return sprintf(buf, "%d\n", info->sc.target_percentage);
}

static ssize_t sc_tuisoc_store(
	struct device *dev, struct device_attribute *attr,
					 const char *buf, size_t size)
{
	long val = 0;
	int ret;
	struct power_supply *chg_psy = NULL;
	struct mtk_charger *info = NULL;

	chg_psy = power_supply_get_by_name("mtk-master-charger");
	if (chg_psy == NULL || IS_ERR(chg_psy)) {
		chr_err("%s Couldn't get chg_psy\n", __func__);
		return -EINVAL;
	}
	info = (struct mtk_charger *)power_supply_get_drvdata(chg_psy);
	if (info == NULL)
		return -EINVAL;

	if (buf != NULL && size != 0) {
		chr_err("[smartcharging tuisoc] buf is %s\n", buf);
		ret = kstrtol(buf, 10, &val);
		if (ret == -ERANGE || ret == -EINVAL)
			return -EINVAL;
		if (val < 0) {
			chr_err(
				"[smartcharging tuisoc] val is %ld ??\n",
				val);
			val = 0;
		}

		if (val >= 0)
			info->sc.target_percentage = (int)val;

		chr_err(
			"[smartcharging stime]tuisoc=%d\n",
			info->sc.target_percentage);
	}
	return size;
}
static DEVICE_ATTR_RW(sc_tuisoc);

static ssize_t sc_ibat_limit_show(
	struct device *dev, struct device_attribute *attr,
					char *buf)
{
	struct power_supply *chg_psy = NULL;
	struct mtk_charger *info = NULL;

	chg_psy = power_supply_get_by_name("mtk-master-charger");
	if (chg_psy == NULL || IS_ERR(chg_psy)) {
		chr_err("%s Couldn't get chg_psy\n", __func__);
		return -EINVAL;
	}
	info = (struct mtk_charger *)power_supply_get_drvdata(chg_psy);
	if (info == NULL)
		return -EINVAL;

	chr_err(
	"[smartcharging ibat limit] : %d\n",
	info->sc.current_limit);

	return sprintf(buf, "%d\n", info->sc.current_limit);
}

static ssize_t sc_ibat_limit_store(
	struct device *dev, struct device_attribute *attr,
					 const char *buf, size_t size)
{
	long val = 0;
	int ret;
	struct power_supply *chg_psy = NULL;
	struct mtk_charger *info = NULL;

	chg_psy = power_supply_get_by_name("mtk-master-charger");
	if (chg_psy == NULL || IS_ERR(chg_psy)) {
		chr_err("%s Couldn't get chg_psy\n", __func__);
		return -EINVAL;
	}
	info = (struct mtk_charger *)power_supply_get_drvdata(chg_psy);
	if (info == NULL)
		return -EINVAL;

	if (buf != NULL && size != 0) {
		chr_err("[smartcharging ibat limit] buf is %s\n", buf);
		ret = kstrtol(buf, 10, &val);
		if (ret == -ERANGE || ret == -EINVAL)
			return -EINVAL;
		if (val < 0) {
			chr_err(
				"[smartcharging ibat limit] val is %ld ??\n",
				val);
			val = 0;
		}

		if (val >= 0)
			info->sc.current_limit = (int)val;

		chr_err(
			"[smartcharging ibat limit]=%d\n",
			info->sc.current_limit);
	}
	return size;
}
static DEVICE_ATTR_RW(sc_ibat_limit);

static ssize_t enable_power_path_show(
	struct device *dev, struct device_attribute *attr,
					char *buf)
{
	struct power_supply *chg_psy = NULL;
	struct mtk_charger *info = NULL;
	bool power_path_en = true;

	chg_psy = power_supply_get_by_name("mtk-master-charger");
	if (chg_psy == NULL || IS_ERR(chg_psy)) {
		chr_err("%s Couldn't get chg_psy\n", __func__);
		return -EINVAL;
	}
	info = (struct mtk_charger *)power_supply_get_drvdata(chg_psy);
	if (info == NULL)
		return -EINVAL;

	charger_dev_is_powerpath_enabled(info->chg1_dev, &power_path_en);
	return sprintf(buf, "%d\n", power_path_en);
}

static ssize_t enable_power_path_store(
	struct device *dev, struct device_attribute *attr,
					 const char *buf, size_t size)
{
	long val = 0;
	int ret;
	bool enable = true;
	struct power_supply *chg_psy = NULL;
	struct mtk_charger *info = NULL;

	chg_psy = power_supply_get_by_name("mtk-master-charger");
	if (chg_psy == NULL || IS_ERR(chg_psy)) {
		chr_err("%s Couldn't get chg_psy\n", __func__);
		return -EINVAL;
	}
	info = (struct mtk_charger *)power_supply_get_drvdata(chg_psy);
	if (info == NULL)
		return -EINVAL;

	if (buf != NULL && size != 0) {
		ret = kstrtoul(buf, 10, &val);
		if (ret == -ERANGE || ret == -EINVAL)
			return -EINVAL;
		if (val == 0)
			enable = false;
		else
			enable = true;

		charger_dev_enable_powerpath(info->chg1_dev, enable);
		info->cmd_pp = enable;
		chr_err("%s: enable power path = %d\n", __func__, enable);
	}

	return size;
}
static DEVICE_ATTR_RW(enable_power_path);

int mtk_chg_enable_vbus_ovp(bool enable)
{
	static struct mtk_charger *pinfo;
	int ret = 0;
	u32 sw_ovp = 0;
	struct power_supply *psy;

	if (pinfo == NULL) {
		psy = power_supply_get_by_name("mtk-master-charger");
		if (psy == NULL) {
			chr_err("[%s]psy is not rdy\n", __func__);
			return -1;
		}

		pinfo = (struct mtk_charger *)power_supply_get_drvdata(psy);
		if (pinfo == NULL) {
			chr_err("[%s]mtk_gauge is not rdy\n", __func__);
			return -1;
		}
	}

	if (enable)
		sw_ovp = pinfo->data.max_charger_voltage_setting;
	else
		sw_ovp = pinfo->data.vbus_sw_ovp_voltage;

	/* Enable/Disable SW OVP status */
	pinfo->data.max_charger_voltage = sw_ovp;

	disable_hw_ovp(pinfo, enable);

	chr_err("[%s] en:%d ovp:%d\n",
			    __func__, enable, sw_ovp);
	return ret;
}
EXPORT_SYMBOL(mtk_chg_enable_vbus_ovp);

/* return false if vbus is over max_charger_voltage */
static bool mtk_chg_check_vbus(struct mtk_charger *info)
{
	int vchr = 0;

	vchr = get_vbus(info) * 1000;
	if (vchr > info->data.max_charger_voltage) {
		chr_err("%s: vbus(%d mV) > %d mV\n", __func__, vchr / 1000,
			info->data.max_charger_voltage / 1000);
		return false;
	}
	return true;
}

static void mtk_battery_notify_VCharger_check(struct mtk_charger *info)
{
#if defined(BATTERY_NOTIFY_CASE_0001_VCHARGER)
	int vchr = 0;

	vchr = get_vbus(info) * 1000; /* uV */
	if (vchr < info->data.max_charger_voltage)
		info->notify_code &= ~CHG_VBUS_OV_STATUS;
	else {
		info->notify_code |= CHG_VBUS_OV_STATUS;
		chr_err("[BATTERY] charger_vol(%d mV) > %d mV\n",
			vchr / 1000, info->data.max_charger_voltage / 1000);
		mtk_chgstat_notify(info);
	}
#endif
}

static void mtk_battery_notify_VBatTemp_check(struct mtk_charger *info)
{
#if defined(BATTERY_NOTIFY_CASE_0002_VBATTEMP)
	if (info->battery_temp >= info->thermal.max_charge_temp) {
		info->notify_code |= CHG_BAT_OT_STATUS;
		chr_err("[BATTERY] bat_temp(%d) out of range(too high)\n",
			info->battery_temp);
		mtk_chgstat_notify(info);
	} else {
		info->notify_code &= ~CHG_BAT_OT_STATUS;
	}

	if (info->enable_sw_jeita == true) {
		if (info->battery_temp < info->data.temp_neg_10_thres) {
			info->notify_code |= CHG_BAT_LT_STATUS;
			chr_err("bat_temp(%d) out of range(too low)\n",
				info->battery_temp);
			mtk_chgstat_notify(info);
		} else {
			info->notify_code &= ~CHG_BAT_LT_STATUS;
		}
	} else {
#ifdef BAT_LOW_TEMP_PROTECT_ENABLE
		if (info->battery_temp < info->thermal.min_charge_temp) {
			info->notify_code |= CHG_BAT_LT_STATUS;
			chr_err("bat_temp(%d) out of range(too low)\n",
				info->battery_temp);
			mtk_chgstat_notify(info);
		} else {
			info->notify_code &= ~CHG_BAT_LT_STATUS;
		}
#endif
	}
#endif
}

static void mtk_battery_notify_VChargerDPDM_check(struct mtk_charger *info)
{
	if (!info->dpdmov_stat)
		info->notify_code &= ~CHG_DPDM_OV_STATUS;
	else {
		info->notify_code |= CHG_DPDM_OV_STATUS;
		chr_err("[BATTERY] DP/DM over voltage!\n");
	}
	if (info->dpdmov_stat != info->lst_dpdmov_stat) {
		mtk_chgstat_notify(info);
		info->lst_dpdmov_stat = info->dpdmov_stat;
	}
}

static void mtk_battery_notify_UI_test(struct mtk_charger *info)
{
	switch (info->notify_test_mode) {
	case 1:
		info->notify_code = CHG_VBUS_OV_STATUS;
		chr_debug("[%s] CASE_0001_VCHARGER\n", __func__);
		break;
	case 2:
		info->notify_code = CHG_BAT_OT_STATUS;
		chr_debug("[%s] CASE_0002_VBATTEMP\n", __func__);
		break;
	case 3:
		info->notify_code = CHG_OC_STATUS;
		chr_debug("[%s] CASE_0003_ICHARGING\n", __func__);
		break;
	case 4:
		info->notify_code = CHG_BAT_OV_STATUS;
		chr_debug("[%s] CASE_0004_VBAT\n", __func__);
		break;
	case 5:
		info->notify_code = CHG_ST_TMO_STATUS;
		chr_debug("[%s] CASE_0005_TOTAL_CHARGINGTIME\n", __func__);
		break;
	case 6:
		info->notify_code = CHG_BAT_LT_STATUS;
		chr_debug("[%s] CASE6: VBATTEMP_LOW\n", __func__);
		break;
	case 7:
		info->notify_code = CHG_TYPEC_WD_STATUS;
		chr_debug("[%s] CASE7: Moisture Detection\n", __func__);
		break;
	default:
		chr_debug("[%s] Unknown BN_TestMode Code: %x\n",
			__func__, info->notify_test_mode);
	}
	mtk_chgstat_notify(info);
}

static void mtk_battery_notify_check(struct mtk_charger *info)
{
	if (info->notify_test_mode == 0x0000) {
		mtk_battery_notify_VCharger_check(info);
		mtk_battery_notify_VBatTemp_check(info);
		mtk_battery_notify_VChargerDPDM_check(info);
	} else {
		mtk_battery_notify_UI_test(info);
	}
}

/* TN Begin modified by xinjun.lu/860715 20240729 CR/EKLAMU-202 */
#if IS_ENABLED(CONFIG_PE50_FFC_SUPPORT)
int pe50_get_prop_from_battery(struct mtk_charger *info,
				enum power_supply_property psp,
				union power_supply_propval *val)
{
	int rc;

	if (!info->bat_psy) {
		info->bat_psy = power_supply_get_by_name("battery");

		if (!info->bat_psy) {
			chr_err("[%s]Error getting battery power sypply\n", __func__);
			return -EINVAL;
		}
	}

	rc = power_supply_get_property(info->bat_psy, psp, val);

	return rc;
}

int pe50_set_prop_to_battery(struct mtk_charger *info,
				enum power_supply_property psp,
				union power_supply_propval *val)
{
	int rc;

	if (!info->bat_psy) {
		info->bat_psy = power_supply_get_by_name("battery");

		if (!info->bat_psy) {
			chr_err("[%s]Error getting battery power sypply\n", __func__);
			return -EINVAL;
		}
	}

	rc = power_supply_set_property(info->bat_psy, psp, val);

	return rc;
}

int pe50_get_prop_from_charger(struct mtk_charger *info,
				enum power_supply_property psp,
				union power_supply_propval *val)
{
	int rc;
	struct power_supply *chg_psy = NULL;

	chg_psy = power_supply_get_by_name("mtk-master-charger");
	if (chg_psy == NULL || IS_ERR(chg_psy)) {
		chr_err("%s Couldn't get chg_psy\n", __func__);
		return -EINVAL;
	}

	rc = power_supply_get_property(chg_psy, psp, val);

	return rc;
}

static int parse_pe50_dt(struct mtk_charger *info, struct device *dev)
{
	struct device_node *node = dev->of_node;
	int rc = 0;
	int byte_len;
	int i;

	if (!node) {
		chr_err("[%s]pe50 dtree info. missing\n",__func__);
		return -ENODEV;
	}
#if 0
	if (of_find_property(node, "pe50,pe50-cycle-cv-steps", &byte_len)) {
		if ((byte_len / sizeof(u32)) % 2) {
			chr_err("[%s]DT error wrong pe50 cycle batt_cv zones, byte_len = %d\n",
				__func__, byte_len);
			return -ENODEV;
		}

		info->pe50.cycle_cv_steps = (struct pe50_cycle_cv_steps *)
			devm_kzalloc(dev, byte_len, GFP_KERNEL);

		if (info->pe50.cycle_cv_steps == NULL)
			return -ENOMEM;

		info->pe50.num_cycle_cv_steps =
			byte_len / sizeof(struct pe50_cycle_cv_steps);

		rc = of_property_read_u32_array(node,
				"pe50,pe50-cycle-cv-steps",
				(u32 *)info->pe50.cycle_cv_steps,
				byte_len / sizeof(u32));
		if (rc < 0) {
			chr_err("[%s]Couldn't read pe50 cycle cv steps rc = %d\n", __func__, rc);
			return rc;
		}
		chr_info("[%s]pe50 cycle cv steps: Num: %d\n",
				__func__,
				info->pe50.num_cycle_cv_steps);
		for (i = 0; i < info->pe50.num_cycle_cv_steps; i++) {
			chr_info("[%s]pe50 cycle cv steps: cycle > %d, delta_cv_mv = %d mV\n",
				__func__,
				info->pe50.cycle_cv_steps[i].cycle,
				info->pe50.cycle_cv_steps[i].delta_cv_mv);
		}
	} else {
		info->pe50.cycle_cv_steps = NULL;
		info->pe50.num_cycle_cv_steps = 0;
		chr_err("[%s]pe50 cycle cv steps is not set\n", __func__);
	}
#endif

	if (of_find_property(node, "pe50,pe50-temp-zones", &byte_len)) {
		if ((byte_len / sizeof(u32)) % 4) {
			chr_err("[%s]DT error wrong pe50 temp zones\n",__func__);
			return -ENODEV;
		}

		info->pe50.temp_zones = (struct pe50_temp_zone *)
			devm_kzalloc(dev, byte_len, GFP_KERNEL);

		if (info->pe50.temp_zones == NULL)
			return -ENOMEM;

		info->pe50.num_temp_zones =
			byte_len / sizeof(struct pe50_temp_zone);

		rc = of_property_read_u32_array(node,
				"pe50,pe50-temp-zones",
				(u32 *)info->pe50.temp_zones,
				byte_len / sizeof(u32));
		if (rc < 0) {
			chr_err("[%s]Couldn't read pe50 temp zones rc = %d\n", __func__, rc);
			return rc;
		}
		chr_info("[%s]"
			"pe50 temp zones: Num: %d\n", __func__, info->pe50.num_temp_zones);
		for (i = 0; i < info->pe50.num_temp_zones; i++) {
			chr_info("[%s]"
				"pe50 temp zones: Zone %d, Temp %d C, "
				"Step Volt %d mV, Full Rate %d mA, "
				"Taper Rate %d mA\n", __func__, i,
				info->pe50.temp_zones[i].temp_c,
				info->pe50.temp_zones[i].norm_mv,
				info->pe50.temp_zones[i].fcc_max_ma,
				info->pe50.temp_zones[i].fcc_norm_ma);
		}
		info->pe50.pres_temp_zone = ZONE_NONE_PE50;
	} else {
		info->pe50.temp_zones = NULL;
		info->pe50.num_temp_zones = 0;
		chr_err("[%s]pe50 temp zones is not set\n", __func__);
	}

	if (of_find_property(node, "pe50,pe50-ffc-zones", &byte_len)) {
		if ((byte_len / sizeof(u32)) % 3) {
			chr_err("DT error wrong pe50 ffc zones\n");
			return -ENODEV;
		}

		info->pe50.ffc_zones = (struct pe50_ffc_zone *)
			devm_kzalloc(dev, byte_len, GFP_KERNEL);

		info->pe50.num_ffc_zones =
			byte_len / sizeof(struct pe50_ffc_zone);

		if (info->pe50.ffc_zones == NULL)
			return -ENOMEM;

		rc = of_property_read_u32_array(node,
				"pe50,pe50-ffc-zones",
				(u32 *)info->pe50.ffc_zones,
				byte_len / sizeof(u32));
		if (rc < 0) {
			chr_err("Couldn't read pe50 ffc zones rc = %d\n", rc);
			return rc;
		}

		for (i = 0; i < info->pe50.num_ffc_zones; i++) {
			chr_err("FFC:Zone %d,Temp %d,Volt %d,Ich %d", i,
				 info->pe50.ffc_zones[i].temp,
				 info->pe50.ffc_zones[i].ffc_max_mv,
				 info->pe50.ffc_zones[i].ffc_chg_iterm);
		}
	} else
		info->pe50.ffc_zones = NULL;

	rc = of_property_read_u32(node, "pe50,iterm-ma",
				  &info->pe50.chrg_iterm);
	if (rc)
		info->pe50.chrg_iterm = 150;
	 info->pe50.back_chrg_iterm = info->pe50.chrg_iterm;

	info->pe50.enable_mux =
		of_property_read_bool(node, "pe50,enable-mux");

	info->pe50.wls_switch_en = of_get_named_gpio(node, "pe50,mux_wls_switch_en", 0);
	if(!gpio_is_valid(info->pe50.wls_switch_en))
		chr_err("pe50 wls_switch_en is %d invalid\n", info->pe50.wls_switch_en );

	info->pe50.wls_boost_en = of_get_named_gpio(node, "pe50,mux_wls_boost_en", 0);
	if(!gpio_is_valid(info->pe50.wls_boost_en))
		chr_err("pe50 wls_boost_en is %d invalid\n", info->pe50.wls_boost_en);

	info->pe50.enable_charging_limit =
		of_property_read_bool(node, "pe50,enable-charging-limit");

	rc = of_property_read_u32(node, "pe50,upper-limit-capacity",
				  &info->pe50.upper_limit_capacity);
	if (rc)
		info->pe50.upper_limit_capacity = 100;

	rc = of_property_read_u32(node, "pe50,lower-limit-capacity",
				  &info->pe50.lower_limit_capacity);
	if (rc)
		info->pe50.lower_limit_capacity = 0;

	rc = of_property_read_u32(node, "pe50,vfloat-comp-uv",
				  &info->pe50.vfloat_comp_mv);
	if (rc)
		info->pe50.vfloat_comp_mv = 0;
	info->pe50.vfloat_comp_mv /= 1000;

	rc = of_property_read_u32(node, "pe50,min-cp-therm-current-ua",
				  &info->pe50.min_therm_current_limit);
	if (rc)
		info->pe50.min_therm_current_limit = 2000000;

	rc = of_property_read_u32(node, "pe50,typec-rp-max-current",
				  &info->pe50.typec_rp_max_current);
	if (rc)
		info->pe50.typec_rp_max_current = 0;

	rc = of_property_read_u32(node, "pe50,pd-pmax-mw",
				  &info->pe50.pd_pmax_mw);
	if (rc)
		info->pe50.pd_pmax_mw = 30000;

	rc = of_property_read_u32(node, "pe50,pd_vbus_upper_bound",
				  &info->pe50.vbus_h);
	if (rc)
		info->pe50.vbus_h = 9000000;

	rc = of_property_read_u32(node, "pe50,pd_vbus_low_bound",
				  &info->pe50.vbus_l);
	if (rc)
		info->pe50.vbus_l = 5000000;
#if 0
	rc = of_property_read_u32(node, "pe50,typec-ntc-pull-up-r",
				  &info->pe50.typec_ntc_pull_up_r);
	if (rc)
		info->pe50.typec_ntc_pull_up_r = 0;

	if (of_find_property(node, "pe50,typec-ntc-table", &byte_len)) {
		if ((byte_len / sizeof(u32)) % 2) {
			chr_err("[%s]DT error wrong pe50 typec ntc table, byte_len = %d\n",
				__func__, byte_len);
			return -ENODEV;
		}

		info->pe50.typec_ntc_table = (struct ntc_temp *)
			devm_kzalloc(dev, byte_len, GFP_KERNEL);

		if (info->pe50.typec_ntc_table == NULL)
			return -ENOMEM;

		info->pe50.num_typec_ntc_table =
			byte_len / sizeof(struct ntc_temp);

		rc = of_property_read_u32_array(node,
				"pe50,typec-ntc-table",
				(u32 *)info->pe50.typec_ntc_table,
				byte_len / sizeof(u32));
		if (rc < 0) {
			chr_err("[%s]Couldn't read pe50 typec ntc table rc = %d\n", __func__, rc);
			return rc;
		}
		chr_info("[%s]pe50 typec ntc table: Num: %d\n",
				__func__,
				info->pe50.num_typec_ntc_table);
		for (i = 0; i < info->pe50.num_typec_ntc_table; i++) {
			chr_info("[%s]pe50 typec ntc table: Temp: %d, Res: %d \n",
				__func__,
				info->pe50.typec_ntc_table[i].Temp,
				info->pe50.typec_ntc_table[i].TemperatureR);
		}
	} else {
		info->pe50.typec_ntc_table = NULL;
		info->pe50.num_typec_ntc_table = 0;
		chr_err("[%s]pe50 typec ntc table is not set\n", __func__);
	}
#endif

	return rc;
}

static int chg_reboot(struct notifier_block *nb,
			 unsigned long event, void *unused)
{
	struct mtk_charger *info = container_of(nb, struct mtk_charger,
						pe50.chg_reboot);
	union power_supply_propval val;
	int rc;

	chr_info("chg Reboot\n");
	if (!info) {
		chr_info("called before chip valid!\n");
		return NOTIFY_DONE;
	}

	if (info->pe50.factory_mode) {
		switch (event) {
		case SYS_POWER_OFF:
		//	aee_kernel_RT_Monitor_api_factory();
			info->is_suspend = true;
			/* Disable Factory Kill */
			info->disable_charger = true;
			/* Disable Charging */
			charger_dev_enable(info->chg1_dev, false);
			/* Suspend USB */
			charger_dev_enable_hz(info->chg1_dev, true);

			rc = pe50_get_prop_from_charger(info,
				POWER_SUPPLY_PROP_ONLINE, &val);
			while (rc >= 0 && val.intval) {
				msleep(100);
				rc = pe50_get_prop_from_charger(info,
					POWER_SUPPLY_PROP_ONLINE, &val);
				chr_info("Wait for VBUS to decay\n");
			}

			chr_info("VBUS UV wait 1 sec!\n");
			/* Delay 1 sec to allow more VBUS decay */
			msleep(1000);
			break;
		default:
			break;
		}
	}

	return NOTIFY_DONE;
}

static ssize_t factory_image_mode_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	unsigned long r;
	unsigned long mode;

	r = kstrtoul(buf, 0, &mode);
	if (r) {
		chr_err("[%s]Invalid factory image mode value = %lu\n", __func__, mode);
		return -EINVAL;
	}

	if (!pe50_info) {
		chr_err("[%s]pe50_info not valid\n", __func__);
		return -ENODEV;
	}

	pe50_info->pe50.is_factory_image = (mode) ? true : false;

	return r ? r : count;
}

static ssize_t factory_image_mode_show(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	int state;

	if (!pe50_info) {
		chr_err("[%s]pe50_info not valid\n", __func__);
		return -ENODEV;
	}

	state = (pe50_info->pe50.is_factory_image) ? 1 : 0;

	return scnprintf(buf, CHG_SHOW_MAX_SIZE, "%d\n", state);
}

static DEVICE_ATTR(factory_image_mode, 0644,
		factory_image_mode_show,
		factory_image_mode_store);

static ssize_t factory_charge_upper_show(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	int state;

	if (!pe50_info) {
		chr_err("[%s]pe50_info not valid\n", __func__);
		return -ENODEV;
	}

	state = pe50_info->pe50.upper_limit_capacity;

	return scnprintf(buf, CHG_SHOW_MAX_SIZE, "%d\n", state);
}

static DEVICE_ATTR(factory_charge_upper, 0444,
		factory_charge_upper_show,
		NULL);

static ssize_t force_demo_mode_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	unsigned long r;
	unsigned long mode;

	r = kstrtoul(buf, 0, &mode);
	if (r) {
		chr_err("[%s]Invalid demo  mode value = %lu\n", __func__, mode);
		return -EINVAL;
	}

	if (!pe50_info) {
		chr_err("[%s]pe50_info not valid\n", __func__);
		return -ENODEV;
	}
	pe50_info->pe50.chrg_taper_cnt = 0;

	if ((mode >= 35) && (mode <= 80))
		pe50_info->pe50.demo_mode = mode;
	else
		pe50_info->pe50.demo_mode = 35;

	return r ? r : count;
}

static ssize_t force_demo_mode_show(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	int state;

	if (!pe50_info) {
		chr_err("[%s]pe50_info not valid\n", __func__);
		return -ENODEV;
	}

	state = pe50_info->pe50.demo_mode;

	return scnprintf(buf, CHG_SHOW_MAX_SIZE, "%d\n", state);
}

static DEVICE_ATTR(force_demo_mode, 0644,
		force_demo_mode_show,
		force_demo_mode_store);

static ssize_t force_max_chrg_temp_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	unsigned long r;
	unsigned long mode;

	r = kstrtoul(buf, 0, &mode);
	if (r) {
		chr_err("[%s]Invalid max temp value = %lu\n", __func__, mode);
		return -EINVAL;
	}

	if (!pe50_info) {
		chr_err("[%s]pe50_info not valid\n", __func__);
		return -ENODEV;
	}

	if ((mode >= MIN_MAX_TEMP_C) && (mode <= MAX_TEMP_C))
		pe50_info->pe50.max_chrg_temp = mode;
	else
		pe50_info->pe50.max_chrg_temp = MAX_TEMP_C;

	return r ? r : count;
}

static ssize_t force_max_chrg_temp_show(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	int state;

	if (!pe50_info) {
		chr_err("[%s]pe50_info not valid\n", __func__);
		return -ENODEV;
	}

	state = pe50_info->pe50.max_chrg_temp;

	return scnprintf(buf, CHG_SHOW_MAX_SIZE, "%d\n", state);
}

static DEVICE_ATTR(force_max_chrg_temp, 0644,
		force_max_chrg_temp_show,
		force_max_chrg_temp_store);


void pe50_init(struct mtk_charger *info)
{
	int rc;

	if (!info)
		return;

	info->pe50.factory_mode = info->atm_enabled;

	info->pe50.is_factory_image = false;
	info->pe50.charging_limit_modes = CHARGING_LIMIT_UNKNOWN;
	info->pe50.adaptive_charging_disable_ibat = false;
	info->pe50.adaptive_charging_disable_ichg = false;
	info->pe50.charging_enable_hz = false;
	info->pe50.battery_charging_disable = false;

	if (info->pe50.factory_mode) {
		info->disable_aicl = true;
	}

	rc = parse_pe50_dt(info, &info->pdev->dev);
	if (rc < 0)
		chr_info("[%s]Error getting pe50 dt items rc = %d\n",__func__, rc);

	if(gpio_is_valid(info->pe50.wls_switch_en)) {
		rc  = devm_gpio_request_one(&info->pdev->dev, info->pe50.wls_switch_en,
				  GPIOF_OUT_INIT_LOW, "mux_wls_switch_en");
		if (rc  < 0)
			chr_err(" [%s] Failed to request wls_switch_en gpio, ret:%d", __func__, rc);
	}
	if(gpio_is_valid(info->pe50.wls_boost_en)) {
		rc  = devm_gpio_request_one(&info->pdev->dev, info->pe50.wls_boost_en,
				  GPIOF_OUT_INIT_LOW, "mux_wls_boost_en");
		if (rc  < 0)
			chr_err(" [%s] Failed to request wls_boost_en gpio, ret:%d", __func__, rc);
	}
	info->pe50.batt_health = POWER_SUPPLY_HEALTH_GOOD;

	info->pe50.chg_reboot.notifier_call = chg_reboot;
	info->pe50.chg_reboot.next = NULL;
	info->pe50.chg_reboot.priority = 1;
	rc = register_reboot_notifier(&info->pe50.chg_reboot);
	if (rc)
		chr_err("SMB register for reboot failed\n");

	rc = device_create_file(&info->pdev->dev,
				&dev_attr_force_demo_mode);
	if (rc) {
		chr_err("[%s]couldn't create force_demo_mode\n", __func__);
	}

	rc = device_create_file(&info->pdev->dev,
				&dev_attr_force_max_chrg_temp);
	if (rc) {
		chr_err("[%s]couldn't create force_max_chrg_temp\n", __func__);
	}

	rc = device_create_file(&info->pdev->dev,
				&dev_attr_factory_image_mode);
	if (rc)
		chr_err("[%s]couldn't create factory_image_mode\n", __func__);

	rc = device_create_file(&info->pdev->dev,
				&dev_attr_factory_charge_upper);
	if (rc)
		chr_err("[%s]couldn't create factory_charge_upper\n", __func__);

	info->pe50.init_done = true;
}

static int pe50_find_colder_temp_zone(int pres_zone, int vbat,
				struct pe50_temp_zone *zones,
				int num_zones)
{
	int i;
	int colder_zone;
	int target_zone;

	if (pres_zone == ZONE_HOT_PE50)
		colder_zone = num_zones - 1;
	else if (pres_zone == ZONE_COLD_PE50 ||
	  zones[pres_zone].temp_c == zones[ZONE_FIRST_PE50].temp_c)
		return ZONE_COLD_PE50;
	else {
		for (i = pres_zone - 1; i >= ZONE_FIRST_PE50; i--) {
			if (zones[pres_zone].temp_c > zones[i].temp_c) {
				colder_zone = i;
				break;
			}
		}
		if (i < 0)
			return ZONE_COLD_PE50;
	}

	target_zone = colder_zone;
	for (i = ZONE_FIRST_PE50; i < colder_zone; i++) {
		if (zones[colder_zone].temp_c == zones[i].temp_c) {
			target_zone = i;
			if (vbat < zones[i].norm_mv)
				break;
		}
	}

	return target_zone;
}

static int pe50_find_hotter_temp_zone(int pres_zone, int vbat,
				struct pe50_temp_zone *zones,
				int num_zones)
{
	int i;
	int hotter_zone;
	int target_zone;

	if (pres_zone == ZONE_COLD_PE50)
		hotter_zone = ZONE_FIRST_PE50;
	else if (pres_zone == ZONE_HOT_PE50 ||
	    zones[pres_zone].temp_c == zones[num_zones - 1].temp_c)
		return ZONE_HOT_PE50;
	else {
		for (i = pres_zone + 1; i < num_zones; i++) {
			if (zones[pres_zone].temp_c < zones[i].temp_c) {
				hotter_zone = i;
				break;
			}
		}
		if (i >= num_zones)
			return ZONE_HOT_PE50;
	}

	target_zone = hotter_zone;
	for (i = hotter_zone; i < num_zones; i++) {
		if (zones[hotter_zone].temp_c == zones[i].temp_c) {
			target_zone = i;
			if (vbat < zones[i].norm_mv)
				break;
		}
	}
	return target_zone;
}

static int pe50_refresh_temp_zone(int pres_zone, int vbat,
				struct pe50_temp_zone *zones,
				int num_zones)
{
	int i;
	int target_zone;

	if (pres_zone == ZONE_COLD_PE50 || pres_zone == ZONE_HOT_PE50)
		return pres_zone;

	target_zone = pres_zone;
	for (i = ZONE_FIRST_PE50; i < num_zones; i++) {
		if (zones[pres_zone].temp_c == zones[i].temp_c) {
			target_zone = i;
			if (vbat < zones[i].norm_mv)
				break;
		}
	}
	return target_zone;
}

static void pe50_find_temp_zone(struct mtk_charger *info, int temp_c, int vbat_mv)
{
	int i;
	//int temp_c;
	//int vbat_mv;
	int max_temp;
	int prev_zone, num_zones;
	int hotter_zone, colder_zone;
	struct pe50_temp_zone *zones;
	int hotter_t, hotter_fcc;
	int colder_t, colder_fcc;

	if (!info) {
		chr_err("called before chg valid!\n");
		return;
	}

	//temp_c = charger->batt_info.batt_temp;
	//vbat_mv = charger->batt_info.batt_mv;
	num_zones = info->pe50.num_temp_zones;
	prev_zone = info->pe50.pres_temp_zone;
	if (!info->pe50.temp_zones) {
		zones = NULL;
		num_zones = 0;
		max_temp = MAX_TEMP_C;
	} else {
		zones = info->pe50.temp_zones;
		if (info->pe50.max_chrg_temp >= MIN_MAX_TEMP_C)
			max_temp = info->pe50.max_chrg_temp;
		else
			max_temp = zones[num_zones - 1].temp_c;
	}

	if (prev_zone == ZONE_NONE_PE50 && zones) {
		for (i = num_zones - 1; i >= 0; i--) {
			if (temp_c >= zones[i].temp_c) {
				info->pe50.pres_temp_zone =
					pe50_find_hotter_temp_zone(i,
							vbat_mv,
							zones,
							num_zones);
				return;
			}
		}
		if (temp_c < MIN_TEMP_C)
			info->pe50.pres_temp_zone = ZONE_COLD_PE50;
		else
			info->pe50.pres_temp_zone =
					pe50_find_hotter_temp_zone(ZONE_COLD_PE50,
							vbat_mv,
							zones,
							num_zones);
		return;
	}

	if (prev_zone == ZONE_COLD_PE50) {
		if (temp_c >= MIN_TEMP_C + HYSTERISIS_DEGC) {
			if (!num_zones)
				info->pe50.pres_temp_zone = ZONE_FIRST_PE50;
			else
				info->pe50.pres_temp_zone =
					pe50_find_hotter_temp_zone(prev_zone,
							vbat_mv,
							zones,
							num_zones);
		}
	} else if (prev_zone == ZONE_HOT_PE50) {
		if (temp_c <=  max_temp - HYSTERISIS_DEGC) {
			if (!num_zones)
				info->pe50.pres_temp_zone = ZONE_FIRST_PE50;
			else
				info->pe50.pres_temp_zone =
					pe50_find_colder_temp_zone(prev_zone,
							vbat_mv,
							zones,
							num_zones);
		}
	} else if (zones) {
		hotter_zone = pe50_find_hotter_temp_zone(prev_zone,
						vbat_mv,
						zones,
						num_zones);
		colder_zone = pe50_find_colder_temp_zone(prev_zone,
						vbat_mv,
						zones,
						num_zones);
		if (hotter_zone == ZONE_HOT_PE50) {
			hotter_fcc = 0;
			hotter_t = zones[prev_zone].temp_c;
		} else {
			hotter_fcc = zones[hotter_zone].fcc_max_ma;
			hotter_t = zones[prev_zone].temp_c;
		}

		if (colder_zone == ZONE_COLD_PE50) {
			colder_fcc = 0;
			colder_t = MIN_TEMP_C;
		} else {
			colder_fcc = zones[colder_zone].fcc_max_ma;
			colder_t = zones[colder_zone].temp_c;
		}

		if (zones[prev_zone].fcc_max_ma < hotter_fcc)
			hotter_t += HYSTERISIS_DEGC;

		if (zones[prev_zone].fcc_max_ma < colder_fcc)
			colder_t -= HYSTERISIS_DEGC;

		if (temp_c < MIN_TEMP_C)
			info->pe50.pres_temp_zone = ZONE_COLD_PE50;
		else if (temp_c >= max_temp)
			info->pe50.pres_temp_zone = ZONE_HOT_PE50;
		else if (temp_c >= hotter_t)
			info->pe50.pres_temp_zone = hotter_zone;
		else if (temp_c < colder_t)
			info->pe50.pres_temp_zone = colder_zone;
		else
			info->pe50.pres_temp_zone =
					pe50_refresh_temp_zone(prev_zone,
							vbat_mv,
							zones,
							num_zones);
	} else {
		if (temp_c < MIN_TEMP_C)
			info->pe50.pres_temp_zone = ZONE_COLD_PE50;
		else if (temp_c >= max_temp)
			info->pe50.pres_temp_zone = ZONE_HOT_PE50;
		else
			info->pe50.pres_temp_zone = ZONE_FIRST_PE50;
	}

	if (prev_zone != info->pe50.pres_temp_zone) {
		chr_info("[C:%s]: temp zone switch %x -> %x\n",
			__func__,
			prev_zone,
			info->pe50.pres_temp_zone);
	}
}

void update_charging_limit_modes(struct mtk_charger *info, int batt_soc)
{
	enum charging_limit_modes charging_limit_modes;

	charging_limit_modes = info->pe50.charging_limit_modes;
	if ((charging_limit_modes != CHARGING_LIMIT_RUN)
	    && (batt_soc >= info->pe50.upper_limit_capacity))
		charging_limit_modes = CHARGING_LIMIT_RUN;
	else if ((charging_limit_modes != CHARGING_LIMIT_OFF)
		   && (batt_soc <= info->pe50.lower_limit_capacity))
		charging_limit_modes = CHARGING_LIMIT_OFF;

	if (charging_limit_modes != info->pe50.charging_limit_modes)
		info->pe50.charging_limit_modes = charging_limit_modes;
}

static int pe50_get_ffc_fv(struct mtk_charger *info, int temp_c)
{
	int ffc_max_fv;
	int i = 0;
	int temp = temp_c;
	int num_zones;
	struct pe50_ffc_zone *zone;

	//temp = charger->batt_info.batt_temp;
	num_zones = info->pe50.num_ffc_zones;
	zone = info->pe50.ffc_zones;
	while (i < num_zones && temp > zone[i++].temp);
	zone = i > 0? &zone[i - 1] : NULL;

	info->pe50.chrg_iterm = zone->ffc_chg_iterm;
	ffc_max_fv = zone->ffc_max_mv;
	chr_info("FFC temp zone %d, fv %d mV, chg iterm %d mA\n",
		  ((i > 0)? (i - 1) : 0), ffc_max_fv, info->pe50.chrg_iterm);

	return ffc_max_fv;
}

#define TAPER_COUNT_PE50 2
#define TAPER_DROP_MA_PE50 100
static bool pe50_has_current_tapered(struct mtk_charger *info,
				    int batt_ma, int taper_ma)
{
	bool change_state = false;
	int allowed_fcc, target_ma, rc;
	bool devchg1_en = false;

	if (!info) {
		chr_err("[%s]called before info valid!\n", __func__);
		return false;
	}

	if (info->dvchg1_dev) {
		rc = charger_dev_is_enabled(info->dvchg1_dev, &devchg1_en);
		if (rc < 0)
			devchg1_en = false;
	}

	if (devchg1_en)
		target_ma = taper_ma;
	else {
		rc = charger_dev_get_charging_current(info->chg1_dev, &allowed_fcc);
		if (rc < 0) {
			chr_err("[%s]can't get charging current!\n", __func__);
		} else
			allowed_fcc = allowed_fcc /1000;

		if (allowed_fcc >= taper_ma)
			target_ma = taper_ma;
		else
			target_ma = allowed_fcc - TAPER_DROP_MA_PE50;
	}

	chr_info("%s: curr target_ma:%d, batt_ma:%d\n", __func__, target_ma, batt_ma);

	if (batt_ma > 0) {
		if (batt_ma <= target_ma)
			if (info->pe50.chrg_taper_cnt >= TAPER_COUNT_PE50) {
				change_state = true;
				info->pe50.chrg_taper_cnt = 0;
			} else
				info->pe50.chrg_taper_cnt++;
		else
			info->pe50.chrg_taper_cnt = 0;
	} else {
		if (info->pe50.chrg_taper_cnt >= TAPER_COUNT_PE50) {
			change_state = true;
			info->pe50.chrg_taper_cnt = 0;
		} else
			info->pe50.chrg_taper_cnt++;
	}

	return change_state;
}

#define WARM_TEMP 45
#define COOL_TEMP 0
#define HYST_STEP_MV 50
#define DEMO_MODE_HYS_SOC 5
#define DEMO_MODE_VOLTAGE 4000
static void pe50_charger_check_status(struct mtk_charger *info)
{
	int rc;
	int batt_mv;
	int batt_ma;
	int batt_soc;
	int batt_temp;
	int usb_mv;
	int charger_present = 0;
	int stop_recharge_hyst;
	int prev_step;
//	int batt_cv_delata;
	struct charger_data *pdata;
	int value;

	union power_supply_propval val;
	struct pe50_params *pe50 = &info->pe50;
	struct pe50_temp_zone *zone;
	int max_fv_mv = -EINVAL;
	int target_fcc = -EINVAL;
	int target_fv = -EINVAL;

	/* Collect Current Information */
#if 0
	rc = pe50_get_prop_from_battery(info,
				POWER_SUPPLY_PROP_VOLTAGE_NOW, &val);
	if (rc < 0) {
		chr_err("[%s]Error getting Batt Voltage rc = %d\n", __func__, rc);
		goto end_check;
	} else
		batt_mv = val.intval / 1000;
#endif
	if (info->dvchg1_dev) {
		pdata = &info->chg_data[DVCHG1_SETTING];
		rc = charger_dev_get_adc(info->dvchg1_dev,
					  ADC_CHANNEL_VBAT,
					  &value, &value);
		if (rc >= 0) {
			batt_mv = value / 1000;
		}
	}

	rc = pe50_get_prop_from_battery(info,
				POWER_SUPPLY_PROP_CURRENT_NOW, &val);
	if (rc < 0) {
		chr_err("[%s]Error getting Batt Current rc = %d\n", __func__, rc);
		goto end_check;
	} else
		batt_ma = val.intval / 1000;

	rc = pe50_get_prop_from_battery(info,
				POWER_SUPPLY_PROP_CAPACITY, &val);
	if (rc < 0) {
		chr_err("[%s]Error getting Batt Capacity rc = %d\n", __func__, rc);
		goto end_check;
	} else
		batt_soc = val.intval;

	rc = pe50_get_prop_from_battery(info,
				POWER_SUPPLY_PROP_TEMP, &val);
	if (rc < 0) {
		chr_err("[%s]Error getting Batt Temperature rc = %d\n", __func__, rc);
		goto end_check;
	} else
		batt_temp = val.intval / 10;

	rc = pe50_get_prop_from_charger(info,
				POWER_SUPPLY_PROP_ONLINE, &val);
	if (rc < 0) {
		chr_err("[%s]Error getting charger online rc = %d\n", __func__, rc);
		goto end_check;
	} else
		charger_present = val.intval;

	usb_mv = get_vbus(info);


	chr_info("[%s]batt=%d mV, %d mA, %d C, USB= %d mV\n", __func__,
		batt_mv, batt_ma, batt_temp, usb_mv);

	if (!pe50->temp_zones) {
		chr_err("[%s]temp_zones is NULL\n", __func__);
		chr_info("[%s]EFFECTIVE: FV = %d, CDIS = %d, FCC = %d, "
		"USBICL = %d, DEMO_DISCHARG = %d\n", __func__,
		pe50->target_fv,
		pe50->chg_disable,
		pe50->target_fcc,
		pe50->target_usb,
		pe50->demo_discharging);

		goto end_check;
	}

	if (pe50->enable_charging_limit && pe50->is_factory_image)
		update_charging_limit_modes(info, batt_soc);

	pe50_find_temp_zone(info, batt_temp, batt_mv);
	if (pe50->pres_temp_zone >= info->pe50.num_temp_zones)
		zone = &pe50->temp_zones[0];
	else
		zone = &pe50->temp_zones[pe50->pres_temp_zone];

	if (pe50->base_fv_mv == 0) {
		pe50->base_fv_mv = info->data.battery_cv / 1000;
	}

	if (info->dvchg1_dev != NULL
		&& adapter_dev_get_property(info->select_adapter, CAP_TYPE) == MTK_PD_APDO
		/*&& info->pd_type == MTK_PD_CONNECT_PE_READY_SNK_APDO*/) {
		max_fv_mv = pe50_get_ffc_fv(info, batt_temp);


		if (max_fv_mv == 0)
			max_fv_mv = pe50->base_fv_mv;

		val.intval = true;
		pe50_set_prop_to_battery(info, POWER_SUPPLY_PROP_TYPE, &val);
	} else {
		max_fv_mv = pe50->base_fv_mv;
		info->pe50.chrg_iterm =  info->pe50.back_chrg_iterm;

		val.intval = false;
		pe50_set_prop_to_battery(info, POWER_SUPPLY_PROP_TYPE, &val);
	}
#if 0
	if(info->pe50.cycle_cv_steps != NULL) {
		rc = pe50_get_prop_from_battery(info,
					POWER_SUPPLY_PROP_CYCLE_COUNT, &val);
		if (rc < 0) {
			chr_err("[%s]Error getting battery cycle count rc = %d\n", __func__, rc);
		} else {
			batt_cv_delata = pe50_get_batt_cv_delata_by_cycle(val.intval);
			if( batt_cv_delata > 0)
				max_fv_mv = max_fv_mv - batt_cv_delata;

			if((max_fv_mv * 1000) != info->data.battery_cv)
				pe50_set_batt_cv_to_fg(max_fv_mv * 1000);
		}
	}
#endif
	/* Determine Next State */
	prev_step = info->pe50.pres_chrg_step;

	if (pe50->charging_limit_modes == CHARGING_LIMIT_RUN)
		pr_warn("Factory Mode/Image so Limiting Charging!!!\n");

	if (!charger_present) {
		pe50->pres_chrg_step = STEP_NONE_PE50;
	} else if ((pe50->pres_temp_zone == ZONE_HOT_PE50) ||
		   (pe50->pres_temp_zone == ZONE_COLD_PE50) ||
		   (pe50->charging_limit_modes == CHARGING_LIMIT_RUN)) {
		info->pe50.pres_chrg_step = STEP_STOP_PE50;
	} else if (pe50->demo_mode) {
		bool voltage_full;
		static int demo_full_soc = 100;
		static int usb_suspend = 0;

		pe50->pres_chrg_step = STEP_DEMO_PE50;
		chr_info("[%s]Battery in Demo Mode charging Limited %dper\n",
				__func__, pe50->demo_mode);

		voltage_full = ((usb_suspend == 0) &&
			((batt_mv + HYST_STEP_MV) >= DEMO_MODE_VOLTAGE) &&
			pe50_has_current_tapered(info, batt_ma,
						pe50->chrg_iterm));

		if ((usb_suspend == 0) &&
		    ((batt_soc >= pe50->demo_mode) ||
		     voltage_full)) {
			demo_full_soc = batt_soc;
			pe50->demo_discharging = true;
			usb_suspend = 1;
		} else if (usb_suspend &&
			   (batt_soc <=
				(demo_full_soc - DEMO_MODE_HYS_SOC))) {
			pe50->demo_discharging = false;
			usb_suspend = 0;
			pe50->chrg_taper_cnt = 0;
		}
		if (usb_suspend)
			charger_dev_set_input_current(info->chg1_dev, 0);

		chr_info("Charge Demo Mode:us = %d, vf = %d, dfs = %d,bs = %d\n",
				usb_suspend, voltage_full, demo_full_soc, batt_soc);
	} else if (pe50->pres_chrg_step == STEP_NONE_PE50) {
		if (zone->norm_mv && ((batt_mv + 2 * HYST_STEP_MV) >= zone->norm_mv)) {
			if (zone->fcc_norm_ma)
				pe50->pres_chrg_step = STEP_NORM_PE50;
			else
				pe50->pres_chrg_step = STEP_STOP_PE50;
		} else
			pe50->pres_chrg_step = STEP_MAX_PE50;
	} else if (pe50->pres_chrg_step == STEP_STOP_PE50) {
		if (batt_temp > COOL_TEMP)
			stop_recharge_hyst = 2 * HYST_STEP_MV;
		else
			stop_recharge_hyst = 5 * HYST_STEP_MV;
		if (zone->norm_mv && ((batt_mv + stop_recharge_hyst) >= zone->norm_mv)) {
			if (zone->fcc_norm_ma)
				pe50->pres_chrg_step = STEP_NORM_PE50;
			else
				pe50->pres_chrg_step = STEP_STOP_PE50;
		} else {
			pe50->pres_chrg_step = STEP_MAX_PE50;
		}
	} else if (pe50->pres_chrg_step == STEP_MAX_PE50) {
		if (!zone->norm_mv) {
			/* No Step in this Zone */
			pe50->chrg_taper_cnt = 0;
			if ((batt_mv + HYST_STEP_MV) >= max_fv_mv)
				pe50->pres_chrg_step = STEP_NORM_PE50;
			else
				pe50->pres_chrg_step = STEP_MAX_PE50;
		} else if ((batt_mv + HYST_STEP_MV) < zone->norm_mv) {
			pe50->chrg_taper_cnt = 0;
			pe50->pres_chrg_step = STEP_MAX_PE50;
		} else if (!zone->fcc_norm_ma) {
			pe50->pres_chrg_step = STEP_FLOAT_PE50;
		} else if (pe50_has_current_tapered(info, batt_ma,
						 zone->fcc_norm_ma)) {
			pe50->chrg_taper_cnt = 0;
			pe50->pres_chrg_step = STEP_NORM_PE50;
		}
	} else if (pe50->pres_chrg_step == STEP_NORM_PE50) {
		if (!zone->fcc_norm_ma)
			pe50->pres_chrg_step = STEP_FLOAT_PE50;
		else if ((batt_mv + HYST_STEP_MV) < zone->norm_mv) {
			pe50->chrg_taper_cnt = 0;
			pe50->pres_chrg_step = STEP_MAX_PE50;
		}
		else if ((batt_mv + HYST_STEP_MV / 2) < max_fv_mv) {
			pe50->chrg_taper_cnt = 0;
			pe50->pres_chrg_step = STEP_NORM_PE50;
		} else if (pe50_has_current_tapered(info, batt_ma,
						   pe50->chrg_iterm)) {
			pe50->pres_chrg_step = STEP_FULL_PE50;
			charger_dev_enable_termination(info->chg1_dev, true);
		}
	} else if (pe50->pres_chrg_step == STEP_FULL_PE50) {
		if (batt_soc <= 99 || batt_mv < (max_fv_mv - HYST_STEP_MV * 2)) {
			pe50->chrg_taper_cnt = 0;
			pe50->pres_chrg_step = STEP_NORM_PE50;
		}
	} else if (pe50->pres_chrg_step == STEP_FLOAT_PE50) {
		if ((zone->fcc_norm_ma) ||
		    ((batt_mv + HYST_STEP_MV) < zone->norm_mv))
			pe50->pres_chrg_step = STEP_MAX_PE50;
		else if (pe50_has_current_tapered(info, batt_ma,
				   pe50->chrg_iterm))
			pe50->pres_chrg_step = STEP_STOP_PE50;
	}

	/* Take State actions */
	switch (pe50->pres_chrg_step) {
	case STEP_FLOAT_PE50:
	case STEP_MAX_PE50:
		if (!zone->norm_mv)
			target_fv = max_fv_mv + pe50->vfloat_comp_mv;
		else
			target_fv = zone->norm_mv + pe50->vfloat_comp_mv;
		target_fcc = zone->fcc_max_ma;
		break;
	case STEP_FULL_PE50:
		target_fv = max_fv_mv;
		target_fcc = -EINVAL;
		break;
	case STEP_NORM_PE50:
		target_fv = max_fv_mv + pe50->vfloat_comp_mv;
		target_fcc = zone->fcc_norm_ma;
		break;
	case STEP_NONE_PE50:
		target_fv = max_fv_mv;
		target_fcc = zone->fcc_norm_ma;
		break;
	case STEP_STOP_PE50:
		target_fv = max_fv_mv;
		target_fcc = -EINVAL;
		break;
	case STEP_DEMO_PE50:
		target_fv = DEMO_MODE_VOLTAGE;
		target_fcc = zone->fcc_max_ma;
		break;
	default:
		break;
	}

	pe50->target_fv = target_fv * 1000;

	pe50->chg_disable = (target_fcc < 0);

	pe50->target_fcc = ((target_fcc >= 0) ? (target_fcc * 1000) : 0);

	if (info->pe50.pres_temp_zone == ZONE_HOT_PE50) {
		info->pe50.batt_health = POWER_SUPPLY_HEALTH_OVERHEAT;
	} else if (info->pe50.pres_temp_zone == ZONE_COLD_PE50) {
		info->pe50.batt_health = POWER_SUPPLY_HEALTH_COLD;
	} else if (batt_temp >= WARM_TEMP) {
		if (info->pe50.pres_chrg_step == STEP_STOP_PE50)
			info->pe50.batt_health = POWER_SUPPLY_HEALTH_OVERHEAT;
		else
			info->pe50.batt_health = POWER_SUPPLY_HEALTH_GOOD;
	} else if (batt_temp <= COOL_TEMP) {
		if (info->pe50.pres_chrg_step == STEP_STOP_PE50)
			info->pe50.batt_health = POWER_SUPPLY_HEALTH_COLD;
		else
			info->pe50.batt_health = POWER_SUPPLY_HEALTH_GOOD;
	} else {
		info->pe50.batt_health = POWER_SUPPLY_HEALTH_GOOD;
	}

	chr_info("[%s]FV %d mV, FCC %d mA\n",
		 __func__, target_fv, target_fcc);
	chr_err("[%s]Step State = %s\n", __func__,
		stepchg_str[(int)pe50->pres_chrg_step]);
	chr_info("[%s]EFFECTIVE: FV = %d, CDIS = %d, FCC = %d, "
		"USBICL = %d, DEMO_DISCHARG = %d\n",
		__func__,
		pe50->target_fv,
		pe50->chg_disable,
		pe50->target_fcc,
		pe50->target_usb,
		pe50->demo_discharging);
	chr_info("[%s]adaptive charging:disable_ibat = %d, "
		"disable_ichg = %d, enable HZ = %d, "
		"charging disable = %d\n",
		__func__,
		pe50->adaptive_charging_disable_ibat,
		pe50->adaptive_charging_disable_ichg,
		pe50->charging_enable_hz,
		pe50->battery_charging_disable);
end_check:

	return;
}
#endif
/* TN End modified by xinjun.lu/860715 20240729 CR/EKLAMU-202 */

static void mtk_chg_get_tchg(struct mtk_charger *info)
{
	int ret;
	int tchg_min = -127, tchg_max = -127;
	struct charger_data *pdata;

	pdata = &info->chg_data[CHG1_SETTING];
	ret = charger_dev_get_temperature(info->chg1_dev, &tchg_min, &tchg_max);
	if (ret < 0) {
		pdata->junction_temp_min = -127;
		pdata->junction_temp_max = -127;
	} else {
		pdata->junction_temp_min = tchg_min;
		pdata->junction_temp_max = tchg_max;
	}

	if (info->chg2_dev) {
		pdata = &info->chg_data[CHG2_SETTING];
		ret = charger_dev_get_temperature(info->chg2_dev,
			&tchg_min, &tchg_max);

		if (ret < 0) {
			pdata->junction_temp_min = -127;
			pdata->junction_temp_max = -127;
		} else {
			pdata->junction_temp_min = tchg_min;
			pdata->junction_temp_max = tchg_max;
		}
	}

	if (info->dvchg1_dev) {
		pdata = &info->chg_data[DVCHG1_SETTING];
		ret = charger_dev_get_adc(info->dvchg1_dev,
					  ADC_CHANNEL_TEMP_JC,
					  &tchg_min, &tchg_max);
		if (ret < 0) {
			pdata->junction_temp_min = -127;
			pdata->junction_temp_max = -127;
		} else {
			pdata->junction_temp_min = tchg_min;
			pdata->junction_temp_max = tchg_max;
		}
	}

	if (info->dvchg2_dev) {
		pdata = &info->chg_data[DVCHG2_SETTING];
		ret = charger_dev_get_adc(info->dvchg2_dev,
					  ADC_CHANNEL_TEMP_JC,
					  &tchg_min, &tchg_max);
		if (ret < 0) {
			pdata->junction_temp_min = -127;
			pdata->junction_temp_max = -127;
		} else {
			pdata->junction_temp_min = tchg_min;
			pdata->junction_temp_max = tchg_max;
		}
	}

	if (info->hvdvchg1_dev) {
		pdata = &info->chg_data[HVDVCHG1_SETTING];
		ret = charger_dev_get_adc(info->hvdvchg1_dev,
					  ADC_CHANNEL_TEMP_JC,
					  &tchg_min, &tchg_max);
		if (ret < 0) {
			pdata->junction_temp_min = -127;
			pdata->junction_temp_max = -127;
		} else {
			pdata->junction_temp_min = tchg_min;
			pdata->junction_temp_max = tchg_max;
		}
	}

	if (info->hvdvchg2_dev) {
		pdata = &info->chg_data[HVDVCHG2_SETTING];
		ret = charger_dev_get_adc(info->hvdvchg2_dev,
					  ADC_CHANNEL_TEMP_JC,
					  &tchg_min, &tchg_max);
		if (ret < 0) {
			pdata->junction_temp_min = -127;
			pdata->junction_temp_max = -127;
		} else {
			pdata->junction_temp_min = tchg_min;
			pdata->junction_temp_max = tchg_max;
		}
	}
}

static void charger_check_status(struct mtk_charger *info)
{
	bool charging = true;
	bool chg_dev_chgen = true;
	int temperature;
	struct battery_thermal_protection_data *thermal;
	int uisoc = 0;
/* TN Begin modified by xinjun.lu/860715 20241011 CR/EKLAMU-202 */
#if IS_ENABLED(CONFIG_PE50_FFC_SUPPORT)
	bool devchg1_en = false;
	int batt_ma = get_battery_current(info);
	int vbat = get_battery_voltage(info);
	int target_fv = 0;
#endif
/* TN End modified by xinjun.lu/860715 20241011 CR/EKLAMU-202 */

	if (get_charger_type(info) == POWER_SUPPLY_TYPE_UNKNOWN)
		return;

	temperature = info->battery_temp;
	thermal = &info->thermal;
	uisoc = get_uisoc(info);

/* TN Begin modified by jirui.li/860702 20240722 CR/EKLAMU-620 */
#if IS_ENABLED(CONFIG_OEM_TINNO_CHARGER) && IS_ENABLED(CONFIG_FACTORY_BUILD)
	if (uisoc >= info->factory_charging_limit_soc) {
		chr_err("TINNO_FACTORY_SUPPORT,soc >= %d stop charging!!\n", info->factory_charging_limit_soc);
		charging = false;
		goto stop_charging;
	}
#endif /* CONFIG_OEM_TINNO_CHARGER && CONFIG_FACTORY_BUILD */
/* TN End modified by jirui.li/860702 20240722 CR/EKLAMU-620 */

/* TN Begin modified by jirui.li/860702 20240814 CR/EKLAMU-1339 */
#if IS_ENABLED(CONFIG_OEM_TINNO_CHARGER)
	if (info->battery_protection_mode) {
		if (uisoc >= BATTERY_PROTECT_MAX_SOC) {
			chr_info("limit battery soc to %d, disable charger\n", BATTERY_PROTECT_MAX_SOC);
			info->is_over_bpm_max_soc = true;
			charging = false;
			goto stop_charging;
		} else if (uisoc <= BATTERY_PROTECT_MIN_SOC) {
			chr_info("soc below %d, disable bpm and start charging\n", BATTERY_PROTECT_MIN_SOC);
			info->is_over_bpm_max_soc = false;
		} else {
			if (info->is_over_bpm_max_soc) {
				chr_info("soc drop to %d - %d,stop charging!!\n", BATTERY_PROTECT_MIN_SOC, BATTERY_PROTECT_MAX_SOC);
				charging = false;
				goto stop_charging;
			} else {
				chr_info("soc first in %d - %d,start charging!!\n", BATTERY_PROTECT_MIN_SOC, BATTERY_PROTECT_MAX_SOC);
			}
		}
	}
	if (info->demo_mode_limit) {
		if (uisoc >= DEMO_MODE_LIMIT_SOC_DEFAULT) {
			chr_err("enter demo_mode_limit,soc >= %d stop charging!!\n", info->demo_mode_limit);
			charging = false;
			goto stop_charging;
		}
	}
#endif /* CONFIG_OEM_TINNO_CHARGER */
/* TN End modified by jirui.li/860702 20240814 CR/EKLAMU-1339 */
	info->setting.vbat_mon_en = true;
	if (info->enable_sw_jeita == true || info->enable_vbat_mon != true ||
	    info->batpro_done == true)
		info->setting.vbat_mon_en = false;

	if (info->enable_sw_jeita == true) {
		do_sw_jeita_state_machine(info);
		if (info->sw_jeita.charging == false) {
			charging = false;
			goto stop_charging;
		}
	} else {

		if (thermal->enable_min_charge_temp) {
			if (temperature < thermal->min_charge_temp) {
				chr_err("Battery Under Temperature or NTC fail %d %d\n",
					temperature, thermal->min_charge_temp);
				thermal->sm = BAT_TEMP_LOW;
				charging = false;
				goto stop_charging;
			} else if (thermal->sm == BAT_TEMP_LOW) {
				if (temperature >=
				    thermal->min_charge_temp_plus_x_degree) {
					chr_err("Battery Temperature raise from %d to %d(%d), allow charging!!\n",
					thermal->min_charge_temp,
					temperature,
					thermal->min_charge_temp_plus_x_degree);
					thermal->sm = BAT_TEMP_NORMAL;
				} else {
					charging = false;
					goto stop_charging;
				}
			}
		}

		if (temperature >= thermal->max_charge_temp) {
			chr_err("Battery over Temperature or NTC fail %d %d\n",
				temperature, thermal->max_charge_temp);
			thermal->sm = BAT_TEMP_HIGH;
			charging = false;
			goto stop_charging;
		} else if (thermal->sm == BAT_TEMP_HIGH) {
			if (temperature
			    < thermal->max_charge_temp_minus_x_degree) {
				chr_err("Battery Temperature raise from %d to %d(%d), allow charging!!\n",
				thermal->max_charge_temp,
				temperature,
				thermal->max_charge_temp_minus_x_degree);
				thermal->sm = BAT_TEMP_NORMAL;
			} else {
				charging = false;
				goto stop_charging;
			}
		}
	}

	mtk_chg_get_tchg(info);

	if (!mtk_chg_check_vbus(info)) {
		charging = false;
		goto stop_charging;
	}

	if (info->cmd_discharging)
		charging = false;
	if (info->safety_timeout)
		charging = false;
	if (info->vbusov_stat)
		charging = false;
	if (info->dpdmov_stat)
		charging = false;
	if (info->sc.disable_charger == true)
		charging = false;
/* TN Begin modified by hao.jia/809321 20240718 CR/EKLAMU-202 */
#if IS_ENABLED(CONFIG_OEM_TINNO_CHARGER)
	if (info->enable_hiz == true)
		charging = false;
	if (info->enable_charger == false)
		charging = false;
#if IS_ENABLED(CONFIG_FACTORY_BUILD)
	if (info->enable_factory_charging_test == true) {
		info->can_charging = false;
		return;
	}
#endif /* CONFIG_FACTORY_BUILD*/
#endif /* CONFIG_OEM_TINNO_CHARGER */
/* TN End modified by hao.jia/809321 20240718 CR/EKLAMU-202 */

/* TN Begin modified by xinjun.lu/860715 20241011 CR/EKLAMU-202 */
#if IS_ENABLED(CONFIG_PE50_FFC_SUPPORT)
	if (info->pe50.pres_chrg_step == STEP_STOP_PE50)
		charging = false;
	if (info->pe50.demo_discharging)
		charging = false;
	if (info->pe50.adaptive_charging_disable_ibat)
		charging = false;
#endif

#if IS_ENABLED(CONFIG_PE50_FFC_SUPPORT)
#if IS_ENABLED(CONFIG_OEM_TURBO_CHARGER)
	bool turbo_active = turbo_charger_active;
#else
	bool turbo_active = false;
#endif
	charger_dev_is_enabled(info->chg1_dev, &chg_dev_chgen);
	if (info->dvchg1_dev)
		charger_dev_is_enabled(info->dvchg1_dev, &devchg1_en);

	if (chg_dev_chgen && !devchg1_en && !turbo_active) {
		if (info->pe50.pres_chrg_step != STEP_FULL_PE50) {
			if (temperature < BATTERY_TEMP_LOW || temperature > BATTERY_TEMP_HIGH) {
				target_fv = pe50_get_ffc_fv(info, temperature);
				info->data.battery_cv = target_fv * 1000;
			}

			if (info->ignore_current_check_time > IGNORE_CURRENT_CHECK_TIME_MAX
				&& (uisoc == BATTERY_CHARGING_FULL_SOC || temperature < BATTERY_TEMP_LOW || temperature > BATTERY_TEMP_HIGH)
				&& vbat > (info->data.battery_cv / 1000) - BATTERY_CV_GAP
				&& pe50_has_current_tapered(info, batt_ma, info->pe50.chrg_iterm))
				info->pe50.pres_chrg_step = STEP_FULL_PE50;

			if (info->ignore_current_check_time <= IGNORE_CURRENT_CHECK_TIME_MAX)
				info->ignore_current_check_time++;
		}
		chr_err("chrg_iterm=%d batt_ma=%d pres_chrg_step=%d time=%d vbat=%d uisoc=%d battery_cv=%d\n",
				info->pe50.chrg_iterm, batt_ma, info->pe50.pres_chrg_step,
				info->ignore_current_check_time, vbat, uisoc, info->data.battery_cv);
	}
#endif
/* TN End modified by xinjun.lu/860715 20241011 CR/EKLAMU-202 */

stop_charging:
	mtk_battery_notify_check(info);

	if (charging && uisoc < 80 && info->batpro_done == true) {
		info->setting.vbat_mon_en = true;
		info->batpro_done = false;
		info->stop_6pin_re_en = false;
	}

/* TN Begin modified by jirui.li/860702 20241010 CR/EKLAMU-202 */
#if IS_ENABLED(CONFIG_OEM_TINNO_CHARGER)
	chr_err("tmp:%d (jeita:%d sm:%d cv:%d en:%d) (sm:%d) en:%d c:%d s:%d ov:%d %d sc:%d %d %d saf_cmd:%d bat_mon:%d %d bpm:%d demo:%d\n",
		temperature, info->enable_sw_jeita, info->sw_jeita.sm,
		info->sw_jeita.cv, info->sw_jeita.charging, thermal->sm,
		charging, info->cmd_discharging, info->safety_timeout,
		info->vbusov_stat, info->dpdmov_stat, info->sc.disable_charger,
		info->can_charging, charging, info->safety_timer_cmd,
		info->enable_vbat_mon, info->batpro_done,
		info->battery_protection_mode, info->demo_mode_limit);
#else
	chr_err("tmp:%d (jeita:%d sm:%d cv:%d en:%d) (sm:%d) en:%d c:%d s:%d ov:%d %d sc:%d %d %d saf_cmd:%d bat_mon:%d %d\n",
		temperature, info->enable_sw_jeita, info->sw_jeita.sm,
		info->sw_jeita.cv, info->sw_jeita.charging, thermal->sm,
		charging, info->cmd_discharging, info->safety_timeout,
		info->vbusov_stat, info->dpdmov_stat, info->sc.disable_charger,
		info->can_charging, charging, info->safety_timer_cmd,
		info->enable_vbat_mon, info->batpro_done);
#endif /* CONFIG_OEM_TINNO_CHARGER */
/* TN End modified by jirui.li/860702 20241010 CR/EKLAMU-202 */

	charger_dev_is_enabled(info->chg1_dev, &chg_dev_chgen);

	if (charging != info->can_charging)
		_mtk_enable_charging(info, charging);
	else if (charging == false && chg_dev_chgen == true)
		_mtk_enable_charging(info, charging);

/*TN Begin modified by hao.jia/809321 20240628 CR/EKLAMU-202 */
#if IS_ENABLED(CONFIG_OEM_TURBO_CHARGER)
	mtk_can_charging = charging;
#endif /* CONFIG_OEM_TURBO_CHARGER */
/*TN End modified by hao.jia/809321 20240628 CR/EKLAMU-202 */

	info->can_charging = charging;
}

static bool charger_init_algo(struct mtk_charger *info)
{
	struct chg_alg_device *alg;
	int idx = 0;
	int ret = 0;

	info->chg1_dev = get_charger_by_name("primary_chg");
	if (info->chg1_dev)
		chr_err("%s, Found primary charger\n", __func__);
	else {
		chr_err("%s, *** Error : can't find primary charger ***\n"
			, __func__);
		return false;
	}

	chr_err("%s, start current_selector init flow: %s\n", __func__, info->curr_select_name);
	if (strcmp(info->curr_select_name, "current_selector_master") == 0)
		info->cschg1_dev = get_charger_by_name("current_selector_master");

	if (info->cschg1_dev) {
		chr_err("%s, Found main current selector charger\n", __func__);
		ret = charger_cs_init_setting(info->cschg1_dev);
		if (ret < 0) {
			chr_err("%s, failed to init cs, close cs function\n", __func__);
			info->cs_hw_disable = true;
		} else {
			ret = charger_dev_set_constant_voltage(info->cschg1_dev, 4350);
		if (ret < 0)
			chr_err("%s: failed to set cs1 cv to: 4350mV.\n", __func__);
		ret = charger_dev_set_charging_current(info->cschg1_dev, AC_CS_NORMAL_CC);
		if (ret < 0)
			chr_err("%s: failed to set cs1 cc to: %d mA.\n", __func__, AC_CS_NORMAL_CC);
		}
		info->cs_cc_now = AC_CS_NORMAL_CC;
	} else {
		chr_err("%s, *** Warning : can't find main current selector charger ***\n"
			, __func__);
	}

	alg = get_chg_alg_by_name("pe5p");
	info->alg[idx] = alg;
	if (alg == NULL)
		chr_err("get pe5p fail\n");
	else {
		chr_err("get pe5p success\n");
		alg->config = info->config;
		alg->alg_id = PE5P_ID;
		chg_alg_init_algo(alg);
		register_chg_alg_notifier(alg, &info->chg_alg_nb);
	}
	idx++;

	alg = get_chg_alg_by_name("hvbp");
	info->alg[idx] = alg;
	if (alg == NULL)
		chr_err("get hvbp fail\n");
	else {
		chr_err("get hvbp success\n");
		alg->config = info->config;
		alg->alg_id = HVBP_ID;
		chg_alg_init_algo(alg);
		register_chg_alg_notifier(alg, &info->chg_alg_nb);
	}
	idx++;

	alg = get_chg_alg_by_name("pe5");
	info->alg[idx] = alg;
	if (alg == NULL)
		chr_err("get pe5 fail\n");
	else {
		chr_err("get pe5 success\n");
		alg->config = info->config;
		alg->alg_id = PE5_ID;
		chg_alg_init_algo(alg);
		register_chg_alg_notifier(alg, &info->chg_alg_nb);
	}
	idx++;

	alg = get_chg_alg_by_name("pe45");
	if (alg == NULL) {
		chr_err("cannot get pe45\n");
		alg = get_chg_alg_by_name("pe4");
		info->alg[idx] = alg;
		if (alg == NULL)
			chr_err("cannot get pe4\n");
		else {
			chr_err("get pe4 success\n");
			alg->config = info->config;
			alg->alg_id = PE4_ID;
			chg_alg_init_algo(alg);
			register_chg_alg_notifier(alg, &info->chg_alg_nb);
		}
	} else {
		info->alg[idx] = alg;
		chr_err("get pe45 success\n");
		alg->config = info->config;
		alg->alg_id = PE4_ID;
		chg_alg_init_algo(alg);
		register_chg_alg_notifier(alg, &info->chg_alg_nb);
	}

	idx++;

	alg = get_chg_alg_by_name("pd");
	info->alg[idx] = alg;
	if (alg == NULL)
		chr_err("get pd fail\n");
	else {
		chr_err("get pd success\n");
		alg->config = info->config;
		alg->alg_id = PDC_ID;
		chg_alg_init_algo(alg);
		register_chg_alg_notifier(alg, &info->chg_alg_nb);
	}
	idx++;

	alg = get_chg_alg_by_name("pe2");
	info->alg[idx] = alg;
	if (alg == NULL)
		chr_err("get pe2 fail\n");
	else {
		chr_err("get pe2 success\n");
		alg->config = info->config;
		alg->alg_id = PE2_ID;
		chg_alg_init_algo(alg);
		register_chg_alg_notifier(alg, &info->chg_alg_nb);
	}
	idx++;

	alg = get_chg_alg_by_name("pe");
	info->alg[idx] = alg;
	if (alg == NULL)
		chr_err("get pe fail\n");
	else {
		chr_err("get pe success\n");
		alg->config = info->config;
		alg->alg_id = PE_ID;
		chg_alg_init_algo(alg);
		register_chg_alg_notifier(alg, &info->chg_alg_nb);
	}

	chr_err("config is %d\n", info->config);
	if (info->config == DUAL_CHARGERS_IN_SERIES) {
		info->chg2_dev = get_charger_by_name("secondary_chg");
		if (info->chg2_dev)
			chr_err("Found secondary charger\n");
		else {
			chr_err("*** Error : can't find secondary charger ***\n");
			return false;
		}
	} else if (info->config == DIVIDER_CHARGER ||
		   info->config == DUAL_DIVIDER_CHARGERS) {
		info->dvchg1_dev = get_charger_by_name("primary_dvchg");
		if (info->dvchg1_dev)
			chr_err("Found primary divider charger\n");
		else {
			chr_err("*** Error : can't find primary divider charger ***\n");
			return false;
		}
		if (info->config == DUAL_DIVIDER_CHARGERS) {
			info->dvchg2_dev =
				get_charger_by_name("secondary_dvchg");
			if (info->dvchg2_dev)
				chr_err("Found secondary divider charger\n");
			else {
				chr_err("*** Error : can't find secondary divider charger ***\n");
				return false;
			}
		}
	} else if (info->config == HVDIVIDER_CHARGER ||
		   info->config == DUAL_HVDIVIDER_CHARGERS) {
		info->hvdvchg1_dev = get_charger_by_name("hvdiv2_chg1");
		if (info->hvdvchg1_dev)
			chr_err("Found primary hvdivider charger\n");
		else {
			chr_err("*** Error : can't find primary hvdivider charger ***\n");
			return false;
		}
		if (info->config == DUAL_HVDIVIDER_CHARGERS) {
			info->hvdvchg2_dev = get_charger_by_name("hvdiv2_chg2");
			if (info->hvdvchg2_dev)
				chr_err("Found secondary hvdivider charger\n");
			else {
				chr_err("*** Error : can't find secondary hvdivider charger ***\n");
				return false;
			}
		}
	}

	chr_err("register chg1 notifier %d %d\n",
		info->chg1_dev != NULL, info->algo.do_event != NULL);
	if (info->chg1_dev != NULL && info->algo.do_event != NULL) {
		chr_err("register chg1 notifier done\n");
		info->chg1_nb.notifier_call = info->algo.do_event;
		register_charger_device_notifier(info->chg1_dev,
						&info->chg1_nb);
		charger_dev_set_drvdata(info->chg1_dev, info);
	}

	chr_err("register dvchg chg1 notifier %d %d\n",
		info->dvchg1_dev != NULL, info->algo.do_dvchg1_event != NULL);
	if (info->dvchg1_dev != NULL && info->algo.do_dvchg1_event != NULL) {
		chr_err("register dvchg chg1 notifier done\n");
		info->dvchg1_nb.notifier_call = info->algo.do_dvchg1_event;
		register_charger_device_notifier(info->dvchg1_dev,
						&info->dvchg1_nb);
		charger_dev_set_drvdata(info->dvchg1_dev, info);
	}

	chr_err("register dvchg chg2 notifier %d %d\n",
		info->dvchg2_dev != NULL, info->algo.do_dvchg2_event != NULL);
	if (info->dvchg2_dev != NULL && info->algo.do_dvchg2_event != NULL) {
		chr_err("register dvchg chg2 notifier done\n");
		info->dvchg2_nb.notifier_call = info->algo.do_dvchg2_event;
		register_charger_device_notifier(info->dvchg2_dev,
						 &info->dvchg2_nb);
		charger_dev_set_drvdata(info->dvchg2_dev, info);
	}

	chr_err("register hvdvchg chg1 notifier %d %d\n",
		info->hvdvchg1_dev != NULL,
		info->algo.do_hvdvchg1_event != NULL);
	if (info->hvdvchg1_dev != NULL &&
	    info->algo.do_hvdvchg1_event != NULL) {
		chr_err("register hvdvchg chg1 notifier done\n");
		info->hvdvchg1_nb.notifier_call = info->algo.do_hvdvchg1_event;
		register_charger_device_notifier(info->hvdvchg1_dev,
						 &info->hvdvchg1_nb);
		charger_dev_set_drvdata(info->hvdvchg1_dev, info);
	}

	chr_err("register hvdvchg chg2 notifier %d %d\n",
		info->hvdvchg2_dev != NULL,
		info->algo.do_hvdvchg2_event != NULL);
	if (info->hvdvchg2_dev != NULL &&
	    info->algo.do_hvdvchg2_event != NULL) {
		chr_err("register hvdvchg chg2 notifier done\n");
		info->hvdvchg2_nb.notifier_call = info->algo.do_hvdvchg2_event;
		register_charger_device_notifier(info->hvdvchg2_dev,
						 &info->hvdvchg2_nb);
		charger_dev_set_drvdata(info->hvdvchg2_dev, info);
	}

	return true;
}

/* TN Begin modified by xinjun.lu/860715 20240710 CR/EKLAMU-202 */
#if IS_ENABLED(CONFIG_OEM_HVDCP_ALGO)
static int hvdcp_charging(struct mtk_charger *info)
{
	int i, ret, vbus_volt_old, vbus_volt_new, time_out = 0;
	struct charger_data *pdata;
	pdata = &info->chg_data[CHG1_SETTING];

	chr_err("%s start\n", __func__);
	ret = get_vbus(info);
	if (ret < 0) {
		chr_err("%s get vbus voltage failed before hvdcp check\n", __func__);
		return ret;
	} else {
		vbus_volt_old = ret;
	}

	if (vbus_volt_old >= HVDCP_TARGE_VOLT) {
		chr_err("%s vbus_volt_old hvdcp detected, vbus old voltage: %d\n", __func__, vbus_volt_old);
		return ret;
	}

	ret = charger_dev_set_dp_voltage(info->chg1_dev, 600000);
	if (ret < 0) {
		chr_err("%s ignore hvdcp detect due to set dp voltage failed(%d)\n", __func__, ret);
		return ret;
	}

	ret = charger_dev_set_dm_voltage(info->chg1_dev, 3300000);
	if (ret < 0) {
		chr_err("%s ignore hvdcp detect due to set dm voltage failed(%d)\n", __func__, ret);
		return ret;
	}
	chr_info("%s enter hvdcp vbus old %d\n", __func__, vbus_volt_old);
	mdelay(100);
	for (i = 0; i < 15; i++) {
		ret = charger_dev_set_dp_voltage(info->chg1_dev, 3300000);
		if (ret < 0) {
			chr_err("%s ignore hvdcp detect due to set dp voltage failed(%d), Loop: %d\n", __func__, ret, i);
			return ret;
		}
		udelay(100);
		ret = charger_dev_set_dp_voltage(info->chg1_dev, 600000);
		if (ret < 0) {
			chr_err("%s ignore hvdcp detect due to set dp voltage failed(%d), Loop: %d\n", __func__, ret, i);
			return ret;
		}
		mdelay(500);
		ret = get_vbus(info);
		if (ret < 0) {
			chr_err("%s get vbus voltage failed\n", __func__);
			return -EINVAL;
		}

		while (time_out < 100) {
			if (first_insert) {
				info->hvdcp_boost_done_time = ktime_get_boottime();
				chr_err("%s charge already plug out,quit hvdcp work\n", __func__);
				return -EINVAL;
			}
			mdelay(10);
			ret = get_vbus(info);
			if (ret < 0) {
				chr_err("%s check hvdcp vbus failed, try count: %d\n", __func__, i);
				return ret;
			}
			vbus_volt_new = get_vbus(info);
			if (vbus_volt_new >= vbus_volt_old - 200) {
				vbus_volt_old = vbus_volt_new;
				chr_info("%s hvdcp detected, vbus new voltage: %d\n",  __func__, vbus_volt_new);
				break;
			}
			time_out++;
		}
		if (time_out == 100) {
			chr_info("%s hvdcp not supported\n", __func__);
			return -EINVAL;
		}

		if (vbus_volt_new >= HVDCP_TARGE_VOLT) {
			chr_err("%s vbus new >= %d, vbus voltage: %d\n", __func__, HVDCP_TARGE_VOLT, vbus_volt_new);
			info->ext_chr_type = POWER_SUPPLY_TYPE_USB_QC3;
			info->hvdcp_boost_done_time = ktime_get_boottime();
			break;
		}
	}

	if (vbus_volt_new >= HVDCP_MAX_VOLT) {
		mdelay(300);
		ret = charger_dev_set_dm_voltage(info->chg1_dev, 600000);
		udelay(100);
		ret = charger_dev_set_dm_voltage(info->chg1_dev, 3300000);
		mdelay(500);
		ret = get_vbus(info);
		if (ret < 0) {
			chr_err("%s get vbus voltage failed\n", __func__);
			return -EINVAL;
		}
		chr_err("%s vbus new > %d , vbus voltage: %d\n", __func__, HVDCP_MAX_VOLT, vbus_volt_new);
	}

	_wake_up_charger(info);

	return ret;
}

static int hvdcp_charger_detect_notifier_cb(struct notifier_block *nb,
			unsigned long event, void *data)
{
	struct mtk_charger *info = container_of(nb,
						struct mtk_charger, hvdcp_charger_detect_nb);
	union power_supply_propval val = {0};
	struct power_supply *psy = data;
	int ret = 0;
	int chr_type = 0;
	ktime_t time_diff;
	struct timespec64 dtime;

	chr_err("%s: enter, power supply name is %s\n", __func__, psy->desc->name);

	if (IS_ERR_OR_NULL(info) || IS_ERR_OR_NULL(info->chg_psy)) {
		chr_err("%s: failed to get mtk_charger device\n", __func__);
		return NOTIFY_DONE;
	}

	if (psy == info->chg_psy) {
		chr_err("%s: %s first insert cable\n", __func__, first_insert ? "is" : "not");
		if (info->ext_chr_type != POWER_SUPPLY_TYPE_USB_QC3 && first_insert) {
			ret = power_supply_get_property(info->chg_psy,
							POWER_SUPPLY_PROP_USB_TYPE, &val);
			if (ret < 0) {
				chr_err("%s: failed to get basic charger type\n", __func__);
			} else {
				chr_type = val.intval;
				if (chr_type == POWER_SUPPLY_USB_TYPE_DCP) {
					if (oem_pcba_charge_power() == CHARGE_POWER_18W) {
						if (adapter_dev_get_property(info->select_adapter, CAP_TYPE) == MTK_PD_APDO) {
							chr_err("%s: ignore QC3 detection due to pd pps adapter\n", __func__);
							return NOTIFY_DONE;
						}

						if (ktime_compare(info->hvdcp_plug_in_time, info->hvdcp_boost_done_time) >= 0) {
							time_diff = ktime_sub(info->hvdcp_plug_in_time, info->hvdcp_boost_done_time);
							dtime = ktime_to_timespec64(time_diff);
							chr_err("%s: dtime %lld.%lld\n", __func__, (long long)dtime.tv_sec, (long long)dtime.tv_nsec);
							if (dtime.tv_sec == 0 && dtime.tv_nsec < HVDCP_IGNORE_TIME_DIFF_NS) {
								chr_err("%s: hvdcp dtime too short, ignore detect QC3.\n", __func__);
								return NOTIFY_DONE;
							}
						}

						chr_err("%s: found 18W device, try to detect QC3 charger\n", __func__);
						charger_dev_set_dp_voltage(info->chg1_dev, 600000);
						schedule_delayed_work(&info->hvdcp_work, msecs_to_jiffies(1500));
						first_insert = false;
					}
				}
			}
		}
	}

	return NOTIFY_OK;
}

static void charger_hvdcp_detect_work(
		struct work_struct *work)
{
	struct mtk_charger *info =
		container_of(work, struct mtk_charger, hvdcp_work.work);
	int ret;
	chr_err("%s enter hvdcp detect work\n", __func__);

	if (info == NULL) {
		chr_err("%s: get info failed\n", __func__);
		cancel_delayed_work(&info->hvdcp_work);
		return;
	}
	info->is_hvdcp_detecting = true;
	/*Set cur is 500ma before do BC1.2 & QC*/
	charger_dev_set_charging_current(info->chg1_dev, 500000);
	charger_dev_set_input_current(info->chg1_dev, 500000);
	ret = hvdcp_charging(info);
	if (ret < 0) {
		cancel_delayed_work(&info->hvdcp_work);
		chr_err("cancel charger_delayed_work hvdcp\n");
		first_insert = true;
	}
	info->is_hvdcp_detecting = false;
}
#endif
/* TN End modified by xinjun.lu/860715 20240710 CR/EKLAMU-202 */

static int mtk_charger_force_disable_power_path(struct mtk_charger *info,
	int idx, bool disable);
static int mtk_charger_plug_out(struct mtk_charger *info)
{
	struct charger_data *pdata1 = &info->chg_data[CHG1_SETTING];
	struct charger_data *pdata2 = &info->chg_data[CHG2_SETTING];
	struct chg_alg_device *alg;
	struct chg_alg_notify notify;
	int i;

	chr_err("%s\n", __func__);
	info->chr_type = POWER_SUPPLY_TYPE_UNKNOWN;
	info->charger_thread_polling = false;
	info->dpdmov_stat = false;
	info->lst_dpdmov_stat = false;
	info->power_path_en = true;
	info->en_power_path = true;

	pdata1->usb_input_current_limit = -1;
	pdata1->pd_input_current_limit = -1;
	pdata1->disable_charging_count = 0;
	pdata1->input_current_limit_by_aicl = -1;
	pdata2->disable_charging_count = 0;
	if (pdata1->thermal_input_current_limit == -1)
		pdata1->input_current_limit = 15000;

	notify.evt = EVT_PLUG_OUT;
	notify.value = 0;
	for (i = 0; i < MAX_ALG_NO; i++) {
		alg = info->alg[i];
		chg_alg_notifier_call(alg, &notify);
		chg_alg_plugout_reset(alg);
	}
	memset(&info->sc.data, 0, sizeof(struct scd_cmd_param_t_1));
	charger_dev_set_input_current(info->chg1_dev, 100000);
	charger_dev_set_mivr(info->chg1_dev, info->data.min_charger_voltage);
	charger_dev_plug_out(info->chg1_dev);
/*TN Begin modified by hao.jia/809321 20240904 CR/EKLAMU-202*/
#if IS_ENABLED(CONFIG_OEM_TINNO_CHARGER)
	info->ext_chr_type = POWER_SUPPLY_TYPE_UNKNOWN;
	sw_jeita_enter_1A = false;
	sw_jeita_enter_cv2 = false;
#if IS_ENABLED(CONFIG_PE50_FFC_SUPPORT)
	info->pe50.pres_chrg_step = STEP_NONE_PE50;
#endif
	charger_dev_enable_termination(info->chg1_dev, true);
	info->ignore_current_check_time = 0;
	info->aicl_check = true;
	info->aicl_final_ic = 0;
	info->restart_hvdcp_work = false;
	info->is_hvdcp_detecting = false;
	charger_dev_do_event(info->chg1_dev, EVENT_DISCHARGE, 0);
#endif /* CONFIG_OEM_TINNO_CHARGER */
#if IS_ENABLED(CONFIG_OEM_TINNO_CHARGER) && IS_ENABLED(CONFIG_FACTORY_BUILD)
	info->start_factory_discharging = false;
#endif
/*TN End modified by hao.jia/809321 20240904 CR/EKLAMU-202*/

/* TN Begin modified by xinjun.lu/860715 20240710 CR/EKLAMU-202 */
#if IS_ENABLED(CONFIG_OEM_HVDCP_ALGO)
	first_insert = true;
	cancel_delayed_work(&info->hvdcp_work);
	chr_err("%s: cancel hvdcp work\n", __func__);
#endif
/* TN End modified by xinjun.lu/860715 20240710 CR/EKLAMU-202 */
	mtk_charger_force_disable_power_path(info, CHG1_SETTING, true);

/*TN Begin modified by hao.jia/809321 20240628 CR/EKLAMU-202*/
#if IS_ENABLED(CONFIG_OEM_TURBO_CHARGER)
	qc3p_charger_ready = 0;
	ffc_batt_full = false;
	info->pres_chrg_step = false;
#endif /* CONFIG_OEM_TURBO_CHARGER */
/*TN End modified by hao.jia/809321 20240628 CR/EKLAMU-202*/
	if (info->enable_vbat_mon)
		charger_dev_enable_6pin_battery_charging(info->chg1_dev, false);

	mtk_adapter_protocol_init(info);
	return 0;
}

static int mtk_charger_plug_in(struct mtk_charger *info,
				int chr_type)
{
	struct chg_alg_device *alg;
	struct chg_alg_notify notify;
	int i, vbat;

	chr_debug("%s\n",
		__func__);

	info->chr_type = chr_type;
	info->usb_type = get_usb_type(info);
	info->charger_thread_polling = true;

	info->can_charging = true;
	//info->enable_dynamic_cv = true;
	info->safety_timeout = false;
	info->vbusov_stat = false;
	info->old_cv = 0;
	info->stop_6pin_re_en = false;
	info->batpro_done = false;
	smart_charging(info);
	chr_err("mtk_is_charger_on plug in, type:%d\n", chr_type);
#if IS_ENABLED(CONFIG_OEM_TINNO_CHARGER) || IS_ENABLED(CONFIG_OEM_HVDCP_ALGO)
	info->hvdcp_plug_in_time = ktime_get_boottime();
#endif

	vbat = get_battery_voltage(info);

	notify.evt = EVT_PLUG_IN;
	notify.value = 0;
	for (i = 0; i < MAX_ALG_NO; i++) {
		alg = info->alg[i];
		chg_alg_notifier_call(alg, &notify);
		chg_alg_set_prop(alg, ALG_REF_VBAT, vbat);
	}

	memset(&info->sc.data, 0, sizeof(struct scd_cmd_param_t_1));
	info->sc.disable_in_this_plug = false;

	charger_dev_plug_in(info->chg1_dev);
	mtk_charger_force_disable_power_path(info, CHG1_SETTING, false);
/* TN Begin modified by xinjun.lu/860715 20240809 CR/EKLAMU-202 */
#if IS_ENABLED(CONFIG_OEM_TINNO_CHARGER)
	charger_dev_set_eoc_current(info->chg1_dev, info->data.eoc_current); //set cut-off current
#endif
/* TN End modified by xinjun.lu/860715 20240809 CR/EKLAMU-202 */


	return 0;
}

static bool mtk_is_charger_on(struct mtk_charger *info)
{
	int chr_type;

	chr_type = get_charger_type(info);
	if (chr_type == POWER_SUPPLY_TYPE_UNKNOWN) {
		if (info->chr_type != POWER_SUPPLY_TYPE_UNKNOWN) {
			mtk_charger_plug_out(info);
			mutex_lock(&info->cable_out_lock);
			info->cable_out_cnt = 0;
			mutex_unlock(&info->cable_out_lock);
		}
	} else {
		if (info->chr_type != chr_type)
			mtk_charger_plug_in(info, chr_type);

		if (info->cable_out_cnt > 0) {
			mtk_charger_plug_out(info);
			mtk_charger_plug_in(info, chr_type);
			mutex_lock(&info->cable_out_lock);
			info->cable_out_cnt = 0;
			mutex_unlock(&info->cable_out_lock);
		}
	}

	if (chr_type == POWER_SUPPLY_TYPE_UNKNOWN)
		return false;

	return true;
}

static void charger_send_kpoc_uevent(struct mtk_charger *info)
{
	static bool first_time = true;
	ktime_t ktime_now;

	if (first_time) {
		info->uevent_time_check = ktime_get();
		first_time = false;
	} else {
		ktime_now = ktime_get();
		if ((ktime_ms_delta(ktime_now, info->uevent_time_check) / 1000) >= 60) {
			mtk_chgstat_notify(info);
			info->uevent_time_check = ktime_now;
		}
	}
}

/* TN Begin modified by jirui.li/860702 20241025 CR/EKLAMU-202 */
#if IS_ENABLED(CONFIG_OEM_TINNO_CHARGER)
#define IGNORE_PD_HARDRESET_COUNT	5
#define IGNORE_PD_HARDRESET_INTERVAL_MS	200
#endif
/* TN End modified by jirui.li/860702 20241025 CR/EKLAMU-202 */
static void kpoc_power_off_check(struct mtk_charger *info)
{
	unsigned int boot_mode = info->bootmode;
	int vbus = 0;
	int counter = 0;
/* TN Begin modified by jirui.li/860702 20241025 CR/EKLAMU-202 */
#if IS_ENABLED(CONFIG_OEM_TINNO_CHARGER)
	int i = 0;
#endif
/* TN End modified by jirui.li/860702 20241025 CR/EKLAMU-202 */
	/* 8 = KERNEL_POWER_OFF_CHARGING_BOOT */
	/* 9 = LOW_POWER_OFF_CHARGING_BOOT */
	if (boot_mode == 8 || boot_mode == 9) {
		vbus = get_vbus(info);
		if (vbus >= 0 && vbus < 2500 && !mtk_is_charger_on(info) &&
			!info->ta_hardreset) {
/* TN Begin modified by jirui.li/860702 20241025 CR/EKLAMU-202 */
#if IS_ENABLED(CONFIG_OEM_TINNO_CHARGER)
			/*
			 * We re-check vbus in 1 second for "anti-shake" when vbus below 2500mv
			 * in power-off mode, so it can ignore the abnormal shutdown request which
			 * caused by PD hardreset.
			 */
			while (i < IGNORE_PD_HARDRESET_COUNT) {
				msleep(IGNORE_PD_HARDRESET_INTERVAL_MS);
				vbus = get_vbus(info);
				if (vbus > 2500) {
					chr_err("in KPOC mode anti-shake, vbus=%d, not shutdown!\n", vbus);
					goto out;
				}
				i++;
			}
#endif
/* TN End modified by jirui.li/860702 20241025 CR/EKLAMU-202 */
			chr_err("Unplug Charger/USB in KPOC mode, vbus=%d, shutdown\n", vbus);
			while (1) {
				if (counter >= 20000) {
					chr_err("%s, wait too long\n", __func__);
					kernel_power_off();
					break;
				}
				if (info->is_suspend == false) {
					chr_err("%s, not in suspend, shutdown\n", __func__);
					kernel_power_off();
					break;
				} else {
					chr_err("%s, suspend! cannot shutdown\n", __func__);
					msleep(20);
				}
				counter++;
			}
		}
/* TN Begin modified by jirui.li/860702 20241025 CR/EKLAMU-202 */
#if IS_ENABLED(CONFIG_OEM_TINNO_CHARGER)
out:
#endif
/* TN End modified by jirui.li/860702 20241025 CR/EKLAMU-202 */
		charger_send_kpoc_uevent(info);
	}
}

static void charger_status_check(struct mtk_charger *info)
{
	union power_supply_propval online = {0}, status = {0};
	struct power_supply *chg_psy = NULL;
	int ret = 0;
	bool charging = true;


	chg_psy = power_supply_get_by_name("primary_chg");

	if (IS_ERR_OR_NULL(chg_psy)) {
		chr_err("%s Couldn't get chg_psy\n", __func__);
	} else {
		ret = power_supply_get_property(chg_psy,
			POWER_SUPPLY_PROP_ONLINE, &online);
		if (ret < 0)
			chr_debug("%s: %d\n", __func__, ret);
		ret = power_supply_get_property(chg_psy,
			POWER_SUPPLY_PROP_STATUS, &status);
		if (ret < 0)
			chr_debug("%s: %d\n", __func__, ret);
		if (!online.intval)
			charging = false;
		else {
			if (status.intval == POWER_SUPPLY_STATUS_NOT_CHARGING)
				charging = false;
		}
	}
	if (charging != info->is_charging)
		power_supply_changed(info->psy1);
	info->is_charging = charging;
}

/*TN Begin modified by hao.jia/809321 20240628 CR/EKLAMU-202 */
#if IS_ENABLED(CONFIG_OEM_TURBO_CHARGER)
static int ffc_bat_get_fv(struct mtk_charger *info, int temp_c)
{
	int ffc_max_fv;
	int i = 0;
	int temp = temp_c;
	int num_zones;
	struct ffc_bat_zone *zone;

	//temp = charger->batt_info.batt_temp;
	num_zones = info->num_ffc_zones;
	if (info->ffc_zones == NULL) {
		chr_err("%s: Invalid ffc_zones\n", __func__);
		return 0;
	}
	zone = info->ffc_zones;
	while (i < num_zones && temp > zone[i++].temp);
	zone = i > 0 ? &zone[i - 1] : NULL;

	info->chrg_iterm = zone->ffc_chg_iterm;
	ffc_max_fv = zone->ffc_max_mv; //mV to uV
	chr_info("%s: FFC temp zone:%d, fv:%d mV, chg iterm:%d mA\n", __func__,
		  ((i > 0) ? (i - 1) : 0), ffc_max_fv, info->chrg_iterm);

	return ffc_max_fv;
}

#define TAPER_COUNT	2
#define TAPER_DROP_MA	100
static bool ffc_bat_check_chg_tapered(struct mtk_charger *info,
	int batt_ma, int taper_ma)
{
	bool change_state = false;
	int allowed_fcc, target_ma, rc;

	if (!info) {
		chr_err("%s: called before info valid!\n", __func__);
		return false;
	}

	rc = charger_dev_get_charging_current(info->chg1_dev, &allowed_fcc);
	if (rc < 0)
		chr_err("%s: can't get charging current!\n", __func__);
	else
		allowed_fcc = allowed_fcc / 1000;

	if (allowed_fcc >= taper_ma)
		target_ma = taper_ma;
	else
		target_ma = allowed_fcc - TAPER_DROP_MA;

	target_ma = taper_ma; // force used taper_ma

	chr_info("%s: curr target_ma:%d, batt_ma:%d\n", __func__, target_ma, batt_ma);

	if (batt_ma <= 0) {
		if (info->chrg_taper_cnt >= TAPER_COUNT) {
			change_state = true;
			info->chrg_taper_cnt = 0;
		} else
			info->chrg_taper_cnt++;
	} else {
		if (batt_ma <= target_ma)
			if (info->chrg_taper_cnt >= TAPER_COUNT) {
				change_state = true;
				info->chrg_taper_cnt = 0;
			} else
				info->chrg_taper_cnt++;
		else
			info->chrg_taper_cnt = 0;
	}

	return change_state;
}

#define FFC_RECHG_VOLT_MV	150
static int ffc_bat_check_chg_done(struct mtk_charger *info)
{
	int ret;
	int batt_mv, batt_ma, batt_soc;
	int batt_temp = 0;
	int usb_mv;
	int target_mv;
	int charger_present = 0;

	struct power_supply *bat_psy = NULL;
	struct power_supply *chg_psy = NULL;
	union power_supply_propval prop = {0,};

	bat_psy = power_supply_get_by_name("battery");
	if (IS_ERR_OR_NULL(bat_psy)) {
		chr_info("%s: get bat_psy fail !!!", __func__);
		return -EINVAL;
	}

#if IS_ENABLED(CONFIG_OEM_SWITCH_CHARGER)
	chg_psy = power_supply_get_by_name("primary_chg");
	if (IS_ERR_OR_NULL(chg_psy)) {
		chr_err("%s: get chg psy failed\n", __func__);
	}
#else
	chg_psy = power_supply_get_by_name("mtk_charger_type");
#endif /* CONFIG_OEM_SWITCH_CHARGER */
	if (chg_psy == NULL || IS_ERR(chg_psy)) {
		chr_err("%s: Couldn't get chg_psy\n", __func__);
		ret = -EINVAL;
	} else {
		ret = power_supply_get_property(chg_psy,
				POWER_SUPPLY_PROP_ONLINE, &prop);
		if (ret < 0) {
			chr_err("%s: getting charger online failed(%d)\n", __func__, ret);
			return -EINVAL;
		} else
			charger_present = prop.intval;
	}

	ret = power_supply_get_property(bat_psy,
			POWER_SUPPLY_PROP_VOLTAGE_NOW, &prop);
	if (ret < 0) {
		chr_err("%s: getting batt volt failed(%d)\n", __func__, ret);
		return -EINVAL;
	} else
		batt_mv = prop.intval / 1000;//uV to mV

	ret = power_supply_get_property(bat_psy,
			POWER_SUPPLY_PROP_CURRENT_NOW, &prop);
	if (ret < 0) {
		chr_err("%s: getting batt curr now failed(%d)\n", __func__, ret);
		return -EINVAL;
	} else
		batt_ma = prop.intval / 1000;// uA to mA

	ret = power_supply_get_property(bat_psy,
			POWER_SUPPLY_PROP_CAPACITY, &prop);
	if (ret < 0) {
		chr_err("%s: getting batt capacity failed(%d)\n", __func__, ret);
		return -EINVAL;
	} else
		batt_soc = prop.intval;

	ret = power_supply_get_property(bat_psy,
			POWER_SUPPLY_PROP_TEMP, &prop);
	if (ret < 0) {
		chr_err("%s: getting batt temp failed(%d)\n", __func__, ret);
		return -EINVAL;
	} else
		batt_temp = prop.intval / 10;

	chr_err("%s: charger_present:%d, vbat:%d mV, ibat:%d mA, batt_soc:%d, batt_temp:%d C\n",
				__func__, charger_present, batt_mv, batt_ma, batt_soc, batt_temp);

	usb_mv = get_vbus(info);

	target_mv = ffc_bat_get_fv(info, batt_temp);
	if (target_mv == 0)
		info->target_mv = info->data.battery_cv;
	else
		info->target_mv = target_mv;

	if (!charger_present) {
		info->pres_chrg_step = STEP_NONE;
	} else if (info->pres_chrg_step == STEP_NONE) {
		if ((info->chrg_iterm > 0) || (batt_mv < target_mv))
			info->pres_chrg_step = STEP_NORM;
	} else if (info->pres_chrg_step == STEP_NORM) {
		if (batt_mv < (target_mv  - FFC_RECHG_VOLT_MV / 2)) {
			info->chrg_taper_cnt = 0;
			info->pres_chrg_step = STEP_NORM;
		} else if (ffc_bat_check_chg_tapered(info, batt_ma, info->chrg_iterm) &&
				(batt_soc == BATTERY_CHARGING_FULL_SOC || batt_temp < BATTERY_TEMP_LOW || batt_temp > BATTERY_TEMP_HIGH)&&
				batt_mv > info->target_mv - BATTERY_CV_GAP)
			info->pres_chrg_step = STEP_FULL;
	} else if (info->pres_chrg_step == STEP_FULL) {
		if (batt_mv < (target_mv - FFC_RECHG_VOLT_MV)) {
			info->chrg_taper_cnt = 0;
			info->pres_chrg_step = STEP_NORM;
		}
	}

	chr_err("%s: pres_chrg_step:%d, target_mv:%d\n", __func__, info->pres_chrg_step, target_mv);
	return 0;
}
#endif
/*TN End modified by hao.jia/809321 20240628 CR/EKLAMU-202 */

static char *dump_charger_type(int chg_type, int usb_type)
{
	switch (chg_type) {
	case POWER_SUPPLY_TYPE_UNKNOWN:
		return "none";
	case POWER_SUPPLY_TYPE_USB:
		if (usb_type == POWER_SUPPLY_USB_TYPE_SDP)
			return "usb";
		else
			return "nonstd";
	case POWER_SUPPLY_TYPE_USB_CDP:
		return "usb-h";
	case POWER_SUPPLY_TYPE_USB_DCP:
		return "std";
/* TN Begin modified by hao.jia/809321 20240729 CR/EKLAMU-202 */
#if IS_ENABLED(CONFIG_OEM_TINNO_CHARGER)
	case POWER_SUPPLY_TYPE_USB_NON_STD:
		return "Non-std";
	case POWER_SUPPLY_TYPE_USB_FLOAT:
		return "Float";
	case POWER_SUPPLY_TYPE_USB_QC2:
		return "QC2.0";
	case POWER_SUPPLY_TYPE_USB_QC3:
		return "QC3.0";
	case POWER_SUPPLY_TYPE_USB_QC3P:
		return "QC3+";
	case POWER_SUPPLY_TYPE_USB_PDC:
		return "PDC";
#endif /* CONFIG_OEM_TINNO_CHARGER */
/* TN End modified by hao.jia/809321 20240729 CR/EKLAMU-202 */
	//case POWER_SUPPLY_TYPE_USB_FLOAT:
	//	return "nonstd";
	default:
		return "unknown";
	}
}

static int charger_routine_thread(void *arg)
{
	struct mtk_charger *info = arg;
	unsigned long flags;
	unsigned int init_times = 3;
	static bool is_module_init_done;
	bool is_charger_on;
	int ret;
	int vbat_min = 0;
	int vbat_max = 0;
	int cs_vbat, cs_ibat;
	u32 chg_cv = 0;

	while (1) {
		ret = wait_event_interruptible(info->wait_que,
			(info->charger_thread_timeout == true));
		if (ret < 0) {
			chr_err("%s: wait event been interrupted(%d)\n", __func__, ret);
			continue;
		}

		while (is_module_init_done == false) {
			if (charger_init_algo(info) == true) {
				is_module_init_done = true;
				if (info->charger_unlimited) {
					info->enable_sw_safety_timer = false;
					charger_dev_enable_safety_timer(info->chg1_dev, false);
				}
			}
			else {
				if (init_times > 0) {
					chr_err("retry to init charger\n");
					init_times = init_times - 1;
					msleep(10000);
				} else {
					chr_err("holding to init charger\n");
					msleep(60000);
				}
			}
		}

		mutex_lock(&info->charger_lock);
		spin_lock_irqsave(&info->slock, flags);
		if (!info->charger_wakelock->active)
			__pm_stay_awake(info->charger_wakelock);
		spin_unlock_irqrestore(&info->slock, flags);
		info->charger_thread_timeout = false;

		info->battery_temp = get_battery_temperature(info);
		ret = charger_dev_get_adc(info->chg1_dev,
			ADC_CHANNEL_VBAT, &vbat_min, &vbat_max);
		ret = charger_dev_get_constant_voltage(info->chg1_dev, &chg_cv);

		if (vbat_min != 0)
			vbat_min = vbat_min / 1000;

		/* get data from chgIC first, cs adc is backup */
		get_cs_side_battery_voltage(info, &cs_vbat);
		get_cs_side_battery_current(info, &cs_ibat);

		is_charger_on = mtk_is_charger_on(info);

		if (info->charger_thread_polling == true)
			mtk_charger_start_timer(info);

		check_battery_exist(info);
		check_dynamic_mivr(info);
/* TN Begin modified by xinjun.lu/860715 20240821 CR/EKLAMU-202 */
#if IS_ENABLED(CONFIG_PE50_FFC_SUPPORT)
		if (!IS_ERR_OR_NULL(info->current_alg) && info->current_alg->alg_id == PE5_ID)
			pe50_charger_check_status(info);
#endif
/* TN End modified by xinjun.lu/860715 20240821 CR/EKLAMU-202 */
		charger_check_status(info);
		mtk_check_ta_status(info);
		kpoc_power_off_check(info);

		if (!info->cs_hw_disable)
			chr_err("Vbat=%d vbat2=%d vbats=%d vbus:%d ibus:%d I=%d I2=%d T=%d uisoc:%d type:%s>%s idx:%d ta_stat:%d swchg_ibat:%d cv:%d cmd_pp:%d\n",
				get_battery_voltage(info),
				cs_vbat,
				vbat_min,
				get_vbus(info),
				get_ibus(info),
				get_battery_current(info),
				cs_ibat,
				info->battery_temp,
				get_uisoc(info),
				dump_charger_type(info->chr_type, info->usb_type),
				dump_charger_type(get_charger_type(info), get_usb_type(info)), info->select_adapter_idx,
				info->ta_status[info->select_adapter_idx], get_ibat(info), chg_cv, info->cmd_pp);
		else
			chr_err("Vbat=%d vbats=%d vbus:%d ibus:%d I=%d T=%d uisoc:%d type:%s>%s idx:%d ta_stat:%d swchg_ibat:%d cv:%d cmd_pp:%d\n",
				get_battery_voltage(info),
				vbat_min,
				get_vbus(info),
				get_ibus(info),
				get_battery_current(info),
				info->battery_temp,
				get_uisoc(info),
				dump_charger_type(info->chr_type, info->usb_type),
				dump_charger_type(get_charger_type(info), get_usb_type(info)), info->select_adapter_idx,
				info->ta_status[info->select_adapter_idx], get_ibat(info), chg_cv, info->cmd_pp);

		if (is_disable_charger(info) == false &&
			is_charger_on == true &&
			info->can_charging == true) {
			if (info->algo.do_algorithm)
				info->algo.do_algorithm(info);
			charger_status_check(info);
/* TN Begin modified by hao.jia/809321 20240628 CR/EKLAMU-202 */
#if IS_ENABLED(CONFIG_OEM_TURBO_CHARGER)
			if (turbo_charger_active == true) {
				ret = ffc_bat_check_chg_done(info);
				if (ret < 0)
					chr_err("ffc_bat_check_chg_done ERR(%d)!!!\n", ret);
			}
#endif /* CONFIG_OEM_TURBO_CHARGER */
/* TN End modified by hao.jia/809321 20240628 CR/EKLAMU-202 */
		} else {
			chr_debug("disable charging %d %d %d\n",
			    is_disable_charger(info), is_charger_on, info->can_charging);
		}
		if (info->bootmode != 1 && info->bootmode != 2 && info->bootmode != 4
			&& info->bootmode != 8 && info->bootmode != 9)
			smart_charging(info);
		spin_lock_irqsave(&info->slock, flags);
		__pm_relax(info->charger_wakelock);
		spin_unlock_irqrestore(&info->slock, flags);
		chr_debug("%s end , %d\n",
			__func__, info->charger_thread_timeout);
		mutex_unlock(&info->charger_lock);

		if (info->enable_boot_volt &&
			ktime_get_seconds() > RESET_BOOT_VOLT_TIME &&
			!info->reset_boot_volt_times) {
			ret = charger_dev_set_boot_volt_times(info->chg1_dev, 0);
			if (ret < 0)
				chr_err("reset boot_battery_voltage times fails %d\n", ret);
			else {
				info->reset_boot_volt_times = 1;
				chr_err("reset boot_battery_voltage times\n");
			}
		}
	}

	return 0;
}


#ifdef CONFIG_PM
static int charger_pm_event(struct notifier_block *notifier,
			unsigned long pm_event, void *unused)
{
	ktime_t ktime_now;
	struct timespec64 now;
	struct mtk_charger *info;

	info = container_of(notifier,
		struct mtk_charger, pm_notifier);

	switch (pm_event) {
	case PM_SUSPEND_PREPARE:
		info->is_suspend = true;
		chr_debug("%s: enter PM_SUSPEND_PREPARE\n", __func__);
		break;
	case PM_POST_SUSPEND:
		info->is_suspend = false;
		chr_debug("%s: enter PM_POST_SUSPEND\n", __func__);
		ktime_now = ktime_get_boottime();
		now = ktime_to_timespec64(ktime_now);

		if (timespec64_compare(&now, &info->endtime) >= 0 &&
			info->endtime.tv_sec != 0 &&
			info->endtime.tv_nsec != 0) {
			chr_err("%s: alarm timeout, wake up charger\n",
				__func__);
			__pm_relax(info->charger_wakelock);
			info->endtime.tv_sec = 0;
			info->endtime.tv_nsec = 0;
			_wake_up_charger(info);
		}
		break;
	default:
		break;
	}
	return NOTIFY_DONE;
}
#endif /* CONFIG_PM */

static enum alarmtimer_restart
	mtk_charger_alarm_timer_func(struct alarm *alarm, ktime_t now)
{
	struct mtk_charger *info =
	container_of(alarm, struct mtk_charger, charger_timer);

	if (info->is_suspend == false) {
		_wake_up_charger(info);
	} else {
		__pm_stay_awake(info->charger_wakelock);
	}

	return ALARMTIMER_NORESTART;
}

static void mtk_charger_init_timer(struct mtk_charger *info)
{
	alarm_init(&info->charger_timer, ALARM_BOOTTIME,
			mtk_charger_alarm_timer_func);
	mtk_charger_start_timer(info);

}

static int mtk_charger_setup_files(struct platform_device *pdev)
{
	int ret = 0;
	struct proc_dir_entry *battery_dir = NULL, *entry = NULL;
	struct mtk_charger *info = platform_get_drvdata(pdev);

	ret = device_create_file(&(pdev->dev), &dev_attr_sw_jeita);
	if (ret)
		goto _out;

	ret = device_create_file(&(pdev->dev), &dev_attr_sw_ovp_threshold);
	if (ret)
		goto _out;

	ret = device_create_file(&(pdev->dev), &dev_attr_chr_type);
	if (ret)
		goto _out;

	ret = device_create_file(&(pdev->dev), &dev_attr_enable_meta_current_limit);
	if (ret)
		goto _out;

	ret = device_create_file(&(pdev->dev), &dev_attr_cs_heatlim);
	if (ret)
		goto _out;

	ret = device_create_file(&(pdev->dev), &dev_attr_cs_para_mode);
	if (ret)
		goto _out;

	ret = device_create_file(&(pdev->dev), &dev_attr_fast_chg_indicator);
	if (ret)
		goto _out;

	ret = device_create_file(&(pdev->dev), &dev_attr_Charging_mode);
	if (ret)
		goto _out;

	ret = device_create_file(&(pdev->dev), &dev_attr_ta_type);
	if (ret)
		goto _out;

	ret = device_create_file(&(pdev->dev), &dev_attr_High_voltage_chg_enable);
	if (ret)
		goto _out;

	ret = device_create_file(&(pdev->dev), &dev_attr_Rust_detect);
	if (ret)
		goto _out;

	ret = device_create_file(&(pdev->dev), &dev_attr_Thermal_throttle);
	if (ret)
		goto _out;

	ret = device_create_file(&(pdev->dev), &dev_attr_alg_new_arbitration);
	if (ret)
		goto _out;

	ret = device_create_file(&(pdev->dev), &dev_attr_alg_unchangeable);
	if (ret)
		goto _out;

	ret = device_create_file(&(pdev->dev), &dev_attr_vbat_mon);
	if (ret)
		goto _out;

	ret = device_create_file(&(pdev->dev), &dev_attr_Pump_Express);
	if (ret)
		goto _out;

	ret = device_create_file(&(pdev->dev), &dev_attr_ADC_Charger_Voltage);
	if (ret)
		goto _out;
	ret = device_create_file(&(pdev->dev), &dev_attr_ADC_Charging_Current);
	if (ret)
		goto _out;
	ret = device_create_file(&(pdev->dev), &dev_attr_input_current);
	if (ret)
		goto _out;

	ret = device_create_file(&(pdev->dev), &dev_attr_charger_log_level);
	if (ret)
		goto _out;
	/* Battery warning */
	ret = device_create_file(&(pdev->dev), &dev_attr_BatteryNotify);
	if (ret)
		goto _out;

	/* sysfs node */
	ret = device_create_file(&(pdev->dev), &dev_attr_enable_sc);
	if (ret)
		goto _out;

	ret = device_create_file(&(pdev->dev), &dev_attr_sc_stime);
	if (ret)
		goto _out;

	ret = device_create_file(&(pdev->dev), &dev_attr_sc_etime);
	if (ret)
		goto _out;

	ret = device_create_file(&(pdev->dev), &dev_attr_sc_tuisoc);
	if (ret)
		goto _out;

	ret = device_create_file(&(pdev->dev), &dev_attr_sc_ibat_limit);
	if (ret)
		goto _out;

	ret = device_create_file(&(pdev->dev), &dev_attr_enable_power_path);
	if (ret)
		goto _out;

/* TN Begin modified by hao.jia/809321 20240718 CR/EKLAMU-202 */
#if IS_ENABLED(CONFIG_OEM_TINNO_CHARGER) && IS_ENABLED(CONFIG_FACTORY_BUILD)
	ret = device_create_file(&(pdev->dev), &dev_attr_factory_enable_switch_charger);
	if (ret)
		goto _out;
	ret = device_create_file(&(pdev->dev), &dev_attr_factory_enable_pump_charger);
	if (ret)
		goto _out;
/* TN Begin modified by jirui.li/860702 20240722 CR/EKLAMU-620 */
	ret = device_create_file(&(pdev->dev), &dev_attr_factory_charging_limit_soc);
	if (ret)
		goto _out;
/* TN End modified by jirui.li/860702 20240722 CR/EKLAMU-620 */
#endif /* CONFIG_OEM_TINNO_CHARGER && CONFIG_FACTORY_BUILD */

#if IS_ENABLED(CONFIG_OEM_TINNO_CHARGER)
	ret = device_create_file(&(pdev->dev), &dev_attr_enable_hiz);
	if (ret)
		goto _out;

	ret = device_create_file(&(pdev->dev), &dev_attr_enable_charger);
	if (ret)
		goto _out;
	ret = device_create_file(&(pdev->dev), &dev_attr_disable_thermal_current_limit);
	if (ret)
		goto _out;
/* TN Begin modified by jirui.li/860702 20240814 CR/EKLAMU-1339 */
	ret = device_create_file(&(pdev->dev), &dev_attr_battery_protection_mode);
	if (ret)
		goto _out;
/* TN End modified by jirui.li/860702 20240814 CR/EKLAMU-1339 */
/* TN Begin modified by jirui.li/860702 20240814 CR/EKLAMU-30 */
#if IS_ENABLED(CONFIG_OEM_TURBO_CHARGER) || IS_ENABLED(CONFIG_PE50_FFC_SUPPORT)
	ret = device_create_file(&(pdev->dev), &dev_attr_turbo_power_mode);
	if (ret)
		goto _out;
#endif
/* TN End modified by jirui.li/860702 20240814 CR/EKLAMU-30 */
/* TN Begin modified by jirui.li/860702 20240814 CR/EKLAMU-202 */
	ret = device_create_file(&(pdev->dev), &dev_attr_demo_mode_limit);
	if (ret)
		goto _out;
/* TN End modified by jirui.li/860702 20240814 CR/EKLAMU-202 */
#endif /* CONFIG_OEM_TINNO_CHARGER */
/* TN End modified by hao.jia/809321 20240718 CR/EKLAMU-202 */

	battery_dir = proc_mkdir("mtk_battery_cmd", NULL);
	if (!battery_dir) {
		chr_err("%s: mkdir /proc/mtk_battery_cmd failed\n", __func__);
		return -ENOMEM;
	}

	entry = proc_create_data("current_cmd", 0644, battery_dir,
			&mtk_chg_current_cmd_fops, info);
	if (!entry) {
		ret = -ENODEV;
		goto fail_procfs;
	}
	entry = proc_create_data("en_power_path", 0644, battery_dir,
			&mtk_chg_en_power_path_fops, info);
	if (!entry) {
		ret = -ENODEV;
		goto fail_procfs;
	}
	entry = proc_create_data("en_safety_timer", 0644, battery_dir,
			&mtk_chg_en_safety_timer_fops, info);
	if (!entry) {
		ret = -ENODEV;
		goto fail_procfs;
	}
	entry = proc_create_data("set_cv", 0644, battery_dir,
			&mtk_chg_set_cv_fops, info);
	if (!entry) {
		ret = -ENODEV;
		goto fail_procfs;
	}

	return 0;

fail_procfs:
	remove_proc_subtree("mtk_battery_cmd", NULL);
_out:
	return ret;
}

void mtk_charger_get_atm_mode(struct mtk_charger *info)
{
	char atm_str[64] = {0};
	char *ptr = NULL, *ptr_e = NULL;
	char keyword[] = "androidboot.atm=";
	int size = 0;

	ptr = strstr(chg_get_cmd(), keyword);
	if (ptr != 0) {
		ptr_e = strstr(ptr, " ");
		if (ptr_e == 0)
			goto end;

		size = ptr_e - (ptr + strlen(keyword));
		if (size <= 0)
			goto end;
		strncpy(atm_str, ptr + strlen(keyword), size);
		atm_str[size] = '\0';
		chr_err("%s: atm_str: %s\n", __func__, atm_str);

		if (!strncmp(atm_str, "enable", strlen("enable")))
			info->atm_enabled = true;
	}
end:
	chr_err("%s: atm_enabled = %d\n", __func__, info->atm_enabled);
}

static int psy_charger_property_is_writeable(struct power_supply *psy,
					       enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		return 1;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		return 1;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		return 1;
	default:
		return 0;
	}
}

static const enum power_supply_usb_type charger_psy_usb_types[] = {
	POWER_SUPPLY_USB_TYPE_UNKNOWN,
	POWER_SUPPLY_USB_TYPE_SDP,
	POWER_SUPPLY_USB_TYPE_DCP,
	POWER_SUPPLY_USB_TYPE_CDP,
	POWER_SUPPLY_USB_TYPE_PD,
	POWER_SUPPLY_USB_TYPE_PD_PPS,
};

static const enum power_supply_property charger_psy_properties[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	POWER_SUPPLY_PROP_VOLTAGE_BOOT,
	POWER_SUPPLY_PROP_USB_TYPE,
};

static int psy_charger_get_property(struct power_supply *psy,
	enum power_supply_property psp, union power_supply_propval *val)
{
	struct mtk_charger *info;
	struct charger_device *chg;
	int ret = 0, chg_vbat = 0, vbat_max = 0, idx = 0;
	struct chg_alg_device *alg = NULL;

	info = (struct mtk_charger *)power_supply_get_drvdata(psy);
	if (info == NULL) {
		chr_err("%s: get info failed\n", __func__);
		return -EINVAL;
	}
	chr_debug("%s psp:%d\n", __func__, psp);

	if (info->psy1 == psy) {
		chg = info->chg1_dev;
		idx = CHG1_SETTING;
	} else if (info->psy2 == psy) {
		chg = info->chg2_dev;
		idx = CHG2_SETTING;
	} else if (info->psy_dvchg1 == psy) {
		chg = info->dvchg1_dev;
		idx = DVCHG1_SETTING;
	} else if (info->psy_dvchg2 == psy) {
		chg = info->dvchg2_dev;
		idx = DVCHG2_SETTING;
	} else if (info->psy_hvdvchg1 == psy) {
		chg = info->hvdvchg1_dev;
		idx = HVDVCHG1_SETTING;
	} else if (info->psy_hvdvchg2 == psy) {
		chg = info->hvdvchg2_dev;
		idx = HVDVCHG2_SETTING;
	} else {
		chr_err("%s fail\n", __func__);
		return 0;
	}

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		if (idx == DVCHG1_SETTING || idx == DVCHG2_SETTING ||
		    idx == HVDVCHG1_SETTING || idx == HVDVCHG2_SETTING) {
			val->intval = false;
			alg = get_chg_alg_by_name("pe5");
			if (alg == NULL)
				chr_err("get pe5 fail\n");
			else {
				ret = chg_alg_is_algo_ready(alg);
				if (ret == ALG_RUNNING)
					val->intval = true;
			}
			break;
		}

		val->intval = is_charger_exist(info);
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		if (chg != NULL)
			val->intval = true;
		else
			val->intval = false;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		val->intval = info->enable_hv_charging;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = get_vbus(info);
		break;

	case POWER_SUPPLY_PROP_TEMP:
		val->intval = info->chg_data[idx].junction_temp_max * 10;
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		val->intval =
			info->chg_data[idx].thermal_charging_current_limit;
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		val->intval =
			info->chg_data[idx].thermal_input_current_limit;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_BOOT:
		val->intval = get_charger_zcv(info, chg);
		break;
	case POWER_SUPPLY_PROP_USB_TYPE:
		chr_debug("not yet\n");
		break;
	case POWER_SUPPLY_PROP_CHARGE_NOW:
		ret = charger_dev_get_adc(info->chg1_dev,
			ADC_CHANNEL_VBAT, &chg_vbat, &vbat_max);
		val->intval = chg_vbat;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int mtk_charger_enable_power_path(struct mtk_charger *info,
	int idx, bool en)
{
	int ret = 0;
	bool is_en = true;
	struct charger_device *chg_dev = NULL;

	if (!info)
		return -EINVAL;

	switch (idx) {
	case CHG1_SETTING:
		chg_dev = get_charger_by_name("primary_chg");
		break;
	case CHG2_SETTING:
		chg_dev = get_charger_by_name("secondary_chg");
		break;
	default:
		return -EINVAL;
	}

	if (IS_ERR_OR_NULL(chg_dev)) {
		chr_err("%s: chg_dev not found\n", __func__);
		return -EINVAL;
	}

	mutex_lock(&info->pp_lock[idx]);
	info->enable_pp[idx] = en;

	if (info->force_disable_pp[idx])
		goto out;

	ret = charger_dev_is_powerpath_enabled(chg_dev, &is_en);
	if (ret < 0) {
		chr_err("%s: get is power path enabled failed\n", __func__);
		goto out;
	}
	if (is_en == en) {
		chr_err("%s: power path is already en = %d\n", __func__, is_en);
		goto out;
	}

	chr_info("%s: enable power path = %d\n", __func__, en);
	ret = charger_dev_enable_powerpath(chg_dev, en);
out:
	mutex_unlock(&info->pp_lock[idx]);
	return ret;
}

static int mtk_charger_force_disable_power_path(struct mtk_charger *info,
	int idx, bool disable)
{
	int ret = 0;
	struct charger_device *chg_dev = NULL;

	if (!info)
		return -EINVAL;

	switch (idx) {
	case CHG1_SETTING:
		chg_dev = get_charger_by_name("primary_chg");
		break;
	case CHG2_SETTING:
		chg_dev = get_charger_by_name("secondary_chg");
		break;
	default:
		return -EINVAL;
	}

	if (IS_ERR_OR_NULL(chg_dev)) {
		chr_err("%s: chg_dev not found\n", __func__);
		return -EINVAL;
	}

	mutex_lock(&info->pp_lock[idx]);

	if (disable == info->force_disable_pp[idx])
		goto out;

	info->force_disable_pp[idx] = disable;
	ret = charger_dev_enable_powerpath(chg_dev,
		info->force_disable_pp[idx] ? false : info->enable_pp[idx]);
out:
	mutex_unlock(&info->pp_lock[idx]);
	return ret;
}

static int psy_charger_set_property(struct power_supply *psy,
			enum power_supply_property psp,
			const union power_supply_propval *val)
{
	struct mtk_charger *info;
	int idx;

	chr_err("%s: prop:%d %d\n", __func__, psp, val->intval);

	info = (struct mtk_charger *)power_supply_get_drvdata(psy);
	if (info == NULL) {
		chr_err("%s: failed to get info\n", __func__);
		return -EINVAL;
	}

	if (info->psy1 == psy)
		idx = CHG1_SETTING;
	else if (info->psy2 == psy)
		idx = CHG2_SETTING;
	else if (info->psy_dvchg1 == psy)
		idx = DVCHG1_SETTING;
	else if (info->psy_dvchg2 == psy)
		idx = DVCHG2_SETTING;
	else if (info->psy_hvdvchg1 == psy)
		idx = HVDVCHG1_SETTING;
	else if (info->psy_hvdvchg2 == psy)
		idx = HVDVCHG2_SETTING;
	else {
		chr_err("%s fail\n", __func__);
		return 0;
	}

	switch (psp) {
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		if (val->intval > 0)
			info->enable_hv_charging = true;
		else
			info->enable_hv_charging = false;
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
/*TN Begin modified by hao.jia/809321 20240628 CR/EKLAMU-202 */
#if IS_ENABLED(CONFIG_OEM_TURBO_CHARGER)
		g_thermal_charging_current_limit = val->intval;
#endif /* CONFIG_OEM_TURBO_CHARGER */
/*TN End modified by hao.jia/809321 20240628 CR/EKLAMU-202 */
		info->chg_data[idx].thermal_charging_current_limit =
			val->intval & UNLIMIT_CURRENT_MASK ?
			-1 : val->intval;
/* TN Begin modified by xinjun.lu/860715 20240719 CR/EKLAMU-202 */
#if IS_ENABLED(CONFIG_OEM_TINNO_CHARGER)
		if (info->disable_thermal_current_limit) {
			info->chg_data[idx].thermal_charging_current_limit = -1;
#if IS_ENABLED(CONFIG_OEM_TURBO_CHARGER)
			g_thermal_charging_current_limit = -1;
#endif
		}
#endif
/* TN End modified by xinjun.lu/860715 20240719 CR/EKLAMU-202 */
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		if (val->intval & USB_CURRENT_MASK) {
			if (info->en_cts_mode)
				info->chg_data[idx].usb_input_current_limit =
				val->intval & UNLIMIT_CURRENT_MASK ?
				-1 : (val->intval & ~(USB_CURRENT_MASK)) * 1000;
		} else {
			info->chg_data[idx].thermal_input_current_limit =
			val->intval & UNLIMIT_CURRENT_MASK ?
			-1 : val->intval;
		}

/* TN Begin modified by xinjun.lu/860715 20240719 CR/EKLAMU-202 */
#if IS_ENABLED(CONFIG_OEM_TINNO_CHARGER)
		if (info->disable_thermal_current_limit) {
			info->chg_data[idx].thermal_input_current_limit = -1;
		}
#endif
/* TN End modified by xinjun.lu/860715 20240719 CR/EKLAMU-202 */
		break;
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
		if (val->intval > 0)
			mtk_charger_enable_power_path(info, idx, false);
		else
			mtk_charger_enable_power_path(info, idx, true);
		break;
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT_MAX:
		if (val->intval > 0)
			mtk_charger_force_disable_power_path(info, idx, true);
		else
			mtk_charger_force_disable_power_path(info, idx, false);
		break;
	default:
		return -EINVAL;
	}
	_wake_up_charger(info);

	return 0;
}

static void mtk_charger_external_power_changed(struct power_supply *psy)
{
	struct mtk_charger *info;
	union power_supply_propval prop = {0};
	union power_supply_propval prop2 = {0};
	union power_supply_propval vbat0 = {0};
	struct power_supply *chg_psy = NULL;
	int ret = 0;

	info = (struct mtk_charger *)power_supply_get_drvdata(psy);
	if (info == NULL) {
		pr_notice("%s: failed to get info\n", __func__);
		return;
	}
	chg_psy = info->chg_psy;

	if (IS_ERR_OR_NULL(chg_psy)) {
		pr_notice("%s Couldn't get chg_psy\n", __func__);

		chg_psy = power_supply_get_by_name("primary_chg");

		info->chg_psy = chg_psy;
	} else {
		ret = power_supply_get_property(chg_psy,
			POWER_SUPPLY_PROP_ONLINE, &prop);
		if (ret < 0)
			chr_debug("%s: %d\n", __func__, ret);
		ret = power_supply_get_property(chg_psy,
			POWER_SUPPLY_PROP_USB_TYPE, &prop2);
		if (ret < 0)
			chr_debug("%s: %d\n", __func__, ret);
		ret = power_supply_get_property(chg_psy,
			POWER_SUPPLY_PROP_ENERGY_EMPTY, &vbat0);
		if (ret < 0)
			chr_debug("%s: %d\n", __func__, ret);
	}

	if (info->vbat0_flag != vbat0.intval) {
		if (vbat0.intval) {
			info->enable_vbat_mon = false;
			charger_dev_enable_6pin_battery_charging(info->chg1_dev, false);
		} else
			info->enable_vbat_mon = info->enable_vbat_mon_bak;

		info->vbat0_flag = vbat0.intval;
	}

	pr_notice("%s event, name:%s online:%d type:%d vbus:%d\n", __func__,
		psy->desc->name, prop.intval, prop2.intval,
		get_vbus(info));

	_wake_up_charger(info);
}

int notify_adapter_event(struct notifier_block *notifier,
			unsigned long evt, void *val)
{
	struct mtk_charger *pinfo = NULL;
	u32 boot_mode = 0;
	bool report_psy = true;
	int index = 0;
	int i = 0;
	struct info_notifier_block *ta_nb;

	ta_nb = container_of(notifier, struct info_notifier_block, nb);
	pinfo = ta_nb->info;
	pinfo->ta_hardreset = false;
	index = ta_nb - pinfo->ta_nb;
	chr_err("%s %lu, %d\n", __func__, evt, index);
	boot_mode = pinfo->bootmode;

	switch (evt) {
	case TA_DETACH:
		mutex_lock(&pinfo->ta_lock);
		chr_err("TA Notify Detach\n");
		pinfo->ta_status[index] = TA_DETACH;
		mutex_unlock(&pinfo->ta_lock);
		mtk_chg_alg_notify_call(pinfo, EVT_DETACH, 0);
		_wake_up_charger(pinfo);
		/* reset PE40 */
		break;

	case TA_ATTACH:
		mutex_lock(&pinfo->ta_lock);
		chr_err("TA Notify Attach\n");
		pinfo->ta_status[index] = TA_ATTACH;
		mutex_unlock(&pinfo->ta_lock);
		_wake_up_charger(pinfo);
		/* reset PE40 */
		break;

	case TA_DETECT_FAIL:
		mutex_lock(&pinfo->ta_lock);
		chr_err("TA Notify Detect Fail\n");
		pinfo->ta_status[index] = TA_DETECT_FAIL;
		mutex_unlock(&pinfo->ta_lock);
		_wake_up_charger(pinfo);
		/* reset PE50 */
		break;

	case TA_HARD_RESET:
		mutex_lock(&pinfo->ta_lock);
		chr_err("TA Notify Hard Reset\n");
		pinfo->ta_status[index] = TA_HARD_RESET;
		pinfo->ta_hardreset = true;
		mutex_unlock(&pinfo->ta_lock);
		_wake_up_charger(pinfo);
		/* PD is ready */
		break;

	case TA_SOFT_RESET:
		mutex_lock(&pinfo->ta_lock);
		chr_err("TA Notify Soft Reset\n");
		pinfo->ta_status[index] = TA_SOFT_RESET;
		mutex_unlock(&pinfo->ta_lock);
		_wake_up_charger(pinfo);
		/* PD30 is ready */
		break;

	case MTK_TYPEC_WD_STATUS:
		chr_err("wd status = %d\n", *(bool *)val);
		pinfo->water_detected = *(bool *)val;
		if (pinfo->water_detected == true) {
			pinfo->notify_code |= CHG_TYPEC_WD_STATUS;
			pinfo->record_water_detected = true;
			if (boot_mode == 8 || boot_mode == 9)
				pinfo->enable_hv_charging = false;
		} else {
			pinfo->notify_code &= ~CHG_TYPEC_WD_STATUS;
			if (boot_mode == 8 || boot_mode == 9)
				pinfo->enable_hv_charging = true;
		}
		mtk_chgstat_notify(pinfo);
		report_psy = boot_mode == 8 || boot_mode == 9;
		break;
	case MTK_SINK_VBUS:
		if (pinfo->en_cts_mode) {
			for (i = 0; i < CHGS_SETTING_MAX; i++)
				pinfo->chg_data[i].pd_input_current_limit = *(int *)val * 1000;
			// charger_dev_set_input_current(pinfo->chg1_dev, *(int *)val);
			if ((*(int *)val) < 100) {
				if (pinfo->power_path_en) {
					mtk_charger_force_disable_power_path(pinfo, CHG1_SETTING,
					true);	// for pdtest, speed up job
					pinfo->power_path_en = false;
				}
				pinfo->en_power_path = false;
			}
			chr_err("mtk get sink vbus ma = %d, pp= %d\n", *(int *)val,
			pinfo->power_path_en);
			_wake_up_charger(pinfo);
		}
		break;
	}
	chr_debug("%s: evt: pd:%d, ufcs:%d\n", __func__,
	pinfo->ta_status[PD], pinfo->ta_status[UFCS]);

	if (report_psy)
		power_supply_changed(pinfo->psy1);
	return NOTIFY_DONE;
}

int chg_alg_event(struct notifier_block *notifier,
			unsigned long event, void *data)
{
	chr_err("%s: evt:%lu\n", __func__, event);

	return NOTIFY_DONE;
}

static char *mtk_charger_supplied_to[] = {
	"battery"
};

static int mtk_charger_probe(struct platform_device *pdev)
{
	struct mtk_charger *info = NULL;
	int i;
	char *name = NULL;

	chr_err("%s: starts\n", __func__);

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;
	platform_set_drvdata(pdev, info);
	info->pdev = pdev;

	mtk_charger_parse_dt(info, &pdev->dev);

	mutex_init(&info->cable_out_lock);
	mutex_init(&info->charger_lock);
	mutex_init(&info->pd_lock);
	mutex_init(&info->ta_lock);
	for (i = 0; i < CHG2_SETTING + 1; i++) {
		mutex_init(&info->pp_lock[i]);
		info->force_disable_pp[i] = false;
		info->enable_pp[i] = true;
	}
	name = devm_kasprintf(&pdev->dev, GFP_KERNEL, "%s",
		"charger suspend wakelock");
	info->charger_wakelock =
		wakeup_source_register(NULL, name);
	spin_lock_init(&info->slock);

	init_waitqueue_head(&info->wait_que);
	info->polling_interval = CHARGING_INTERVAL;
	mtk_charger_init_timer(info);
#ifdef CONFIG_PM
	if (register_pm_notifier(&info->pm_notifier)) {
		chr_err("%s: register pm failed\n", __func__);
		return -ENODEV;
	}
	info->pm_notifier.notifier_call = charger_pm_event;
#endif /* CONFIG_PM */
	srcu_init_notifier_head(&info->evt_nh);
	mtk_charger_setup_files(pdev);
	mtk_charger_get_atm_mode(info);

	for (i = 0; i < CHGS_SETTING_MAX; i++) {
		info->chg_data[i].thermal_charging_current_limit = -1;
		info->chg_data[i].thermal_input_current_limit = -1;
		info->chg_data[i].usb_input_current_limit = -1;
		info->chg_data[i].pd_input_current_limit = -1;
		info->chg_data[i].input_current_limit_by_aicl = -1;
	}
	info->enable_hv_charging = true;

/* TN Begin modified by jirui.li/860702 20240724 CR/EKLAMU-620 */
#if IS_ENABLED(CONFIG_OEM_TINNO_CHARGER) && IS_ENABLED(CONFIG_FACTORY_BUILD)
	info->enable_factory_charging_test = false;
	info->factory_enable_switch_charger = false;
	info->factory_enable_pump_charger = false;
	info->factory_charging_limit_soc = FACTORY_CHARGING_LIMIT_SOC_DEFAULT;
#endif /* CONFIG_OEM_TINNO_CHARGER && CONFIG_FACTORY_BUILD */
/* TN Begin modified by jirui.li/860702 20240724 CR/EKLAMU-620 */
	info->psy_desc1.name = "mtk-master-charger";
	info->psy_desc1.type = POWER_SUPPLY_TYPE_UNKNOWN;
	info->psy_desc1.usb_types = charger_psy_usb_types;
	info->psy_desc1.num_usb_types = ARRAY_SIZE(charger_psy_usb_types);
	info->psy_desc1.properties = charger_psy_properties;
	info->psy_desc1.num_properties = ARRAY_SIZE(charger_psy_properties);
	info->psy_desc1.get_property = psy_charger_get_property;
	info->psy_desc1.set_property = psy_charger_set_property;
	info->psy_desc1.property_is_writeable =
			psy_charger_property_is_writeable;
	info->psy_desc1.external_power_changed =
		mtk_charger_external_power_changed;
	info->psy_cfg1.drv_data = info;
	info->psy_cfg1.supplied_to = mtk_charger_supplied_to;
	info->psy_cfg1.num_supplicants = ARRAY_SIZE(mtk_charger_supplied_to);
	info->psy1 = power_supply_register(&pdev->dev, &info->psy_desc1,
			&info->psy_cfg1);


	info->chg_psy = power_supply_get_by_name("primary_chg");

	if (IS_ERR_OR_NULL(info->chg_psy))
		chr_err("%s: devm power fail to get chg_psy\n", __func__);

#if !IS_ENABLED(CONFIG_MTK_PLAT_POWER_6893)
	info->bc12_psy = power_supply_get_by_name("primary_chg");
	if (IS_ERR_OR_NULL(info->bc12_psy))
		chr_err("%s: devm power fail to get bc12_psy\n", __func__);
#endif

	info->bat_psy = devm_power_supply_get_by_phandle(&pdev->dev,
		"gauge");
	if (IS_ERR_OR_NULL(info->bat_psy))
		chr_err("%s: devm power fail to get bat_psy\n", __func__);

	if (IS_ERR(info->psy1))
		chr_err("register psy1 fail:%ld\n",
			PTR_ERR(info->psy1));

	info->psy_desc2.name = "mtk-slave-charger";
	info->psy_desc2.type = POWER_SUPPLY_TYPE_UNKNOWN;
	info->psy_desc2.usb_types = charger_psy_usb_types;
	info->psy_desc2.num_usb_types = ARRAY_SIZE(charger_psy_usb_types);
	info->psy_desc2.properties = charger_psy_properties;
	info->psy_desc2.num_properties = ARRAY_SIZE(charger_psy_properties);
	info->psy_desc2.get_property = psy_charger_get_property;
	info->psy_desc2.set_property = psy_charger_set_property;
	info->psy_desc2.property_is_writeable =
			psy_charger_property_is_writeable;
	info->psy_cfg2.drv_data = info;
	info->psy2 = power_supply_register(&pdev->dev, &info->psy_desc2,
			&info->psy_cfg2);

	if (IS_ERR(info->psy2))
		chr_err("register psy2 fail:%ld\n",
			PTR_ERR(info->psy2));

	info->psy_dvchg_desc1.name = "mtk-mst-div-chg";
	info->psy_dvchg_desc1.type = POWER_SUPPLY_TYPE_UNKNOWN;
	info->psy_dvchg_desc1.usb_types = charger_psy_usb_types;
	info->psy_dvchg_desc1.num_usb_types = ARRAY_SIZE(charger_psy_usb_types);
	info->psy_dvchg_desc1.properties = charger_psy_properties;
	info->psy_dvchg_desc1.num_properties =
		ARRAY_SIZE(charger_psy_properties);
	info->psy_dvchg_desc1.get_property = psy_charger_get_property;
	info->psy_dvchg_desc1.set_property = psy_charger_set_property;
	info->psy_dvchg_desc1.property_is_writeable =
		psy_charger_property_is_writeable;
	info->psy_dvchg_cfg1.drv_data = info;
	info->psy_dvchg1 = power_supply_register(&pdev->dev,
						 &info->psy_dvchg_desc1,
						 &info->psy_dvchg_cfg1);
	if (IS_ERR(info->psy_dvchg1))
		chr_err("register psy dvchg1 fail:%ld\n",
			PTR_ERR(info->psy_dvchg1));

	info->psy_dvchg_desc2.name = "mtk-slv-div-chg";
	info->psy_dvchg_desc2.type = POWER_SUPPLY_TYPE_UNKNOWN;
	info->psy_dvchg_desc2.usb_types = charger_psy_usb_types;
	info->psy_dvchg_desc2.num_usb_types = ARRAY_SIZE(charger_psy_usb_types);
	info->psy_dvchg_desc2.properties = charger_psy_properties;
	info->psy_dvchg_desc2.num_properties =
		ARRAY_SIZE(charger_psy_properties);
	info->psy_dvchg_desc2.get_property = psy_charger_get_property;
	info->psy_dvchg_desc2.set_property = psy_charger_set_property;
	info->psy_dvchg_desc2.property_is_writeable =
		psy_charger_property_is_writeable;
	info->psy_dvchg_cfg2.drv_data = info;
	info->psy_dvchg2 = power_supply_register(&pdev->dev,
						 &info->psy_dvchg_desc2,
						 &info->psy_dvchg_cfg2);
	if (IS_ERR(info->psy_dvchg2))
		chr_err("register psy dvchg2 fail:%ld\n",
			PTR_ERR(info->psy_dvchg2));

	info->psy_hvdvchg_desc1.name = "mtk-mst-hvdiv-chg";
	info->psy_hvdvchg_desc1.type = POWER_SUPPLY_TYPE_UNKNOWN;
	info->psy_hvdvchg_desc1.usb_types = charger_psy_usb_types;
	info->psy_hvdvchg_desc1.num_usb_types = ARRAY_SIZE(charger_psy_usb_types);
	info->psy_hvdvchg_desc1.properties = charger_psy_properties;
	info->psy_hvdvchg_desc1.num_properties =
					     ARRAY_SIZE(charger_psy_properties);
	info->psy_hvdvchg_desc1.get_property = psy_charger_get_property;
	info->psy_hvdvchg_desc1.set_property = psy_charger_set_property;
	info->psy_hvdvchg_desc1.property_is_writeable =
					      psy_charger_property_is_writeable;
	info->psy_hvdvchg_cfg1.drv_data = info;
	info->psy_hvdvchg1 = power_supply_register(&pdev->dev,
						   &info->psy_hvdvchg_desc1,
						   &info->psy_hvdvchg_cfg1);
	if (IS_ERR(info->psy_hvdvchg1))
		chr_err("register psy hvdvchg1 fail:%ld\n",
					PTR_ERR(info->psy_hvdvchg1));

	info->psy_hvdvchg_desc2.name = "mtk-slv-hvdiv-chg";
	info->psy_hvdvchg_desc2.type = POWER_SUPPLY_TYPE_UNKNOWN;
	info->psy_hvdvchg_desc2.usb_types = charger_psy_usb_types;
	info->psy_hvdvchg_desc2.num_usb_types = ARRAY_SIZE(charger_psy_usb_types);
	info->psy_hvdvchg_desc2.properties = charger_psy_properties;
	info->psy_hvdvchg_desc2.num_properties =
					     ARRAY_SIZE(charger_psy_properties);
	info->psy_hvdvchg_desc2.get_property = psy_charger_get_property;
	info->psy_hvdvchg_desc2.set_property = psy_charger_set_property;
	info->psy_hvdvchg_desc2.property_is_writeable =
					      psy_charger_property_is_writeable;
	info->psy_hvdvchg_cfg2.drv_data = info;
	info->psy_hvdvchg2 = power_supply_register(&pdev->dev,
						   &info->psy_hvdvchg_desc2,
						   &info->psy_hvdvchg_cfg2);
	if (IS_ERR(info->psy_hvdvchg2))
		chr_err("register psy hvdvchg2 fail:%ld\n",
					PTR_ERR(info->psy_hvdvchg2));

	info->log_level = CHRLOG_ERROR_LEVEL;
	mtk_adapter_protocol_init(info);

	for (i = 0;i < MAX_TA_IDX;i++) {
		info->adapter_dev[i] =
			get_adapter_by_name(adapter_type_names[i]);
		if (!info->adapter_dev[i])
			chr_err("%s: No %s found\n", __func__, adapter_type_names[i]);
		else {
			info->ta_nb[i].nb.notifier_call = notify_adapter_event;
			info->ta_nb[i].info = info;
			register_adapter_device_notifier(info->adapter_dev[i],
					&(info->ta_nb[i].nb));
		}
	}

	sc_init(&info->sc);
	info->chg_alg_nb.notifier_call = chg_alg_event;
/* TN Begin modified by xinjun.lu/860715 20240725 CR/EKLAMU-202 */
#if IS_ENABLED(CONFIG_OEM_TINNO_CHARGER)
	info->fast_charging_indicator = PDC_ID | PE5_ID;
	info->ignore_current_check_time = 0;
#endif
#if IS_ENABLED(CONFIG_OEM_TINNO_CHARGER) && IS_ENABLED(CONFIG_FACTORY_BUILD)
	info->start_factory_discharging = false;
#endif
/* TN End modified by xinjun.lu/860715 20240725 CR/EKLAMU-202 */
	info->enable_meta_current_limit = 1;

	if (strcmp(info->curr_select_name,"NULL")) {
		info->cs_para_mode = 0;
		info->cs_heatlim = 5;
		info->dual_chg_stat = STILL_CHG;
	}

	info->is_charging = false;
	info->power_path_en = true;
	info->en_power_path = true;
	info->safety_timer_cmd = -1;
	info->cmd_pp = -1;

/* TN Begin modified by hao.jia/809321 20240718 CR/EKLAMU-202 */
#if IS_ENABLED(CONFIG_OEM_TINNO_CHARGER)
	info->enable_hiz = false;
	info->enable_charger = true;
	info->disable_thermal_current_limit = 0;
	info->battery_protection_mode = false;
	info->is_over_bpm_max_soc = false;
	info->demo_mode_limit = false;
	info->aicl_check = true;
	info->aicl_final_ic = 0;
	info->is_hvdcp_detecting = false;
	info->restart_hvdcp_work = false;
#endif /* CONFIG_OEM_TINNO_CHARGER */
/* TN End modified by hao.jia/809321 20240718 CR/EKLAMU-202 */

	/* 8 = KERNEL_POWER_OFF_CHARGING_BOOT */
	/* 9 = LOW_POWER_OFF_CHARGING_BOOT */
	if (info != NULL && info->bootmode != 8 && info->bootmode != 9)
		mtk_charger_force_disable_power_path(info, CHG1_SETTING, true);

/* TN Begin modified by xinjun.lu/860715 20240710 CR/EKLAMU-202 */
#if IS_ENABLED(CONFIG_OEM_HVDCP_ALGO)
	INIT_DELAYED_WORK(&info->hvdcp_work, charger_hvdcp_detect_work);
	info->hvdcp_charger_detect_nb.notifier_call = hvdcp_charger_detect_notifier_cb;
	power_supply_reg_notifier(&info->hvdcp_charger_detect_nb);
#endif
/* TN End modified by xinjun.lu/860715 20240710 CR/EKLAMU-202 */

/* TN Begin modified by xinjun.lu/860715 20240729 CR/EKLAMU-202 */
#if IS_ENABLED(CONFIG_PE50_FFC_SUPPORT)
	pe50_info = info;
	pe50_init(info);
#endif
/* TN End modified by xinjun.lu/860715 20240729 CR/EKLAMU-202 */

	kthread_run(charger_routine_thread, info, "charger_thread");

	return 0;
}

static int mtk_charger_remove(struct platform_device *dev)
{
	return 0;
}

static void mtk_charger_shutdown(struct platform_device *dev)
{
	struct mtk_charger *info = platform_get_drvdata(dev);
	int i;

	for (i = 0; i < MAX_ALG_NO; i++) {
		if (info->alg[i] == NULL)
			continue;
		chg_alg_stop_algo(info->alg[i]);
	}
}

static const struct of_device_id mtk_charger_of_match[] = {
	{.compatible = "mediatek,charger",},
	{},
};

MODULE_DEVICE_TABLE(of, mtk_charger_of_match);

struct platform_device mtk_charger_device = {
	.name = "charger",
	.id = -1,
};

static struct platform_driver mtk_charger_driver = {
	.probe = mtk_charger_probe,
	.remove = mtk_charger_remove,
	.shutdown = mtk_charger_shutdown,
	.driver = {
		   .name = "charger",
		   .of_match_table = mtk_charger_of_match,
	},
};

static int __init mtk_charger_init(void)
{
	return platform_driver_register(&mtk_charger_driver);
}
#if IS_BUILTIN(CONFIG_MTK_CHARGER)
late_initcall(mtk_charger_init);
#else
module_init(mtk_charger_init);
#endif
static void __exit mtk_charger_exit(void)
{
	platform_driver_unregister(&mtk_charger_driver);
}
module_exit(mtk_charger_exit);


MODULE_AUTHOR("wy.chuang <wy.chuang@mediatek.com>");
MODULE_DESCRIPTION("MTK Charger Driver");
MODULE_LICENSE("GPL");
