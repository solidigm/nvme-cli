// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2022 Solidigm.
 *
 * Author: haro.panosyan@solidigm.com
 */

#include "solidigm-versions.h"

#include <stdio.h>

int solidigm_cloud_ssd_plugin_version(int argc, char **argv,
	struct command *cmd, struct plugin *plugin)
{
	int OCP_PLUGIN_MAJOR = 0;
	int OCP_PLUGIN_MINOR = 1;

	printf("%d.%d\n", OCP_PLUGIN_MAJOR, OCP_PLUGIN_MINOR);
	return 0;
}
