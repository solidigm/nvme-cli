/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2022 Solidigm.
 *
 * Authors: Author: haro.panosyan@solidigm.com
 */

#ifndef SOLIDIGM_VERSIONS_H
#define SOLIDIGM_VERSIONS_H

struct command;
struct plugin;

int solidigm_cloud_ssd_plugin_version(int argc, char **argv,
	struct command *cmd, struct plugin *plugin);

#endif
