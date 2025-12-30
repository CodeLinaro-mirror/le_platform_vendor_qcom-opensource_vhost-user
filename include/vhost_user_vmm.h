/* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef VHOST_USER_VMM_H
#define VHOST_USER_VMM_H

/**
 * VHOST_USER_QTI_INIT: The initial state of the vhost user qti
 * VHOST_USER_QTI_READY: vhost usr qti is already waiting for
 * vmm (e.g., crosvm) process to connect with it through the socket.
 * VHOST_USER_QTI_CONNECTED: vhost usr qti is successfully
 * connected with vmm.
 * VHOST_USER_QTI_RESETING_VHOST_DEV: vhost user qti is about
 * to send the VHOST_RESET_OWNER command to vhost hab.
 */
typedef enum vhost_user_q_state {
    VHOST_USER_QTI_INIT,
    VHOST_USER_QTI_READY,
    VHOST_USER_QTI_CONNECTED,
    VHOST_USER_QTI_RESETING_VHOST_DEV
} vhost_user_q_state;

int hab_register_to_vmm(const char *socket_path, void **vmm_handle,
    char *hab_vmm_client_name, int len, int *vmid);
void hab_unregister_from_vmm(int vmid, void *vmm_handle);
#endif /* VHOST_USER_VMM_H */
