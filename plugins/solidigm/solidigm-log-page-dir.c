// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2022 Solidigm.
 *
 * Author: karl.dedow@solidigm.com
 */

#include "solidigm-log-page-dir.h"

#include <errno.h>
#include <stdio.h>

#include "common.h"
#include "nvme-print.h"

struct __attribute__((packed)) supported_log_pages {
	__u32 supported[256];
};

const char *lid_to_description(const int lid)
{
	struct log_description {
		int lid;
		const char *description;
	};

	static struct log_description log_descriptions[] = {
		{ 0x00, "Supported Log Pages"},
		{ 0x01, "Error Information"},
		{ 0x02, "SMART / Health Information"},
		{ 0x03, "Firmware Slot Information"},
		{ 0x04, "Changed Namespace List"},
		{ 0x05, "Commands Supported and Effects"},
		{ 0x06, "Device Self Test"},
		{ 0x07, "Telemetry Host-Initiated"},
		{ 0x08, "Telemetry Controller-Initiated"},
		{ 0x09, "Endurance Group Information"},
		{ 0x0A, "Predictable Latency Per NVM Set"},
		{ 0x0B, "Predictable Latency Event Aggregate"},
		{ 0x0C, "Asymmetric Namespace Access"},
		{ 0x0D, "Persistent Event Log"},
		{ 0x0E, "Predictable Latency Event Aggregate"},
		{ 0x0F, "Endurance Group Event Aggregate"},
		{ 0x10, "Media Unit Status"},
		{ 0x11, "Supported Capacity Configuration List"},
		{ 0x12, "Feature Identifiers Supported and Effects"},
		{ 0x13, "NVMe-MI Commands Supported and Effects"},
		{ 0x14, "Command and Feature lockdown"},
		{ 0x15, "Boot Partition"},
		{ 0x16, "Rotational Media Information"},
		{ 0x70, "Discovery"},
		{ 0x80, "Reservation Notification"},
		{ 0x81, "Sanitize Status"},
		// TODO: Assume OCP logs for now. Eventually need to add UUID support
		{ 0xC0, "OCP SMART / Health Information Extended"},
		{ 0xC1, "OCP Error Recovery or Read Commands Latency Statistics"},
		{ 0xC2, "OCP Firmware Activation History or Write Commands Latency Statistics"},
		{ 0xC3, "OCP Latency Monitor"},
		{ 0xC4, "OCP Device Capabilities or Endurance Manager Statistics"},
		{ 0xC5, "OCP Unsupported Requirements or Temperature Statistics"},
	};

	for (int index = 0; index < sizeof(log_descriptions) /
		sizeof(struct log_description); index++) {
		if (lid == log_descriptions[index].lid)
			return log_descriptions[index].description;
	}

	return "Unknown";
}

static void solidigm_supported_log_pages_print(
	const struct supported_log_pages *supported)
{
	printf("Log Page Directory Log:\n");
	printf("  supported:\n");

	for (int index = 0; index < sizeof(supported->supported) / sizeof(__u32);
		index++) {
		if (supported->supported[index] == 0)
			continue;

		printf("    log page:\n");
		printf("      %-16s%d\n", "lid:", le32_to_cpu(index));
		printf("      %-16s%s\n", "description:", lid_to_description(index));
	}

	printf("\n");
}

void solidigm_supported_log_pages_json(
	const struct supported_log_pages *supported)
{
	struct json_object *root = json_create_object();

	struct json_object *supported_arry = json_create_array();

	for (int index = 0; index < sizeof(supported->supported) / sizeof(__u32);
		index++) {
		if (supported->supported[index] == 0)
			continue;

		struct json_object *supported_obj = json_create_object();

		json_object_add_value_uint(supported_obj, "lid", le32_to_cpu(index));
		json_object_add_value_string(supported_obj, "description",
			lid_to_description(index));

		json_array_add_value_object(supported_arry, supported_obj);
	}

	json_object_add_value_array(root, "supported", supported_arry);

	json_print_object(root, NULL);
	json_free_object(root);

	printf("\n");
}

int solidigm_get_log_page_directory_log(int argc, char **argv,
	struct command *cmd, struct plugin *plugin)
{
	const __u8 log_id = 0x00;
	const char *description =
		"Retrieves and parses supported log pages log.";

	__u32 uuid_index = 0;
	char *format = "normal";

	OPT_ARGS(options) = {
		OPT_UINT("uuid-index", 'u', &uuid_index, "UUID index value : (integer)"),
		OPT_FMT("output-format", 'o', &format, "output format : normal | json"),
		OPT_END()
	};

	struct nvme_dev *dev = NULL;
	int err = parse_and_open(&dev, argc, argv, description, options);

	if (err)
		return err;

	struct supported_log_pages supported_data = { 0 };

	struct nvme_get_log_args args = {
		.lpo = 0,
		.result = NULL,
		.log = &supported_data,
		.args_size = sizeof(args),
		.fd = dev_fd(dev),
		.timeout = NVME_DEFAULT_IOCTL_TIMEOUT,
		.lid = log_id,
		.len = sizeof(supported_data),
		.nsid = NVME_NSID_ALL,
		.csi = NVME_CSI_NVM,
		.lsi = NVME_LOG_LSI_NONE,
		.lsp = 0,
		.uuidx = uuid_index,
		.rae = false,
		.ot = false,
	};

	err = nvme_get_log(&args);

	if (err)
		nvme_show_status(err);
	else {
		const enum nvme_print_flags print_flag = validate_output_format(format);

		if (print_flag == JSON)
			solidigm_supported_log_pages_json(&supported_data);
		else if (print_flag == NORMAL)
			solidigm_supported_log_pages_print(&supported_data);
		else {
			fprintf(stderr, "Error: Failed to parse.\n");
			err = -EINVAL;
		}
	}

	dev_close(dev);
	return err;
}
