#ifndef PTL_CONFIG_H
#define PTL_CONFIG_H

#define PTL_TARGET_PID 2056

#define PTL_SPDK_PROTOCOL_VERSION 1UL
/**
  * Portal index number where the actual nvme commands and data are transfered
**/
#define PTL_PT_INDEX 1

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
#define PTL_CQ_SIZE 128


// #define PTL_RMA_ME_OPTS                                                                                                 \
// 	PTL_ME_OP_PUT | PTL_ME_OP_GET | PTL_ME_EVENT_LINK_DISABLE | PTL_ME_MAY_ALIGN | PTL_ME_IS_ACCESSIBLE | \
// 		PTL_ME_NO_TRUNCATE
//gilles staff
#define PTL_RMA_ME_OPTS PTL_ME_OP_PUT | PTL_ME_OP_GET | PTL_ME_EVENT_LINK_DISABLE | PTL_ME_EVENT_UNLINK_DISABLE | PTL_ME_EVENT_COMM_DISABLE

#define PTL_SRV_ME_OPTS   PTL_ME_OP_PUT | PTL_ME_EVENT_LINK_DISABLE | PTL_ME_MAY_ALIGN | PTL_ME_IS_ACCESSIBLE | PTL_ME_NO_TRUNCATE | PTL_LE_USE_ONCE


#define PTL_IOVEC_SIZE 2


#endif

