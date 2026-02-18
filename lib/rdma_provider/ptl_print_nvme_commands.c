#include "ptl_print_nvme_commands.h"
#include "ptl_log.h"
#include "spdk/nvme.h"
#include "spdk/nvme_spec.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
// Function to print NVMe command in human-readable format
int ptl_print_nvme_cmd(const struct spdk_nvme_cmd *cmd, const char *prefix, int cq_id)
{
	const char *command;
	if (!cmd) {
		SPDK_PTL_INFO("NVMe-cmd: NULL command");
		return 1;
	}
	// Interpret common opcodes
	switch (cmd->opc) {
	case SPDK_NVME_OPC_READ:
		command = "READ";
		break;
	case SPDK_NVME_OPC_WRITE:
		command = "WRITE";
		break;
	case SPDK_NVME_OPC_FLUSH:
		command = "FLUSH";
		break;
	case SPDK_NVME_OPC_IDENTIFY:
		command = "IDENTIFY";
		break;

	/* 0x03 - reserved */
	case SPDK_NVME_OPC_WRITE_UNCORRECTABLE:
		command = "WRITE_UNCORRECTABLE";
		break;
	case SPDK_NVME_OPC_COMPARE:
		command = "OPC_COMPARE";
		break;
	case SPDK_NVME_OPC_WRITE_ZEROES:
		command = "OPC_WRITE_ZEROES";
		break;
	case SPDK_NVME_OPC_DATASET_MANAGEMENT:
		command = "DATASET_MANAGEMENT";
		break;
	case SPDK_NVME_OPC_VERIFY:
		command = "OPC_VERIFY";
		break;
	case SPDK_NVME_OPC_RESERVATION_REGISTER:
		command = "RESERVATION_REGISTER";
		break;
	case SPDK_NVME_OPC_RESERVATION_REPORT:
		command = "RESERVATION_REPORT";
		break;
	case SPDK_NVME_OPC_RESERVATION_ACQUIRE:
		command = "RESERVATION_ACQUIRE";
		break;
	case SPDK_NVME_OPC_IO_MANAGEMENT_RECEIVE:
		command = "MANAGEMENT_RECEIVE";
		break;
	case SPDK_NVME_OPC_RESERVATION_RELEASE:
		command = "OPC_RESERVATION_RELEASE";
		break;
	case SPDK_NVME_OPC_COPY:
		command = "OPC_COPY";
		break;
	case SPDK_NVME_OPC_IO_MANAGEMENT_SEND:
		command = "OPC_IO_MANAGEMET";
		break;
	case 0x7f: // NVMe-oF Fabric Command (CONNECT)
		command = "FABRIC_CONNECT";
		break;
	default:
		command = "UNKNOWN";
		break;
	}

	SPDK_PTL_INFO("%s: command opcode: 0x%02x  human-readable: %s from cq_id: %d", prefix, cmd->opc,
		      command, cq_id);
	SPDK_PTL_INFO("%s: NSID: %u", prefix, cmd->nsid);
	SPDK_PTL_INFO("%s: CID: %u", prefix, cmd->cid);
	SPDK_PTL_INFO("%s:  FUSE: %u", prefix, cmd->fuse);
	SPDK_PTL_INFO("%s:  PSDT: %u", prefix, cmd->psdt);
	SPDK_PTL_INFO("%s:  CDW10-15: 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x", prefix,
		      cmd->cdw10, cmd->cdw11, cmd->cdw12, cmd->cdw13, cmd->cdw14, cmd->cdw15);

	// Print DPTR (CDW6-9) - Raw values (accessed via union)
	SPDK_PTL_INFO("%s:  DPTR (CDW6-7): 0x%016" PRIx64, prefix, cmd->dptr.sgl1.address);
	SPDK_PTL_INFO("%s:  DPTR (CDW8-9): 0x%08x 0x%08x", prefix,
		      ((uint32_t *)&cmd->dptr.sgl1.address)[2],
		      ((uint32_t *)&cmd->dptr.sgl1.address)[3]);
	// Decode and print SGL/DPTR information for all commands with PSDT != 0
	if (cmd->psdt != 0) {
		uint8_t sgl_type = cmd->dptr.sgl1.generic.type;
		uint8_t sgl_subtype = cmd->dptr.sgl1.generic.subtype;

		SPDK_PTL_INFO("%s:  SGL Type: 0x%02x, Subtype: 0x%02x", prefix, sgl_type, sgl_subtype);
		SPDK_PTL_INFO("%s:  SGL Address: 0x%016" PRIx64, prefix, cmd->dptr.sgl1.address);

		// Decode based on SGL type
		if (sgl_type == SPDK_NVME_SGL_TYPE_DATA_BLOCK) {
			// In-capsule data (unkeyed)
			SPDK_PTL_INFO("%s:  SGL Descriptor: DATA_BLOCK (In-Capsule)", prefix);
			SPDK_PTL_INFO("%s:  Length: %u bytes", prefix, cmd->dptr.sgl1.unkeyed.length);
			if (sgl_subtype == SPDK_NVME_SGL_SUBTYPE_OFFSET) {
				SPDK_PTL_INFO("%s:  Subtype: OFFSET (offset into capsule)", prefix);
			}
		} else if (sgl_type == SPDK_NVME_SGL_TYPE_KEYED_DATA_BLOCK) {
			// Remote RDMA buffer (keyed)
			SPDK_PTL_INFO("%s:  SGL Descriptor: KEYED_DATA_BLOCK (Remote RDMA)", prefix);
			SPDK_PTL_INFO("%s:  Length: %u bytes", prefix, cmd->dptr.sgl1.keyed.length);
			SPDK_PTL_INFO("%s:  RKey: 0x%08x", prefix, cmd->dptr.sgl1.keyed.key);
			if (sgl_subtype == SPDK_NVME_SGL_SUBTYPE_ADDRESS) {
				SPDK_PTL_INFO("%s:  Subtype: ADDRESS (remote memory address)", prefix);
			}
		} else if (sgl_type == SPDK_NVME_SGL_TYPE_BIT_BUCKET) {
			SPDK_PTL_INFO("%s:  SGL Descriptor: BIT_BUCKET (discard data)", prefix);
			SPDK_PTL_INFO("%s:  Length: %u bytes", prefix, cmd->dptr.sgl1.unkeyed.length);
		} else {
			SPDK_PTL_INFO("%s:  SGL Descriptor: UNKNOWN TYPE (0x%02x)", prefix, sgl_type);
		}
	} else if (cmd->psdt == 0) {
		// PRPs used
		SPDK_PTL_INFO("%s:  Data Transfer: PRPs (not SGL)", prefix);
		SPDK_PTL_INFO("%s:  PRP1: 0x%016" PRIx64, prefix, cmd->dptr.prp.prp1);
		SPDK_PTL_INFO("%s:  PRP2: 0x%016" PRIx64, prefix, cmd->dptr.prp.prp2);
	}

	return 1;
}

// Function to print NVMe completion in human-readable format
int ptl_print_nvme_cpl(const struct spdk_nvme_cpl *cpl, const char *prefix, int cq_id)
{
	if (!cpl) {
		SPDK_PTL_INFO("NVMe-cpl: NULL completion");
		return 1;
	}

	SPDK_PTL_INFO("%s: Completion Status: 0x%04x from cq_id: %d", prefix, cpl->status_raw, cq_id);

	// Interpret status code
	if (cpl->status.sct == SPDK_NVME_SCT_GENERIC) {
		SPDK_PTL_INFO("%s:  Status Code Type: Generic (0x%x)", prefix, cpl->status.sct);
		switch (cpl->status.sc) {
		case SPDK_NVME_SC_SUCCESS:
			SPDK_PTL_INFO("%s:  Status Code: Success (0x%x)", prefix, cpl->status.sc);
			break;
		case SPDK_NVME_SC_INVALID_OPCODE:
			SPDK_PTL_INFO("%s:  Status Code: Invalid Opcode (0x%x)", prefix, cpl->status.sc);
			break;
		case SPDK_NVME_SC_INVALID_FIELD:
			SPDK_PTL_INFO("%s:  Status Code: Invalid Field (0x%x)", prefix, cpl->status.sc);
			break;
		case SPDK_NVME_SC_INVALID_NAMESPACE_OR_FORMAT:
			SPDK_PTL_INFO("%s:  Status Code: Invalid Namespace or FORMAT (0x%x)", prefix, cpl->status.sc);
			break;

		default:
			SPDK_PTL_INFO("%s:  Status Code: Unknown (0x%x)", prefix, cpl->status.sc);
			break;
		}
	} else {
		SPDK_PTL_INFO("%s:  Status Code Type: 0x%x", prefix, cpl->status.sct);
		printf("  Status Code: 0x%x\n", cpl->status.sc);
	}

	SPDK_PTL_INFO("%s:  Phase: %u", prefix, cpl->status.p);
	SPDK_PTL_INFO("%s:  CID: %u", prefix, cpl->cid);
	SPDK_PTL_INFO("%s:  SQID: %u", prefix, cpl->sqid);
	SPDK_PTL_INFO("%s:  SQHD: %u", prefix, cpl->sqhd);
	SPDK_PTL_INFO("%s:  Result Data: 0x%08x", prefix, cpl->cdw0);
	return 1;
}

