#include "../../lib/rdma_provider/ptl_log.h"
#include <spdk/env.h>
#include <spdk/log.h>
#include <spdk/nvme.h>
#include <spdk/string.h>

#define MAX_VOLUMES 16
#define IO_SIZE 4096
#define NUM_IO_QUEUES 4

struct volume_context {
	struct spdk_nvme_ctrlr *ctrlr;
	struct spdk_nvme_ns *ns;
	struct spdk_nvme_qpair *qpair[NUM_IO_QUEUES];
	uint32_t nsid;
	char *transport_id;
	bool connected;
	int volume_id;
};

struct test_context {
	struct volume_context volumes[MAX_VOLUMES];
	int num_volumes;
	int completed_ios;
	int total_ios;
	bool test_complete;
};

static struct test_context g_test_ctx = {0};

static void
io_complete_cb(void *arg, const struct spdk_nvme_cpl *completion)
{
	struct volume_context *vol_ctx = (struct volume_context *)arg;

	if (spdk_nvme_cpl_is_error(completion)) {
		SPDK_PTL_FATAL("Volume %d: I/O error - status: %s",
			       vol_ctx->volume_id,
			       spdk_nvme_cpl_get_status_string(&completion->status));
	} else {
		SPDK_PTL_INFO("Volume %d: Read completed successfully (NSID: %u)",
			      vol_ctx->volume_id, vol_ctx->nsid);
	}

	g_test_ctx.completed_ios++;

	if (g_test_ctx.completed_ios >= g_test_ctx.total_ios) {
		g_test_ctx.test_complete = true;
	}
}

static bool
probe_cb(void *cb_ctx, const struct spdk_nvme_transport_id *trid,
	 struct spdk_nvme_ctrlr_opts *opts)
{
	SPDK_PTL_INFO("Probing controller: %s\\n", trid->traddr);
	return true;
}

static void
attach_cb(void *cb_ctx, const struct spdk_nvme_transport_id *trid,
	  struct spdk_nvme_ctrlr *ctrlr,
	  const struct spdk_nvme_ctrlr_opts *opts)
{
	struct volume_context *vol_ctx = (struct volume_context *)cb_ctx;
	struct spdk_nvme_ns *ns;
	uint32_t nsid;

	SPDK_PTL_INFO("Volume %d: Controller attached: %s", vol_ctx->volume_id, trid->traddr);

	vol_ctx->ctrlr = ctrlr;

	// Find the first active namespace
	for (nsid = spdk_nvme_ctrlr_get_first_active_ns(ctrlr);
	     nsid != 0;
	     nsid = spdk_nvme_ctrlr_get_next_active_ns(ctrlr, nsid)) {

		ns = spdk_nvme_ctrlr_get_ns(ctrlr, nsid);
		if (ns == NULL) {
			continue;
		}

		if (!spdk_nvme_ns_is_active(ns)) {
			continue;
		}

		vol_ctx->ns = ns;
		vol_ctx->nsid = nsid;
		SPDK_PTL_INFO("Volume %d: Found active namespace %u\\n", vol_ctx->volume_id, nsid);
		break;
	}

	if (vol_ctx->ns == NULL) {
		SPDK_PTL_INFO("Volume %d: No active namespace found", vol_ctx->volume_id);
		return;
	}

	// Create I/O queue pairs
	for (int i = 0; i < NUM_IO_QUEUES; i++) {
		vol_ctx->qpair[i] = spdk_nvme_ctrlr_alloc_io_qpair(ctrlr, NULL, 0);
		if (vol_ctx->qpair[i] == NULL) {
			SPDK_PTL_INFO("Volume %d: Failed to allocate I/O queue pair %d", vol_ctx->volume_id, i);
			return;
		}
		SPDK_PTL_INFO("Volume %d: Created I/O queue pair %d", vol_ctx->volume_id, i);
	}

	vol_ctx->connected = true;
}

static int
connect_to_volume(int volume_id, const char *transport_addr, const char *transport_svcid,
		  const char *subnqn)
{
	struct volume_context *vol_ctx = &g_test_ctx.volumes[volume_id];
	struct spdk_nvme_transport_id trid = {};
	int rc;

	vol_ctx->volume_id = volume_id;

	// Setup transport ID
	snprintf(trid.traddr, sizeof(trid.traddr), "%s", transport_addr);
	snprintf(trid.trsvcid, sizeof(trid.trsvcid), "%s", transport_svcid);
	snprintf(trid.subnqn, sizeof(trid.subnqn), "%s", subnqn);
	trid.trtype = SPDK_NVME_TRANSPORT_RDMA;
	trid.adrfam = SPDK_NVMF_ADRFAM_IPV4;

	SPDK_PTL_INFO("Volume %d: Connecting to %s:%s (subnqn: %s)",
		      volume_id, transport_addr, transport_svcid, subnqn);

	// Probe and attach
	rc = spdk_nvme_probe(&trid, vol_ctx, probe_cb, attach_cb, NULL);
	if (rc != 0) {
		SPDK_PTL_FATAL("Volume %d: Failed to probe NVMe controller", volume_id);
		return rc;
	}

	return 0;
}

static void
send_read_commands(void)
{
	void *buffer;
	int rc;

	// Allocate DMA buffer
	buffer = spdk_dma_zmalloc(IO_SIZE, 0x1000, NULL);
	if (buffer == NULL) {
		SPDK_PTL_FATAL("Failed to allocate DMA buffer");
		return;
	}

	SPDK_PTL_INFO("\\n=== Sending Read Commands ===\\n");

	for (int i = 0; i < g_test_ctx.num_volumes; i++) {
		struct volume_context *vol_ctx = &g_test_ctx.volumes[i];

		if (!vol_ctx->connected || vol_ctx->ns == NULL) {
			SPDK_PTL_INFO("Volume %d: Skipping - not connected\\n", i);
			continue;
		}

		// Send read command using the first I/O queue pair
		rc = spdk_nvme_ns_cmd_read(vol_ctx->ns, vol_ctx->qpair[0], buffer,
					   0, /* LBA start */
					   1, /* number of LBAs */
					   io_complete_cb, vol_ctx, 0);

		if (rc != 0) {
			SPDK_PTL_INFO("Volume %d: Failed to submit read command\\n", i);
		} else {
			SPDK_PTL_INFO("Volume %d: Read command submitted (using qpair[0])\\n", i);
			g_test_ctx.total_ios++;
		}
	}

	SPDK_PTL_INFO("Total I/Os submitted: %d\\n", g_test_ctx.total_ios);

	// Process completions
	while (!g_test_ctx.test_complete) {
		for (int i = 0; i < g_test_ctx.num_volumes; i++) {
			struct volume_context *vol_ctx = &g_test_ctx.volumes[i];

			if (!vol_ctx->connected) {
				continue;
			}

			for (int j = 0; j < NUM_IO_QUEUES; j++) {
				if (vol_ctx->qpair[j] != NULL) {
					spdk_nvme_qpair_process_completions(vol_ctx->qpair[j], 0);
				}
			}
		}

		if (g_test_ctx.total_ios == 0) {
			break;
		}
	}

	spdk_dma_free(buffer);
}

static void
cleanup_volumes(void)
{
	SPDK_PTL_INFO("\\n=== Cleaning up ===\\n");

	for (int i = 0; i < g_test_ctx.num_volumes; i++) {
		struct volume_context *vol_ctx = &g_test_ctx.volumes[i];

		if (!vol_ctx->connected) {
			continue;
		}

		// Free I/O queue pairs
		for (int j = 0; j < NUM_IO_QUEUES; j++) {
			if (vol_ctx->qpair[j] != NULL) {
				spdk_nvme_ctrlr_free_io_qpair(vol_ctx->qpair[j]);
				SPDK_PTL_INFO("Volume %d: Freed I/O queue pair %d\\n", i, j);
			}
		}

		// Detach controller
		if (vol_ctx->ctrlr != NULL) {
			spdk_nvme_detach(vol_ctx->ctrlr);
			SPDK_PTL_INFO("Volume %d: Controller detached\\n", i);
		}
	}
}

int
main(int argc, char **argv)
{
	struct spdk_env_opts opts;
	int rc;

	if (argc < 2) {
		SPDK_PTL_INFO("Usage: %s <num_volumes> [transport_addr] [transport_svcid]\\n", argv[0]);
		SPDK_PTL_INFO("Example: %s 3 192.168.1.100 4420\\n", argv[0]);
		return 1;
	}

	int num_volumes = atoi(argv[1]);
	const char *transport_addr = (argc > 2) ? argv[2] : "192.168.1.100";
	const char *transport_svcid = (argc > 3) ? argv[3] : "4420";

	if (num_volumes <= 0 || num_volumes > MAX_VOLUMES) {
		SPDK_PTL_INFO("Number of volumes must be between 1 and %d\\n", MAX_VOLUMES);
		return 1;
	}

	g_test_ctx.num_volumes = num_volumes;

	SPDK_PTL_INFO("=== SPDK NVMe-oF Multi-Volume Test ===\\n");
	SPDK_PTL_INFO("Volumes to connect: %d\\n", num_volumes);
	SPDK_PTL_INFO("Target address: %s:%s\\n", transport_addr, transport_svcid);
	SPDK_PTL_INFO("I/O queues per volume: %d\\n", NUM_IO_QUEUES);

	// Initialize SPDK environment
	spdk_env_opts_init(&opts);
	opts.name = "nvmeof_test";

	if (spdk_env_init(&opts) < 0) {
		SPDK_PTL_INFO("Failed to initialize SPDK environment\\n");
		return 1;
	}

	SPDK_PTL_INFO("\\n=== Connecting to Volumes ===\\n");

	// Connect to multiple volumes (assuming different subsystems)
	for (int i = 0; i < num_volumes; i++) {
		char subnqn[256];
		snprintf(subnqn, sizeof(subnqn), "nqn.2016-06.io.spdk:cnode%d", i + 1);

		rc = connect_to_volume(i, transport_addr, transport_svcid, subnqn);
		if (rc != 0) {
			SPDK_PTL_INFO("Failed to connect to volume %d\\n", i);
		}

		// Small delay between connections
		usleep(100000); // 100ms
	}

	// Wait a bit for all connections to establish
	sleep(2);

	// Send read commands
	send_read_commands();

	SPDK_PTL_INFO("\\n=== Test Results ===\\n");
	SPDK_PTL_INFO("Connected volumes: ");
	for (int i = 0; i < num_volumes; i++) {
		if (g_test_ctx.volumes[i].connected) {
			SPDK_PTL_INFO("%d ", i);
		}
	}
	SPDK_PTL_INFO("\\n");
	SPDK_PTL_INFO("Completed I/Os: %d/%d\\n", g_test_ctx.completed_ios, g_test_ctx.total_ios);

	// Cleanup
	cleanup_volumes();

	spdk_env_fini();

	return 0;
}

