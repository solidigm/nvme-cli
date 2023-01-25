/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2022 Solidigm.
 *
 * Authors: haro.panosyan@solidigm.com
 *          karl.dedow@solidigm.com
 */

#ifndef OCP_CLEAR_PCIE_CORRECTABLE_H
#define OCP_CLEAR_PCIE_CORRECTABLE_H

struct command;
struct plugin;

int ocp_clear_pcie_correctable_errors(int argc, char **argv,
	struct command *cmd, struct plugin *plugin);

#endif
