/*
 * Samsung Exynos5 SoC series Actuator driver
 *
 *
 * Copyright (c) 2020 Samsung Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef IS_DEVICE_DW9818_H
#define IS_DEVICE_DW9818_H

#define ACTUATOR_NAME		"DW9818"

#define DW9818_POS_SIZE_BIT		ACTUATOR_POS_SIZE_10BIT
#define DW9818_POS_MAX_SIZE		((1 << DW9818_POS_SIZE_BIT) - 1)
#define DW9818_POS_DIRECTION		ACTUATOR_RANGE_INF_TO_MAC
#define RINGING_CONTROL			0x2F /* 11ms */
#define PRESCALER				0x01 /* Tvib x 1 */

#define PWR_ON_DELAY	5000 /* DW9818 need delay for 5msec after power-on */
#define INIT_DELAY	1000 /* DW9818 need delay for 1msec after init */

struct is_caldata_list_dw9818 {
	u32 af_position_type;
	u32 af_position_worst;
	u32 af_macro_position_type;
	u32 af_macro_position_worst;
	u32 af_default_position;
	u8 reserved0[12];
	u16 info_position;
	u16 mac_position;
	u32 equipment_info1;
	u32 equipment_info2;
	u32 equipment_info3;
	u8 control_mode; // for DW9818
	u8 prescale;     // for DW9818
	u8 resonance;    // for DW9818
	u8 reserved1[13];
	u8 reserved2[16];

	u8 core_version;
	u8 pixel_number[2];
	u8 is_pid;
	u8 sensor_maker;
	u8 year;
	u8 month;
	u16 release_number;
	u8 manufacturer_id;
	u8 module_version;
	u8 reserved3[161];
	u32 check_sum;
};

struct is_caldata_sac_dw9818 {
	u8 control_mode;
	u8 resonance;
};

#endif /* IS_DEVICE_DW9818_H */
