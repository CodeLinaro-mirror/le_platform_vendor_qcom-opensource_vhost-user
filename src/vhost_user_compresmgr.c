/* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */
#include "vhost_user.h"
#include "vhost_user_compresmgr.h"
#include <sched.h>
#include <pthread.h>
#include "amss/compresmgr_client_api.h"

#if !defined(HAB_DESKTOP_USER) && !defined(HAVE_STRLCPY) && defined(USE_GLIB)
#include <glib.h>
#define strlcpy g_strlcpy
#endif

#define VHOST_USER_COMPRESMGR_DEFAULT_RT_POLICY     SCHED_FIFO
#define VHOST_USER_COMPRESMGR_DEFAULT_RT_PRIORITY   10

static void strip_dev_prefix(const char *dev_path, char *output, size_t out_size) {
    const char *prefix = "/dev/vhost";
    size_t prefix_len = strlen(prefix);

    if (strncmp(dev_path, prefix, prefix_len) == 0) {
        dev_path += prefix_len;
    }

    snprintf(output, out_size, "vhw%s", dev_path);
}

void set_vhost_worker_sched(const char *dev_path)
{
    CPUConfigReq_t cpu_config_req  = {0};
    CPUConfigResp_t cpu_config_resp = {0};
    const char *proc_name = "hab";
    CompResmgrRet_e cr_ret = COMPRESMGR_RET_EFAILURE;
    struct sched_param sch_param = {0};
    char thread_grp_name[256] = {0};
    int ret = 0;
    size_t n = 0;

    strip_dev_prefix(dev_path, thread_grp_name, 256);

    n = strlcpy(cpu_config_req.procName, proc_name, sizeof(cpu_config_req.procName));
    if (n >= sizeof(cpu_config_req.procName)) {
        pr_warn("procName truncated: src_len=%zu, dst_cap=%zu, src='%s'\n",
                n, sizeof(cpu_config_req.procName), proc_name);
    }

    n = strlcpy(cpu_config_req.thrdGrpName, thread_grp_name, sizeof(cpu_config_req.thrdGrpName));
    if (n >= sizeof(cpu_config_req.thrdGrpName)) {
        pr_warn("thrdGrpName truncated: src_len=%zu, dst_cap=%zu, src='%s'\n",
                n, sizeof(cpu_config_req.thrdGrpName), thread_grp_name);
    }

    pr_debug("proc_name %s, thread_grp_name %s\n",
            cpu_config_req.procName, cpu_config_req.thrdGrpName);

    cr_ret = CompResmgrGetCPUConfig(&cpu_config_req, &cpu_config_resp);
    if (cr_ret == COMPRESMGR_RET_SUCCESS) {
        if (cpu_config_resp.schedPolicy != VHOST_USER_COMPRESMGR_DEFAULT_RT_POLICY)
            pr_warn("sched policy is %d, expect %d\n",
                    cpu_config_resp.schedPolicy, VHOST_USER_COMPRESMGR_DEFAULT_RT_POLICY);

        sch_param.sched_priority = cpu_config_resp.priority;
        ret = pthread_setschedparam(pthread_self(), cpu_config_resp.schedPolicy, &sch_param);
        if (ret == 0) {
            pr_info("pthread_setschedparam set policy %d prio %d\n",
                    cpu_config_resp.schedPolicy, sch_param.sched_priority);
        } else {
            pr_err("pthread_setschedparam set failed policy %d prio %d\n",
                    cpu_config_resp.schedPolicy, sch_param.sched_priority);
        }
    } else {
        sch_param.sched_priority = VHOST_USER_COMPRESMGR_DEFAULT_RT_PRIORITY;
        pr_warn("CompResmgrGetCPUConfig failed, try to set default policy %d prio %d\n",
                VHOST_USER_COMPRESMGR_DEFAULT_RT_POLICY, sch_param.sched_priority);

        ret = pthread_setschedparam(pthread_self(), VHOST_USER_COMPRESMGR_DEFAULT_RT_POLICY, &sch_param);
        if (ret == 0) {
            pr_info("pthread_setschedparam set success: default policy %d prio %d\n",
                    VHOST_USER_COMPRESMGR_DEFAULT_RT_POLICY, sch_param.sched_priority);
        } else {
            pr_err("pthread_setschedparam set failed: default policy %d prio %d\n",
                    VHOST_USER_COMPRESMGR_DEFAULT_RT_POLICY, sch_param.sched_priority);
        }
    }
}

