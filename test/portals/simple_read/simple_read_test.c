#include "spdk/env.h"
#include "spdk/log.h"
#include "spdk/nvme.h"
#include "spdk/nvme_zns.h"
#include "spdk/stdinc.h"
#include "spdk/string.h"
#include "spdk/vmd.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#define CLOCK_FREQ 2900000000.0
#define STARTING_LBA 8192UL
#define PROGRESS_REPORTING_PERIOD 65536U


#define RTEST_PTL_INFO(fmt, ...)                                             \
    do {                                                                      \
        time_t t = time(NULL);                                                \
        struct tm *tm = localtime(&t);                                        \
        char timestamp[32];                                                   \
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm);      \
        fprintf(stderr, "\033[32mBENCH: [PTL_INFO][%s][%s:%s:%d] " fmt "\033[0m\n",  \
                timestamp, __FILE__, __func__, __LINE__, ##__VA_ARGS__);      \
    } while (0)

#define RTEST_PTL_FATAL(fmt, ...)                                             \
    do {                                                                      \
        time_t t = time(NULL);                                                \
        struct tm *tm = localtime(&t);                                        \
        char timestamp[32];                                                   \
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm);      \
        fprintf(stderr, "\x1b[31mBENCH: [PTL_FATAL][%s][%s:%s:%d] " fmt "\x1b[0m\n", \
                timestamp, __FILE__, __func__, __LINE__, ##__VA_ARGS__);      \
        raise(SIGINT);  \
        _exit(EXIT_FAILURE);                                                    \
    } while (0)


static bool g_check_data;
static uint64_t g_expected_checksum;

static uint64_t calculate_crc64(const void *data, size_t length)
{
	uint64_t crc = 0xFFFFFFFFFFFFFFFFULL;
	const unsigned char *p = data;

	for (size_t i = 0; i < length; i++) {
		crc ^= (uint64_t)p[i];
		for (int j = 0; j < 8; j++) {
			if (crc & 1) {
				crc = (crc >> 1) ^ 0xC96C5795D7870F42ULL;
			} else {
				crc >>= 1;
			}
		}
	}
	return crc ^ 0xFFFFFFFFFFFFFFFFULL;
}

// RDTSC inline function to read timestamp counter
static inline uint64_t rdtsc(void)
{
	uint32_t lo, hi;
	__asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
	return ((uint64_t)hi << 32) | lo;
}

struct ctrlr_entry {
	struct spdk_nvme_ctrlr *ctrlr;
	struct ctrlr_entry *next;
	char name[1024];
};

struct ns_entry {
	struct spdk_nvme_ctrlr *ctrlr;
	struct spdk_nvme_ns *ns;
	struct ns_entry *next;
	struct spdk_nvme_qpair *qpair;
};

struct io_request {
	void *buf;
	bool complete;
	struct ns_entry *ns_entry;
};

static struct ctrlr_entry *g_controllers = NULL;
static struct ns_entry *g_namespaces = NULL;
static uint32_t g_queue_depth = 0;
static uint32_t g_request_size = 0;
static uint32_t g_outstanding_commands = 0;
static uint32_t g_total_ios = 0;
static uint32_t g_submitted_ios = 0;
static uint32_t g_completed_ios = 0;
static uint64_t g_start_tsc = 0;
static uint64_t g_end_tsc = 0;

static void
io_complete(void *arg, const struct spdk_nvme_cpl *completion)
{
	struct io_request *req = arg;
	uint64_t crc;

	req->complete = true;
	g_outstanding_commands--;
	g_completed_ios++;

	if (spdk_nvme_cpl_is_error(completion)) {
		spdk_nvme_qpair_print_completion(req->ns_entry->qpair, (struct spdk_nvme_cpl *)completion);
		RTEST_PTL_FATAL("I/O error status: %s\n", spdk_nvme_cpl_get_status_string(&completion->status));
	}

	if (g_check_data && !spdk_nvme_cpl_is_error(completion)) {
		crc = calculate_crc64(req->buf, g_request_size);
		if (crc != g_expected_checksum) {
			RTEST_PTL_FATAL("Checksum mismatch! expected=%016lx got=%016lx",
					g_expected_checksum, crc);
		}
		// RTEST_PTL_INFO("Checksum test passed!");
	}
}

static void init_pattern_and_write(struct ns_entry *ns_entry)
{
	void *buf = spdk_zmalloc(g_request_size, 0x1000, NULL,
				 SPDK_ENV_SOCKET_ID_ANY, SPDK_MALLOC_DMA);
	if (!buf) {
		RTEST_PTL_FATAL(
			"Failed to allocate DMA buffer for init pattern");
	}

	// Fill with random
	unsigned char *p = buf;
	for (uint32_t i = 0; i < g_request_size; i++) {
		p[i] = rand() & 0xFF;
	}

	g_expected_checksum = calculate_crc64(buf, g_request_size);
	RTEST_PTL_INFO("Generated checksum for init block: %016lx...",
		       g_expected_checksum);

	uint32_t lba_count =
		g_request_size / spdk_nvme_ns_get_sector_size(ns_entry->ns);

	int rc = spdk_nvme_ns_cmd_write(ns_entry->ns, ns_entry->qpair, buf,
					STARTING_LBA, lba_count, NULL, NULL, 0);
	if (rc != 0) {
		RTEST_PTL_FATAL("Failed to submit initial write");
	}

	// Process completions for the write
	while (spdk_nvme_qpair_process_completions(ns_entry->qpair, 0) == 0) {
		// spin until write completes
	}
	RTEST_PTL_INFO("Generated checksum for init block: %016lx...DONE",
		       g_expected_checksum);

	spdk_free(buf);
}

static void
submit_single_io(struct ns_entry *ns_entry, struct io_request *req)
{
	int rc;
	uint32_t lba_count = g_request_size / spdk_nvme_ns_get_sector_size(ns_entry->ns);

	req->complete = false;

	rc = spdk_nvme_ns_cmd_read(ns_entry->ns, ns_entry->qpair, req->buf,
				   STARTING_LBA, lba_count, io_complete, req, 0);

	if (rc != 0) {
		RTEST_PTL_FATAL("Failed to submit read I/O");
		_exit(EXIT_FAILURE);
	}

	g_outstanding_commands++;
	g_submitted_ios++;
}

static void
cleanup(void)
{
	struct ns_entry *ns_entry, *next_ns;
	struct ctrlr_entry *ctrlr_entry, *next_ctrlr;

	ns_entry = g_namespaces;
	while (ns_entry) {
		next_ns = ns_entry->next;

		if (ns_entry->qpair) {
			spdk_nvme_ctrlr_free_io_qpair(ns_entry->qpair);
		}

		free(ns_entry);
		ns_entry = next_ns;
	}

	ctrlr_entry = g_controllers;
	while (ctrlr_entry) {
		next_ctrlr = ctrlr_entry->next;
		spdk_nvme_detach(ctrlr_entry->ctrlr);
		free(ctrlr_entry);
		ctrlr_entry = next_ctrlr;
	}

	spdk_env_fini();
}

static bool
probe_cb(void *cb_ctx, const struct spdk_nvme_transport_id *trid,
	 struct spdk_nvme_ctrlr_opts *opts)
{
	RTEST_PTL_INFO("Attaching to %s", trid->traddr);
	return true;
}

static void
attach_cb(void *cb_ctx, const struct spdk_nvme_transport_id *trid,
	  struct spdk_nvme_ctrlr *ctrlr, const struct spdk_nvme_ctrlr_opts *opts)
{
	int nsid;
	struct ctrlr_entry *entry;
	struct spdk_nvme_ns *ns;
	const struct spdk_nvme_ctrlr_data *cdata;

	entry = malloc(sizeof(struct ctrlr_entry));
	if (entry == NULL) {
		perror("ctrlr_entry malloc");
		exit(1);
	}

	cdata = spdk_nvme_ctrlr_get_data(ctrlr);

	snprintf(entry->name, sizeof(entry->name), "%-20.20s (%-20.20s)", cdata->mn, cdata->sn);

	entry->ctrlr = ctrlr;
	entry->next = g_controllers;
	g_controllers = entry;

	for (nsid = spdk_nvme_ctrlr_get_first_active_ns(ctrlr);
	     nsid != 0; nsid = spdk_nvme_ctrlr_get_next_active_ns(ctrlr, nsid)) {

		ns = spdk_nvme_ctrlr_get_ns(ctrlr, nsid);
		if (ns == NULL) {
			continue;
		}

		struct ns_entry *ns_entry = malloc(sizeof(struct ns_entry));
		if (ns_entry == NULL) {
			perror("ns_entry malloc");
			exit(1);
		}

		ns_entry->ctrlr = ctrlr;
		ns_entry->ns = ns;
		ns_entry->next = g_namespaces;
		g_namespaces = ns_entry;

		printf("Namespace ID: %d size: %juGB\n", spdk_nvme_ns_get_id(ns),
		       spdk_nvme_ns_get_size(ns) / 10000);
	}
}

static void run_io_loop(bool check_data)
{
	struct ns_entry *ns_entry;
	struct io_request *requests;
	uint32_t io_progress_counter = PROGRESS_REPORTING_PERIOD;
	uint32_t i;

	ns_entry = g_namespaces;
	if (!ns_entry) {
		RTEST_PTL_FATAL("No namespaces found");
	}

	// Allocate I/O queue pair
	ns_entry->qpair = spdk_nvme_ctrlr_alloc_io_qpair(ns_entry->ctrlr, NULL, 0);
	if (ns_entry->qpair == NULL) {
		RTEST_PTL_FATAL("ERROR: spdk_nvme_ctrlr_alloc_io_qpair() failed");
	}

	spdk_nvme_ctrlr_process_admin_completions(ns_entry->ctrlr);

	if (check_data) {
		init_pattern_and_write(g_namespaces);
	}

	// Allocate request structures
	requests = calloc(g_queue_depth, sizeof(struct io_request));
	if (!requests) {
		RTEST_PTL_FATAL("Failed to allocate request structures");
	}

	// Allocate buffers for each request
	for (i = 0; i < g_queue_depth; i++) {
		requests[i].buf = spdk_zmalloc(g_request_size, 0x1000, NULL, SPDK_ENV_SOCKET_ID_ANY,
					       SPDK_MALLOC_DMA);
		if (!requests[i].buf) {
			RTEST_PTL_FATAL("Failed to allocate DMA buffer for request %d", i);
			return;
		}
		RTEST_PTL_INFO("Buffer no %d to land the dmas from the target: %lu", i, (uint64_t)requests[i].buf);
		requests[i].ns_entry = ns_entry;
		requests[i].complete = true;
	}

	RTEST_PTL_INFO("Starting I/O loop with queue depth %d, request size %d bytes, LBA %lu, total IOs %d\n",
		       g_queue_depth, g_request_size, STARTING_LBA, g_total_ios);

	// Record start timestamp
	g_start_tsc = rdtsc();

	// Main I/O loop - run until all IOs are completed
	while (g_completed_ios < g_total_ios) {
		// Submit new I/Os to maintain queue depth (but don't exceed total_ios)
		for (i = 0; i < g_queue_depth; i++) {
			if (requests[i].complete &&
			    g_outstanding_commands < g_queue_depth &&
			    g_submitted_ios < g_total_ios) {
				submit_single_io(ns_entry, &requests[i]);
			}
		}

		// Process completions
		spdk_nvme_qpair_process_completions(ns_entry->qpair, 0);
		spdk_nvme_ctrlr_process_admin_completions(ns_entry->ctrlr);
		if (g_completed_ios > io_progress_counter) {
			RTEST_PTL_INFO("Progress: completed IOs: %u Total IOs: %u", g_completed_ios, g_total_ios);
			io_progress_counter += PROGRESS_REPORTING_PERIOD;
		}
		// Small delay to prevent busy waiting
		// usleep(1);
	}
	RTEST_PTL_INFO("*COMPLETE* Total Progress: completed IOs: %u Submitted IOs: %u", g_completed_ios,
		       g_total_ios);

	// Record end timestamp
	g_end_tsc = rdtsc();

	RTEST_PTL_INFO("I/O test completed. Submitted: %d, Completed: %d",
		       g_submitted_ios, g_completed_ios);

	// Calculate and display throughput
	uint64_t total_cycles = g_end_tsc - g_start_tsc;
	uint64_t total_bytes = (uint64_t)g_completed_ios * g_request_size;

	RTEST_PTL_INFO("=== THROUGHPUT RESULTS ===");
	RTEST_PTL_INFO("Start TSC: %lu", g_start_tsc);
	RTEST_PTL_INFO("End TSC: %lu", g_end_tsc);
	RTEST_PTL_INFO("Total cycles: %lu", total_cycles);
	RTEST_PTL_INFO("Total bytes transferred: %lu", total_bytes);
	RTEST_PTL_INFO("Cycles per IO: %.2f", (double)total_cycles / g_completed_ios);

	double estimated_time_seconds = (double)total_cycles / CLOCK_FREQ;
	double throughput_mbps = (double)total_bytes / (1024.0 * 1024.0) / estimated_time_seconds;
  double iops = g_completed_ios / estimated_time_seconds;

	RTEST_PTL_INFO("Estimated time: %.6f seconds", estimated_time_seconds);
	RTEST_PTL_INFO("Estimated throughput: %.2f MB/s", throughput_mbps);
	RTEST_PTL_INFO("Estimated IOPs: %.2f", iops);

	// Cleanup buffers
	for (i = 0; i < g_queue_depth; i++) {
		if (requests[i].buf) {
			spdk_free(requests[i].buf);
		}
	}
	free(requests);
}

int main(int argc, char **argv)
{
	int rc;
	struct spdk_env_opts opts;

	if (argc < 5 || argc > 6) {
		RTEST_PTL_INFO("Usage: %s <transport_string> <queue_depth> <request_size_bytes> <total_ios> [--check_data]",
			       argv[0]);
		return 1;
	}

	const char *trid_str = argv[1];
	g_queue_depth = atoi(argv[2]);
	g_request_size = atoi(argv[3]);
	g_total_ios = atoi(argv[4]);

	if (argc == 6 && strcmp(argv[5], "--check_data") == 0) {
		g_check_data = true;
		RTEST_PTL_INFO("Checksum validation enabled");
	}

	if (g_queue_depth <= 0 || g_request_size <= 0) {
		RTEST_PTL_FATAL("Queue depth and request size must be positive integers");
		return 1;
	}

	RTEST_PTL_INFO("Transport: %s", trid_str);
	RTEST_PTL_INFO("Queue Depth: %d\n", g_queue_depth);
	RTEST_PTL_INFO("Request Size: %d bytes\n", g_request_size);
	RTEST_PTL_INFO("Total IOs: %d\n", g_total_ios);
	RTEST_PTL_INFO("Starting LBA: %lu\n", STARTING_LBA);

	spdk_env_opts_init(&opts);
	opts.name = "spdk_read_test";
	opts.iova_mode = "va";

	if (spdk_env_init(&opts) < 0) {
		RTEST_PTL_FATAL("Unable to initialize SPDK env");
		return 1;
	}

	// Parse transport ID from command line
	struct spdk_nvme_transport_id trid = {};
	if (spdk_nvme_transport_id_parse(&trid, trid_str) != 0) {
		RTEST_PTL_FATAL("Invalid transport string: %s", trid_str);
		cleanup();
		return 1;
	}

	RTEST_PTL_INFO("Initializing NVMe Controllers with transport: %s", trid_str);

	rc = spdk_nvme_probe(&trid, NULL, probe_cb, attach_cb, NULL);
	if (rc != 0) {
		RTEST_PTL_FATAL("spdk_nvme_probe() failed");
		cleanup();
		return 1;
	}

	if (g_controllers == NULL) {
		RTEST_PTL_FATAL("No NVMe controllers found");
		cleanup();
		return 1;
	}


	RTEST_PTL_INFO("Initialization complete. Starting I/O.");
	run_io_loop(true);

	cleanup();
	return 0;
}

