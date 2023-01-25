// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2022 Solidigm.
 *
 * Authors: haro.panosyan@solidigm.com
 *          karl.dedow@solidigm.com
 */
#include "ocp-clear-pcie-correctable.h"

#include <unistd.h>

#include "ocp-utils.h"
#include "nvme-print.h"

int ocp_clear_pcie_correctable_errors(int argc, char **argv,
	struct command *cmd, struct plugin *plugin)
{
	const __u8 fid = 0xC3;
	const char *description = "Clears the OCP PCIe correctable errors.";
	int uuid_index = 0;
	bool no_uuid = false;

	OPT_ARGS(options) = {
		OPT_FLAG("no-uuid", 'n', &no_uuid,
			 "Skip UUID index search (UUID index not required for OCP 1.0)"),
		OPT_END()
	};

	struct nvme_dev *dev = NULL;
	int err = parse_and_open(&dev, argc, argv, description, options);

	if (err)
		return err;

	if (no_uuid == false) {
		// OCP 2.0 requires UUID index support
		err = ocp_get_uuid_index(dev, &uuid_index);
		if (err || uuid_index == 0)
			fprintf(stderr, "ERROR: No OCP UUID index found\n");
	}

	if (!err) {
		__u32 clear_bit = 1 << 31;

		struct nvme_set_features_args args = {
			.result = NULL,
			.data = NULL,
			.args_size = sizeof(args),
			.fd = dev_fd(dev),
			.timeout = NVME_DEFAULT_IOCTL_TIMEOUT,
			.nsid = 0,
			.cdw11 = clear_bit,
			.cdw12 = 0,
			.cdw13 = 0,
			.cdw15 = 0,
			.data_len = 0,
			.save = 0,
			.uuidx = uuid_index,
			.fid = fid,
		};

		err = nvme_set_features(&args);

		if (err)
			nvme_show_status(err);
	}

	dev_close(dev);

	if (!err)
		printf("Success : Cleared PCIe correctable errors.\n");
	else
		printf("Fail : Did not clear PCIe correctable errors.\n");

	return err;
}
