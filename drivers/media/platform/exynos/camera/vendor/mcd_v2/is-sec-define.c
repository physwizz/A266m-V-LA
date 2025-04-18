/*
 * Samsung Exynos5 SoC series FIMC-IS driver
 *
 * exynos5 fimc-is video functions
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "is-sec-define.h"
#include "is-vender.h"
#include "is-vender-specific.h"
#include "is-interface-library.h"

#include <linux/i2c.h>
#include "is-device-eeprom.h"
#include "is-vender-caminfo.h"

#include "is-device-sensor-peri.h"

#define prefix "[FROM]"

#define IS_DEFAULT_CAL_SIZE		(20 * 1024)
#define IS_DUMP_CAL_SIZE			(172 * 1024)
#define IS_LATEST_ROM_VERSION_M	'M'

#define IS_READ_MAX_EEP_CAL_SIZE	(32 * 1024)

#define I2C_READ  4
#define I2C_WRITE 3
#define I2C_BYTE  2
#define I2C_DATA  1
#define I2C_ADDR  0

#define NUM_OF_DUALIZATION_CHECK 6

bool force_caldata_dump = false;

#ifdef USES_STANDARD_CAL_RELOAD
bool sec2lsi_reload = false;
#endif

static int cam_id = CAMERA_SINGLE_REAR;
bool is_dumped_fw_loading_needed = false;
static struct is_rom_info sysfs_finfo[ROM_ID_MAX];
static struct is_rom_info sysfs_pinfo[ROM_ID_MAX];
#if defined(CAMERA_UWIDE_DUALIZED)
static bool rear3_dualized_rom_probe = false;
static struct is_rom_info sysfs_finfo_rear3_otp;
#endif
#if defined(FRONT_OTPROM_EEPROM)
static bool front_dualized_rom_probe = false;
static struct is_rom_info sysfs_finfo_front_otp;
#endif

static char rom_buf[ROM_ID_MAX][IS_MAX_CAL_SIZE];
#if defined(CONFIG_CAMERA_FROM) && defined(CAMERA_MODULE_DUALIZE)
static char fw_buf[IS_MAX_FW_BUFFER_SIZE];
#endif
char loaded_fw[IS_HEADER_VER_SIZE + 1] = {0, };
char loaded_companion_fw[30] = {0, };

#if IS_REACHABLE(CONFIG_CAMERA_OTPROM_SUPPORT_REAR)
static u32 is_check_dualized_sensor[SENSOR_POSITION_MAX] = {false};
#endif

#ifdef CONFIG_SEC_CAL_ENABLE
#ifdef USE_KERNEL_VFS_READ_WRITE
static char *eeprom_cal_dump_path[ROM_ID_MAX] = {
	"dump/eeprom_rear_cal.bin",
	"dump/eeprom_front_cal.bin",
	"dump/eeprom_rear2_cal.bin",
	"dump/eeprom_front2_cal.bin",
	"dump/eeprom_rear3_cal.bin",
	"dump/eeprom_front3_cal.bin",
	"dump/eeprom_rear4_cal.bin",
	"dump/eeprom_front4_cal.bin",
};

static char *otprom_cal_dump_path[ROM_ID_MAX] = {
	"dump/otprom_rear_cal.bin",
	"dump/otprom_front_cal.bin",
	"dump/otprom_rear2_cal.bin",
	"dump/otprom_front2_cal.bin",
	"dump/otprom_rear3_cal.bin",
	"dump/otprom_front3_cal.bin",
	"dump/otprom_rear4_cal.bin",
	"dump/otprom_front4_cal.bin",
};
#endif
#ifdef IS_REAR_MAX_CAL_SIZE
static char cal_buf_rom_data_rear[IS_REAR_MAX_CAL_SIZE];
#endif
#ifdef IS_FRONT_MAX_CAL_SIZE
static char cal_buf_rom_data_front[IS_FRONT_MAX_CAL_SIZE];
#endif
#ifdef IS_REAR2_MAX_CAL_SIZE
static char cal_buf_rom_data_rear2[IS_REAR2_MAX_CAL_SIZE];
#endif
#ifdef IS_FRONT2_MAX_CAL_SIZE
static char cal_buf_rom_data_front2[IS_FRONT2_MAX_CAL_SIZE];
#endif
#ifdef IS_REAR3_MAX_CAL_SIZE
static char cal_buf_rom_data_rear3[IS_REAR3_MAX_CAL_SIZE];
#endif
#ifdef IS_FRONT3_MAX_CAL_SIZE
static char cal_buf_rom_data_front3[IS_FRONT3_MAX_CAL_SIZE];
#endif
#ifdef IS_REAR4_MAX_CAL_SIZE
static char cal_buf_rom_data_rear4[IS_REAR4_MAX_CAL_SIZE];
#endif
#ifdef IS_FRONT4_MAX_CAL_SIZE
static char cal_buf_rom_data_front4[IS_FRONT4_MAX_CAL_SIZE];
#endif

/* cal_buf_rom_data is used for storing original rom data, before standard cal conversion */
static char *cal_buf_rom_data[ROM_ID_MAX] = {

#ifdef IS_REAR_MAX_CAL_SIZE
	cal_buf_rom_data_rear,
#else
	NULL,
#endif
#ifdef IS_FRONT_MAX_CAL_SIZE
	cal_buf_rom_data_front,
#else
	NULL,
#endif
#ifdef IS_REAR2_MAX_CAL_SIZE
	cal_buf_rom_data_rear2,
#else
	NULL,
#endif
#ifdef IS_FRONT2_MAX_CAL_SIZE
	cal_buf_rom_data_front2,
#else
	NULL,
#endif
#ifdef IS_REAR3_MAX_CAL_SIZE
	cal_buf_rom_data_rear3,
#else
	NULL,
#endif
#ifdef IS_FRONT3_MAX_CAL_SIZE
	cal_buf_rom_data_front3,
#else
	NULL,
#endif
#ifdef IS_REAR4_MAX_CAL_SIZE
	cal_buf_rom_data_rear4,
#else
	NULL,
#endif
#ifdef IS_FRONT4_MAX_CAL_SIZE
	cal_buf_rom_data_front4,
#else
	NULL,
#endif
};
#endif

enum {
	CAL_DUMP_STEP_INIT = 0,
	CAL_DUMP_STEP_CHECK,
	CAL_DUMP_STEP_DONE,
};

int check_need_cal_dump = CAL_DUMP_STEP_INIT;

bool is_sec_get_force_caldata_dump(void)
{
	return force_caldata_dump;
}

int is_sec_set_force_caldata_dump(bool fcd)
{
	force_caldata_dump = fcd;
	if (fcd)
		info("forced caldata dump enabled!!\n");
	return 0;
}

int is_sec_set_registers(struct i2c_client *client, const u32 *regs, const u32 size)
{
	int ret = 0;
	int i = 0;

	BUG_ON(!regs);

	for (i = 0; i < size; i += I2C_WRITE) {
		if (regs[i + I2C_BYTE] == I2C_WRITE_FORMAT_ADDR8_DATA8) {
			ret = is_sensor_addr8_write8(client, regs[i + I2C_ADDR], regs[i + I2C_DATA]);
			if (ret < 0) {
				err("is_sensor_addr8_write8 fail, ret(%d), addr(%#x), data(%#x)",
					ret, regs[i + I2C_ADDR], regs[i + I2C_DATA]);
				break;
			}
		} else if (regs[i + I2C_BYTE] == I2C_WRITE_FORMAT_ADDR16_DATA8) {
			ret = is_sensor_write8(client, regs[i + I2C_ADDR], regs[i + I2C_DATA]);
			if (ret < 0) {
				err("is_sensor_write8 fail, ret(%d), addr(%#x), data(%#x)",
					ret, regs[i + I2C_ADDR], regs[i + I2C_DATA]);
				break;
			}
		} else if (regs[i + I2C_BYTE] == I2C_WRITE_FORMAT_ADDR16_DATA16) {
			ret = is_sensor_write16(client, regs[i + I2C_ADDR], regs[i + I2C_DATA]);
			if (ret < 0) {
				err("is_sensor_write16 fail, ret(%d), addr(%#x), data(%#x)",
					ret, regs[i + I2C_ADDR], regs[i + I2C_DATA]);
				break;
			}
		}
	}
	return ret;
}

#if defined(CAMERA_UWIDE_DUALIZED)
void is_sec_set_rear3_dualized_rom_probe(void) {
	rear3_dualized_rom_probe = true;
}
EXPORT_SYMBOL_GPL(is_sec_set_rear3_dualized_rom_probe);
#endif

#if defined(FRONT_OTPROM_EEPROM)
void is_sec_set_front_dualized_rom_probe(void) {
	front_dualized_rom_probe = true;
}
EXPORT_SYMBOL_GPL(is_sec_set_front_dualized_rom_probe);
#endif

int is_sec_get_sysfs_finfo(struct is_rom_info **finfo, int rom_id)
{
#if defined(CAMERA_UWIDE_DUALIZED)
	if(rom_id == ROM_ID_REAR3 && rear3_dualized_rom_probe) {
		*finfo = &sysfs_finfo_rear3_otp;
		rear3_dualized_rom_probe = false;
	}
	else {
		*finfo = &sysfs_finfo[rom_id];
	}
#elif defined(FRONT_OTPROM_EEPROM)
	if(rom_id == ROM_ID_FRONT && front_dualized_rom_probe) {
		*finfo = &sysfs_finfo_front_otp;
		front_dualized_rom_probe = false;
	}
	else {
		*finfo = &sysfs_finfo[rom_id];
	}
#else
	*finfo = &sysfs_finfo[rom_id];
#endif
	return 0;
}
EXPORT_SYMBOL_GPL(is_sec_get_sysfs_finfo);

#ifdef CONFIG_SEC_CAL_ENABLE

#ifdef USES_STANDARD_CAL_RELOAD
bool is_sec_sec2lsi_check_cal_reload(void)
{
	info("%s is_sec_sec2lsi_check_cal_reload=%d\n", __func__, sec2lsi_reload);
	return sec2lsi_reload;
}

void is_sec_sec2lsi_set_cal_reload(bool val)
{
	sec2lsi_reload = val;
}
#endif

int is_sec_get_sysfs_finfo_by_position(int position, struct is_rom_info **finfo)
{
	*finfo = &sysfs_finfo[position];

	if (*finfo == NULL) {
		err("finfo addr is null. postion %d", position);
		/*WARN(true, "finfo is null\n");*/
		return -EINVAL;
	}

	return 0;
}

bool is_sec_readcal_dump_post_sec2lsi(struct is_core *core, char *buf, int position)
{
	int ret = false;
#ifdef USE_KERNEL_VFS_READ_WRITE
	int rom_type = ROM_TYPE_NONE;
	int cal_size = 0;
	bool rom_valid = false;

	struct file *key_fp = NULL;
	struct file *dump_fp = NULL;

	mm_segment_t old_fs;
	loff_t pos = 0;
	struct is_vender_specific *specific = core->vender.private_data;
	struct is_rom_info *finfo;
	struct is_module_enum *module;
	int rom_id = is_vendor_get_rom_id_from_position(position);

	is_sec_get_sysfs_finfo(&finfo, rom_id);

	if (!finfo) {
		err("%s: There is no cal map (rom_id : %d)\n", __func__, rom_id);
		ret = false;
		goto EXIT;
	}

	is_vendor_get_module_from_position(position, &module);

	if (!module) {
		err("%s: There is no module (rom_id : %d)\n", __func__, rom_id);
		ret = false;
		goto EXIT;
	}

	old_fs = get_fs();
	set_fs(KERNEL_DS);

	key_fp = filp_open("/data/vendor/camera/1q2w3e4r.key", O_RDONLY, 0);
	if (IS_ERR(key_fp)) {
		info("KEY does not exist.\n");
		key_fp = NULL;
		goto key_err;
	}

	dump_fp = filp_open("/data/vendor/camera/dump", O_RDONLY, 0);
	if (IS_ERR(dump_fp)) {
		info("dump folder does not exist.\n");
		dump_fp = NULL;
		goto key_err;
	}

	rom_valid = specific->rom_valid[rom_id];
	rom_type = module->pdata->rom_type;
	cal_size = finfo->rom_size;

	if (rom_valid == true) {
		char path[100] = IS_SETFILE_SDCARD_PATH;

		if (rom_type == ROM_TYPE_EEPROM) {
			info("dump folder exist, Dump EEPROM cal data.\n");

			strcat(path, eeprom_cal_dump_path[rom_id]);
			strcat(path, ".post_sec2lsi.bin");
			if (write_data_to_file(path, buf, cal_size, &pos) < 0) {
				info("Failed to rear dump cal data.\n");
				goto dump_err;
			}

			ret = true;
		} else if (rom_type == ROM_TYPE_OTPROM) {
			info("dump folder exist, Dump OTPROM cal data.\n");

			strcat(path, otprom_cal_dump_path[rom_id]);
			strcat(path, ".post_sec2lsi.bin");
			if (write_data_to_file(path, buf, cal_size, &pos) < 0) {
				info("Failed to dump cal data.\n");
				goto dump_err;
			}
			ret = true;
		}
	}

dump_err:
	if (dump_fp)
		filp_close(dump_fp, current->files);

key_err:
	if (key_fp)
		filp_close(key_fp, current->files);


	set_fs(old_fs);

EXIT:
#endif
	return ret;
}

int is_sec_get_max_cal_size(struct is_core *core, int rom_id)
{
	int size = 0;
	struct is_vender_specific *specific = core->vender.private_data;
	struct is_rom_info *finfo = NULL;

	if (!specific->rom_valid[rom_id]) {
		err("Invalid rom_id[%d]. This rom_id don't have rom!\n", rom_id);
		return size;
	}

	is_sec_get_sysfs_finfo(&finfo, rom_id);

	if (finfo == NULL) {
		err("rom_%d: There is no cal map!\n", rom_id);
		return size;
	}

	size = finfo->rom_size;

	if (!size)
		err("Cal size is 0 (rom_id %d). Check cal size!", rom_id);

	return size;
}

bool is_sec_check_awb_lsc_crc32_post_sec2lsi(char *buf, int position, int awb_length, int lsc_length)
{
	u32 *buf32 = NULL;
	u32 checksum, check_base, checksum_base;
	u32 address_boundary;
	int rom_id = is_vendor_get_rom_id_from_position(position);
	bool crc32_check_temp = true;

	struct is_core *core;
	struct is_vender_specific *specific;
	struct is_rom_info *finfo = NULL;
	struct is_rom_info *default_finfo = NULL;
	struct is_module_enum *module = NULL;
	int i = 0;
	int ret = 0;

	bool is_running_camera, is_cal_reload;

	struct rom_standard_cal_data *standard_cal_data;

	core = is_get_is_core();
	specific = core->vender.private_data;
	buf32 = (u32 *)buf;

	is_running_camera = is_vendor_check_camera_running(position);

	info("%s E\n", __func__);

	/***** START CHECK CRC *****/
	is_sec_get_sysfs_finfo(&finfo, rom_id);
	address_boundary = is_sec_get_max_cal_size(core, rom_id);

	standard_cal_data = &(finfo->standard_cal_data);

	/* AWB Cal Data CRC CHECK */

	if (awb_length > 0) {
		checksum = 0;
		check_base = standard_cal_data->rom_awb_start_addr / 4;
		checksum_base = standard_cal_data->rom_awb_section_crc_addr / 4;

#ifdef ROM_CRC32_DEBUG
		printk(KERN_INFO "[CRC32_DEBUG] AWB CRC32 Check. check_length = %d, crc addr = 0x%08X\n",
			awb_length, standard_cal_data->rom_awb_section_crc_addr);
		printk(KERN_INFO "[CRC32_DEBUG] start = 0x%08X, end = 0x%08X\n",
			standard_cal_data->rom_awb_start_addr, standard_cal_data->rom_awb_end_addr);
#endif

		if (check_base > address_boundary || checksum_base > address_boundary) {
			err("Camera[%d]: AWB address has error: start(0x%08X), end(0x%08X)",
				position, standard_cal_data->rom_awb_start_addr, standard_cal_data->rom_awb_end_addr);
			crc32_check_temp = false;
			goto out;
		}

		checksum = (u32)getCRC((u16 *)&buf32[check_base], awb_length, NULL, NULL);
		if (checksum != buf32[checksum_base]) {
			err("Camera[%d]: AWB address has error: start(0x%08X), end(0x%08X) Crc address(0x%08X)",
				position, standard_cal_data->rom_awb_start_addr, standard_cal_data->rom_awb_end_addr,
				standard_cal_data->rom_awb_section_crc_addr);
			err("Camera[%d]: CRC32 error at the AWB (0x%08X != 0x%08X)", position, checksum, buf32[checksum_base]);
			crc32_check_temp = false;
			goto out;
		} else {
			info("%s  AWB CRC is pass! ", __func__);
		}
	} else {
		err("Camera[%d]: Skip to check awb crc32\n", position);
	}

	/* Shading Cal Data CRC CHECK*/
	if (lsc_length > 0) {
		checksum = 0;
		check_base = standard_cal_data->rom_shading_start_addr / 4;
		checksum_base = standard_cal_data->rom_shading_section_crc_addr / 4;

#ifdef ROM_CRC32_DEBUG
		printk(KERN_INFO "[CRC32_DEBUG] Shading CRC32 Check. check_length = %d, crc addr = 0x%08X\n",
			lsc_length, standard_cal_data->rom_shading_section_crc_addr);
		printk(KERN_INFO "[CRC32_DEBUG] start = 0x%08X, end = 0x%08X\n",
			standard_cal_data->rom_shading_start_addr, standard_cal_data->rom_shading_end_addr);
#endif

		if (check_base > address_boundary || checksum_base > address_boundary) {
			err("Camera[%d]: Shading address has error: start(0x%08X), end(0x%08X)",
				position, standard_cal_data->rom_shading_start_addr,
				standard_cal_data->rom_shading_end_addr);
			crc32_check_temp = false;
			goto out;
		}

		checksum = (u32)getCRC((u16 *)&buf32[check_base], lsc_length, NULL, NULL);
		if (checksum != buf32[checksum_base]) {
			err("Camera[%d]: Shading address has error: start(0x%08X), end(0x%08X) Crc address(0x%08X)",
				position, standard_cal_data->rom_shading_start_addr, standard_cal_data->rom_shading_end_addr,
				standard_cal_data->rom_shading_section_crc_addr);
			err("Camera[%d]: CRC32 error at the Shading (0x%08X != 0x%08X)", position, checksum, buf32[checksum_base]);
			crc32_check_temp = false;
			goto out;

		} else {
			info("%s  LSC CRC is pass! ", __func__);
		}

	} else {
		err("Camera[%d]: Skip to check shading crc32\n", position);
	}

out:
	/* Sync DDK Cal with cal_buf during cal reload */
	is_sec_get_sysfs_finfo(&default_finfo, ROM_ID_REAR);
	is_cal_reload = test_bit(IS_ROM_STATE_CAL_RELOAD, &default_finfo->rom_state);
	// todo cal reaload part
	info("%s: Sensor running = %d\n", __func__, is_running_camera);
	if (crc32_check_temp && is_cal_reload == true && is_running_camera == true) {
		for (i = 0; i < SENSOR_POSITION_MAX; i++) {
			is_search_sensor_module_with_position(&core->sensor[i], position, &module);
			if (module)
				break;
		}

		if (!module) {
			err("%s: Could not find sensor id.", __func__);
			crc32_check_temp = false;
			goto out;
		}

		ret =  is_vender_cal_load(&core->vender, module);
		if (ret < 0)
			err("(%s) Unable to sync cal, is_vender_cal_load failed\n", __func__);
	}
	info("%s X\n", __func__);
	return crc32_check_temp;
}
#endif

int is_sec_get_sysfs_pinfo(struct is_rom_info **pinfo, int rom_id)
{
	*pinfo = &sysfs_pinfo[rom_id];
	return 0;
}

int is_sec_get_cal_buf(char **buf, int rom_id)
{
	*buf = rom_buf[rom_id];
	return 0;
}
EXPORT_SYMBOL_GPL(is_sec_get_cal_buf);

#ifdef CONFIG_SEC_CAL_ENABLE
int is_sec_get_cal_buf_rom_data(char **buf, int rom_id)
{
	*buf = cal_buf_rom_data[rom_id];

	if (*buf == NULL) {
		err("cal buf rom data is null. rom_id %d", rom_id);
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(is_sec_get_cal_buf_rom_data);
#endif

int is_sec_get_loaded_fw(char **buf)
{
	*buf = &loaded_fw[0];
	return 0;
}

int is_sec_set_loaded_fw(char *buf)
{
	strncpy(loaded_fw, buf, IS_HEADER_VER_SIZE);
	return 0;
}

int is_sec_set_camid(int id)
{
	cam_id = id;
	return 0;
}

int is_sec_get_camid(void)
{
	return cam_id;
}

int is_sec_fw_revision(char *fw_ver)
{
	int revision = 0;
	revision = revision + ((int)fw_ver[FW_PUB_YEAR] - 58) * 10000;
	revision = revision + ((int)fw_ver[FW_PUB_MON] - 64) * 100;
	revision = revision + ((int)fw_ver[FW_PUB_NUM] - 48) * 10;
	revision = revision + (int)fw_ver[FW_PUB_NUM + 1] - 48;

	return revision;
}

bool is_sec_fw_module_compare(char *fw_ver1, char *fw_ver2)
{
	if (fw_ver1[FW_CORE_VER] != fw_ver2[FW_CORE_VER]
		|| fw_ver1[FW_PIXEL_SIZE] != fw_ver2[FW_PIXEL_SIZE]
		|| fw_ver1[FW_PIXEL_SIZE + 1] != fw_ver2[FW_PIXEL_SIZE + 1]
		|| fw_ver1[FW_ISP_COMPANY] != fw_ver2[FW_ISP_COMPANY]
		|| fw_ver1[FW_SENSOR_MAKER] != fw_ver2[FW_SENSOR_MAKER]) {
		return false;
	}

	return true;
}

bool is_sec_fw_module_compare_for_dump(char *fw_ver1, char *fw_ver2)
{
	if (fw_ver1[FW_CORE_VER] != fw_ver2[FW_CORE_VER]
		|| fw_ver1[FW_PIXEL_SIZE] != fw_ver2[FW_PIXEL_SIZE]
		|| fw_ver1[FW_PIXEL_SIZE + 1] != fw_ver2[FW_PIXEL_SIZE + 1]
		|| fw_ver1[FW_ISP_COMPANY] != fw_ver2[FW_ISP_COMPANY]) {
		return false;
	}

	return true;
}

int is_sec_compare_ver(int rom_id)
{
	struct is_rom_info *finfo = NULL;

	is_sec_get_sysfs_finfo(&finfo, rom_id);

	if (finfo->cal_map_ver[0] == 'V'
		&& finfo->cal_map_ver[1] >= '0' && finfo->cal_map_ver[1] <= '9'
		&& finfo->cal_map_ver[2] >= '0' && finfo->cal_map_ver[2] <= '9'
		&& finfo->cal_map_ver[3] >= '0' && finfo->cal_map_ver[3] <= '9') {
		return ((finfo->cal_map_ver[2] - '0') * 10) + (finfo->cal_map_ver[3] - '0');
	} else {
		err("ROM core version is invalid. version is %c%c%c%c",
			finfo->cal_map_ver[0], finfo->cal_map_ver[1], finfo->cal_map_ver[2], finfo->cal_map_ver[3]);
		return -1;
	}
}

bool is_sec_check_rom_ver(struct is_core *core, int rom_id)
{
	struct is_rom_info *finfo = NULL;
	char compare_version;
	int rom_ver;
	int latest_rom_ver;

	is_sec_get_sysfs_finfo(&finfo, rom_id);

	if (test_bit(IS_ROM_STATE_SKIP_CAL_LOADING, &finfo->rom_state)) {
		err("[rom_id:%d] skip_cal_loading implemented", rom_id);
		return false;
	}

	if (test_bit(IS_ROM_STATE_SKIP_HEADER_LOADING, &finfo->rom_state)) {
		err("[rom_id:%d] skip_header_loading implemented", rom_id);
		return true;
	}

	latest_rom_ver = finfo->cal_map_es_version;
	compare_version = finfo->camera_module_es_version;

	rom_ver = is_sec_compare_ver(rom_id);

	if ((rom_ver < latest_rom_ver && finfo->rom_header_cal_map_ver_start_addr > 0) ||
		(finfo->header_ver[10] < compare_version)) {
		err("[%d]invalid rom version. rom_ver %c, header_ver[10] %c\n",
			rom_id, rom_ver, finfo->header_ver[10]);
		return false;
	} else {
		return true;
	}
}

bool is_sec_check_cal_crc32(char *buf, int rom_id)
{
	u8 *buf8 = NULL;
	u32 checksum;
	u32 check_base;
	u32 check_length;
	u32 checksum_base;
	u32 address_boundary;
	bool crc32_temp, crc32_header_temp, crc32_dual_temp;
	struct is_rom_info *finfo = NULL;
	struct is_core *core;
	struct is_vender_specific *specific;
 	int i;

	core = is_get_is_core();
	specific = core->vender.private_data;
	buf8 = (u8 *)buf;

	info("+++ %s\n", __func__);

	is_sec_get_sysfs_finfo(&finfo, rom_id);
	finfo->crc_error = 0; /* clear all bits */

	if (test_bit(IS_ROM_STATE_SKIP_CRC_CHECK, &finfo->rom_state)) {
		info("%s : skip crc check. return\n", __func__);
		return true;
	}

	crc32_temp = true;
	crc32_header_temp = true;
	crc32_dual_temp = true;

	address_boundary = IS_MAX_CAL_SIZE;

	/* header crc check */
	for (i = 0; i < finfo->header_crc_check_list_len; i += 3) {
		checksum = 0;
		check_base = finfo->header_crc_check_list[i];
		check_length = (finfo->header_crc_check_list[i+1] - finfo->header_crc_check_list[i] + 1) ;
		checksum_base = finfo->header_crc_check_list[i+2];

		if (check_base > address_boundary || checksum_base > address_boundary || check_length <= 0) {
			err("[rom%d/header cal:%d] address has error: start(0x%08X), end(0x%08X)",
				rom_id, i, finfo->header_crc_check_list[i], finfo->header_crc_check_list[i+1]);
			crc32_header_temp = false;
			goto out;
		}

		checksum = (u32)getCRC((u16 *)&buf8[check_base], check_length, NULL, NULL);
		if (checksum != *((u32 *)&buf8[checksum_base])) {
			err("[rom%d/header cal:%d] CRC32 error (0x%08X != 0x%08X), base[0x%X] len[0x%X] checksum[0x%X]",
				rom_id, i, checksum, *((u32 *)&buf8[checksum_base]), check_base, check_length, checksum_base);
			crc32_header_temp = false;
			goto out;
		}
	}

	/* main crc check */
	for (i = 0; i < finfo->crc_check_list_len; i += 3) {
		checksum = 0;
		check_base = finfo->crc_check_list[i];
		check_length = (finfo->crc_check_list[i+1] - finfo->crc_check_list[i] + 1) ;
		checksum_base = finfo->crc_check_list[i+2];

		if (check_base > address_boundary || checksum_base > address_boundary || check_length <= 0) {
			err("[rom%d/main cal:%d] address has error: start(0x%08X), end(0x%08X)",
				rom_id, i, finfo->crc_check_list[i], finfo->crc_check_list[i+1]);
			crc32_temp = false;
			goto out;
		}

		checksum = (u32)getCRC((u16 *)&buf8[check_base], check_length, NULL, NULL);
		if (checksum != *((u32 *)&buf8[checksum_base])) {
			err("[rom%d/main cal:%d] CRC32 error (0x%08X != 0x%08X), base[0x%X] len[0x%X] checksum[0x%X]",
				rom_id, i, checksum, *((u32 *)&buf8[checksum_base]), check_base, check_length, checksum_base);
			crc32_temp = false;
			goto out;
		}
	}

	/* dual crc check */
	for (i = 0; i < finfo->dual_crc_check_list_len; i += 3) {
		checksum = 0;
		check_base = finfo->dual_crc_check_list[i];
		check_length = (finfo->dual_crc_check_list[i+1] - finfo->dual_crc_check_list[i] + 1) ;
		checksum_base = finfo->dual_crc_check_list[i+2];

		if (check_base > address_boundary || checksum_base > address_boundary || check_length <= 0) {
			err("[rom%d/dual cal:%d] data address has error: start(0x%08X), end(0x%08X)",
				rom_id, i, finfo->dual_crc_check_list[i], finfo->dual_crc_check_list[i+1]);
			crc32_temp = false;
			crc32_dual_temp = false;
			goto out;
		}

		checksum = (u32)getCRC((u16 *)&buf8[check_base], check_length, NULL, NULL);
		if (checksum != *((u32 *)&buf8[checksum_base])) {
			err("[rom%d/main cal:%d] CRC32 error (0x%08X != 0x%08X), base[0x%X] len[0x%X] checksum[0x%X]",
				rom_id, i, checksum, *((u32 *)&buf8[checksum_base]), check_base, check_length, checksum_base);
			crc32_temp = false;
			crc32_dual_temp = false;
			goto out;
		}
	}

out:
	if (!crc32_temp)
		set_bit(IS_CRC_ERROR_ALL_SECTION, &finfo->crc_error);
	if (!crc32_header_temp)
		set_bit(IS_CRC_ERROR_HEADER, &finfo->crc_error);
	if (!crc32_dual_temp)
		set_bit(IS_CRC_ERROR_DUAL_CAMERA, &finfo->crc_error);

	info("[rom_id:%d] crc32_check %d crc32_header %d crc32_dual %d\n",
		rom_id, crc32_temp, crc32_header_temp, crc32_dual_temp);

	return crc32_temp && crc32_header_temp;
}

#if defined(CONFIG_CAMERA_FROM) && defined(CAMERA_MODULE_DUALIZE)
bool is_sec_check_fw_crc32(char *buf, u32 checksum_seed, unsigned long size)
{
	u32 *buf32 = NULL;
	u32 checksum;
	u32 checksum_base;

	struct is_rom_info *finfo = NULL;
	is_sec_get_sysfs_finfo(&finfo, ROM_ID_REAR);

	buf32 = (u32 *)buf;

	info("Camera: Start checking CRC32 FW\n");

	checksum = (u32)getCRC((u16 *)&buf32[0], size, NULL, NULL);
	checksum_base = (checksum_seed & 0xffffffff) / 4;
	if (checksum != buf32[checksum_base]) {
		err("Camera: CRC32 error at the binary section (0x%08X != 0x%08X) %x, %x",
					checksum, buf32[checksum_base], checksum_base, checksum_seed);
		set_bit(IS_CRC_ERROR_FIRMWARE, &finfo->crc_error);
	} else {
		clear_bit(IS_CRC_ERROR_FIRMWARE, &finfo->crc_error);
	}

	info("Camera: End checking CRC32 FW\n");

	return !test_bit(IS_CRC_ERROR_FIRMWARE, &finfo->crc_error);
}
#endif

#if defined(CONFIG_CAMERA_FROM) && defined(CAMERA_MODULE_DUALIZE)
bool is_sec_check_setfile_crc32(char *buf)
{
	u32 *buf32 = NULL;
	u32 checksum;
	u32 checksum_base;
	u32 checksum_seed;
	struct is_rom_info *finfo = NULL;

	buf32 = (u32 *)buf;

	is_sec_get_sysfs_finfo(&finfo, ROM_ID_REAR);

	if (finfo->header_ver[FW_SENSOR_MAKER] == FW_SENSOR_MAKER_SLSI ||
		finfo->header_ver[FW_SENSOR_MAKER] == FW_SENSOR_MAKER_SLSI_SONY)
		checksum_seed = CHECKSUM_SEED_SETF_LL;
	else
		checksum_seed = CHECKSUM_SEED_SETF_LS;

	info("Camera: Start checking CRC32 Setfile\n");

	checksum_seed -= finfo->setfile_start_addr;
	checksum = (u32)getCRC((u16 *)&buf32[0], finfo->setfile_size, NULL, NULL);
	checksum_base = (checksum_seed & 0xffffffff) / 4;
	if (checksum != buf32[checksum_base]) {
		err("Camera: CRC32 error at the binary section (0x%08X != 0x%08X) %x %x",
					checksum, buf32[checksum_base], checksum_base, checksum_seed);
		set_bit(IS_CRC_ERROR_SETFILE_1, &finfo->crc_error);
	} else {
		clear_bit(IS_CRC_ERROR_SETFILE_1, &finfo->crc_error);
	}

	info("Camera: End checking CRC32 Setfile\n");

	return !test_bit(IS_CRC_ERROR_SETFILE_1, &finfo->crc_error);
}

#ifdef CAMERA_MODULE_FRONT_SETF_DUMP
bool is_sec_check_front_setfile_crc32(char *buf)
{
	u32 *buf32 = NULL;
	u32 checksum;
	u32 checksum_base;
	u32 checksum_seed;
	struct is_rom_info *finfo = NULL;
	struct is_rom_info *finfo_front = NULL;

	buf32 = (u32 *)buf;

	is_sec_get_sysfs_finfo(&finfo, ROM_ID_REAR);
	is_sec_get_sysfs_finfo(&finfo_front, ROM_ID_FRONT);

	if (finfo_front->header_ver[FW_SENSOR_MAKER] == FW_SENSOR_MAKER_SLSI)
		checksum_seed = CHECKSUM_SEED_FRONT_SETF_LL;
	else
		checksum_seed = CHECKSUM_SEED_FRONT_SETF_LS;

	info("Camera: Start checking CRC32 Front setfile\n");

	checksum_seed -= finfo->front_setfile_start_addr;
	checksum = (u32)getCRC((u16 *)&buf32[0], finfo->front_setfile_size, NULL, NULL);
	checksum_base = (checksum_seed & 0xffffffff) / 4;
	if (checksum != buf32[checksum_base]) {
		err("Camera: CRC32 error at the binary section (0x%08X != 0x%08X) %d %d",
					checksum, buf32[checksum_base], checksum_seed, checksum_base);
		set_bit(IS_CRC_ERROR_SETFILE_2, &finfo->crc_error);
	} else {
		clear_bit(IS_CRC_ERROR_SETFILE_2, &finfo->crc_error);
	}

	info("Camera: End checking CRC32 front setfile\n");

	return !test_bit(IS_CRC_ERROR_SETFILE_1, &finfo->crc_error);
}
#endif
#endif

void remove_dump_fw_file(void)
{
#ifdef USE_KERNEL_VFS_READ_WRITE
	mm_segment_t old_fs;
	long ret;
	char fw_path[100];
	struct is_rom_info *finfo = NULL;
	struct file *fp;
	struct inode *parent_inode;

	old_fs = get_fs();
	set_fs(KERNEL_DS);

	is_sec_get_sysfs_finfo(&finfo, ROM_ID_REAR);

	/* RTA binary */
	snprintf(fw_path, sizeof(fw_path), "%s%s", IS_FW_DUMP_PATH, finfo->load_rta_fw_name);

	fp = filp_open(fw_path, O_RDONLY, 0);

	if (!IS_ERR(fp)) {
		parent_inode= fp->f_path.dentry->d_parent->d_inode;
		inode_lock(parent_inode);
		ret = vfs_unlink(parent_inode, fp->f_path.dentry, NULL);
		inode_unlock(parent_inode);
	} else {
		ret = -ENOENT;
	}

	info("sys_unlink (%s) %ld", fw_path, ret);

	/* DDK binary */
	snprintf(fw_path, sizeof(fw_path), "%s%s", IS_FW_DUMP_PATH, finfo->load_fw_name);

	fp = filp_open(fw_path, O_RDONLY, 0);

	if (!IS_ERR(fp)) {
		parent_inode= fp->f_path.dentry->d_parent->d_inode;
		inode_lock(parent_inode);
		ret = vfs_unlink(parent_inode, fp->f_path.dentry, NULL);
		inode_unlock(parent_inode);
	} else {
		ret = -ENOENT;
	}

	info("sys_unlink (%s) %ld", fw_path, ret);

	set_fs(old_fs);

	is_dumped_fw_loading_needed = false;
#endif
}

ssize_t write_data_to_file(char *name, char *buf, size_t count, loff_t *pos)
{
	ssize_t tx = -ENOENT;
#ifdef USE_KERNEL_VFS_READ_WRITE
	struct file *fp;

	if (force_caldata_dump) {
		fp = filp_open(name, O_RDONLY, 0);

		if (IS_ERR(fp)) {
			fp = filp_open(name, O_WRONLY | O_CREAT | O_TRUNC | O_SYNC, 0666);
		} else {
			struct inode *parent_inode= fp->f_path.dentry->d_parent->d_inode;
			inode_lock(parent_inode);
			vfs_rmdir(parent_inode, fp->f_path.dentry);
			inode_unlock(parent_inode);
			fp = filp_open(name, O_WRONLY | O_CREAT | O_TRUNC | O_SYNC, 0666);
		}
	} else {
		fp = filp_open(name, O_WRONLY | O_CREAT | O_TRUNC | O_SYNC, 0666);
	}

	if (IS_ERR(fp)) {
		err("open file error: %s", name);
		return -EINVAL;
	}

	tx = kernel_write(fp, buf, count, pos);
	if (tx != count) {
		err("fail to write %s. ret %zd", name, tx);
		tx = -ENOENT;
	}
	vfs_fsync(fp, 0);
	fput(fp);

	filp_close(fp, NULL);
#endif
	return tx;
}

ssize_t read_data_rom_file(char *name, char *buf, size_t count, loff_t *pos)
{
#ifdef USE_KERNEL_VFS_READ_WRITE
	struct file *fp;
	ssize_t tx;

	fp = filp_open(name, O_RDONLY, 0);

	if (IS_ERR(fp)) {
		info("%s: error %d, failed to open %s\n", __func__, PTR_ERR(fp), name);
		return -EINVAL;
	}

	tx = kernel_read(fp, buf, count, pos);

	if (tx != count) {
		err("fail to read %s. ret %zd", name, tx);
		tx = -ENOENT;
	}

	fput(fp);
	filp_close(fp, NULL);
#endif
	return count;
}

bool is_sec_file_exist(char *name)
{
#ifdef USE_KERNEL_VFS_READ_WRITE
	struct file *fp;
	mm_segment_t old_fs;
	bool exist = true;

	old_fs = get_fs();
	set_fs(KERNEL_DS);

	fp = filp_open(name, O_RDONLY, 0);

	if (IS_ERR(fp)) {
		exist = false;
		info("%s: error %d, failed to open %s\n", __func__, PTR_ERR(fp), name);
	} else {
		filp_close(fp, NULL);
	}

	set_fs(old_fs);
	return exist;
#endif
	return false;
}

void is_sec_make_crc32_table(u32 *table, u32 id)
{
	u32 i, j, k;

	for (i = 0; i < 256; ++i) {
		k = i;
		for (j = 0; j < 8; ++j) {
			if (k & 1)
				k = (k >> 1) ^ id;
			else
				k >>= 1;
		}
		table[i] = k;
	}
}

/**
 * is_sec_ldo_enabled: check whether the ldo has already been enabled.
 *
 * @ return: true, false or error value
 */
int is_sec_ldo_enabled(struct device *dev, char *name) {
	struct regulator *regulator = NULL;
	int enabled = 0;

	regulator = regulator_get(dev, name);
	if (IS_ERR_OR_NULL(regulator)) {
		err("%s : regulator_get(%s) fail", __func__, name);
		return -EINVAL;
	}

	enabled = regulator_is_enabled(regulator);

	regulator_put(regulator);

	return enabled;
}

int is_sec_ldo_enable(struct device *dev, char *name, bool on)
{
	struct regulator *regulator = NULL;
	int ret = 0;

	regulator = regulator_get(dev, name);
	if (IS_ERR_OR_NULL(regulator)) {
		err("%s : regulator_get(%s) fail", __func__, name);
		return -EINVAL;
	}

	if (on) {
		if (regulator_is_enabled(regulator)) {
			pr_warn("%s: regulator is already enabled\n", name);
			goto exit;
		}

		ret = regulator_enable(regulator);
		if (ret) {
			err("%s : regulator_enable(%s) fail", __func__, name);
			goto exit;
		}
	} else {
		if (!regulator_is_enabled(regulator)) {
			pr_warn("%s: regulator is already disabled\n", name);
			goto exit;
		}

		ret = regulator_disable(regulator);
		if (ret) {
			err("%s : regulator_disable(%s) fail", __func__, name);
			goto exit;
		}
	}

exit:
	regulator_put(regulator);

	return ret;
}

int is_sec_rom_power_on(struct is_core *core, int position)
{
	int ret = 0;
	struct exynos_platform_is_module *module_pdata;
	struct is_module_enum *module = NULL;
	int i = 0;

	info("%s: Sensor position = %d.", __func__, position);

	for (i = 0; i < SENSOR_POSITION_MAX; i++) {
		is_search_sensor_module_with_position(&core->sensor[i], position, &module);
		if (module)
			break;
	}

	if (!module) {
		err("%s: Could not find sensor id.", __func__);
		ret = -EINVAL;
		goto p_err;
	}

	module_pdata = module->pdata;

	if (!module_pdata->gpio_cfg) {
		err("gpio_cfg is NULL");
		ret = -EINVAL;
		goto p_err;
	}

	ret = module_pdata->gpio_cfg(module, SENSOR_SCENARIO_READ_ROM, GPIO_SCENARIO_ON);
	if (ret) {
		err("gpio_cfg is fail(%d)", ret);
		goto p_err;
	}

p_err:
	return ret;
}

int is_sec_rom_power_off(struct is_core *core, int position)
{
	int ret = 0;
	struct exynos_platform_is_module *module_pdata;
	struct is_module_enum *module = NULL;
	int i = 0;

	info("%s: Sensor position = %d.", __func__, position);

	for (i = 0; i < SENSOR_POSITION_MAX; i++) {
		is_search_sensor_module_with_position(&core->sensor[i], position, &module);
		if (module)
			break;
	}

	if (!module) {
		err("%s: Could not find sensor id.", __func__);
		ret = -EINVAL;
		goto p_err;
	}

	module_pdata = module->pdata;

	if (!module_pdata->gpio_cfg) {
		err("gpio_cfg is NULL");
		ret = -EINVAL;
		goto p_err;
	}

	ret = module_pdata->gpio_cfg(module, SENSOR_SCENARIO_READ_ROM, GPIO_SCENARIO_OFF);
	if (ret) {
		err("gpio_cfg is fail(%d)", ret);
		goto p_err;
	}

p_err:
	return ret;
}

int is_sec_check_is_sensor(struct is_core *core, int position, int nextSensorId,
			   bool *bVerified)
{
	int ret;
	struct is_device_sensor_peri *sensor_peri = NULL;
	struct v4l2_subdev *subdev_cis = NULL;
	struct is_vender_specific *specific = core->vender.private_data;
	int i;
	u32 i2c_channel;
	struct exynos_platform_is_module *module_pdata;
	struct is_module_enum *module = NULL;
	const u32 scenario = SENSOR_SCENARIO_NORMAL;

	for (i = 0; i < IS_SENSOR_COUNT; i++) {
		is_search_sensor_module_with_position(&core->sensor[i],
							  position, &module);
		if (module)
			break;
	}

	if (!module) {
		err("%s: Could not find sensor id.", __func__);
		ret = -EINVAL;
		goto p_err;
	}
	module_pdata = module->pdata;
	if (!module_pdata->gpio_cfg) {
		err("gpio_cfg is NULL");
		ret = -EINVAL;
		goto p_err;
	}

	ret = module_pdata->gpio_cfg(module, scenario, GPIO_SCENARIO_ON);
	if (ret) {
		err("gpio_cfg is fail(%d)", ret);
	} else {
		sensor_peri =
			(struct is_device_sensor_peri *)module->private_data;
		if (sensor_peri->subdev_cis) {
			i2c_channel = module_pdata->sensor_i2c_ch;
			if (i2c_channel < SENSOR_CONTROL_I2C_MAX) {
				sensor_peri->cis.i2c_lock =
					&core->i2c_lock[i2c_channel];
			} else {
				warn("%s: wrong cis i2c_channel(%d)", __func__,
					 i2c_channel);
				ret = -EINVAL;
				goto p_err;
			}
		} else {
			err("%s: subdev cis is NULL. dual_sensor check failed",
				__func__);
			ret = -EINVAL;
			goto p_err;
		}

		subdev_cis = sensor_peri->subdev_cis;
		ret = CALL_CISOPS(&sensor_peri->cis, cis_check_rev_on_init,
				  subdev_cis);
		if (ret < 0) {
			*bVerified = false;
			specific->sensor_id[position] = nextSensorId;
			warn("CIS test failed, check the nextSensor");
		} else {
			*bVerified = true;
			specific->sensor_name[position] =
				module->sensor_name;
			info("%s CIS test passed (sensorId[%d] = %d)", __func__,
				 module->device, specific->sensor_id[position]);
		}

		ret = module_pdata->gpio_cfg(module, scenario,
						 GPIO_SCENARIO_OFF);
		if (ret) {
			err("%s gpio_cfg is fail(%d)", __func__, ret);
		}

		/* Requested by HQE to meet the power guidance, add 20ms delay */
#ifdef PUT_30MS_BETWEEN_EACH_CAL_LOADING
		msleep(30);
#else
		msleep(20);
#endif
	}
p_err:
	return ret;
}

#if IS_REACHABLE(CONFIG_CAMERA_OTPROM_SUPPORT_REAR)
static int is_sec_update_dualized_sensor(struct is_core *core, int rom_id) {
	int ret = 0;
	int position, sensorid_1st, sensorid_2nd;
	struct is_vender_specific *specific = core->vender.private_data;

	position = is_vendor_get_position_from_rom_id(rom_id);
	sensorid_1st = specific->sensor_id[position];
	sensorid_2nd = is_vender_get_dualized_sensorid(position);

	if (sensorid_2nd != SENSOR_NAME_NOTHING && !is_check_dualized_sensor[position]) {
		int count_check = NUM_OF_DUALIZATION_CHECK;
		bool bVerified = false;

		while (count_check-- > 0) {
			ret = is_sec_check_is_sensor(core, position,
							 sensorid_2nd, &bVerified);
			if (ret) {
				err("%s: Failed to check corresponding sensor equipped",
					__func__);
				break;
			}

			if (bVerified) {
				is_check_dualized_sensor[position] = true;
				break;
			}

			sensorid_2nd = sensorid_1st;
			sensorid_1st = specific->sensor_id[position];

			if (count_check < NUM_OF_DUALIZATION_CHECK - 2) {
				err("Failed to find out both sensors on this device !!! (count : %d)",
					count_check);
				if (count_check <= 0)
					err("None of camera was equipped (sensorid : %d, %d)",
						sensorid_1st, sensorid_2nd);
			}
		}
	}

	return ret;
}
#endif

void is_sec_check_module_state(struct is_rom_info *finfo)
{
	struct is_core *core = is_get_is_core();
	struct is_vender_specific *specific = core->vender.private_data;

	if (test_bit(IS_ROM_STATE_SKIP_HEADER_LOADING, &finfo->rom_state)) {
		clear_bit(IS_ROM_STATE_OTHER_VENDOR, &finfo->rom_state);
		info("%s : skip header loading. return ", __func__);
		return;
	}

	if (finfo->header_ver[3] == 'L' || finfo->header_ver[3] == 'X'
#ifdef CAMERA_CAL_VERSION_GC5035B
		|| finfo->header_ver[3] == 'Q'
#endif
#ifdef CAMERA_STANDARD_CAL_ISP_VERSION
		|| finfo->header_ver[3] == CAMERA_STANDARD_CAL_ISP_VERSION
#endif
	) {
		clear_bit(IS_ROM_STATE_OTHER_VENDOR, &finfo->rom_state);
	} else {
		set_bit(IS_ROM_STATE_OTHER_VENDOR, &finfo->rom_state);
	}

	if (specific->use_module_check) {
		if (finfo->header_ver[10] >= finfo->camera_module_es_version) {
			set_bit(IS_ROM_STATE_LATEST_MODULE, &finfo->rom_state);
		} else {
			clear_bit(IS_ROM_STATE_LATEST_MODULE, &finfo->rom_state);
		}

		if (finfo->header_ver[10] == IS_LATEST_ROM_VERSION_M) {
			set_bit(IS_ROM_STATE_FINAL_MODULE, &finfo->rom_state);
		} else {
			clear_bit(IS_ROM_STATE_FINAL_MODULE, &finfo->rom_state);
		}
	} else {
		set_bit(IS_ROM_STATE_LATEST_MODULE, &finfo->rom_state);
		set_bit(IS_ROM_STATE_FINAL_MODULE, &finfo->rom_state);
	}
}

#define ADDR_SIZE	2
int is_i2c_read(struct i2c_client *client, void *buf, u32 addr, size_t size)
{
	const u32 max_retry = 2;
	u8 addr_buf[ADDR_SIZE];
	int retries = max_retry;
	int ret = 0;

	if (!client) {
		info("%s: client is null\n", __func__);
		return -ENODEV;
	}

	/* Send addr */
	addr_buf[0] = ((u16)addr) >> 8;
	addr_buf[1] = (u8)addr;

	for (retries = max_retry; retries > 0; retries--) {
		ret = i2c_master_send(client, addr_buf, ADDR_SIZE);
		if (likely(ADDR_SIZE == ret))
			break;

		info("%s: i2c_master_send failed(%d), try %d, addr(%x)\n", __func__, ret, retries, client->addr);
		usleep_range(1000, 1000);
	}

	if (unlikely(ret <= 0)) {
		err("%s: error %d, fail to write 0x%04X", __func__, ret, addr);
		return ret ? ret : -ETIMEDOUT;
	}

	/* Receive data */
	for (retries = max_retry; retries > 0; retries--) {
		ret = i2c_master_recv(client, buf, size);
		if (likely(ret == size))
			break;

		info("%s: i2c_master_recv failed(%d), try %d\n", __func__,  ret, retries);
		usleep_range(1000, 1000);
	}

	if (unlikely(ret <= 0)) {
		err("%s: error %d, fail to read 0x%04X", __func__, ret, addr);
		return ret ? ret : -ETIMEDOUT;
	}

	return 0;
}

#define WRITE_BUF_SIZE	3
int is_i2c_write(struct i2c_client *client, u16 addr, u8 data)
{
	const u32 max_retry = 2;
	u8 write_buf[WRITE_BUF_SIZE];
	int retries = max_retry;
	int ret = 0;

	if (!client) {
		pr_info("%s: client is null\n", __func__);
		return -ENODEV;
	}

	/* Send addr+data */
	write_buf[0] = ((u16)addr) >> 8;
	write_buf[1] = (u8)addr;
	write_buf[2] = data;


	for (retries = max_retry; retries > 0; retries--) {
		ret = i2c_master_send(client, write_buf, WRITE_BUF_SIZE);
		if (likely(WRITE_BUF_SIZE == ret))
			break;

		pr_info("%s: i2c_master_send failed(%d), try %d\n", __func__, ret, retries);
		usleep_range(1000, 1000);
	}

	if (unlikely(ret <= 0)) {
		pr_err("%s: error %d, fail to write 0x%04X\n", __func__, ret, addr);
		return ret ? ret : -ETIMEDOUT;
	}

	return 0;
}

int is_sec_check_reload(struct is_core *core)
{
#ifdef USE_KERNEL_VFS_READ_WRITE
	struct file *reload_key_fp = NULL;
	struct file *supend_resume_key_fp = NULL;
	mm_segment_t old_fs;
	struct is_vender_specific *specific = core->vender.private_data;
	char file_path[100];

	old_fs = get_fs();
	set_fs(KERNEL_DS);

	memset(file_path, 0x00, sizeof(file_path));
	snprintf(file_path, sizeof(file_path), "%sreload/r1e2l3o4a5d.key", IS_FW_DUMP_PATH);
	reload_key_fp = filp_open(file_path, O_RDONLY, 0);
	if (IS_ERR(reload_key_fp)) {
		reload_key_fp = NULL;
	} else {
		info("Reload KEY exist, reload cal data.\n");
		force_caldata_dump = true;
#ifdef USES_STANDARD_CAL_RELOAD
		sec2lsi_reload = true;
#endif
		specific->suspend_resume_disable = true;
	}

	if (reload_key_fp)
		filp_close(reload_key_fp, current->files);

	memset(file_path, 0x00, sizeof(file_path));
	snprintf(file_path, sizeof(file_path), "%si1s2p3s4r.key", IS_FW_DUMP_PATH);
	supend_resume_key_fp = filp_open(file_path, O_RDONLY, 0);
	if (IS_ERR(supend_resume_key_fp)) {
		supend_resume_key_fp = NULL;
	} else {
		info("Supend_resume KEY exist, disable runtime supend/resume. \n");
		specific->suspend_resume_disable = true;
	}

	if (supend_resume_key_fp)
		filp_close(supend_resume_key_fp, current->files);

	set_fs(old_fs);
#endif
	return 0;
}

#ifndef CONFIG_SAMSUNG_PRODUCT_SHIP
void is_sec_cal_dump(struct is_core *core)
{
#ifdef USE_KERNEL_VFS_READ_WRITE
	struct file *key_fp = NULL;
	struct file *dump_fp = NULL;
	mm_segment_t old_fs;
	char file_path[100];
	char *cal_buf;
	struct is_rom_info *finfo = NULL;
	struct is_vender_specific *specific = core->vender.private_data;
	int i;

	loff_t pos = 0;

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	memset(file_path, 0x00, sizeof(file_path));
	snprintf(file_path, sizeof(file_path), "%s1q2w3e4r.key", IS_FW_DUMP_PATH);
	key_fp = filp_open(file_path, O_RDONLY, 0);
	if (IS_ERR(key_fp)) {
		info("KEY does not exist.\n");
		key_fp = NULL;
		goto key_err;
	} else {
		memset(file_path, 0x00, sizeof(file_path));
		snprintf(file_path, sizeof(file_path), "%sdump", IS_FW_DUMP_PATH);
		dump_fp = filp_open(file_path, O_RDONLY, 0);
		if (IS_ERR(dump_fp)) {
			info("dump folder does not exist.\n");
			dump_fp = NULL;
			goto key_err;
		} else {
			info("dump folder exist.\n");
			for (i = 0; i < ROM_ID_MAX; i++) {
				if (specific->rom_valid[i] == true) {
#ifdef CONFIG_SEC_CAL_ENABLE
					if (is_need_use_standard_cal(i) == true)
						is_sec_get_cal_buf_rom_data(&cal_buf, i);
					else
#endif
						is_sec_get_cal_buf(&cal_buf, i);
					is_sec_get_sysfs_finfo(&finfo, i);
					pos = 0;
					info("Dump ROM_ID(%d) cal data.\n", i);
					memset(file_path, 0x00, sizeof(file_path));
					snprintf(file_path, sizeof(file_path), "%sdump/rom_%d_cal.bin", IS_FW_DUMP_PATH, i);
					if (write_data_to_file(file_path, cal_buf, finfo->rom_size, &pos) < 0) {
						info("Failed to dump rom_id(%d) cal data.\n", i);
						goto dump_err;
					}
				}
			}
		}
	}

dump_err:
	if (dump_fp)
		filp_close(dump_fp, current->files);
key_err:
	if (key_fp)
		filp_close(key_fp, current->files);
	set_fs(old_fs);
#endif
}
#endif

int is_sec_parse_rom_info(struct is_rom_info *finfo, char *buf, int rom_id)
{
#if defined(CONFIG_CAMERA_USE_MCU) || defined(CONFIG_CAMERA_USE_INTERNAL_MCU)
	struct is_ois_info *ois_pinfo = NULL;
#endif

	if (finfo->rom_header_version_start_addr != -1) {
		memcpy(finfo->header_ver, &buf[finfo->rom_header_version_start_addr], IS_HEADER_VER_SIZE);
		finfo->header_ver[IS_HEADER_VER_SIZE] = '\0';
	}

	if (finfo->rom_header_sensor2_version_start_addr != -1) {
		memcpy(finfo->header2_ver, &buf[finfo->rom_header_sensor2_version_start_addr], IS_HEADER_VER_SIZE);
		finfo->header2_ver[IS_HEADER_VER_SIZE] = '\0';
	}

	if (finfo->rom_header_project_name_start_addr != -1) {
		memcpy(finfo->project_name, &buf[finfo->rom_header_project_name_start_addr], IS_PROJECT_NAME_SIZE);
		finfo->project_name[IS_PROJECT_NAME_SIZE] = '\0';
	}

	if (finfo->rom_header_module_id_addr != -1) {
		memcpy(finfo->rom_module_id, &buf[finfo->rom_header_module_id_addr], IS_MODULE_ID_SIZE);
		finfo->rom_module_id[IS_MODULE_ID_SIZE] = '\0';
	}

	if (finfo->rom_header_sensor_id_addr != -1) {
		memcpy(finfo->rom_sensor_id, &buf[finfo->rom_header_sensor_id_addr], IS_SENSOR_ID_SIZE);
		finfo->rom_sensor_id[IS_SENSOR_ID_SIZE] = '\0';
	}

	if (finfo->rom_header_sensor2_id_addr != -1) {
		memcpy(finfo->rom_sensor2_id, &buf[finfo->rom_header_sensor2_id_addr], IS_SENSOR_ID_SIZE);
		finfo->rom_sensor2_id[IS_SENSOR_ID_SIZE] = '\0';
	}

	if (finfo->rom_header_cal_map_ver_start_addr != -1) {
		memcpy(finfo->cal_map_ver, &buf[finfo->rom_header_cal_map_ver_start_addr], IS_CAL_MAP_VER_SIZE);
		finfo->cal_map_ver[IS_CAL_MAP_VER_SIZE] = '\0';
	}

#if defined(CONFIG_CAMERA_USE_MCU) || defined(CONFIG_CAMERA_USE_INTERNAL_MCU)
	is_sec_get_ois_pinfo(&ois_pinfo);
	if (finfo->rom_ois_list_len == IS_ROM_OIS_MAX_LIST) {
		if (rom_id == WIDE_OIS_ROM_ID) {
			memcpy(ois_pinfo->wide_romdata.xgg, &buf[finfo->rom_ois_list[0]], IS_OIS_GYRO_DATA_SIZE);
			memcpy(ois_pinfo->wide_romdata.ygg, &buf[finfo->rom_ois_list[1]], IS_OIS_GYRO_DATA_SIZE);
			memcpy(ois_pinfo->wide_romdata.xcoef, &buf[finfo->rom_ois_list[2]], IS_OIS_COEF_DATA_SIZE);
			memcpy(ois_pinfo->wide_romdata.ycoef, &buf[finfo->rom_ois_list[3]], IS_OIS_COEF_DATA_SIZE);
			memcpy(ois_pinfo->wide_romdata.supperssion_xratio, &buf[finfo->rom_ois_list[4]], IS_OIS_SUPPERSSION_RATIO_DATA_SIZE);
			memcpy(ois_pinfo->wide_romdata.supperssion_yratio, &buf[finfo->rom_ois_list[5]], IS_OIS_SUPPERSSION_RATIO_DATA_SIZE);
			memcpy(ois_pinfo->wide_romdata.cal_mark, &buf[finfo->rom_ois_list[6]], IS_OIS_CAL_MARK_DATA_SIZE);
		}
	} else if (finfo->rom_ois_list_len == IS_ROM_OIS_SINGLE_MODULE_MAX_LIST) {
		if (rom_id == WIDE_OIS_ROM_ID) {
			memcpy(ois_pinfo->wide_romdata.xgg, &buf[finfo->rom_ois_list[0]], IS_OIS_GYRO_DATA_SIZE);
			memcpy(ois_pinfo->wide_romdata.ygg, &buf[finfo->rom_ois_list[1]], IS_OIS_GYRO_DATA_SIZE);
			memcpy(ois_pinfo->wide_romdata.xcoef, &buf[finfo->rom_ois_list[2]], IS_OIS_COEF_DATA_SIZE);
			memcpy(ois_pinfo->wide_romdata.ycoef, &buf[finfo->rom_ois_list[3]], IS_OIS_COEF_DATA_SIZE);
			memcpy(ois_pinfo->wide_romdata.supperssion_xratio, &buf[finfo->rom_ois_list[4]], IS_OIS_SUPPERSSION_RATIO_DATA_SIZE);
			memcpy(ois_pinfo->wide_romdata.supperssion_yratio, &buf[finfo->rom_ois_list[5]], IS_OIS_SUPPERSSION_RATIO_DATA_SIZE);
			memcpy(ois_pinfo->wide_romdata.cal_mark, &buf[finfo->rom_ois_list[6]], IS_OIS_CAL_MARK_DATA_SIZE);
		}
	}
#endif

#if 1
	/* debug info dump */
	info("++++ ROM data info - rom_id:%d\n", rom_id);
	info("1. Header info\n");
	info("Module info : %s\n", finfo->header_ver);
	info(" ID : %c\n", finfo->header_ver[FW_CORE_VER]);
	info(" Pixel num : %c%c\n", finfo->header_ver[FW_PIXEL_SIZE],
		finfo->header_ver[FW_PIXEL_SIZE + 1]);
	info(" ISP ID : %c\n", finfo->header_ver[FW_ISP_COMPANY]);
	info(" Sensor Maker : %c\n", finfo->header_ver[FW_SENSOR_MAKER]);
	info(" Year : %c\n", finfo->header_ver[FW_PUB_YEAR]);
	info(" Month : %c\n", finfo->header_ver[FW_PUB_MON]);
	info(" Release num : %c%c\n", finfo->header_ver[FW_PUB_NUM],
		finfo->header_ver[FW_PUB_NUM + 1]);
	info(" Manufacturer ID : %c\n", finfo->header_ver[FW_MODULE_COMPANY]);
	info(" Module ver : %c\n", finfo->header_ver[FW_VERSION_INFO]);
	info(" Project name : %s\n", finfo->project_name);
	info(" Cal data map ver : %s\n", finfo->cal_map_ver);
	info(" MODULE ID : %c%c%c%c%c%02X%02X%02X%02X%02X\n",
		finfo->rom_module_id[0], finfo->rom_module_id[1], finfo->rom_module_id[2],
		finfo->rom_module_id[3], finfo->rom_module_id[4], finfo->rom_module_id[5],
		finfo->rom_module_id[6], finfo->rom_module_id[7], finfo->rom_module_id[8],
		finfo->rom_module_id[9]);
	info("---- ROM data info\n");
#endif
	return 0;
}

int is_sec_read_eeprom_header(int rom_id)
{
	int ret = 0;
	struct is_core *core = is_get_is_core();
	struct is_vender_specific *specific;
	u8 header_version[IS_HEADER_VER_SIZE + 1] = {0, };
	u8 header2_version[IS_HEADER_VER_SIZE + 1] = {0, };
	struct i2c_client *client;
	struct is_rom_info *finfo = NULL;
	struct is_device_eeprom *eeprom;

	specific = core->vender.private_data;
	client = specific->eeprom_client[rom_id];

	eeprom = i2c_get_clientdata(client);

	is_sec_get_sysfs_finfo(&finfo, rom_id);

	if (finfo->rom_header_version_start_addr != -1) {
		ret = is_i2c_read(client, header_version,
					finfo->rom_header_version_start_addr,
					IS_HEADER_VER_SIZE);

		if (unlikely(ret)) {
			err("failed to is_i2c_read for header version (%d)\n", ret);
			ret = -EINVAL;
		}

		memcpy(finfo->header_ver, header_version, IS_HEADER_VER_SIZE);
		finfo->header_ver[IS_HEADER_VER_SIZE] = '\0';
	}

	if (finfo->rom_header_sensor2_version_start_addr != -1) {
		ret = is_i2c_read(client, header2_version,
					finfo->rom_header_sensor2_version_start_addr,
					IS_HEADER_VER_SIZE);

		if (unlikely(ret)) {
			err("failed to is_i2c_read for header version (%d)\n", ret);
			ret = -EINVAL;
		}

		memcpy(finfo->header2_ver, header2_version, IS_HEADER_VER_SIZE);
		finfo->header2_ver[IS_HEADER_VER_SIZE] = '\0';
	}

	return ret;
}

#if IS_REACHABLE(CONFIG_CAMERA_OTPROM_SUPPORT_REAR)
int is_sec_read_otprom_header(int rom_id)
{
	int ret = 0;
	struct is_core *core = is_get_is_core();
	struct is_vender_specific *specific;
	u8 header_version[IS_HEADER_VER_SIZE + 1] = {0, };
	u8 header2_version[IS_HEADER_VER_SIZE + 1] = {0, };
	struct i2c_client *client;
	struct is_rom_info *finfo = NULL;
	struct is_device_otprom *otprom;

	specific = core->vender.private_data;
	client = specific->otprom_client[rom_id];

	otprom = i2c_get_clientdata(client);

	is_sec_get_sysfs_finfo(&finfo, rom_id);

	if (finfo->rom_header_version_start_addr != -1) {
		ret = is_i2c_read(client, header_version,
					finfo->rom_header_version_start_addr,
					IS_HEADER_VER_SIZE);

		if (unlikely(ret)) {
			err("failed to is_i2c_read for header version (%d)\n", ret);
			ret = -EINVAL;
		}

		memcpy(finfo->header_ver, header_version, IS_HEADER_VER_SIZE);
		finfo->header_ver[IS_HEADER_VER_SIZE] = '\0';
	}

	if (finfo->rom_header_sensor2_version_start_addr != -1) {
		ret = is_i2c_read(client, header2_version,
					finfo->rom_header_sensor2_version_start_addr,
					IS_HEADER_VER_SIZE);

		if (unlikely(ret)) {
			err("failed to is_i2c_read for header version (%d)\n", ret);
			ret = -EINVAL;
		}

		memcpy(finfo->header2_ver, header2_version, IS_HEADER_VER_SIZE);
		finfo->header2_ver[IS_HEADER_VER_SIZE] = '\0';
	}

	return ret;
}

int is_sec_readcal_otprom_hi1339(int rom_id)
{
	int ret = 0;
	char *buf = NULL;
	int retry = IS_CAL_RETRY_CNT;
	struct is_core *core = is_get_is_core();
	struct is_rom_info *finfo = NULL;
	struct is_vender_specific *specific = core->vender.private_data;
	int cal_size = 0;
	struct i2c_client *client = NULL;
	bool camera_running;
	int position = is_vendor_get_position_from_rom_id(rom_id);
	u32 read_addr;
	struct v4l2_subdev *subdev_cis = NULL;
	struct is_device_sensor_peri *sensor_peri = NULL;
	struct is_module_enum *module = NULL;
	u32 i2c_channel;
	u8 bank;
	u16 start_addr = 0;
#ifdef CONFIG_SEC_CAL_ENABLE
	char *buf_rom_data = NULL;
#endif

	is_vendor_get_module_from_position(position,&module);
	info("Camera: read cal data from OTPROM (rom_id:%d)\n", rom_id);

	sensor_peri = (struct is_device_sensor_peri *)module->private_data;
	subdev_cis = sensor_peri->subdev_cis;

#if defined(FRONT_OTPROM_EEPROM)
	sysfs_finfo[ROM_ID_FRONT] = sysfs_finfo_front_otp;
#endif
	is_sec_get_sysfs_finfo(&finfo, rom_id);
	is_sec_get_cal_buf(&buf, rom_id);
	client = specific->otprom_client[rom_id];

	cal_size = finfo->rom_size;
	info("%s: rom_id : %d, cal_size :%d\n", __func__, rom_id, cal_size);

	camera_running = is_vendor_check_camera_running(position);
	/* sensor initial settings */
i2c_write_retry_global:
	if (camera_running == false) {
		i2c_channel = module->pdata->sensor_i2c_ch;
		if (i2c_channel < SENSOR_CONTROL_I2C_MAX) {
			sensor_peri->cis.i2c_lock = &core->i2c_lock[i2c_channel];
		} else {
			warn("%s: wrong cis i2c_channel(%d)", __func__, i2c_channel);
			ret = -EINVAL;
			goto exit;
		}
		ret = CALL_CISOPS(&sensor_peri->cis, cis_set_global_setting, subdev_cis);

		if (unlikely(ret)) {
			err("failed to apply global settings (%d)\n", ret);
			if (retry >= 0) {
				retry--;
				msleep(50);
				goto i2c_write_retry_global;
			}
			ret = -EINVAL;
			goto exit;
		}
	}

	ret = CALL_CISOPS(&sensor_peri->cis, cis_mode_change, subdev_cis, 1);
	if (unlikely(ret)) {
		err("failed to apply cis_mode_change (%d)\n", ret);
		ret = -EINVAL;
		goto exit;
	}

	read_addr = 0x0308;
crc_retry:
	is_i2c_write(client, 0x0808, 0x0001); /* stream on */

	//is_i2c_write(client, 0x0B02, 0x01); /* fast standby on */
	//is_i2c_write(client, 0x0809, 0x00); /* stream off */
	is_i2c_write(client, 0x0B00, 0x00); /* stream off */
	usleep_range(10000,10000); /* sleep 10msec */
	is_i2c_write(client, 0x0260, 0x10); /* OTP test mode enable */
	//is_i2c_write(client, 0x0809, 0x01); /* stream on */
	is_i2c_write(client, 0x0b00, 0x01); /* stream on */
	usleep_range(1000,1000); /* sleep 1msec */

	/* read otp bank */
	is_i2c_write(client, 0x030A, ((0x73A) >> 8) & 0xFF); /* upper 16bit */
	is_i2c_write(client, 0x030B, 0x73A & 0xFF); /* lower 16bit */
	is_i2c_write(client, 0x0302, 0x01); /* read mode */

	ret = is_i2c_read(client, &bank, read_addr, 1);
	if (unlikely(ret)) {
		err("failed to read OTP bank address (%d)\n", ret);
		goto exit;
	}

	/* select start address */
	switch (bank) {
	case 0x01 :
		start_addr = HI1339_OTP_START_ADDR_BANK1;
		break;
	case 0x03 :
		start_addr = HI1339_OTP_START_ADDR_BANK2;
		break;
	case 0x07 :
		start_addr = HI1339_OTP_START_ADDR_BANK3;
		break;
	default :
		start_addr = HI1339_OTP_START_ADDR_BANK1;
		break;
	}
	info("%s: otp_bank = %d start_addr = %x\n", __func__, bank, start_addr);

	/* OTP burst read */
	is_i2c_write(client, 0x030A, ((start_addr) >> 8) & 0xFF); /* upper 16bit */
	is_i2c_write(client, 0x030B, start_addr & 0xFF); /* lower 16bit */
	is_i2c_write(client, 0x0302, 0x01); /* read mode */
	is_i2c_write(client, 0x0712, 0x01); /* burst read register on */

	info("Camera: I2C read cal data for rom_id:%d\n",rom_id);
	ret = is_i2c_read(client, &buf[0], read_addr, IS_READ_MAX_HI1339_OTP_CAL_SIZE);
	if (ret) {
		err("failed to is_i2c_read (%d)\n", ret);
		ret = -EINVAL;
		goto exit;
	}
	
	is_sensor_write16(client, 0x0712, 0x00); /* burst read register off */

	if (finfo->rom_header_cal_map_ver_start_addr != -1)
		memcpy(finfo->cal_map_ver, &buf[finfo->rom_header_cal_map_ver_start_addr], IS_CAL_MAP_VER_SIZE);

	if (finfo->rom_header_version_start_addr != -1)
		memcpy(finfo->header_ver, &buf[finfo->rom_header_version_start_addr], IS_HEADER_VER_SIZE);

	if (!is_sec_check_rom_ver(core, rom_id)) {
		info("Camera: Do not read eeprom cal data. EEPROM version is low.\n");
		return -EINVAL;
	}

	is_sec_parse_rom_info(finfo, buf, rom_id);

#ifdef CAMERA_REAR_TOF
	if (rom_id == REAR_TOF_ROM_ID) {
#ifdef REAR_TOF_CHECK_SENSOR_ID
		is_sec_sensorid_find_rear_tof(core);
		if (specific->rear_tof_sensor_id == SENSOR_NAME_IMX316) {
			finfo->rom_tof_cal_size_addr_len = 1;
			if (finfo->cal_map_ver[3] == '1') {
				finfo->crc_check_list[1] = REAR_TOF_IMX316_CRC_ADDR1_MAP001;
			} else {
				finfo->crc_check_list[1] = REAR_TOF_IMX316_CRC_ADDR1_MAP002;
			}
		}
#endif
		is_sec_sensor_find_rear_tof_mode_id(core, buf);
	}
#endif

#ifdef CAMERA_FRONT_TOF
	if (rom_id == FRONT_TOF_ROM_ID) {
		is_sec_sensor_find_front_tof_mode_id(core, buf);
	}
#endif

#ifdef DEBUG_FORCE_DUMP_ENABLE
	{
		char file_path[100];

		loff_t pos = 0;

		memset(file_path, 0x00, sizeof(file_path));
		snprintf(file_path, sizeof(file_path), "%srom%d_dump.bin", IS_FW_DUMP_PATH, rom_id);

		if (write_data_to_file(file_path, buf, cal_size, &pos) < 0) {
			info("Failed to dump cal data. rom_id:%d\n", rom_id);
		}
	}
#endif
	/* CRC check */
	retry = IS_CAL_RETRY_CNT;
	if (!is_sec_check_cal_crc32(buf, rom_id) && (retry > 0)) {
		retry--;
		goto crc_retry;
	}

	is_sec_check_module_state(finfo);

#ifdef USE_CAMERA_NOTIFY_WACOM
	if (!test_bit(IS_CRC_ERROR_HEADER, &finfo->crc_error))
		is_eeprom_info_update(rom_id, finfo->header_ver);
#endif

#ifdef CONFIG_SEC_CAL_ENABLE
	/* Store original rom data before conversion for intrinsic cal */
	if (is_sec_check_cal_crc32(buf, rom_id) == true && is_need_use_standard_cal(rom_id)) {
		is_sec_get_cal_buf_rom_data(&buf_rom_data, rom_id);
		if (buf != NULL && buf_rom_data != NULL)
			memcpy(buf_rom_data, buf, is_sec_get_max_cal_size(core, rom_id));
	}
#endif
exit:
	/* streaming mode change */
	//is_i2c_write(client, 0x0809, 0x00); /* stream off */
	is_i2c_write(client, 0x0b00, 0x00); /* stream off */
	usleep_range(10000, 10000); /* sleep 10msec */
	is_i2c_write(client, 0x0302, 0x00); //OTP r/w mode off
	is_i2c_write(client, 0x0260, 0x00); /* OTP mode off */
	//is_i2c_write(client, 0x0809, 0x01); /* stream on */
	//is_i2c_write(client, 0x0b00, 0x01); /* stream on */
	usleep_range(1000, 1000); /* sleep 1msec */
	return ret;
}


int is_sec_clear_and_initialize_command_4ha(struct i2c_client *client) {
	int ret = 0;
	ret = is_sensor_write8(client, S5K4HA_OTP_R_W_MODE_ADDR, 0x04); /* clear error bit */
	if (ret < 0) {
		err("%s is->ixc_ops->write8 fail, ret(%d), addr(%#x), data(%#x)\n",
			__func__, ret, S5K4HA_OTP_R_W_MODE_ADDR, 0x04);
	}
	msleep(1); /* sleep 1msec */

	ret = is_sensor_write8(client, S5K4HA_OTP_R_W_MODE_ADDR, 0x00); /* initial command */
	if (ret < 0) {
		err("%s is->ixc_ops->write8 fail, ret(%d), addr(%#x), data(%#x)\n",
			__func__, ret, S5K4HA_OTP_R_W_MODE_ADDR, 0x00);
	}

	return ret;
}

int is_sec_write_read_command_4ha(struct i2c_client *client) {
	int ret = 0;
	int retry = IS_CAL_RETRY_CNT;
	u8 read_command_complete_check = 0;

	ret = is_sensor_write8(client, S5K4HA_OTP_R_W_MODE_ADDR, 0x01); /* write "read" command */
	if (unlikely(ret)) {
		err("%s is->ixc_ops->write8 fail, ret(%d), addr(%#x), data(%#x)\n",
			__func__, ret, S5K4HA_OTP_R_W_MODE_ADDR, 0x01);
		goto exit;
	}

	/* confirm write complete */
write_complete_retry:
	msleep(1);

	ret = is_sensor_read8(client, S5K4HA_OTP_CHECK_ADDR, &read_command_complete_check);
	if (unlikely(ret)) {
		err("is->ixc_ops->read8 fail, ret(%d), addr(%#x), retry = %d\n",
			ret, S5K4HA_OTP_CHECK_ADDR, retry);
		goto exit;
	}
	if (read_command_complete_check != 1) {
		if (retry >= 0) {
			retry--;
			goto write_complete_retry;
		}
		ret = -EINVAL;
		goto exit;
	}
exit:
	return ret;
}


u16 is_i2c_select_otp_bank_4ha(struct i2c_client *client) {
	int ret = 0;
	u8 otp_bank = 0;
	u16 curr_page = 0;

	//The Bank details itself is present in bank-1 page-0
	ret = is_sensor_write8(client, S5K4HA_OTP_PAGE_SELECT_ADDR, S5K4HA_OTP_START_PAGE_BANK1);
	if (ret < 0) {
		err("is_sensor_write8 fail, ret(%d), addr(%#x), data(%#x)\n",
			ret, S5K4HA_OTP_PAGE_SELECT_ADDR, S5K4HA_OTP_START_PAGE_BANK1);
		goto exit;
	}	
	ret = is_sensor_read8(client, S5K4HA_OTP_BANK_SELECT, &otp_bank);
	if (unlikely(ret)) {
		err("failed to is_sensor_read8 (%d). OTP Bank selection failed\n", ret);
		goto exit;
	}
	info("%s otp_bank = %d\n", __func__, otp_bank);

	switch(otp_bank) {
	case 0x01 :
		curr_page = S5K4HA_OTP_START_PAGE_BANK1;
		break;
	case 0x03 :
		curr_page = S5K4HA_OTP_START_PAGE_BANK2;
		break;
	case 0x07 :
		curr_page = S5K4HA_OTP_START_PAGE_BANK3;
		break;
	case 0x0F :
		curr_page = S5K4HA_OTP_START_PAGE_BANK4;
		break;
	case 0x1F :
		curr_page = S5K4HA_OTP_START_PAGE_BANK5;
		break;
	default :
		curr_page = S5K4HA_OTP_START_PAGE_BANK1;
		break;
	}

exit:
	return curr_page;
}

int is_sec_i2c_read_otp_4ha(struct i2c_client *client, char *buf, u16 start_addr, size_t size)
{
	int ret = 0;
	int index = 0;
	u16 curr_addr = start_addr;
	u16 curr_page;

	curr_page = is_i2c_select_otp_bank_4ha(client);

	is_sec_clear_and_initialize_command_4ha(client);
	is_sec_write_read_command_4ha(client);
	ret = is_sensor_write8(client, S5K4HA_OTP_PAGE_SELECT_ADDR, curr_page);
	if (ret < 0) {
		err("is->ixc_ops->write8 fail, ret(%d), addr(%#x), data(%#x)\n",
			S5K4HA_OTP_PAGE_SELECT_ADDR, curr_page);
		goto exit;
	}

	for (index = 0; index < size ; index++) {
		ret = is_sensor_read8(client, curr_addr, &buf[index]); /* OTP read */
		if (unlikely(ret)) {
			err("failed to is_sensor_read8 (%d)\n", ret);
			goto exit;
		}
		curr_addr++;
		if (curr_addr > S5K4HA_OTP_PAGE_ADDR_H) {
			curr_addr = S5K4HA_OTP_PAGE_ADDR_L;
			curr_page++;
			is_sec_clear_and_initialize_command_4ha(client);
			is_sec_write_read_command_4ha(client);
			ret = is_sensor_write8(client, S5K4HA_OTP_PAGE_SELECT_ADDR, curr_page);
			if (ret < 0) {
				err("is_sensor_write8, ret(%d), addr(%#x), data(%#x)\n",
					ret, S5K4HA_OTP_PAGE_SELECT_ADDR, curr_page);
				goto exit;
			}
		}
	}
exit:
	return ret;
}

int is_sec_readcal_otprom_4ha(int rom_id)
{
	int ret = 0;
	int retry = IS_CAL_RETRY_CNT;
	char *buf = NULL;
	//u8 read_command_complete_check = 0;
	bool camera_running;

	struct is_core *core = is_get_is_core();
	struct is_vender_specific *specific = core->vender.private_data;
	struct is_rom_info *finfo = NULL;
	struct i2c_client *client = NULL;
	struct v4l2_subdev *subdev_cis = NULL;
	struct is_device_sensor_peri *sensor_peri = NULL;
	struct is_module_enum *module = NULL;
	int position = is_vendor_get_position_from_rom_id(rom_id);

	is_vendor_get_module_from_position(position,&module);
	info("Camera: read cal data from OTPROM (rom_id:%d)\n", rom_id);

	sensor_peri = (struct is_device_sensor_peri *)module->private_data;
	subdev_cis = sensor_peri->subdev_cis;
	is_sec_get_sysfs_finfo(&finfo, rom_id);
	is_sec_get_cal_buf(&buf, rom_id);
	client = specific->otprom_client[rom_id];

	camera_running = is_vendor_check_camera_running(finfo->rom_power_position);

	/* Sensor Initial Settings (Global) */
i2c_write_retry_global:
	if (camera_running == false) {
		ret = is_sec_set_registers(client, sensor_otp_4ha_global, sensor_otp_4ha_global_size);
		if (unlikely(ret)) {
			err("failed to apply otprom global settings (%d)\n", ret);
			if (retry >= 0) {
				retry--;
				msleep(50);
				goto i2c_write_retry_global;
			}
			ret = -EINVAL;
			goto exit;
		}
	}

	ret = is_sensor_write8(client, S5K4HA_STREAM_ON_ADDR, 0x01); /* streaming on */
	if (ret < 0) {
		err("is_sensor_write8 fail, ret(%d), addr(%#x), data(%#x)\n",
			ret, S5K4HA_STREAM_ON_ADDR, 0x01);
		return ret;
	}
	msleep(50); /* sleep 50msec */

	ret = is_sec_write_read_command_4ha(client);
	if (unlikely(ret)) {
		err("failed to write \"read\" OTP 4HA (%d)\n", ret);
		goto exit;
	}

crc_retry:
	/* Read OTP Cal Data */
	info("I2C read cal data\n");
	ret = is_sec_i2c_read_otp_4ha(client, buf, S5K4HA_OTP_START_ADDR, S5K4HA_OTP_USED_CAL_SIZE);
	if (unlikely(ret)) {
		err("failed to read otp 4ha (%d)\n", ret);
		ret = -EINVAL;
		goto exit;
	}

	/* Parse Cal */
	retry = IS_CAL_RETRY_CNT;
	ret = is_sec_read_otprom_header(rom_id);
	if (unlikely(ret)) {
		if (retry >= 0) {
			retry--;
			goto crc_retry;
		}
		err("OTPROM CRC failed, retry %d", retry);
	}
	info("OTPROM CRC passed\n");
	is_sec_parse_rom_info(finfo, buf, rom_id);

exit:
	/* streaming mode change */
	is_sec_clear_and_initialize_command_4ha(client);

	ret = is_sensor_write8(client, S5K4HA_STREAM_ON_ADDR, 0x00); /* streaming off */
	if (ret < 0) {
		err("failed to turn off streaming");
		return ret;
	}
	
	info("%s X\n", __func__);
	return ret;
}

#if defined(CAMERA_UWIDE_DUALIZED)
int is_sec_readcal_otprom_hi1336(int rom_id)
{
	int ret = 0;
	char *buf = NULL;
	int retry = IS_CAL_RETRY_CNT;
	struct is_core *core = is_get_is_core();
	struct is_rom_info *finfo = NULL;
	struct is_vender_specific *specific = core->vender.private_data;
	int cal_size = 0;
	struct i2c_client *client = NULL;
	bool camera_running;
	int position = is_vendor_get_position_from_rom_id(rom_id);
	u32 read_addr;
	struct v4l2_subdev *subdev_cis = NULL;
	struct is_device_sensor_peri *sensor_peri = NULL;
	struct is_module_enum *module = NULL;
	u32 i2c_channel;
	u8 bank;
	u16 start_addr = 0;
#ifdef CONFIG_SEC_CAL_ENABLE
	char *buf_rom_data = NULL;
#endif

	is_vendor_get_module_from_position(position,&module);
	info("Camera: read cal data from OTPROM (rom_id:%d)\n", rom_id);

	sensor_peri = (struct is_device_sensor_peri *)module->private_data;
	subdev_cis = sensor_peri->subdev_cis;

	is_sec_get_sysfs_finfo(&finfo, rom_id);
	is_sec_get_cal_buf(&buf, rom_id);
	client = specific->otprom_client[rom_id];

	cal_size = finfo->rom_size;
	info("%s: rom_id : %d, cal_size :%d\n", __func__, rom_id, cal_size);

	camera_running = is_vendor_check_camera_running(position);
	/* sensor initial settings */
i2c_write_retry_global:
	if (camera_running == false) {
		i2c_channel = module->pdata->sensor_i2c_ch;
		if (i2c_channel < SENSOR_CONTROL_I2C_MAX) {
			sensor_peri->cis.i2c_lock = &core->i2c_lock[i2c_channel];
		} else {
			warn("%s: wrong cis i2c_channel(%d)", __func__, i2c_channel);
			ret = -EINVAL;
			goto exit;
		}
		ret = CALL_CISOPS(&sensor_peri->cis, cis_set_global_setting, subdev_cis);

		if (unlikely(ret)) {
			err("failed to apply global settings (%d)\n", ret);
			if (retry >= 0) {
				retry--;
				msleep(50);
				goto i2c_write_retry_global;
			}
			ret = -EINVAL;
			goto exit;
		}
	}

	ret = CALL_CISOPS(&sensor_peri->cis, cis_mode_change, subdev_cis, 1);
	if (unlikely(ret)) {
		err("failed to apply cis_mode_change (%d)\n", ret);
		ret = -EINVAL;
		goto exit;
	}

	read_addr = 0x0308;
crc_retry:
	is_i2c_write(client, 0x0808, 0x0001); /* stream on */

	is_i2c_write(client, 0x0B02, 0x01); /* fast standby on */
	is_i2c_write(client, 0x0809, 0x00); /* stream off */
	is_i2c_write(client, 0x0B00, 0x00); /* stream off */
	usleep_range(10000,10000); /* sleep 10msec */
	is_i2c_write(client, 0x0260, 0x10); /* OTP test mode enable */
	is_i2c_write(client, 0x0809, 0x01); /* stream on */
	is_i2c_write(client, 0x0b00, 0x01); /* stream on */
	usleep_range(1000,1000); /* sleep 1msec */

	/* read otp bank */
	is_i2c_write(client, 0x030A, ((0x400) >> 8) & 0xFF); /* upper 16bit */
	is_i2c_write(client, 0x030B, 0x400 & 0xFF); /* lower 16bit */
	is_i2c_write(client, 0x0302, 0x01); /* read mode */

	ret = is_i2c_read(client, &bank, read_addr, 1);
	if (unlikely(ret)) {
		err("failed to read OTP bank address (%d)\n", ret);
		goto exit;
	}

	/* select start address */
	switch (bank) {
	case 0x01 :
		start_addr = HI1336_OTP_START_ADDR_BANK1;
		break;
	case 0x03 :
		start_addr = HI1336_OTP_START_ADDR_BANK2;
		break;
	case 0x07 :
		start_addr = HI1336_OTP_START_ADDR_BANK3;
		break;
	case 0x0F :
		start_addr = HI1336_OTP_START_ADDR_BANK4;
		break;
	default :
		start_addr = HI1336_OTP_START_ADDR_BANK1;
		break;
	}
	info("%s: otp_bank = %d start_addr = %x\n", __func__, bank, start_addr);

	/* OTP burst read */
	is_i2c_write(client, 0x030A, ((start_addr) >> 8) & 0xFF); /* upper 16bit */
	is_i2c_write(client, 0x030B, start_addr & 0xFF); /* lower 16bit */
	is_i2c_write(client, 0x0302, 0x01); /* read mode */
	is_i2c_write(client, 0x0712, 0x01); /* burst read register on */

	info("Camera: I2C read cal data for rom_id:%d\n",rom_id);
	ret = is_i2c_read(client, &buf[0], read_addr, IS_READ_MAX_HI1336_OTP_CAL_SIZE);
	if (ret) {
		err("failed to is_i2c_read (%d)\n", ret);
		ret = -EINVAL;
		goto exit;
	}
	
	is_sensor_write16(client, 0x0712, 0x00); /* burst read register off */

	if (finfo->rom_header_cal_map_ver_start_addr != -1)
		memcpy(finfo->cal_map_ver, &buf[finfo->rom_header_cal_map_ver_start_addr], IS_CAL_MAP_VER_SIZE);

	if (finfo->rom_header_version_start_addr != -1)
		memcpy(finfo->header_ver, &buf[finfo->rom_header_version_start_addr], IS_HEADER_VER_SIZE);

	if (!is_sec_check_rom_ver(core, rom_id)) {
		info("Camera: Do not read eeprom cal data. EEPROM version is low.\n");
		return -EINVAL;
	}

	is_sec_parse_rom_info(finfo, buf, rom_id);

#ifdef CAMERA_REAR_TOF
	if (rom_id == REAR_TOF_ROM_ID) {
#ifdef REAR_TOF_CHECK_SENSOR_ID
		is_sec_sensorid_find_rear_tof(core);
		if (specific->rear_tof_sensor_id == SENSOR_NAME_IMX316) {
			finfo->rom_tof_cal_size_addr_len = 1;
			if (finfo->cal_map_ver[3] == '1') {
				finfo->crc_check_list[1] = REAR_TOF_IMX316_CRC_ADDR1_MAP001;
			} else {
				finfo->crc_check_list[1] = REAR_TOF_IMX316_CRC_ADDR1_MAP002;
			}
		}
#endif
		is_sec_sensor_find_rear_tof_mode_id(core, buf);
	}
#endif

#ifdef CAMERA_FRONT_TOF
	if (rom_id == FRONT_TOF_ROM_ID) {
		is_sec_sensor_find_front_tof_mode_id(core, buf);
	}
#endif

#ifdef DEBUG_FORCE_DUMP_ENABLE
	{
		char file_path[100];

		loff_t pos = 0;

		memset(file_path, 0x00, sizeof(file_path));
		snprintf(file_path, sizeof(file_path), "%srom%d_dump.bin", IS_FW_DUMP_PATH, rom_id);

		if (write_data_to_file(file_path, buf, cal_size, &pos) < 0) {
			info("Failed to dump cal data. rom_id:%d\n", rom_id);
		}
	}
#endif
	/* CRC check */
	retry = IS_CAL_RETRY_CNT;
	if (!is_sec_check_cal_crc32(buf, rom_id) && (retry > 0)) {
		retry--;
		goto crc_retry;
	}

	is_sec_check_module_state(finfo);

#ifdef USE_CAMERA_NOTIFY_WACOM
	if (!test_bit(IS_CRC_ERROR_HEADER, &finfo->crc_error))
		is_eeprom_info_update(rom_id, finfo->header_ver);
#endif

#ifdef CONFIG_SEC_CAL_ENABLE
	/* Store original rom data before conversion for intrinsic cal */
	if (is_sec_check_cal_crc32(buf, rom_id) == true && is_need_use_standard_cal(rom_id)) {
		is_sec_get_cal_buf_rom_data(&buf_rom_data, rom_id);
		if (buf != NULL && buf_rom_data != NULL)
			memcpy(buf_rom_data, buf, is_sec_get_max_cal_size(core, rom_id));
	}
#endif
exit:
	/* streaming mode change */
	is_i2c_write(client, 0x0809, 0x00); /* stream off */
	is_i2c_write(client, 0x0b00, 0x00); /* stream off */
	usleep_range(10000, 10000); /* sleep 10msec */
	is_i2c_write(client, 0x0260, 0x00); /* OTP mode display */
	is_i2c_write(client, 0x0809, 0x01); /* stream on */
	is_i2c_write(client, 0x0b00, 0x01); /* stream on */
	usleep_range(1000, 1000); /* sleep 1msec */
	return ret;
}
#endif /* CAMERA_UWIDE_DUALIZED */

int is_sec_i2c_read_otp_gc5035(struct i2c_client *client, char *buf, u16 start_addr, size_t size)   
{
	u16 curr_addr = start_addr; 
	u8 start_addr_h = 0;
	u8 start_addr_l = 0;
	u8 busy_flag = 0;
	int retry = 8;
	int ret = 0;
	int index;

	for (index = 0; index < size; index++)
	{
		start_addr_h = ((curr_addr>>8) & 0xFF);
		start_addr_l = (curr_addr & 0xFF);
		ret = is_sensor_addr8_write8(client, GC5035_OTP_PAGE_ADDR, GC5035_OTP_PAGE);
		ret |= is_sensor_addr8_write8(client, GC5035_OTP_ACCESS_ADDR_HIGH, start_addr_h);
		ret |= is_sensor_addr8_write8(client, GC5035_OTP_ACCESS_ADDR_LOW, start_addr_l);
		ret |= is_sensor_addr8_write8(client, GC5035_OTP_MODE_ADDR, 0x20);
		if (unlikely(ret))
		{
			err("failed to is_sensor_addr8_write8 (%d)\n", ret);
			goto exit;
		}

		ret = is_sensor_addr8_read8(client, GC5035_OTP_BUSY_ADDR, &busy_flag);
		if (unlikely(ret))
		{
			err("failed to is_sensor_addr8_read8 (%d)\n", ret);
			goto exit;
		}

		while ((busy_flag & 0x2) > 0 && retry > 0) {
			ret = is_sensor_addr8_read8(client, GC5035_OTP_BUSY_ADDR, &busy_flag);
			if (unlikely(ret))
			{
				err("failed to is_sensor_addr8_read8 (%d)\n", ret);
				goto exit;
			}
			retry--;
			msleep(1);
		}

		if ((busy_flag & 0x1))
		{
			err("Sensor OTP_check_flag failed\n");
			goto exit;
		}

		ret = is_sensor_addr8_read8(client, GC5035_OTP_READ_ADDR, &buf[index]);
		if (unlikely(ret))
		{
			err("failed to is_sensor_addr8_read8 (%d)\n", ret);
			goto exit;
		}
		curr_addr += 8;
	}

exit:
	return ret;
}

int is_sec_readcal_otprom_gc5035(int rom_id)
{
	int ret = 0;
	int retry = IS_CAL_RETRY_CNT;
	char *buf = NULL;
	u8 otp_bank = 0;
	u16 start_addr = 0;
	u8 busy_flag = 0;
	bool camera_running;
	u16 bank1_addr = 0;
	u16 bank2_addr = 0;
	u16 cal_size = 0;

	struct is_core *core = is_get_is_core();
	struct is_vender_specific *specific = core->vender.private_data;
	struct is_rom_info *finfo = NULL;
	struct i2c_client *client = NULL;
	struct v4l2_subdev *subdev_cis = NULL;
	struct is_device_sensor_peri *sensor_peri = NULL;
	struct is_module_enum *module = NULL;
	u32 i2c_channel;
	int position = is_vendor_get_position_from_rom_id(rom_id);

	if(rom_id == 1) {
		bank1_addr = GC5035_FRONT_OTP_START_ADDR_BANK1;
		bank2_addr = GC5035_FRONT_OTP_START_ADDR_BANK2;
		cal_size = GC5035_FRONT_OTP_USED_CAL_SIZE;
	}
	else if(rom_id == 2) {
		bank1_addr = GC5035_BOKEH_OTP_START_ADDR_BANK1;
		bank2_addr = GC5035_BOKEH_OTP_START_ADDR_BANK2;
		cal_size = GC5035_BOKEH_OTP_USED_CAL_SIZE;
	}
	else if(rom_id == 4) {
		bank1_addr = GC5035_UW_OTP_START_ADDR_BANK1;
		bank2_addr = GC5035_UW_OTP_START_ADDR_BANK2;
		cal_size = GC5035_UW_OTP_USED_CAL_SIZE;
	}
	else if(rom_id == 6) {
		bank1_addr = GC5035_MACRO_OTP_START_ADDR_BANK1;
		bank2_addr = GC5035_MACRO_OTP_START_ADDR_BANK2;
		cal_size = GC5035_MACRO_OTP_USED_CAL_SIZE;
	}

	is_vendor_get_module_from_position(position,&module);
	info("Camera: read cal data from OTPROM (rom_id:%d)\n", rom_id);

	sensor_peri = (struct is_device_sensor_peri *)module->private_data;
	subdev_cis = sensor_peri->subdev_cis;
	is_sec_get_sysfs_finfo(&finfo, rom_id);
	is_sec_get_cal_buf(&buf, rom_id);
	client = specific->otprom_client[rom_id];

	camera_running = is_vendor_check_camera_running(finfo->rom_power_position);
i2c_write_retry_global:
	if (camera_running == false) {
		i2c_channel = module->pdata->sensor_i2c_ch;
		if (i2c_channel < SENSOR_CONTROL_I2C_MAX) {
			sensor_peri->cis.i2c_lock = &core->i2c_lock[i2c_channel];
		} else {
			warn("%s: wrong cis i2c_channel(%d)", __func__, i2c_channel);
			ret = -EINVAL;
			goto exit;
		}
		ret = is_sec_set_registers(client, sensor_gc5035_setfile_otp_global, ARRAY_SIZE(sensor_gc5035_setfile_otp_global));
		if (unlikely(ret)) {
			err("failed to apply otprom global settings (%d)\n", ret);
			if (retry >= 0) {
				retry--;
				msleep(50);
				goto i2c_write_retry_global;
			}
			ret = -EINVAL;
			goto exit;
		}
	}

	ret = is_sec_set_registers(client, sensor_gc5035_setfile_otp_init, ARRAY_SIZE(sensor_gc5035_setfile_otp_init));
	if (unlikely(ret)) {
		err("failed to apply otprom init setting (%d)\n", ret);
		ret = -EINVAL;
		goto exit;
	}

	/* Read OTP page */
	ret = is_sensor_addr8_write8(client, GC5035_OTP_PAGE_ADDR, GC5035_OTP_PAGE);
	ret |= is_sensor_addr8_write8(client, GC5035_OTP_ACCESS_ADDR_HIGH, (GC5035_BANK_SELECT_ADDR >> 8) & 0x1F);
	ret |= is_sensor_addr8_write8(client, GC5035_OTP_ACCESS_ADDR_LOW, GC5035_BANK_SELECT_ADDR & 0xFF);
	ret |= is_sensor_addr8_write8(client, GC5035_OTP_MODE_ADDR, 0x20);
	if (unlikely(ret))
	{
		err("failed to is_sensor_addr8_write8 (%d)\n", ret);
		ret = -EINVAL;
		goto exit;
	}
	ret = is_sensor_addr8_read8(client, GC5035_OTP_BUSY_ADDR, &busy_flag);
	if (unlikely(ret))
	{
		err("failed to is_sensor_addr8_read8 (%d)\n", ret);
		ret = -EINVAL;
		goto exit;
	}

	retry = IS_CAL_RETRY_CNT;

	while ((busy_flag & 0x2) > 0 && retry > 0) {
		ret = is_sensor_addr8_read8(client, GC5035_OTP_BUSY_ADDR, &busy_flag);
		if (unlikely(ret))
		{
			err("failed to is_sensor_addr8_read8 (%d)\n", ret);
			ret = -EINVAL;
			goto exit;
		}
		retry--;
		msleep(1);
	}

	if ((busy_flag & 0x1))
	{
		err("Sensor OTP_check_flag failed\n");
		goto exit;
	}

	is_sensor_addr8_read8(client, GC5035_OTP_READ_ADDR, &otp_bank);
	if (unlikely(ret))
	{
		err("failed to is_sensor_addr8_read8 (%d)\n", ret);
		ret = -EINVAL;
		goto exit;
	}

	/* select start address */
	switch(otp_bank) {
	case 0x01 :
		start_addr = bank1_addr;
		break;
	case 0x03 :
		start_addr = bank2_addr;
		break;
	default :
		start_addr = bank1_addr;
		break;
	}

	info("%s: otp_bank = %d otp_start_addr = %x\n", __func__, otp_bank, start_addr);

crc_retry:
	/* read cal data */
	info("I2C read cal data\n");
	ret = is_sec_i2c_read_otp_gc5035(client, buf, start_addr, cal_size);
	if (unlikely(ret)) {
		err("failed to read otp gc5035 (%d)\n", ret);
		ret = -EINVAL;
		goto exit;
	}

	ret = is_sec_read_otprom_header(rom_id);
	if (unlikely(ret)) {
		if (retry >= 0) {
			retry--;
			goto crc_retry;
		}
		err("OTPROM CRC failed, retry %d", retry);
	}
	info("OTPROM CRC passed\n");

#ifdef DEBUG_FORCE_DUMP_ENABLE
	{
		char file_path[100];

		loff_t pos = 0;

		memset(file_path, 0x00, sizeof(file_path));
		snprintf(file_path, sizeof(file_path), "%srom%d_dump.bin", FIMC_IS_FW_DUMP_PATH, rom_id);

		if (write_data_to_file(file_path, buf, finfo->rom_size, &pos) < 0) {
			info("Failed to dump cal data. rom_id:%d\n", rom_id);
		}
	}
#endif
	is_sec_parse_rom_info(finfo, buf, rom_id);

	/* CRC check */
	if (!is_sec_check_cal_crc32(buf, rom_id) && (retry > 0)) {
		err("OTP CRC failed (retry:%d)\n", retry);
		retry--;
		goto crc_retry;
	}

exit:
	return ret;
}

int is_sec_i2c_read_otp_gc02m1(struct i2c_client *client, char *buf, u16 start_addr, size_t size)
{
	u16 curr_addr = start_addr;
	int ret = 0;
	int index;

	for(index = 0; index < size; index++) {
		ret = is_sensor_addr8_write8(client, GC02M1_OTP_PAGE_ADDR, GC02M1_OTP_PAGE);
		ret |= is_sensor_addr8_write8(client, GC02M1_OTP_ACCESS_ADDR, curr_addr);
		ret |= is_sensor_addr8_write8(client, GC02M1_OTP_MODE_ADDR, 0x34); // Read Pulse
		if (unlikely(ret))
		{
			err("failed to is_sensor_addr8_write8 (%d)\n", ret);
			goto exit;
		}

		ret = is_sensor_addr8_read8(client, GC02M1_OTP_READ_ADDR, &buf[index]);
		if (unlikely(ret)) {
			err("failed to is_sensor_addr8_read8 (%d)\n", ret);
			goto exit;
		}
		curr_addr += 8;
	}
exit:
	return ret;
}

int is_sec_readcal_otprom_gc02m1(int rom_id)
{
	int ret = 0;
	int retry = IS_CAL_RETRY_CNT;
	char *buf = NULL;
	bool camera_running;

	struct is_core *core = is_get_is_core();
	struct is_vender_specific *specific = core->vender.private_data;
	struct is_rom_info *finfo = NULL;
	struct i2c_client *client = NULL;
	struct v4l2_subdev *subdev_cis = NULL;
	struct is_device_sensor_peri *sensor_peri = NULL;
	struct is_module_enum *module = NULL;
	u32 i2c_channel;
	int position = is_vendor_get_position_from_rom_id(rom_id);

	is_vendor_get_module_from_position(position,&module);
	info("Camera: read cal data from OTPROM (rom_id:%d)\n", rom_id);

	sensor_peri = (struct is_device_sensor_peri *)module->private_data;
	subdev_cis = sensor_peri->subdev_cis;
	is_sec_get_sysfs_finfo(&finfo, rom_id);
	is_sec_get_cal_buf(&buf, rom_id);
	client = specific->otprom_client[rom_id];

	camera_running = is_vendor_check_camera_running(finfo->rom_power_position);
i2c_write_retry_global:
	if (camera_running == false) {
		i2c_channel = module->pdata->sensor_i2c_ch;
		if (i2c_channel < SENSOR_CONTROL_I2C_MAX) {
			sensor_peri->cis.i2c_lock = &core->i2c_lock[i2c_channel];
		} else {
			warn("%s: wrong cis i2c_channel(%d)", __func__, i2c_channel);
			ret = -EINVAL;
			goto exit;
		}
		ret = is_sec_set_registers(client, sensor_gc02m1_setfile_otp_global, ARRAY_SIZE(sensor_gc02m1_setfile_otp_global));
		if (unlikely(ret)) {
			err("failed to apply otprom global settings (%d)\n", ret);
			if (retry >= 0) {
				retry--;
				msleep(50);
				goto i2c_write_retry_global;
			}
			ret = -EINVAL;
			goto exit;
		}
	}

	ret = is_sec_set_registers(client, sensor_gc02m1_setfile_otp_mode, ARRAY_SIZE(sensor_gc02m1_setfile_otp_mode));
	if (unlikely(ret)) {
		err("failed to apply otprom mode (%d)\n", ret);
		ret = -EINVAL;
		goto exit;
	}

	ret = is_sec_set_registers(client, sensor_gc02m1_setfile_otp_init, ARRAY_SIZE(sensor_gc02m1_setfile_otp_init));
	if (unlikely(ret)) {
		err("failed to apply otprom init setting (%d)\n", ret);
		ret = -EINVAL;
		goto exit;
	}

crc_retry:
	/* read cal data */
	info("I2C read cal data\n");
	ret = is_sec_i2c_read_otp_gc02m1(client, buf, GC02M1_OTP_START_ADDR, GC02M1_OTP_USED_CAL_SIZE);
	if (unlikely(ret)) {
		err("failed to read otp gc02m1 (%d)\n", ret);
		ret = -EINVAL;
		goto exit;
	}

	retry = IS_CAL_RETRY_CNT;

	ret = is_sec_read_otprom_header(rom_id);
	if (unlikely(ret)) {
		if (retry >= 0) {
			retry--;
			goto crc_retry;
		}
		err("OTPROM CRC failed, retry %d", retry);
	}
	info("OTPROM CRC passed\n");

#ifdef DEBUG_FORCE_DUMP_ENABLE
	{
		char file_path[100];

		loff_t pos = 0;

		memset(file_path, 0x00, sizeof(file_path));
		snprintf(file_path, sizeof(file_path), "%srom%d_dump.bin", FIMC_IS_FW_DUMP_PATH, rom_id);

		if (write_data_to_file(file_path, buf, finfo->rom_size, &pos) < 0) {
			info("Failed to dump cal data. rom_id:%d\n", rom_id);
		}
	}
#endif

	is_sec_parse_rom_info(finfo, buf, rom_id);

	/* CRC check */
	if (!is_sec_check_cal_crc32(buf, rom_id) && (retry > 0)) {
		err("OTP CRC failed (retry:%d)\n", retry);
		retry--;
		goto crc_retry;
	}
	else
		pr_info("OTP CRC passed for GC02M1 \n");

exit:
	info("%s X\n", __func__);
	return ret;
}

int is_sec_readcal_otprom(int rom_id)
{
	int ret = 0;
	int position = is_vendor_get_position_from_rom_id(rom_id);
	int sensor_id = is_vendor_get_sensor_id_from_position(position);

	switch(sensor_id) {
		case SENSOR_NAME_GC5035:
			ret = is_sec_readcal_otprom_gc5035(rom_id);
			break;
		case SENSOR_NAME_GC02M1:
			ret = is_sec_readcal_otprom_gc02m1(rom_id);
			break;
#if defined(CAMERA_UWIDE_DUALIZED)
		case SENSOR_NAME_HI1336:
			ret = is_sec_readcal_otprom_hi1336(rom_id);
			break;
#endif
		case SENSOR_NAME_S5K4HA:
			ret = is_sec_readcal_otprom_4ha(rom_id);
			break;
		case SENSOR_NAME_HI1339:
			ret = is_sec_readcal_otprom_hi1339(rom_id);
			break;
		default:
			err("[%s] No supported sensor_id:%d for rom_id:%d",__func__,sensor_id,rom_id);
			ret = -EINVAL;
			break;
	}
	return ret;
}
#endif

#if defined (CONFIG_CAMERA_EEPROM_DUALIZED)
int is_sec_readcal_eeprom_dualized(int rom_id)
{
	int ret = 0;
	int i;
	struct is_rom_info *finfo = NULL;
#ifdef CONFIG_SEC_CAL_ENABLE
	struct rom_standard_cal_data *standard_cal_data;
	struct rom_standard_cal_data *backup_standard_cal_data;
#endif

	is_sec_get_sysfs_finfo(&finfo, rom_id);

#ifdef CONFIG_SEC_CAL_ENABLE
	standard_cal_data = &(finfo->standard_cal_data);
	backup_standard_cal_data = &(finfo->backup_standard_cal_data);
#endif

	if (finfo->is_read_dualized_values){
		pr_info("Dualized values already read\n");
		goto EXIT;
	}

	for (i = 0 ; i < IS_ROM_CRC_MAX_LIST; i++){
		finfo->header_crc_check_list[i] = finfo->header_dualized_crc_check_list[i];
		finfo->crc_check_list[i] = finfo->crc_dualized_check_list[i];
		finfo->dual_crc_check_list[i] = finfo->dual_crc_dualized_check_list[i];
	}
	finfo->header_crc_check_list_len = finfo->header_dualized_crc_check_list_len;
	finfo->crc_check_list_len = finfo->crc_dualized_check_list_len;
	finfo->dual_crc_check_list_len = finfo->dual_crc_dualized_check_list_len;
	
	for (i = 0 ; i < IS_ROM_DUAL_TILT_MAX_LIST; i++){
		finfo->rom_dualcal_slave0_tilt_list[i] = finfo->rom_dualized_dualcal_slave0_tilt_list[i];
		finfo->rom_dualcal_slave1_tilt_list[i] = finfo->rom_dualized_dualcal_slave1_tilt_list[i];
		finfo->rom_dualcal_slave2_tilt_list[i] = finfo->rom_dualized_dualcal_slave2_tilt_list[i];
		finfo->rom_dualcal_slave3_tilt_list[i] = finfo->rom_dualized_dualcal_slave3_tilt_list[i];
	}
	finfo->rom_dualcal_slave0_tilt_list_len = finfo->rom_dualized_dualcal_slave0_tilt_list_len;
	finfo->rom_dualcal_slave1_tilt_list_len = finfo->rom_dualized_dualcal_slave1_tilt_list_len;
	finfo->rom_dualcal_slave2_tilt_list_len = finfo->rom_dualized_dualcal_slave2_tilt_list_len;
	finfo->rom_dualcal_slave3_tilt_list_len = finfo->rom_dualized_dualcal_slave3_tilt_list_len;

	for (i = 0 ; i < IS_ROM_OIS_MAX_LIST; i++)
		finfo->rom_ois_list[i] = finfo->rom_dualized_ois_list[i];	
	finfo->rom_ois_list_len = finfo->rom_dualized_ois_list_len;

	/* set -1 if not support */
	finfo->rom_header_cal_data_start_addr = finfo->rom_dualized_header_cal_data_start_addr;
	finfo->rom_header_version_start_addr = finfo->rom_dualized_header_version_start_addr;
	finfo->rom_header_cal_map_ver_start_addr = finfo->rom_dualized_header_cal_map_ver_start_addr;
	finfo->rom_header_isp_setfile_ver_start_addr = finfo->rom_dualized_header_isp_setfile_ver_start_addr;
	finfo->rom_header_project_name_start_addr = finfo->rom_dualized_header_project_name_start_addr;
	finfo->rom_header_module_id_addr = finfo->rom_dualized_header_module_id_addr;
	finfo->rom_header_sensor_id_addr =	finfo->rom_dualized_header_sensor_id_addr;
	finfo->rom_header_mtf_data_addr = finfo->rom_dualized_header_mtf_data_addr;
	finfo->rom_header_f2_mtf_data_addr = finfo->rom_dualized_header_f2_mtf_data_addr;
	finfo->rom_header_f3_mtf_data_addr = finfo->rom_dualized_header_f3_mtf_data_addr;
	finfo->rom_awb_master_addr = finfo->rom_dualized_awb_master_addr;
	finfo->rom_awb_module_addr = finfo->rom_dualized_awb_module_addr;

	for (i = 0 ; i < AF_CAL_MAX; i++)
		finfo->rom_af_cal_addr[i] = finfo->rom_dualized_af_cal_addr[i];	
	finfo->rom_af_cal_addr_len = finfo->rom_dualized_af_cal_addr_len;
	finfo->rom_paf_cal_start_addr = finfo->rom_dualized_paf_cal_start_addr;

#ifdef CONFIG_SEC_CAL_ENABLE
	/* standard cal */
	finfo->use_standard_cal = finfo->use_dualized_standard_cal;	
	memcpy(&finfo->standard_cal_data, &finfo->standard_dualized_cal_data, sizeof(struct rom_standard_cal_data));
	memcpy(&finfo->backup_standard_cal_data, &finfo->backup_standard_dualized_cal_data, sizeof(struct rom_standard_cal_data));
#endif

	finfo->rom_header_sensor2_id_addr = finfo->rom_dualized_header_sensor2_id_addr;
	finfo->rom_header_sensor2_version_start_addr = finfo->rom_dualized_header_sensor2_version_start_addr;
	finfo->rom_header_sensor2_mtf_data_addr = finfo->rom_dualized_header_sensor2_mtf_data_addr;
	finfo->rom_sensor2_awb_master_addr = finfo->rom_dualized_sensor2_awb_master_addr;
	finfo->rom_sensor2_awb_module_addr = finfo->rom_dualized_sensor2_awb_module_addr;

	for (i = 0 ; i < AF_CAL_MAX; i++)
		finfo->rom_sensor2_af_cal_addr[i] = finfo->rom_dualized_sensor2_af_cal_addr[i];

	finfo->rom_sensor2_af_cal_addr_len = finfo->rom_dualized_sensor2_af_cal_addr_len;
	finfo->rom_sensor2_paf_cal_start_addr = finfo->rom_dualized_sensor2_paf_cal_start_addr;

	finfo->rom_dualcal_slave0_start_addr = finfo->rom_dualized_dualcal_slave0_start_addr;
	finfo->rom_dualcal_slave0_size = finfo->rom_dualized_dualcal_slave0_size;
	finfo->rom_dualcal_slave1_start_addr = finfo->rom_dualized_dualcal_slave1_start_addr;
	finfo->rom_dualcal_slave1_size = finfo->rom_dualized_dualcal_slave1_size;
	finfo->rom_dualcal_slave2_start_addr = finfo->rom_dualized_dualcal_slave2_start_addr;
	finfo->rom_dualcal_slave2_size = finfo->rom_dualized_dualcal_slave2_size;

	finfo->rom_pdxtc_cal_data_start_addr = finfo->rom_dualized_pdxtc_cal_data_start_addr;
	finfo->rom_pdxtc_cal_data_0_size = finfo->rom_dualized_pdxtc_cal_data_0_size;
	finfo->rom_pdxtc_cal_data_1_size = finfo->rom_dualized_pdxtc_cal_data_1_size;

	finfo->rom_spdc_cal_data_start_addr = finfo->rom_dualized_spdc_cal_data_start_addr;
	finfo->rom_spdc_cal_data_size = finfo->rom_dualized_spdc_cal_data_size;
	finfo->rom_xtc_cal_data_start_addr = finfo->rom_dualized_xtc_cal_data_start_addr;
	finfo->rom_xtc_cal_data_size = finfo->rom_dualized_xtc_cal_data_size;
	finfo->rom_pdxtc_cal_endian_check = finfo->rom_dualized_pdxtc_cal_endian_check;
	
	for (i = 0 ; i < CROSSTALK_CAL_MAX; i++)
		finfo->rom_pdxtc_cal_data_addr_list[i] = finfo->rom_dualized_pdxtc_cal_data_addr_list[i];

	finfo->rom_pdxtc_cal_data_addr_list_len = finfo->rom_dualized_pdxtc_cal_data_addr_list_len;
	finfo->rom_gcc_cal_endian_check = finfo->rom_dualized_gcc_cal_endian_check;

	for (i = 0 ; i < CROSSTALK_CAL_MAX; i++)
		finfo->rom_gcc_cal_data_addr_list[i] = finfo->rom_dualized_gcc_cal_data_addr_list[i];

	finfo->rom_gcc_cal_data_addr_list_len = finfo->rom_dualized_gcc_cal_data_addr_list_len;
	finfo->rom_xtc_cal_endian_check = finfo->rom_dualized_xtc_cal_endian_check;

	for (i = 0 ; i < CROSSTALK_CAL_MAX; i++)
		finfo->rom_xtc_cal_data_addr_list[i] = finfo->rom_dualized_xtc_cal_data_addr_list[i];

	finfo->rom_xtc_cal_data_addr_list_len = finfo->rom_dualized_xtc_cal_data_addr_list_len;

	finfo->rom_dualcal_slave1_cropshift_x_addr = finfo->rom_dualized_dualcal_slave1_cropshift_x_addr;
	finfo->rom_dualcal_slave1_cropshift_y_addr = finfo->rom_dualized_dualcal_slave1_cropshift_y_addr;
	finfo->rom_dualcal_slave1_oisshift_x_addr = finfo->rom_dualized_dualcal_slave1_oisshift_x_addr;
	finfo->rom_dualcal_slave1_oisshift_y_addr = finfo->rom_dualized_dualcal_slave1_oisshift_y_addr;
	finfo->rom_dualcal_slave1_dummy_flag_addr = finfo->rom_dualized_dualcal_slave1_dummy_flag_addr;

#ifdef CONFIG_SEC_CAL_ENABLE
	standard_cal_data->rom_standard_cal_start_addr = standard_cal_data->rom_dualized_standard_cal_start_addr;
	standard_cal_data->rom_standard_cal_end_addr = standard_cal_data->rom_dualized_standard_cal_end_addr;
	standard_cal_data->rom_standard_cal_module_crc_addr = standard_cal_data->rom_dualized_standard_cal_module_crc_addr;
	standard_cal_data->rom_standard_cal_module_checksum_len = standard_cal_data->rom_dualized_standard_cal_module_checksum_len;
	standard_cal_data->rom_standard_cal_sec2lsi_end_addr = standard_cal_data->rom_dualized_standard_cal_sec2lsi_end_addr;
	standard_cal_data->rom_header_standard_cal_end_addr = standard_cal_data->rom_dualized_header_standard_cal_end_addr;
	standard_cal_data->rom_header_main_shading_end_addr = standard_cal_data->rom_dualized_header_main_shading_end_addr;
	standard_cal_data->rom_awb_start_addr = standard_cal_data->rom_dualized_awb_start_addr;
	standard_cal_data->rom_awb_end_addr = standard_cal_data->rom_dualized_awb_end_addr;
	standard_cal_data->rom_awb_section_crc_addr = standard_cal_data->rom_dualized_awb_section_crc_addr;
	standard_cal_data->rom_shading_start_addr = standard_cal_data->rom_dualized_shading_start_addr;
	standard_cal_data->rom_shading_end_addr = standard_cal_data->rom_dualized_shading_end_addr;
	standard_cal_data->rom_shading_section_crc_addr = standard_cal_data->rom_dualized_shading_section_crc_addr;
	standard_cal_data->rom_factory_start_addr = standard_cal_data->rom_dualized_factory_start_addr;
	standard_cal_data->rom_factory_end_addr = standard_cal_data->rom_dualized_factory_end_addr;
	standard_cal_data->rom_awb_sec2lsi_start_addr = standard_cal_data->rom_dualized_awb_sec2lsi_start_addr;
	standard_cal_data->rom_awb_sec2lsi_end_addr = standard_cal_data->rom_dualized_awb_sec2lsi_end_addr;
	standard_cal_data->rom_awb_sec2lsi_checksum_addr = standard_cal_data->rom_dualized_awb_sec2lsi_checksum_addr;
	standard_cal_data->rom_awb_sec2lsi_checksum_len = standard_cal_data->rom_dualized_awb_sec2lsi_checksum_len;
	standard_cal_data->rom_shading_sec2lsi_start_addr = standard_cal_data->rom_dualized_shading_sec2lsi_start_addr;
	standard_cal_data->rom_shading_sec2lsi_end_addr = standard_cal_data->rom_dualized_shading_sec2lsi_end_addr;
	standard_cal_data->rom_shading_sec2lsi_checksum_addr = standard_cal_data->rom_dualized_shading_sec2lsi_checksum_addr;
	standard_cal_data->rom_shading_sec2lsi_checksum_len = standard_cal_data->rom_dualized_shading_sec2lsi_checksum_len;
	standard_cal_data->rom_factory_sec2lsi_start_addr = standard_cal_data->rom_dualized_factory_sec2lsi_start_addr;

	backup_standard_cal_data->rom_standard_cal_start_addr = backup_standard_cal_data->rom_dualized_standard_cal_start_addr;
	backup_standard_cal_data->rom_standard_cal_end_addr = backup_standard_cal_data->rom_dualized_standard_cal_end_addr;
	backup_standard_cal_data->rom_standard_cal_module_crc_addr = backup_standard_cal_data->rom_dualized_standard_cal_module_crc_addr;
	backup_standard_cal_data->rom_standard_cal_module_checksum_len = backup_standard_cal_data->rom_dualized_standard_cal_module_checksum_len;
	backup_standard_cal_data->rom_standard_cal_sec2lsi_end_addr = backup_standard_cal_data->rom_dualized_standard_cal_sec2lsi_end_addr;
	backup_standard_cal_data->rom_header_standard_cal_end_addr = backup_standard_cal_data->rom_dualized_header_standard_cal_end_addr;
	backup_standard_cal_data->rom_header_main_shading_end_addr = backup_standard_cal_data->rom_dualized_header_main_shading_end_addr;
	backup_standard_cal_data->rom_awb_start_addr = backup_standard_cal_data->rom_dualized_awb_start_addr;
	backup_standard_cal_data->rom_awb_end_addr = backup_standard_cal_data->rom_dualized_awb_end_addr;
	backup_standard_cal_data->rom_awb_section_crc_addr = backup_standard_cal_data->rom_dualized_awb_section_crc_addr;
	backup_standard_cal_data->rom_shading_start_addr = backup_standard_cal_data->rom_dualized_shading_start_addr;
	backup_standard_cal_data->rom_shading_end_addr = backup_standard_cal_data->rom_dualized_shading_end_addr;
	backup_standard_cal_data->rom_shading_section_crc_addr = backup_standard_cal_data->rom_dualized_shading_section_crc_addr;
	backup_standard_cal_data->rom_factory_start_addr = backup_standard_cal_data->rom_dualized_factory_start_addr;
	backup_standard_cal_data->rom_factory_end_addr = backup_standard_cal_data->rom_dualized_factory_end_addr;
	backup_standard_cal_data->rom_awb_sec2lsi_start_addr = backup_standard_cal_data->rom_dualized_awb_sec2lsi_start_addr;
	backup_standard_cal_data->rom_awb_sec2lsi_end_addr = backup_standard_cal_data->rom_dualized_awb_sec2lsi_end_addr;
	backup_standard_cal_data->rom_awb_sec2lsi_checksum_addr = backup_standard_cal_data->rom_dualized_awb_sec2lsi_checksum_addr;
	backup_standard_cal_data->rom_awb_sec2lsi_checksum_len = backup_standard_cal_data->rom_dualized_awb_sec2lsi_checksum_len;
	backup_standard_cal_data->rom_shading_sec2lsi_start_addr = backup_standard_cal_data->rom_dualized_shading_sec2lsi_start_addr;
	backup_standard_cal_data->rom_shading_sec2lsi_end_addr = backup_standard_cal_data->rom_dualized_shading_sec2lsi_end_addr;
	backup_standard_cal_data->rom_shading_sec2lsi_checksum_addr = backup_standard_cal_data->rom_dualized_shading_sec2lsi_checksum_addr;
	backup_standard_cal_data->rom_shading_sec2lsi_checksum_len = backup_standard_cal_data->rom_dualized_shading_sec2lsi_checksum_len;
	backup_standard_cal_data->rom_factory_sec2lsi_start_addr = backup_standard_cal_data->rom_dualized_factory_sec2lsi_start_addr;
#endif

	finfo->is_read_dualized_values = true;
	pr_info("%s EEPROM dualized values read\n",__func__);
EXIT:
	return ret;
}
#endif

int is_sec_readcal_eeprom(int rom_id)
{
	int ret = 0;
	int count = 0;
	char *buf = NULL;
	int retry = IS_CAL_RETRY_CNT;
	struct is_core *core = is_get_is_core();
	struct is_rom_info *finfo = NULL;
	struct is_vender_specific *specific = core->vender.private_data;
	int cal_size = 0;
	u32 read_addr = 0x0;
	struct i2c_client *client = NULL;
	struct is_device_eeprom *eeprom;
#if defined(CONFIG_CAMERA_EEPROM_DUALIZED)
	int rom_type;
	struct is_module_enum *module;
	int position = is_vendor_get_position_from_rom_id(rom_id);
	int sensor_id = is_vendor_get_sensor_id_from_position(position);
#endif

#ifdef CONFIG_SEC_CAL_ENABLE
	char *buf_rom_data = NULL;
#endif

	info("Camera: read cal data from EEPROM (rom_id:%d)\n", rom_id);

	is_sec_get_sysfs_finfo(&finfo, rom_id);
	is_sec_get_cal_buf(&buf, rom_id);
	client = specific->eeprom_client[rom_id];

	eeprom = i2c_get_clientdata(client);

	cal_size = finfo->rom_size;
	info("%s: rom_id : %d, cal_size :%d\n", __func__, rom_id, cal_size);

#if defined (CONFIG_CAMERA_EEPROM_DUALIZED)
	is_vendor_get_module_from_position(position, &module);

	if (!module) {
		err("%s: There is no module (rom_id : %d)\n", __func__, rom_id);
		ret = false;
		goto exit;
	}
	rom_type = module->pdata->rom_type;

	if (!(sensor_id == SENSOR_NAME_S5KGD2))	//put all dualized sensor names here
		finfo->is_read_dualized_values = true;
	if (!(finfo->is_read_dualized_values)){
		is_sec_readcal_eeprom_dualized(rom_id); // call this function only once for EEPROM dualized sensors
	}
#endif

	if (finfo->rom_header_cal_map_ver_start_addr != -1) {
		ret = is_i2c_read(client, finfo->cal_map_ver,
					finfo->rom_header_cal_map_ver_start_addr,
					IS_CAL_MAP_VER_SIZE);

		if (unlikely(ret)) {
			err("failed to is_i2c_read (%d)\n", ret);
			ret = -EINVAL;
			goto exit;
		}
	}

	if (finfo->rom_header_version_start_addr != -1) {
		ret = is_i2c_read(client, finfo->header_ver,
					finfo->rom_header_version_start_addr,
					IS_HEADER_VER_SIZE);

		if (unlikely(ret)) {
			err("failed to is_i2c_read (%d)\n", ret);
			ret = -EINVAL;
			goto exit;
		}
	}

	info("Camera : EEPROM Cal map_version = %s(%x%x%x%x)\n", finfo->cal_map_ver, finfo->cal_map_ver[0],
			finfo->cal_map_ver[1], finfo->cal_map_ver[2], finfo->cal_map_ver[3]);
	info("EEPROM header version = %s(%x%x%x%x)\n", finfo->header_ver,
		finfo->header_ver[0], finfo->header_ver[1], finfo->header_ver[2], finfo->header_ver[3]);

	if (!is_sec_check_rom_ver(core, rom_id)) {
		info("Camera: Do not read eeprom cal data. EEPROM version is low.\n");
		return 0;
	}

crc_retry:

	/* read cal data */
	read_addr = 0x0;
	if (cal_size > IS_READ_MAX_EEP_CAL_SIZE) {
		for (count = 0; count < cal_size/IS_READ_MAX_EEP_CAL_SIZE; count++) {
			info("Camera: I2C read cal data : offset = 0x%x, size = 0x%x\n", read_addr, IS_READ_MAX_EEP_CAL_SIZE);
			ret = is_i2c_read(client, &buf[read_addr], read_addr, IS_READ_MAX_EEP_CAL_SIZE);
			if (ret) {
				err("failed to is_i2c_read (%d)\n", ret);
				ret = -EINVAL;
				goto exit;
			}
			read_addr += IS_READ_MAX_EEP_CAL_SIZE;
		}

		if (read_addr < cal_size) {
			ret = is_i2c_read(client, &buf[read_addr], read_addr, cal_size - read_addr);
		}
	} else {
		info("Camera: I2C read cal data\n");
		ret = is_i2c_read(client, buf, read_addr, cal_size);
		if (ret) {
			err("failed to is_i2c_read (%d)\n", ret);
			ret = -EINVAL;
			goto exit;
		}
	}

	is_sec_parse_rom_info(finfo, buf, rom_id);


#ifdef DEBUG_FORCE_DUMP_ENABLE
	{
		char file_path[100];

		loff_t pos = 0;

		memset(file_path, 0x00, sizeof(file_path));
		snprintf(file_path, sizeof(file_path), "%srom%d_dump.bin", IS_FW_DUMP_PATH, rom_id);

		if (write_data_to_file(file_path, buf, cal_size, &pos) < 0) {
			info("Failed to dump cal data. rom_id:%d\n", rom_id);
		}
	}
#endif

	/* CRC check */
	if (!is_sec_check_cal_crc32(buf, rom_id) && (retry > 0)) {
		retry--;
		goto crc_retry;
	}

	is_sec_check_module_state(finfo);

#ifdef USE_CAMERA_NOTIFY_WACOM
	if (!test_bit(IS_CRC_ERROR_HEADER, &finfo->crc_error))
		is_eeprom_info_update(rom_id, finfo->header_ver);
#endif

#ifdef CONFIG_SEC_CAL_ENABLE
	/* Store original rom data before conversion for intrinsic cal */
	if (is_sec_check_cal_crc32(buf, rom_id) == true && is_need_use_standard_cal(rom_id)) {
		is_sec_get_cal_buf_rom_data(&buf_rom_data, rom_id);
		if (buf != NULL && buf_rom_data != NULL)
			memcpy(buf_rom_data, buf, is_sec_get_max_cal_size(core, rom_id));
	}
#endif
exit:
	return ret;
}

#if defined(CONFIG_CAMERA_FROM)
int is_sec_read_from_header(void)
{
	int ret = 0;
	struct is_core *core = is_get_is_core();
	u8 header_version[IS_HEADER_VER_SIZE + 1] = {0, };
	struct is_rom_info *finfo = NULL;

	is_sec_get_sysfs_finfo(&finfo, ROM_ID_REAR);

	ret = is_spi_read(&core->spi0, header_version,
		finfo->rom_header_version_start_addr, IS_HEADER_VER_SIZE);
	if (ret < 0) {
		printk(KERN_ERR "failed to is_spi_read for header version (%d)\n", ret);
		ret = -EINVAL;
	}

	memcpy(finfo->header_ver, header_version, IS_HEADER_VER_SIZE);
	finfo->header_ver[IS_HEADER_VER_SIZE] = '\0';

	return ret;
}

int is_sec_check_status(struct is_core *core)
{
	int retry_read = 50;
	u8 temp[5] = {0x0, };
	int ret = 0;

	do {
		memset(temp, 0x0, sizeof(temp));
		is_spi_read_status_bit(&core->spi0, &temp[0]);
		if (retry_read < 0) {
			ret = -EINVAL;
			err("check status failed.");
			break;
		}
		retry_read--;
		msleep(3);
	} while (temp[0]);

	return ret;
}

#if defined(CONFIG_CAMERA_FROM) && defined(CAMERA_MODULE_DUALIZE)
int is_sec_read_fw_from_sdcard(char *name, unsigned long *size)
{
	int ret = 0;
#ifdef USE_KERNEL_VFS_READ_WRITE
	struct file *fw_fp = NULL;
	mm_segment_t old_fs;
	loff_t pos = 0;
	char data_path[100];
	unsigned long fsize;

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	snprintf(data_path, sizeof(data_path), "%s", name);
	memset(fw_buf, 0x0, IS_MAX_FW_BUFFER_SIZE);

	fw_fp = filp_open(data_path, O_RDONLY, 0);
	if (IS_ERR_OR_NULL(fw_fp)) {
		info("%s does not exist.\n", data_path);
		fw_fp = NULL;
		ret = -EIO;
		goto fw_err;
	} else {
		info("%s exist, Dump from sdcard.\n", name);
		fsize = fw_fp->f_path.dentry->d_inode->i_size;
		if (IS_MAX_FW_BUFFER_SIZE >= fsize) {
			read_data_rom_file(name, fw_buf, fsize, &pos);
			*size = fsize;
		} else {
			err("FW size is larger than FW buffer.\n");
			BUG();
		}
	}

fw_err:
	if (fw_fp)
		filp_close(fw_fp, current->files);
	set_fs(old_fs);
#endif
	return ret;
}

u32 is_sec_get_fw_crc32(char *buf, size_t size)
{
	u32 *buf32 = NULL;
	u32 checksum;

	buf32 = (u32 *)buf;
	checksum = (u32)getCRC((u16 *)&buf32[0], size, NULL, NULL);

	return checksum;
}

int is_sec_change_from_header(struct is_core *core)
{
	int ret = 0;
	u8 crc_value[4];
	u32 crc_result = 0;
	struct is_rom_info *finfo = NULL;

	is_sec_get_sysfs_finfo(&finfo, ROM_ID_REAR);

	/* read header data */
	info("Camera: Start SPI read header data\n");
	memset(fw_buf, 0x0, IS_MAX_FW_BUFFER_SIZE);

	ret = is_spi_read(&core->spi0, fw_buf, 0x0, HEADER_CRC32_LEN);
	if (ret) {
		err("failed to is_spi_read (%d)\n", ret);
		ret = -EINVAL;
		goto exit;
	}

	fw_buf[0x7] = (finfo->bin_end_addr & 0xFF000000) >> 24;
	fw_buf[0x6] = (finfo->bin_end_addr & 0xFF0000) >> 16;
	fw_buf[0x5] = (finfo->bin_end_addr & 0xFF00) >> 8;
	fw_buf[0x4] = (finfo->bin_end_addr & 0xFF);
	fw_buf[0x27] = (finfo->setfile_end_addr & 0xFF000000) >> 24;
	fw_buf[0x26] = (finfo->setfile_end_addr & 0xFF0000) >> 16;
	fw_buf[0x25] = (finfo->setfile_end_addr & 0xFF00) >> 8;
	fw_buf[0x24] = (finfo->setfile_end_addr & 0xFF);
	fw_buf[0x37] = (finfo->concord_bin_end_addr & 0xFF000000) >> 24;
	fw_buf[0x36] = (finfo->concord_bin_end_addr & 0xFF0000) >> 16;
	fw_buf[0x35] = (finfo->concord_bin_end_addr & 0xFF00) >> 8;
	fw_buf[0x34] = (finfo->concord_bin_end_addr & 0xFF);

	strncpy(&fw_buf[0x40], finfo->header_ver, 9);
	strncpy(&fw_buf[0x50], finfo->concord_header_ver, IS_HEADER_VER_SIZE);
	strncpy(&fw_buf[0x64], finfo->setfile_ver, IS_ISP_SETFILE_VER_SIZE);

	is_spi_write_enable(&core->spi0);
	ret = is_spi_erase_sector(&core->spi0, 0x0);
	if (ret) {
		err("failed to is_spi_erase_sector (%d)\n", ret);
		ret = -EINVAL;
		goto exit;
	}
	ret = is_sec_check_status(core);
	if (ret) {
		err("failed to is_sec_check_status (%d)\n", ret);
		ret = -EINVAL;
		goto exit;
	}

	ret = is_spi_write_enable(&core->spi0);
	ret = is_spi_write(&core->spi0, 0x0, fw_buf, HEADER_CRC32_LEN);
	if (ret) {
		err("failed to is_spi_write (%d)\n", ret);
		ret = -EINVAL;
		goto exit;
	}
	ret = is_sec_check_status(core);
	if (ret) {
		err("failed to is_sec_check_status (%d)\n", ret);
		ret = -EINVAL;
		goto exit;
	}

	crc_result = is_sec_get_fw_crc32(fw_buf, HEADER_CRC32_LEN);
	crc_value[3] = (crc_result & 0xFF000000) >> 24;
	crc_value[2] = (crc_result & 0xFF0000) >> 16;
	crc_value[1] = (crc_result & 0xFF00) >> 8;
	crc_value[0] = (crc_result & 0xFF);

	ret = is_spi_write_enable(&core->spi0);
	ret = is_spi_write(&core->spi0, 0x0FFC, crc_value, 0x4);
	if (ret) {
		err("failed to is_spi_write (%d)\n", ret);
		ret = -EINVAL;
		goto exit;
	}
	ret = is_sec_check_status(core);
	if (ret) {
		err("failed to is_sec_check_status (%d)\n", ret);
		ret = -EINVAL;
		goto exit;
	}

	info("Camera: End SPI read header data\n");

exit:
	return ret;
}

int is_sec_write_fw_to_from(struct is_core *core, char *name, bool first_section)
{
	int ret = 0;
	unsigned long i = 0;
	unsigned long size = 0;
	u32 start_addr = 0, erase_addr = 0, end_addr = 0;
	u32 checksum_addr = 0, crc_result = 0, erase_end_addr = 0;
	u8 crc_value[4];
	struct is_rom_info *finfo = NULL;

	is_sec_get_sysfs_finfo(&finfo, ROM_ID_REAR);

	if (!strcmp(name, IS_FW_FROM_SDCARD)) {
		ret = is_sec_read_fw_from_sdcard(IS_FW_FROM_SDCARD, &size);
		start_addr = finfo->bin_start_addr;
		end_addr = (u32)size + start_addr - 1;
		finfo->bin_end_addr = end_addr;
		checksum_addr = 0x3FFFFF;
		finfo->fw_size = size;
		strncpy(finfo->header_ver, &fw_buf[size - IS_HEADER_VER_OFFSET], 9);
	} else if (!strcmp(name, IS_SETFILE_FROM_SDCARD)) {
		ret = is_sec_read_fw_from_sdcard(IS_SETFILE_FROM_SDCARD, &size);
		start_addr = finfo->setfile_start_addr;
		end_addr = (u32)size + start_addr - 1;
		finfo->setfile_end_addr = end_addr;
		if (is_sec_fw_module_compare(finfo->header_ver, FW_2P2_B))
			checksum_addr = ROM_WRITE_CHECKSUM_SETF_LL;
		else
			checksum_addr = ROM_WRITE_CHECKSUM_SETF_LS;
		finfo->setfile_size = size;
		strncpy(finfo->setfile_ver, &fw_buf[size - 64], IS_ISP_SETFILE_VER_SIZE);
	} else if (!strcmp(name, IS_COMPANION_FROM_SDCARD)) {
		ret = is_sec_read_fw_from_sdcard(IS_COMPANION_FROM_SDCARD, &size);
		start_addr = finfo->concord_bin_start_addr;
		end_addr = (u32)size + start_addr - 1;
		finfo->concord_bin_end_addr = end_addr;
		if (is_sec_fw_module_compare(finfo->header_ver, FW_2P2_B))
			checksum_addr = ROM_WRITE_CHECKSUM_COMP_LL;
		else
			checksum_addr = ROM_WRITE_CHECKSUM_COMP_LS;
		erase_end_addr = 0x3FFFFF;
		finfo->comp_fw_size = size;
		strncpy(finfo->concord_header_ver, &fw_buf[size - 16], IS_HEADER_VER_SIZE);
	} else {
		err("Not supported binary type.");
		return -EIO;
	}

	if (ret < 0) {
		err("FW is not exist in sdcard.");
		return -EIO;
	}

	info("Start %s write to FROM.\n", name);

	if (first_section) {
		for (erase_addr = start_addr; erase_addr < erase_end_addr; erase_addr += IS_FROM_ERASE_SIZE) {
			ret = is_spi_write_enable(&core->spi0);
			ret |= is_spi_erase_block(&core->spi0, erase_addr);
			if (ret) {
				err("failed to is_spi_erase_block (%d)\n", ret);
				ret = -EINVAL;
				goto exit;
			}
			ret = is_sec_check_status(core);
			if (ret) {
				err("failed to is_sec_check_status (%d)\n", ret);
				ret = -EINVAL;
				goto exit;
			}
		}
	}

	for (i = 0; i < size; i += 256) {
		ret = is_spi_write_enable(&core->spi0);
		if (size - i >= 256) {
			ret = is_spi_write(&core->spi0, start_addr + i, fw_buf + i, 256);
			if (ret) {
				err("failed to is_spi_write (%d)\n", ret);
				ret = -EINVAL;
				goto exit;
			}
		} else {
			ret = is_spi_write(&core->spi0, start_addr + i, fw_buf + i, size - i);
			if (ret) {
				err("failed to is_spi_write (%d)\n", ret);
				ret = -EINVAL;
				goto exit;
			}
		}
		ret = is_sec_check_status(core);
		if (ret) {
			err("failed to is_sec_check_status (%d)\n", ret);
			ret = -EINVAL;
			goto exit;
		}
	}

	crc_result = is_sec_get_fw_crc32(fw_buf, size);
	crc_value[3] = (crc_result & 0xFF000000) >> 24;
	crc_value[2] = (crc_result & 0xFF0000) >> 16;
	crc_value[1] = (crc_result & 0xFF00) >> 8;
	crc_value[0] = (crc_result & 0xFF);

	ret = is_spi_write_enable(&core->spi0);
	if (ret) {
		err("failed to is_spi_write_enable (%d)\n", ret);
		ret = -EINVAL;
		goto exit;
	}

	ret = is_spi_write(&core->spi0, checksum_addr -4 + 1, crc_value, 0x4);
	if (ret) {
		err("failed to is_spi_write (%d)\n", ret);
		ret = -EINVAL;
		goto exit;
	}
	ret = is_sec_check_status(core);
	if (ret) {
		err("failed to is_sec_check_status (%d)\n", ret);
		ret = -EINVAL;
		goto exit;
	}

	info("End %s write to FROM.\n", name);

exit:
	return ret;
}

int is_sec_write_fw(struct is_core *core, struct device *dev)
{
	int ret = 0;
#ifdef USE_KERNEL_VFS_READ_WRITE
	struct file *key_fp = NULL;
	struct file *comp_fw_fp = NULL;
	struct file *setfile_fp = NULL;
	struct file *isp_fw_fp = NULL;
	mm_segment_t old_fs;
	struct is_vender_specific *specific = core->vender.private_data;
	bool camera_running;

	camera_running = is_vendor_check_camera_running(SENSOR_POSITION_REAR);

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	key_fp = filp_open(IS_KEY_FROM_SDCARD, O_RDONLY, 0);
	if (IS_ERR_OR_NULL(key_fp)) {
		info("KEY does not exist.\n");
		key_fp = NULL;
		ret = -EIO;
		goto key_err;
	} else {
		comp_fw_fp = filp_open(IS_COMPANION_FROM_SDCARD, O_RDONLY, 0);
		if (IS_ERR_OR_NULL(comp_fw_fp)) {
			info("Companion FW does not exist.\n");
			comp_fw_fp = NULL;
			ret = -EIO;
			goto comp_fw_err;
		}

		setfile_fp = filp_open(IS_SETFILE_FROM_SDCARD, O_RDONLY, 0);
		if (IS_ERR_OR_NULL(setfile_fp)) {
			info("setfile does not exist.\n");
			setfile_fp = NULL;
			ret = -EIO;
			goto setfile_err;
		}

		isp_fw_fp = filp_open(IS_FW_FROM_SDCARD, O_RDONLY, 0);
		if (IS_ERR_OR_NULL(isp_fw_fp)) {
			info("ISP FW does not exist.\n");
			isp_fw_fp = NULL;
			ret = -EIO;
			goto isp_fw_err;
		}
	}

	info("FW file exist, Write Firmware to FROM .\n");

	if (!is_sec_ldo_enabled(dev, "VDDIO_1.8V_CAM"))
		is_sec_rom_power_on(core, SENSOR_POSITION_REAR);

	ret = is_sec_write_fw_to_from(core, IS_COMPANION_FROM_SDCARD, true);
	if (ret) {
		err("is_sec_write_fw_to_from failed.");
		ret = -EIO;
		goto err;
	}

	ret = is_sec_write_fw_to_from(core, IS_SETFILE_FROM_SDCARD, false);
	if (ret) {
		err("is_sec_write_fw_to_from failed.");
		ret = -EIO;
		goto err;
	}

	ret = is_sec_write_fw_to_from(core, IS_FW_FROM_SDCARD, false);
	if (ret) {
		err("is_sec_write_fw_to_from failed.");
		ret = -EIO;
		goto err;
	}

	/* Off to reset FROM operation. Without this routine, spi read does not work. */
	if (!camera_running)
		is_sec_rom_power_off(core, SENSOR_POSITION_REAR);

	if (!is_sec_ldo_enabled(dev, "VDDIO_1.8V_CAM"))
		is_sec_rom_power_on(core, SENSOR_POSITION_REAR);

	ret = is_sec_change_from_header(core);
	if (ret) {
		err("is_sec_change_from_header failed.");
		ret = -EIO;
		goto err;
	}

err:
	if (!camera_running)
		is_sec_rom_power_off(core, SENSOR_POSITION_REAR);

isp_fw_err:
	if (isp_fw_fp)
		filp_close(isp_fw_fp, current->files);

setfile_err:
	if (setfile_fp)
		filp_close(setfile_fp, current->files);

comp_fw_err:
	if (comp_fw_fp)
		filp_close(comp_fw_fp, current->files);

key_err:
	if (key_fp)
		filp_close(key_fp, current->files);
	set_fs(old_fs);
#endif
	return ret;
}
#endif

int is_sec_readcal(struct is_core *core)
{
	int ret = 0;
	int retry = IS_CAL_RETRY_CNT;
	int module_retry = IS_CAL_RETRY_CNT;
	u16 id = 0;
	int cal_map_ver = 0;

	struct is_vender_specific *specific = core->vender.private_data;
	struct is_rom_info *finfo = NULL;
	char *cal_buf;

	is_sec_get_cal_buf(&cal_buf, ROM_ID_REAR);
	is_sec_get_sysfs_finfo(&finfo, ROM_ID_REAR);

	/* reset spi */
	if (!core->spi0.device) {
		err("spi0 device is not available");
		goto exit;
	}

	ret = is_spi_reset(&core->spi0);
	if (ret) {
		err("failed to is_spi_read (%d)\n", ret);
		ret = -EINVAL;
		goto exit;
	}

module_id_retry:
	ret = is_spi_read_module_id(&core->spi0, &id,
		ROM_HEADER_MODULE_ID_START_ADDR, ROM_HEADER_MODULE_ID_SIZE);
	if (ret) {
		printk(KERN_ERR "is_spi_read_module_id (%d)\n", ret);
		ret = -EINVAL;
		goto exit;
	}

	info("Camera: FROM Module ID = 0x%04x\n", id);
	if (id == 0) {
		err("FROM rear module id NULL %d\n", module_retry);
		if (module_retry > 0) {
			usleep_range(5000, 6000);
			module_retry--;
			goto module_id_retry;
		}
	}

	ret = is_spi_read(&core->spi0, finfo->cal_map_ver,
		finfo->rom_header_cal_map_ver_start_addr, IS_CAL_MAP_VER_SIZE);
	if (ret) {
		printk(KERN_ERR "failed to is_spi_read (%d)\n", ret);
		ret = -EINVAL;
		goto exit;
	}
	info("Camera: FROM Cal map_version = %c%c%c%c\n", finfo->cal_map_ver[0],
		finfo->cal_map_ver[1], finfo->cal_map_ver[2], finfo->cal_map_ver[3]);

crc_retry:
	/* read cal data */
	info("Camera: SPI read cal data\n");
	ret = is_spi_read(&core->spi0, cal_buf, 0x0, finfo->rom_size);
	if (ret) {
		err("failed to is_spi_read (%d)\n", ret);
		ret = -EINVAL;
		goto exit;
	}

	finfo->bin_start_addr = *((u32 *)&cal_buf[ROM_HEADER_ISP_BINARY_START_ADDR]);
	finfo->bin_end_addr = *((u32 *)&cal_buf[ROM_HEADER_ISP_BINARY_END_ADDR]);
	info("Binary start = 0x%08x, end = 0x%08x\n",
		(finfo->bin_start_addr), (finfo->bin_end_addr));

	memcpy(finfo->setfile_ver, &cal_buf[ROM_HEADER_ISP_SETFILE_VER_START_ADDR], IS_ISP_SETFILE_VER_SIZE);
	finfo->setfile_ver[IS_ISP_SETFILE_VER_SIZE] = '\0';

	is_sec_parse_rom_info(finfo, cal_buf, ROM_ID_REAR);

	finfo->fw_size = (unsigned long)(finfo->bin_end_addr - finfo->bin_start_addr + 1);
	info("fw_size = %ld\n", finfo->fw_size);
	finfo->rta_fw_size = (unsigned long)(finfo->rta_bin_end_addr - finfo->rta_bin_start_addr + 1);
	info("rta_fw_size = %ld\n", finfo->rta_fw_size);

#ifdef DEBUG_FORCE_DUMP_ENABLE
	{
		char file_path[100];

		loff_t pos = 0;

		memset(file_path, 0x00, sizeof(file_path));
		snprintf(file_path, sizeof(file_path), "%sfrom_cal.bin", IS_FW_DUMP_PATH);
		if (write_data_to_file(file_path, cal_buf, IS_DUMP_CAL_SIZE, &pos) < 0)
			info("Failed to dump from cal data.\n");
	}
#endif

	/* CRC check */
	if (is_sec_check_rom_ver(core, ROM_ID_REAR)) {
		if (!is_sec_check_cal_crc32(cal_buf, ROM_ID_REAR) && (retry > 0)) {
			retry--;
			goto crc_retry;
		}
	} else {
		set_bit(IS_ROM_STATE_INVALID_ROM_VERSION, &finfo->rom_state);
		set_bit(IS_CRC_ERROR_ALL_SECTION, &finfo->crc_error);
		set_bit(IS_CRC_ERROR_DUAL_CAMERA, &finfo->crc_error);
	}

	is_sec_check_module_state(finfo);

exit:
	return ret;
}

#if defined(CONFIG_CAMERA_FROM) && defined(CAMERA_MODULE_DUALIZE)
void is_sec_get_rta_bin_addr(void)
{
	struct is_rom_info *finfo = NULL;
	struct is_rom_info *finfo_front = NULL;
	char *cal_buf;

	is_sec_get_cal_buf(&cal_buf, ROM_ID_REAR);
	is_sec_get_sysfs_finfo(&finfo, ROM_ID_REAR);
	is_sec_get_sysfs_finfo(&finfo_front, ROM_ID_FRONT);

	if (finfo_front->header_ver[FW_SENSOR_MAKER] == FW_SENSOR_MAKER_SLSI) {
		finfo->rta_bin_start_addr = *((u32 *)&cal_buf[ROM_HEADER_RTA_BINARY_LY_START_ADDR]);
		finfo->rta_bin_end_addr = *((u32 *)&cal_buf[ROM_HEADER_RTA_BINARY_LY_END_ADDR]);
	} else if (finfo_front->header_ver[FW_SENSOR_MAKER] == FW_SENSOR_MAKER_SONY) {
		finfo->rta_bin_start_addr = *((u32 *)&cal_buf[ROM_HEADER_RTA_BINARY_LS_START_ADDR]);
		finfo->rta_bin_end_addr = *((u32 *)&cal_buf[ROM_HEADER_RTA_BINARY_LS_END_ADDR]);
	} else {
		finfo->rta_bin_start_addr = *((u32 *)&cal_buf[ROM_HEADER_RTA_BINARY_LY_START_ADDR]);
		finfo->rta_bin_end_addr = *((u32 *)&cal_buf[ROM_HEADER_RTA_BINARY_LY_END_ADDR]);
	}

	info("rta bin start = 0x%08x, end = 0x%08x\n",
		finfo->rta_bin_start_addr, finfo->rta_bin_end_addr);
	finfo->rta_fw_size = (unsigned long)(finfo->rta_bin_end_addr - finfo->rta_bin_start_addr + 1);
	info("rta fw size = %ld\n", finfo->rta_fw_size);
}

void is_sec_get_rear_setfile_addr(void)
{
	struct is_rom_info *finfo = NULL;
	char *cal_buf;

	is_sec_get_cal_buf(&cal_buf, ROM_ID_REAR);
	is_sec_get_sysfs_finfo(&finfo, ROM_ID_REAR);

	if (finfo->header_ver[FW_SENSOR_MAKER] == FW_SENSOR_MAKER_SLSI ||
		finfo->header_ver[FW_SENSOR_MAKER] == FW_SENSOR_MAKER_SLSI_SONY) {
#ifdef CAMERA_MODULE_REAR2_SETF_DUMP
		finfo->setfile_start_addr = 0;
		finfo->setfile_end_addr = 0;
#else
		finfo->setfile_start_addr = *((u32 *)&cal_buf[ROM_HEADER_ISP_SETFILE_LL_START_ADDR]);
		finfo->setfile_end_addr = *((u32 *)&cal_buf[ROM_HEADER_ISP_SETFILE_LL_END_ADDR]);
#endif
	} else if (finfo->header_ver[FW_SENSOR_MAKER] == FW_SENSOR_MAKER_SONY ||
		finfo->header_ver[FW_SENSOR_MAKER] == FW_SENSOR_MAKER_SONY_LSI) {
		finfo->setfile_start_addr = *((u32 *)&cal_buf[ROM_HEADER_ISP_SETFILE_LS_START_ADDR]);
		finfo->setfile_end_addr = *((u32 *)&cal_buf[ROM_HEADER_ISP_SETFILE_LS_END_ADDR]);
	} else {
#ifdef CAMERA_MODULE_REAR2_SETF_DUMP
		finfo->setfile_start_addr = *((u32 *)&cal_buf[ROM_HEADER_ISP_SETFILE_LS_START_ADDR]);
		finfo->setfile_end_addr = *((u32 *)&cal_buf[ROM_HEADER_ISP_SETFILE_LS_END_ADDR]);
#else
		finfo->setfile_start_addr = *((u32 *)&cal_buf[ROM_HEADER_ISP_SETFILE_LL_START_ADDR]);
		finfo->setfile_end_addr = *((u32 *)&cal_buf[ROM_HEADER_ISP_SETFILE_LL_END_ADDR]);
#endif
	}
	if (finfo->setfile_end_addr < ROM_ISP_BINARY_SETFILE_START_ADDR
		|| finfo->setfile_end_addr > ROM_ISP_BINARY_SETFILE_END_ADDR) {
		err("setfile end_addr has error!!  0x%08x\n", finfo->setfile_end_addr);
		finfo->setfile_end_addr = 0x1fffff;
	}

	info("Setfile start = 0x%08x, end = 0x%08x\n",
		(finfo->setfile_start_addr), (finfo->setfile_end_addr));
	finfo->setfile_size = (unsigned long)(finfo->setfile_end_addr - finfo->setfile_start_addr + 1);
	info("setfile_size = %ld\n", finfo->setfile_size);
}

#ifdef CAMERA_MODULE_REAR2_SETF_DUMP
void is_sec_get_rear2_setfile_addr(void)
{
	struct is_rom_info *finfo = NULL;
	char *cal_buf;

	is_sec_get_cal_buf(&cal_buf, ROM_ID_REAR);
	is_sec_get_sysfs_finfo(&finfo, ROM_ID_REAR);

	if (finfo->header_ver[FW_SENSOR_MAKER] == FW_SENSOR_MAKER_SLSI ||
		finfo->header_ver[FW_SENSOR_MAKER] == FW_SENSOR_MAKER_SLSI_SONY) {
		finfo->setfile2_start_addr = 0;
		finfo->setfile2_end_addr = 0;
	} else {
		finfo->setfile2_start_addr = *((u32 *)&cal_buf[ROM_HEADER_ISP_SETFILE2_LL_START_ADDR]);
		finfo->setfile2_end_addr = *((u32 *)&cal_buf[ROM_HEADER_ISP_SETFILE2_LL_END_ADDR]);
	}

	info("Setfile2 start = 0x%08x, end = 0x%08x\n",
		(finfo->setfile2_start_addr), (finfo->setfile2_end_addr));
	finfo->setfile2_size =
		(unsigned long)(finfo->setfile2_end_addr - finfo->setfile2_start_addr + 1);
	info("setfile2_size = %ld\n", finfo->setfile2_size);
}
#endif

void is_sec_get_front_setfile_addr(void)
{
	struct is_rom_info *finfo = NULL;
	struct is_rom_info *finfo_front = NULL;
	char *cal_buf;

	is_sec_get_cal_buf(&cal_buf, ROM_ID_REAR);
	is_sec_get_sysfs_finfo(&finfo, ROM_ID_REAR);
	is_sec_get_sysfs_finfo(&finfo_front, ROM_ID_FRONT);

	if (finfo_front->header_ver[FW_SENSOR_MAKER] == FW_SENSOR_MAKER_SLSI) {
		finfo->front_setfile_start_addr = *((u32 *)&cal_buf[ROM_HEADER_ISP_FRONT_SETFILE_LL_START_ADDR]);
		finfo->front_setfile_end_addr = *((u32 *)&cal_buf[ROM_HEADER_ISP_FRONT_SETFILE_LL_END_ADDR]);
	} else if (finfo_front->header_ver[FW_SENSOR_MAKER] == FW_SENSOR_MAKER_SONY) {
		finfo->front_setfile_start_addr = *((u32 *)&cal_buf[ROM_HEADER_ISP_FRONT_SETFILE_LS_START_ADDR]);
		finfo->front_setfile_end_addr = *((u32 *)&cal_buf[ROM_HEADER_ISP_FRONT_SETFILE_LS_END_ADDR]);
	} else {
		finfo->front_setfile_start_addr = *((u32 *)&cal_buf[ROM_HEADER_ISP_FRONT_SETFILE_LL_START_ADDR]);
		finfo->front_setfile_end_addr = *((u32 *)&cal_buf[ROM_HEADER_ISP_FRONT_SETFILE_LL_END_ADDR]);
	}
	info("Front setfile start = 0x%08x, end = 0x%08x\n",
		(finfo->front_setfile_start_addr), (finfo->front_setfile_end_addr));
	finfo->front_setfile_size = (unsigned long)(finfo->front_setfile_end_addr - finfo->front_setfile_start_addr + 1);
	info("front setfile_size = %ld\n", finfo->front_setfile_size);
}

#ifdef CAMERA_MODULE_FRONT_SETF_DUMP
int is_sec_get_front_setf_name(struct is_core *core)
{
	int ret = 0;
	struct is_module_enum *module = NULL;
	int i = 0;
	struct is_rom_info *finfo = NULL;
	is_sec_get_sysfs_finfo(&finfo, ROM_ID_REAR);

	for (i = 0; i < IS_SENSOR_COUNT; i++) {
		is_search_sensor_module_with_position(&core->sensor[i], SENSOR_POSITION_FRONT, &module);
		if (module)
			break;
	}

	if (!module) {
		err("%s: Could not find sensor id.", __func__);
		ret = -EINVAL;
		goto p_err;
	}

	memcpy(finfo->load_front_setfile_name, module->setfile_name,
		sizeof(finfo->load_front_setfile_name));

p_err:
	return ret;
}
#endif

#ifdef USE_RTA_BINARY
int is_sec_readfw_rta(struct is_core *core)
{
	int ret = 0;
	loff_t pos = 0;
	char fw_path[100];
	int retry = IS_FW_RETRY_CNT;
	u32 checksum_seed;
#ifdef CAMERA_MODULE_COMPRESSED_FW_DUMP
	u8 *buf = NULL;
#endif
	struct is_rom_info *finfo = NULL;
	struct is_rom_info *finfo_front = NULL;

	is_sec_get_sysfs_finfo(&finfo, ROM_ID_REAR);
	is_sec_get_sysfs_finfo(&finfo_front, ROM_ID_FRONT);

	info("RTA FW need to be dumped\n");

crc_retry:
	/* read fw data */
	if (IS_MAX_FW_BUFFER_SIZE >= IS_MAX_FW_SIZE) {
		info("Start SPI read rta fw data\n");
		memset(fw_buf, 0x0, IS_MAX_FW_SIZE);
		ret = is_spi_read(&core->spi0, fw_buf, finfo->rta_bin_start_addr, IS_MAX_RTA_FW_SIZE);
		if (ret) {
			err("failed to is_spi_read (%d)\n", ret);
			ret = -EINVAL;
			goto exit;
		}
		info("End SPI read rta fw data %x %ld\n", finfo->rta_bin_start_addr, finfo->rta_fw_size);

		if (finfo_front->header_ver[FW_SENSOR_MAKER] == FW_SENSOR_MAKER_SLSI)
			checksum_seed = CHECKSUM_SEED_ISP_FW_RTA_LY;
		else
			checksum_seed = CHECKSUM_SEED_ISP_FW_RTA_LS;

		checksum_seed -= finfo->rta_bin_start_addr;

		/* CRC check */
		if (!is_sec_check_fw_crc32(fw_buf, checksum_seed, finfo->rta_fw_size) && (retry > 0)) {
			retry--;
			goto crc_retry;
		} else if (!retry) {
			ret = -EINVAL;
			err("RTA FW Data has dumped fail.. CRC ERROR ! \n");
			goto exit;
		}

		snprintf(fw_path, sizeof(fw_path), "%s%s", IS_FW_DUMP_PATH, finfo->load_rta_fw_name);
		pos = 0;

#ifdef CAMERA_MODULE_COMPRESSED_FW_DUMP
		buf = (u8 *)fw_buf;
		is_sec_inflate_fw(&buf, &finfo->rta_fw_size);
#endif
		if (write_data_to_file(fw_path, fw_buf, finfo->rta_fw_size, &pos) < 0) {
			ret = -EIO;
			goto exit;
		}

		info("RTA FW Data has dumped successfully\n");
	} else {
		err("FW size is larger than FW buffer.\n");
		BUG();
	}

exit:
	return ret;
}
#endif

int is_sec_readfw(struct is_core *core)
{
	int ret = 0;
	loff_t pos = 0;
	char fw_path[100];
	int retry = IS_FW_RETRY_CNT;
	u32 checksum_seed;
#ifdef CAMERA_MODULE_COMPRESSED_FW_DUMP
	u8 *buf = NULL;
#endif
	struct is_rom_info *finfo = NULL;
	is_sec_get_sysfs_finfo(&finfo, ROM_ID_REAR);

	info("FW need to be dumped\n");

crc_retry:
	/* read fw data */
	if (IS_MAX_FW_BUFFER_SIZE >= IS_MAX_FW_SIZE) {
		info("Start SPI read fw data\n");
		memset(fw_buf, 0x0, IS_MAX_FW_SIZE);
		ret = is_spi_read(&core->spi0, fw_buf, finfo->bin_start_addr, IS_MAX_FW_SIZE);
		if (ret) {
			err("failed to is_spi_read (%d)\n", ret);
			ret = -EINVAL;
			goto exit;
		}
		info("End SPI read fw data \n");

		checksum_seed = CHECKSUM_SEED_ISP_FW - finfo->bin_start_addr;

		/* CRC check */
		if (!is_sec_check_fw_crc32(fw_buf, checksum_seed, finfo->fw_size) && (retry > 0)) {
			retry--;
			goto crc_retry;
		} else if (!retry) {
			ret = -EINVAL;
			err("FW Data has dumped fail.. CRC ERROR ! \n");
			goto exit;
		}

		snprintf(fw_path, sizeof(fw_path), "%s%s", IS_FW_DUMP_PATH, finfo->load_fw_name);
		pos = 0;

#ifdef CAMERA_MODULE_COMPRESSED_FW_DUMP
		buf = (u8 *)fw_buf;
		is_sec_inflate_fw(&buf, &finfo->fw_size);
#endif
		if (write_data_to_file(fw_path, fw_buf, finfo->fw_size, &pos) < 0) {
			ret = -EIO;
			goto exit;
		}

		info("FW Data has dumped successfully\n");
	} else {
		err("FW size is larger than FW buffer.\n");
		BUG();
	}

#ifdef USE_RTA_BINARY
	ret = is_sec_readfw_rta(core);
#endif

exit:
	return ret;
}

int is_sec_read_setfile(struct is_core *core)
{
	int ret = 0;
	loff_t pos = 0;
	char setfile_path[100];
	int retry = IS_FW_RETRY_CNT;
	u32 read_size = 0;
#ifdef CAMERA_MODULE_COMPRESSED_FW_DUMP
	u8 *buf = NULL;
#endif
	struct is_rom_info *finfo = NULL;
	is_sec_get_sysfs_finfo(&finfo, ROM_ID_REAR);

	info("Setfile need to be dumped\n");

setfile_crc_retry:
	/* read setfile data */
	info("Start SPI read setfile data\n");
	memset(fw_buf, 0x0, IS_MAX_FW_BUFFER_SIZE);

	if (finfo->header_ver[FW_SENSOR_MAKER] == FW_SENSOR_MAKER_SLSI ||
		finfo->header_ver[FW_SENSOR_MAKER] == FW_SENSOR_MAKER_SLSI_SONY)
		read_size = IS_MAX_SETFILE_SIZE_LL;
	else
		read_size = IS_MAX_SETFILE_SIZE_LS;

	ret = is_spi_read(&core->spi0, fw_buf, finfo->setfile_start_addr,
			read_size);
	if (ret) {
		err("failed to is_spi_read (%d)\n", ret);
		ret = -EINVAL;
		goto exit;
	}
	info("End SPI read setfile data %x %ld\n", finfo->setfile_start_addr, finfo->setfile_size);

	/* CRC check */
	if (!is_sec_check_setfile_crc32(fw_buf) && (retry > 0)) {
		retry--;
		goto setfile_crc_retry;
	} else if (!retry) {
		err("Setfile has dumped fail.. CRC ERROR ! \n");
		ret = -EINVAL;
		goto exit;
	}

	snprintf(setfile_path, sizeof(setfile_path), "%s%s", IS_FW_DUMP_PATH, finfo->load_setfile_name);
	pos = 0;

#ifdef CAMERA_MODULE_COMPRESSED_FW_DUMP
	buf = (u8 *)fw_buf;
	is_sec_inflate_fw(&buf, &finfo->setfile_size);
#endif
	if (write_data_to_file(setfile_path, fw_buf, finfo->setfile_size, &pos) < 0) {
		ret = -EIO;
		goto exit;
	}

	info("Setfile has dumped successfully\n");

exit:
	return ret;
}
#ifdef CAMERA_MODULE_REAR2_SETF_DUMP
int is_sec_read_setfile2(struct is_core *core)
{
	int ret = 0;
	loff_t pos = 0;
	char setfile_path[100];
	int retry = IS_FW_RETRY_CNT;
	u32 read_size = 0;
#ifdef CAMERA_MODULE_COMPRESSED_FW_DUMP
	u8 *buf = NULL;
#endif
	struct is_rom_info *finfo = NULL;
	is_sec_get_sysfs_finfo(&finfo, ROM_ID_REAR);

	info("Setfile2 need to be dumped\n");

setfile_crc_retry:
	/* read setfile data */
	info("Start SPI read setfile2 data\n");
	memset(fw_buf, 0x0, IS_MAX_FW_BUFFER_SIZE);

	if (finfo->header_ver[FW_SENSOR_MAKER] == FW_SENSOR_MAKER_SLSI ||
		finfo->header_ver[FW_SENSOR_MAKER] == FW_SENSOR_MAKER_SLSI_SONY)
		read_size = IS_MAX_SETFILE_SIZE_LL;
	else
		read_size = IS_MAX_SETFILE_SIZE_LS;

	ret = is_spi_read(&core->spi0, fw_buf, finfo->setfile2_start_addr,
			read_size);
	if (ret) {
		err("failed to is_spi_read (%d)\n", ret);
		ret = -EINVAL;
		goto exit;
	}
	info("End SPI read setfile2 data %x %ld\n", finfo->setfile2_start_addr, finfo->setfile2_size);

	/* CRC check */
	if (!is_sec_check_setfile2_crc32(fw_buf) && (retry > 0)) {
		retry--;
		goto setfile_crc_retry;
	} else if (!retry) {
		err("Setfile2 has dumped fail.. CRC ERROR !\n");
		ret = -EINVAL;
		goto exit;
	}

	snprintf(setfile_path, sizeof(setfile_path), "%s%s", IS_FW_DUMP_PATH, finfo->load_setfile2_name);
	pos = 0;

#ifdef CAMERA_MODULE_COMPRESSED_FW_DUMP
	buf = (u8 *)fw_buf;
	is_sec_inflate_fw(&buf, &finfo->setfile2_size);
#endif
	if (write_data_to_file(setfile_path, fw_buf, finfo->setfile2_size, &pos) < 0) {
		ret = -EIO;
		goto exit;
	}

	info("Setfile2 has dumped successfully\n");

exit:
	return ret;
}
#endif

#ifdef CAMERA_MODULE_FRONT_SETF_DUMP
int is_sec_read_front_setfile(struct is_core *core)
{
	int ret = 0;
	loff_t pos = 0;
	char setfile_path[100];
	int retry = IS_FW_RETRY_CNT;
	u32 read_size = 0;
#ifdef CAMERA_MODULE_COMPRESSED_FW_DUMP
	u8 *buf = NULL;
#endif
	struct is_rom_info *finfo = NULL;
	struct is_rom_info *finfo_front = NULL;

	is_sec_get_sysfs_finfo(&finfo, ROM_ID_REAR);
	is_sec_get_sysfs_finfo(&finfo_front, ROM_ID_FRONT);

	info("Front setfile need to be dumped\n");

setfile_crc_retry:
	/* read setfile data */
	info("Start SPI read front setfile data\n");
	memset(fw_buf, 0x0, IS_MAX_FW_BUFFER_SIZE);

	if (finfo_front->header_ver[FW_SENSOR_MAKER] == FW_SENSOR_MAKER_SLSI)
		read_size = IS_MAX_SETFILE_SIZE_FRONT_LL;
	else
		read_size = IS_MAX_SETFILE_SIZE_FRONT_LS;

	ret = is_spi_read(&core->spi0, fw_buf, finfo->front_setfile_start_addr,
			read_size);

	if (ret) {
		err("failed to is_spi_read (%d)\n", ret);
		ret = -EINVAL;
		goto exit;
	}
	info("End SPI read front setfile data\n");

	/* CRC check */
	if (!is_sec_check_front_setfile_crc32(fw_buf) && (retry > 0)) {
		retry--;
		goto setfile_crc_retry;
	} else if (!retry) {
		ret = -EINVAL;
		err("Front setfile has dumped fail.. CRC ERROR ! \n");
		goto exit;
	}

	is_sec_get_front_setf_name(core);
	snprintf(setfile_path, sizeof(setfile_path), "%s%s", IS_FW_DUMP_PATH,
		finfo->load_front_setfile_name);
	pos = 0;

#ifdef CAMERA_MODULE_COMPRESSED_FW_DUMP
	buf = (u8 *)fw_buf;
	is_sec_inflate_fw(&buf, &finfo->front_setfile_size);
#endif
	if (write_data_to_file(setfile_path, fw_buf, finfo->front_setfile_size, &pos) < 0) {
		ret = -EIO;
		goto exit;
	}

	info("Front setfile has dumped successfully\n");

exit:
	return ret;
}
#endif



#ifdef CAMERA_MODULE_COMPRESSED_FW_DUMP
int is_sec_inflate_fw(u8 **buf, unsigned long *size)
{
	z_stream zs_inflate;
	int ret = 0;
	char *unzip_buf;

	unzip_buf = vmalloc(IS_MAX_FW_BUFFER_SIZE);
	if (!unzip_buf) {
		err("failed to allocate memory\n");
		ret = -ENOMEM;
		goto exit;
	}
	memset(unzip_buf, 0x0, IS_MAX_FW_BUFFER_SIZE);

	zs_inflate.workspace = vmalloc(zlib_inflate_workspacesize());
	ret = zlib_inflateInit2(&zs_inflate, -MAX_WBITS);
	if (ret != Z_OK) {
		err("Camera : inflateInit error\n");
	}

	zs_inflate.next_in = *buf;
	zs_inflate.next_out = unzip_buf;
	zs_inflate.avail_in = *size;
	zs_inflate.avail_out = IS_MAX_FW_BUFFER_SIZE;

	ret = zlib_inflate(&zs_inflate, Z_NO_FLUSH);
	if (ret != Z_STREAM_END) {
		err("Camera : zlib_inflate error %d \n", ret);
	}

	zlib_inflateEnd(&zs_inflate);
	vfree(zs_inflate.workspace);

	*size = IS_MAX_FW_BUFFER_SIZE - zs_inflate.avail_out;
	memset(*buf, 0x0, IS_MAX_FW_BUFFER_SIZE);
	memcpy(*buf, unzip_buf, *size);
	vfree(unzip_buf);

exit:
	return ret;
}
#endif
#endif
#endif

int is_sec_get_pixel_size(char *header_ver)
{
	int pixelsize = 0;

	pixelsize += (int) (header_ver[FW_PIXEL_SIZE] - 0x30) * 10;
	pixelsize += (int) (header_ver[FW_PIXEL_SIZE + 1] - 0x30);

	return pixelsize;
}

int is_sec_core_voltage_select(struct device *dev, char *header_ver)
{
	struct regulator *regulator = NULL;
	int ret = 0;
	int minV, maxV;
	int pixelSize = 0;

	regulator = regulator_get(dev, "cam_sensor_core_1.2v");
	if (IS_ERR_OR_NULL(regulator)) {
		err("%s : regulator_get fail",
			__func__);
		return -EINVAL;
	}
	pixelSize = is_sec_get_pixel_size(header_ver);

	if (header_ver[FW_SENSOR_MAKER] == FW_SENSOR_MAKER_SONY ||
		header_ver[FW_SENSOR_MAKER] == FW_SENSOR_MAKER_SONY_LSI) {
		if (pixelSize == 13) {
			minV = 1050000;
			maxV = 1050000;
		} else if (pixelSize == 8) {
			minV = 1100000;
			maxV = 1100000;
		} else {
			minV = 1050000;
			maxV = 1050000;
		}
	} else if (header_ver[FW_SENSOR_MAKER] == FW_SENSOR_MAKER_SLSI ||
				header_ver[FW_SENSOR_MAKER] == FW_SENSOR_MAKER_SLSI_SONY) {
		minV = 1200000;
		maxV = 1200000;
	} else {
		minV = 1050000;
		maxV = 1050000;
	}

	ret = regulator_set_voltage(regulator, minV, maxV);

	if (ret >= 0)
		info("%s : set_core_voltage %d, %d successfully\n",
				__func__, minV, maxV);
	regulator_put(regulator);

	return ret;
}

#if defined(CONFIG_CAMERA_FROM)
int is_sec_check_bin_files(struct is_core *core)
{
	int ret = 0;
#ifdef USE_KERNEL_VFS_READ_WRITE
	char fw_path[100];
	char dump_fw_path[100];
	char dump_fw_version[IS_HEADER_VER_SIZE + 1] = {0, };
	char phone_fw_version[IS_HEADER_VER_SIZE + 1] = {0, };
#if defined(CONFIG_CAMERA_FROM) && defined(CAMERA_MODULE_DUALIZE)
	int from_fw_revision = 0;
	int dump_fw_revision = 0;
	int phone_fw_revision = 0;
	bool dump_flag = false;
	struct file *setfile_fp = NULL;
	char setfile_path[100];
#else
	static char fw_buf[IS_MAX_FW_BUFFER_SIZE];
#endif
	struct file *fp = NULL;
	mm_segment_t old_fs;
	long fsize, nread;
	u8 *temp_buf = NULL;
	bool is_dump_existed = false;
	bool is_dump_needed = false;
	struct is_vender_specific *specific = core->vender.private_data;
	struct is_spi *spi = &core->spi0;
	struct is_rom_info *finfo = NULL;
	struct is_rom_info *pinfo = NULL;
	bool camera_running;

	camera_running = is_vendor_check_camera_running(SENSOR_POSITION_REAR);

	is_sec_get_sysfs_finfo(&finfo, ROM_ID_REAR);
	is_sec_get_sysfs_pinfo(&pinfo, ROM_ID_REAR);

	/* Use mutex for spi read */
	mutex_lock(&specific->rom_lock);

	if (test_bit(IS_ROM_STATE_CHECK_BIN_DONE, &finfo->rom_state) && !force_caldata_dump) {
		mutex_unlock(&specific->rom_lock);
		return 0;
	}

	is_set_fw_names(finfo->load_fw_name, finfo->load_rta_fw_name);

	memset(fw_buf, 0x0, IS_HEADER_VER_SIZE+10);
	temp_buf = fw_buf;

	is_sec_rom_power_on(core, SENSOR_POSITION_REAR);
	usleep_range(1000, 1000);

	/* Set SPI function */
	is_spi_s_pin(spi, SPI_PIN_STATE_ISP_FW); //spi-chip-select-mode required

	ret = is_spi_reset(&core->spi0);

#if defined(CONFIG_CAMERA_FROM) && defined(CAMERA_MODULE_DUALIZE)
	is_sec_get_rta_bin_addr();
	is_sec_get_rear_setfile_addr();
	is_sec_get_front_setfile_addr();
#ifdef CAMERA_MODULE_REAR2_SETF_DUMP
	is_sec_get_rear2_setfile_addr();
#endif
#endif

	snprintf(fw_path, sizeof(fw_path), "%s%s", IS_FW_PATH, finfo->load_rta_fw_name);

	snprintf(dump_fw_path, sizeof(dump_fw_path), "%s%s",
		IS_FW_DUMP_PATH, finfo->load_rta_fw_name);
	info("Camera: f-rom fw version: %s\n", finfo->header_ver);

	fp = filp_open(dump_fw_path, O_RDONLY, 0);
	if (IS_ERR(fp)) {
		info("Camera: There is no dumped firmware(%s)\n", dump_fw_path);
		is_dump_existed = false;
		goto read_phone_fw;
	} else {
		is_dump_existed = true;
	}

	fsize = fp->f_path.dentry->d_inode->i_size;
	info("start, file path %s, size %ld Bytes\n",
		dump_fw_path, fsize);

	fp->f_pos = fsize - IS_HEADER_VER_OFFSET;
	fsize = IS_HEADER_VER_SIZE;
	nread = kernel_read(fp, temp_buf, fsize, &fp->f_pos);
	if (nread != fsize) {
		err("failed to read firmware file, %ld Bytes", nread);
		ret = -EIO;
		goto read_phone_fw;
	}

	strncpy(dump_fw_version, temp_buf, IS_HEADER_VER_SIZE);
	info("Camera: dumped fw version: %s\n", dump_fw_version);

read_phone_fw:
	if (fp && is_dump_existed) {
		filp_close(fp, current->files);
		fp = NULL;
	}

	if (ret < 0)
		goto exit;

	fp = filp_open(fw_path, O_RDONLY, 0);
	if (IS_ERR(fp)) {
		err("Camera: Failed open phone firmware(%s)", fw_path);
		fp = NULL;
		goto read_phone_fw_exit;
	}

	fsize = fp->f_path.dentry->d_inode->i_size;
	info("start, file path %s, size %ld Bytes\n", fw_path, fsize);

	fp->f_pos = fsize - IS_HEADER_VER_OFFSET;
	fsize = IS_HEADER_VER_SIZE;
	nread = kernel_read(fp, temp_buf, fsize, &fp->f_pos);
	if (nread != fsize) {
		err("failed to read firmware file, %ld Bytes", nread);
		ret = -EIO;
		goto read_phone_fw_exit;
	}

	strncpy(phone_fw_version, temp_buf, IS_HEADER_VER_SIZE);
	strncpy(pinfo->header_ver, temp_buf, IS_HEADER_VER_SIZE);
	info("Camera: phone fw version: %s\n", phone_fw_version);

read_phone_fw_exit:
	if (fp) {
		filp_close(fp, current->files);
		fp = NULL;
	}

	if (ret < 0)
		goto exit;

#if defined(CAMERA_MODULE_DUALIZE) && defined(CAMERA_MODULE_AVAILABLE_DUMP_VERSION)
	if (!strncmp(CAMERA_MODULE_AVAILABLE_DUMP_VERSION, finfo->header_ver, 3)) {
		from_fw_revision = is_sec_fw_revision(finfo->header_ver);
		phone_fw_revision = is_sec_fw_revision(phone_fw_version);
		if (is_dump_existed) {
			dump_fw_revision = is_sec_fw_revision(dump_fw_version);
		}

		info("from_fw_revision = %d, phone_fw_revision = %d, dump_fw_revision = %d\n",
			from_fw_revision, phone_fw_revision, dump_fw_revision);

		if (is_sec_compare_ver(ROM_ID_REAR) >= 0 /* Check if a module is connected or not */
			&& (!is_sec_fw_module_compare_for_dump(finfo->header_ver, phone_fw_version) ||
				(from_fw_revision > phone_fw_revision))) {
			is_dumped_fw_loading_needed = true;
			if (is_dump_existed) {
				if (!is_sec_fw_module_compare_for_dump(finfo->header_ver,
							dump_fw_version)) {
					is_dump_needed = true;
				} else if (from_fw_revision > dump_fw_revision) {
					is_dump_needed = true;
				} else {
					is_dump_needed = false;
				}
			} else {
				is_dump_needed = true;
			}
		} else {
			is_dump_needed = false;
			if (is_dump_existed) {
				if (!is_sec_fw_module_compare_for_dump(phone_fw_version,
					dump_fw_version)) {
					is_dumped_fw_loading_needed = false;
				} else if (phone_fw_revision > dump_fw_revision) {
					is_dumped_fw_loading_needed = false;
				} else {
					is_dumped_fw_loading_needed = true;
				}
			} else {
				is_dumped_fw_loading_needed = false;
			}
		}

		if (force_caldata_dump) {
			if ((!is_sec_fw_module_compare_for_dump(finfo->header_ver, phone_fw_version))
				|| (from_fw_revision > phone_fw_revision))
				dump_flag = true;
		} else {
			if (is_dump_needed) {
				dump_flag = true;
				set_bit(IS_CRC_ERROR_FIRMWARE, &finfo->crc_error);
				set_bit(IS_CRC_ERROR_SETFILE_1, &finfo->crc_error);
#ifdef CAMERA_MODULE_FRONT_SETF_DUMP
				set_bit(IS_CRC_ERROR_SETFILE_2, &finfo->crc_error);
#endif
			}
		}

		if (dump_flag) {
			info("Dump ISP Firmware.\n");

			ret = is_sec_readfw(core);
			msleep(20);
			ret |= is_sec_read_setfile(core);
#ifdef CAMERA_MODULE_REAR2_SETF_DUMP
			ret |= is_sec_read_setfile2(core);
#endif
#ifdef CAMERA_MODULE_FRONT_SETF_DUMP
			ret |= is_sec_read_front_setfile(core);
#endif
			if (ret < 0) {
				if (test_bit(IS_CRC_ERROR_FIRMWARE, &finfo->crc_error)
					|| test_bit(IS_CRC_ERROR_SETFILE_1, &finfo->crc_error)
#ifdef CAMERA_MODULE_FRONT_SETF_DUMP
					|| test_bit(IS_CRC_ERROR_SETFILE_2, &finfo->crc_error)
#endif
					) {
					is_dumped_fw_loading_needed = false;
					err("Firmware CRC is not valid. Does not use dumped firmware.\n");
				}
			}
		}

		if (phone_fw_version[0] == 0) {
			strcpy(pinfo->header_ver, "NULL");
		}

		if (is_dumped_fw_loading_needed) {
			old_fs = get_fs();
			set_fs(KERNEL_DS);
			snprintf(setfile_path, sizeof(setfile_path), "%s%s",
				IS_FW_DUMP_PATH, finfo->load_setfile_name);
			setfile_fp = filp_open(setfile_path, O_RDONLY, 0);
			if (IS_ERR_OR_NULL(setfile_fp)) {
				set_bit(IS_CRC_ERROR_SETFILE_1, &finfo->crc_error);
				info("setfile does not exist. Retry setfile dump.\n");

				is_sec_read_setfile(core);
				setfile_fp = NULL;
			} else {
				if (setfile_fp) {
					filp_close(setfile_fp, current->files);
				}
				setfile_fp = NULL;
			}

#ifdef CAMERA_MODULE_FRONT_SETF_DUMP
			memset(setfile_path, 0x0, sizeof(setfile_path));
			is_sec_get_front_setf_name(core);
			snprintf(setfile_path, sizeof(setfile_path), "%s%s",
				IS_FW_DUMP_PATH, finfo->load_front_setfile_name);
			setfile_fp = filp_open(setfile_path, O_RDONLY, 0);
			if (IS_ERR_OR_NULL(setfile_fp)) {
				set_bit(IS_CRC_ERROR_SETFILE_2, &finfo->crc_error);
				info("setfile does not exist. Retry front setfile dump.\n");

				is_sec_read_front_setfile(core);
				setfile_fp = NULL;
			} else {
				if (setfile_fp) {
					filp_close(setfile_fp, current->files);
				}
				setfile_fp = NULL;
			}
#endif
			set_fs(old_fs);
		}
	}
#endif

	if (is_dump_needed && is_dumped_fw_loading_needed) {
		strncpy(loaded_fw, finfo->header_ver, IS_HEADER_VER_SIZE);
	} else if (!is_dump_needed && is_dumped_fw_loading_needed) {
		strncpy(loaded_fw, dump_fw_version, IS_HEADER_VER_SIZE);
	} else {
		strncpy(loaded_fw, phone_fw_version, IS_HEADER_VER_SIZE);
	}

exit:
	/* Set spi pin to out */
	is_spi_s_pin(spi, SPI_PIN_STATE_IDLE);

	set_bit(IS_ROM_STATE_CHECK_BIN_DONE, &finfo->rom_state);

	if ((specific->f_rom_power == FROM_POWER_SOURCE_REAR_SECOND && !camera_running) ||
		(specific->f_rom_power == FROM_POWER_SOURCE_REAR && !camera_running)) {
		is_sec_rom_power_off(core, SENSOR_POSITION_REAR);
	}

	mutex_unlock(&specific->rom_lock);
#endif
	return 0;
}
#endif

int is_sec_sensorid_find(struct is_core *core)
{
	struct is_vender_specific *specific = core->vender.private_data;
	struct is_rom_info *finfo = NULL;

	is_sec_get_sysfs_finfo(&finfo, ROM_ID_REAR);

	snprintf(finfo->load_fw_name, sizeof(IS_DDK), "%s", IS_DDK);

	if (is_sec_fw_module_compare(finfo->header_ver, FW_IMX582)) {
		specific->sensor_id[SENSOR_POSITION_REAR] = SENSOR_NAME_IMX582;
	}
	info("%s sensor id %d\n", __func__, specific->sensor_id[SENSOR_POSITION_REAR]);

	return 0;
}

int is_sec_sensorid_find_front(struct is_core *core)
{
	struct is_vender_specific *specific = core->vender.private_data;
	struct is_rom_info *finfo = NULL;

	is_sec_get_sysfs_finfo(&finfo, ROM_ID_FRONT);

	if (is_sec_fw_module_compare(finfo->header_ver, FW_IMX576_C)) {
		specific->sensor_id[SENSOR_POSITION_FRONT] = SENSOR_NAME_IMX576;
	} else if (is_sec_fw_module_compare(finfo->header_ver, FW_IMX616_PAPAYA)) {
		specific->sensor_id[SENSOR_POSITION_FRONT] = SENSOR_NAME_IMX616;
	} else if (is_sec_fw_module_compare(finfo->header_ver, FW_S5KGD2)) {
		specific->sensor_id[SENSOR_POSITION_FRONT] = SENSOR_NAME_S5KGD2;
	}

	info("%s sensor id %d\n", __func__, specific->sensor_id[SENSOR_POSITION_FRONT]);

	return 0;
}


int is_get_dual_cal_buf(int slave_position, char **buf, int *size)
{
	char *cal_buf;
	int rom_dual_cal_start_addr;
	int rom_dual_cal_size;
	u32 rom_dual_flag_dummy_addr = 0;
	int rom_type;
	int rom_dualcal_id;
	int rom_dualcal_index;
	struct is_core *core = is_get_is_core();
	struct is_rom_info *finfo = NULL;

	*buf = NULL;
	*size = 0;

	is_vendor_get_rom_dualcal_info_from_position(slave_position, &rom_type, &rom_dualcal_id, &rom_dualcal_index);
	if (rom_type == ROM_TYPE_NONE) {
		err("[rom_dualcal_id:%d pos:%d] not support, no rom for camera", rom_dualcal_id, slave_position);
		return -EINVAL;
	} else if (rom_dualcal_id == ROM_ID_NOTHING) {
		err("[rom_dualcal_id:%d pos:%d] invalid ROM ID", rom_dualcal_id, slave_position);
		return -EINVAL;
	}

	is_sec_get_cal_buf(&cal_buf, rom_dualcal_id);
	if (is_sec_check_rom_ver(core, rom_dualcal_id) == false) {
		err("[rom_dualcal_id:%d pos:%d] ROM version is low. Cannot load dual cal.",
			rom_dualcal_id, slave_position);
		return -EINVAL;
	}

	is_sec_get_sysfs_finfo(&finfo, rom_dualcal_id);
	if (test_bit(IS_CRC_ERROR_ALL_SECTION, &finfo->crc_error) ||
		test_bit(IS_CRC_ERROR_DUAL_CAMERA, &finfo->crc_error)) {
		err("[rom_dualcal_id:%d pos:%d] ROM Cal CRC is wrong. Cannot load dual cal.",
			rom_dualcal_id, slave_position);
		return -EINVAL;
	}

	if (rom_dualcal_index == ROM_DUALCAL_SLAVE0) {
		rom_dual_cal_start_addr = finfo->rom_dualcal_slave0_start_addr;
		rom_dual_cal_size = finfo->rom_dualcal_slave0_size;
	} else if (rom_dualcal_index == ROM_DUALCAL_SLAVE1) {
		rom_dual_cal_start_addr = finfo->rom_dualcal_slave1_start_addr;
		rom_dual_cal_size = finfo->rom_dualcal_slave1_size;
		rom_dual_flag_dummy_addr = finfo->rom_dualcal_slave1_dummy_flag_addr;
	} else {
		err("[index:%d] not supported index.", rom_dualcal_index);
		return -EINVAL;
	}

	if (rom_dual_flag_dummy_addr != 0 && cal_buf[rom_dual_flag_dummy_addr] != 7) {
		err("[rom_dualcal_id:%d pos:%d addr:%x] invalid dummy_flag [%d]. Cannot load dual cal.",
			rom_dualcal_id, slave_position, rom_dual_flag_dummy_addr, cal_buf[rom_dual_flag_dummy_addr]);
		return -EINVAL;
	}

	if (rom_dual_cal_start_addr <= 0) {
		info("[%s] not available dual_cal\n", __func__);
		return -EINVAL;
	}

	*buf = &cal_buf[rom_dual_cal_start_addr];
	*size = rom_dual_cal_size;

	return 0;
}

int is_sec_fw_find(struct is_core *core)
{
	struct is_vender_specific *specific = core->vender.private_data;
	int front_id = specific->sensor_id[SENSOR_POSITION_FRONT];
	int rear_id = specific->sensor_id[SENSOR_POSITION_REAR];
	struct is_rom_info *finfo = NULL;
	struct is_rom_info *finfo_front = NULL;

	is_sec_get_sysfs_finfo(&finfo, ROM_ID_REAR);
	is_sec_get_sysfs_finfo(&finfo_front, ROM_ID_FRONT);

	if (test_bit(IS_ROM_STATE_FW_FIND_DONE, &finfo->rom_state) && !force_caldata_dump)
		return 0;

	switch (rear_id) {
	case SENSOR_NAME_IMX582:
		snprintf(finfo_front->load_front_setfile_name, sizeof(IS_IMX582_SETF), "%s", IS_IMX582_SETF);
		break;
#if IS_REACHABLE(CONFIG_CAMERA_OTPROM_SUPPORT_REAR)
	case SENSOR_NAME_GC5035:
		snprintf(finfo_front->load_front_setfile_name, sizeof(IS_GC5035_SETF), "%s", IS_GC5035_SETF);
		break;
	case SENSOR_NAME_GC02M1:
		snprintf(finfo_front->load_front_setfile_name, sizeof(IS_GC02M1_SETF), "%s", IS_GC02M1_SETF);
		break;
#endif
	default:
		snprintf(finfo->load_setfile_name, sizeof(IS_IMX582_SETF), "%s", IS_IMX582_SETF);
		snprintf(finfo->load_rta_fw_name, sizeof(IS_RTA), "%s", IS_RTA);
		break;
	}

	switch (front_id) {
	case SENSOR_NAME_IMX616:
		snprintf(finfo_front->load_front_setfile_name, sizeof(IS_IMX616_SETF), "%s", IS_IMX616_SETF);
		break;
	case SENSOR_NAME_S5KGD2:
		snprintf(finfo_front->load_front_setfile_name, sizeof(IS_GD2_SETF), "%s", IS_GD2_SETF);
		break;
	default:
		snprintf(finfo_front->load_front_setfile_name, sizeof(IS_IMX616_SETF), "%s", IS_IMX616_SETF);
		break;
	}

	info("%s rta name is %s\n", __func__, finfo->load_rta_fw_name);
	set_bit(IS_ROM_STATE_FW_FIND_DONE, &finfo->rom_state);

	return 0;
}

#if IS_REACHABLE(CONFIG_CAMERA_OTPROM_SUPPORT_REAR)
int is_sec_fw_sel_rom(int rom_id, bool headerOnly)
{
	int ret;
	struct is_rom_info *finfo = NULL;
	struct is_module_enum *module;
	int rom_type;
	int position = is_vendor_get_position_from_rom_id(rom_id);

	if (position == SENSOR_POSITION_MAX) {
		ret = -EINVAL;
		goto EXIT;
	}

	is_sec_get_sysfs_finfo(&finfo, rom_id);

	if (!finfo) {
		err("%s: There is no cal map (rom_id : %d)\n", __func__, rom_id);
		ret = false;
		goto EXIT;
	}

	is_vendor_get_module_from_position(position, &module);

	if (!module) {
		err("%s: There is no module (rom_id : %d)\n", __func__, rom_id);
		ret = false;
		goto EXIT;
	}
	rom_type = module->pdata->rom_type;

	if (rom_type == ROM_TYPE_EEPROM)
		ret = is_sec_fw_sel_eeprom(rom_id, headerOnly);
	else if (rom_type == ROM_TYPE_OTPROM)
		ret = is_sec_fw_sel_otprom(rom_id, headerOnly);
	else {
		err("[%s] unknown rom_type:%d ",__func__,rom_type);
		ret = -EINVAL;
	}
EXIT:
	return ret;
}
#endif

#ifdef USE_PERSISTENT_DEVICE_PROPERTIES_FOR_CAL
int is_sec_run_fw_sel(int rom_id)
{
	struct is_core *core = is_get_is_core();
	int ret = 0;
	struct is_vender_specific *specific = core->vender.private_data;
	struct is_rom_info *finfo = NULL;

	is_sec_get_sysfs_finfo(&finfo, rom_id);

	if (!test_bit(IS_ROM_STATE_CAL_READ_DONE, &finfo->rom_state)) {
#if defined(CONFIG_CAMERA_FROM)
		ret = is_sec_fw_sel(core, false);
#else
#if IS_REACHABLE(CONFIG_CAMERA_OTPROM_SUPPORT_REAR)
			ret = is_sec_fw_sel_rom(rom_id, false);
#else
			ret = is_sec_fw_sel_eeprom(rom_id, false);
#endif
#endif
	}

	if (specific->check_sensor_vendor) {
		if (is_sec_check_rom_ver(core, rom_id)) {
			is_sec_check_module_state(finfo);

			if (test_bit(IS_ROM_STATE_OTHER_VENDOR, &finfo->rom_state)) {
				err("Not supported module. Rom ID = %d, Module ver = %s", rom_id, finfo->header_ver);
				return  -EIO;
			}
		}
	}

	return ret;
}

#else

int is_sec_run_fw_sel(int rom_id)
{
	struct is_core *core = is_get_is_core();
	int ret = 0;
	struct is_vender_specific *specific = core->vender.private_data;
	struct is_rom_info *finfo = NULL;
	struct is_rom_info *finfo_rear = NULL;

	is_sec_get_sysfs_finfo(&finfo, rom_id);

	/* IS_FW_DUMP_PATH folder cannot access until User unlock handset */
	if (!test_bit(IS_ROM_STATE_CAL_RELOAD, &finfo->rom_state)) {
		if (is_sec_file_exist(IS_FW_DUMP_PATH)) {
			/* Check reload cal data enabled */
			is_sec_check_reload(core);
			set_bit(IS_ROM_STATE_CAL_RELOAD, &finfo->rom_state);
			check_need_cal_dump = CAL_DUMP_STEP_CHECK;
			info("CAL_DUMP_STEP_CHECK");
		}
	}

	/* Check need to dump cal data */
	if (check_need_cal_dump == CAL_DUMP_STEP_CHECK) {
		if (test_bit(IS_ROM_STATE_CAL_READ_DONE, &finfo->rom_state)) {
#ifndef CONFIG_SAMSUNG_PRODUCT_SHIP
			is_sec_cal_dump(core);
#endif
			check_need_cal_dump = CAL_DUMP_STEP_DONE;
			info("CAL_DUMP_STEP_DONE");
		}
	}

	info("%s rom id[%d] %d\n", __func__, rom_id,
		test_bit(IS_ROM_STATE_CAL_READ_DONE, &finfo->rom_state));

	if (rom_id != ROM_ID_REAR) {
		is_sec_get_sysfs_finfo(&finfo_rear, ROM_ID_REAR);
		if (!test_bit(IS_ROM_STATE_CAL_READ_DONE, &finfo_rear->rom_state) || force_caldata_dump) {
#if IS_REACHABLE(CONFIG_CAMERA_OTPROM_SUPPORT_REAR)
			ret = is_sec_fw_sel_rom(ROM_ID_REAR, true);
#else
			ret = is_sec_fw_sel_eeprom(ROM_ID_REAR, true);
#endif
		}
	}

	if (!test_bit(IS_ROM_STATE_CAL_READ_DONE, &finfo->rom_state) || force_caldata_dump) {
#if defined(CONFIG_CAMERA_FROM)
		ret = is_sec_fw_sel(core, false);
#else
#ifdef CONFIG_SEC_CAL_ENABLE
#ifdef USES_STANDARD_CAL_RELOAD
		if (sec2lsi_reload) {
			if (rom_id == 0) {
				for (rom_id = ROM_ID_REAR; rom_id < ROM_ID_MAX; rom_id++) {
					if (specific->rom_valid[rom_id] == true) {
#if IS_REACHABLE(CONFIG_CAMERA_OTPROM_SUPPORT_REAR)
						ret = is_sec_fw_sel_rom(rom_id, false);
#else
						ret = is_sec_fw_sel_eeprom(rom_id, false);
#endif
						sec2lsi_conversion_done[rom_id] = false;
						info("sec2lsi reload for rom %d", rom_id);
						if (ret) {
							err("is_sec_run_fw_sel for [%d] is fail(%d)", rom_id, ret);
							return ret;
						}
					}
				}
			}
		} else
#endif
#endif
#if IS_REACHABLE(CONFIG_CAMERA_OTPROM_SUPPORT_REAR)
			ret = is_sec_fw_sel_rom(rom_id, false);
#else
			ret = is_sec_fw_sel_eeprom(rom_id, false);
#endif
#endif
	}

	if (specific->check_sensor_vendor) {
		if (is_sec_check_rom_ver(core, rom_id)) {
			is_sec_check_module_state(finfo);

			if (test_bit(IS_ROM_STATE_OTHER_VENDOR, &finfo->rom_state)) {
				err("Not supported module. Rom ID = %d, Module ver = %s", rom_id, finfo->header_ver);
				return  -EIO;
			}
		}
	}

	return ret;
}
#endif

int is_get_remosaic_cal_buf(int sensor_position, char **buf, int *size)
{
	int ret = -1;
	char *cal_buf;
	u32 start_addr;
	struct is_rom_info *finfo;
#ifdef MODIFY_CAL_MAP_FOR_SWREMOSAIC_LIB
/* to do check */
	int sensor_id = is_vendor_get_sensor_id_from_position(sensor_position);
#endif

	ret = is_sec_get_cal_buf(&cal_buf, sensor_position);
	if (ret < 0) {
		err("[%s]: get_cal_buf fail", __func__);
		return ret;
	}

	ret = is_sec_get_sysfs_finfo_by_position(sensor_position, &finfo);
	if (ret < 0) {
		err("[%s]: get_sysfs_finfo fail", __func__);
		return -EINVAL;
	}
	
	start_addr = finfo->rom_xtc_cal_data_start_addr;

#ifdef MODIFY_CAL_MAP_FOR_SWREMOSAIC_LIB
// Modify cal_buf for some sensors (if required)
	switch(sensor_id) {
		case SENSOR_NAME_S5KJN1:
			ret = is_modify_remosaic_cal_buf(sensor_position, cal_buf, buf, size);
			break;
		default:
			*buf  = &cal_buf[start_addr];
			*size = finfo->rom_xtc_cal_data_size;
			break;
	}
#else
	*buf  = &cal_buf[start_addr];
	*size = finfo->rom_xtc_cal_data_size;
#endif
	return ret;
}

#ifdef MODIFY_CAL_MAP_FOR_SWREMOSAIC_LIB
int is_modify_remosaic_cal_buf(int sensor_position, char *cal_buf, char **buf, int *size)
{
	int ret = -1;
	char *modified_cal_buf;
	u32 start_addr;
	u32 cal_size = 0;
	u32 curr_pos = 0;

	struct is_rom_info *finfo;

	ret = is_sec_get_sysfs_finfo_by_position(sensor_position, &finfo);
	if (ret < 0) {
		err("[%s]: get_sysfs_finfo fail", __func__);
		return -EINVAL;
	}

	cal_size = finfo->rear_remosaic_tetra_xtc_size + finfo->rear_remosaic_sensor_xtc_size + finfo->rear_remosaic_pdxtc_size
		+ finfo->rear_remosaic_sw_ggc_size;

	start_addr = finfo->rom_xtc_cal_data_start_addr;

	if (cal_size <= 0 ) {
		err("[%s]: invalid cal_size(%d)",
			__func__, cal_size);
		return -EINVAL;
	}
	
	modified_cal_buf = (char *)kmalloc(cal_size * sizeof(char), GFP_KERNEL);
	
	memcpy(&modified_cal_buf[curr_pos], &cal_buf[finfo->rear_remosaic_tetra_xtc_start_addr], finfo->rear_remosaic_tetra_xtc_size);
	curr_pos += finfo->rear_remosaic_tetra_xtc_size;

	memcpy(&modified_cal_buf[curr_pos], &cal_buf[finfo->rear_remosaic_sensor_xtc_start_addr], finfo->rear_remosaic_sensor_xtc_size);
	curr_pos += finfo->rear_remosaic_sensor_xtc_size;

	memcpy(&modified_cal_buf[curr_pos], &cal_buf[finfo->rear_remosaic_pdxtc_start_addr], finfo->rear_remosaic_pdxtc_size);
	curr_pos += finfo->rear_remosaic_pdxtc_size;

	memcpy(&modified_cal_buf[curr_pos], &cal_buf[finfo->rear_remosaic_sw_ggc_start_addr], finfo->rear_remosaic_sw_ggc_size);

	*buf  = &modified_cal_buf[0];
	*size = cal_size;

	return 0;
}
#endif

int is_sec_fw_sel_eeprom(int rom_id, bool headerOnly)
{
	int ret = 0;
	char fw_path[100];
#ifdef USE_KERNEL_VFS_READ_WRITE
	char phone_fw_version[IS_HEADER_VER_SIZE + 1] = {0, };
	struct file *fp = NULL;
	long fsize, nread;
	u8 *read_buf = NULL;
	u8 *temp_buf = NULL;
#endif
	bool is_ldo_enabled;
	struct is_core *core = is_get_is_core();
	struct is_vender_specific *specific = core->vender.private_data;
	struct is_rom_info *finfo = NULL;
	struct is_rom_info *pinfo = NULL;
	bool camera_running;

#if IS_REACHABLE(CONFIG_CAMERA_OTPROM_SUPPORT_REAR)
	struct is_module_enum *module = NULL;
	int position = is_vendor_get_position_from_rom_id(rom_id);
	ret = is_sec_update_dualized_sensor(core, rom_id);
	is_vendor_get_module_from_position(position, &module);
	if (!module) {
		err("%s, module is NULL", __func__);
		ret = -EINVAL;
		return ret;
	}

	if (module->pdata->rom_type == ROM_TYPE_OTPROM) {
		ret = -ENODEV;
		return ret;
	}
#endif

	is_sec_get_sysfs_pinfo(&pinfo, rom_id);
	is_sec_get_sysfs_finfo(&finfo, rom_id);

	is_ldo_enabled = false;

	if (test_bit(IS_ROM_STATE_SKIP_CAL_LOADING, &finfo->rom_state)) {
		info("%s: skipping cal read as skip_cal_loading is enabled for rom_id %d", __func__, rom_id);
		return 0;
	}
	/* Use mutex for i2c read */
	mutex_lock(&specific->rom_lock);

	if (!test_bit(IS_ROM_STATE_CAL_READ_DONE, &finfo->rom_state) || force_caldata_dump) {
		if (rom_id == ROM_ID_REAR)
			is_dumped_fw_loading_needed = false;

		if (force_caldata_dump)
			info("forced caldata dump!!\n");

		is_sec_rom_power_on(core, finfo->rom_power_position);
		is_ldo_enabled = true;

		info("Camera: read cal data from EEPROM (ROM ID:%d)\n", rom_id);
		if (rom_id == ROM_ID_REAR && headerOnly) {
			is_sec_read_eeprom_header(rom_id);
		} else {
			if (!is_sec_readcal_eeprom(rom_id)) {
				set_bit(IS_ROM_STATE_CAL_READ_DONE, &finfo->rom_state);
			}
		}

		if (rom_id != ROM_ID_REAR) {
			goto exit;
		}
	}

	is_sec_sensorid_find(core);
	if (headerOnly) {
		goto exit;
	}

	snprintf(fw_path, sizeof(fw_path), "%s%s", IS_FW_PATH, finfo->load_fw_name);

#ifdef USE_KERNEL_VFS_READ_WRITE
	fp = filp_open(fw_path, O_RDONLY, 0);
	if (IS_ERR(fp)) {
		err("Camera: Failed open phone firmware");
		ret = -EIO;
		fp = NULL;
		goto read_phone_fw_exit;
	}

	fsize = fp->f_path.dentry->d_inode->i_size;
	info("start, file path %s, size %ld Bytes\n",
		fw_path, fsize);

#if defined(CONFIG_CAMERA_FROM) && defined(CAMERA_MODULE_DUALIZE)
	if (IS_MAX_FW_BUFFER_SIZE >= fsize) {
		memset(fw_buf, 0x0, IS_MAX_FW_BUFFER_SIZE);
		temp_buf = fw_buf;
	} else
#endif
	{
		info("Phone FW size is larger than FW buffer. Use vmalloc.\n");
		read_buf = vmalloc(fsize);
		if (!read_buf) {
			err("failed to allocate memory");
			ret = -ENOMEM;
			goto read_phone_fw_exit;
		}
		temp_buf = read_buf;
	}
	nread = kernel_read(fp, temp_buf, fsize, &fp->f_pos);
	if (nread != fsize) {
		err("failed to read firmware file, %ld Bytes", nread);
		ret = -EIO;
		goto read_phone_fw_exit;
	}

	strncpy(phone_fw_version, temp_buf + nread - IS_HEADER_VER_OFFSET, IS_HEADER_VER_SIZE);
	strncpy(pinfo->header_ver, temp_buf + nread - IS_HEADER_VER_OFFSET, IS_HEADER_VER_SIZE);
	info("Camera: phone fw version: %s\n", phone_fw_version);

read_phone_fw_exit:
	if (read_buf) {
		vfree(read_buf);
		read_buf = NULL;
		temp_buf = NULL;
	}

	if (fp) {
		filp_close(fp, current->files);
		fp = NULL;
	}
#endif

exit:
	camera_running = is_vendor_check_camera_running(finfo->rom_power_position);
	if (is_ldo_enabled && !camera_running)
		is_sec_rom_power_off(core, finfo->rom_power_position);

	mutex_unlock(&specific->rom_lock);

	return ret;
}

#if IS_REACHABLE(CONFIG_CAMERA_OTPROM_SUPPORT_REAR)
int is_sec_fw_sel_otprom(int rom_id, bool headerOnly)
{
	int ret = 0;
	char fw_path[100];
#ifdef USE_KERNEL_VFS_READ_WRITE
	char phone_fw_version[IS_HEADER_VER_SIZE + 1] = {0, };
	struct file *fp = NULL;
	long fsize, nread;
	u8 *read_buf = NULL;
	u8 *temp_buf = NULL;
#endif
	bool is_ldo_enabled;
	struct is_core *core = is_get_is_core();
	struct is_vender_specific *specific = core->vender.private_data;
	struct is_rom_info *finfo = NULL;
	struct is_rom_info *pinfo = NULL;
	bool camera_running;
	struct is_module_enum *module = NULL;
	struct is_device_sensor_peri *sensor_peri;
	int position = is_vendor_get_position_from_rom_id(rom_id);
	ret = is_sec_update_dualized_sensor(core, rom_id);
	is_vendor_get_module_from_position(position, &module);
	if (!module) {
		err("%s, module is NULL", __func__);
		ret = -EINVAL;
		return ret;
	}

	if (module->pdata->rom_type == ROM_TYPE_EEPROM) {
		ret = -ENODEV;
		return ret;
	}

	is_sec_get_sysfs_pinfo(&pinfo, rom_id);
	is_sec_get_sysfs_finfo(&finfo, rom_id);

	is_ldo_enabled = false;

	sensor_peri = (struct is_device_sensor_peri *)module->private_data;

	if (sensor_peri->cis.client) {
		specific->otprom_client[rom_id] = sensor_peri->cis.client;
		info("%s sensor client will be used for otprom", __func__);
	}

	if (test_bit(IS_ROM_STATE_SKIP_CAL_LOADING, &finfo->rom_state)) {
		info("%s: skipping cal read as skip_cal_loading is enabled for rom_id %d", __func__, rom_id);
		return 0;
	}
	/* Temp code for sensor bring up - Start - Remove this after HI1336 Cal Bring-up*/
#if defined (A33X_TEMP_DISABLE_UW_OTP_CAL_LOADING)
	if(rom_id == 4) {
		info("%s Skip cal for UW dualization check");
		return 0;
	}
#endif
	/* Use mutex for i2c read */
	mutex_lock(&specific->rom_lock);

	if (!test_bit(IS_ROM_STATE_CAL_READ_DONE, &finfo->rom_state) || force_caldata_dump) {
		if (rom_id == ROM_ID_REAR)
			is_dumped_fw_loading_needed = false;

		if (force_caldata_dump)
			info("forced caldata dump!!\n");

		is_sec_rom_power_on(core, finfo->rom_power_position);
		is_ldo_enabled = true;

		info("Camera: read cal data from OTPROM (ROM ID:%d)\n", rom_id);
		if (rom_id == ROM_ID_REAR && headerOnly) {
			is_sec_read_otprom_header(rom_id);
		} else {
			if (!is_sec_readcal_otprom(rom_id)) {
				set_bit(IS_ROM_STATE_CAL_READ_DONE, &finfo->rom_state);
			}
		}

		if (rom_id != ROM_ID_REAR) {
			goto exit;
		}
	}

	is_sec_sensorid_find(core);
	if (headerOnly) {
		goto exit;
	}

	snprintf(fw_path, sizeof(fw_path), "%s%s", IS_FW_PATH, finfo->load_fw_name);

#ifdef USE_KERNEL_VFS_READ_WRITE
	fp = filp_open(fw_path, O_RDONLY, 0);
	if (IS_ERR(fp)) {
		err("Camera: Failed open phone firmware");
		ret = -EIO;
		fp = NULL;
		goto read_phone_fw_exit;
	}

	fsize = fp->f_path.dentry->d_inode->i_size;
	info("start, file path %s, size %ld Bytes\n",
		fw_path, fsize);

#if defined(CONFIG_CAMERA_FROM) && defined(CAMERA_MODULE_DUALIZE)
	if (IS_MAX_FW_BUFFER_SIZE >= fsize) {
		memset(fw_buf, 0x0, IS_MAX_FW_BUFFER_SIZE);
		temp_buf = fw_buf;
	} else
#endif
	{
		info("Phone FW size is larger than FW buffer. Use vmalloc.\n");
		read_buf = vmalloc(fsize);
		if (!read_buf) {
			err("failed to allocate memory");
			ret = -ENOMEM;
			goto read_phone_fw_exit;
		}
		temp_buf = read_buf;
	}
	nread = kernel_read(fp, temp_buf, fsize, &fp->f_pos);
	if (nread != fsize) {
		err("failed to read firmware file, %ld Bytes", nread);
		ret = -EIO;
		goto read_phone_fw_exit;
	}

	strncpy(phone_fw_version, temp_buf + nread - IS_HEADER_VER_OFFSET, IS_HEADER_VER_SIZE);
	strncpy(pinfo->header_ver, temp_buf + nread - IS_HEADER_VER_OFFSET, IS_HEADER_VER_SIZE);
	info("Camera: phone fw version: %s\n", phone_fw_version);

read_phone_fw_exit:
	if (read_buf) {
		vfree(read_buf);
		read_buf = NULL;
		temp_buf = NULL;
	}

	if (fp) {
		filp_close(fp, current->files);
		fp = NULL;
	}
#endif

exit:
	camera_running = is_vendor_check_camera_running(finfo->rom_power_position);
	if (is_ldo_enabled && !camera_running)
		is_sec_rom_power_off(core, finfo->rom_power_position);

	mutex_unlock(&specific->rom_lock);

	return ret;
}
#endif

#if defined(CONFIG_CAMERA_FROM)
int is_sec_fw_sel(struct is_core *core, bool headerOnly)
{
	int ret = 0;
	struct is_vender_specific *specific = core->vender.private_data;
	struct is_spi *spi = &core->spi0;
	bool is_FromPower_enabled = false;
	struct exynos_platform_is *core_pdata = NULL;
	struct is_rom_info *finfo = NULL;
	bool camera_running;
	bool camera_running_rear2;
	struct device *is_dev;

	is_dev = is_get_is_dev();

	camera_running = is_vendor_check_camera_running(SENSOR_POSITION_REAR);
	camera_running_rear2 = is_vendor_check_camera_running(SENSOR_POSITION_REAR2);

	is_sec_get_sysfs_finfo(&finfo, ROM_ID_REAR);

	core_pdata = dev_get_platdata(is_dev);
	if (!core_pdata) {
		err("core->pdata is null\n");
		return -EINVAL;
	}

	/* Use mutex for spi read */
	mutex_lock(&specific->rom_lock);
	if (!test_bit(IS_ROM_STATE_CAL_READ_DONE, &finfo->rom_state) || force_caldata_dump) {
		is_dumped_fw_loading_needed = false;
		if (force_caldata_dump)
			info("forced caldata dump!!\n");

		is_sec_rom_power_on(core, SENSOR_POSITION_REAR);
		usleep_range(1000, 1000);
		is_FromPower_enabled = true;

		info("read cal data from FROM\n");
		/* Set SPI function */
		is_spi_s_pin(spi, SPI_PIN_STATE_ISP_FW); //spi-chip-select-mode required

		if (headerOnly) {
			is_sec_read_from_header();
		} else {
			if (!is_sec_readcal(core)) {
				set_bit(IS_ROM_STATE_CAL_READ_DONE, &finfo->rom_state);
			}
		}

		/*select AF actuator*/
		if (test_bit(IS_CRC_ERROR_ALL_SECTION, &finfo->crc_error)) {
			info("Camera : CRC32 error for all section.\n");
		}

		is_sec_sensorid_find(core);
		if (headerOnly) {
			goto exit;
		}

	} else {
		info("already loaded the firmware, Phone version=%s, F-ROM version=%s\n",
			sysfs_pinfo.header_ver, finfo->header_ver);
	}

exit:
	/* Set spi pin to out */
	is_spi_s_pin(spi, SPI_PIN_STATE_IDLE);
	if (is_FromPower_enabled &&
		((specific->f_rom_power == FROM_POWER_SOURCE_REAR_SECOND && !camera_running_rear2) ||
		(specific->f_rom_power == FROM_POWER_SOURCE_REAR && !camera_running))) {
		is_sec_rom_power_off(core, SENSOR_POSITION_REAR);
	}

	mutex_unlock(&specific->rom_lock);

	return ret;
}
#endif
