// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018 Samsung Electronics Co., Ltd. All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include "include/defex_internal.h"

#ifdef DEFEX_PERMISSIVE_IM
unsigned char global_immutable_status = 2;
#else
unsigned char global_immutable_status = 1;
#endif /* DEFEX_PERMISSIVE_IM */

#ifdef DEFEX_PERMISSIVE_IMR
unsigned char global_immutable_root_status = 2;
#else
unsigned char global_immutable_root_status = 1;
#endif /* DEFEX_PERMISSIVE_IMR */


int immutable_status_store(const char *status_str)
{
	int ret;
	unsigned int status;

	if (!status_str)
		return -EINVAL;

	ret = kstrtouint(status_str, 10, &status);
	if (ret != 0 || status > 2)
		return -EINVAL;

	global_immutable_status = status;
	return 0;
}

int immutable_root_status_store(const char *status_str)
{
	int ret;
	unsigned int status;

	if (!status_str)
		return -EINVAL;

	ret = kstrtouint(status_str, 10, &status);
	if (ret != 0 || status > 2)
		return -EINVAL;

	global_immutable_root_status = status;
	return 0;
}
