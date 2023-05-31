engin启动流程大致见脑图所示。这里列举一些主要的结构体
## Engin srv
### DAOS server threading model

1. 当每个engin上有至少两个target 线程（target XS (xstream) set）时，其中一个线程为主线程`main XS`,其余的为辅助线程`offload XS`。
- 对于`main XS`,其主要任务为：

                处理IO请求的RPC服务
协程 ULT 服务 
`rebuild scanner/puller`、
`rebalance, aggregation`、
`data scrubbing`、
`pool service (tgt connect/disconnect`、
`container open/close`）

- 对于`offload XS (dss_tgt_offload_xs_nr)`,其主要任务为：

 		仅协程 ULT 服务
`IO request dispatch`
`Acceleration of EC/checksum/compress`

2. 每个engin上仅有一个target 线程时`main XS:`任务为

`drpc listener`
 			`RDB request and meta-data service`
			`management request for mgmt module`
 			`pool request`
			`container request (including the OID allocate)`
 			`rebuild request such as REBUILD_OBJECTS_SCAN/REBUILD_OBJECTS`
 			`rebuild status checker`
 			`rebalance request`
			`IV, bcast, and SWIM message handling`

dss_module
每个module（container、pool、server等）都提供了一个`dss_module`的结构体并在其中定义了模型接口
```c
struct dss_module {
	/* Name of the module */
	const char			*sm_name;
	/* Module id see enum daos_module_id */
	int				sm_mod_id;
	/* Module version */
	int				sm_ver;
	/* Module facility bitmask, can be feature bits like DSS_FAC_LOAD_CLI */
	uint64_t			sm_facs;
	/* key of local thread storage */
	struct dss_module_key		*sm_key;
	/* Initialization function, invoked just after successful load */
	int				(*sm_init)(void);
	/* Finalization function, invoked just before module unload */
	int				(*sm_fini)(void);
	/* Setup function, invoked after starting progressing */
	int				(*sm_setup)(void);
	/* Cleanup function, invoked before stopping progressing */
	int				(*sm_cleanup)(void);
	/* Whole list of RPC definition for request sent by nodes */
	struct crt_proto_format		*sm_proto_fmt;
	/* The count of RPCs which are dedicated for client nodes only */
	uint32_t			sm_cli_count;
	/* RPC handler of these RPC, last entry of the array must be empty */
	struct daos_rpc_handler		*sm_handlers;
	/* dRPC handlers, for unix socket comm, last entry must be empty */
	struct dss_drpc_handler		*sm_drpc_handlers;

	/* Different module operation */
	struct dss_module_ops		*sm_mod_ops;

	/* Per-pool metrics (optional) */
	struct dss_module_metrics	*sm_metrics;
};
```
```c
/* Loaded module instance */
struct loaded_mod {
	/* library handle grabbed with dlopen(3) */
	void			*lm_hdl;
	/* module interface looked up via dlsym(3) */
	struct dss_module	*lm_dss_mod;
	/* linked list of loaded module */
	d_list_t		 lm_lk;
	/** Module is initialized */
	bool			 lm_init;
};
```

```c
/* Define an array for faster accessing the module by mod_id */
static struct dss_module	*dss_modules[DAOS_MAX_MODULE];

enum daos_module_id {
	DAOS_VOS_MODULE		= 0, /** version object store */
	DAOS_MGMT_MODULE	= 1, /** storage management */
	DAOS_POOL_MODULE	= 2, /** pool service */
	DAOS_CONT_MODULE	= 3, /** container service */
	DAOS_OBJ_MODULE		= 4, /** object service */
	DAOS_REBUILD_MODULE	= 5, /** rebuild **/
	DAOS_RSVC_MODULE	= 6, /** replicated service server */
	DAOS_RDB_MODULE		= 7, /** rdb */
	DAOS_RDBT_MODULE	= 8, /** rdb test */
	DAOS_SEC_MODULE		= 9, /** security framework */
	DAOS_DTX_MODULE		= 10, /** DTX */

	DAOS_NR_MODULE		= 11, /** number of defined modules */
	DAOS_MAX_MODULE		= 64  /** Size of uint64_t see dmg profile */
};

struct dss_module vos_srv_module =  {
	.sm_name	= "vos_srv",
	.sm_mod_id	= DAOS_VOS_MODULE,
	.sm_ver		= 1,
	.sm_init	= vos_mod_init,
	.sm_fini	= vos_mod_fini,
	.sm_key		= &vos_module_key,
};
```

## Proto
### mgmt.proto
rpc的主要调用函数
```protobuf
// Management Service is replicated on a small number of servers in the system,
// these requests will be processed on a host that is a member of the management
// service.
//
// MgmtSvc RPCs will be forwarded over dRPC to be handled in data plane or
// forwarded over gRPC to be handled by the management service.
// MgmtSvc RPC将通过dRPC转发，以在数据平面中处理，或通过gRPC转发，由管理服务处理
service MgmtSvc {
	// Join the server described by JoinReq to the system.
	rpc Join(JoinReq) returns (JoinResp) {}
	// ClusterEvent notify MS of a RAS event in the cluster.
	rpc ClusterEvent(shared.ClusterEventReq) returns (shared.ClusterEventResp) {}
	// LeaderQuery provides a mechanism for clients to discover
	// the system's current Management Service leader
	rpc LeaderQuery(LeaderQueryReq) returns (LeaderQueryResp) {}
	// Create a DAOS pool allocated across a number of ranks
	rpc PoolCreate(PoolCreateReq) returns (PoolCreateResp) {}
	// Destroy a DAOS pool allocated across a number of ranks.
	rpc PoolDestroy(PoolDestroyReq) returns (PoolDestroyResp) {}
	// Evict a DAOS pool's connections.
	rpc PoolEvict(PoolEvictReq) returns (PoolEvictResp) {}
	// Exclude a pool target.
	rpc PoolExclude(PoolExcludeReq) returns (PoolExcludeResp) {}
	// Drain a pool target.
	rpc PoolDrain(PoolDrainReq) returns (PoolDrainResp) {}
	// Extend a pool.
	rpc PoolExtend(PoolExtendReq) returns (PoolExtendResp) {}
	// Reintegrate a pool target.
	rpc PoolReintegrate(PoolReintegrateReq) returns (PoolReintegrateResp) {}
	// PoolQuery queries a DAOS pool.
	rpc PoolQuery(PoolQueryReq) returns (PoolQueryResp) {}
	// Set a DAOS pool property.
	rpc PoolSetProp(PoolSetPropReq) returns (PoolSetPropResp) {}
	// Get a DAOS pool property list.
	rpc PoolGetProp(PoolGetPropReq) returns (PoolGetPropResp) {}
	// Fetch the Access Control List for a DAOS pool.
	rpc PoolGetACL(GetACLReq) returns (ACLResp) {}
	// Overwrite the Access Control List for a DAOS pool with a new one.
	rpc PoolOverwriteACL(ModifyACLReq) returns (ACLResp) {}
	// Update existing the Access Control List for a DAOS pool with new entries.
	rpc PoolUpdateACL(ModifyACLReq) returns (ACLResp) {}
	// Delete an entry from a DAOS pool's Access Control List.
	rpc PoolDeleteACL(DeleteACLReq) returns (ACLResp) {}
	// Get the information required by libdaos to attach to the system.
	rpc GetAttachInfo(GetAttachInfoReq) returns (GetAttachInfoResp) {}
	// List all pools in a DAOS system: basic info: UUIDs, service ranks.
	rpc ListPools(ListPoolsReq) returns (ListPoolsResp) {}
	// List all containers in a pool
	rpc ListContainers(ListContReq) returns (ListContResp) {}
	// Change the owner of a DAOS container
	rpc ContSetOwner(ContSetOwnerReq) returns (ContSetOwnerResp) {}
	// Query DAOS system status
	rpc SystemQuery(SystemQueryReq) returns(SystemQueryResp) {}
        // Query Daos system control node status
        rpc SystemControlQuery(SystemControlQueryReq) returns(SystemControlQueryResp) {}
	// Stop DAOS system (shutdown data-plane instances)
	rpc SystemStop(SystemStopReq) returns(SystemStopResp) {}
	// Start DAOS system (restart data-plane instances)
	rpc SystemStart(SystemStartReq) returns(SystemStartResp) {}
	// Erase DAOS system database prior to reformat
	rpc SystemErase(SystemEraseReq) returns(SystemEraseResp) {}
	rpc PoolHealth(PoolHealthReq) returns (PoolHealthResp) {}
	rpc PoolDump(PoolDumpReq) returns (PoolDumpResp) {}
	rpc SystemTgtObjNumQuery(SysTgtObjNumQueryReq) returns(SysTgtObjNumQueryResp) {}
	// generic cmd to engine
	rpc EngineCmd(EngineCmdReq) returns(EngineCmdResp) {}
	rpc DySetConfig(SetConfigReq) returns(ConfigResp) {}
	rpc DyGetConfig(GetConfigReq) returns(ConfigResp) {}
	rpc FaultMapDel(FaultMapDelReq) returns(FaultMapDelResp) {}
	// Create a protect domain allocated across a number of ranks
	rpc ProtectDMCreate(ProtectDMCreateReq) returns (ProtectDMCreateResp) {}
	rpc ProtectDMAddMember(ProtectDMAddMemberReq) returns (ProtectDMAddMemberResp) {}
	rpc ProtectDMDump(ProtectDMDumpReq) returns (ProtectDMDumpResp) {}
	rpc GetPoolServiceRanksbyrank(GetPoolSvcbyRankReq) returns (GetPoolSvcbyRankResp) {}
	// Update the version number after upgrade
	rpc UpdateCeastorVersion(UpdateCeastorVersionReq) returns (UpdateCeastorVersionResp) {}
	// Query the version number
	rpc CeastorVersionQuery(CeastorVersionQueryReq) returns (CeastorVersionQueryResp) {}
	rpc ProtectDMDelete(ProtectDMDeleteReq) returns (ProtectDMDeleteResp) {}
	rpc SystemScale(SystemScaleReq) returns (SystemScaleResp) {}
	rpc ScaleQuery(ScaleQueryReq) returns (ScaleQueryResp) {}
	// PoolIoStatsQuery queries a DAOS pool iostats.
	rpc PoolIoStatsQuery(PoolIoQueryReq) returns (PoolIoQueryResp) {}
	rpc StorageIoStatsQuery(StorageIoQueryReq) returns (StorageIoQueryResp) {}
	// profile start
	rpc ProfileStart(ProfileStartReq) returns(ProfileStartResp) {}
	// profile stop
	rpc ProfileStop(ProfileStopReq) returns(ProfileStopResp) {}
	// profile clear
	rpc ProfileClear(ProfileClearReq) returns(ProfileClearResp) {}
	// profile dump
	rpc ProfileDump(ProfileDumpReq) returns(ProfileDumpResp) {}
	// Set the Pool Ceastor version
	rpc PoolCeaVerSet(PoolCeaVerSetReq) returns (PoolCeaVerSetResp) {}
}

```
```c
//用于标识module在哪个xstream初始化
enum dss_module_tag {
	DAOS_SYS_TAG	= 1 << 0, /** only run on system xstream */
	DAOS_TGT_TAG	= 1 << 1, /** only run on target xstream */
	DAOS_OFF_TAG	= 1 << 2, /** only run on offload/helper xstream */
	DAOS_SERVER_TAG	= 0xff,	  /** run on all xstream */
};

struct dss_module_key {
	/* Indicate where the keys should be instantiated */
	enum dss_module_tag dmk_tags;

	/* The position inside the dss_module_keys */
	int dmk_index;
	...
};

struct dss_module {
	/* Name of the module */
	const char			*sm_name;
	/* Module id see enum daos_module_id */
	int				sm_mod_id;
	/* key of local thread storage */
	struct dss_module_key		*sm_key;

	...
};
```
### module load
```c
loaded_mod_list //用于存储module的列表
```
## RPC handler && dRPC Handler
### 两者结构体对比
```c
struct daos_rpc_handler {
	/* Operation code */
	crt_opcode_t		 dr_opc;
	/* Request handler, only relevant on the server side */
	crt_rpc_cb_t		 dr_hdlr;
	/* CORPC operations (co_aggregate == NULL for point-to-point RPCs) */
	struct crt_corpc_ops	*dr_corpc_ops;
};


/* Any dss_module that accepts dRPC communications over the Unix Domain Socket
 * must provide one or more dRPC handler functions. The handler is used by the
 * I/O Engine to multiplex incoming dRPC messages for processing
*/
struct dss_drpc_handler {
	int		module_id;	/** dRPC messaging module ID */
	drpc_handler_t	handler;	/** dRPC handler for the module */
};

typedef void (*drpc_handler_t)(Drpc__Call *, Drpc__Response *);
```
```c
enum drpc_module {
	DRPC_MODULE_TEST		= 0,	/* Reserved for testing */
	DRPC_MODULE_SEC_AGENT		= 1,	/* daos_agent security */
	DRPC_MODULE_MGMT		= 2,	/* daos_server mgmt */
	DRPC_MODULE_SRV			= 3,	/* daos_server */
	DRPC_MODULE_SEC			= 4,	/* daos_server security */

	NUM_DRPC_MODULES			/* Must be last */
};
```
### dRPC
在unix域套接字上使用packet socket一次接收整个消息，而不知道其大小。因此，出于这个原因，我们需要限制最大消息大小，这样我们就可以预先分配一个缓冲区来放置所有信息。该值也在相应的控制平面文件drpc_server.go中定义。如果在此处更改，也必须在该文件中更改.
```c
#define UNIXCOMM_MAXMSGSIZE (1 << 17)
```
```c
struct unixcomm {
	int fd; /** unix域套接字的文件描述符 */
	int flags; /** unix域套接字上设置的标志 */
};
```
```c
//drpc_handler函数指针
typedef void (*drpc_handler_t)(Drpc__Call *, Drpc__Response *);

struct drpc {
	struct unixcomm	*comm; 	/**  unix域套接字通信上下文 */
	int		sequence; 		/**  最新发送消息的序列号*/
	uint32_t	ref_count; 	/** open refs to this ctx */

	 //侦听drpc上下文接收的消息的处理程序
	drpc_handler_t	handler;
};
```
```c
//unix活动类型
enum unixcomm_activity {
	UNIXCOMM_ACTIVITY_NONE,
	UNIXCOMM_ACTIVITY_DATA_IN,
	UNIXCOMM_ACTIVITY_PEER_DISCONNECTED,
	UNIXCOMM_ACTIVITY_ERROR
};
```
```c
struct unixcomm_poll {
	struct unixcomm		*comm;
	enum unixcomm_activity	activity;
};
```
### 各个modul的RPC、dRPC
#### mgmt

- mgmt  dRPC
```c
static struct dss_drpc_handler mgmt_drpc_handlers[] = {
	{
		.module_id = DRPC_MODULE_MGMT,
		.handler = process_drpc_request
	},
	{
		.module_id = 0,
		.handler = NULL
	}
};
```
```c
//所被调用的函数实现在src\mgmt\srv_drpc.c
static void
process_drpc_request(Drpc__Call *drpc_req, Drpc__Response *drpc_resp)
{
	/**
	 * Process drpc request and populate daos response,
	 * command errors should be indicated inside daos response.
	 */
	D_DEBUG(DB_MGMT, "recv drpc: %d\n", drpc_req->method);
	switch (drpc_req->method) {
	case DRPC_METHOD_MGMT_PREP_SHUTDOWN:
		ds_mgmt_drpc_prep_shutdown(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_PING_RANK:
		ds_mgmt_drpc_ping_rank(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_SET_LOG_MASKS:
		ds_mgmt_drpc_set_log_masks(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_SET_RANK:
		ds_mgmt_drpc_set_rank(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_POOL_CREATE:
		ds_mgmt_drpc_pool_create(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_POOL_DESTROY:
		ds_mgmt_drpc_pool_destroy(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_POOL_EVICT:
		ds_mgmt_drpc_pool_evict(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_SET_UP:
		ds_mgmt_drpc_set_up(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_EXCLUDE:
		ds_mgmt_drpc_pool_exclude(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_DRAIN:
		ds_mgmt_drpc_pool_drain(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_REINTEGRATE:
		ds_mgmt_drpc_pool_reintegrate(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_EXTEND:
		ds_mgmt_drpc_pool_extend(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_BIO_HEALTH_QUERY:
		ds_mgmt_drpc_bio_health_query(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_SMD_LIST_DEVS:
		ds_mgmt_drpc_smd_list_devs(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_SMD_LIST_POOLS:
		ds_mgmt_drpc_smd_list_pools(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_DEV_STATE_QUERY:
		ds_mgmt_drpc_dev_state_query(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_DEV_SET_FAULTY:
		ds_mgmt_drpc_dev_set_faulty(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_DEV_REPLACE:
		ds_mgmt_drpc_dev_replace(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_DEV_IDENTIFY:
		ds_mgmt_drpc_dev_identify(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_POOL_GET_ACL:
		ds_mgmt_drpc_pool_get_acl(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_POOL_OVERWRITE_ACL:
		ds_mgmt_drpc_pool_overwrite_acl(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_POOL_UPDATE_ACL:
		ds_mgmt_drpc_pool_update_acl(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_POOL_DELETE_ACL:
		ds_mgmt_drpc_pool_delete_acl(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_LIST_CONTAINERS:
		ds_mgmt_drpc_pool_list_cont(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_POOL_SET_PROP:
		ds_mgmt_drpc_pool_set_prop(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_POOL_GET_PROP:
		ds_mgmt_drpc_pool_get_prop(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_POOL_QUERY:
		ds_mgmt_drpc_pool_query(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_CONT_SET_OWNER:
		ds_mgmt_drpc_cont_set_owner(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_GROUP_UPDATE:
		ds_mgmt_drpc_group_update(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_POOL_HEALTH:
		ds_mgmt_drpc_pool_health(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_POOL_DUMP:
		ds_mgmt_drpc_pool_dump(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGT_TGT_OBJNUM_GET:
		ds_mgmt_drpc_tgt_objnum_get(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_ENGINE_CMD:
		ds_mgmt_drpc_engine_cmd(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_DYCONFIG_SET:
		ds_mgmt_drpc_dyconfig_set(drpc_req, drpc_resp);
		break;
	case DPRC_METHOD_MGMT_DYCONFIG_GET:
		ds_mgmt_drpc_dyconfig_get(drpc_req, drpc_resp);
		break;
  case DRPC_METHOD_MGMT_FAULTMAP_DEL:
    ds_mgmt_drpc_pool_faultmap_del(drpc_req, drpc_resp);
    break;
  case DRPC_METHOD_MGMT_SET_MAINTENANCE_FLAG:
	  ds_mgmt_drpc_set_maintenance_flag(drpc_req, drpc_resp);
	  break;
	case DRPC_METHOD_MGMT_POOL_QUERY_IO_STATS:
		ds_mgmt_drpc_pool_iostats_query(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_STORAGE_QUERY_IO_STATS:
		ds_mgmt_drpc_storage_iostats_query(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_PROFILE_START:
		ds_mgmt_drpc_profile_start(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_PROFILE_STOP:
		ds_mgmt_drpc_profile_stop(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_PROFILE_CLEAR:
		ds_mgmt_drpc_profile_clear(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_PROFILE_DUMP:
		ds_mgmt_drpc_profile_dump(drpc_req, drpc_resp);
	case DRPC_METHOD_MGMT_POOL_CEA_VER_SET:
		ds_mgmt_drpc_pool_cea_ver_set(drpc_req, drpc_resp);
		break;
	default:
		drpc_resp->status = DRPC__STATUS__UNKNOWN_METHOD;
		D_ERROR("Unknown method\n");
	}
}
```

- mgmt  RPC
```c
static struct daos_rpc_handler mgmt_handlers[] = {
	MGMT_PROTO_CLI_RPC_LIST,
	MGMT_PROTO_SRV_RPC_LIST,
};
```
```c
#define MGMT_PROTO_CLI_RPC_LIST						\
	X(MGMT_SVC_RIP,							\
		DAOS_RPC_NO_REPLY, &CQF_mgmt_svc_rip,			\
		ds_mgmt_hdlr_svc_rip, NULL),				\
	X(MGMT_PARAMS_SET,						\
		0, &CQF_mgmt_params_set,				\
		ds_mgmt_params_set_hdlr, NULL),				\
	X(MGMT_PROFILE,							\
		0, &CQF_mgmt_profile,					\
		ds_mgmt_profile_hdlr, NULL),				\
	X(MGMT_POOL_GET_SVCRANKS,					\
		0, &CQF_mgmt_pool_get_svcranks,				\
		ds_mgmt_pool_get_svcranks_hdlr, NULL),			\
	X(MGMT_POOL_FIND,						\
		0, &CQF_mgmt_pool_find,					\
		ds_mgmt_pool_find_hdlr, NULL),				\
	X(MGMT_MARK,							\
		0, &CQF_mgmt_mark,					\
		ds_mgmt_mark_hdlr, NULL),				\
	X(MGMT_GET_BS_STATE,						\
		0, &CQF_mgmt_get_bs_state,				\
		ds_mgmt_hdlr_get_bs_state, NULL)
```
```c
#define MGMT_PROTO_SRV_RPC_LIST						\
	X(MGMT_TGT_CREATE,						\
		0, &CQF_mgmt_tgt_create,				\
		ds_mgmt_hdlr_tgt_create,				\
		&ds_mgmt_hdlr_tgt_create_co_ops),			\
	X(MGMT_TGT_DESTROY,						\
		0, &CQF_mgmt_tgt_destroy,				\
		ds_mgmt_hdlr_tgt_destroy, NULL),			\
	X(MGMT_TGT_PARAMS_SET,						\
		0, &CQF_mgmt_tgt_params_set,				\
		ds_mgmt_tgt_params_set_hdlr, NULL),			\
	X(MGMT_TGT_PROFILE,						\
		0, &CQF_mgmt_profile,					\
		ds_mgmt_tgt_profile_hdlr,				\
		&ds_mgmt_tgt_profile_hdlr_co_ops),			\
	X(MGMT_TGT_MAP_UPDATE,						\
		0, &CQF_mgmt_tgt_map_update,				\
		ds_mgmt_hdlr_tgt_map_update,				\
		&ds_mgmt_hdlr_tgt_map_update_co_ops),			\
	X(MGMT_TGT_MARK,						\
		0, &CQF_mgmt_mark,					\
		ds_mgmt_tgt_mark_hdlr, NULL),		\
	X(MGMT_TGT_QUERY,                              \
		0, &CQF_mgmt_tgt_query,                    \
		ds_mgmt_hdlr_tgt_query,                    \
		&ds_mgmt_hdlr_tgt_query_co_ops)
```
#### vos 
其rpc和drpc调用函数与前面的类似mgmt类似
#### con
## dss_srv_init 
### xtream
```c
/** Number of dRPC xstreams */
#define DRPC_XS_NR	(1)
/** Number of offload XS */
unsigned int	dss_tgt_offload_xs_nr;
/** Number of target (XS set) per engine */
unsigned int	dss_tgt_nr;
/** Number of system XS */
unsigned int	dss_sys_xs_nr = DAOS_TGT0_OFFSET + DRPC_XS_NR;
```
```c

struct dss_xstream {
	char			dx_name[DSS_XS_NAME_LEN];
	ABT_future		dx_shutdown;
	ABT_future		dx_stopping;
	hwloc_cpuset_t		dx_cpuset;
	ABT_xstream		dx_xstream;
	ABT_pool		dx_pools[DSS_POOL_CNT];
	ABT_sched		dx_sched;
	ABT_thread		dx_progress;
	struct sched_info	dx_sched_info;
	tse_sched_t		dx_sched_dsc;
	struct dss_rpc_cntr	dx_rpc_cntrs[DSS_RC_MAX];
	/* xstream id, [0, DSS_XS_NR_TOTAL - 1] */
	int			dx_xs_id;
	/* VOS target id, [0, dss_tgt_nr - 1]. Invalid (-1) for system XS.
	 * For offload XS it is same value as its main XS.
	 */
	int			dx_tgt_id;
	/* CART context id, invalid (-1) for the offload XS w/o CART context */
	int			dx_ctx_id;
	int			dx_back_ctx_id;
	/* Cart progress timeout in micro-seconds */
	unsigned int		dx_timeout;
	bool			dx_main_xs;	/* true for main XS */
	bool			dx_comm;	/* true with cart context */
	bool			dx_dsc_started;	/* DSC progress ULT started */
	bool			dx_progress_started;	/* Network poll started */
	uuid_t			dx_pool_id;	/* tgt belong to this pool*/
	bool			dx_back_sep;
};

```
```c
struct dss_xstream_data {
	/** Initializing step, it is for cleanup of global states */
	int			  xd_init_step;
	int			  xd_ult_init_rc;
	bool			  xd_ult_signal;
	/** total number of XS including system XS, main XS and offload XS */
	int			  xd_xs_nr;
	/** created XS pointer array */
	struct dss_xstream	**xd_xs_ptrs;
	/** serialize initialization of ULTs */
	ABT_cond		  xd_ult_init;
	/** barrier for all ULTs to enter handling loop */
	ABT_cond		  xd_ult_barrier;
	ABT_mutex		  xd_mutex;
	struct dss_thread_local_storage *xd_dtc;
};
```
