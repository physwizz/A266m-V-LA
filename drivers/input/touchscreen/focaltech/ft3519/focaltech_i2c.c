/*
 *
 * FocalTech TouchScreen driver.
 *
 * Copyright (c) 2012-2020, FocalTech Systems, Ltd., all rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

/************************************************************************
*
* File Name: focaltech_i2c.c
*
* Author: Focaltech Driver Team
*
* Created: 2016-08-04
*
* Abstract: i2c communication with TP
*
* Version: v1.0
*
* Revision History:
*
************************************************************************/

/*****************************************************************************
* Included header files
*****************************************************************************/
#include "focaltech_core.h"
#if IS_ENABLED(CONFIG_SAMSUNG_TUI)
#include <linux/input/stui_inf.h>
#endif
/*****************************************************************************
* Private constant and macro definitions using #define
*****************************************************************************/
#define I2C_RETRY_NUMBER                    3
#define I2C_BUF_LENGTH                      256

/*****************************************************************************
* Private enumerations, structures and unions using typedef
*****************************************************************************/

/*****************************************************************************
* Static variables
*****************************************************************************/

/*****************************************************************************
* Global variable or extern global variabls/functions
*****************************************************************************/

/*****************************************************************************
* Static function prototypes
*****************************************************************************/

/*****************************************************************************
* functions body
*****************************************************************************/
int fts_read(u8 *cmd, u32 cmdlen, u8 *data, u32 datalen)
{
	int ret = 0;
	int i = 0;
	struct fts_ts_data *ts_data = fts_data;
	struct i2c_msg msg_list[2];
	struct i2c_msg *msg = NULL;
	int msg_num = 0;

	/* must have data when read */
	if (!ts_data || !ts_data->client || !data || !datalen
		|| (datalen > I2C_BUF_LENGTH) || (cmdlen > I2C_BUF_LENGTH)) {
			FTS_ERROR("fts_data/client/cmdlen(%d)/data/datalen(%d) is invalid",
				cmdlen, datalen);
		return -EINVAL;
	}

#if IS_ENABLED(CONFIG_SAMSUNG_TUI)
	if (STUI_MODE_TOUCH_SEC & stui_get_mode())
		return -EBUSY;
#endif

	mutex_lock(&ts_data->bus_lock);
	memset(&msg_list[0], 0, sizeof(struct i2c_msg));
	memset(&msg_list[1], 0, sizeof(struct i2c_msg));
	memcpy(ts_data->bus_tx_buf, cmd, cmdlen);
	msg_list[0].addr = ts_data->client->addr;
	msg_list[0].flags = 0;
	msg_list[0].len = cmdlen;
	msg_list[0].buf = ts_data->bus_tx_buf;
	msg_list[1].addr = ts_data->client->addr;
	msg_list[1].flags = I2C_M_RD;
	msg_list[1].len = datalen;
	msg_list[1].buf = ts_data->bus_rx_buf;
	if (cmd && cmdlen) {
		msg = &msg_list[0];
		msg_num = 2;
	} else {
		msg = &msg_list[1];
		msg_num = 1;
	}

	for (i = 0; i < I2C_RETRY_NUMBER; i++) {
		ret = i2c_transfer(ts_data->client->adapter, msg, msg_num);
		if (ret < 0) {
			FTS_ERROR("i2c_transfer(read) fail,ret:%d", ret);
		} else {
			memcpy(data, ts_data->bus_rx_buf, datalen);
			break;
		}
	}
	mutex_unlock(&ts_data->bus_lock);

	if (ts_data->debug_flag & SEC_TS_DEBUG_PRINT_READ_CMD) {
		char *reg_buff;
		char *data_buff;
		char cbuff[10];

		reg_buff = kzalloc(cmdlen * 3 + 1, GFP_KERNEL);
		if (!reg_buff)
			goto not_err;

		data_buff = kzalloc(datalen * 3 + 1, GFP_KERNEL);
		if (!data_buff) {
			kfree(reg_buff);
			goto not_err;
		}

		for (i = 0; i < cmdlen; i++) {
			memset(cbuff, 0x00, sizeof(cbuff));
			snprintf(cbuff, sizeof(cbuff), "%02X ", cmd[i]);
			strlcat(reg_buff, cbuff, cmdlen * 3);
		}

		for (i = 0; i < datalen; i++) {
			memset(cbuff, 0x00, sizeof(cbuff));
			snprintf(cbuff, sizeof(cbuff), "%02X ", data[i]);
			strlcat(data_buff, cbuff, datalen * 3);
		}

		FTS_INFO("debug: reg: %s(%d), data: %s(%d)", reg_buff, cmdlen, data_buff, datalen);

		kfree(reg_buff);
		kfree(data_buff);
	}
not_err:
	return ret;
}

int fts_write(u8 *writebuf, u32 writelen)
{
	int ret = 0;
	int i = 0;
	struct fts_ts_data *ts_data = fts_data;
	struct i2c_msg msgs;

	if (!ts_data || !ts_data->client || !writebuf || !writelen
		|| (writelen > I2C_BUF_LENGTH)) {
		FTS_ERROR("fts_data/client/data/datalen(%d) is invalid", writelen);
		return -EINVAL;
	}

#if IS_ENABLED(CONFIG_SAMSUNG_TUI)
	if (STUI_MODE_TOUCH_SEC & stui_get_mode())
		return -EBUSY;
#endif

	mutex_lock(&ts_data->bus_lock);
	memset(&msgs, 0, sizeof(struct i2c_msg));
	memcpy(ts_data->bus_tx_buf, writebuf, writelen);
	msgs.addr = ts_data->client->addr;
	msgs.flags = 0;
	msgs.len = writelen;
	msgs.buf = ts_data->bus_tx_buf;
	for (i = 0; i < I2C_RETRY_NUMBER; i++) {
		ret = i2c_transfer(ts_data->client->adapter, &msgs, 1);
		if (ret < 0) {
			FTS_ERROR("i2c_transfer(write) fail,ret:%d", ret);
		} else {
			break;
		}
	}
	mutex_unlock(&ts_data->bus_lock);

	if (ts_data->debug_flag & SEC_TS_DEBUG_PRINT_WRITE_CMD) {
		char *reg_buff;
		char cbuff[10];

		reg_buff = kzalloc(writelen * 3 + 1, GFP_KERNEL);
		if (!reg_buff)
			goto not_err;

		for (i = 0; i < writelen; i++) {
			memset(cbuff, 0x00, sizeof(cbuff));
			snprintf(cbuff, sizeof(cbuff), "%02X ", writebuf[i]);
			strlcat(reg_buff, cbuff, writelen * 3);
		}

		FTS_INFO("debug: reg: %s(%d)\n", reg_buff, writelen);

		kfree(reg_buff);
	}
not_err:
	return ret;
}

int fts_read_reg(u8 addr, u8 *value)
{
	return fts_read(&addr, 1, value, 1);
}

int fts_write_reg(u8 addr, u8 value)
{
	u8 buf[2] = { 0 };

	buf[0] = addr;
	buf[1] = value;
	return fts_write(buf, (u32)sizeof(buf));
}

int fts_bus_init(struct fts_ts_data *ts_data)
{
	FTS_FUNC_ENTER();
	ts_data->bus_tx_buf = kzalloc(I2C_BUF_LENGTH, GFP_KERNEL);
	if (NULL == ts_data->bus_tx_buf) {
		FTS_ERROR("failed to allocate memory for bus_tx_buf");
		return -ENOMEM;
	}

	ts_data->bus_rx_buf = kzalloc(I2C_BUF_LENGTH, GFP_KERNEL);
	if (NULL == ts_data->bus_rx_buf) {
		kfree(ts_data->bus_tx_buf);
		FTS_ERROR("failed to allocate memory for bus_rx_buf");
		return -ENOMEM;
	}
	FTS_FUNC_EXIT();
	return 0;
}

int fts_bus_exit(struct fts_ts_data *ts_data)
{
	FTS_FUNC_ENTER();
	if (ts_data && ts_data->bus_tx_buf) {
		kfree(ts_data->bus_tx_buf);
		ts_data->bus_tx_buf = NULL;
	}

	if (ts_data && ts_data->bus_rx_buf) {
		kfree(ts_data->bus_rx_buf);
		ts_data->bus_rx_buf = NULL;
	}
	FTS_FUNC_EXIT();
	return 0;
}
