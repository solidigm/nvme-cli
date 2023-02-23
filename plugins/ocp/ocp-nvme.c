// SPDX-License-Identifier: GPL-2.0-or-later
/* Copyright (c) 2022 Meta Platforms, Inc.
 *
 * Authors: Arthur Shau <arthurshau@fb.com>,
 *          Wei Zhang <wzhang@fb.com>,
 *   	    Venkat Ramesh <venkatraghavan@fb.com>
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <errno.h>
#include <limits.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>

#include "common.h"
#include "nvme.h"
#include "libnvme.h"
#include "plugin.h"
#include "linux/types.h"
#include "util/types.h"
#include "nvme-print.h"

#include "ocp-utils.h"
#include "ocp-clear-fw-update-history.h"

#define CREATE_CMD
#include "ocp-nvme.h"

/* SMART / Health Info Extended Log Page (0xC0) */
#define SMART_CLOUD_ATTR_OPCODE		0xC0
#define SMART_CLOUD_ATTR_LEN		512
#define SMART_CLOUD_GUID_LENGTH		16
#define SMART_CLOUD_LOG_VERSION_2	2

static __u8 scao_guid[SMART_CLOUD_GUID_LENGTH]  = { 0xC5, 0xAF, 0x10, 0x28, 0xEA, 0xBF,
                0xF2, 0xA4, 0x9C, 0x4F, 0x6F, 0x7C, 0xC9, 0x14, 0xD5, 0xAF };

typedef enum {
        SCAO_PMUW               =  0,	/* Physical media units written */
        SCAO_PMUR               = 16,	/* Physical media units read */
        SCAO_BUNBR              = 32,	/* Bad user nand blocks raw */
        SCAO_BUNBN              = 38,	/* Bad user nand blocks normalized */
        SCAO_BSNBR              = 40,	/* Bad system nand blocks raw */
        SCAO_BSNBN              = 46,	/* Bad system nand blocks normalized */
        SCAO_XRC                = 48,	/* XOR recovery count */
        SCAO_UREC               = 56,	/* Uncorrectable read error count */
        SCAO_SEEC               = 64,	/* Soft ecc error count */
        SCAO_EEDE               = 72,	/* End to end detected errors */
        SCAO_EECE               = 76,	/* End to end corrected errors */
        SCAO_SDPU               = 80,	/* System data percent used */
        SCAO_RFSC               = 81,	/* Refresh counts */
        SCAO_MXUDEC             = 88,	/* Max User data erase counts */
        SCAO_MNUDEC             = 92,	/* Min User data erase counts */
        SCAO_NTTE               = 96,	/* Number of Thermal throttling events */
        SCAO_CTS                = 97,	/* Current throttling status */
        SCAO_EVF                = 98,   /* Errata Version Field */
        SCAO_PVF                = 99,   /* Point Version Field */
        SCAO_MIVF               = 101,  /* Minor Version Field */
        SCAO_MAVF               = 103,  /* Major Version Field */
        SCAO_PCEC               = 104,	/* PCIe correctable error count */
        SCAO_ICS                = 112,	/* Incomplete shutdowns */
        SCAO_PFB                = 120,	/* Percent free blocks */
        SCAO_CPH                = 128,	/* Capacitor health */
        SCAO_NEV                = 130,  /* NVMe Errata Version */
        SCAO_UIO                = 136,	/* Unaligned I/O */
        SCAO_SVN                = 144,	/* Security Version Number */
        SCAO_NUSE               = 152,	/* NUSE - Namespace utilization */
        SCAO_PSC                = 160,	/* PLP start count */
        SCAO_EEST               = 176,	/* Endurance estimate */
        SCAO_PLRC               = 192,  /* PCIe Link Retraining Count */
        SCAO_PSCC               = 200,  /* Power State Change Count */
        SCAO_LPV                = 494,	/* Log page version */
        SCAO_LPG                = 496,	/* Log page GUID */
} SMART_CLOUD_ATTRIBUTE_OFFSETS;

/* C3 Latency Monitor Log Page */
#define C3_LATENCY_MON_LOG_BUF_LEN          0x200
#define C3_LATENCY_MON_OPCODE               0xC3
#define C3_LATENCY_MON_VERSION              0x0001
#define C3_GUID_LENGTH                      16
#define C0_ACTIVE_BUCKET_TIMER_INCREMENT    5
#define C0_ACTIVE_THRESHOLD_INCREMENT       5
#define C0_MINIMUM_WINDOW_INCREMENT         100
static __u8 lat_mon_guid[C3_GUID_LENGTH] = { 0x92, 0x7a, 0xc0, 0x8c, 0xd0, 0x84,
                0x6c, 0x9c, 0x70, 0x43, 0xe6, 0xd4, 0x58, 0x5e, 0xd4, 0x85 };

#define READ            0
#define WRITE           1
#define TRIM            2
#define RESERVED        3

struct __attribute__((__packed__)) ssd_latency_monitor_log {
        __u8    feature_status;                         /* 0x00  */
        __u8    rsvd1;                                  /* 0x01  */
        __le16  active_bucket_timer;                    /* 0x02  */
        __le16  active_bucket_timer_threshold;          /* 0x04  */
        __u8    active_threshold_a;                     /* 0x06  */
        __u8    active_threshold_b;                     /* 0x07  */
        __u8    active_threshold_c;                     /* 0x08  */
        __u8    active_threshold_d;                     /* 0x09  */
        __le16  active_latency_config;                  /* 0x0A  */
        __u8    active_latency_min_window;              /* 0x0C  */
        __u8    rsvd2[0x13];                            /* 0x0D  */

        __le32  active_bucket_counter[4][4] ;           /* 0x20 - 0x5F   */
        __le64  active_latency_timestamp[4][3];         /* 0x60 - 0xBF   */
        __le16  active_measured_latency[4][3];          /* 0xC0 - 0xD7   */
        __le16  active_latency_stamp_units;             /* 0xD8  */
        __u8    rsvd3[0x16];                            /* 0xDA  */

        __le32  static_bucket_counter[4][4] ;           /* 0xF0  - 0x12F */
        __le64  static_latency_timestamp[4][3];         /* 0x130 - 0x18F */
        __le16  static_measured_latency[4][3];          /* 0x190 - 0x1A7 */
        __le16  static_latency_stamp_units;             /* 0x1A8 */
        __u8    rsvd4[0x16];                            /* 0x1AA */

        __le16  debug_log_trigger_enable;               /* 0x1C0 */
        __le16  debug_log_measured_latency;             /* 0x1C2 */
        __le64  debug_log_latency_stamp;                /* 0x1C4 */
        __le16  debug_log_ptr;                          /* 0x1CC */
        __le16  debug_log_counter_trigger;              /* 0x1CE */
        __u8    debug_log_stamp_units;                  /* 0x1D0 */
        __u8    rsvd5[0x1D];                            /* 0x1D1 */

        __le16  log_page_version;                       /* 0x1EE */
        __u8    log_page_guid[0x10];                    /* 0x1F0 */
};

static int convert_ts(time_t time, char *ts_buf)
{
        struct tm  gmTimeInfo;
        time_t     time_Human, time_ms;
        char       buf[80];

        time_Human = time/1000;
        time_ms = time % 1000;

        gmtime_r((const time_t *)&time_Human, &gmTimeInfo);

        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &gmTimeInfo);
        sprintf(ts_buf, "%s.%03ld GMT", buf, time_ms);

        return 0;
}

static void print_smart_cloud_log(__u8 *log_data)
{
	printf("SMART Cloud Health Log Page Data:\n");

	printf("  %-40s%s\n", "Physical Media Units Written:",
		uint128_t_to_string(le128_to_cpu(&log_data[SCAO_PMUW])));
	printf("  %-40s%s\n", "Physical Media Units Read:",
		uint128_t_to_string(le128_to_cpu(&log_data[SCAO_PMUR])));
	printf("  %-40s%"PRIu64"\n", "Bad User NAND Blocks - Raw:",
		le64_to_cpu(*(uint64_t *)&log_data[SCAO_BUNBR] & 0x0000FFFFFFFFFFFF));
	printf("  %-40s%d\n", "Bad User NAND Blocks - Normalized:",
		le16_to_cpu(*(uint16_t *)&log_data[SCAO_BUNBN]));
	printf("  %-40s%"PRIu64"\n", "Bad System NAND Blocks - Raw:",
		le64_to_cpu(*(uint64_t *)&log_data[SCAO_BSNBR] & 0x0000FFFFFFFFFFFF));
	printf("  %-40s%d\n", "Bad System NAND Blocks - Normalized:",
		le16_to_cpu(*(uint16_t *)&log_data[SCAO_BSNBN]));
	printf("  %-40s%"PRIu64"\n", "XOR Recovery Count:",
		le64_to_cpu(*(uint64_t *)&log_data[SCAO_XRC]));
	printf("  %-40s%"PRIu64"\n", "Uncorrectable Read Error Count:",
		le64_to_cpu(*(uint64_t *)&log_data[SCAO_UREC]));
	printf("  %-40s%"PRIu64"\n", "Soft ECC Error Count:",
		le64_to_cpu(*(uint64_t *)&log_data[SCAO_SEEC]));
	printf("  %-40s%"PRIu32"\n", "End to End Detected Errors:",
		le32_to_cpu(*(uint32_t *)&log_data[SCAO_EEDE]));
	printf("  %-40s%"PRIu32"\n", "End to End Corrected Errors:",
		le32_to_cpu(*(uint32_t *)&log_data[SCAO_EECE]));
	printf("  %-40s%d\n", "System Data Percent Used:", log_data[SCAO_SDPU]);
	printf("  %-40s%"PRIu64"\n", "Refresh Counts:",
		le64_to_cpu(*(uint64_t *)&log_data[SCAO_RFSC] & 0x00FFFFFFFFFFFFFF));
	printf("  %-40s%"PRIu32"\n", "Max User Data Erase Counts:",
		le32_to_cpu(*(uint32_t *)&log_data[SCAO_MXUDEC]));
	printf("  %-40s%"PRIu32"\n", "Min User Data Erase Counts:",
		le32_to_cpu(*(uint32_t *)&log_data[SCAO_MNUDEC]));
	printf("  %-40s%d\n", "Number of Thermal Throttling Events:", log_data[SCAO_NTTE]);
	printf("  %-40s%d\n", "Current Throttling Status:", log_data[SCAO_CTS]);
	printf("  %-40s%"PRIu64"\n", "PCIe Correctable Error Count:",
		le64_to_cpu(*(uint64_t *)&log_data[SCAO_PCEC]));
	printf("  %-40s%"PRIu32"\n", "Incomplete Shutdowns:",
		le32_to_cpu(*(uint32_t *)&log_data[SCAO_ICS]));
	printf("  %-40s%d\n", "Percent Free Blocks:", log_data[SCAO_PFB]);
	printf("  %-40s%"PRIu16"\n", "Capacitor Health:",
		le16_to_cpu(*(uint16_t *)&log_data[SCAO_CPH]));
	printf("  %-40s%"PRIu64"\n", "Unaligned I/O:",
		le64_to_cpu(*(uint64_t *)&log_data[SCAO_UIO]));
	printf("  %-40s%"PRIu64"\n", "Security Version Number:",
		le64_to_cpu(*(uint64_t *)&log_data[SCAO_SVN]));
	printf("  %-40s%"PRIu64"\n", "Namespace Utilization:",
		le64_to_cpu(*(uint64_t *)&log_data[SCAO_NUSE]));
	printf("  %-40s%s\n", "PLP Start Count:",
		uint128_t_to_string(le128_to_cpu(&log_data[SCAO_PSC])));
	printf("  %-40s%s\n", "Endurance Estimate:",
		uint128_t_to_string(le128_to_cpu(&log_data[SCAO_EEST])));

	uint16_t smart_log_ver = le16_to_cpu(*(uint16_t *)&log_data[SCAO_LPV]);

	printf("  %-40s%"PRIu16"\n", "Log Page Version:", smart_log_ver);
	printf("  %-40s0x%"PRIx64"%"PRIx64"\n", "Log Page GUID:",
		le64_to_cpu(*(uint64_t *)&log_data[SCAO_LPG + 8]),
		le64_to_cpu(*(uint64_t *)&log_data[SCAO_LPG]));

	if (smart_log_ver > SMART_CLOUD_LOG_VERSION_2) {
		printf("  %-40s%d\n", "Errata Version:", log_data[SCAO_EVF]);
		printf("  %-40s%"PRIu16"\n", "Point Version:",
			le16_to_cpu(*(uint16_t *)&log_data[SCAO_PVF]));
		printf("  %-40s%"PRIu16"\n", "Minor Version:",
			le16_to_cpu(*(uint16_t *)&log_data[SCAO_MIVF]));
		printf("  %-40s%d\n", "Major Version:", log_data[SCAO_MAVF]);
		printf("  %-40s%d\n", "NVMe Errata Version:", log_data[SCAO_NEV]);
		printf("  %-40s%"PRIu64"\n", "PCIe Link Retraining Count:",
			le64_to_cpu(*(uint64_t *)&log_data[SCAO_PLRC]));
		printf("  %-40s%"PRIu64"\n", "Power State Change Count:",
			le64_to_cpu(*(uint64_t *)&log_data[SCAO_PSCC]));
	}
}

static void print_smart_cloud_log_json(__u8 *log_data)
{
	struct json_object *root = json_create_object();
	struct json_object *pmuw = json_create_object();
	struct json_object *pmur = json_create_object();
	struct json_object *bunb = json_create_object();
	struct json_object *bsnb = json_create_object();
	struct json_object *psc  = json_create_object();
	struct json_object *eest = json_create_object();

	json_object_add_value_uint64(pmuw, "hi",
				     le64_to_cpu(*(uint64_t *)&log_data[SCAO_PMUW + 8]));
	json_object_add_value_uint64(pmuw, "lo",
				     le64_to_cpu(*(uint64_t *)&log_data[SCAO_PMUW]));
	json_object_add_value_object(root, "physical media units written", pmuw);

	json_object_add_value_uint64(pmur, "hi",
				     le64_to_cpu(*(uint64_t *)&log_data[SCAO_PMUR + 8]));
	json_object_add_value_uint64(pmur, "lo",
				     le64_to_cpu(*(uint64_t *)&log_data[SCAO_PMUR]));
	json_object_add_value_object(root, "physical media units read", pmur);

	json_object_add_value_uint64(bunb, "raw",
				     le64_to_cpu(*(uint64_t *)&log_data[SCAO_BUNBR] & 0x0000FFFFFFFFFFFF));
	json_object_add_value_uint(bunb, "normalized",
				   le16_to_cpu(*(uint16_t *)&log_data[SCAO_BUNBN]));
	json_object_add_value_object(root, "bad user nand blocks", bunb);

	json_object_add_value_uint64(bsnb, "raw",
				     le64_to_cpu(*(uint64_t *)&log_data[SCAO_BSNBR] & 0x0000FFFFFFFFFFFF));
	json_object_add_value_uint(bsnb, "normalized",
				   le16_to_cpu(*(uint16_t *)&log_data[SCAO_BSNBN]));
	json_object_add_value_object(root, "bad system nand blocks", bsnb);

	json_object_add_value_uint64(root, "xor recovery count",
				     le64_to_cpu(*(uint64_t *)&log_data[SCAO_XRC]));
	json_object_add_value_uint64(root, "uncorrectable read error count",
				     le64_to_cpu(*(uint64_t *)&log_data[SCAO_UREC]));
	json_object_add_value_uint64(root, "soft ecc error count",
				     le64_to_cpu(*(uint64_t *)&log_data[SCAO_SEEC]));
	json_object_add_value_uint(root, "end to end detected errors",
				   le32_to_cpu(*(uint32_t *)&log_data[SCAO_EEDE]));
	json_object_add_value_uint(root, "end to end corrected errors",
				   le32_to_cpu(*(uint32_t *)&log_data[SCAO_EECE]));
	json_object_add_value_uint(root, "system data percent used", log_data[SCAO_SDPU]);
	json_object_add_value_uint64(root, "refresh counts",
				     le64_to_cpu(*(uint64_t *)&log_data[SCAO_RFSC] & 0x00FFFFFFFFFFFFFF));
	json_object_add_value_uint(root, "max user data erase counts",
				   le32_to_cpu(*(uint32_t *)&log_data[SCAO_MXUDEC]));
	json_object_add_value_uint(root, "min user data erase counts",
				   le32_to_cpu(*(uint32_t *)&log_data[SCAO_MNUDEC]));
	json_object_add_value_uint(root, "number of thermal throttling events",
				   log_data[SCAO_NTTE]);
	json_object_add_value_uint(root, "current throttling status",
				   log_data[SCAO_CTS]);
	json_object_add_value_uint64(root, "pcie correctable error count",
				     le64_to_cpu(*(uint64_t *)&log_data[SCAO_PCEC]));
	json_object_add_value_uint(root, "incomplete shutdowns",
				   le32_to_cpu(*(uint32_t *)&log_data[SCAO_ICS]));
	json_object_add_value_uint(root, "percent free blocks", log_data[SCAO_PFB]);
	json_object_add_value_uint(root, "capacitor health",
				   le16_to_cpu(*(uint16_t *)&log_data[SCAO_CPH]));
	json_object_add_value_uint64(root, "unaligned i/o",
				     le64_to_cpu(*(uint64_t *)&log_data[SCAO_UIO]));
	json_object_add_value_uint64(root, "security version number",
				     le64_to_cpu(*(uint64_t *)&log_data[SCAO_SVN]));
	json_object_add_value_uint64(root, "namespace utilization",
				     le64_to_cpu(*(uint64_t *)&log_data[SCAO_NUSE]));

	json_object_add_value_uint64(psc, "hi",
				     le64_to_cpu(*(uint64_t *)&log_data[SCAO_PSC + 8]));
	json_object_add_value_uint64(psc, "lo",
				     le64_to_cpu(*(uint64_t *)&log_data[SCAO_PSC]));
	json_object_add_value_object(root, "plp start count", psc);

	json_object_add_value_uint64(eest, "hi",
				     le64_to_cpu(*(uint64_t *)&log_data[SCAO_EEST + 8]));
	json_object_add_value_uint64(eest, "lo",
				     le64_to_cpu(*(uint64_t *)&log_data[SCAO_EEST]));
	json_object_add_value_object(root, "endurance estimate", eest);

	__u16 smart_log_ver = le16_to_cpu(*(uint16_t *)&log_data[SCAO_LPV]);

	json_object_add_value_uint(root, "log page version", smart_log_ver);

	char guid[40] = { 0 };

	sprintf(guid, "0x%"PRIx64"%"PRIx64"",
		le64_to_cpu(*(uint64_t *)&log_data[SCAO_LPG + 8]),
		le64_to_cpu(*(uint64_t *)&log_data[SCAO_LPG]));
	json_object_add_value_string(root, "log page guid", guid);

	if (smart_log_ver > SMART_CLOUD_LOG_VERSION_2) {
		json_object_add_value_uint(root, "errata version field", log_data[SCAO_EVF]);
		json_object_add_value_uint(root, "point version",
					   le16_to_cpu(*(uint16_t *)&log_data[SCAO_PVF]));
		json_object_add_value_uint(root, "minor version",
					   le16_to_cpu(*(uint16_t *)&log_data[SCAO_MIVF]));
		json_object_add_value_uint(root, "major version", log_data[SCAO_MAVF]);
		json_object_add_value_uint(root, "nvme errata version", log_data[SCAO_NEV]);
		json_object_add_value_uint(root, "pcie link retraining count",
					   le64_to_cpu(*(uint64_t *)&log_data[SCAO_PLRC]));
		json_object_add_value_uint(root, "power state change count",
					   le64_to_cpu(*(uint64_t *)&log_data[SCAO_PSCC]));
	}

	json_print_object(root, NULL);
	json_free_object(root);

	printf("\n");
}

static int get_smart_cloud_log_page(struct nvme_dev *dev, char *format)
{
	__u8 data[SMART_CLOUD_ATTR_LEN] = { 0 };
	int uuid_index = 0;

	// Best effort attempt at uuid. Otherwise, assume no index (i.e. 0)
	// Log GUID check will ensure correctness of returned data
	ocp_get_uuid_index(dev, &uuid_index);

	struct nvme_get_log_args args = {
		.lpo = 0,
		.result = NULL,
		.log = &data,
		.args_size = sizeof(args),
		.fd = dev_fd(dev),
		.timeout = NVME_DEFAULT_IOCTL_TIMEOUT,
		.lid =  SMART_CLOUD_ATTR_OPCODE,
		.len = sizeof(data),
		.nsid = NVME_NSID_ALL,
		.csi = NVME_CSI_NVM,
		.lsi = NVME_LOG_LSI_NONE,
		.lsp = 0,
		.uuidx = uuid_index,
		.rae = false,
		.ot = false,
	};

	int ret = nvme_get_log(&args);

	if (ret)
		fprintf(stderr, "NVMe status: %s (0x%x)\n",
			nvme_status_to_string(ret, false), ret);

	if (!ret && !memcmp(scao_guid, &data[SCAO_LPG],
		sizeof(scao_guid)) == 0) {

		fprintf(stderr, "ERROR: Unknown GUID in log page data\n");

		fprintf(stderr, "ERROR: Expected GUID: 0x%"PRIx64"%"PRIx64"\n",
			le64_to_cpu(*(uint64_t *)&scao_guid[8]),
			le64_to_cpu(*(uint64_t *)&scao_guid[0]));

		fprintf(stderr, "ERROR: Actual GUID: 0x%"PRIx64"%"PRIx64"\n",
			le64_to_cpu(*(uint64_t *)&data[SCAO_LPG + 8]),
			le64_to_cpu(*(uint64_t *)&data[SCAO_LPG]));

		ret = -1;
	}

	if (!ret) {
		const enum nvme_print_flags print_flag = validate_output_format(format);

		if (print_flag == JSON)
			print_smart_cloud_log_json(data);
		else if (print_flag == NORMAL)
			print_smart_cloud_log(data);
		else {
			fprintf(stderr, "ERROR: Failed to parse. Unknown output option: %s\n", format);
			ret = -EINVAL;
		}
	}

	return ret;
}

static int ocp_smart_cloud_log(int argc, char **argv, struct command *cmd,
			     struct plugin *plugin)
{
	const char *desc = "Retrieve the extended SMART health data.";
	struct nvme_dev *dev = NULL;
	int ret = 0;

	struct config {
		char *output_format;
	};

	struct config cfg = {
		.output_format = "normal",
	};

	OPT_ARGS(opts) = {
		OPT_FMT("output-format", 'o', &cfg.output_format, "output format: normal|json"),
		OPT_END()
	};

	ret = parse_and_open(&dev, argc, argv, desc, opts);
	if (ret)
		return ret;

	ret = get_smart_cloud_log_page(dev, cfg.output_format);
	if (ret)
		fprintf(stderr, "ERROR: Failure reading the extended SMART health log page, ret = %d\n",
			ret);

	dev_close(dev);
	return ret;
}

static int ocp_print_C3_log_normal(struct nvme_dev *dev,
				   struct ssd_latency_monitor_log *log_data)
{
        printf("-Latency Monitor/C3 Log Page Data- \n");
        printf("  Controller   :  %s\n", dev->name);
        int i, j;
        int pos = 0;
        char       ts_buf[128];

        printf("  Feature Status                     0x%x \n",
                log_data->feature_status);
        printf("  Active Bucket Timer                %d min \n",
                 C0_ACTIVE_BUCKET_TIMER_INCREMENT *
                 le16_to_cpu(log_data->active_bucket_timer));
        printf("  Active Bucket Timer Threshold      %d min \n",
                 C0_ACTIVE_BUCKET_TIMER_INCREMENT *
                 le16_to_cpu(log_data->active_bucket_timer_threshold));
        printf("  Active Threshold A                 %d ms \n",
                 C0_ACTIVE_THRESHOLD_INCREMENT *
                 le16_to_cpu(log_data->active_threshold_a+1));
        printf("  Active Threshold B                 %d ms \n",
                 C0_ACTIVE_THRESHOLD_INCREMENT *
                 le16_to_cpu(log_data->active_threshold_b+1));
        printf("  Active Threshold C                 %d ms \n",
                 C0_ACTIVE_THRESHOLD_INCREMENT *
                 le16_to_cpu(log_data->active_threshold_c+1));
        printf("  Active Threshold D                 %d ms \n",
                 C0_ACTIVE_THRESHOLD_INCREMENT *
                 le16_to_cpu(log_data->active_threshold_d+1));
        printf("  Active Latency Minimum Window      %d ms \n",
                 C0_MINIMUM_WINDOW_INCREMENT *
                 le16_to_cpu(log_data->active_latency_min_window));
        printf("  Active Latency Stamp Units         %d \n",
                 le16_to_cpu(log_data->active_latency_stamp_units));
        printf("  Static Latency Stamp Units         %d \n",
                 le16_to_cpu(log_data->static_latency_stamp_units));
        printf("  Debug Log Trigger Enable           %d \n",
                 le16_to_cpu(log_data->debug_log_trigger_enable));

        printf("                                                            Read                           Write                 Deallocate/Trim \n");
        for (i = 0; i <= 3; i++) {
                printf("  Active Latency Mode: Bucket %d      %27d     %27d     %27d \n",
                        i,
                        log_data->active_latency_config & (1 << pos),
                        log_data->active_latency_config & (1 << pos),
                        log_data->active_latency_config & (1 << pos));
        }
        printf("\n");
        for (i = 0; i <= 3; i++) {
                printf("  Active Bucket Counter: Bucket %d    %27d     %27d     %27d \n",
                        i,
                        le32_to_cpu(log_data->active_bucket_counter[i][READ]),
                        le32_to_cpu(log_data->active_bucket_counter[i][WRITE]),
                        le32_to_cpu(log_data->active_bucket_counter[i][TRIM]));
        }

        for (i = 0; i <= 3; i++) {
                printf("  Active Measured Latency: Bucket %d  %27d ms  %27d ms  %27d ms \n",
                        i,
                        le16_to_cpu(log_data->active_measured_latency[i][READ]),
                        le16_to_cpu(log_data->active_measured_latency[i][WRITE]),
                        le16_to_cpu(log_data->active_measured_latency[i][TRIM]));
        }

        for (i = 0; i <= 3; i++) {
                printf("  Active Latency Time Stamp: Bucket %d    ", i);
                for (j = 0; j <= 2; j++) {
                        if (le64_to_cpu(log_data->active_latency_timestamp[i][j]) == -1)
                                printf("                    N/A         ");
                        else {
                                convert_ts(le64_to_cpu(log_data->active_latency_timestamp[i][j]), ts_buf);
                                printf("%s     ", ts_buf);
                        }
                }
                printf("\n");
        }

        for (i = 0; i <= 3; i++) {
                printf("  Static Bucket Counter: Bucket %d    %27d     %27d     %27d \n",
                        i,
                        le32_to_cpu(log_data->static_bucket_counter[i][READ]),
                        le32_to_cpu(log_data->static_bucket_counter[i][WRITE]),
                        le32_to_cpu(log_data->static_bucket_counter[i][TRIM]));
        }

        for (i = 0; i <= 3; i++) {
                printf("  Static Measured Latency: Bucket %d  %27d ms  %27d ms  %27d ms \n",
                        i,
                        le16_to_cpu(log_data->static_measured_latency[i][READ]),
                        le16_to_cpu(log_data->static_measured_latency[i][WRITE]),
                        le16_to_cpu(log_data->static_measured_latency[i][TRIM]));
        }

        for (i = 0; i <= 3; i++) {
                printf("  Static Latency Time Stamp: Bucket %d    ", i);
                for (j = 0; j <= 2; j++) {
                        if (le64_to_cpu(log_data->static_latency_timestamp[i][j]) == -1)
                                printf("                    N/A         ");
                        else {
                                convert_ts(le64_to_cpu(log_data->static_latency_timestamp[i][j]), ts_buf);
                                printf("%s     ", ts_buf);
                        }
                }
                printf("\n");
        }

        return 0;
}

static void ocp_print_C3_log_json(struct ssd_latency_monitor_log *log_data)
{
        int i, j;
        int pos = 0;
        char	buf[128];
        char    ts_buf[128];
        char	*operation[3] = {"Read", "Write", "Trim"};
        struct json_object *root;
        root = json_create_object();

        json_object_add_value_uint(root, "Feature Status",
                        log_data->feature_status);
        json_object_add_value_uint(root, "Active Bucket Timer",
                        C0_ACTIVE_BUCKET_TIMER_INCREMENT *
                        le16_to_cpu(log_data->active_bucket_timer));
        json_object_add_value_uint(root, "Active Bucket Timer Threshold",
                        C0_ACTIVE_BUCKET_TIMER_INCREMENT *
                        le16_to_cpu(log_data->active_bucket_timer_threshold));
        json_object_add_value_uint(root, "Active Threshold A",
                        C0_ACTIVE_THRESHOLD_INCREMENT *
                        le16_to_cpu(log_data->active_threshold_a+1));
        json_object_add_value_uint(root, "Active Threshold B",
                        C0_ACTIVE_THRESHOLD_INCREMENT *
                        le16_to_cpu(log_data->active_threshold_b+1));
        json_object_add_value_uint(root, "Active Threshold C",
                        C0_ACTIVE_THRESHOLD_INCREMENT *
                        le16_to_cpu(log_data->active_threshold_c+1));
        json_object_add_value_uint(root, "Active Threshold D",
                        C0_ACTIVE_THRESHOLD_INCREMENT *
                        le16_to_cpu(log_data->active_threshold_d+1));
        json_object_add_value_uint(root, "Active Lantency Minimum Window",
                        C0_MINIMUM_WINDOW_INCREMENT *
                        le16_to_cpu(log_data->active_latency_min_window));
        json_object_add_value_uint(root, "Active Latency Stamp Units",
                        le16_to_cpu(log_data->active_latency_stamp_units));
        json_object_add_value_uint(root, "Static Latency Stamp Units",
                        le16_to_cpu(log_data->static_latency_stamp_units));
        json_object_add_value_uint(root, "Debug Log Trigger Enable",
                        le16_to_cpu(log_data->debug_log_trigger_enable));

        for (i = 0; i <= 3; i++) {
                struct json_object *bucket;
                bucket = json_create_object();
                sprintf(buf, "Active Latency Mode: Bucket %d", i);
                for (j = 0; j <= 2; j++) {
                        json_object_add_value_uint(bucket, operation[j],
                                        log_data->active_latency_config & (1 << pos));
                }
                json_object_add_value_object(root, buf, bucket);
        }
        for (i = 0; i <= 3; i++) {
                struct json_object *bucket;
                bucket = json_create_object();
                sprintf(buf, "Active Bucket Counter: Bucket %d", i);
                for (j = 0; j <= 2; j++) {
                        json_object_add_value_uint(bucket, operation[j],
                                        le32_to_cpu(log_data->active_bucket_counter[i][j]));
                }
                json_object_add_value_object(root, buf, bucket);
        }
        for (i = 0; i <= 3; i++) {
                struct json_object *bucket;
                bucket = json_create_object();
                sprintf(buf, "Active Measured Latency: Bucket %d", i);
                for (j = 0; j <= 2; j++) {
                        json_object_add_value_uint(bucket, operation[j],
                                        le16_to_cpu(log_data->active_measured_latency[i][j]));
                }
                json_object_add_value_object(root, buf, bucket);
        }
        for (i = 0; i <= 3; i++) {
                struct json_object *bucket;
                bucket = json_create_object();
                sprintf(buf, "Active Latency Time Stamp: Bucket %d", i);
                for (j = 0; j <= 2; j++) {
                        if (le64_to_cpu(log_data->active_latency_timestamp[i][j]) == -1)
                                json_object_add_value_string(bucket, operation[j], "NA");
                        else {
                                convert_ts(le64_to_cpu(log_data->active_latency_timestamp[i][j]), ts_buf);
                                json_object_add_value_string(bucket, operation[j], ts_buf);
                        }
                }
                json_object_add_value_object(root, buf, bucket);
        }
        for (i = 0; i <= 3; i++) {
                struct json_object *bucket;
                bucket = json_create_object();
                sprintf(buf, "Static Bucket Counter: Bucket %d", i);
                for (j = 0; j <= 2; j++) {
                        json_object_add_value_uint(bucket, operation[j],
                                        le32_to_cpu(log_data->static_bucket_counter[i][j]));
                }
                json_object_add_value_object(root, buf, bucket);
        }
        for (i = 0; i <= 3; i++) {
                struct json_object *bucket;
                bucket = json_create_object();
                sprintf(buf, "Static Measured Latency: Bucket %d", i);
                for (j = 0; j <= 2; j++) {
                        json_object_add_value_uint(bucket, operation[j],
                                        le16_to_cpu(log_data->static_measured_latency[i][j]));
                }
                json_object_add_value_object(root, buf, bucket);
        }
        for (i = 0; i <= 3; i++) {
                struct json_object *bucket;
                bucket = json_create_object();
                sprintf(buf, "Static Latency Time Stamp: Bucket %d", i);
                for (j = 0; j <= 2; j++) {
                        if (le64_to_cpu(log_data->static_latency_timestamp[i][j]) == -1)
                                json_object_add_value_string(bucket, operation[j], "NA");
                        else {
                                convert_ts(le64_to_cpu(log_data->static_latency_timestamp[i][j]), ts_buf);
                                json_object_add_value_string(bucket, operation[j], ts_buf);
                        }
                }
                json_object_add_value_object(root, buf, bucket);
        }

        json_print_object(root, NULL);
        printf("\n");

        json_free_object(root);
}

static int get_c3_log_page(struct nvme_dev *dev, char *format)
{
        int ret = 0;
        int fmt = -1;
        __u8 *data;
        int i;
        struct ssd_latency_monitor_log *log_data;

        fmt = validate_output_format(format);
        if (fmt < 0) {
                fprintf(stderr, "ERROR : OCP : invalid output format\n");
                return fmt;
        }

        if ((data = (__u8 *) malloc(sizeof(__u8) * C3_LATENCY_MON_LOG_BUF_LEN)) == NULL) {
                fprintf(stderr, "ERROR : OCP : malloc : %s\n", strerror(errno));
                return -1;
        }
        memset(data, 0, sizeof (__u8) * C3_LATENCY_MON_LOG_BUF_LEN);

        ret = nvme_get_log_simple(dev_fd(dev), C3_LATENCY_MON_OPCODE,
                                  C3_LATENCY_MON_LOG_BUF_LEN, data);

        if (strcmp(format, "json"))
                fprintf(stderr,
                        "NVMe Status:%s(%x)\n",
                        nvme_status_to_string(ret, false),
                        ret);

        if (ret == 0) {
                log_data = (struct ssd_latency_monitor_log*)data;

                /* check log page version */
                if (log_data->log_page_version != C3_LATENCY_MON_VERSION) {
                        fprintf(stderr,
                                "ERROR : OCP : invalid latency monitor version\n");
                        ret = -1;
                        goto out;
                }

                /* check log page guid */
                /* Verify GUID matches */
                for (i=0; i<16; i++) {
                        if (lat_mon_guid[i] != log_data->log_page_guid[i]) {
                                fprintf(stderr,"ERROR : OCP : Unknown GUID in C3 Log Page data\n");
                                int j;
                                fprintf(stderr, "ERROR : OCP : Expected GUID: 0x");
                                for (j = 0; j<16; j++) {
                                        fprintf(stderr, "%x", lat_mon_guid[j]);
                                }
                                fprintf(stderr, "\nERROR : OCP : Actual GUID: 0x");
                                for (j = 0; j<16; j++) {
                                        fprintf(stderr, "%x", log_data->log_page_guid[j]);
                                }
                                fprintf(stderr, "\n");

                                ret = -1;
                                goto out;
                        }
                }

                switch (fmt) {
                case NORMAL:
                        ocp_print_C3_log_normal(dev, log_data);
                        break;
                case JSON:
                        ocp_print_C3_log_json(log_data);
                        break;
                }
        } else {
                fprintf(stderr,
                        "ERROR : OCP : Unable to read C3 data from buffer\n");
        }

out:
        free(data);
        return ret;
}

static int ocp_latency_monitor_log(int argc, char **argv, struct command *command,
                struct plugin *plugin)
{
        const char *desc = "Retrieve latency monitor log data.";
	struct nvme_dev *dev;
        int ret = 0;

        struct config {
                char *output_format;
        };

        struct config cfg = {
                .output_format = "normal",
        };

        OPT_ARGS(opts) = {
                OPT_FMT("output-format", 'o', &cfg.output_format,
                        "output Format: normal|json"),
                OPT_END()
        };

        ret = parse_and_open(&dev, argc, argv, desc, opts);
        if (ret)
                return ret;

        ret = get_c3_log_page(dev, cfg.output_format);
        if (ret)
                fprintf(stderr,
                        "ERROR : OCP : Failure reading the C3 Log Page, ret = %d\n",
                        ret);
        dev_close(dev);
        return ret;
}

static int clear_fw_update_history(int argc, char **argv, struct command *cmd,
				   struct plugin *plugin)
{
	return ocp_clear_fw_update_history(argc, argv, cmd, plugin);
}
