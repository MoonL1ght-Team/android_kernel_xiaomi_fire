/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#ifndef __MTK_CHARGER_H
#define __MTK_CHARGER_H

#include <linux/alarmtimer.h>
#include "charger_class.h"
#include "adapter_class.h"
#include "mtk_charger_algorithm_class.h"
#include <linux/power_supply.h>
#include "mtk_smartcharging.h"
/* TN Begin modified by xinjun.lu/860715 20240924 CR/EKLAMU-202 */
#if IS_ENABLED(CONFIG_OEM_TINNO_CHARGER)
#include "mtk_battery.h"
#endif
/* TN End modified by xinjun.lu/860715 20240924 CR/EKLAMU-202 */

#define CHARGING_INTERVAL 10
#define CHARGING_FULL_INTERVAL 20

#define CHRLOG_ERROR_LEVEL	1
#define CHRLOG_INFO_LEVEL	2
#define CHRLOG_DEBUG_LEVEL	3

#define SC_TAG "smartcharging"

extern int chr_get_debug_level(void);

#define chr_err(fmt, args...)					\
do {								\
	if (chr_get_debug_level() >= CHRLOG_ERROR_LEVEL) {	\
		pr_notice(fmt, ##args);				\
	}							\
} while (0)

#define chr_info(fmt, args...)					\
do {								\
	if (chr_get_debug_level() >= CHRLOG_INFO_LEVEL) {	\
		pr_notice_ratelimited(fmt, ##args);		\
	}							\
} while (0)

#define chr_debug(fmt, args...)					\
do {								\
	if (chr_get_debug_level() >= CHRLOG_DEBUG_LEVEL) {	\
		pr_notice(fmt, ##args);				\
	}							\
} while (0)

struct mtk_charger;
struct charger_data;
#define BATTERY_CV 4350000
#define V_CHARGER_MAX 6500000 /* 6.5 V */
#define V_CHARGER_MIN 4600000 /* 4.6 V */
#define VBUS_OVP_VOLTAGE 15000000 /* 15V */
/* TN Begin modified by xinjun.lu/860715 20240809 CR/EKLAMU-202 */
#if IS_ENABLED(CONFIG_OEM_TINNO_CHARGER)
#define EOC_CURRENT 335000 /* ua */
#define PDC_CHARGER_CURRENT			3600000
#define PDC_CHARGER_INPUT_CURRENT		3000000
#endif
/* TN End modified by xinjun.lu/860715 20240809 CR/EKLAMU-202 */

/* dual battery */
#define V_CS_BATTERY_CV 4350 /* mV */
#define AC_CS_NORMAL_CC 2000 /* mV */
#define AC_CS_FAST_CC 2000 /* mV */
#define CS_CC_MIN 100 /* mA */
#define V_BATT_EXTRA_DIFF 300 /* 265 mV */

#define USB_CHARGER_CURRENT_SUSPEND		0 /* def CONFIG_USB_IF */
#define USB_CHARGER_CURRENT_UNCONFIGURED	70000 /* 70mA */
#define USB_CHARGER_CURRENT_CONFIGURED		500000 /* 500mA */
#define USB_CHARGER_CURRENT			500000 /* 500mA */

/* TN Begin modified by jirui.li/860702 20240722 CR/EKLAMU-834 */
#define AC_CHARGER_CURRENT			2400000
#define AC_CHARGER_INPUT_CURRENT		2000000
#define NON_STD_AC_CHARGER_CURRENT		1500000
#define CHARGING_HOST_CHARGER_CURRENT		1500000
#define JEITA_TEMP_ABOVE_T4_CURRENT		0
#define JEITA_TEMP_T3_TO_T4_CURRENT		3600000
#define JEITA_TEMP_T2_TO_T3_CURRENT		3600000
#define JEITA_TEMP_T1_TO_T2_CURRENT		2550000
#define JEITA_TEMP_T0_TO_T1_CURRENT		1000000
#define JEITA_TEMP_BELOW_T0_CURRENT		0
/* TN End modified by jirui.li/860702 20240722 CR/EKLAMU-834 */
/* TN Begin modified by xinjun.lu/860715 20240710 CR/EKLAMU-202 */
#if IS_ENABLED(CONFIG_OEM_HVDCP_ALGO)
#define HVDCP_CHARGER_CURRENT			3500000
#define HVDCP_CHARGER_INPUT_CURRENT		3200000
#endif
/* TN End modified by xinjun.lu/860715 20240710 CR/EKLAMU-202 */

/* dynamic mivr */
#define V_CHARGER_MIN_1 4400000 /* 4.4 V */
#define V_CHARGER_MIN_2 4200000 /* 4.2 V */
#define MAX_DMIVR_CHARGER_CURRENT 1800000 /* 1.8 A */

/* battery warning */
#define BATTERY_NOTIFY_CASE_0001_VCHARGER
#define BATTERY_NOTIFY_CASE_0002_VBATTEMP

/* charging abnormal status */
#define CHG_VBUS_OV_STATUS	(1 << 0)
#define CHG_BAT_OT_STATUS	(1 << 1)
#define CHG_OC_STATUS		(1 << 2)
#define CHG_BAT_OV_STATUS	(1 << 3)
#define CHG_ST_TMO_STATUS	(1 << 4)
#define CHG_BAT_LT_STATUS	(1 << 5)
#define CHG_TYPEC_WD_STATUS	(1 << 6)
#define CHG_DPDM_OV_STATUS	(1 << 7)

/* Battery Temperature Protection */
#define MIN_CHARGE_TEMP  0
#define MIN_CHARGE_TEMP_PLUS_X_DEGREE	6
#define MAX_CHARGE_TEMP  50
#define MAX_CHARGE_TEMP_MINUS_X_DEGREE	47

#define MAX_ALG_NO 10

#define RESET_BOOT_VOLT_TIME 50

#if IS_ENABLED(CONFIG_OEM_TINNO_CHARGER) || IS_ENABLED(CONFIG_OEM_TURBO_CHARGER)
#define BATTERY_TEMP_LOW		0
#define BATTERY_TEMP_HIGH		45
#endif

/*TN Begin modified by hao.jia/809321 20240628 CR/EKLAMU-202 */
#if IS_ENABLED(CONFIG_OEM_TURBO_CHARGER)
struct ffc_bat_zone {
	int temp;
	int ffc_max_mv;
	int ffc_chg_iterm;
};

enum ffc_chrg_step {
	STEP_NORM,
	STEP_FULL,
	STEP_NONE = 0xFF,
};
#endif /* CONFIG_OEM_TURBO_CHARGER */
/*TN End modified by hao.jia/809321 20240628 CR/EKLAMU-202 */
#define USB_CURRENT_MASK 0x80000000
#define UNLIMIT_CURRENT_MASK 0x10000000

enum bat_temp_state_enum {
	BAT_TEMP_LOW = 0,
	BAT_TEMP_NORMAL,
	BAT_TEMP_HIGH
};

enum DUAL_CHG_STAT {
	BOTH_EOC,
	STILL_CHG,
};

enum ADC_SOURCE {
	NULL_HANDLE,
	FROM_CHG_IC,
	FROM_CS_ADC,
};

enum TA_STATE {
	TA_INIT_FAIL,
	TA_CHECKING,
	TA_NOT_SUPPORT,
	TA_NOT_READY,
	TA_READY,
	TA_PD_PPS_READY,
};

enum adapter_protocol_state {
	FIRST_HANDSHAKE,
	RUN_ON_PD,
	RUN_ON_UFCS,
};

enum TA_CAP_STATE {
	APDO_TA,
	WO_APDO_TA,
	STD_TA,
	ONLY_APDO_TA,
};

enum chg_dev_notifier_events {
	EVENT_FULL,
	EVENT_RECHARGE,
	EVENT_DISCHARGE,
};

struct battery_thermal_protection_data {
	int sm;
	bool enable_min_charge_temp;
	int min_charge_temp;
	int min_charge_temp_plus_x_degree;
	int max_charge_temp;
	int max_charge_temp_minus_x_degree;
};

/* sw jeita */
#define JEITA_TEMP_ABOVE_T4_CV	4240000
#define JEITA_TEMP_T3_TO_T4_CV	4240000
#define JEITA_TEMP_T2_TO_T3_CV	4340000
#define JEITA_TEMP_T1_TO_T2_CV	4240000
#define JEITA_TEMP_T0_TO_T1_CV	4040000
#define JEITA_TEMP_BELOW_T0_CV	4040000
/* TN Begin modified by jirui.li/860702 20240929 CR/EKLAMU-202 */
#if IS_ENABLED(CONFIG_OEM_TINNO_CHARGER)
#define TEMP_T4_THRES  60
#define TEMP_T4_THRES_MINUS_X_DEGREE 57
#define TEMP_T3_THRES  45
#define TEMP_T3_THRES_MINUS_X_DEGREE 42
#define TEMP_T2_THRES  15
#define TEMP_T2_THRES_PLUS_X_DEGREE 18
#define TEMP_T1_THRES  0
#define TEMP_T1_THRES_PLUS_X_DEGREE 3
#define TEMP_T0_THRES  -20
#define TEMP_T0_THRES_PLUS_X_DEGREE  -17
#define TEMP_NEG_10_THRES -22
#else
#define TEMP_T4_THRES  50
#define TEMP_T4_THRES_MINUS_X_DEGREE 47
#define TEMP_T3_THRES  45
#define TEMP_T3_THRES_MINUS_X_DEGREE 39
#define TEMP_T2_THRES  10
#define TEMP_T2_THRES_PLUS_X_DEGREE 16
#define TEMP_T1_THRES  0
#define TEMP_T1_THRES_PLUS_X_DEGREE 6
#define TEMP_T0_THRES  0
#define TEMP_T0_THRES_PLUS_X_DEGREE  0
#define TEMP_NEG_10_THRES 0
#endif /* CONFIG_OEM_TINNO_CHARGER */
/* TN End modified by jirui.li/860702 20240929 CR/EKLAMU-202 */

/*
 * Software JEITA
 * T0: -10 degree Celsius
 * T1: 0 degree Celsius
 * T2: 10 degree Celsius
 * T3: 45 degree Celsius
 * T4: 50 degree Celsius
 */
enum sw_jeita_state_enum {
	TEMP_BELOW_T0 = 0,
	TEMP_T0_TO_T1,
	TEMP_T1_TO_T2,
	TEMP_T2_TO_T3,
	TEMP_T3_TO_T4,
	TEMP_ABOVE_T4
};

struct info_notifier_block {
	struct notifier_block nb;
	struct mtk_charger *info;
};

struct sw_jeita_data {
	int sm;
	int pre_sm;
	int cv;
	bool charging;
	bool error_recovery_flag;
};

struct mtk_charger_algorithm {

	int (*do_algorithm)(struct mtk_charger *info);
	int (*enable_charging)(struct mtk_charger *info, bool en);
	int (*do_event)(struct notifier_block *nb, unsigned long ev, void *v);
	int (*do_dvchg1_event)(struct notifier_block *nb, unsigned long ev,
			       void *v);
	int (*do_dvchg2_event)(struct notifier_block *nb, unsigned long ev,
			       void *v);
	int (*do_hvdvchg1_event)(struct notifier_block *nb, unsigned long ev,
			       void *v);
	int (*do_hvdvchg2_event)(struct notifier_block *nb, unsigned long ev,
			       void *v);
	int (*change_current_setting)(struct mtk_charger *info);
	void *algo_data;
};

struct charger_custom_data {
	int battery_cv;	/* uv */
	int max_charger_voltage;
	int max_charger_voltage_setting;
	int min_charger_voltage;
	int vbus_sw_ovp_voltage;

	int usb_charger_current;
	int ac_charger_current;
	int ac_charger_input_current;
	int charging_host_charger_current;

	/* sw jeita */
	int jeita_temp_above_t4_cv;
	int jeita_temp_t3_to_t4_cv;
	int jeita_temp_t2_to_t3_cv;
	int jeita_temp_t1_to_t2_cv;
	int jeita_temp_t0_to_t1_cv;
	int jeita_temp_below_t0_cv;
	int temp_t4_thres;
	int temp_t4_thres_minus_x_degree;
	int temp_t3_thres;
	int temp_t3_thres_minus_x_degree;
	int temp_t2_thres;
	int temp_t2_thres_plus_x_degree;
	int temp_t1_thres;
	int temp_t1_thres_plus_x_degree;
	int temp_t0_thres;
	int temp_t0_thres_plus_x_degree;
	int temp_neg_10_thres;
/* TN Begin modified by jirui.li/860702 20240722 CR/EKLAMU-834 */
	int jeita_temp_above_t4_icurrent;
	int jeita_temp_t3_to_t4_icurrent;
	int jeita_temp_t2_to_t3_icurrent;
	int jeita_temp_t1_to_t2_icurrent;
	int jeita_temp_t0_to_t1_icurrent;
	int jeita_temp_below_t0_icurrent;
/* TN End modified by jirui.li/860702 20240722 CR/EKLAMU-834 */
/* TN Begin modified by xinjun.lu/860715 20240710 CR/EKLAMU-202 */
#if IS_ENABLED(CONFIG_OEM_HVDCP_ALGO)
	int hvdcp_charging_current_limit;
	int hvdcp_input_current_limit;
#endif
/* TN End modified by xinjun.lu/860715 20240710 CR/EKLAMU-202 */

	/* battery temperature protection */
	int mtk_temperature_recharge_support;
	int max_charge_temp;
	int max_charge_temp_minus_x_degree;
	int min_charge_temp;
	int min_charge_temp_plus_x_degree;

	/* dynamic mivr */
	int min_charger_voltage_1;
	int min_charger_voltage_2;
	int max_dmivr_charger_current;
/* TN Begin modified by xinjun.lu/860715 20240809 CR/EKLAMU-202 */
#if IS_ENABLED(CONFIG_OEM_TINNO_CHARGER)
	int pdc_charging_current_limit;
	int pdc_input_current_limit;
	int eoc_current;
#endif
/* TN End modified by xinjun.lu/860715 20240809 CR/EKLAMU-202 */
};

struct charger_data {
	int input_current_limit;
	int charging_current_limit;

	int force_charging_current;
	int thermal_input_current_limit;
	int thermal_charging_current_limit;
	int usb_input_current_limit;
	int pd_input_current_limit;
	bool thermal_throttle_record;
	int disable_charging_count;
	int input_current_limit_by_aicl;
	int junction_temp_min;
	int junction_temp_max;
/* TN Begin modified by jirui.li/860702 20240722 CR/EKLAMU-834 */
	int temp_charging_current_limit;
/* TN End modified by jirui.li/860702 20240722 CR/EKLAMU-834 */
};

enum chg_data_idx_enum {
	CHG1_SETTING,
	CHG2_SETTING,
	DVCHG1_SETTING,
	DVCHG2_SETTING,
	HVDVCHG1_SETTING,
	HVDVCHG2_SETTING,
	CHGS_SETTING_MAX,
};

/* TN Begin modified by xinjun.lu/860715 20240729 CR/EKLAMU-202 */
#if IS_ENABLED(CONFIG_PE50_FFC_SUPPORT)
enum charging_limit_modes {
	CHARGING_LIMIT_OFF,
	CHARGING_LIMIT_RUN,
	CHARGING_LIMIT_UNKNOWN,
};

struct pe50_battery_info {
	int batt_mv;
	int batt_ma;
	int batt_soc;
	int batt_temp;
	int batt_status;
	int batt_full_uah;
	int batt_design_uah;
//	char batt_sn[pe50_BATT_SN_LEN];
};

struct pe50_charger_info {
	int chrg_mv;
	int chrg_ma;
	int chrg_type;
	int chrg_pmax_mw;
	int chrg_present;
};

struct pe50_charger_cfg {
	int target_fcc;
	int target_fv;
	int fg_iterm;
	int chrg_iterm;
	bool full_charged;
	bool charging_reset;
	bool taper_kickoff;
	bool charging_disable;
	bool charger_suspend;
	int demo_mode;
//	bool factory_mode;
//	bool factory_version;
//	int dcp_pmax;
//	int hvdcp_pmax;
};

struct pe50_charger_driver {
	char *name;
	struct device *dev;
	int (*get_batt_info)(void *data, struct pe50_battery_info *batt_info);
	int (*get_chg_info)(void *data, struct pe50_charger_info *chg_info);
	int (*config_charge)(void *data, struct pe50_charger_cfg *config);
	bool (*is_charge_tapered)(void *data, int tapered_ma);
	bool (*is_charge_halt)(void *data);
	void *data;
};

struct pe50_ffc_zone {
	int temp;
	int ffc_max_mv;
	int ffc_chg_iterm;
};

struct pe50_temp_zone {
	int temp_c;
	int norm_mv;
	int fcc_max_ma;
	int fcc_norm_ma;
};

#define MAX_NUM_STEPS 10

enum pe50_temp_zones {
	ZONE_FIRST_PE50 =0,
	/* states 0-9 are reserved for zones */
	ZONE_LAST_PE50 = MAX_NUM_STEPS + ZONE_FIRST_PE50 - 1,
	ZONE_HOT_PE50,
	ZONE_COLD_PE50,
	ZONE_NONE_PE50 = 0XFF,
};

enum pe50_chrg_step {
	STEP_MAX_PE50,
	STEP_NORM_PE50,
	STEP_FULL_PE50,
	STEP_FLOAT_PE50,
	STEP_DEMO_PE50,
	STEP_STOP_PE50,
	STEP_NONE_PE50 = 0xFF,
};

struct pe50_params {
	bool init_done;
	bool factory_mode;
	int demo_mode;
	bool demo_discharging;
	bool factory_kill_armed;
	/*adaptive charging*/
	bool adaptive_charging_disable_ichg;
	bool adaptive_charging_disable_ibat;
	bool charging_enable_hz;
	bool battery_charging_disable;
	/* Charge Profile */
	int num_temp_zones;
	struct pe50_temp_zone *temp_zones;
	enum pe50_temp_zones pres_temp_zone;
	enum pe50_chrg_step pres_chrg_step;
	int chrg_taper_cnt;
	int temp_state;
	int chrg_iterm;
	int back_chrg_iterm;
	int num_ffc_zones;
	struct pe50_ffc_zone *ffc_zones;
	bool enable_charging_limit;
	bool is_factory_image;
	enum charging_limit_modes charging_limit_modes;
	int upper_limit_capacity;
	int lower_limit_capacity;
	int base_fv_mv;
	int vfloat_comp_mv;
	int batt_health;
	int max_chrg_temp;
	/*target parameter*/
	int target_fv;
	bool chg_disable;
	int target_fcc;
	int target_usb;
	struct notifier_block chg_reboot;
	int min_therm_current_limit;
	bool enable_mux;
//	struct pe50_mux_chan mux_channel;
	int wls_switch_en;
	int wls_boost_en;
	int charge_rate;
	unsigned int active_fast_alg;
	int typec_rp_max_current;
	int pd_pmax_mw;
	struct adapter_auth_data apdo_cap;
	int pd_cap_max_watt;
	int vbus_h;
	int vbus_l;
	int charger_watt;
};
#endif
/* TN End modified by xinjun.lu/860715 20240729 CR/EKLAMU-202 */

struct mtk_charger {
	struct platform_device *pdev;
	struct charger_device *chg1_dev;
	struct notifier_block chg1_nb;
	struct charger_device *chg2_dev;
	struct charger_device *dvchg1_dev;
	struct notifier_block dvchg1_nb;
	struct charger_device *dvchg2_dev;
	struct notifier_block dvchg2_nb;
	struct charger_device *hvdvchg1_dev;
	struct notifier_block hvdvchg1_nb;
	struct charger_device *hvdvchg2_dev;
	struct notifier_block hvdvchg2_nb;
	struct charger_device *bkbstchg_dev;
	struct notifier_block bkbstchg_nb;
	struct charger_device *cschg1_dev;
	struct notifier_block cschg1_nb;
	struct charger_device *cschg2_dev;
	struct notifier_block cschg2_nb;


	struct charger_data chg_data[CHGS_SETTING_MAX];
	struct chg_limit_setting setting;
	enum charger_configuration config;

	struct power_supply_desc psy_desc1;
	struct power_supply_config psy_cfg1;
	struct power_supply *psy1;

	struct power_supply_desc psy_desc2;
	struct power_supply_config psy_cfg2;
	struct power_supply *psy2;

	struct power_supply_desc psy_dvchg_desc1;
	struct power_supply_config psy_dvchg_cfg1;
	struct power_supply *psy_dvchg1;

	struct power_supply_desc psy_dvchg_desc2;
	struct power_supply_config psy_dvchg_cfg2;
	struct power_supply *psy_dvchg2;

	struct power_supply_desc psy_hvdvchg_desc1;
	struct power_supply_config psy_hvdvchg_cfg1;
	struct power_supply *psy_hvdvchg1;

	struct power_supply_desc psy_hvdvchg_desc2;
	struct power_supply_config psy_hvdvchg_cfg2;
	struct power_supply *psy_hvdvchg2;

	struct power_supply  *chg_psy;
	struct power_supply  *bc12_psy;
	struct power_supply  *bat_psy;
	struct power_supply  *bat2_psy;
	struct power_supply  *bat_manager_psy;
	struct adapter_device *select_adapter;
	struct adapter_device *pd_adapter;
	struct adapter_device *adapter_dev[MAX_TA_IDX];
	struct notifier_block *nb_addr;
	struct info_notifier_block ta_nb[MAX_TA_IDX];
	struct adapter_device *ufcs_adapter;
	struct mutex pd_lock;
	struct mutex ufcs_lock;
	struct mutex ta_lock;

	u32 bootmode;
	u32 boottype;

	int ta_status[MAX_TA_IDX];
	int select_adapter_idx;
	int ta_hardreset;
	int chr_type;
	int usb_type;
	int usb_state;
	int adapter_priority;
	int en_cts_mode;

	struct mutex cable_out_lock;
	int cable_out_cnt;

	/* system lock */
	spinlock_t slock;
	struct wakeup_source *charger_wakelock;
	struct mutex charger_lock;

	/* thread related */
	wait_queue_head_t  wait_que;
	bool charger_thread_timeout;
	unsigned int polling_interval;
	bool charger_thread_polling;

	/* alarm timer */
	struct alarm charger_timer;
	struct timespec64 endtime;
	bool is_suspend;
	struct notifier_block pm_notifier;

	/* notify charger user */
	struct srcu_notifier_head evt_nh;

	/* common info */
	int log_level;
	bool usb_unlimited;
	bool charger_unlimited;
	bool disable_charger;
	bool disable_aicl;
	int battery_temp;
	bool can_charging;
	bool cmd_discharging;
	bool safety_timeout;
	int safety_timer_cmd;
	bool vbusov_stat;
	bool dpdmov_stat;
	bool lst_dpdmov_stat;
	bool is_chg_done;
	bool power_path_en;
	bool en_power_path;
	/* ATM */
	bool atm_enabled;

	const char *algorithm_name;
	const char *curr_select_name;
	struct mtk_charger_algorithm algo;

	/* dtsi custom data */
	struct charger_custom_data data;

	/* battery warning */
	unsigned int notify_code;
	unsigned int notify_test_mode;

	/* sw safety timer */
	bool enable_sw_safety_timer;
	bool sw_safety_timer_setting;
	struct timespec64 charging_begin_time;

	/* vbat monitor, 6pin bat */
	bool batpro_done;
	bool enable_vbat_mon;
	bool enable_vbat_mon_bak;
	int old_cv;
	bool stop_6pin_re_en;
	int vbat0_flag;

	/* sw jeita */
	bool enable_sw_jeita;
	struct sw_jeita_data sw_jeita;

	/* battery thermal protection */
	struct battery_thermal_protection_data thermal;

	struct chg_alg_device *alg[MAX_ALG_NO];
	int lst_rnd_alg_idx;
	bool alg_new_arbitration;
	bool alg_unchangeable;
	struct notifier_block chg_alg_nb;
	bool enable_hv_charging;

	/* water detection */
	bool water_detected;
	bool record_water_detected;

	bool enable_dynamic_mivr;

	/* fast charging algo support indicator */
	bool enable_fast_charging_indicator;
	unsigned int fast_charging_indicator;

	/* diasable meta current limit for testing */
	unsigned int enable_meta_current_limit;

	/* set current selector parallel mode */
	int cs_heatlim;
	unsigned int cs_para_mode;
	int cs_gpio_index;
	bool cs_hw_disable;
	int dual_chg_stat;
	int cs_cc_now;
	int comp_resist;
	struct smartcharging sc;
	bool cs_with_gauge;

	/*daemon related*/
	struct sock *daemo_nl_sk;
	u_int g_scd_pid;
	struct scd_cmd_param_t_1 sc_data;

	/*charger IC charging status*/
	bool is_charging;
	bool is_cs_chg_done;

	ktime_t uevent_time_check;

	bool force_disable_pp[CHG2_SETTING + 1];
	bool enable_pp[CHG2_SETTING + 1];
	struct mutex pp_lock[CHG2_SETTING + 1];
	int cmd_pp;

	/* enable boot volt*/
	bool enable_boot_volt;
	bool reset_boot_volt_times;

	/* adapter switch control */
	int protocol_state;
	int ta_capability;
	int wait_times;

/* TN Begin modified by jirui.li/860702 20240724 CR/EKLAMU-620 */
#if IS_ENABLED(CONFIG_OEM_TINNO_CHARGER) && IS_ENABLED(CONFIG_FACTORY_BUILD)
	bool enable_factory_charging_test;
	bool factory_enable_switch_charger;
	bool factory_enable_pump_charger;
	int factory_charging_limit_soc;
#endif /* CONFIG_OEM_TINNO_CHARGER && CONFIG_FACTORY_BUILD */
/* TN Begin modified by jirui.li/860702 20240724 CR/EKLAMU-620 */
/* TN Begin modified by xinjun.lu/860715 20240710 CR/EKLAMU-202 */
#if IS_ENABLED(CONFIG_OEM_HVDCP_ALGO)
	struct delayed_work hvdcp_work;
	struct notifier_block hvdcp_charger_detect_nb;
#endif
/* TN End modified by xinjun.lu/860715 20240710 CR/EKLAMU-202 */

#if IS_ENABLED(CONFIG_OEM_TINNO_CHARGER) || IS_ENABLED(CONFIG_OEM_HVDCP_ALGO)
	int ext_chr_type;
	bool restart_hvdcp_work;
	int aicl_final_ic;
	bool is_hvdcp_detecting;
	ktime_t hvdcp_boost_done_time;
	ktime_t hvdcp_plug_in_time;
#endif

/* TN Begin modified by hao.jia/809321 20240628 CR/EKLAMU-202 */
#if IS_ENABLED(CONFIG_OEM_TURBO_CHARGER)
	struct ffc_bat_zone *ffc_zones;
	int num_ffc_zones;
	int chrg_iterm;
	int pres_chrg_step;
	int chrg_taper_cnt;
	int target_mv;
#endif /* CONFIG_OEM_TURBO_CHARGER */
/* TN End modified by hao.jia/809321 20240628 CR/EKLAMU-202 */

/* TN Begin modified by hao.jia/809321 20240924 CR/EKLAMU-202 */
#if IS_ENABLED(CONFIG_OEM_TINNO_CHARGER)
	bool enable_hiz;
	bool enable_charger;
	int disable_thermal_current_limit;
	struct power_supply  *qc_phy_psy;
	bool battery_protection_mode;
	bool is_over_bpm_max_soc;
	bool demo_mode_limit;
	struct mtk_battery_manager *bm;
	int ignore_current_check_time;
	int charge_full_soc_for_over_temp;
	bool aicl_check;
#endif /* CONFIG_OEM_TINNO_CHARGER */
/* TN End modified by hao.jia/809321 20240924 CR/EKLAMU-202 */
	//struct pe50_charger_cfg pe50;

/* TN Begin modified by xinjun.lu/860715 20240821 CR/EKLAMU-202 */
#if IS_ENABLED(CONFIG_PE50_FFC_SUPPORT)
	struct pe50_params pe50;
	struct chg_alg_device *current_alg;
#endif

#if IS_ENABLED(CONFIG_OEM_TINNO_CHARGER) && IS_ENABLED(CONFIG_FACTORY_BUILD)
	bool start_factory_discharging;
#endif
/* TN End modified by xinjun.lu/860715 20240821 CR/EKLAMU-202 */
};

static inline int mtk_chg_alg_notify_call(struct mtk_charger *info,
					  enum chg_alg_notifier_events evt,
					  int value)
{
	int i;
	struct chg_alg_notify notify = {
		.evt = evt,
		.value = value,
	};

	for (i = 0; i < MAX_ALG_NO; i++) {
		if (info->alg[i])
			chg_alg_notifier_call(info->alg[i], &notify);
	}
	return 0;
}

/* functions which framework needs*/
extern int mtk_basic_charger_init(struct mtk_charger *info);
extern int mtk_pulse_charger_init(struct mtk_charger *info);
extern int get_uisoc(struct mtk_charger *info);
extern int get_battery_voltage(struct mtk_charger *info);
extern int get_battery_temperature(struct mtk_charger *info);
extern int get_battery_current(struct mtk_charger *info);
extern int get_cs_side_battery_current(struct mtk_charger *info, int *ibat);
extern int get_cs_side_battery_voltage(struct mtk_charger *info, int *vbat);
extern int get_chg_output_vbat(struct mtk_charger *info, int *vbat);
extern int get_vbus(struct mtk_charger *info);
extern int get_ibat(struct mtk_charger *info);
extern int get_ibus(struct mtk_charger *info);
extern bool is_battery_exist(struct mtk_charger *info);
extern int get_charger_type(struct mtk_charger *info);
extern int get_usb_type(struct mtk_charger *info);
extern int disable_hw_ovp(struct mtk_charger *info, int en);
extern bool is_charger_exist(struct mtk_charger *info);
extern int get_charger_temperature(struct mtk_charger *info,
	struct charger_device *chg);
extern int get_charger_charging_current(struct mtk_charger *info,
	struct charger_device *chg);
extern int get_charger_input_current(struct mtk_charger *info,
	struct charger_device *chg);
extern int get_charger_zcv(struct mtk_charger *info,
	struct charger_device *chg);
extern void _wake_up_charger(struct mtk_charger *info);
extern int mtk_adapter_switch_control(struct mtk_charger *info);
extern int mtk_selected_adapter_ready(struct mtk_charger *info);
extern int mtk_adapter_protocol_init(struct mtk_charger *info);
extern void mtk_check_ta_status(struct mtk_charger *info);
/* functions for other */
extern int mtk_chg_enable_vbus_ovp(bool enable);

#define ONLINE(idx, attach)		((idx & 0xf) << 4 | (attach & 0xf))
#define ONLINE_GET_IDX(online)		((online >> 4) & 0xf)
#define ONLINE_GET_ATTACH(online)	(online & 0xf)

#endif /* __MTK_CHARGER_H */
