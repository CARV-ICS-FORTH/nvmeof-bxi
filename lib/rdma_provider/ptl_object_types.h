#ifndef PTL_OBJECT_TYPES
#define PTL_OBJECT_TYPES
typedef enum ptl_obj_type {
	PTL_RECV_OP = 100,
	PTL_SEND_OP,
	PTL_CONTEXT,
	PTL_PD,
	PTL_QP,
	PTL_STATIC_CQ,
	PTL_CQ,
	PTL_CM_ID,
	PTL_SRQ,
} ptl_obj_type_e;
#endif
