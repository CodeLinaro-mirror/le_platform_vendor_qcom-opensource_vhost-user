/* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <stdarg.h>
#include <errno.h>
#include <string.h>
#include <vmm_clib.h>
#include "vhost_user.h"
#include "vhost_user_vmm.h"
#if defined(HAB_DESKTOP_USER)
#include <bsd/string.h>
#endif

#if !defined(HAB_DESKTOP_USER) && !defined(HAVE_STRLCAT) && defined(USE_GLIB)
#include <glib.h>
#define strlcat g_strlcat
#endif

#if !defined(HAB_DESKTOP_USER) && !defined(HAVE_STRLCPY) && defined(USE_GLIB)
#include <glib.h>
#define strlcpy g_strlcpy
#endif

/*
 * priority level of clients.
 * clients would be notified according to priority order, LEVEL_0 being notified first.
 * hab clients priority is same as other platform.
 * LEVEL_0 > LEVEL_1 > LEVEL_2 > LEVEL_3 > LEVEL_4
 */
#define HAB_VMM_PRIO LEVEL_1

extern vhost_user_q_state vhost_user_qti_state;

/*
 * Because VMIDs are finite, we use an array to make a simple dictionary.
 * The index of the array is the HAB OS ID, and the value of the array
 * is the VMID defined by gunyah hypervisor used to register as an VMM
 * service client.
 * When the value of the array HAB_OSID_TO_GH_VMID is -1, it means that
 * the OS ID does not have a corresponding VMID.
 * Assumption: the valid GVM VMIDs should > 0.
 */
static int HAB_OSID_TO_GH_VMID[7] = {-1, -1, 52, 53, -1, -1, -1};

typedef struct VMIDtoHABOSID {
    int vmid;
    int hab_osid;
} VMIDtoHABOSID;

static VMIDtoHABOSID gh_vmid_to_osid[] = {
    {52, 2},
    {53, 3},
};

static int hab_socket_path_to_vmid(const char *socket_path)
{
    int osid = 0;
    int ret;
    const char *pos;
    size_t osid_max = sizeof(HAB_OSID_TO_GH_VMID)/sizeof(int) - 1;

    pos = strstr(socket_path, "vm");
    if (!pos) {
        pr_err("osid info is not found in socket_path\n");
        return -1;
    }

    /* skip "vm" */
    pos += 2;
    while (*pos && isdigit(*pos)) {
        osid = osid * 10 + (*pos - '0');
        pos++;
    }

    if (osid > osid_max) {
        pr_err("vmid is too big %d, expect max %d\n", osid, osid_max);
        return -1;
    }

    return HAB_OSID_TO_GH_VMID[osid];
}

static int vmid_to_hab_osid(int vmid)
{
    int arr_len = sizeof(gh_vmid_to_osid) / sizeof (VMIDtoHABOSID);
    int i;

    for (i = 0; i < arr_len; i++)
        if (vmid == gh_vmid_to_osid[i].vmid)
            return gh_vmid_to_osid[i].hab_osid;

    pr_err("vmid %d cannot find osid\n", vmid);
    return -1;
}

static int hab_generate_vmm_client_name(int vmid, const char *socket_path,
            char *hab_vmm_client_name, int len)
{
    const char *end;
    size_t copy_len;
    const char *pos;
    char tmp[256] = {0};
    int ret;

    pos = strstr(socket_path, "vm");
    if (!pos) {
        pr_err("pchan group name is not found in dev_path\n");
        return -EINVAL;
    }

    snprintf(hab_vmm_client_name, len, "gunyah-vm%d-hab-", vmid);

    copy_len = strlen(pos + 4) - strlen("-skt");
    strlcpy(tmp, pos + 4, 256);
    tmp[copy_len] = '\0';

    strlcat(hab_vmm_client_name, tmp, len);

    return 0;
}

/**
 * We are waiting here while the main thread performs the actual work
 * (e.g., resetting the owner and re-creating the socket).
 */
static void is_hab_ready_for_gvm_relaunch(vhost_user_q_state *tmp_vhost_user_qti_state,
                const char *hab_vmm_client_name)
{
    volatile vhost_user_q_state *vhost_user_qti_state = tmp_vhost_user_qti_state;

    while (*vhost_user_qti_state != VHOST_USER_QTI_READY) {
        pr_err("wait for %s socket ready\n", hab_vmm_client_name);
        sleep(2);
    }
}

/* callback function for vmm */
static int hab_vmm_callback(uint32_t vmid, vmm_event_t event, void *priv_data)
{
    const char *hab_vmm_client_name = (const char *)priv_data;
    int osid = vmid_to_hab_osid(vmid);

    if (osid < 0) {
        pr_err("unvalid vmid %d\n", vmid);
        return -EINVAL;
    }

    /**
     * 1. Regardless of the reason for the reboot (graceful/abnormal), the clean work
     *    is same for hab.
     * 2. Assumption: GVM_SHUTDOWN_LEVEL_2 will be sent after
     *    GVM_WDOG_BITE, GVM_CONTAINER_CRASH, or GVM_HANDLED_CONTAINER_CRASH is sent
     *    for corresponding reboot case.
     *    vmm_service will guarantee it.
     * 3. We use GVM_SHUTDOWN_LEVEL_2 so that we can delay the hab readiness check, and
     *    increase the success rate of the first hab readiness check.
     */
    switch(event){
        case GVM_SHUTDOWN:
        case GVM_STOPPED:
        case GVM_BAD_STATE:
            pr_debug("%s: GVM_SHUTDOWN_LEVEL_1 event %d\n", hab_vmm_client_name, event);
            is_hab_ready_for_gvm_relaunch(&vhost_user_qti_state, hab_vmm_client_name);
            break;
        case GVM_SHUTDOWN_LEVEL_2:
            pr_debug("%s: GVM_SHUTDOWN_LEVEL_2\n", hab_vmm_client_name);
            is_hab_ready_for_gvm_relaunch(&vhost_user_qti_state, hab_vmm_client_name);
            break;
        default:
            pr_err("%s: Client default event %d\n", hab_vmm_client_name, event);
            break;
    }

    return 0;
}

int hab_register_to_vmm(const char *socket_path, void **vmm_handle,
    char *hab_vmm_client_name, int len, int *vmid)
{
    int ret = 0;
    uint32_t mask = 0;
    vmm_subscribe_attr_t attr;
    int tmp_vmid;

    tmp_vmid = hab_socket_path_to_vmid(socket_path);
    if (tmp_vmid < 0) {
        pr_err("invalid vmid %d, skip registering as a vmm service client\n", tmp_vmid);
        return -EINVAL;
    }

    ret = hab_generate_vmm_client_name(tmp_vmid, socket_path, hab_vmm_client_name, len);
    if (ret) {
        pr_err("hab_vmm_client_name is null, skip registering as a vmm service client\n");
        return -EINVAL;
    }

    ret = vmm_client_connect(hab_vmm_client_name, VMM_SERVICE_SERVER, vmm_handle);
    if (ret < 0) {
        pr_err("Failed to connect to vmm service, Error: %d \n", ret);
        return ret;
    }

    memset(&attr, 0, sizeof(vmm_subscribe_attr_t));
    attr.event_cb_func = hab_vmm_callback;
    attr.event_mask = GVM_SHUTDOWN_LEVEL_1 | GVM_SHUTDOWN_LEVEL_2;
    attr.level = HAB_VMM_PRIO;
    attr.priv_data = (void *)hab_vmm_client_name;

    ret = vmm_subscribe_event_notification(*vmm_handle, 1, &tmp_vmid, &attr);
    if (ret < 0) {
        pr_err("vmm register failed %d\n", ret);
        return ret;
    } else
        pr_debug("vmm register success\n");

    *vmid = tmp_vmid;
    return 0;
}

void hab_unregister_from_vmm(int vmid, void *vmm_handle)
{
    int ret = 0;

    ret = vmm_unsubscribe_event_notification(vmm_handle, 1, &vmid);
    if (ret < 0)
        pr_err("vmm unregister failed %d\n", ret);
    else
        pr_debug("vmm unregister success\n");

    ret = vmm_client_disconnect(vmm_handle);
    if (ret < 0)
        pr_err("vmm disconnect failed %d\n", ret);
    else
        pr_debug("vmm disconnect success\n");
}
