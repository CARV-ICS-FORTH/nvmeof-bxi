#ifndef PTL_CONFIG_H
#define PTL_CONFIG_H

#define PTL_TARGET_PID 2056

#define PTL_SPDK_PROTOCOL_VERSION 1UL
/**
  * Portal index number where the actual nvme commands and data are transfered
**/
#define PTL_PT_INDEX 1


/*
 * PTL_ENABLE_IOVEC_RECEIVE
 *
 * When enabled (set to 1), the target posts receive buffers using a Portals
 * I/O vector (PTL_IOVEC) ME. This ME always contains exactly two iovec
 * entries per posted receive:
 *
 *   - iovec[0]: 64 bytes, reserved for the NVMe command capsule
 *   - iovec[1]: X bytes, user-configurable at volume creation time, used
 *               for inline data (e.g., small writes fully inlined so a 4 KiB
 *               write can be completed with a single send)
 *
 * This mode relies on PTL_IOVEC being honored by the provider, with:
 *   me.start  = pointer to ptl_iovec_t array
 *   me.length = number of iovec entries (2)
 *   me.options including PTL_ME_OP_PUT | PTL_IOVEC and related flags.
 *
 * When disabled (set to 0), the inlining feature is considered unavailable.
 * The code paths that depend on inline data placement must not be used, or
 * the code will not function correctly. In this mode, only the 64-byte NVMe
 * command buffer is posted (no inline payload buffer).
 *
 * Test guidance:
 *   For conformance and to avoid provider/ABI issues with iovec handling,
 *   tests may set the inline data size to the smallest supported value
 *   (e.g., 256 bytes). This minimizes exposure to iovec misconfiguration
 *   while still exercising the inline path when PTL_ENABLE_IOVEC_RECEIVE=1.
 *
 * Notes:
 *   - Ensure wr->num_sge <= provider max_iovecs and that the second iovec’s
 *     length (X) does not exceed PTL_SIZE_MAX.
 *   - Event semantics: in iovec mode, event.start will equal iovec[0].iov_base
 *     (the first buffer), not the address of the iovec array.
 */
#define PTL_ENABLE_IOVEC_RECEIVE 0  /* set to 0 to disable iovec-based receives */


/**
 * PTL_ENABLE_BIND_PER_OP
 *
 * Enables an experimental feature where the buffer is bound on the fly
 * prior to any PtlGet or PtlPut operation. This feature may be removed
 * in future commits if it proves to add a lot of overhead.
 */
#define PTL_ENABLE_BIND_PER_OP 0


/**
 * Portal index number where clients use during rdma_connect to notify the
 * targer a)about their presence and b) enable the target to build a lookup
 * table from queue pair id to nid,pid that Portals uses for point to point
 * communication.
 */
#define PTL_CP_SERVER_PTE 128

/**
 * Number of recv buffers posted through PtlLEAppend for receiving new
 *connection info from clients.
 **/
#define PTL_CONTROL_PLANE_NUM_RECV_BUFFERS 2048U

/**
 * Size of the Portals event queue
**/
#define PTL_CQ_SIZE 4096


// #define PTL_RMA_ME_OPTS                                                                                                 \
// 	PTL_ME_OP_PUT | PTL_ME_OP_GET | PTL_ME_EVENT_LINK_DISABLE | PTL_ME_MAY_ALIGN | PTL_ME_IS_ACCESSIBLE | \
// 		PTL_ME_NO_TRUNCATE
//gilles staff
#define PTL_RMA_ME_OPTS PTL_ME_OP_PUT | PTL_ME_OP_GET | PTL_ME_EVENT_LINK_DISABLE | PTL_ME_EVENT_UNLINK_DISABLE | PTL_ME_EVENT_COMM_DISABLE

#define PTL_SRV_ME_OPTS   (PTL_ME_OP_PUT | PTL_ME_EVENT_LINK_DISABLE | PTL_ME_MAY_ALIGN | PTL_ME_IS_ACCESSIBLE | PTL_ME_NO_TRUNCATE | PTL_ME_USE_ONCE)


#define PTL_IOVEC_SIZE 2


#endif

