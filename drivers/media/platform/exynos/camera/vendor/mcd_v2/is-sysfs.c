/*
 * Samsung Exynos5 SoC series FIMC-IS driver
 *
 * exynos5 fimc-is core functions
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/pm_qos.h>
#include <linux/device.h>

#include "is-sysfs.h"
#include "is-core.h"
#include "is-err.h"
#include "is-sec-define.h"
#if defined (CONFIG_OIS_USE)
#include "is-device-ois.h"
#endif
#ifdef CONFIG_AF_HOST_CONTROL
#include "is-device-af.h"
#endif
#include "is-device-sensor-peri.h"
#include "is-vender-specific.h"
#include "is-interface-library.h"
#include "is-ois-mcu.h"
#if IS_ENABLED(CONFIG_CAMERA_USE_AOIS)
#include "is-aois-if.h"
#endif

/* #define FORCE_CAL_LOAD */
#define SYSFS_MAX_READ_SIZE	4096
#define VER_MAX_SIZE 100
#if IS_ENABLED(RELAX_OIS_GYRO_OFFSET_SPEC)
#define CAMERA_OIS_GYRO_OFFSET_SPEC 15000
#else
#define CAMERA_OIS_GYRO_OFFSET_SPEC 10000
#endif
extern struct device *is_dev;
struct class *camera_class = NULL;
EXPORT_SYMBOL_GPL(camera_class);
struct device *camera_front_dev;
struct device *camera_rear_dev;

#if defined (CONFIG_OIS_USE)
struct device *camera_ois_dev;
#endif
static struct is_core *sysfs_core;
static struct is_cam_info cam_infos[CAM_INFO_MAX];
static struct is_common_cam_info common_cam_infos;

extern bool force_caldata_dump;
extern int test_phy_margin_check(char *buffer, int cam_index);
#if defined (CONFIG_OIS_USE)
bool check_ois_power = false;
bool check_shaking_noise;
int ois_power_count = 0;
int shaking_power_count = 0;
int ois_threshold = 150;
#ifdef CAMERA_2ND_OIS
int ois_threshold_rear2 = 150;
#endif
long x_init_raw = 0;
long y_init_raw = 0;
long z_init_raw = 0;
#endif
static bool check_module_init = false;

#ifdef READ_DUAL_CAL_FIRMWARE_DATA
static char dual_cal_data[DUAL_CAL_DATA_SIZE_DEFAULT] = {0,};
#endif

#define SSRM_INFO_PRINT(container, member)						\
	do {										\
		sprintf(temp_buffer, #member"=%d;", (container.member));		\
		strncat(buf, temp_buffer, strlen(temp_buffer));				\
	} while(0)

struct ssrm_camera_data {
	int ID;
	int ON;
	int ISPWidth;
	int ISPHeight;
};

struct ssrm_camera_data_common {
	int previewWidth;
	int previewHeight;
	int minFPS;
	int maxFPS;
	int FPSHint;
	int capture;
	int HDRCapture;
	int MODE;
	int HDR10;
	int zoom;
	int flash;
	int ssteady;
	int mem;
} SsrmCameraInfoExt;

struct ssrm_send_format {
	int operation;
	// per Camera Info
	struct ssrm_camera_data per_camera_info;
	// Common Info
	struct ssrm_camera_data_common common_info;
};

enum ssrm_camerainfo_operation {
	SSRM_CAMERA_INFO_CLEAR,
	SSRM_CAMERA_INFO_SET,
	SSRM_CAMERA_INFO_UPDATE,
};

struct ssrm_camera_data SsrmCameraInfo[SENSOR_POSITION_MAX];
int ssrmCameraInfoCnt = 0;

/* read firmware */
int read_from_firmware_version(int rom_id)
{
	int ret = 0;
	struct is_rom_info *finfo;

	is_vender_check_hw_init_running();
	is_sec_get_sysfs_finfo(&finfo, rom_id);

	if (!test_bit(IS_ROM_STATE_CAL_READ_DONE, &finfo->rom_state)) {
		ret = is_sec_run_fw_sel(rom_id);
		if (ret) {
			err("is_sec_run_fw_sel is fail(%d)", ret);
		}
	}

	return 0;
}

static int read_from_firmware_version_reload(int rom_id)
{
	int ret = 0;

	ret = is_sec_run_fw_sel(rom_id);
	if (ret)
		err("is_sec_run_fw_sel is fail(%d)", ret);

	return 0;
}

/* common utility */
int is_get_cam_info(struct is_cam_info **caminfo)
{
	*caminfo = cam_infos;
	return 0;
}

int is_get_cam_info_from_index(struct is_cam_info **caminfo, enum is_cam_info_index cam_index)
{
	*caminfo = &(cam_infos[cam_index]);
	return 0;
}

void is_get_common_cam_info(struct is_common_cam_info **caminfo)
{
	*caminfo = &common_cam_infos;
}

static int is_get_sensor_data(char *maker, char *name, int position)
{
	struct is_module_enum *module;

	is_vendor_get_module_from_position(position, &module);

	if (module) {
		if (maker != NULL)
			sprintf(maker, "%s", module->sensor_maker ?
					module->sensor_maker : "UNKNOWN");
		if (name != NULL)
			sprintf(name, "%s", module->sensor_name ?
					module->sensor_name : "UNKNOWN");
		return 0;
	}

	err("%s: there's no matched sensor id", __func__);

	return -ENODEV;
}

/* common function for sysfs */
static ssize_t camera_info_show(char *buf, enum is_cam_info_index cam_index)
{
	char camera_info[130] = {0, };
#ifdef CONFIG_OF
	struct is_cam_info *cam_info;

	is_get_cam_info_from_index(&cam_info, cam_index);

	if (!cam_info->valid) {
		strcpy(camera_info, "ISP=NULL;CALMEM=NULL;READVER=NULL;COREVOLT=NULL;UPGRADE=NULL;"
		"FWWRITE=NULL;FWDUMP=NULL;CC=NULL;OIS=NULL;VALID=N");

		if (cam_index == CAM_INFO_REAR2) {
			strncat(camera_info, ";DUALOPEN=N", strlen(";DUALOPEN=N"));
		}

		return sprintf(buf, "%s\n", camera_info);
	} else {
		strcpy(camera_info, "ISP=");
		switch (cam_info->isp) {
			case CAM_INFO_ISP_TYPE_INTERNAL:
				strncat(camera_info, "INT;", strlen("INT;"));
				break;
			case CAM_INFO_ISP_TYPE_EXTERNAL:
				strncat(camera_info, "EXT;", strlen("EXT;"));
				break;
			case CAM_INFO_ISP_TYPE_SOC:
				strncat(camera_info, "SOC;", strlen("SOC;"));
				break;
			default:
				strncat(camera_info, "NULL;", strlen("NULL;"));
				break;
		}

		strncat(camera_info, "CALMEM=", strlen("CALMEM="));
		switch (cam_info->cal_memory) {
			case CAM_INFO_CAL_MEM_TYPE_NONE:
				strncat(camera_info, "N;", strlen("N;"));
				break;
			case CAM_INFO_CAL_MEM_TYPE_FROM:
			case CAM_INFO_CAL_MEM_TYPE_EEPROM:
			case CAM_INFO_CAL_MEM_TYPE_OTP:
				strncat(camera_info, "Y;", strlen("Y;"));
				break;
			default:
				strncat(camera_info, "NULL;", strlen("NULL;"));
				break;
		}

		strncat(camera_info, "READVER=", strlen("READVER="));
		switch (cam_info->read_version) {
			case CAM_INFO_READ_VER_SYSFS:
				strncat(camera_info, "SYSFS;", strlen("SYSFS;"));
				break;
			case CAM_INFO_READ_VER_CAMON:
				strncat(camera_info, "CAMON;", strlen("CAMON;"));
				break;
			default:
				strncat(camera_info, "NULL;", strlen("NULL;"));
				break;
		}

		strncat(camera_info, "COREVOLT=", strlen("COREVOLT="));
		switch (cam_info->core_voltage) {
			case CAM_INFO_CORE_VOLT_NONE:
				strncat(camera_info, "N;", strlen("N;"));
				break;
			case CAM_INFO_CORE_VOLT_USE:
				strncat(camera_info, "Y;", strlen("Y;"));
				break;
			default:
				strncat(camera_info, "NULL;", strlen("NULL;"));
				break;
		}

		strncat(camera_info, "UPGRADE=", strlen("UPGRADE="));
		switch (cam_info->upgrade) {
			case CAM_INFO_FW_UPGRADE_NONE:
				strncat(camera_info, "N;", strlen("N;"));
				break;
			case CAM_INFO_FW_UPGRADE_SYSFS:
				strncat(camera_info, "SYSFS;", strlen("SYSFS;"));
				break;
			case CAM_INFO_FW_UPGRADE_CAMON:
				strncat(camera_info, "CAMON;", strlen("CAMON;"));
				break;
			default:
				strncat(camera_info, "NULL;", strlen("NULL;"));
				break;
		}

		strncat(camera_info, "FWWRITE=", strlen("FWWRITE="));
		switch (cam_info->fw_write) {
			case CAM_INFO_FW_WRITE_NONE:
				strncat(camera_info, "N;", strlen("N;"));
				break;
			case CAM_INFO_FW_WRITE_OS:
				strncat(camera_info, "OS;", strlen("OS;"));
				break;
			case CAM_INFO_FW_WRITE_SD:
				strncat(camera_info, "SD;", strlen("SD;"));
				break;
			case CAM_INFO_FW_WRITE_ALL:
				strncat(camera_info, "ALL;", strlen("ALL;"));
				break;
			default:
				strncat(camera_info, "NULL;", strlen("NULL;"));
				break;
		}

		strncat(camera_info, "FWDUMP=", strlen("FWDUMP="));
		switch (cam_info->fw_dump) {
			case CAM_INFO_FW_DUMP_NONE:
				strncat(camera_info, "N;", strlen("N;"));
				break;
			case CAM_INFO_FW_DUMP_USE:
				strncat(camera_info, "Y;", strlen("Y;"));
				break;
			default:
				strncat(camera_info, "NULL;", strlen("NULL;"));
				break;
		}

		strncat(camera_info, "CC=", strlen("CC="));
		switch (cam_info->companion) {
			case CAM_INFO_COMPANION_NONE:
				strncat(camera_info, "N;", strlen("N;"));
				break;
			case CAM_INFO_COMPANION_USE:
				strncat(camera_info, "Y;", strlen("Y;"));
				break;
			default:
				strncat(camera_info, "NULL;", strlen("NULL;"));
				break;
		}

		strncat(camera_info, "OIS=", strlen("OIS="));
		switch (cam_info->ois) {
			case CAM_INFO_OIS_NONE:
				strncat(camera_info, "N;", strlen("N;"));
				break;
			case CAM_INFO_OIS_USE:
				strncat(camera_info, "Y;", strlen("Y;"));
				break;
			default:
				strncat(camera_info, "NULL;", strlen("NULL;"));
				break;
		}

		strncat(camera_info, "VALID=", strlen("VALID="));
		switch (cam_info->valid) {
			case CAM_INFO_INVALID:
				strncat(camera_info, "N;", strlen("N;"));
				break;
			case CAM_INFO_VALID:
				strncat(camera_info, "Y;", strlen("Y;"));
				break;
			default:
				strncat(camera_info, "NULL;", strlen("NULL;"));
				break;
		}

		if (cam_index == CAM_INFO_REAR2) {
			strncat(camera_info, "DUALOPEN=", strlen("DUALOPEN="));
			switch (cam_info->dual_open) {
			case CAM_INFO_SINGLE_OPEN:
				strncat(camera_info, "N;", strlen("N;"));
				break;
			case CAM_INFO_DUAL_OPEN:
				strncat(camera_info, "Y;", strlen("Y;"));
				break;
			default:
				strncat(camera_info, "NULL;", strlen("NULL;"));
				break;
			}
		}

		return sprintf(buf, "%s\n", camera_info);
	}
#endif
	strcpy(camera_info, "ISP=NULL;CALMEM=NULL;READVER=NULL;COREVOLT=NULL;UPGRADE=NULL;"
		"FWWRITE=NULL;FWDUMP=NULL;CC=NULL;OIS=NULL;VALID=N");

	if (cam_index == CAM_INFO_REAR2) {
		strncat(camera_info, ";DUALOPEN=N", strlen(";DUALOPEN=N"));
	}

	return sprintf(buf, "%s\n", camera_info);
}

static ssize_t camera_camfw_show(char *buf, enum is_cam_info_index cam_index, bool camfw_full)
{
	struct is_rom_info *finfo;
	struct is_rom_info *pinfo;
	struct is_cam_info *cam_info;
	char command_ack[20] = {0, };
	char sensor_name[20] = {0, };
	char *cam_fw;
	char *loaded_fw;
	char *phone_fw;
	int position;
	int rom_type;
	int rom_id;
	int rom_cal_index;
	int ret;
	bool other_vendor;
	bool reload;

	is_vender_check_hw_init_running();
	is_get_cam_info_from_index(&cam_info, cam_index);

	position = cam_info->internal_id;
	is_vendor_get_rom_info_from_position(position, &rom_type, &rom_id, &rom_cal_index);

	if (rom_type == ROM_TYPE_NONE) {
		ret = is_get_sensor_data(NULL, sensor_name, position);

		if (ret < 0)
			return sprintf(buf, "UNKNOWN UNKNOWN\n");
		else
			return sprintf(buf, "%s N\n", sensor_name);
	}

	if (rom_id == ROM_ID_NOTHING) {
		err("%s: invalid ROM ID [%d][%d][%d]", __func__, cam_index, position, rom_id);
		goto err_camfw;
	}

	info("%s: [%d][%d][%d]", __func__, cam_index, position, rom_id);

	is_sec_get_sysfs_finfo(&finfo, rom_id);
	is_sec_get_sysfs_pinfo(&pinfo, rom_id);

	reload = test_bit(IS_ROM_STATE_CAL_RELOAD, &finfo->rom_state);

	if (is_vendor_check_camera_running(position)) {
		info("%s: camera running [%d][%d][%d]", __func__, cam_index, position, rom_id);
		reload = false;
	}

	if (!camfw_full && reload) {
		read_from_firmware_version_reload(rom_id);
	} else {
		read_from_firmware_version(rom_id);
	}

	if (!is_sec_check_rom_ver(sysfs_core, rom_id)) {
		err("%s: invalid ROM version [%d][%d][%d]", __func__, cam_index, position, rom_id);
		goto err_camfw;
	}

	if (rom_cal_index == 1)
		cam_fw = finfo->header2_ver;
	else
		cam_fw = finfo->header_ver;

	if (rom_type == ROM_TYPE_FROM) {
		is_sec_get_loaded_fw(&loaded_fw);
	} else {
		switch(position) {
			case SENSOR_POSITION_REAR_TOF:
			case SENSOR_POSITION_FRONT_TOF:
				loaded_fw = "N";
				break;
			default :
				loaded_fw = cam_fw;
				break;
		}
	}

	phone_fw = "N";

	other_vendor = test_bit(IS_ROM_STATE_OTHER_VENDOR, &finfo->rom_state);

	if (!test_bit(IS_ROM_STATE_INVALID_ROM_VERSION, &finfo->rom_state)) {
		if (finfo->crc_error == 0 && !other_vendor) {
			if (camfw_full)
				return sprintf(buf, "%s %s %s\n", cam_fw, phone_fw, loaded_fw);
			else
				return sprintf(buf, "%s %s\n", cam_fw, loaded_fw);
		} else {
			err("%s: NG CRC Check Fail [%d][%d][%d]", __func__, cam_index, position, rom_id);
			if (position == SENSOR_POSITION_REAR_TOF ||
				position == SENSOR_POSITION_FRONT_TOF) {
				strcpy(command_ack, "N");
			} else {
				strcpy(command_ack, "NG");
			}

			if (camfw_full)
				return sprintf(buf, "%s %s %s\n", cam_fw, phone_fw, command_ack);
			else
				return sprintf(buf, "%s %s\n", cam_fw, command_ack);
		}
	} else {
		err("%s: FW VER CRC Check Fail [%d][%d][%d]", __func__, cam_index, position, rom_id);
	}

err_camfw:
	if (position == SENSOR_POSITION_REAR_TOF ||
		position == SENSOR_POSITION_FRONT_TOF) {
		strcpy(command_ack, "N");
	} else {
		strcpy(command_ack, "NG");
	}

	cam_fw = "NULL";

	if (position == SENSOR_POSITION_REAR)
		phone_fw = "NULL";
	else
		phone_fw = "N";

	if (camfw_full)
		return sprintf(buf, "%s %s %s\n", cam_fw, phone_fw, command_ack);
	else
		return sprintf(buf, "%s %s\n", cam_fw, command_ack);
}

static ssize_t camera_checkfw_user_show(char *buf, enum is_cam_info_index cam_index)
{
	struct is_rom_info *finfo;
	struct is_cam_info *cam_info;
	int position;
	int rom_type;
	int rom_id;
	int rom_cal_index;

	is_vender_check_hw_init_running();
	is_get_cam_info_from_index(&cam_info, cam_index);

	position = cam_info->internal_id;
	is_vendor_get_rom_info_from_position(position, &rom_type, &rom_id, &rom_cal_index);

	if (rom_id == ROM_ID_NOTHING) {
		err("%s: invalid ROM ID [%d][%d][%d]", __func__, cam_index, position, rom_id);
		goto err_user_checkfw;
	}

	is_sec_get_sysfs_finfo(&finfo, rom_id);

	read_from_firmware_version(rom_id);

	if (!is_sec_check_rom_ver(sysfs_core, rom_id)) {
		err(" NG, invalid ROM version");
		goto err_user_checkfw;
	}

	if (!test_bit(IS_ROM_STATE_INVALID_ROM_VERSION, &finfo->rom_state)) {
		if (test_bit(IS_CRC_ERROR_FIRMWARE, &finfo->crc_error)
			|| test_bit(IS_CRC_ERROR_ALL_SECTION, &finfo->crc_error)
			|| test_bit(IS_CRC_ERROR_DUAL_CAMERA, &finfo->crc_error)) {
			err(" NG, crc check fail");
			return sprintf(buf, "%s\n", "NG");
		} else {
			if (!test_bit(IS_ROM_STATE_LATEST_MODULE, &finfo->rom_state)) {
				err(" NG, not latest cam module");
				return sprintf(buf, "%s\n", "NG");
			} else {
				return sprintf(buf, "%s\n", "OK");
			}
		}
	} else {
		err(" NG, fw ver crc check fail");
	}

err_user_checkfw:
	return sprintf(buf, "%s\n", "NG");
}

static ssize_t camera_checkfw_factory_show(char *buf, enum is_cam_info_index cam_index)
{
	struct is_rom_info *finfo;
	struct is_cam_info *cam_info;
	int position;
	int rom_type;
	int rom_id;
	int rom_cal_index;

	is_vender_check_hw_init_running();
	is_get_cam_info_from_index(&cam_info, cam_index);

	position = cam_info->internal_id;
	is_vendor_get_rom_info_from_position(position, &rom_type, &rom_id, &rom_cal_index);

	if (rom_id == ROM_ID_NOTHING) {
		err("%s: invalid ROM ID [%d][%d][%d]", __func__, cam_index, position, rom_id);
		goto err_factory_checkfw;
	}

	is_sec_get_sysfs_finfo(&finfo, rom_id);

	read_from_firmware_version(rom_id);

	if (!is_sec_check_rom_ver(sysfs_core, rom_id)) {
		err(" NG, invalid ROM version");
		goto err_factory_checkfw;
	}

	if (!test_bit(IS_ROM_STATE_INVALID_ROM_VERSION, &finfo->rom_state)) {
		if (finfo->crc_error == 0) {
			if (!test_bit(IS_ROM_STATE_FINAL_MODULE, &finfo->rom_state)) {
				err(" NG, not final cam module");
				return sprintf(buf, "%s\n", "NG_VER");
			} else {
				return sprintf(buf, "%s\n", "OK");
			}
		} else {
			err(" NG, crc check fail");
			return sprintf(buf, "%s\n", "NG_CRC");
		}
	} else {
		err(" NG, fw ver crc check fail");
	}

err_factory_checkfw:
	return sprintf(buf, "%s\n", "NG_VER");
}

static ssize_t camera_sensorid_exif_show(char *buf, enum is_cam_info_index cam_index)
{
	struct is_rom_info *finfo;
	struct is_cam_info *cam_info;
	int position;
	int rom_type;
	int rom_id;
	int rom_cal_index;

	is_get_cam_info_from_index(&cam_info, cam_index);

	position = cam_info->internal_id;
	is_vendor_get_rom_info_from_position(position, &rom_type, &rom_id, &rom_cal_index);

	if (rom_type == ROM_TYPE_NONE) {
		err("%s: not support, no rom for camera[%d][%d]", __func__, cam_index, position);
		goto err_sensorid_exif;
	} else if (rom_id == ROM_ID_NOTHING) {
		err("%s: invalid ROM ID [%d][%d]", __func__, position, rom_id);
		goto err_sensorid_exif;
	}

	is_sec_get_sysfs_finfo(&finfo, rom_id);

	if (rom_cal_index == 1)
		memcpy(buf, finfo->rom_sensor2_id, IS_SENSOR_ID_SIZE);
	else
		memcpy(buf, finfo->rom_sensor_id, IS_SENSOR_ID_SIZE);

	return IS_SENSOR_ID_SIZE;

err_sensorid_exif:
	memset(buf, '\0', IS_SENSOR_ID_SIZE);

	return IS_SENSOR_ID_SIZE;
}

static ssize_t camera_mtf_exif_show(char *buf, enum is_cam_info_index cam_index, int f_index)
{
	struct is_rom_info *finfo;
	struct is_cam_info *cam_info;
	int position;
	int rom_type;
	int rom_id;
	int rom_cal_index;
	int mtf_offset;
	char *cal_buf;

	is_get_cam_info_from_index(&cam_info, cam_index);

	position = cam_info->internal_id;
	is_vendor_get_rom_info_from_position(position, &rom_type, &rom_id, &rom_cal_index);

	if (rom_type == CAM_INFO_CAL_MEM_TYPE_NONE) {
		err("%s: not support, no rom for camera[%d][%d]", __func__, cam_index, position);
		goto err_mtf_exif;
	} else if (rom_id == ROM_ID_NOTHING) {
		err("%s: invalid ROM ID [%d][%d]", __func__, position, rom_id);
		goto err_mtf_exif;
	}

	is_sec_get_sysfs_finfo(&finfo, rom_id);
	is_sec_get_cal_buf(&cal_buf, rom_id);

	if (rom_cal_index == 1) {
		mtf_offset = finfo->rom_header_sensor2_mtf_data_addr;
	} else {
		switch(f_index) {
		case FNUMBER_1ST :
			mtf_offset = finfo->rom_header_mtf_data_addr;
			break;
		default :
			err("%s: invalid f_index [%d][%d], so set default", __func__, position, f_index);
			mtf_offset = finfo->rom_header_mtf_data_addr;
			break;
		}
	}

	if (mtf_offset != -1)
		memcpy(buf, &cal_buf[mtf_offset], IS_RESOLUTION_DATA_SIZE);
	else
		memset(buf, 0, IS_RESOLUTION_DATA_SIZE);

	return IS_RESOLUTION_DATA_SIZE;

err_mtf_exif:
	memset(buf, '\0', IS_RESOLUTION_DATA_SIZE);

	return IS_RESOLUTION_DATA_SIZE;
}

static ssize_t camera_paf_cal_check_show(char *buf, enum is_cam_info_index cam_index, int f_index)
{
	struct is_rom_info *finfo;
	struct is_cam_info *cam_info;
	int position;
	int rom_type;
	int rom_id;
	int rom_cal_index;
	int paf_cal_start_addr;
	char *cal_buf;
	u8 data[5] = {0, };
	u32 paf_err_data_result = 0;

	is_get_cam_info_from_index(&cam_info, cam_index);

	position = cam_info->internal_id;
	is_vendor_get_rom_info_from_position(position, &rom_type, &rom_id, &rom_cal_index);

	if (rom_type == CAM_INFO_CAL_MEM_TYPE_NONE) {
		err("%s: not support, no rom for camera[%d][%d]", __func__, cam_index, position);
		goto err_paf_cal_check;
	} else if (rom_id == ROM_ID_NOTHING) {
		err("%s: invalid ROM ID [%d][%d]", __func__, position, rom_id);
		goto err_paf_cal_check;
	}

	read_from_firmware_version(rom_id);

	is_sec_get_sysfs_finfo(&finfo, rom_id);
	is_sec_get_cal_buf(&cal_buf, rom_id);

	if (rom_cal_index == ROM_CAL_SLAVE0) {
		paf_cal_start_addr = finfo->rom_sensor2_paf_cal_start_addr;
	} else {
		switch(f_index) {
		case FNUMBER_1ST :
			paf_cal_start_addr = finfo->rom_paf_cal_start_addr;
			break;

		default :
			err("%s: invalid f_index [%d][%d], so set default", __func__, position, f_index);
			paf_cal_start_addr = finfo->rom_paf_cal_start_addr;
			break;
		}
	}

	if (paf_cal_start_addr != -1)
		memcpy(data, &cal_buf[paf_cal_start_addr + IS_PAF_CAL_ERR_CHECK_OFFSET], 4);
	else
		memset(data, 0, 4);

	paf_err_data_result = *data | ( *(data + 1) << 8) | ( *(data + 2) << 16) | (*(data + 3) << 24);

	return sprintf(buf, "%08X\n", paf_err_data_result);

err_paf_cal_check:
	memset(data, '\0', 4);

	return sprintf(buf, "%08X\n", paf_err_data_result);
}

static ssize_t camera_moduleid_show(char *buf, enum is_cam_info_index cam_index)
{
	struct is_rom_info *finfo;
	struct is_cam_info *cam_info;

	int position;
	int rom_type;
	int rom_id;
	int rom_cal_index;

	is_get_cam_info_from_index(&cam_info, cam_index);

	position = cam_info->internal_id;
	is_vendor_get_rom_info_from_position(position, &rom_type, &rom_id, &rom_cal_index);

	if (rom_type == ROM_TYPE_NONE) {
		err("%s: not support, no rom for camera[%d][%d]", __func__, cam_index, position);
		goto err_moduleid;
	} else if (rom_id == ROM_ID_NOTHING) {
		err("%s: invalid ROM ID [%d][%d]", __func__, position, rom_id);
		goto err_moduleid;
	}

	read_from_firmware_version(rom_id);
	is_sec_get_sysfs_finfo(&finfo, rom_id);

	if (is_sec_is_valid_moduleid(finfo->rom_module_id)) {
		return sprintf(buf, "%c%c%c%c%c%02X%02X%02X%02X%02X\n",
			finfo->rom_module_id[0], finfo->rom_module_id[1], finfo->rom_module_id[2],
			finfo->rom_module_id[3], finfo->rom_module_id[4], finfo->rom_module_id[5],
			finfo->rom_module_id[6], finfo->rom_module_id[7], finfo->rom_module_id[8],
			finfo->rom_module_id[9]);
	}

err_moduleid:
	return sprintf(buf, "%s\n", "0000000000");
}

static ssize_t camera_afcal_show(char *buf, enum is_cam_info_index cam_index)
{
	struct is_rom_info *finfo;
	struct is_cam_info *cam_info;

	int position;
	int rom_type;
	int rom_id;
	int rom_cal_index;
	char *cal_buf;
	bool is_front = false;
	int i;
	char tempbuf[30] = {0, };
	char tmpChar[5] = {0, };

	is_get_cam_info_from_index(&cam_info, cam_index);

	position = cam_info->internal_id;
	is_vendor_get_rom_info_from_position(position, &rom_type, &rom_id, &rom_cal_index);

	if (rom_type == ROM_TYPE_NONE) {
		err("%s: not support, no rom for camera[%d][%d]", __func__, cam_index, position);
		goto err_afcal;
	} else if (rom_id == ROM_ID_NOTHING) {
		err("%s: invalid ROM ID [%d][%d]", __func__, position, rom_id);
		goto err_afcal;
	}

	read_from_firmware_version(rom_id);
	is_sec_get_sysfs_finfo(&finfo, rom_id);
	is_sec_get_cal_buf(&cal_buf, rom_id);

	if (position == SENSOR_POSITION_FRONT ||
		position == SENSOR_POSITION_FRONT2 ||
		position == SENSOR_POSITION_FRONT3 ||
		position == SENSOR_POSITION_FRONT4 ) {
		is_front = true;
	}

	strcpy(tempbuf, "20");

	for (i = 0; i < AF_CAL_MAX; i++) {
		if (rom_cal_index == 1) {
			if (i >= finfo->rom_sensor2_af_cal_addr_len) {
				strncat(tempbuf, " N", strlen(" N"));
			} else {
				sprintf(tmpChar, " %d", *((s32*)&cal_buf[finfo->rom_sensor2_af_cal_addr[i]]));
				strncat(tempbuf, tmpChar, strlen(tmpChar));
			}
		} else {
			if (i >= finfo->rom_af_cal_addr_len) {
				strncat(tempbuf, " N", strlen(" N"));
			} else {
				sprintf(tmpChar, " %d", *((s32*)&cal_buf[finfo->rom_af_cal_addr[i]]));
				strncat(tempbuf, tmpChar, strlen(tmpChar));
			}
		}
	}

	return sprintf(buf, "%s\n", tempbuf);

err_afcal:
	return sprintf(buf, "1 N N\n");
}


/* PAF offset read */
static ssize_t camera_paf_offset_show(char *buf, bool is_mid, int f_index)
{
	char *cal_buf;
	char tempbuf[10];
	int i;
	int divider;
	int paf_offset_count;
	u32 paf_offset_base = 0;
	struct is_rom_info *finfo;

	read_from_firmware_version(ROM_ID_REAR);
	is_sec_get_sysfs_finfo(&finfo, ROM_ID_REAR);
	is_sec_get_cal_buf(&cal_buf, ROM_ID_REAR);

	switch(f_index) {
	case FNUMBER_1ST :
#ifdef CAMERA_REAR_PAFCAL
		if (finfo->rom_paf_cal_start_addr == -1) {
			err("%s fail, check DT", __func__);
			strncat(buf, "\n", strlen("\n"));
			return strlen(buf);
		}
		if (is_mid)
			paf_offset_base = finfo->rom_paf_cal_start_addr + IS_PAF_OFFSET_MID_OFFSET;
		else
			paf_offset_base = finfo->rom_paf_cal_start_addr + IS_PAF_OFFSET_PAN_OFFSET;
#endif
		break;
	default :
		err("%s: invalid f_index [%d][%d], so set default", __func__, is_mid, f_index);
#ifdef CAMERA_REAR_PAFCAL
		if (finfo->rom_paf_cal_start_addr == -1) {
			err("%s fail, check DT", __func__);
			strncat(buf, "\n", strlen("\n"));
			return strlen(buf);
		}

		if (is_mid)
			paf_offset_base = finfo->rom_paf_cal_start_addr + IS_PAF_OFFSET_MID_OFFSET;
		else
			paf_offset_base = finfo->rom_paf_cal_start_addr + IS_PAF_OFFSET_PAN_OFFSET;
#endif
		break;
	}

	if (paf_offset_base == 0) {
		strncat(buf, "\n", strlen("\n"));
		err("%s: no definition for paf offset mid/pan[%d] f[%d]", __func__, is_mid, f_index);
		return strlen(buf);
	}

	if (is_mid) {
		divider = 8;
		paf_offset_count = IS_PAF_OFFSET_MID_SIZE / divider;
	} else {
		divider = 2;
		paf_offset_count = IS_PAF_OFFSET_PAN_SIZE / divider;
	}

	memset(tempbuf, 0, sizeof(tempbuf));

	for (i = 0; i < paf_offset_count - 1; i++) {
		sprintf(tempbuf, "%d,", *((s16 *)&cal_buf[paf_offset_base + divider * i]));
		strncat(buf, tempbuf, strlen(tempbuf));
		memset(tempbuf, 0, sizeof(tempbuf));
	}
	sprintf(tempbuf, "%d", *((s16 *)&cal_buf[paf_offset_base + divider * i]));
	strncat(buf, tempbuf, strlen(tempbuf));

	strncat(buf, "\n", strlen("\n"));
	return strlen(buf);
}

static ssize_t camera_phy_tune_show(char *buf, enum is_cam_info_index cam_index)
{
	char tempBuf[30];
	int position;
	struct is_cam_info *cam_info;

	is_get_cam_info_from_index(&cam_info, cam_index);
	position = cam_info->internal_id;

	memset(tempBuf, 0, sizeof(tempBuf));
	test_phy_margin_check(tempBuf, position);
	sprintf(buf, "%s", tempBuf);
	strncat(buf, "\n", strlen("\n"));

	return strlen(buf);
}
/* end of common function for sysfs */

static ssize_t camera_ssrm_camera_info_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct ssrm_send_format recv_data;
	struct ssrm_camera_data *per_camera_info;
	struct ssrm_camera_data_common *common_info;
	int index = -1;
	int i = 0, capturetmp[2] = {SsrmCameraInfoExt.capture, SsrmCameraInfoExt.HDRCapture};

	if (count != sizeof(struct ssrm_send_format)) {
		err("err not matched format");
		return -EINVAL;
	}
	memcpy(&recv_data, buf, sizeof(struct ssrm_send_format));

	per_camera_info = &recv_data.per_camera_info;
	common_info = &recv_data.common_info;
	common_info->capture += capturetmp[0];
	common_info->HDRCapture += capturetmp[1];

	switch (recv_data.operation) {
	case SSRM_CAMERA_INFO_CLEAR:
		for (i = 0; i < SENSOR_POSITION_MAX; i++) { /* clear */
			if (SsrmCameraInfo[i].ID == per_camera_info->ID) {
				SsrmCameraInfo[i].ID = -1;
				ssrmCameraInfoCnt--;
			}
		}
		break;

	case SSRM_CAMERA_INFO_SET:
		for (i = 0; i < SENSOR_POSITION_MAX; i++) { /* find empty space*/
			if (SsrmCameraInfo[i].ID == -1) {
				index = i;
				break;
			}
		}

		if (index == -1)
			return -EPERM;

		memcpy(&SsrmCameraInfo[i], per_camera_info, sizeof(struct ssrm_camera_data));
		memcpy(&SsrmCameraInfoExt, common_info, sizeof(struct ssrm_camera_data_common));
		ssrmCameraInfoCnt++;
		break;

	case SSRM_CAMERA_INFO_UPDATE:
		for (i = 0; i < SENSOR_POSITION_MAX; i++) {
			if (SsrmCameraInfo[i].ID == per_camera_info->ID) {
				index = i;
				break;
			}
		}
		if (index == -1)
			return -EPERM;

		memcpy(&SsrmCameraInfo[i], per_camera_info, sizeof(struct ssrm_camera_data));
		memcpy(&SsrmCameraInfoExt, common_info, sizeof(struct ssrm_camera_data_common));
		break;
	default:
		break;
	}

	return count;
}

static ssize_t camera_ssrm_camera_info_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	char temp_buffer[65] = {0,};
	int i = 0, zoomtmp = 0;

	if (ssrmCameraInfoCnt <= 0) {
		SsrmCameraInfoExt.capture = 0;
		SsrmCameraInfoExt.HDRCapture = 0;
		return 0;
	}

	SSRM_INFO_PRINT(SsrmCameraInfoExt, MODE);
	strncat(buf, "\n", strlen("\n"));

	for (i = 0; i < SENSOR_POSITION_MAX; i++) {
		if (SsrmCameraInfo[i].ID != -1) {
			strncat(buf, "[", strlen("["));
			sprintf(temp_buffer, "ID=%d<%d>;", SsrmCameraInfo[i].ID, SsrmCameraInfo[i].ON);
			strncat(buf, temp_buffer, strlen(temp_buffer));
			if (SsrmCameraInfo[i].ISPWidth && SsrmCameraInfo[i].ISPHeight) {
				sprintf(temp_buffer, "ISP=%d,%d;",
						SsrmCameraInfo[i].ISPWidth, SsrmCameraInfo[i].ISPHeight);
				strncat(buf, temp_buffer, strlen(temp_buffer));
			}
			strncat(buf, "]\n", strlen("]\n"));
		}
	}

	if (SsrmCameraInfoExt.minFPS && SsrmCameraInfoExt.maxFPS) {
		sprintf(temp_buffer, "[FPS=%d,%d(%d);",
				SsrmCameraInfoExt.minFPS, SsrmCameraInfoExt.maxFPS, SsrmCameraInfoExt.FPSHint);
		strncat(buf, temp_buffer, strlen(temp_buffer));
	}
	if (SsrmCameraInfoExt.previewWidth && SsrmCameraInfoExt.previewHeight) {
		sprintf(temp_buffer, "SIZE=%d,%d;",
				SsrmCameraInfoExt.previewWidth, SsrmCameraInfoExt.previewHeight);
		strncat(buf, temp_buffer, strlen(temp_buffer));
	}
	sprintf(temp_buffer, "cap=%d(%d);", SsrmCameraInfoExt.capture, SsrmCameraInfoExt.HDRCapture);
	strncat(buf, temp_buffer, strlen(temp_buffer));
	SsrmCameraInfoExt.capture = SsrmCameraInfoExt.HDRCapture = 0;

	if (SsrmCameraInfoExt.zoom >= 1000) {
	    zoomtmp = SsrmCameraInfoExt.zoom / 1000;
	    sprintf(temp_buffer, "zoom=%dx;", zoomtmp);
	} else {
	    zoomtmp = SsrmCameraInfoExt.zoom / 100;
	    sprintf(temp_buffer, "zoom=0.%dx;", zoomtmp);
	}
	strncat(buf, temp_buffer, strlen(temp_buffer));

	SSRM_INFO_PRINT(SsrmCameraInfoExt, ssteady);
	SSRM_INFO_PRINT(SsrmCameraInfoExt, HDR10);
	SSRM_INFO_PRINT(SsrmCameraInfoExt, flash);
	SSRM_INFO_PRINT(SsrmCameraInfoExt, mem);
	strncat(buf, "]\n", strlen("]\n"));

	return strlen(buf);
}

static ssize_t camera_front_camtype_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	char sensor_maker[50];
	char sensor_name[50];
	int ret;

	ret = is_get_sensor_data(sensor_maker, sensor_name, SENSOR_POSITION_FRONT);

	if (ret < 0)
		return sprintf(buf, "UNKNOWN_UNKNOWN_EXYNOS_IS\n");
	else
		return sprintf(buf, "%s_%s_EXYNOS_IS\n", sensor_maker, sensor_name);
}

static ssize_t camera_front_camfw_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return camera_camfw_show(buf, CAM_INFO_FRONT, false);
}

#if defined(CAMERA_EEPROM_SUPPORT_FRONT)
static ssize_t camera_front_camfw_full_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return camera_camfw_show(buf, CAM_INFO_FRONT, true);
}

static ssize_t camera_front_checkfw_factory_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return camera_checkfw_factory_show(buf, CAM_INFO_FRONT);
}
#endif

static ssize_t camera_front_info_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return camera_info_show(buf, CAM_INFO_FRONT);
}

static ssize_t camera_front_mipi_clock_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct is_core *core;
	struct is_device_sensor *device;
	struct is_module_enum *module;
	char mipi_string[20] = {0, };
	int i;
	struct device *is_dev;

	is_dev = is_get_is_dev();

	if (!is_dev) {
		dev_err(dev, "%s: is_dev is not yet probed", __func__);
		return -ENODEV;
	}

	core = is_get_is_core();
	if (!core) {
		dev_err(dev, "%s: core is NULL", __func__);
		return -EINVAL;
	}

	for (i = 0; i < SENSOR_POSITION_MAX; i++) {
		device = &core->sensor[i];
		is_search_sensor_module_with_position(&core->sensor[i],
				SENSOR_POSITION_FRONT, &module);
		if (module)
			break;
	}

	is_vendor_get_mipi_clock_string(device, mipi_string);

	return sprintf(buf, "%s\n", mipi_string);
}

static ssize_t camera_front_phy_tune_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return camera_phy_tune_show(buf, CAM_INFO_FRONT);
}

#if defined(CONFIG_CAMERA_FROM) && defined(CAMERA_MODULE_DUALIZE)
static ssize_t camera_rear_writefw_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
/* Disabled for product. Security Hole */
#if 0
	struct device *is_dev = is_get_is_dev();
	int ret = 0;

	ret = is_sec_write_fw(sysfs_core, is_dev);

	if (ret)
		return sprintf(buf, "NG\n");
	else
		return sprintf(buf, "OK\n");
#else
	return sprintf(buf, "OK\n");
#endif
}
#endif

static ssize_t camera_rear_camtype_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	char sensor_maker[50];
	char sensor_name[50];
	int ret;

	ret = is_get_sensor_data(sensor_maker, sensor_name, SENSOR_POSITION_REAR);

	if (ret < 0)
		return sprintf(buf, "UNKNOWN_UNKNOWN_EXYNOS_IS\n");
	else
		return sprintf(buf, "%s_%s_EXYNOS_IS\n", sensor_maker, sensor_name);
}

#ifdef CAMERA_REAR3
static ssize_t camera_rear3_camtype_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	char sensor_maker[50];
	char sensor_name[50];
	int ret;

	ret = is_get_sensor_data(sensor_maker, sensor_name, SENSOR_POSITION_REAR3);

	if (ret < 0)
		return sprintf(buf, "UNKNOWN_UNKNOWN_EXYNOS_IS\n");
	else
		return sprintf(buf, "%s_%s_EXYNOS_IS\n", sensor_maker, sensor_name);
}
#endif

static ssize_t camera_rear_camfw_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return camera_camfw_show(buf, CAM_INFO_REAR, false);
}

static ssize_t camera_rear_camfw_full_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return camera_camfw_show(buf, CAM_INFO_REAR, true);
}

static ssize_t camera_rear_checkfw_user_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return camera_checkfw_user_show(buf, CAM_INFO_REAR);
}

static ssize_t camera_rear_checkfw_factory_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return camera_checkfw_factory_show(buf, CAM_INFO_REAR);
}

static ssize_t camera_rear_info_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return camera_info_show(buf, CAM_INFO_REAR);
}

static ssize_t camera_rear_phy_tune_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return camera_phy_tune_show(buf, CAM_INFO_REAR);
}

static ssize_t camera_supported_cameraIds_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	char temp_buf[SENSOR_POSITION_MAX];
	char *end = "\n";
	int i;

	for(i = 0; i < common_cam_infos.max_supported_camera; i++)	{
		sprintf(temp_buf, "%d ", common_cam_infos.supported_camera_ids[i]);
		strncat(buf, temp_buf, strlen(temp_buf));
	}
	strncat(buf, end, strlen(end));

	return strlen(buf);
}

#ifdef READ_DUAL_CAL_FIRMWARE_DATA
#ifdef USE_KERNEL_VFS_READ_WRITE
int is_dual_cal_read_firmware(char *name, int offset, int size)
{
	int ret = 0;
	long fsize, nread;
	struct file *fp;
	char *temp_buf = NULL;

	info("%s: E", __func__);

	fp = filp_open(name, O_RDONLY, 0);
	if (IS_ERR_OR_NULL(fp)) {
		ret = PTR_ERR(fp);
		err("filp_open(%s) fail(%d)!!\n", name, ret);
		goto p_err;
	}

	temp_buf = vmalloc(size);
	if (!temp_buf) {
		err("failed to allocate memory");
		ret = -ENOMEM;
		goto p_err;
	}

	fsize = fp->f_path.dentry->d_inode->i_size;
	if (fsize < offset + size) {
		err("dual cal data not exist");
		ret = -EIO;
		goto p_err;
	}
	info("dual cal fw start read sdcard, file path %s, size %lu Bytes , request size=%d\n", name, fsize,size);

	fp->f_pos = offset;
	info("cal read f_pos set offset %x", fp->f_pos);
	nread = kernel_read(fp,  (void *)temp_buf, size, &fp->f_pos);

	if (nread != size) {
		err("failed to read dual cal data from file, (%ld) Bytes", nread);
		ret = -EIO;
		goto p_err;
	}

	/* Cal data save */
	memcpy(dual_cal_data, temp_buf, DUAL_CAL_DATA_SIZE_DEFAULT);
	info("%s cal data copy size:%d bytes", __func__, DUAL_CAL_DATA_SIZE_DEFAULT);

p_err:
	if (temp_buf)
		vfree(temp_buf);

	if (!IS_ERR_OR_NULL(fp))
		filp_close(fp, NULL);

	info("%s: X", __func__);
	return ret;
}
#else
int is_dual_cal_read_firmware(struct device *dev, char *path, char *name, int offset, int size)
{
	int ret = 0;
	struct is_binary bin;

	info("%s: E", __func__);

	setup_binary_loader(&bin, 3, -EAGAIN, NULL, NULL);
	ret = request_binary(&bin, path, name, dev);

	if (ret) {
		err("filp_open(%s) fail(%d)!!\n", name, ret);
		goto p_err;
	}

	if (bin.size != size) {
		err("failed to read dual cal data from file, (%ld) Bytes", bin.size);
		ret = -EIO;
		goto p_err;
	}

	info("dual cal fw start read sdcard, file path %s, size %lu Bytes , request size=%d\n", name, bin.size, size);

	/* Cal data save */
	memcpy(dual_cal_data, (char *)bin.data, bin.size);
	info("%s cal data copy size:%d bytes", __func__, bin.size);

p_err:
	release_binary(&bin);
	info("%s: X", __func__);
	return ret;
}
#endif
#endif

#ifdef CAMERA_REAR_DUAL_CAL
static ssize_t camera_rear_dualcal_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int ret;
	int copy_size = 0;
	char *temp_buf;

	ret = is_get_dual_cal_buf(SENSOR_POSITION_REAR, &temp_buf, &copy_size);
	if (ret < 0) {
		err("%s: Fail to get dualcal of %d", __func__, SENSOR_POSITION_REAR);
	} else {
		info("%s: success to get dualcal of %d", __func__, SENSOR_POSITION_REAR);
		memcpy(buf, temp_buf, copy_size);
	}

	return copy_size;
}

static ssize_t camera_rear2_dualcal_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int ret;
	int copy_size = 0;
	char *temp_buf;

	ret = is_get_dual_cal_buf(SENSOR_POSITION_REAR3, &temp_buf, &copy_size);
	if (ret < 0) {
		err("%s: Fail to get dualcal of %d", __func__, SENSOR_POSITION_REAR3);
	} else {
		info("%s: success to get dualcal of %d", __func__, SENSOR_POSITION_REAR3);
		memcpy(buf, temp_buf, copy_size);
	}

	return copy_size;
}

static ssize_t camera_rear3_dualcal_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int ret;
	int copy_size = 0;
	char *temp_buf;

	ret = is_get_dual_cal_buf(SENSOR_POSITION_REAR2, &temp_buf, &copy_size);
	if (ret < 0) {
		err("%s: Fail to get dualcal of %d", __func__, SENSOR_POSITION_REAR2);
	} else {
		info("%s: success to get dualcal of %d", __func__, SENSOR_POSITION_REAR2);
		memcpy(buf, temp_buf, copy_size);
	}

#ifdef READ_DUAL_CAL_FIRMWARE_DATA
	info("  load multical Cal Data %s \n", DUAL_CAL_DATA_PATH);
	copy_size = DUAL_CAL_DATA_SIZE_DEFAULT;
#ifdef USE_KERNEL_VFS_READ_WRITE
	ret = is_dual_cal_read_firmware(DUAL_CAL_DATA_PATH, 0,
			DUAL_CAL_DATA_SIZE_DEFAULT);
#else
	ret = is_dual_cal_read_firmware(dev, DUAL_CAL_DATA_PATH, DUAL_CAL_DATA_BIN_NAME, 0,
			DUAL_CAL_DATA_SIZE_DEFAULT);
#endif
	if (ret < 0) {
		err("CAL read %s is fail\n", DUAL_CAL_DATA_PATH);
		return 0;
	} else {
		info("%s: success to get dualcal from firmware of %d", __func__, SENSOR_POSITION_REAR2);
		memcpy(buf, dual_cal_data, copy_size);
	}
#endif

	return copy_size;
}

static ssize_t camera_rear3_dualcal_size_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int ret;
	int copy_size = 0;
	char *temp_buf;

	ret = is_get_dual_cal_buf(SENSOR_POSITION_REAR2, &temp_buf, &copy_size);
	if (ret < 0) {
		err("%s: Fail to get dualcal of %d", __func__, SENSOR_POSITION_REAR2);
	} else {
		info("%s: success to get dualcal of %d", __func__, SENSOR_POSITION_REAR2);
		memcpy(buf, temp_buf, copy_size);
	}

#ifdef READ_DUAL_CAL_FIRMWARE_DATA
	info("  load multical Cal Data %s \n", DUAL_CAL_DATA_PATH);
	copy_size = DUAL_CAL_DATA_SIZE_DEFAULT;
#ifdef USE_KERNEL_VFS_READ_WRITE
	ret = is_dual_cal_read_firmware(DUAL_CAL_DATA_PATH, 0,
			DUAL_CAL_DATA_SIZE_DEFAULT);
#else
	ret = is_dual_cal_read_firmware(dev, DUAL_CAL_DATA_PATH, DUAL_CAL_DATA_BIN_NAME, 0,
			DUAL_CAL_DATA_SIZE_DEFAULT);
#endif
	if (ret < 0) {
		err("CAL read %s is fail\n", DUAL_CAL_DATA_PATH);
		return 0;
	} else {
		info("%s: success to get dualcal from firmware of %d", __func__, SENSOR_POSITION_REAR2);
	}
#endif

	return sprintf(buf, "%d",copy_size );
}
#endif

#ifdef CAMERA_FRONT_DUAL_CAL
static ssize_t camera_front_dualcal_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
	int ret;
	int copy_size = 0;
	char *temp_buf;

	ret = is_get_dual_cal_buf(SENSOR_POSITION_FRONT, &temp_buf, &copy_size);
	if (ret < 0) {
		err("%s: Fail to get dualcal of %d", __func__, SENSOR_POSITION_FRONT);
	} else {
		info("%s: success to get dualcal of %d", __func__, SENSOR_POSITION_FRONT);
		memcpy(buf, temp_buf, copy_size);
	}

	return copy_size;
}

static ssize_t camera_front2_dualcal_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
	int ret;
	int copy_size = 0;
	char *temp_buf;

	ret = is_get_dual_cal_buf(SENSOR_POSITION_FRONT2, &temp_buf, &copy_size);
	if (ret < 0) {
		err("%s: Fail to get dualcal of %d", __func__, SENSOR_POSITION_FRONT2);
	} else {
		info("%s: success to get dualcal of %d", __func__, SENSOR_POSITION_FRONT2);
		memcpy(buf, temp_buf, copy_size);
	}

	return copy_size;
}
#endif

#ifdef CAMERA_REAR2
static ssize_t camera_rear2_info_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return camera_info_show(buf, CAM_INFO_REAR2);
}

static ssize_t camera_rear2_camfw_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return camera_camfw_show(buf, CAM_INFO_REAR2, false);
}

static ssize_t camera_rear2_camfw_full_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return camera_camfw_show(buf, CAM_INFO_REAR2, true);
}

static ssize_t camera_rear2_checkfw_user_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return camera_checkfw_user_show(buf, CAM_INFO_REAR2);
}

static ssize_t camera_rear2_checkfw_factory_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return camera_checkfw_factory_show(buf, CAM_INFO_REAR2);
}

static ssize_t camera_rear2_sensorid_exif_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return camera_sensorid_exif_show(buf, CAM_INFO_REAR2);
}

static ssize_t camera_rear2_mtf_exif_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return camera_mtf_exif_show(buf, CAM_INFO_REAR2, FNUMBER_1ST);
}

static ssize_t camera_rear2_phy_tune_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return camera_phy_tune_show(buf, CAM_INFO_REAR2);
}

#ifdef CAMERA_REAR2_MODULEID
static ssize_t camera_rear2_moduleid_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return camera_moduleid_show(buf, CAM_INFO_REAR2);
}
#endif

#ifdef CAMERA_REAR2_AFCAL
static ssize_t camera_rear2_afcal_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return camera_afcal_show(buf, CAM_INFO_REAR2);
}

static ssize_t camera_rear2_paf_cal_check_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return camera_paf_cal_check_show(buf, CAM_INFO_REAR2, FNUMBER_1ST);
}
#endif
#endif

#ifdef CAMERA_REAR3
static ssize_t camera_rear3_info_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return camera_info_show(buf, CAM_INFO_REAR3);
}

static ssize_t camera_rear3_camfw_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return camera_camfw_show(buf, CAM_INFO_REAR3, false);
}

static ssize_t camera_rear3_camfw_full_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return camera_camfw_show(buf, CAM_INFO_REAR3, true);
}

static ssize_t camera_rear3_checkfw_user_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return camera_checkfw_user_show(buf, CAM_INFO_REAR3);
}

static ssize_t camera_rear3_checkfw_factory_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return camera_checkfw_factory_show(buf, CAM_INFO_REAR3);
}

static ssize_t camera_rear3_sensorid_exif_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return camera_sensorid_exif_show(buf, CAM_INFO_REAR3);
}

static ssize_t camera_rear3_mtf_exif_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return camera_mtf_exif_show(buf, CAM_INFO_REAR3, FNUMBER_1ST);
}

static ssize_t camera_rear3_paf_cal_check_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return camera_paf_cal_check_show(buf, CAM_INFO_REAR3, FNUMBER_1ST);
}

static ssize_t camera_rear3_phy_tune_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return camera_phy_tune_show(buf, CAM_INFO_REAR3);
}

#ifdef CAMERA_REAR3_MODULEID
static ssize_t camera_rear3_moduleid_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return camera_moduleid_show(buf, CAM_INFO_REAR3);
}
#endif

#ifdef CAMERA_REAR3_AFCAL
static ssize_t camera_rear3_afcal_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return camera_afcal_show(buf, CAM_INFO_REAR3);
}
#endif

#endif

#ifdef CAMERA_REAR4
static ssize_t camera_rear4_info_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return camera_info_show(buf, CAM_INFO_REAR4);
}

static ssize_t camera_rear4_camfw_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return camera_camfw_show(buf, CAM_INFO_REAR4, false);
}

static ssize_t camera_rear4_camfw_full_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return camera_camfw_show(buf, CAM_INFO_REAR4, true);
}

static ssize_t camera_rear4_checkfw_user_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return camera_checkfw_user_show(buf, CAM_INFO_REAR4);
}

static ssize_t camera_rear4_checkfw_factory_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return camera_checkfw_factory_show(buf, CAM_INFO_REAR4);
}

static ssize_t camera_rear4_sensorid_exif_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return camera_sensorid_exif_show(buf, CAM_INFO_REAR4);
}

static ssize_t camera_rear4_mtf_exif_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return camera_mtf_exif_show(buf, CAM_INFO_REAR4, FNUMBER_1ST);
}

static ssize_t camera_rear4_phy_tune_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return camera_phy_tune_show(buf, CAM_INFO_REAR4);
}

#ifdef CAMERA_REAR4_MODULEID
static ssize_t camera_rear4_moduleid_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return camera_moduleid_show(buf, CAM_INFO_REAR4);
}
#endif

#ifdef CAMERA_REAR4_AFCAL
static ssize_t camera_rear4_afcal_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return camera_afcal_show(buf, CAM_INFO_REAR4);
}

static ssize_t camera_rear4_paf_cal_check_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return camera_paf_cal_check_show(buf, CAM_INFO_REAR4, FNUMBER_1ST);
}
#endif

#endif



static ssize_t camera_rear_sensor_standby(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	switch (buf[0]) {
	case '0':
		break;
	case '1':
		break;
	default:
		pr_debug("%s: %c\n", __func__, buf[0]);
		break;
	}

	return count;
}

static ssize_t camera_rear_sensor_standby_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "Rear sensor standby \n");
}

static ssize_t camera_rear_camfw_write(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	ssize_t ret = -EINVAL;

	if ((size == 1 || size == 2) && (buf[0] == 'F' || buf[0] == 'f')) {
		is_sec_set_force_caldata_dump(true);
		ret = size;
	} else {
		is_sec_set_force_caldata_dump(false);
	}
	return ret;
}

static ssize_t camera_rear_calcheck_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct is_rom_info *finfo;
	struct is_cam_info *cam_info;
	char rear_sensor[10] = {0, };
	char front_sensor[10] = {0, };
	int position;
	int rom_type;
	int rom_id;
	int rom_cal_index;

	is_get_cam_info_from_index(&cam_info, CAM_INFO_REAR);

	position = cam_info->internal_id;
	is_vendor_get_rom_info_from_position(position, &rom_type, &rom_id, &rom_cal_index);

	if (rom_type == ROM_TYPE_NONE) {
		strcpy(rear_sensor, "Null");
	} else if (rom_id == ROM_ID_NOTHING) {
		err("%s: invalid ROM ID [%d][%d]", __func__, position, rom_id);
		strcpy(rear_sensor, "Null");
	} else {
		read_from_firmware_version(rom_id);

		is_sec_get_sysfs_finfo(&finfo, rom_id);

		if (is_sec_check_rom_ver(sysfs_core, position)
			&& !test_bit(IS_ROM_STATE_OTHER_VENDOR, &finfo->rom_state)
			&& !test_bit(IS_CRC_ERROR_ALL_SECTION, &finfo->crc_error))
			strcpy(rear_sensor, "Normal");
		else
			strcpy(rear_sensor, "Abnormal");
	}

	/* front check */
	is_get_cam_info_from_index(&cam_info, CAM_INFO_FRONT);

	position = cam_info->internal_id;
	is_vendor_get_rom_info_from_position(position, &rom_type, &rom_id, &rom_cal_index);

	if (rom_type == ROM_TYPE_NONE) {
		strcpy(front_sensor, "Null");
	} else if (rom_id == ROM_ID_NOTHING) {
		err("%s: invalid ROM ID [%d][%d]", __func__, position, rom_id);
		strcpy(front_sensor, "Null");
	} else {
		read_from_firmware_version(rom_id);

		is_sec_get_sysfs_finfo(&finfo, rom_id);

		if (is_sec_check_rom_ver(sysfs_core, position)
			&& !test_bit(IS_ROM_STATE_OTHER_VENDOR, &finfo->rom_state)
			&& !test_bit(IS_CRC_ERROR_ALL_SECTION, &finfo->crc_error))
			strcpy(front_sensor, "Normal");
		else
			strcpy(front_sensor, "Abnormal");
	}

	return sprintf(buf, "%s %s\n", rear_sensor, front_sensor);
}

static ssize_t camera_hw_init_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct is_vender *vender;
	int i;
	struct is_lib_support *lib = is_get_lib_support();

	vender = &sysfs_core->vender;

	is_load_ctrl_lock();
	if (!check_module_init
		|| lib->binary_code_load_flg != BINARY_LOAD_ALL_DONE) {
		is_vender_hw_init(vender);
		check_module_init = true;
		for (i = 0; i < SENSOR_POSITION_MAX; i++) {
			SsrmCameraInfo[i].ID = -1;
		}
	}
	is_load_ctrl_unlock();

	return sprintf(buf, "%s\n", "HW init done.");
}

#if defined (CONFIG_OIS_USE)
void ois_factory_resource_clean(void)
{
	struct is_vender *vender;

	vender = &sysfs_core->vender;

	info("%s ois power count = %d, shaking power count = %d\n", __func__, ois_power_count, shaking_power_count);

	if (check_ois_power) {
		is_ois_gpio_off(sysfs_core);
		check_ois_power = false;
		info("%s ois_power off before entering suspend.\n", __func__);
	} else if (check_shaking_noise) {
		is_vendor_shaking_gpio_off(vender);
		info("%s shaking_power off before entering suspend.\n", __func__);
	}

	return;
}

void ois_power_control(int onoff)
{
	bool camera_running;
	bool camera_running2;
#ifdef CAMERA_3RD_OIS
	bool camera_running3;
#endif
	struct is_vender *vender;

	vender = &sysfs_core->vender;

	is_vender_check_hw_init_running();

	camera_running = is_vendor_check_camera_running(SENSOR_POSITION_REAR);
	camera_running2 = is_vendor_check_camera_running(SENSOR_POSITION_REAR2);
#ifdef CAMERA_3RD_OIS
	camera_running3 = is_vendor_check_camera_running(SENSOR_POSITION_REAR4);
#endif

	switch (onoff) {
	case 0:
		if (!camera_running && !camera_running2
#ifdef CAMERA_3RD_OIS
			&& !camera_running3
#endif
			&& check_ois_power) {
			check_ois_power = false;
			is_ois_gpio_off(sysfs_core);
			pr_info("%s ois_power off.\n", __func__);
		} else {
			pr_info("%s Do not set OIS power off.\n", __func__);
		}
		break;
	case 1:
		if (!camera_running && !camera_running2
#ifdef CAMERA_3RD_OIS
			&& !camera_running3
#endif
			&& !check_ois_power) {
			check_ois_power = true;

			if (check_shaking_noise) {
				is_vendor_shaking_gpio_off(vender);
				pr_info("%s shaking off before start ois power on.\n", __func__);
			}

			is_ois_gpio_on(sysfs_core);
			msleep(150);
			pr_info("%s ois_power on.\n", __func__);
		} else {
			pr_info("%s Do not set OIS power on .\n", __func__);
		}
		ois_power_count++;
		break;
	default:
		pr_debug("%s\n", __func__);
		break;
	}

	return;
}

static bool read_ois_version(void)
{
	bool ret = true;
	struct is_vender_specific *specific = sysfs_core->vender.private_data;

	pr_info("%s\n", __func__);

	if (!specific->ois_ver_read || force_caldata_dump) {
#ifndef CONFIG_CAMERA_USE_INTERNAL_MCU
		ois_power_control(1);
#endif
#if IS_ENABLED(CONFIG_CAMERA_USE_AOIS)
		cam_ois_set_aois_fac_mode_on();
#endif
		ret = is_ois_check_fw(sysfs_core);
#if IS_ENABLED(CONFIG_CAMERA_USE_AOIS)
		cam_ois_set_aois_fac_mode_off();
#endif
#ifndef CONFIG_CAMERA_USE_INTERNAL_MCU
		ois_power_control(0);
#endif
	}

	return ret;
}

static ssize_t camera_ois_power_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)

{
	pr_info("%s: %c\n", __func__, buf[0]);

	switch (buf[0]) {
	case '0':
		ois_power_control(0);
		break;
	case '1':
		ois_power_control(1);
		break;
	default:
		pr_debug("%s: %c\n", __func__, buf[0]);
		break;
	}

	return count;
}

static ssize_t camera_ois_autotest_store(struct device *dev,
				    struct device_attribute *attr, const char *buf, size_t count)

{
	int value = 0;

	if (kstrtoint(buf, 10, &value)) {
		err("convert fail");
	}

	ois_threshold = value;

	return count;
}

static ssize_t camera_ois_set_mode_store(struct device *dev,
				    struct device_attribute *attr, const char *buf, size_t count)
{
	int value = 0;

	if (kstrtoint(buf, 10, &value)) {
		err("convert fail");
	}

	if (check_ois_power) {
#if IS_ENABLED(CONFIG_CAMERA_USE_AOIS)
		cam_ois_set_aois_fac_mode_on();
#endif
		is_ois_set_mode(sysfs_core, value);
#if IS_ENABLED(CONFIG_CAMERA_USE_AOIS)
		cam_ois_set_aois_fac_mode_off();
#endif
	} else
		err("OIS power is not enabled.");

	return count;
}

static ssize_t camera_ois_autotest_show(struct device *dev,
				    struct device_attribute *attr, char *buf)

{
	bool x_result = false, y_result = false;
	bool x_result_2nd = false, y_result_2nd = false;
	bool x_result_3rd = false, y_result_3rd = false;
	int sin_x = 0, sin_y = 0;
	int sin_x_2nd = 0, sin_y_2nd = 0;
	int sin_x_3rd = 0, sin_y_3rd = 0;
	char wide_buffer[90] = {0,};
#ifdef CAMERA_2ND_OIS
	char tele_buffer[30] = {0,};
#endif
#ifdef CAMERA_3RD_OIS
	char tele2_buffer[30] = {0,};
#endif

	if (check_ois_power) {
		is_ois_auto_test(sysfs_core, ois_threshold,
			&x_result, &y_result, &sin_x, &sin_y,
			&x_result_2nd, &y_result_2nd, &sin_x_2nd, &sin_y_2nd,
			&x_result_3rd, &y_result_3rd, &sin_x_3rd, &sin_y_3rd);

		if (x_result && y_result) {
			sprintf(wide_buffer, "%s,%d,%s,%d,", "pass", 0, "pass", 0);
		} else if (x_result) {
			sprintf(wide_buffer, "%s,%d,%s,%d,", "pass", 0, "fail", sin_y);
		} else if (y_result) {
			sprintf(wide_buffer, "%s,%d,%s,%d,", "fail", sin_x, "pass", 0);
		} else {
			sprintf(wide_buffer, "%s,%d,%s,%d,", "fail", sin_x, "fail", sin_y);
		}
#ifdef CAMERA_2ND_OIS
		if (x_result_2nd && y_result_2nd) {
			sprintf(tele_buffer, "%s,%d,%s,%d", "pass", 0, "pass", 0);
		} else if (x_result_2nd) {
			sprintf(tele_buffer, "%s,%d,%s,%d", "pass", 0, "fail", sin_y_2nd);
		} else if (y_result_2nd) {
			sprintf(tele_buffer, "%s,%d,%s,%d", "fail", sin_x_2nd, "pass", 0);
		} else {
			sprintf(tele_buffer, "%s,%d,%s,%d", "fail", sin_x_2nd, "fail", sin_y_2nd);
		}
		strncat(wide_buffer, tele_buffer, strlen(tele_buffer));
#endif
#ifdef CAMERA_3RD_OIS
		if (x_result_3rd && y_result_3rd) {
			sprintf(tele2_buffer, ",%s,%d,%s,%d", "pass", 0, "pass", 0);
		} else if (x_result_3rd) {
			sprintf(tele2_buffer, ",%s,%d,%s,%d", "pass", 0, "fail", sin_y_3rd);
		} else if (y_result_3rd) {
			sprintf(tele2_buffer, ",%s,%d,%s,%d", "fail", sin_x_3rd, "pass", 0);
		} else {
			sprintf(tele2_buffer, ",%s,%d,%s,%d", "fail", sin_x_3rd, "fail", sin_y_3rd);
		}

		strncat(wide_buffer, tele2_buffer, strlen(tele2_buffer));
#endif
		info("%s result =  %s\n", __func__, wide_buffer);

		return sprintf(buf, "%s\n", wide_buffer);
	} else {
		err("OIS power is not enabled.");
		return sprintf(buf, "%s,%d,%s,%d\n", "fail", sin_x, "fail", sin_y,
			"fail", sin_x_2nd, "fail", sin_y_2nd, "fail", sin_x_3rd, "fail", sin_y_3rd);
	}
}

#ifdef CAMERA_2ND_OIS
static ssize_t camera_ois_rear3_gain_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct is_ois_info *ois_pinfo = NULL;
	u32 xgain = 0;
	u32 ygain = 0;

	is_ois_get_phone_version(&ois_pinfo);
	xgain = (ois_pinfo->tele_romdata.xgg[3] << 24) | (ois_pinfo->tele_romdata.xgg[2] << 16) | (ois_pinfo->tele_romdata.xgg[1] << 8) | (ois_pinfo->tele_romdata.xgg[0]);
	ygain = (ois_pinfo->tele_romdata.ygg[3] << 24) | (ois_pinfo->tele_romdata.ygg[2] << 16) | (ois_pinfo->tele_romdata.ygg[1] << 8) | (ois_pinfo->tele_romdata.ygg[0]);

	info("%s xgain/ygain = 0x%08x/0x%08x", __func__, xgain, ygain);

	if ((xgain == 0xFFFFFFFF) && (ygain == 0xFFFFFFFF)) {
		return sprintf(buf, "%d\n", 2);
	} else if (ois_pinfo->tele_romdata.cal_mark[0] != 0xBB) {
		return sprintf(buf, "%d\n", 1);
	} else {
		return sprintf(buf, "%d,0x%08x,0x%08x\n", 0, xgain, ygain);
	}

}

static ssize_t camera_ois_rear3_supperssion_ratio_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct is_ois_info *ois_pinfo = NULL;
	u16 xratio = 0;
	u16 yratio = 0;

	is_ois_get_phone_version(&ois_pinfo);
	xratio = (ois_pinfo->tele_romdata.supperssion_xratio[1] << 8) | (ois_pinfo->tele_romdata.supperssion_xratio[0]);
	yratio = (ois_pinfo->tele_romdata.supperssion_yratio[1] << 8) | (ois_pinfo->tele_romdata.supperssion_yratio[0]);

	info("%s xratio/yratio = %d.%d/%d.%d", __func__, xratio / 100, xratio % 100, yratio / 100, yratio % 100);

	if ((xratio == 0xFFFF) && (yratio == 0xFFFF)) {
		return sprintf(buf, "%d\n", 2);
	} else if (ois_pinfo->tele_romdata.cal_mark[0] != 0xBB) {
		return sprintf(buf, "%d\n", 1);
	} else {
		return sprintf(buf, "%d,%d.%d,%d.%d\n", 0, xratio / 100, xratio % 100, yratio / 100, yratio % 100);
	}
}
#endif

#ifdef CAMERA_3RD_OIS
static ssize_t camera_ois_rear4_gain_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct is_ois_info *ois_pinfo = NULL;
	u32 xgain = 0;
	u32 ygain = 0;

	is_ois_get_phone_version(&ois_pinfo);
	xgain = (ois_pinfo->tele2_romdata.xgg[3] << 24) | (ois_pinfo->tele2_romdata.xgg[2] << 16) | (ois_pinfo->tele2_romdata.xgg[1] << 8) | (ois_pinfo->tele2_romdata.xgg[0]);
	ygain = (ois_pinfo->tele2_romdata.ygg[3] << 24) | (ois_pinfo->tele2_romdata.ygg[2] << 16) | (ois_pinfo->tele2_romdata.ygg[1] << 8) | (ois_pinfo->tele2_romdata.ygg[0]);

	info("%s xgain/ygain = 0x%08x/0x%08x", __func__, xgain, ygain);

	if ((xgain == 0xFFFFFFFF) && (ygain == 0xFFFFFFFF)) {
		return sprintf(buf, "%d\n", 2);
	} else if (ois_pinfo->tele2_romdata.cal_mark[0] != 0xBB) {
		return sprintf(buf, "%d\n", 1);
	} else {
		return sprintf(buf, "%d,0x%08x,0x%08x\n", 0, xgain, ygain);
	}

}

static ssize_t camera_ois_rear4_supperssion_ratio_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct is_ois_info *ois_pinfo = NULL;
	u16 xratio = 0;
	u16 yratio = 0;

	is_ois_get_phone_version(&ois_pinfo);
	xratio = (ois_pinfo->tele2_romdata.supperssion_xratio[1] << 8) | (ois_pinfo->tele2_romdata.supperssion_xratio[0]);
	yratio = (ois_pinfo->tele2_romdata.supperssion_yratio[1] << 8) | (ois_pinfo->tele2_romdata.supperssion_yratio[0]);

	info("%s xratio/yratio = %d.%d/%d.%d", __func__, xratio / 100, xratio % 100, yratio / 100, yratio % 100);

	if ((xratio == 0xFFFF) && (yratio == 0xFFFF)) {
		return sprintf(buf, "%d\n", 2);
	} else if (ois_pinfo->tele2_romdata.cal_mark[0] != 0xBB) {
		return sprintf(buf, "%d\n", 1);
	} else {
		return sprintf(buf, "%d,%d.%d,%d.%d\n", 0, xratio / 100, xratio % 100, yratio / 100, yratio % 100);
	}
}
#endif

static ssize_t camera_ois_selftest_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	int result_total = 0;
	bool result_offset = 0, result_selftest = 0;
	int selftest_ret = 0, offsettest_ret = 0;
	long raw_data_x = 0, raw_data_y = 0, raw_data_z = 0;
	char x_buf[90] = {0,};
	char y_buf[30] = {0,};
	char z_buf[30] = {0,};

	if (check_ois_power) {
		is_ois_init_factory(sysfs_core);
		offsettest_ret = is_ois_offset_test(sysfs_core, &raw_data_x, &raw_data_y, &raw_data_z);
		msleep(50);
		selftest_ret = is_ois_self_test(sysfs_core);

		if (selftest_ret == 0x0) {
			result_selftest = true;
		} else {
			result_selftest = false;
		}

		if (abs(raw_data_x) > CAMERA_OIS_GYRO_OFFSET_SPEC || abs(raw_data_y) > CAMERA_OIS_GYRO_OFFSET_SPEC
			|| abs(raw_data_z) > CAMERA_OIS_GYRO_OFFSET_SPEC
			|| abs(raw_data_x - x_init_raw) > CAMERA_OIS_GYRO_OFFSET_SPEC
			|| abs(raw_data_y - y_init_raw) > CAMERA_OIS_GYRO_OFFSET_SPEC
			|| abs(raw_data_z - z_init_raw) > CAMERA_OIS_GYRO_OFFSET_SPEC
			|| offsettest_ret == false)  {
			result_offset = false;
		} else {
			result_offset = true;
		}

		if (result_offset && result_selftest) {
			result_total = 0;
		} else if (!result_offset && !result_selftest) {
			result_total = 3;
		} else if (!result_offset) {
			result_total = 1;
		} else if (!result_selftest) {
			result_total = 2;
		}

		if (raw_data_x < 0) {
			sprintf(x_buf, "%d,-%ld.%03ld", result_total, abs(raw_data_x / 1000), abs(raw_data_x % 1000));
		} else if (raw_data_x >= 0) {
			sprintf(x_buf, "%d,%ld.%03ld", result_total, abs(raw_data_x / 1000), abs(raw_data_x % 1000));
		}

		if (raw_data_y < 0) {
			sprintf(y_buf, ",-%ld.%03ld", abs(raw_data_y / 1000), abs(raw_data_y % 1000));
		} else if (raw_data_y >= 0) {
			sprintf(y_buf, ",%ld.%03ld", abs(raw_data_y / 1000), abs(raw_data_y % 1000));
		}

		strncat(x_buf, y_buf, strlen(y_buf));

		if (raw_data_z < 0) {
			sprintf(z_buf, ",-%ld.%03ld", abs(raw_data_z / 1000), abs(raw_data_z % 1000));
		} else if (raw_data_z >= 0) {
			sprintf(z_buf, ",%ld.%03ld", abs(raw_data_z / 1000), abs(raw_data_z % 1000));
		}

		strncat(x_buf, z_buf, strlen(z_buf));
		
		info("%s result_total =  %d, result_selftest = %d, result_offset = %d, raw_data_x = %ld, raw_data_y = %ld, raw_data_z = %ld\n",
			__func__, result_total, result_selftest, result_offset, raw_data_x, raw_data_y, raw_data_z);

		return sprintf(buf, "%s\n", x_buf);
	} else {
		err("OIS power is not enabled.");
		return sprintf(buf, "%d,%ld.%03ld,%ld.%03ld,%ld.%03ld\n", result_total, raw_data_x / 1000,
			 raw_data_x % 1000, raw_data_y / 1000, raw_data_y % 1000 , raw_data_z / 1000, raw_data_z % 1000);
	}
}

static ssize_t camera_ois_rawdata_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	uint8_t raw_data[MAX_GYRO_EFS_DATA_LENGTH + 1] = {0, };
	long raw_data_x = 0, raw_data_y = 0, raw_data_z = 0;
	long efs_size = 0;

	if (check_ois_power) {
		if (count > MAX_GYRO_EFS_DATA_LENGTH || count == 0) {
			err("%s count is abnormal, count = %d", __func__, count);
			return 0;
		}

		sscanf(buf, "%s", raw_data);

		efs_size = strlen(raw_data);

		is_ois_parsing_raw_data(sysfs_core, raw_data, efs_size, &raw_data_x, &raw_data_y, &raw_data_z);

		x_init_raw = raw_data_x;
		y_init_raw = raw_data_y;
		z_init_raw = raw_data_z;
		info("%s: raw x = %ld, raw y = %ld, raw z = %ld, efs size = %ld, efs data = %s\n",
				__func__, raw_data_x, raw_data_y, raw_data_z, efs_size, buf);
	} else {
		err("%s OIS power is not enabled.", __func__);
	}

	return count;
}

static ssize_t camera_ois_rawdata_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	long raw_data_x = 0, raw_data_y = 0, raw_data_z = 0;
	char x_buf[90] = {0,};
	char y_buf[30] = {0,};
	char z_buf[30] = {0,};

	if (check_ois_power) {
		raw_data_x = x_init_raw;
		raw_data_y = y_init_raw;
		raw_data_z = z_init_raw;

		if (raw_data_x < 0) {
			sprintf(x_buf, "-%ld.%03ld", abs(raw_data_x / 1000), abs(raw_data_x % 1000));
		} else if (raw_data_x >= 0) {
			sprintf(x_buf, "%ld.%03ld", abs(raw_data_x / 1000), abs(raw_data_x % 1000));
		}

		if (raw_data_y < 0) {
			sprintf(y_buf, ",-%ld.%03ld", abs(raw_data_y / 1000), abs(raw_data_y % 1000));
		} else if (raw_data_y >= 0) {
			sprintf(y_buf, ",%ld.%03ld", abs(raw_data_y / 1000), abs(raw_data_y % 1000));
		}

		strncat(x_buf, y_buf, strlen(y_buf));

		if (raw_data_z < 0) {
			sprintf(z_buf, ",-%ld.%03ld", abs(raw_data_z / 1000), abs(raw_data_z % 1000));
		} else if (raw_data_z >= 0) {
			sprintf(z_buf, ",%ld.%03ld", abs(raw_data_z / 1000), abs(raw_data_z % 1000));
		}

		strncat(x_buf, z_buf, strlen(z_buf));

		info("%s raw_data_x = %ld, raw_data_y = %ld, raw_data_z = %ld\n", __func__, raw_data_x, raw_data_y, raw_data_z);

		return sprintf(buf, "%s\n", x_buf);
	} else {
		err("OIS power is not enabled.");
		return sprintf(buf, "%ld.%03ld,%ld.%03ld,%ld.%03ld\n", raw_data_x / 1000, raw_data_x % 1000,
			raw_data_y / 1000, raw_data_y % 1000, raw_data_z / 1000, raw_data_z % 1000);
	}
}

static ssize_t camera_ois_calibrationtest_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	bool result = false;
	long raw_data_x = 0, raw_data_y = 0, raw_data_z = 0;
	char x_buf[90] = {0,};
	char y_buf[30] = {0,};
	char z_buf[30] = {0,};

	if (check_ois_power) {
#if defined(CONFIG_CAMERA_USE_MCU) || defined(CONFIG_CAMERA_USE_INTERNAL_MCU)
		is_ois_init_factory(sysfs_core);
		result = is_ois_gyrocal_test(sysfs_core, &raw_data_x, &raw_data_y, &raw_data_z);
#endif

		if (raw_data_x < 0) {
			sprintf(x_buf, "%d,-%ld.%03ld", result, abs(raw_data_x / 1000), abs(raw_data_x % 1000));
		} else if (raw_data_x >= 0) {
			sprintf(x_buf, "%d,%ld.%03ld", result, abs(raw_data_x / 1000), abs(raw_data_x % 1000));
		}

		if (raw_data_y < 0) {
			sprintf(y_buf, ",-%ld.%03ld", abs(raw_data_y / 1000), abs(raw_data_y % 1000));
		} else if (raw_data_y >= 0) {
			sprintf(y_buf, ",%ld.%03ld", abs(raw_data_y / 1000), abs(raw_data_y % 1000));
		}

		strncat(x_buf, y_buf, strlen(y_buf));

		if (raw_data_z < 0) {
			sprintf(z_buf, ",-%ld.%03ld", abs(raw_data_z / 1000), abs(raw_data_z % 1000));
		} else if (raw_data_z >= 0) {
			sprintf(z_buf, ",%ld.%03ld", abs(raw_data_z / 1000), abs(raw_data_z % 1000));
		}

		strncat(x_buf, z_buf, strlen(z_buf));

		info("%s raw_data_x = %ld, raw_data_y = %ld, raw_data_z = %ld\n", __func__, raw_data_x, raw_data_y, raw_data_z);

		return sprintf(buf, "%s\n", x_buf);
	} else {
		err("OIS power is not enabled.");
		return sprintf(buf, "%d,%ld.%03ld,%ld.%03ld,%ld.%03ld\n", result, raw_data_x / 1000,
			 raw_data_x % 1000, raw_data_y / 1000, raw_data_y % 1000, raw_data_z / 1000, raw_data_z % 1000);
	}
}

static ssize_t camera_ois_gyronoise_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	bool result = false;
	long raw_data_x = 0, raw_data_y = 0;
	bool camera_running;
#ifdef CAMERA_2ND_OIS
	bool camera_running2;
#endif
#ifdef CAMERA_3RD_OIS
	bool camera_running3;
#endif
	bool check_power = false;

	camera_running = is_vendor_check_camera_running(SENSOR_POSITION_REAR);
#ifdef CAMERA_2ND_OIS
	camera_running2 = is_vendor_check_camera_running(SENSOR_POSITION_REAR2);
#endif
#ifdef CAMERA_3RD_OIS
	camera_running3 = is_vendor_check_camera_running(SENSOR_POSITION_REAR4);
#endif

	if (!camera_running
#ifdef CAMERA_2ND_OIS
		&& !camera_running2
#endif
#ifdef CAMERA_3RD_OIS
		&& !camera_running3
#endif
	) {
		check_power = check_ois_power;
		if (check_power)
			is_ois_init_factory(sysfs_core);
	} else {
		check_power = true;
	}

	if (check_power) {
		result = is_ois_gyronoise_test(sysfs_core, &raw_data_x, &raw_data_y);

		info("%s raw_data_x = %ld, raw_data_y = %ld\n", __func__, raw_data_x, raw_data_y);
		if (raw_data_x < 0 && raw_data_y < 0) {
			return sprintf(buf, "%d,-%ld.%03ld,-%ld.%03ld\n", result, abs(raw_data_x / 1000),
				abs(raw_data_x % 1000), abs(raw_data_y / 1000), abs(raw_data_y % 1000));
		} else if (raw_data_x < 0) {
			return sprintf(buf, "%d,-%ld.%03ld,%ld.%03ld\n", result, abs(raw_data_x / 1000),
				abs(raw_data_x % 1000), raw_data_y / 1000, raw_data_y % 1000);
		} else if (raw_data_y < 0) {
			return sprintf(buf, "%d,%ld.%03ld,-%ld.%03ld\n", result, raw_data_x / 1000,
				raw_data_x % 1000, abs(raw_data_y / 1000), abs(raw_data_y % 1000));
		} else {
			return sprintf(buf, "%d,%ld.%03ld,%ld.%03ld\n", result, raw_data_x / 1000,
				raw_data_x % 1000, raw_data_y / 1000, raw_data_y % 1000);
		}
	} else {
		err("OIS power is not enabled.");
		return sprintf(buf, "%d,%ld.%03ld,%ld.%03ld\n", result, raw_data_x / 1000,
			 raw_data_x % 1000, raw_data_y / 1000, raw_data_y % 1000);
	}
}

static ssize_t camera_ois_hall_position_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	u16 targetPos[6] = {0, };
	u16 hallPos[6] = {0, };
	bool camera_running;
#ifdef CAMERA_2ND_OIS
	bool camera_running2;
#endif
#ifdef CAMERA_3RD_OIS
	bool camera_running3;
#endif

	camera_running = is_vendor_check_camera_running(SENSOR_POSITION_REAR);
#ifdef CAMERA_2ND_OIS
	camera_running2 = is_vendor_check_camera_running(SENSOR_POSITION_REAR2);
#endif
#ifdef CAMERA_3RD_OIS
	camera_running3 = is_vendor_check_camera_running(SENSOR_POSITION_REAR4);
#endif

	if (camera_running
#ifdef CAMERA_2ND_OIS
		|| camera_running2
#endif
#ifdef CAMERA_3RD_OIS
		|| camera_running3
#endif
		)
		is_ois_get_hall_pos(sysfs_core, targetPos, hallPos);
	else
		err("Camera power is not enabled.");

#ifdef CAMERA_3RD_OIS
	info("[%s] %u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u", __func__,
		targetPos[0], targetPos[1], targetPos[2], targetPos[3], targetPos[4], targetPos[5],
		hallPos[0], hallPos[1], hallPos[2], hallPos[3], hallPos[4], hallPos[5]);

	return sprintf(buf, "%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u",
		targetPos[0], targetPos[1], targetPos[2], targetPos[3], targetPos[4], targetPos[5],
		hallPos[0], hallPos[1], hallPos[2], hallPos[3], hallPos[4], hallPos[5]);
#elif defined(CAMERA_2ND_OIS)
	info("[%s] %u,%u,%u,%u,%u,%u,%u,%u", __func__,
		targetPos[0], targetPos[1], targetPos[2], targetPos[3],
		hallPos[0], hallPos[1], hallPos[2], hallPos[3]);

	return sprintf(buf, "%u,%u,%u,%u,%u,%u,%u,%u",
		targetPos[0], targetPos[1], targetPos[2], targetPos[3],
		hallPos[0], hallPos[1], hallPos[2], hallPos[3]);
#else
	info("[%s] %u,%u,%u,%u", __func__,
		targetPos[0], targetPos[1],
		hallPos[0], hallPos[1]);

	return sprintf(buf, "%u,%u,%u,%u,%u,%u,%u,%u",
		targetPos[0], targetPos[1], 2048, 2048,
		hallPos[0], hallPos[1], 2048, 2048);
#endif
}

static ssize_t camera_ois_version_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct is_ois_info *ois_minfo = NULL;
	struct is_ois_info *ois_pinfo = NULL;
	bool ret = false;

	is_vender_check_hw_init_running();
	ret = read_ois_version();
	is_ois_get_module_version(&ois_minfo);
	is_ois_get_phone_version(&ois_pinfo);

	info("%s ois fw info = %s/%s", __func__, ois_minfo->header_ver, ois_pinfo->header_ver);

	if (ois_minfo->checksum != 0x00 || !ret) {
		return sprintf(buf, "%s %s\n", "NG_FW2", "NULL");
	} else if (ois_minfo->caldata != 0x00) {
		return sprintf(buf, "%s %s\n", "NG_CD2", ois_pinfo->header_ver);
	} else {
		return sprintf(buf, "%s %s\n", ois_minfo->header_ver, ois_pinfo->header_ver);
	}
}

static ssize_t camera_ois_diff_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int x_diff = 0, y_diff = 0;

#if !defined (CONFIG_CAMERA_USE_MCU) && !defined (CONFIG_CAMERA_USE_INTERNAL_MCU)
	bool result = false;

	if (check_ois_power) {
		result = is_ois_diff_test(sysfs_core, &x_diff, &y_diff);

		return sprintf(buf, "%d,%d,%d\n", result == true ? 0 : 1, x_diff, y_diff);
	} else {
		err("OIS power is not enabled.");
		return sprintf(buf, "%d,%d,%d\n", 0, x_diff, y_diff);
	}
#else
	return sprintf(buf, "%d,%d,%d\n", 0, x_diff, y_diff);
#endif
}

static ssize_t camera_ois_rear_gain_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct is_ois_info *ois_pinfo = NULL;
	u32 xgain = 0;
	u32 ygain = 0;

	is_ois_get_phone_version(&ois_pinfo);
	xgain = (ois_pinfo->wide_romdata.xgg[3] << 24) | (ois_pinfo->wide_romdata.xgg[2] << 16) | (ois_pinfo->wide_romdata.xgg[1] << 8) | (ois_pinfo->wide_romdata.xgg[0]);
	ygain = (ois_pinfo->wide_romdata.ygg[3] << 24) | (ois_pinfo->wide_romdata.ygg[2] << 16) | (ois_pinfo->wide_romdata.ygg[1] << 8) | (ois_pinfo->wide_romdata.ygg[0]);

	info("%s xgain/ygain = 0x%08x/0x%08x", __func__, xgain, ygain);

	if ((xgain == 0xFFFFFFFF) && (ygain == 0xFFFFFFFF)) {
		return sprintf(buf, "%d\n", 2);
	} else if (ois_pinfo->wide_romdata.cal_mark[0]!= 0xBB) {
		return sprintf(buf, "%d\n", 1);
	} else {
		return sprintf(buf, "%d,0x%08x,0x%08x\n", 0, xgain, ygain);
	}

}

static ssize_t camera_ois_rear_supperssion_ratio_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct is_ois_info *ois_pinfo = NULL;
	u16 xratio = 0;
	u16 yratio = 0;

	is_ois_get_phone_version(&ois_pinfo);
	xratio = (ois_pinfo->wide_romdata.supperssion_xratio[1] << 8) | (ois_pinfo->wide_romdata.supperssion_xratio[0]);
	yratio = (ois_pinfo->wide_romdata.supperssion_yratio[1] << 8) | (ois_pinfo->wide_romdata.supperssion_yratio[0]);

	info("%s xratio/yratio = %d.%d/%d.%d", __func__, xratio / 100, xratio % 100, yratio / 100, yratio % 100);

	if ((xratio == 0xFFFF) && (yratio == 0xFFFF)) {
		return sprintf(buf, "%d\n", 2);
	} else if (ois_pinfo->wide_romdata.cal_mark[0] != 0xBB) {
		return sprintf(buf, "%d\n", 1);
	} else {
		return sprintf(buf, "%d,%d.%d,%d.%d\n", 0, xratio / 100, xratio % 100, yratio / 100, yratio % 100);
	}
}

static ssize_t camera_ois_exif_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct is_ois_info *ois_minfo = NULL;
	struct is_ois_info *ois_pinfo = NULL;
	struct is_ois_info *ois_uinfo = NULL;
	struct is_ois_exif *ois_exif = NULL;

	is_ois_get_module_version(&ois_minfo);
	is_ois_get_phone_version(&ois_pinfo);
	is_ois_get_user_version(&ois_uinfo);
	is_ois_get_exif_data(&ois_exif);

	return sprintf(buf, "%s %s %s %d %d", ois_minfo->header_ver, ois_pinfo->header_ver,
		ois_uinfo->header_ver, ois_exif->error_data, ois_exif->status_data);
}

static ssize_t camera_ois_reset_check(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct is_ois_info *ois_pinfo = NULL;

	is_ois_get_phone_version(&ois_pinfo);

	return sprintf(buf, "%d", ois_pinfo->reset_check);
}

static ssize_t camera_ois_shaking_noise_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)

{
	bool camera_running;
	bool camera_running2;
	struct is_vender *vender;

	vender = &sysfs_core->vender;

#if defined(CONFIG_SEC_FACTORY)
	return 0;
#endif
	pr_info("%s: %c\n", __func__, buf[0]);

	is_vender_check_hw_init_running();

	camera_running = is_vendor_check_camera_running(SENSOR_POSITION_REAR);
	camera_running2 = is_vendor_check_camera_running(SENSOR_POSITION_REAR2);

	switch (buf[0]) {
	case '0':
		if (!camera_running && !camera_running2 && check_shaking_noise && !check_ois_power) {
			is_vendor_shaking_gpio_off(vender);
			pr_info("%s shaking_power off.\n", __func__);
		} else {
			pr_info("%s Do not set shaking power off.\n", __func__);
		}

		check_shaking_noise = false;
		break;
	case '1':
		if (!camera_running && !camera_running2 && !check_shaking_noise && !check_ois_power) {
			is_vendor_shaking_gpio_on(vender);
			pr_info("%s shaking_power on.\n", __func__);
		} else {
			pr_info("%s Do not set shaking power on.\n", __func__);
		}

		shaking_power_count++;
		break;
	default:
		pr_debug("%s: %c\n", __func__, buf[0]);
		break;
	}
	return count;
}

static ssize_t camera_ois_check_cross_talk_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	u16 hall_data[11] = {0, };
	int result = 0;

	if (check_ois_power) {
		is_ois_check_cross_talk(sysfs_core, hall_data);
		result = 1;
	} else {
		err("OIS power is not enabled.");
		result = 0;
	}

	return sprintf(buf, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n", result, hall_data[0], hall_data[1],
		hall_data[2], hall_data[3], hall_data[4], hall_data[5], hall_data[6], hall_data[7], hall_data[8], hall_data[9]);
}

static ssize_t camera_ois_check_hall_cal_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	u16 hall_cal_data[9] = {0, };
	int result = 0;

	if (check_ois_power) {
		is_ois_check_hall_cal(sysfs_core, hall_cal_data);
		result = 1;

		info("[%s] %d,%d,%d,%d,%d,%d,%d,%d\n", __func__, hall_cal_data[0], hall_cal_data[1],
			hall_cal_data[2], hall_cal_data[3], hall_cal_data[4], hall_cal_data[5], hall_cal_data[6], hall_cal_data[7]);
	} else {
		err("OIS power is not enabled.");
		result = 0;
	}

	return sprintf(buf, "%d,%d,%d,%d,%d,%d,%d,%d,%d\n", result, hall_cal_data[0], hall_cal_data[1],
		hall_cal_data[2], hall_cal_data[3], hall_cal_data[4], hall_cal_data[5], hall_cal_data[6], hall_cal_data[7]);
}

static ssize_t camera_ois_check_valid_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
#if defined (CONFIG_CAMERA_USE_INTERNAL_MCU)
	u8 value = 0;
	u8 wide_val = 0;
	u8 tele_val = 0;
	u8 tele2_val = 0;
#else
	u8 value = 0;
#endif

	if (check_ois_power) {
#if defined (CONFIG_CAMERA_USE_INTERNAL_MCU)
		is_ois_check_valid(sysfs_core, &value);

		wide_val = value & 0x06;
		tele_val = value & 0x18;
		tele2_val = value & 0x60;

		info("[%s] 0x%02x,0x%02x,0x%02x\n", __func__, wide_val, tele_val, tele2_val);
		return sprintf(buf, "0x%02x,0x%02x,0x%02x\n", wide_val, tele_val, tele2_val);
#else
		is_ois_check_valid(sysfs_core, &value);

		info("[%s] 0x%02x,0x%02x,0x%02x\n", __func__, value, 0, 0);
		return sprintf(buf, "0x%02x,0x%02x,0x%02x\n", value, 0, 0);
#endif
	} else {
		err("OIS power is not enabled.");
		return sprintf(buf, "0x%02x,0x%02x,0x%02x\n", 0x06, 0x18, 0x60);
	}
}

static ssize_t camera_ois_center_shift_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int ret = 0;
	int16_t shiftValue[7] = {0, };
	struct is_core *core = is_get_is_core();
	bool camera_running;
#if defined(CAMERA_2ND_OIS)
	bool camera_running2;
#endif
#if defined(CAMERA_3RD_OIS)
	bool camera_running3;
#endif
	
	camera_running = is_vendor_check_camera_running(SENSOR_POSITION_REAR);
#if defined(CAMERA_2ND_OIS)
	camera_running2 = is_vendor_check_camera_running(SENSOR_POSITION_REAR2);
#endif
#if defined(CAMERA_3RD_OIS)
	camera_running3 = is_vendor_check_camera_running(SENSOR_POSITION_REAR4);
#endif

	ret = sscanf(buf, "%hd,%hd,%hd,%hd,%hd,%hd",
		&shiftValue[0], &shiftValue[1], &shiftValue[2], &shiftValue[3], &shiftValue[4], &shiftValue[5]);

	if (camera_running
#ifdef CAMERA_2ND_OIS
		|| camera_running2
#endif
#ifdef CAMERA_3RD_OIS
		|| camera_running3
#endif
		|| check_ois_power)
		is_ois_set_center_shift(core, shiftValue);
	else
		err("Camera power is not enabled.");

	return count;
}

static ssize_t camera_ois_read_ext_clock_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	u32 clock = 0;

	if (check_ois_power) {
		is_ois_read_ext_clock(sysfs_core, &clock);
	} else {
		err("OIS power is not enabled.");
	}

	return sprintf(buf, "%ld\n", clock);
}

static ssize_t camera_ois_rear3_read_cross_talk_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct is_ois_info *ois_pinfo = NULL;
	int xvalue = 0;
	int yvalue = 0;
	int xvalue_pre = 0;
	int yvalue_pre = 0;
	int xvalue_post = 0;
	int yvalue_post = 0;

	is_ois_get_phone_version(&ois_pinfo);
	xvalue = (ois_pinfo->tele_romdata.xcoef[1] << 8) | (ois_pinfo->tele_romdata.xcoef[0]);
	yvalue = (ois_pinfo->tele_romdata.ycoef[1] << 8) | (ois_pinfo->tele_romdata.ycoef[0]);

	info("%s value =  0x%02x, 0x%02x, 0x%02x, 0x%02x", __func__, ois_pinfo->tele_romdata.xcoef[0], ois_pinfo->tele_romdata.xcoef[1],
		ois_pinfo->tele_romdata.ycoef[0], ois_pinfo->tele_romdata.ycoef[1]);

	if ((ois_pinfo->tele_romdata.xcoef[0] == 0xFF && ois_pinfo->tele_romdata.xcoef[1] == 0xFF) &&
		(ois_pinfo->tele_romdata.ycoef[0] == 0xFF && ois_pinfo->tele_romdata.ycoef[1] == 0xFF)) {
		return sprintf(buf, "NONE\n");
	} else if (ois_pinfo->tele_romdata.cal_mark[0] != 0xBB) {
		return sprintf(buf, "NONE\n");
	} else {
		xvalue_pre = xvalue / 100;
		yvalue_pre = yvalue / 100;
		xvalue_post = abs(xvalue) - abs(xvalue_pre) * 100;
		yvalue_post = abs(yvalue) - abs(yvalue_pre) * 100;
		return sprintf(buf, "%d.%d%d,%d.%d%d\n", xvalue_pre, xvalue_post / 10, xvalue_post % 10,
			yvalue_pre, yvalue_post / 10, yvalue_post % 10);
	}
}

#endif

#ifdef FORCE_CAL_LOAD
static ssize_t camera_rear_force_cal_load_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	is_sec_set_force_caldata_dump(true);
	return sprintf(buf, "FORCE CALDATA LOAD\n");
}
#endif

static ssize_t camera_rear_afcal_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return camera_afcal_show(buf, CAM_INFO_REAR);
}

/* PAF MID offset read */
static ssize_t camera_rear_paf_offset_mid_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return camera_paf_offset_show(buf, true, FNUMBER_1ST);
}

/* PAF PAN offset read */
static ssize_t camera_rear_paf_offset_far_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return camera_paf_offset_show(buf, false, FNUMBER_1ST);
}

static ssize_t camera_rear_sensorid_exif_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return camera_sensorid_exif_show(buf, CAM_INFO_REAR);
}

static ssize_t camera_rear_mtf_exif_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return camera_mtf_exif_show(buf, CAM_INFO_REAR, FNUMBER_1ST);
}

static ssize_t camera_rear_paf_cal_check_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return camera_paf_cal_check_show(buf, CAM_INFO_REAR, FNUMBER_1ST);
}




static ssize_t camera_rear_moduleid_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return camera_moduleid_show(buf, CAM_INFO_REAR);
}

#if defined(CAMERA_EEPROM_SUPPORT_FRONT)
#ifndef CAMERA_FRONT_FIXED_FOCUS
static ssize_t camera_front_afcal_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return camera_afcal_show(buf, CAM_INFO_FRONT);
}

#ifdef CAMERA_FRONT_PAFCAL
static ssize_t camera_front_paf_cal_check_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return camera_paf_cal_check_show(buf, CAM_INFO_FRONT, FNUMBER_1ST);
}
#endif
#endif
static ssize_t camera_front_moduleid_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return camera_moduleid_show(buf, CAM_INFO_FRONT);
}

static ssize_t camera_front_mtf_exif_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return camera_mtf_exif_show(buf, CAM_INFO_FRONT, FNUMBER_1ST);
}

static ssize_t camera_front_sensorid_exif_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return camera_sensorid_exif_show(buf, CAM_INFO_FRONT);
}
#endif

int camera_fw_show_sub(char *path, char *buf, int startPos, int readsize)
{
#ifdef USE_KERNEL_VFS_READ_WRITE
	struct file *fp = NULL;
	long fsize, nread;
	int ret = 0;

	fp = filp_open(path, O_RDONLY, 0660);

	fsize = fp->f_path.dentry->d_inode->i_size;

	if (IS_ERR(fp)) {
		err("Camera: Failed open phone firmware");
		ret = -EIO;
		fp = NULL;
		goto exit;
	}

	if (startPos < 0)
		fp->f_pos = fsize + startPos;
	else
		fp->f_pos = startPos;

	fsize = readsize;
	nread = kernel_read(fp, buf, fsize, &fp->f_pos);
	if (nread != fsize) {
		err("failed to read firmware file, %ld Bytes", nread);
		ret = -EIO;
	}

exit:
	if (fp) {
		filp_close(fp, current->files);
		fp = NULL;
	}

	return ret;
#endif
	return 0;
}

char *rta_fw_list[] = {
#ifdef RTA_FW_LL
	RTA_FW_LL,
#endif
#ifdef RTA_FW_LX
	RTA_FW_LX,
#endif
#ifdef RTA_FW_LY
	RTA_FW_LY,
#endif
#ifdef RTA_FW_LS
	RTA_FW_LS
#endif
};

char *companion_fw_list[] = {
#ifdef COMPANION_FW_LL
	COMPANION_FW_LL,
#endif
#ifdef COMPANION_FW_LX
	COMPANION_FW_LX,
#endif
#ifdef COMPANION_FW_LY
	COMPANION_FW_LY,
#endif
#ifdef COMPANION_FW_LS
	COMPANION_FW_LS
#endif
};

static ssize_t camera_rear_camfw_all_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	#define VER_MAX_SIZE 100
	int ret;
	mm_segment_t old_fs;
	char fw_ver[VER_MAX_SIZE] = {0, };
	char output[1024] = {0, };
	char path[128] = {0, };
	int cnt = 0, loop = 0, i;

	old_fs = get_fs();
	set_fs(KERNEL_DS);

	sprintf(output, "[RCAM]");
	cnt = 0;

	loop = sizeof(rta_fw_list) / sizeof(char *);
	for (i = 0; i < loop; i++) {
		sprintf(path, IS_FW_PATH"%s", rta_fw_list[i]);
		ret = camera_fw_show_sub(path, fw_ver,
						(-IS_HEADER_VER_OFFSET), IS_HEADER_VER_SIZE);
		if (ret == 0) {
			if (cnt++ > 0)
				strcat(output, ",");
			strcat(output, fw_ver);
			memset(fw_ver, 0, sizeof(fw_ver));
		}
	}

	strcat(output, ";[COMPANION]");
	cnt = 0;

	loop = sizeof(companion_fw_list) / sizeof(char *);
	for (i = 0; i < loop; i++) {
		sprintf(path, IS_FW_PATH"%s", companion_fw_list[i]);
		ret = camera_fw_show_sub(path, fw_ver,
						(-16), IS_HEADER_VER_SIZE);
		if (ret == 0) {
			if (cnt++ > 0)
				strcat(output, ",");
			strcat(output, fw_ver);
			memset(fw_ver, 0, sizeof(fw_ver));
		}
	}

	set_fs(old_fs);

#ifdef CONFIG_OIS_USE
	strcat(output, ";[OIS]");
	ret = is_ois_read_fw_ver(sysfs_core, IS_FW_PATH FIMC_OIS_FW_NAME_SEC, fw_ver);
	if (ret == 0) {
		strcat(output, fw_ver);
		memset(fw_ver, 0, sizeof(fw_ver));
	}

	ret = is_ois_read_fw_ver(sysfs_core, IS_FW_PATH FIMC_OIS_FW_NAME_DOM, fw_ver);
	if (ret == 0) {
		strcat(output, ",");
		strcat(output, fw_ver);
		memset(fw_ver, 0, sizeof(fw_ver));
	}
#endif

	return sprintf(buf, "%s\n", output);
}

#ifdef USE_CAMERA_HW_BIG_DATA
static ssize_t rear_camera_hw_param_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct cam_hw_param *ec_param = NULL;
	struct is_rom_info *finfo;
	struct is_cam_info *cam_info;
	int position;

	is_get_cam_info_from_index(&cam_info, CAM_INFO_REAR);
	position = cam_info->internal_id;

	is_sec_get_sysfs_finfo(&finfo, is_vendor_get_rom_id_from_position(position));
	is_sec_get_hw_param(&ec_param, position);

	if (is_sec_is_valid_moduleid(finfo->rom_module_id)) {
		return sprintf(buf, "\"CAMIR_ID\":\"%c%c%c%c%cXX%02X%02X%02X\",\"I2CR_AF\":\"%d\","
			"\"I2CR_COM\":\"%d\",\"I2CR_OIS\":\"%d\",\"I2CR_SEN\":\"%d\",\"MIPIR_COM\":\"%d\",\"MIPIR_SEN\":\"%d\"\n",
			finfo->rom_module_id[0], finfo->rom_module_id[1], finfo->rom_module_id[2], finfo->rom_module_id[3],
			finfo->rom_module_id[4], finfo->rom_module_id[7], finfo->rom_module_id[8], finfo->rom_module_id[9],
			ec_param->i2c_af_err_cnt, ec_param->i2c_comp_err_cnt, ec_param->i2c_ois_err_cnt,
			ec_param->i2c_sensor_err_cnt, ec_param->mipi_comp_err_cnt, ec_param->mipi_sensor_err_cnt);
	} else {
		return sprintf(buf, "\"CAMIR_ID\":\"MIR_ERR\",\"I2CR_AF\":\"%d\","
			"\"I2CR_COM\":\"%d\",\"I2CR_OIS\":\"%d\",\"I2CR_SEN\":\"%d\",\"MIPIR_COM\":\"%d\",\"MIPIR_SEN\":\"%d\"\n",
			ec_param->i2c_af_err_cnt, ec_param->i2c_comp_err_cnt, ec_param->i2c_ois_err_cnt,
			ec_param->i2c_sensor_err_cnt, ec_param->mipi_comp_err_cnt, ec_param->mipi_sensor_err_cnt);
	}
}

static ssize_t rear_camera_hw_param_store(struct device *dev,
				    struct device_attribute *attr, const char *buf, size_t count)
{
	struct cam_hw_param *ec_param = NULL;
	struct is_cam_info *cam_info;
	int position;

	is_get_cam_info_from_index(&cam_info, CAM_INFO_REAR);
	position = cam_info->internal_id;

	if (!strncmp(buf, "c", 1)) {
		is_sec_get_hw_param(&ec_param, position);

		if (ec_param)
			is_sec_init_err_cnt(ec_param);
	}

	return count;
}

static ssize_t front_camera_hw_param_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct cam_hw_param *ec_param = NULL;
	struct is_rom_info *finfo;
	struct is_cam_info *cam_info;
	int position;

	is_get_cam_info_from_index(&cam_info, CAM_INFO_FRONT);
	position = cam_info->internal_id;

	is_sec_get_sysfs_finfo(&finfo, is_vendor_get_rom_id_from_position(position));
	is_sec_get_hw_param(&ec_param, position);

	if (is_sec_is_valid_moduleid(finfo->rom_module_id)) {
		return sprintf(buf, "\"CAMIF_ID\":\"%c%c%c%c%cXX%02X%02X%02X\",\"I2CF_AF\":\"%d\","
			"\"I2CF_COM\":\"%d\",\"I2CF_OIS\":\"%d\",\"I2CF_SEN\":\"%d\",\"MIPIF_COM\":\"%d\",\"MIPIF_SEN\":\"%d\"\n",
			finfo->rom_module_id[0], finfo->rom_module_id[1], finfo->rom_module_id[2],
			finfo->rom_module_id[3], finfo->rom_module_id[4], finfo->rom_module_id[7],
			finfo->rom_module_id[8], finfo->rom_module_id[9], ec_param->i2c_af_err_cnt,
			ec_param->i2c_comp_err_cnt, ec_param->i2c_ois_err_cnt, ec_param->i2c_sensor_err_cnt, ec_param->mipi_comp_err_cnt,
			ec_param->mipi_sensor_err_cnt);
	} else {
		return sprintf(buf, "\"CAMIF_ID\":\"MIR_ERR\",\"I2CF_AF\":\"%d\","
			"\"I2CF_COM\":\"%d\",\"I2CF_OIS\":\"%d\",\"I2CF_SEN\":\"%d\",\"MIPIF_COM\":\"%d\",\"MIPIF_SEN\":\"%d\"\n",
			ec_param->i2c_af_err_cnt, ec_param->i2c_comp_err_cnt, ec_param->i2c_ois_err_cnt, ec_param->i2c_sensor_err_cnt,
			ec_param->mipi_comp_err_cnt, ec_param->mipi_sensor_err_cnt);
	}
}

static ssize_t front_camera_hw_param_store(struct device *dev,
				    struct device_attribute *attr, const char *buf, size_t count)
{
	struct cam_hw_param *ec_param = NULL;
	struct is_cam_info *cam_info;
	int position;

	is_get_cam_info_from_index(&cam_info, CAM_INFO_FRONT);
	position = cam_info->internal_id;

	if (!strncmp(buf, "c", 1)) {
		is_sec_get_hw_param(&ec_param, position);

		if (ec_param)
			is_sec_init_err_cnt(ec_param);
	}

	return count;
}

#ifdef CAMERA_REAR2
static ssize_t rear2_camera_hw_param_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct cam_hw_param *ec_param = NULL;
	struct is_rom_info *finfo;
	struct is_cam_info *cam_info;
	int position;

	is_get_cam_info_from_index(&cam_info, CAM_INFO_REAR2);
	position = cam_info->internal_id;

	is_sec_get_sysfs_finfo(&finfo, is_vendor_get_rom_id_from_position(position));
	is_sec_get_hw_param(&ec_param, position);

	if (is_sec_is_valid_moduleid(finfo->rom_module_id)) {
		return sprintf(buf, "\"CAMIR2_ID\":\"%c%c%c%c%cXX%02X%02X%02X\",\"I2CR2_AF\":\"%d\","
			"\"I2CR2_COM\":\"%d\",\"I2CR2_OIS\":\"%d\",\"I2CR2_SEN\":\"%d\",\"MIPIR2_COM\":\"%d\",\"MIPIR2_SEN\":\"%d\"\n",
			finfo->rom_module_id[0], finfo->rom_module_id[1], finfo->rom_module_id[2], finfo->rom_module_id[3],
			finfo->rom_module_id[4], finfo->rom_module_id[7], finfo->rom_module_id[8], finfo->rom_module_id[9],
			ec_param->i2c_af_err_cnt, ec_param->i2c_comp_err_cnt, ec_param->i2c_ois_err_cnt,
			ec_param->i2c_sensor_err_cnt, ec_param->mipi_comp_err_cnt, ec_param->mipi_sensor_err_cnt);
	} else {
		return sprintf(buf, "\"CAMIR2_ID\":\"MIR_ERR\",\"I2CR2_AF\":\"%d\","
			"\"I2CR2_COM\":\"%d\",\"I2CR2_OIS\":\"%d\",\"I2CR2_SEN\":\"%d\",\"MIPIR2_COM\":\"%d\",\"MIPIR2_SEN\":\"%d\"\n",
			ec_param->i2c_af_err_cnt, ec_param->i2c_comp_err_cnt, ec_param->i2c_ois_err_cnt,
			ec_param->i2c_sensor_err_cnt, ec_param->mipi_comp_err_cnt, ec_param->mipi_sensor_err_cnt);
	}
}

static ssize_t rear2_camera_hw_param_store(struct device *dev,
				    struct device_attribute *attr, const char *buf, size_t count)
{
	struct cam_hw_param *ec_param = NULL;
	struct is_cam_info *cam_info;
	int position;

	is_get_cam_info_from_index(&cam_info, CAM_INFO_REAR2);
	position = cam_info->internal_id;

	if (!strncmp(buf, "c", 1)) {
		is_sec_get_hw_param(&ec_param, position);

		if (ec_param)
			is_sec_init_err_cnt(ec_param);
	}

	return count;
}
#endif

#ifdef CAMERA_REAR3
static ssize_t rear3_camera_hw_param_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct cam_hw_param *ec_param = NULL;
	struct is_rom_info *finfo;
	struct is_cam_info *cam_info;
	int position;

	is_get_cam_info_from_index(&cam_info, CAM_INFO_REAR3);
	position = cam_info->internal_id;

	is_sec_get_sysfs_finfo(&finfo, is_vendor_get_rom_id_from_position(position));
	is_sec_get_hw_param(&ec_param, position);

	if (is_sec_is_valid_moduleid(finfo->rom_module_id)) {
		return sprintf(buf, "\"CAMIR3_ID\":\"%c%c%c%c%cXX%02X%02X%02X\",\"I2CR3_AF\":\"%d\","
			"\"I2CR3_COM\":\"%d\",\"I2CR3_OIS\":\"%d\",\"I2CR3_SEN\":\"%d\",\"MIPIR3_COM\":\"%d\",\"MIPIR3_SEN\":\"%d\"\n",
			finfo->rom_module_id[0], finfo->rom_module_id[1], finfo->rom_module_id[2], finfo->rom_module_id[3],
			finfo->rom_module_id[4], finfo->rom_module_id[7], finfo->rom_module_id[8], finfo->rom_module_id[9],
			ec_param->i2c_af_err_cnt, ec_param->i2c_comp_err_cnt, ec_param->i2c_ois_err_cnt,
			ec_param->i2c_sensor_err_cnt, ec_param->mipi_comp_err_cnt, ec_param->mipi_sensor_err_cnt);
	} else {
		return sprintf(buf, "\"CAMIR3_ID\":\"MIR_ERR\",\"I2CR3_AF\":\"%d\","
			"\"I2CR3_COM\":\"%d\",\"I2CR3_OIS\":\"%d\",\"I2CR3_SEN\":\"%d\",\"MIPIR3_COM\":\"%d\",\"MIPIR3_SEN\":\"%d\"\n",
			ec_param->i2c_af_err_cnt, ec_param->i2c_comp_err_cnt, ec_param->i2c_ois_err_cnt,
			ec_param->i2c_sensor_err_cnt, ec_param->mipi_comp_err_cnt, ec_param->mipi_sensor_err_cnt);
	}
}

static ssize_t rear3_camera_hw_param_store(struct device *dev,
				    struct device_attribute *attr, const char *buf, size_t count)
{
	struct cam_hw_param *ec_param = NULL;
	struct is_cam_info *cam_info;
	int position;

	is_get_cam_info_from_index(&cam_info, CAM_INFO_REAR3);
	position = cam_info->internal_id;

	if (!strncmp(buf, "c", 1)) {
		is_sec_get_hw_param(&ec_param, position);

		if (ec_param)
			is_sec_init_err_cnt(ec_param);
	}

	return count;
}
#endif

#ifdef CAMERA_REAR4
static ssize_t rear4_camera_hw_param_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct cam_hw_param *ec_param = NULL;
	struct is_rom_info *finfo;
	struct is_cam_info *cam_info;
	int position;

	is_get_cam_info_from_index(&cam_info, CAM_INFO_REAR4);
	position = cam_info->internal_id;

	is_sec_get_sysfs_finfo(&finfo, is_vendor_get_rom_id_from_position(position));
	is_sec_get_hw_param(&ec_param, position);

	if (is_sec_is_valid_moduleid(finfo->rom_module_id)) {
		return sprintf(buf, "\"CAMIR4_ID\":\"%c%c%c%c%cXX%02X%02X%02X\",\"I2CR4_AF\":\"%d\","
			"\"I2CR4_COM\":\"%d\",\"I2CR4_OIS\":\"%d\",\"I2CR4_SEN\":\"%d\",\"MIPIR4_COM\":\"%d\",\"MIPIR4_SEN\":\"%d\"\n",
			finfo->rom_module_id[0], finfo->rom_module_id[1], finfo->rom_module_id[2], finfo->rom_module_id[3],
			finfo->rom_module_id[4], finfo->rom_module_id[7], finfo->rom_module_id[8], finfo->rom_module_id[9],
			ec_param->i2c_af_err_cnt, ec_param->i2c_comp_err_cnt, ec_param->i2c_ois_err_cnt,
			ec_param->i2c_sensor_err_cnt, ec_param->mipi_comp_err_cnt, ec_param->mipi_sensor_err_cnt);
	} else {
		return sprintf(buf, "\"CAMIR4_ID\":\"MIR_ERR\",\"I2CR4_AF\":\"%d\","
			"\"I2CR4_COM\":\"%d\",\"I2CR4_OIS\":\"%d\",\"I2CR4_SEN\":\"%d\",\"MIPIR4_COM\":\"%d\",\"MIPIR4_SEN\":\"%d\"\n",
			ec_param->i2c_af_err_cnt, ec_param->i2c_comp_err_cnt, ec_param->i2c_ois_err_cnt,
			ec_param->i2c_sensor_err_cnt, ec_param->mipi_comp_err_cnt, ec_param->mipi_sensor_err_cnt);
	}
}

static ssize_t rear4_camera_hw_param_store(struct device *dev,
				    struct device_attribute *attr, const char *buf, size_t count)
{
	struct cam_hw_param *ec_param = NULL;
	struct is_cam_info *cam_info;
	int position;

	is_get_cam_info_from_index(&cam_info, CAM_INFO_REAR4);
	position = cam_info->internal_id;

	if (!strncmp(buf, "c", 1)) {
		is_sec_get_hw_param(&ec_param, position);

		if (ec_param)
			is_sec_init_err_cnt(ec_param);
	}

	return count;
}
#endif
#endif


static DEVICE_ATTR(supported_cameraIds, S_IRUGO, camera_supported_cameraIds_show, NULL);

#if defined(CONFIG_CAMERA_FROM) && defined(CAMERA_MODULE_DUALIZE)
static DEVICE_ATTR(from_write, S_IRUGO, camera_rear_writefw_show, NULL);
#endif

static DEVICE_ATTR(rear_camtype, S_IRUGO, camera_rear_camtype_show, NULL);
static DEVICE_ATTR(rear_calcheck, S_IRUGO, camera_rear_calcheck_show, NULL);

static DEVICE_ATTR(rear_caminfo, S_IRUGO, camera_rear_info_show, NULL);
static DEVICE_ATTR(rear_camfw, S_IRUGO|S_IWUSR, camera_rear_camfw_show, camera_rear_camfw_write);
static DEVICE_ATTR(rear_camfw_full, S_IRUGO, camera_rear_camfw_full_show, NULL);
static DEVICE_ATTR(rear_checkfw_user, S_IRUGO, camera_rear_checkfw_user_show, NULL);
static DEVICE_ATTR(rear_checkfw_factory, S_IRUGO, camera_rear_checkfw_factory_show, NULL);
static DEVICE_ATTR(rear_sensorid_exif, S_IRUGO, camera_rear_sensorid_exif_show, NULL);
static DEVICE_ATTR(rear_mtf_exif, S_IRUGO, camera_rear_mtf_exif_show, NULL);
static DEVICE_ATTR(rear_afcal, S_IRUGO, camera_rear_afcal_show, NULL);
static DEVICE_ATTR(rear_paf_cal_check, S_IRUGO, camera_rear_paf_cal_check_show, NULL);
static DEVICE_ATTR(rear_moduleid, S_IRUGO, camera_rear_moduleid_show, NULL);
static DEVICE_ATTR(rear_phy_tune, S_IRUGO, camera_rear_phy_tune_show, NULL);
static DEVICE_ATTR(SVC_rear_module, S_IRUGO, camera_rear_moduleid_show, NULL);

#ifdef CAMERA_REAR_DUAL_CAL
static DEVICE_ATTR(rear_dualcal, S_IRUGO, camera_rear_dualcal_show, NULL);
static DEVICE_ATTR(rear2_dualcal, S_IRUGO, camera_rear2_dualcal_show, NULL);
static DEVICE_ATTR(rear3_dualcal, S_IRUGO, camera_rear3_dualcal_show, NULL);
static DEVICE_ATTR(rear3_dualcal_size, S_IRUGO, camera_rear3_dualcal_size_show, NULL);
#endif

#ifdef CAMERA_FRONT_DUAL_CAL
static DEVICE_ATTR(front_dualcal, S_IRUGO, camera_front_dualcal_show, NULL);
static DEVICE_ATTR(front2_dualcal, S_IRUGO, camera_front2_dualcal_show, NULL);
#endif

#ifdef CAMERA_REAR2
static DEVICE_ATTR(rear2_caminfo, S_IRUGO, camera_rear2_info_show, NULL);
static DEVICE_ATTR(rear2_camfw, S_IRUGO, camera_rear2_camfw_show, camera_rear_camfw_write);
static DEVICE_ATTR(rear2_camfw_full, S_IRUGO, camera_rear2_camfw_full_show, NULL);
static DEVICE_ATTR(rear2_checkfw_user, S_IRUGO, camera_rear2_checkfw_user_show, NULL);
static DEVICE_ATTR(rear2_checkfw_factory, S_IRUGO, camera_rear2_checkfw_factory_show, NULL);
static DEVICE_ATTR(rear2_sensorid_exif, S_IRUGO, camera_rear2_sensorid_exif_show, NULL);
static DEVICE_ATTR(rear2_mtf_exif, S_IRUGO, camera_rear2_mtf_exif_show, NULL);
static DEVICE_ATTR(rear2_phy_tune, S_IRUGO, camera_rear2_phy_tune_show, NULL);
#ifdef CAMERA_REAR2_AFCAL
static DEVICE_ATTR(rear2_afcal, S_IRUGO, camera_rear2_afcal_show, NULL);
static DEVICE_ATTR(rear2_paf_cal_check, S_IRUGO, camera_rear2_paf_cal_check_show, NULL);
#endif
#ifdef CAMERA_REAR2_TILT
static DEVICE_ATTR(rear2_tilt, S_IRUGO, camera_rear2_tilt_show, NULL);
#endif
#ifdef CAMERA_REAR2_MODULEID
static DEVICE_ATTR(rear2_moduleid, S_IRUGO, camera_rear2_moduleid_show, NULL);
static DEVICE_ATTR(SVC_rear_module2, S_IRUGO, camera_rear2_moduleid_show, NULL);
#endif
#endif
#ifdef CAMERA_REAR3
static DEVICE_ATTR(rear3_caminfo, S_IRUGO, camera_rear3_info_show, NULL);
static DEVICE_ATTR(rear3_camtype, S_IRUGO, camera_rear3_camtype_show, NULL);
static DEVICE_ATTR(rear3_camfw, S_IRUGO, camera_rear3_camfw_show, camera_rear_camfw_write);
static DEVICE_ATTR(rear3_camfw_full, S_IRUGO, camera_rear3_camfw_full_show, NULL);
static DEVICE_ATTR(rear3_checkfw_user, S_IRUGO, camera_rear3_checkfw_user_show, NULL);
static DEVICE_ATTR(rear3_checkfw_factory, S_IRUGO, camera_rear3_checkfw_factory_show, NULL);
static DEVICE_ATTR(rear3_sensorid_exif, S_IRUGO, camera_rear3_sensorid_exif_show, NULL);
static DEVICE_ATTR(rear3_mtf_exif, S_IRUGO, camera_rear3_mtf_exif_show, NULL);
static DEVICE_ATTR(rear3_paf_cal_check, S_IRUGO, camera_rear3_paf_cal_check_show, NULL);
static DEVICE_ATTR(rear3_phy_tune, S_IRUGO, camera_rear3_phy_tune_show, NULL);
#ifdef CAMERA_REAR3_AFCAL
static DEVICE_ATTR(rear3_afcal, S_IRUGO, camera_rear3_afcal_show, NULL);
#endif
#ifdef CAMERA_REAR3_TILT
static DEVICE_ATTR(rear3_tilt, S_IRUGO, camera_rear3_tilt_show, NULL);
#endif
#ifdef CAMERA_REAR3_MODULEID
static DEVICE_ATTR(rear3_moduleid, S_IRUGO, camera_rear3_moduleid_show, NULL);
static DEVICE_ATTR(SVC_rear_module3, S_IRUGO, camera_rear3_moduleid_show, NULL);
#endif
#endif

#ifdef CAMERA_REAR4
static DEVICE_ATTR(rear4_caminfo, S_IRUGO, camera_rear4_info_show, NULL);
static DEVICE_ATTR(rear4_camfw, S_IRUGO, camera_rear4_camfw_show, camera_rear_camfw_write);
static DEVICE_ATTR(rear4_camfw_full, S_IRUGO, camera_rear4_camfw_full_show, NULL);
static DEVICE_ATTR(rear4_checkfw_user, S_IRUGO, camera_rear4_checkfw_user_show, NULL);
static DEVICE_ATTR(rear4_checkfw_factory, S_IRUGO, camera_rear4_checkfw_factory_show, NULL);
static DEVICE_ATTR(rear4_sensorid_exif, S_IRUGO, camera_rear4_sensorid_exif_show, NULL);
static DEVICE_ATTR(rear4_mtf_exif, S_IRUGO, camera_rear4_mtf_exif_show, NULL);
static DEVICE_ATTR(rear4_phy_tune, S_IRUGO, camera_rear4_phy_tune_show, NULL);
#ifdef CAMERA_REAR4_AFCAL
static DEVICE_ATTR(rear4_afcal, S_IRUGO, camera_rear4_afcal_show, NULL);
static DEVICE_ATTR(rear4_paf_cal_check, S_IRUGO, camera_rear4_paf_cal_check_show, NULL);
#endif
#ifdef CAMERA_REAR4_TILT
static DEVICE_ATTR(rear4_tilt, S_IRUGO, camera_rear4_tilt_show, NULL);
#endif
#ifdef CAMERA_REAR4_MODULEID
static DEVICE_ATTR(rear4_moduleid, S_IRUGO, camera_rear4_moduleid_show, NULL);
static DEVICE_ATTR(SVC_rear_module4, S_IRUGO, camera_rear4_moduleid_show, NULL);
#endif
#endif



static DEVICE_ATTR(front_caminfo, S_IRUGO, camera_front_info_show, NULL);
static DEVICE_ATTR(front_camtype, S_IRUGO, camera_front_camtype_show, NULL);
static DEVICE_ATTR(front_camfw, S_IRUGO, camera_front_camfw_show, NULL);
static DEVICE_ATTR(front_mipi_clock, 0440, camera_front_mipi_clock_show, NULL);
static DEVICE_ATTR(front_phy_tune, S_IRUGO, camera_front_phy_tune_show, NULL);
#if defined(CAMERA_EEPROM_SUPPORT_FRONT)
static DEVICE_ATTR(front_camfw_full, S_IRUGO, camera_front_camfw_full_show, NULL);
static DEVICE_ATTR(front_checkfw_factory, S_IRUGO, camera_front_checkfw_factory_show, NULL);
#ifndef CAMERA_FRONT_FIXED_FOCUS
static DEVICE_ATTR(front_afcal, S_IRUGO, camera_front_afcal_show, NULL);
#ifdef CAMERA_FRONT_PAFCAL
static DEVICE_ATTR(front_paf_cal_check, S_IRUGO, camera_front_paf_cal_check_show, NULL);
#endif
#endif
static DEVICE_ATTR(front_mtf_exif, S_IRUGO, camera_front_mtf_exif_show, NULL);
static DEVICE_ATTR(front_sensorid_exif, S_IRUGO, camera_front_sensorid_exif_show, NULL);
static DEVICE_ATTR(front_moduleid, S_IRUGO, camera_front_moduleid_show, NULL);
static DEVICE_ATTR(SVC_front_module, S_IRUGO, camera_front_moduleid_show, NULL);
#endif
#ifdef CAMERA_FRONT2
static DEVICE_ATTR(front2_caminfo, S_IRUGO, camera_front2_info_show, NULL);
static DEVICE_ATTR(front2_camfw, S_IRUGO, camera_front2_camfw_show, NULL);
static DEVICE_ATTR(front2_camfw_full, S_IRUGO,	camera_front2_camfw_full_show, NULL);
static DEVICE_ATTR(front2_checkfw_factory, S_IRUGO, camera_front2_checkfw_factory_show, NULL);
static DEVICE_ATTR(front2_sensorid_exif, S_IRUGO, camera_front2_sensorid_exif_show, NULL);
static DEVICE_ATTR(front2_mtf_exif, S_IRUGO, camera_front2_mtf_exif_show, NULL);
#ifdef CAMERA_FRONT2_TILT
static DEVICE_ATTR(front2_tilt, S_IRUGO, camera_front2_tilt_show, NULL);
#endif
#ifdef CAMERA_FRONT2_MODULEID
static DEVICE_ATTR(front2_moduleid, S_IRUGO, camera_front2_moduleid_show, NULL);
static DEVICE_ATTR(SVC_front_module2, S_IRUGO, camera_front2_moduleid_show, NULL);
#endif
#endif


static DEVICE_ATTR(rear_sensor_standby, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH,
		camera_rear_sensor_standby_show, camera_rear_sensor_standby);

static DEVICE_ATTR(fw_update, S_IRUGO, camera_hw_init_show, NULL);
#if defined (CONFIG_OIS_USE)
static DEVICE_ATTR(selftest, S_IRUGO, camera_ois_selftest_show, NULL);
static DEVICE_ATTR(ois_power, S_IWUSR, NULL, camera_ois_power_store);
static DEVICE_ATTR(autotest, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH,
		camera_ois_autotest_show, camera_ois_autotest_store);
static DEVICE_ATTR(ois_set_mode, S_IWUSR, NULL, camera_ois_set_mode_store);
#ifdef CAMERA_2ND_OIS
static DEVICE_ATTR(ois_gain_rear3, S_IRUGO, camera_ois_rear3_gain_show, NULL);
static DEVICE_ATTR(ois_supperssion_ratio_rear3, S_IRUGO, camera_ois_rear3_supperssion_ratio_show, NULL);
#endif
#ifdef CAMERA_3RD_OIS
static DEVICE_ATTR(ois_gain_rear4, S_IRUGO, camera_ois_rear4_gain_show, NULL);
static DEVICE_ATTR(ois_supperssion_ratio_rear4, S_IRUGO, camera_ois_rear4_supperssion_ratio_show, NULL);
#endif
static DEVICE_ATTR(ois_rawdata, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH,
		camera_ois_rawdata_show, camera_ois_rawdata_store);
static DEVICE_ATTR(calibrationtest, S_IRUGO, camera_ois_calibrationtest_show, NULL);
static DEVICE_ATTR(ois_noise_stdev, S_IRUGO, camera_ois_gyronoise_show, NULL);
static DEVICE_ATTR(ois_hall_position, S_IRUGO, camera_ois_hall_position_show, NULL);
static DEVICE_ATTR(oisfw, S_IRUGO, camera_ois_version_show, NULL);
static DEVICE_ATTR(ois_diff, S_IRUGO, camera_ois_diff_show, NULL);
static DEVICE_ATTR(ois_exif, S_IRUGO, camera_ois_exif_show, NULL);
static DEVICE_ATTR(ois_gain_rear, S_IRUGO, camera_ois_rear_gain_show, NULL);
static DEVICE_ATTR(ois_supperssion_ratio_rear, S_IRUGO, camera_ois_rear_supperssion_ratio_show, NULL);
static DEVICE_ATTR(reset_check, S_IRUGO, camera_ois_reset_check, NULL);
static DEVICE_ATTR(prevent_shaking_noise, S_IWUSR, NULL, camera_ois_shaking_noise_store);
static DEVICE_ATTR(check_cross_talk, S_IRUGO, camera_ois_check_cross_talk_show, NULL);
static DEVICE_ATTR(check_hall_cal, S_IRUGO, camera_ois_check_hall_cal_show, NULL);
static DEVICE_ATTR(check_ois_valid, S_IRUGO, camera_ois_check_valid_show, NULL);
static DEVICE_ATTR(ois_center_shift, S_IWUSR, NULL, camera_ois_center_shift_store);
static DEVICE_ATTR(ois_ext_clk, S_IRUGO, camera_ois_read_ext_clock_show, NULL);
static DEVICE_ATTR(rear3_read_cross_talk, S_IRUGO, camera_ois_rear3_read_cross_talk_show, NULL);
#endif
#ifdef FORCE_CAL_LOAD
static DEVICE_ATTR(rear_force_cal_load, S_IRUGO, camera_rear_force_cal_load_show, NULL);
#endif
static DEVICE_ATTR(rear_paf_offset_mid, S_IRUGO, camera_rear_paf_offset_mid_show, NULL);
static DEVICE_ATTR(rear_paf_offset_far, S_IRUGO, camera_rear_paf_offset_far_show, NULL);

static DEVICE_ATTR(rear_camfw_all, S_IRUGO, camera_rear_camfw_all_show, NULL);
static DEVICE_ATTR(ssrm_camera_info, 0644, camera_ssrm_camera_info_show, camera_ssrm_camera_info_store);

#ifdef USE_CAMERA_HW_BIG_DATA
static DEVICE_ATTR(rear_hwparam, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH,
				rear_camera_hw_param_show, rear_camera_hw_param_store);
static DEVICE_ATTR(front_hwparam, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH,
				front_camera_hw_param_show, front_camera_hw_param_store);
#ifdef CAMERA_FRONT2
static DEVICE_ATTR(front2_hwparam, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH,
				front2_camera_hw_param_show, front2_camera_hw_param_store);
#endif

#ifdef CAMERA_REAR2
static DEVICE_ATTR(rear2_hwparam, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH,
				rear2_camera_hw_param_show, rear2_camera_hw_param_store);
#endif
#ifdef CAMERA_REAR3
static DEVICE_ATTR(rear3_hwparam, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH,
				rear3_camera_hw_param_show, rear3_camera_hw_param_store);
#endif

#ifdef CAMERA_REAR4
static DEVICE_ATTR(rear4_hwparam, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH,
				rear4_camera_hw_param_show, rear4_camera_hw_param_store);
#endif
#endif

static struct attribute *svc_cam_attrs[] = {
	&dev_attr_SVC_rear_module.attr,
#ifdef CAMERA_REAR2_MODULEID
	&dev_attr_SVC_rear_module2.attr,
#endif
#ifdef CAMERA_REAR3_MODULEID
	&dev_attr_SVC_rear_module3.attr,
#endif
#ifdef CAMERA_REAR4_MODULEID
	&dev_attr_SVC_rear_module4.attr,
#endif
#if defined(CAMERA_EEPROM_SUPPORT_FRONT)
	&dev_attr_SVC_front_module.attr,
#ifdef CAMERA_FRONT2_MODULEID
	&dev_attr_SVC_front_module2.attr,
#endif
#endif
	NULL,
};

static struct attribute_group svc_cam_group = {
	.attrs = svc_cam_attrs,
};

static const struct attribute_group *svc_cam_groups[] = {
	&svc_cam_group,
	NULL,
};

static void svc_cam_release(struct device *dev)
{
	kfree(dev);
}
int svc_cheating_prevent_device_file_create(void)
{
	struct kernfs_node *svc_sd;
	struct kobject *data;
	struct device *dev;
	int err;
	struct device *is_dev;
	struct kobject *top_kobj;

	is_dev = is_get_is_dev();

	/* To find svc kobject */
	top_kobj = &is_dev->kobj.kset->kobj;

	svc_sd = sysfs_get_dirent(top_kobj->sd, "svc");
	if (IS_ERR_OR_NULL(svc_sd)) {
		/* try to create svc kobject */
		data = kobject_create_and_add("svc", top_kobj);
		if (IS_ERR_OR_NULL(data))
			pr_info("Failed to create sys/devices/svc already exist svc : 0x%pK\n", data);
		else
			pr_info("Success to create sys/devices/svc svc : 0x%pK\n", data);
	} else {
		data = (struct kobject *)svc_sd->priv;
		pr_info("Success to find svc_sd : 0x%pK svc : 0x%pK\n", svc_sd, data);
	}

	dev = kzalloc(sizeof(struct device), GFP_KERNEL);
	if (!dev) {
		pr_err("Error allocating svc_ap device\n");
		return -ENOMEM;
	}

	err = dev_set_name(dev, "Camera");
	if (err < 0)
		goto err_name;

	dev->kobj.parent = data;
	dev->groups = svc_cam_groups;
	dev->release = svc_cam_release;

	err = device_register(dev);
	if (err < 0)
		goto err_dev_reg;

	return 0;

err_dev_reg:
	put_device(dev);
err_name:
	kfree(dev);
	dev = NULL;
	return err;
}

int is_create_sysfs(struct is_core *core)
{
	if (!core) {
		err("is_core is null");
		return -EINVAL;
	}

	svc_cheating_prevent_device_file_create();

	if (camera_class == NULL) {
		camera_class = class_create(THIS_MODULE, "camera");
		if (IS_ERR(camera_class)) {
			pr_err("Failed to create class(camera)!\n");
			return PTR_ERR(camera_class);
		}
	}

	camera_front_dev = device_create(camera_class, NULL, 0, NULL, "front");
	if (IS_ERR(camera_front_dev)) {
		pr_err("failed to create front device!\n");
	} else {
		if (device_create_file(camera_front_dev, &dev_attr_front_camtype) < 0) {
			pr_err("failed to create front device file, %s\n",
				dev_attr_front_camtype.attr.name);
		}
		if (device_create_file(camera_front_dev, &dev_attr_front_camfw) < 0) {
			pr_err("failed to create front device file, %s\n",
				dev_attr_front_camfw.attr.name);
		}
		if (device_create_file(camera_front_dev, &dev_attr_front_caminfo) < 0) {
			pr_err("failed to create front device file, %s\n",
				dev_attr_front_caminfo.attr.name);
		}
#if defined(CAMERA_EEPROM_SUPPORT_FRONT)
		if (device_create_file(camera_front_dev, &dev_attr_front_camfw_full) < 0) {
			pr_err("failed to create front device file, %s\n",
				dev_attr_front_camfw_full.attr.name);
		}
		if (device_create_file(camera_front_dev, &dev_attr_front_checkfw_factory) < 0) {
			pr_err("failed to create front device file, %s\n",
				dev_attr_front_checkfw_factory.attr.name);
		}
		if (device_create_file(camera_front_dev, &dev_attr_front_sensorid_exif) < 0) {
			pr_err("failed to create front device file, %s\n",
				dev_attr_front_sensorid_exif.attr.name);
		}
		if (device_create_file(camera_front_dev, &dev_attr_front_mtf_exif) < 0) {
			pr_err("failed to create front device file, %s\n",
					dev_attr_front_mtf_exif.attr.name);
		}
#ifndef CAMERA_FRONT_FIXED_FOCUS
#ifdef CAMERA_FRONT_PAFCAL
		if (device_create_file(camera_front_dev, &dev_attr_front_paf_cal_check) < 0) {
			pr_err("failed to create front device file, %s\n",
					dev_attr_front_paf_cal_check.attr.name);
		}
#endif
		if (device_create_file(camera_front_dev, &dev_attr_front_afcal) < 0) {
			pr_err("failed to create front device file, %s\n",
					dev_attr_front_afcal.attr.name);
		}
#endif
		if (device_create_file(camera_front_dev, &dev_attr_front_moduleid) < 0) {
			pr_err("failed to create front device file, %s\n",
					dev_attr_front_moduleid.attr.name);
		}
#endif
#ifdef CAMERA_FRONT_DUAL_CAL
		if (device_create_file(camera_front_dev, &dev_attr_front_dualcal) < 0) {
			pr_err("failed to create rear device file, %s\n",
					dev_attr_rear_dualcal.attr.name);
		}
		if (device_create_file(camera_front_dev, &dev_attr_front2_dualcal) < 0) {
			pr_err("failed to create rear device file, %s\n",
					dev_attr_rear_dualcal.attr.name);
		}
#endif
#ifdef CAMERA_FRONT2
		if (device_create_file(camera_front_dev, &dev_attr_front2_caminfo) < 0) {
			pr_err("failed to create front device file, %s\n",
				dev_attr_front2_caminfo.attr.name);
		}
		if (device_create_file(camera_front_dev, &dev_attr_front2_camfw) < 0) {
			pr_err("failed to create front device file, %s\n",
				dev_attr_front2_camfw.attr.name);
		}
		if (device_create_file(camera_front_dev, &dev_attr_front2_camfw_full) < 0) {
			pr_err("failed to create front device file, %s\n",
				dev_attr_front2_camfw_full.attr.name);
		}
		if (device_create_file(camera_front_dev, &dev_attr_front2_checkfw_factory) < 0) {
			pr_err("failed to create front device file, %s\n",
				dev_attr_front2_checkfw_factory.attr.name);
		}
		if (device_create_file(camera_front_dev, &dev_attr_front2_sensorid_exif) < 0) {
			pr_err("failed to create front device file, %s\n",
				dev_attr_front2_sensorid_exif.attr.name);
		}
		if (device_create_file(camera_front_dev, &dev_attr_front2_mtf_exif) < 0) {
			pr_err("failed to create front device file, %s\n",
					dev_attr_front2_mtf_exif.attr.name);
		}
#ifdef CAMERA_FRONT2_TILT
		if (device_create_file(camera_front_dev, &dev_attr_front2_tilt) < 0) {
			pr_err("failed to create front device file, %s\n",
					dev_attr_front2_tilt.attr.name);
		}
#endif
#ifdef USE_CAMERA_HW_BIG_DATA
		if (device_create_file(camera_front_dev, &dev_attr_front2_hwparam) < 0) {
			pr_err("failed to create front device file, %s\n",
				dev_attr_front2_hwparam.attr.name);
		}
#endif
#ifdef CAMERA_FRONT2_MODULEID
		if (device_create_file(camera_front_dev, &dev_attr_front2_moduleid) < 0) {
			pr_err("failed to create front device file, %s\n",
					dev_attr_front2_moduleid.attr.name);
		}
#endif
#endif


#ifdef USE_CAMERA_HW_BIG_DATA
		if (device_create_file(camera_front_dev, &dev_attr_front_hwparam) < 0) {
			pr_err("failed to create front device file, %s\n",
				dev_attr_front_hwparam.attr.name);
		}
#endif
		if (device_create_file(camera_front_dev, &dev_attr_front_mipi_clock) < 0) {
			pr_err("failed to create front device file, %s\n",
				dev_attr_front_mipi_clock.attr.name);
		}
		if (device_create_file(camera_front_dev, &dev_attr_front_phy_tune) < 0) {
			pr_err("failed to create front device file, %s\n",
				dev_attr_front_phy_tune.attr.name);
		}
	}

	camera_rear_dev = device_create(camera_class, NULL, 1, NULL, "rear");
	if (IS_ERR(camera_rear_dev)) {
		pr_err("failed to create rear device!\n");
	} else {
#if defined(CONFIG_CAMERA_FROM) && defined(CAMERA_MODULE_DUALIZE)
		if (device_create_file(camera_rear_dev, &dev_attr_from_write) < 0) {
			pr_err("failed to create rear device file, %s\n",
				dev_attr_from_write.attr.name);
		}
#endif
		if (device_create_file(camera_rear_dev, &dev_attr_rear_camtype) < 0) {
			pr_err("failed to create rear device file, %s\n",
				dev_attr_rear_camtype.attr.name);
		}
		if (device_create_file(camera_rear_dev, &dev_attr_rear_caminfo) < 0) {
			pr_err("failed to create rear device file, %s\n",
				dev_attr_rear_caminfo.attr.name);
		}
		if (device_create_file(camera_rear_dev, &dev_attr_rear_camfw) < 0) {
			pr_err("failed to create rear device file, %s\n",
				dev_attr_rear_camfw.attr.name);
		}
		if (device_create_file(camera_rear_dev, &dev_attr_rear_camfw_full) < 0) {
			pr_err("failed to create rear device file, %s\n",
				dev_attr_rear_camfw_full.attr.name);
		}
		if (device_create_file(camera_rear_dev, &dev_attr_rear_checkfw_user) < 0) {
			pr_err("failed to create rear device file, %s\n",
				dev_attr_rear_checkfw_user.attr.name);
		}
		if (device_create_file(camera_rear_dev, &dev_attr_rear_checkfw_factory) < 0) {
			pr_err("failed to create rear device file, %s\n",
				dev_attr_rear_checkfw_factory.attr.name);
		}
		if (device_create_file(camera_rear_dev, &dev_attr_rear_sensorid_exif) < 0) {
			pr_err("failed to create rear device file, %s\n",
					dev_attr_rear_sensorid_exif.attr.name);
		}
		if (device_create_file(camera_rear_dev, &dev_attr_rear_mtf_exif) < 0) {
			pr_err("failed to create rear device file, %s\n",
					dev_attr_rear_mtf_exif.attr.name);
		}
		if (device_create_file(camera_rear_dev, &dev_attr_rear_afcal) < 0) {
			pr_err("failed to create rear device file, %s\n",
					dev_attr_rear_afcal.attr.name);
		}
		if (device_create_file(camera_rear_dev, &dev_attr_rear_moduleid) < 0) {
			pr_err("failed to create rear device file, %s\n",
					dev_attr_rear_moduleid.attr.name);
		}
		if (device_create_file(camera_rear_dev, &dev_attr_rear_phy_tune) < 0) {
			pr_err("failed to create rear device file, %s\n",
				dev_attr_rear_phy_tune.attr.name);
		}
#ifdef CAMERA_REAR_DUAL_CAL
		if (device_create_file(camera_rear_dev, &dev_attr_rear_dualcal) < 0) {
			pr_err("failed to create rear device file, %s\n",
					dev_attr_rear_dualcal.attr.name);
		}
		if (device_create_file(camera_rear_dev, &dev_attr_rear2_dualcal) < 0) {
			pr_err("failed to create rear device file, %s\n",
					dev_attr_rear_dualcal.attr.name);
		}
		if (device_create_file(camera_rear_dev, &dev_attr_rear3_dualcal) < 0) {
			pr_err("failed to create rear device file, %s\n",
					dev_attr_rear_dualcal.attr.name);
		}
		if (device_create_file(camera_rear_dev, &dev_attr_rear3_dualcal_size) < 0) {
			pr_err("failed to create rear device file, %s\n",
					dev_attr_rear_dualcal.attr.name);
		}
#endif

#ifdef CAMERA_REAR2
		if (device_create_file(camera_rear_dev, &dev_attr_rear2_caminfo) < 0) {
			pr_err("failed to create rear device file, %s\n",
				dev_attr_rear2_caminfo.attr.name);
		}
		if (device_create_file(camera_rear_dev, &dev_attr_rear2_camfw) < 0) {
			pr_err("failed to create rear device file, %s\n",
				dev_attr_rear2_camfw.attr.name);
		}
		if (device_create_file(camera_rear_dev, &dev_attr_rear2_camfw_full) < 0) {
			pr_err("failed to create rear device file, %s\n",
				dev_attr_rear2_camfw_full.attr.name);
		}
		if (device_create_file(camera_rear_dev, &dev_attr_rear2_checkfw_user) < 0) {
			pr_err("failed to create rear device file, %s\n",
				dev_attr_rear2_checkfw_user.attr.name);
		}
		if (device_create_file(camera_rear_dev, &dev_attr_rear2_checkfw_factory) < 0) {
			pr_err("failed to create rear device file, %s\n",
				dev_attr_rear2_checkfw_factory.attr.name);
		}
		if (device_create_file(camera_rear_dev, &dev_attr_rear2_sensorid_exif) < 0) {
			pr_err("failed to create rear device file, %s\n",
					dev_attr_rear2_sensorid_exif.attr.name);
		}
		if (device_create_file(camera_rear_dev, &dev_attr_rear2_mtf_exif) < 0) {
			pr_err("failed to create rear device file, %s\n",
					dev_attr_rear2_mtf_exif.attr.name);
		}
		if (device_create_file(camera_rear_dev, &dev_attr_rear2_phy_tune) < 0) {
			pr_err("failed to create rear device file, %s\n",
				dev_attr_rear2_phy_tune.attr.name);
		}
#ifdef CAMERA_REAR2_AFCAL
		if (device_create_file(camera_rear_dev, &dev_attr_rear2_afcal) < 0) {
			pr_err("failed to create rear device file, %s\n",
					dev_attr_rear2_afcal.attr.name);
		}
		if (device_create_file(camera_rear_dev, &dev_attr_rear2_paf_cal_check) < 0) {
			pr_err("failed to create rear device file, %s\n",
					dev_attr_rear2_paf_cal_check.attr.name);
		}
#endif
#ifdef CAMERA_REAR2_TILT
		if (device_create_file(camera_rear_dev, &dev_attr_rear2_tilt) < 0) {
			pr_err("failed to create rear device file, %s\n",
					dev_attr_rear2_tilt.attr.name);
		}
#endif
#ifdef USE_CAMERA_HW_BIG_DATA
		if (device_create_file(camera_rear_dev, &dev_attr_rear2_hwparam) < 0) {
			pr_err("failed to create rear device file, %s\n",
				dev_attr_rear2_hwparam.attr.name);
		}
#endif
#ifdef CAMERA_REAR2_MODULEID
		if (device_create_file(camera_rear_dev, &dev_attr_rear2_moduleid) < 0) {
			pr_err("failed to create rear device file, %s\n",
					dev_attr_rear2_moduleid.attr.name);
		}
#endif
#endif
#ifdef CAMERA_REAR3
		if (device_create_file(camera_rear_dev, &dev_attr_rear3_caminfo) < 0) {
			pr_err("failed to create rear device file, %s\n",
				dev_attr_rear3_caminfo.attr.name);
		}
		if (device_create_file(camera_rear_dev, &dev_attr_rear3_camtype) < 0) {
			pr_err("failed to create rear device file, %s\n",
				dev_attr_rear3_camtype.attr.name);
		}
		if (device_create_file(camera_rear_dev, &dev_attr_rear3_camfw) < 0) {
			pr_err("failed to create rear device file, %s\n",
				dev_attr_rear3_camfw.attr.name);
		}
		if (device_create_file(camera_rear_dev, &dev_attr_rear3_camfw_full) < 0) {
			pr_err("failed to create rear device file, %s\n",
				dev_attr_rear3_camfw_full.attr.name);
		}
		if (device_create_file(camera_rear_dev, &dev_attr_rear3_checkfw_user) < 0) {
			pr_err("failed to create rear device file, %s\n",
				dev_attr_rear3_checkfw_user.attr.name);
		}
		if (device_create_file(camera_rear_dev, &dev_attr_rear3_checkfw_factory) < 0) {
			pr_err("failed to create rear device file, %s\n",
				dev_attr_rear3_checkfw_factory.attr.name);
		}
		if (device_create_file(camera_rear_dev, &dev_attr_rear3_sensorid_exif) < 0) {
			pr_err("failed to create rear device file, %s\n",
					dev_attr_rear3_sensorid_exif.attr.name);
		}
		if (device_create_file(camera_rear_dev, &dev_attr_rear3_mtf_exif) < 0) {
			pr_err("failed to create rear device file, %s\n",
					dev_attr_rear3_mtf_exif.attr.name);
		}
		if (device_create_file(camera_rear_dev, &dev_attr_rear3_paf_cal_check) < 0) {
			pr_err("failed to create rear device file, %s\n",
					dev_attr_rear3_paf_cal_check.attr.name);
		}
		if (device_create_file(camera_rear_dev, &dev_attr_rear3_phy_tune) < 0) {
			pr_err("failed to create rear device file, %s\n",
				dev_attr_rear3_phy_tune.attr.name);
		}
#ifdef CAMERA_REAR3_AFCAL
		if (device_create_file(camera_rear_dev, &dev_attr_rear3_afcal) < 0) {
			pr_err("failed to create rear device file, %s\n",
					dev_attr_rear3_afcal.attr.name);
		}
#endif
#ifdef CAMERA_REAR3_TILT
		if (device_create_file(camera_rear_dev, &dev_attr_rear3_tilt) < 0) {
			pr_err("failed to create rear device file, %s\n",
					dev_attr_rear3_tilt.attr.name);
		}
#endif
#ifdef USE_CAMERA_HW_BIG_DATA
		if (device_create_file(camera_rear_dev, &dev_attr_rear3_hwparam) < 0) {
			pr_err("failed to create rear device file, %s\n",
				dev_attr_rear3_hwparam.attr.name);
		}
#endif
#ifdef CAMERA_REAR3_MODULEID
		if (device_create_file(camera_rear_dev, &dev_attr_rear3_moduleid) < 0) {
			pr_err("failed to create rear device file, %s\n",
					dev_attr_rear3_moduleid.attr.name);
		}
#endif
#endif

#ifdef CAMERA_REAR4
		if (device_create_file(camera_rear_dev, &dev_attr_rear4_caminfo) < 0) {
			pr_err("failed to create rear device file, %s\n",
				dev_attr_rear4_caminfo.attr.name);
		}
		if (device_create_file(camera_rear_dev, &dev_attr_rear4_camfw) < 0) {
			pr_err("failed to create rear device file, %s\n",
				dev_attr_rear4_camfw.attr.name);
		}
		if (device_create_file(camera_rear_dev, &dev_attr_rear4_camfw_full) < 0) {
			pr_err("failed to create rear device file, %s\n",
				dev_attr_rear4_camfw_full.attr.name);
		}
		if (device_create_file(camera_rear_dev, &dev_attr_rear4_checkfw_user) < 0) {
			pr_err("failed to create rear device file, %s\n",
				dev_attr_rear4_checkfw_user.attr.name);
		}
		if (device_create_file(camera_rear_dev, &dev_attr_rear4_checkfw_factory) < 0) {
			pr_err("failed to create rear device file, %s\n",
				dev_attr_rear4_checkfw_factory.attr.name);
		}
		if (device_create_file(camera_rear_dev, &dev_attr_rear4_sensorid_exif) < 0) {
			pr_err("failed to create rear device file, %s\n",
					dev_attr_rear4_sensorid_exif.attr.name);
		}
		if (device_create_file(camera_rear_dev, &dev_attr_rear4_mtf_exif) < 0) {
			pr_err("failed to create rear device file, %s\n",
					dev_attr_rear4_mtf_exif.attr.name);
		}
		if (device_create_file(camera_rear_dev, &dev_attr_rear4_phy_tune) < 0) {
			pr_err("failed to create rear device file, %s\n",
				dev_attr_rear4_phy_tune.attr.name);
		}
#ifdef CAMERA_REAR4_AFCAL
		if (device_create_file(camera_rear_dev, &dev_attr_rear4_afcal) < 0) {
			pr_err("failed to create rear device file, %s\n",
					dev_attr_rear4_afcal.attr.name);
		}
		if (device_create_file(camera_rear_dev, &dev_attr_rear4_paf_cal_check) < 0) {
			pr_err("failed to create rear device file, %s\n",
					dev_attr_rear4_paf_cal_check.attr.name);
		}
#endif
#ifdef CAMERA_REAR4_TILT
		if (device_create_file(camera_rear_dev, &dev_attr_rear4_tilt) < 0) {
			pr_err("failed to create rear device file, %s\n",
					dev_attr_rear4_tilt.attr.name);
		}
#endif
#ifdef USE_CAMERA_HW_BIG_DATA
		if (device_create_file(camera_rear_dev, &dev_attr_rear4_hwparam) < 0) {
			pr_err("failed to create rear device file, %s\n",
				dev_attr_rear4_hwparam.attr.name);
		}
#endif
#ifdef CAMERA_REAR4_MODULEID
		if (device_create_file(camera_rear_dev, &dev_attr_rear4_moduleid) < 0) {
			pr_err("failed to create rear device file, %s\n",
					dev_attr_rear4_moduleid.attr.name);
		}
#endif
#endif

		if (device_create_file(camera_rear_dev, &dev_attr_ssrm_camera_info) < 0) {
			pr_err("failed to create front device file, %s\n",
				dev_attr_ssrm_camera_info.attr.name);
		}
		if (device_create_file(camera_rear_dev, &dev_attr_rear_sensor_standby) < 0) {
			pr_err("failed to create rear device file, %s\n",
				dev_attr_rear_sensor_standby.attr.name);
		}
		if (device_create_file(camera_rear_dev, &dev_attr_supported_cameraIds) < 0) {
			pr_err("failed to create rear device file, %s\n",
				dev_attr_supported_cameraIds.attr.name);
		}
#ifdef USE_CAMERA_HW_BIG_DATA
		if (device_create_file(camera_rear_dev, &dev_attr_rear_hwparam) < 0) {
			pr_err("failed to create rear device file, %s\n",
				dev_attr_rear_hwparam.attr.name);
		}
#endif
		if (device_create_file(camera_rear_dev, &dev_attr_rear_calcheck) < 0) {
			pr_err("failed to create rear device file, %s\n",
				dev_attr_rear_calcheck.attr.name);
		}
		if (device_create_file(camera_rear_dev, &dev_attr_fw_update) < 0) {
			pr_err("failed to create rear device file, %s\n",
				dev_attr_fw_update.attr.name);
		}
#ifdef FORCE_CAL_LOAD
		if (device_create_file(camera_rear_dev, &dev_attr_rear_force_cal_load) < 0) {
			pr_err("failed to create rear device file, %s\n",
					dev_attr_rear_force_cal_load.attr.name);
		}
#endif
		if (device_create_file(camera_rear_dev, &dev_attr_rear_camfw_all) < 0) {
			pr_err("failed to create rear device file, %s\n",
					dev_attr_rear_camfw_all.attr.name);
		}
		if (device_create_file(camera_rear_dev, &dev_attr_rear_paf_offset_mid) < 0) {
			printk(KERN_ERR "failed to create rear device file, %s\n",
					dev_attr_rear_paf_offset_far.attr.name);
		}
		if (device_create_file(camera_rear_dev, &dev_attr_rear_paf_offset_far) < 0) {
			printk(KERN_ERR "failed to create rear device file, %s\n",
					dev_attr_rear_paf_offset_far.attr.name);
		}
		if (device_create_file(camera_rear_dev, &dev_attr_rear_paf_cal_check) < 0) {
			printk(KERN_ERR "failed to create rear device file, %s\n",
					dev_attr_rear_paf_cal_check.attr.name);
		}
	}

#if defined (CONFIG_OIS_USE)
	camera_ois_dev = device_create(camera_class, NULL, 2, NULL, "ois");
	if (IS_ERR(camera_ois_dev)) {
		pr_err("failed to create ois device!\n");
	} else {
		if (device_create_file(camera_ois_dev, &dev_attr_selftest) < 0) {
			pr_err("failed to create ois device file, %s\n",
				dev_attr_selftest.attr.name);
		}
		if (device_create_file(camera_ois_dev, &dev_attr_ois_power) < 0) {
			pr_err("failed to create ois device file, %s\n",
				dev_attr_ois_power.attr.name);
		}
		if (device_create_file(camera_ois_dev, &dev_attr_ois_set_mode) < 0) {
			pr_err("failed to create ois device file, %s\n",
				dev_attr_ois_set_mode.attr.name);
		}
		if (device_create_file(camera_ois_dev, &dev_attr_autotest) < 0) {
			pr_err("failed to create ois device file, %s\n",
				dev_attr_autotest.attr.name);
		}

		if (device_create_file(camera_ois_dev, &dev_attr_ois_rawdata) < 0) {
			pr_err("failed to create ois device file, %s\n",
				dev_attr_ois_rawdata.attr.name);
		}
		if (device_create_file(camera_ois_dev, &dev_attr_calibrationtest) < 0) {
			pr_err("failed to create ois device file, %s\n",
				dev_attr_calibrationtest.attr.name);
		}
		if (device_create_file(camera_ois_dev, &dev_attr_ois_noise_stdev) < 0) {
			pr_err("failed to create ois device file, %s\n",
				dev_attr_ois_noise_stdev.attr.name);
		}
		if (device_create_file(camera_ois_dev, &dev_attr_ois_hall_position) < 0) {
			pr_err("failed to create ois device file, %s\n",
				dev_attr_ois_hall_position.attr.name);
		}
		if (device_create_file(camera_ois_dev, &dev_attr_oisfw) < 0) {
			pr_err("failed to create ois device file, %s\n",
				dev_attr_oisfw.attr.name);
		}
		if (device_create_file(camera_ois_dev, &dev_attr_ois_diff) < 0) {
			pr_err("failed to create ois device file, %s\n",
				dev_attr_ois_diff.attr.name);
		}
		if (device_create_file(camera_ois_dev, &dev_attr_ois_exif) < 0) {
			pr_err("failed to create ois device file, %s\n",
				dev_attr_ois_exif.attr.name);
		}
		if (device_create_file(camera_ois_dev, &dev_attr_ois_gain_rear) < 0) {
			pr_err("failed to create ois device file, %s\n",
				dev_attr_ois_gain_rear.attr.name);
		}
		if (device_create_file(camera_ois_dev, &dev_attr_ois_supperssion_ratio_rear) < 0) {
			pr_err("failed to create ois device file, %s\n",
				dev_attr_ois_supperssion_ratio_rear.attr.name);
		}
		if (device_create_file(camera_ois_dev, &dev_attr_reset_check) < 0) {
			pr_err("failed to create ois device file, %s\n",
				dev_attr_reset_check.attr.name);
		}
		if (device_create_file(camera_ois_dev, &dev_attr_prevent_shaking_noise) < 0) {
			pr_err("failed to create ois device file, %s\n",
				dev_attr_prevent_shaking_noise.attr.name);
		}
		if (device_create_file(camera_ois_dev, &dev_attr_check_cross_talk) < 0) {
			pr_err("failed to create ois device file, %s\n",
				dev_attr_check_cross_talk.attr.name);
		}
		if (device_create_file(camera_ois_dev, &dev_attr_check_hall_cal) < 0) {
			pr_err("failed to create ois device file, %s\n",
				dev_attr_check_hall_cal.attr.name);
		}
		if (device_create_file(camera_ois_dev, &dev_attr_check_ois_valid) < 0) {
			pr_err("failed to create ois device file, %s\n",
				dev_attr_check_ois_valid.attr.name);
		}
		if (device_create_file(camera_ois_dev, &dev_attr_ois_center_shift) < 0) {
			pr_err("failed to create ois device file, %s\n",
				dev_attr_ois_center_shift.attr.name);
		}
		if (device_create_file(camera_ois_dev, &dev_attr_ois_ext_clk) < 0) {
			pr_err("failed to create ois device file, %s\n",
				dev_attr_ois_ext_clk.attr.name);
		}
		if (device_create_file(camera_ois_dev, &dev_attr_rear3_read_cross_talk) < 0) {
			pr_err("failed to create ois device file, %s\n",
				dev_attr_rear3_read_cross_talk.attr.name);
		}
	}
#endif

	sysfs_core = core;

	return 0;
}

int is_destroy_sysfs(struct is_core *core)
{
	if (camera_front_dev) {
		device_remove_file(camera_front_dev, &dev_attr_front_camtype);
		device_remove_file(camera_front_dev, &dev_attr_front_caminfo);
		device_remove_file(camera_front_dev, &dev_attr_front_camfw);
#if defined(CAMERA_EEPROM_SUPPORT_FRONT)
#ifndef CAMERA_FRONT_FIXED_FOCUS
		device_remove_file(camera_front_dev, &dev_attr_front_afcal);
#ifdef CAMERA_FRONT_PAFCAL
		device_remove_file(camera_front_dev, &dev_attr_front_paf_cal_check);
#endif
#endif
		device_remove_file(camera_front_dev, &dev_attr_front_camfw_full);
		device_remove_file(camera_front_dev, &dev_attr_front_checkfw_factory);
		device_remove_file(camera_front_dev, &dev_attr_front_moduleid);
		device_remove_file(camera_front_dev, &dev_attr_front_mtf_exif);
		device_remove_file(camera_front_dev, &dev_attr_front_sensorid_exif);
#endif
#ifdef CAMERA_FRONT_DUAL_CAL
		device_remove_file(camera_front_dev, &dev_attr_front_dualcal);
		device_remove_file(camera_front_dev, &dev_attr_front2_dualcal);
#endif


#ifdef USE_CAMERA_HW_BIG_DATA
		device_remove_file(camera_front_dev, &dev_attr_front_hwparam);
#endif
		device_remove_file(camera_front_dev, &dev_attr_front_mipi_clock);
		device_remove_file(camera_front_dev, &dev_attr_front_phy_tune);
	}

	if (camera_rear_dev) {
#if defined(CONFIG_CAMERA_FROM) && defined(CAMERA_MODULE_DUALIZE)
		device_remove_file(camera_rear_dev, &dev_attr_from_write);
#endif
		device_remove_file(camera_rear_dev, &dev_attr_rear_camtype);
		device_remove_file(camera_rear_dev, &dev_attr_rear_caminfo);
		device_remove_file(camera_rear_dev, &dev_attr_rear_camfw);
		device_remove_file(camera_rear_dev, &dev_attr_rear_camfw_full);
		device_remove_file(camera_rear_dev, &dev_attr_rear_checkfw_user);
		device_remove_file(camera_rear_dev, &dev_attr_rear_checkfw_factory);
		device_remove_file(camera_rear_dev, &dev_attr_rear_sensorid_exif);
		device_remove_file(camera_rear_dev, &dev_attr_rear_mtf_exif);
		device_remove_file(camera_rear_dev, &dev_attr_rear_afcal);
		device_remove_file(camera_rear_dev, &dev_attr_rear_moduleid);
		device_remove_file(camera_rear_dev, &dev_attr_rear_phy_tune);
#ifdef CAMERA_REAR_DUAL_CAL
		device_remove_file(camera_rear_dev, &dev_attr_rear_dualcal);
		device_remove_file(camera_rear_dev, &dev_attr_rear2_dualcal);
		device_remove_file(camera_rear_dev, &dev_attr_rear3_dualcal);
		device_remove_file(camera_rear_dev, &dev_attr_rear3_dualcal_size);
#endif
#ifdef CAMERA_REAR2
		device_remove_file(camera_rear_dev, &dev_attr_rear2_caminfo);
		device_remove_file(camera_rear_dev, &dev_attr_rear2_camfw);
		device_remove_file(camera_rear_dev, &dev_attr_rear2_camfw_full);
		device_remove_file(camera_rear_dev, &dev_attr_rear2_checkfw_user);
		device_remove_file(camera_rear_dev, &dev_attr_rear2_checkfw_factory);
		device_remove_file(camera_rear_dev, &dev_attr_rear2_sensorid_exif);
		device_remove_file(camera_rear_dev, &dev_attr_rear2_mtf_exif);
		device_remove_file(camera_rear_dev, &dev_attr_rear2_phy_tune);
#ifdef CAMERA_REAR2_AFCAL
		device_remove_file(camera_rear_dev, &dev_attr_rear2_afcal);
		device_remove_file(camera_rear_dev, &dev_attr_rear2_paf_cal_check);
#endif
#ifdef CAMERA_REAR2_TILT
		device_remove_file(camera_rear_dev, &dev_attr_rear2_tilt);
#endif
#ifdef USE_CAMERA_HW_BIG_DATA
		device_remove_file(camera_rear_dev, &dev_attr_rear2_hwparam);
#endif
#ifdef CAMERA_REAR2_MODULEID
		device_remove_file(camera_rear_dev, &dev_attr_rear2_moduleid);
#endif
#endif
#ifdef CAMERA_REAR3
		device_remove_file(camera_rear_dev, &dev_attr_rear3_caminfo);
		device_remove_file(camera_rear_dev, &dev_attr_rear3_camfw);
		device_remove_file(camera_rear_dev, &dev_attr_rear3_camfw_full);
		device_remove_file(camera_rear_dev, &dev_attr_rear3_checkfw_user);
		device_remove_file(camera_rear_dev, &dev_attr_rear3_checkfw_factory);
		device_remove_file(camera_rear_dev, &dev_attr_rear3_sensorid_exif);
		device_remove_file(camera_rear_dev, &dev_attr_rear3_mtf_exif);
		device_remove_file(camera_rear_dev, &dev_attr_rear3_phy_tune);
		device_remove_file(camera_rear_dev, &dev_attr_rear3_paf_cal_check);
#ifdef CAMERA_REAR3_AFCAL
		device_remove_file(camera_rear_dev, &dev_attr_rear3_afcal);
#endif
#ifdef CAMERA_REAR3_TILT
		device_remove_file(camera_rear_dev, &dev_attr_rear3_tilt);
#endif
#ifdef USE_CAMERA_HW_BIG_DATA
		device_remove_file(camera_rear_dev, &dev_attr_rear3_hwparam);
#endif
#ifdef CAMERA_REAR3_MODULEID
		device_remove_file(camera_rear_dev, &dev_attr_rear3_moduleid);
#endif
#endif

#ifdef CAMERA_REAR4
		device_remove_file(camera_rear_dev, &dev_attr_rear4_caminfo);
		device_remove_file(camera_rear_dev, &dev_attr_rear4_camfw);
		device_remove_file(camera_rear_dev, &dev_attr_rear4_camfw_full);
		device_remove_file(camera_rear_dev, &dev_attr_rear4_checkfw_user);
		device_remove_file(camera_rear_dev, &dev_attr_rear4_checkfw_factory);
		device_remove_file(camera_rear_dev, &dev_attr_rear4_sensorid_exif);
		device_remove_file(camera_rear_dev, &dev_attr_rear4_mtf_exif);
		device_remove_file(camera_rear_dev, &dev_attr_rear4_phy_tune);
#ifdef CAMERA_REAR4_AFCAL
		device_remove_file(camera_rear_dev, &dev_attr_rear4_afcal);
		device_remove_file(camera_rear_dev, &dev_attr_rear4_paf_cal_check);
#endif
#ifdef CAMERA_REAR4_TILT
		device_remove_file(camera_rear_dev, &dev_attr_rear4_tilt);
#endif
#ifdef USE_CAMERA_HW_BIG_DATA
		device_remove_file(camera_rear_dev, &dev_attr_rear4_hwparam);
#endif
#ifdef CAMERA_REAR4_MODULEID
		device_remove_file(camera_rear_dev, &dev_attr_rear4_moduleid);
#endif
#endif

		device_remove_file(camera_rear_dev, &dev_attr_rear_sensor_standby);
		device_remove_file(camera_rear_dev, &dev_attr_rear_calcheck);
		device_remove_file(camera_rear_dev, &dev_attr_supported_cameraIds);
#ifdef FORCE_CAL_LOAD
		device_remove_file(camera_rear_dev, &dev_attr_rear_force_cal_load);
#endif


		device_remove_file(camera_rear_dev, &dev_attr_fw_update);
		device_remove_file(camera_rear_dev, &dev_attr_ssrm_camera_info);
#ifdef USE_CAMERA_HW_BIG_DATA
		device_remove_file(camera_rear_dev, &dev_attr_rear_hwparam);
#endif
		device_remove_file(camera_rear_dev, &dev_attr_rear_camfw_all);
		device_remove_file(camera_rear_dev, &dev_attr_rear_paf_offset_mid);
		device_remove_file(camera_rear_dev, &dev_attr_rear_paf_offset_far);
		device_remove_file(camera_rear_dev, &dev_attr_rear_paf_cal_check);
	}

#if defined (CONFIG_OIS_USE)
	if (camera_ois_dev) {
		device_remove_file(camera_ois_dev, &dev_attr_selftest);
		device_remove_file(camera_ois_dev, &dev_attr_ois_power);
		device_remove_file(camera_ois_dev, &dev_attr_ois_set_mode);
		device_remove_file(camera_ois_dev, &dev_attr_autotest);
#ifdef CAMERA_2ND_OIS
		device_remove_file(camera_ois_dev, &dev_attr_ois_gain_rear3);
		device_remove_file(camera_ois_dev, &dev_attr_ois_supperssion_ratio_rear3);
#endif
#ifdef CAMERA_3RD_OIS
		device_remove_file(camera_ois_dev, &dev_attr_ois_gain_rear4);
		device_remove_file(camera_ois_dev, &dev_attr_ois_supperssion_ratio_rear4);
#endif
		device_remove_file(camera_ois_dev, &dev_attr_ois_rawdata);
		device_remove_file(camera_ois_dev, &dev_attr_calibrationtest);
		device_remove_file(camera_ois_dev, &dev_attr_ois_noise_stdev);
		device_remove_file(camera_ois_dev, &dev_attr_ois_hall_position);
		device_remove_file(camera_ois_dev, &dev_attr_oisfw);
		device_remove_file(camera_ois_dev, &dev_attr_ois_diff);
		device_remove_file(camera_ois_dev, &dev_attr_ois_exif);
		device_remove_file(camera_ois_dev, &dev_attr_ois_gain_rear);
		device_remove_file(camera_ois_dev, &dev_attr_ois_supperssion_ratio_rear);
		device_remove_file(camera_ois_dev, &dev_attr_reset_check);
		device_remove_file(camera_ois_dev, &dev_attr_prevent_shaking_noise);
		device_remove_file(camera_ois_dev, &dev_attr_check_cross_talk);
		device_remove_file(camera_ois_dev, &dev_attr_check_hall_cal);
		device_remove_file(camera_ois_dev, &dev_attr_check_ois_valid);
		device_remove_file(camera_ois_dev, &dev_attr_ois_center_shift);
		device_remove_file(camera_ois_dev, &dev_attr_ois_ext_clk);
		device_remove_file(camera_ois_dev, &dev_attr_rear3_read_cross_talk);
	}
#endif

	if (camera_class) {
		if (camera_front_dev)
			device_destroy(camera_class, camera_front_dev->devt);

		if (camera_rear_dev)
			device_destroy(camera_class, camera_rear_dev->devt);

#if defined (CONFIG_OIS_USE)
		if (camera_ois_dev)
			device_destroy(camera_class, camera_ois_dev->devt);
#endif
	}

	class_destroy(camera_class);

	return 0;
}

struct class *is_get_camera_class(void)
{
	return camera_class;
}
EXPORT_SYMBOL_GPL(is_get_camera_class);
