/*
 * Copyright (c) 2018      Mellanox Technologies.  All rights reserved.
 *                         All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#ifndef _COMMON_UCX_H_
#define _COMMON_UCX_H_

#include "opal_config.h"

#include <stdint.h>

#include <ucp/api/ucp.h>

#include "opal/mca/mca.h"
#include "opal/util/output.h"
#include "opal/runtime/opal_progress.h"
#include "opal/include/opal/constants.h"
#include "opal/class/opal_list.h"

BEGIN_C_DECLS

#define MCA_COMMON_UCX_ENABLE_DEBUG   OPAL_ENABLE_DEBUG
#if MCA_COMMON_UCX_ENABLE_DEBUG
#  define MCA_COMMON_UCX_MAX_VERBOSE  100
#  define MCA_COMMON_UCX_ASSERT(_x)   assert(_x)
#else
#  define MCA_COMMON_UCX_MAX_VERBOSE  2
#  define MCA_COMMON_UCX_ASSERT(_x)
#endif

#define _MCA_COMMON_UCX_QUOTE(_x) \
    # _x
#define MCA_COMMON_UCX_QUOTE(_x) \
    _MCA_COMMON_UCX_QUOTE(_x)

#define MCA_COMMON_UCX_ERROR(...)                                   \
    opal_output_verbose(0, opal_common_ucx.output,                  \
                        __FILE__ ":" MCA_COMMON_UCX_QUOTE(__LINE__) \
                        " Error: " __VA_ARGS__)

#define MCA_COMMON_UCX_VERBOSE(_level, ... )                                \
    if (((_level) <= MCA_COMMON_UCX_MAX_VERBOSE) &&                         \
        ((_level) <= opal_common_ucx.verbose)) {                            \
        opal_output_verbose(_level, opal_common_ucx.output,                 \
                            __FILE__ ":" MCA_COMMON_UCX_QUOTE(__LINE__) " " \
                            __VA_ARGS__);                                   \
    }

typedef struct opal_common_ucx_module {
    int  output;
    int  verbose;
    int  progress_iterations;
    int  registered;
    bool opal_mem_hooks;
} opal_common_ucx_module_t;

typedef struct opal_common_ucx_del_proc {
    ucp_ep_h ep;
    size_t   vpid;
} opal_common_ucx_del_proc_t;

extern opal_common_ucx_module_t opal_common_ucx;

OPAL_DECLSPEC void opal_common_ucx_mca_register(void);
OPAL_DECLSPEC void opal_common_ucx_mca_deregister(void);
OPAL_DECLSPEC void opal_common_ucx_empty_complete_cb(void *request, ucs_status_t status);
OPAL_DECLSPEC void opal_common_ucx_mca_pmix_fence(ucp_worker_h worker);
OPAL_DECLSPEC int opal_common_ucx_del_procs(opal_common_ucx_del_proc_t *procs, size_t count,
                                            size_t my_rank, size_t max_disconnect, ucp_worker_h worker);

static inline
int opal_common_ucx_wait_request(ucs_status_ptr_t request, ucp_worker_h worker,
                                 const char *msg)
{
    ucs_status_t status;
    int i;
#if !HAVE_DECL_UCP_REQUEST_CHECK_STATUS
    ucp_tag_recv_info_t info;
#endif

    /* check for request completed or failed */
    if (OPAL_LIKELY(UCS_OK == request)) {
        return OPAL_SUCCESS;
    } else if (OPAL_UNLIKELY(UCS_PTR_IS_ERR(request))) {
        MCA_COMMON_UCX_VERBOSE(1, "%s failed: %d, %s", msg ? msg : __FUNCTION__,
                               UCS_PTR_STATUS(request),
                               ucs_status_string(UCS_PTR_STATUS(request)));
        return OPAL_ERROR;
    }

    while (1) {
        /* call UCX progress */
        for (i = 0; i < opal_common_ucx.progress_iterations; i++) {
            if (UCS_INPROGRESS != (status =
#if HAVE_DECL_UCP_REQUEST_CHECK_STATUS
                ucp_request_check_status(request)
#else
                ucp_request_test(request, &info)
#endif
                )) {
                ucp_request_free(request);
                if (OPAL_LIKELY(UCS_OK == status)) {
                    return OPAL_SUCCESS;
                } else {
                    MCA_COMMON_UCX_VERBOSE(1, "%s failed: %d, %s", msg ? msg : __FUNCTION__,
                                           UCS_PTR_STATUS(request),
                                           ucs_status_string(UCS_PTR_STATUS(request)));
                    return OPAL_ERROR;
                }
            }
            ucp_worker_progress(worker);
        }
        /* call OPAL progress on every opal_common_ucx_progress_iterations
         * calls to UCX progress */
        opal_progress();
    }
}

static inline
int opal_common_ucx_ep_flush(ucp_ep_h ep, ucp_worker_h worker)
{
#if HAVE_DECL_UCP_EP_FLUSH_NB
    ucs_status_ptr_t request;

    request = ucp_ep_flush_nb(ep, 0, opal_common_ucx_empty_complete_cb);
    return opal_common_ucx_wait_request(request, worker, "ucp_ep_flush_nb");
#else
    ucs_status_t status;

    status = ucp_ep_flush(ep);
    return (status == UCS_OK) ? OPAL_SUCCESS : OPAL_ERROR;
#endif
}

static inline
int opal_common_ucx_worker_flush(ucp_worker_h worker)
{
#if HAVE_DECL_UCP_WORKER_FLUSH_NB
    ucs_status_ptr_t request;

    request = ucp_worker_flush_nb(worker, 0, opal_common_ucx_empty_complete_cb);
    return opal_common_ucx_wait_request(request, worker, "ucp_worker_flush_nb");
#else
    ucs_status_t status;

    status = ucp_worker_flush(worker);
    return (status == UCS_OK) ? OPAL_SUCCESS : OPAL_ERROR;
#endif
}

static inline
int opal_common_ucx_atomic_fetch(ucp_ep_h ep, ucp_atomic_fetch_op_t opcode,
                                 uint64_t value, void *result, size_t op_size,
                                 uint64_t remote_addr, ucp_rkey_h rkey,
                                 ucp_worker_h worker)
{
    ucs_status_ptr_t request;

    request = ucp_atomic_fetch_nb(ep, opcode, value, result, op_size,
                                  remote_addr, rkey, opal_common_ucx_empty_complete_cb);
    return opal_common_ucx_wait_request(request, worker, "ucp_atomic_fetch_nb");
}

static inline
int opal_common_ucx_atomic_cswap(ucp_ep_h ep, uint64_t compare,
                                 uint64_t value, void *result, size_t op_size,
                                 uint64_t remote_addr, ucp_rkey_h rkey,
                                 ucp_worker_h worker)
{
    uint64_t tmp = value;
    int ret;

    ret = opal_common_ucx_atomic_fetch(ep, UCP_ATOMIC_FETCH_OP_CSWAP, compare, &tmp,
                                       op_size, remote_addr, rkey, worker);
    if (OPAL_LIKELY(OPAL_SUCCESS == ret)) {
        /* in case if op_size is constant (like sizeof(type)) then this condition
         * is evaluated in compile time */
        if (op_size == sizeof(uint64_t)) {
            *(uint64_t*)result = tmp;
        } else {
            assert(op_size == sizeof(uint32_t));
            *(uint32_t*)result = tmp;
        }
    }
    return ret;
}

END_C_DECLS

#endif
