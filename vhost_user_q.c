/*
 * Copyright (c) 2021 The Linux Foundation. All rights reserved.
 * Copyright IBM, Corp. 2007
 * Copyright (c) 2016 Red Hat, Inc.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * Authors:
 * Anthony Liguori <aliguori@us.ibm.com>
 * Marc-André Lureau <mlureau@redhat.com>
 * Victor Kaplansky <victork@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the NOTICE file in the top-level directory.
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <stdarg.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/eventfd.h>
#include <sys/mman.h>

#if defined(__linux__)
#include <sys/syscall.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/vhost.h>
#include <linux/virtio_gpu.h>
#endif

#include "vhost_user_q.h"

#define VHOST_USER_HDR_SIZE offsetof(VhostUserMsg, payload.u64)

/* The version of the protocol we support */
#define VHOST_USER_VERSION 1
typedef struct VhostUserQtiMemRegion {
    /* Guest Physical address. */
    uint64_t gpa;
    /* Memory region size. */
    uint64_t size;
    /* Hypervisor virtual address (userspace). */
    uint64_t hva;
    /* Starting offset in our mmaped space. */
    uint64_t mmap_offset;
    /* Start address of mmaped space. */
    uint64_t mmap_addr;
} VhostUserQtiMemRegion;

typedef struct VhostUserQtiDev {
    int dev_fd;
    int sock_fd;
    char *socket_path;
    uint16_t max_queues;

    uint32_t nregions;
    VhostUserQtiMemRegion regions[VHOST_MEMORY_MAX_NREGIONS];
    uint32_t driver_reset;
    uint32_t need_setowner;
} VhostUserQtiDev;

/* implement strlcpy to replace banned function */
static size_t strlcpy(char *dst, const char *src, size_t size)
{
    size_t n = 0;

    if (NULL == dst) {
        return n;
    } else if ((NULL == src) || (0 == size)) {
        *dst = '\0';
        return n;
    }

    while ((size > 1) && (*src != '\0')) {
        *dst++ = *src++;
        n++;
        size--;
    }

    *dst = '\0';

    return n;
}

static const char *
vhost_user_qti_request_to_string(unsigned int req)
{
#define REQ(req) [req] = #req
    static const char *vhost_user_qti_request_str[] = {
        REQ(VHOST_USER_NONE),
        REQ(VHOST_USER_GET_FEATURES),
        REQ(VHOST_USER_SET_FEATURES),
        REQ(VHOST_USER_SET_OWNER),
        REQ(VHOST_USER_RESET_OWNER),
        REQ(VHOST_USER_SET_MEM_TABLE),
        REQ(VHOST_USER_SET_LOG_BASE),
        REQ(VHOST_USER_SET_LOG_FD),
        REQ(VHOST_USER_SET_VRING_NUM),
        REQ(VHOST_USER_SET_VRING_ADDR),
        REQ(VHOST_USER_SET_VRING_BASE),
        REQ(VHOST_USER_GET_VRING_BASE),
        REQ(VHOST_USER_SET_VRING_KICK),
        REQ(VHOST_USER_SET_VRING_CALL),
        REQ(VHOST_USER_SET_VRING_ERR),
        REQ(VHOST_USER_GET_PROTOCOL_FEATURES),
        REQ(VHOST_USER_SET_PROTOCOL_FEATURES),
        REQ(VHOST_USER_GET_QUEUE_NUM),
        REQ(VHOST_USER_SET_VRING_ENABLE),
        REQ(VHOST_USER_SEND_RARP),
        REQ(VHOST_USER_NET_SET_MTU),
        REQ(VHOST_USER_SET_SLAVE_REQ_FD),
        REQ(VHOST_USER_IOTLB_MSG),
        REQ(VHOST_USER_SET_VRING_ENDIAN),
        REQ(VHOST_USER_GET_CONFIG),
        REQ(VHOST_USER_SET_CONFIG),
        REQ(VHOST_USER_CREATE_CRYPTO_SESSION),
        REQ(VHOST_USER_CLOSE_CRYPTO_SESSION),
        REQ(VHOST_USER_POSTCOPY_ADVISE),
        REQ(VHOST_USER_POSTCOPY_LISTEN),
        REQ(VHOST_USER_POSTCOPY_END),
        REQ(VHOST_USER_GET_INFLIGHT_FD),
        REQ(VHOST_USER_SET_INFLIGHT_FD),
        REQ(VHOST_USER_GPU_SET_SOCKET),
        REQ(VHOST_USER_RESET_DEVICE),
        REQ(VHOST_USER_VRING_KICK),
        REQ(VHOST_USER_GET_MAX_MEM_SLOTS),
        REQ(VHOST_USER_ADD_MEM_REG),
        REQ(VHOST_USER_REM_MEM_REG),
        REQ(VHOST_USER_SET_STATUS),
        REQ(VHOST_USER_GET_STATUS),
        REQ(VHOST_USER_MAX),
    };
#undef REQ

    if (req < VHOST_USER_MAX) {
        return vhost_user_qti_request_str[req];
    } else {
        return "unknown";
    }
}

/* Translate hypervisor process virtual address to our virtual address.  */
static void *
hva_to_va(VhostUserQtiDev *dev, uint64_t hyp_addr)
{
    int i;

    /* Find matching memory region.  */
    for (i = 0; i < dev->nregions; i++) {
        VhostUserQtiMemRegion *r = &dev->regions[i];

        if ((hyp_addr >= r->hva) && (hyp_addr < (r->hva + r->size))) {
            return (void *)(uintptr_t)
                hyp_addr - r->hva + r->mmap_addr + r->mmap_offset;
        }
    }

    return NULL;
}

static void
vmsg_close_fds(VhostUserMsg *vmsg)
{
    int i;

    for (i = 0; i < vmsg->fd_num; i++) {
        close(vmsg->fds[i]);
    }
}

/* Set reply payload.u64 and clear request flags and fd_num */
static void vmsg_set_reply_u64(VhostUserMsg *vmsg, uint64_t val)
{
    vmsg->flags = 0; /* defaults will be set by vhost_user_qti_send_reply() */
    vmsg->size = sizeof(vmsg->payload.u64);
    vmsg->payload.u64 = val;
    vmsg->fd_num = 0;
}

static int
vhost_user_qti_message_read(VhostUserQtiDev *dev, VhostUserMsg *vmsg)
{
    char control[CMSG_SPACE(VHOST_MEMORY_MAX_NREGIONS * sizeof(int))] = { };
    struct iovec iov = {
        .iov_base = (char *)vmsg,
        .iov_len = VHOST_USER_HDR_SIZE,
    };
    struct msghdr msg = {
        .msg_iov = &iov,
        .msg_iovlen = 1,
        .msg_control = control,
        .msg_controllen = sizeof(control),
    };
    size_t fd_size;
    struct cmsghdr *cmsg;
    int rc;

    do {
        rc = recvmsg(dev->sock_fd, &msg, 0);
        EPRINT("recvmsg returns %d\n", rc);
    } while (rc < 0 && (errno == EINTR || errno == EAGAIN));

    if (rc <= 0) {
        EPRINT("Error while recvmsg: %d, %s\n", rc, strerror(errno));
        return -EFAULT;
    }

    vmsg->fd_num = 0;
    for (cmsg = CMSG_FIRSTHDR(&msg);
         cmsg != NULL;
         cmsg = CMSG_NXTHDR(&msg, cmsg))
    {
        if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
            fd_size = cmsg->cmsg_len - CMSG_LEN(0);
            vmsg->fd_num = fd_size / sizeof(int);
            memcpy(vmsg->fds, CMSG_DATA(cmsg), fd_size);
            break;
        }
    }

    if (vmsg->size > sizeof(vmsg->payload)) {
        EPRINT("Error: too big message request: %d, size: vmsg->size: %u, "
               "while sizeof(vmsg->payload) = %zu\n",
               vmsg->request, vmsg->size, sizeof(vmsg->payload));
        rc = -EINVAL;
        goto fail;
    }

    if (vmsg->size) {
        do {
            rc = read(dev->sock_fd, &vmsg->payload, vmsg->size);
        } while (rc < 0 && (errno == EINTR || errno == EAGAIN));

        if (rc <= 0) {
            EPRINT("Error while reading: %s", strerror(errno));
            rc = -EFAULT;
            goto fail;
        }

        assert(rc == vmsg->size);
    }

    return 0;

fail:
    vmsg_close_fds(vmsg);

    return rc;
}

static int
vhost_user_qti_message_write(VhostUserQtiDev *dev, VhostUserMsg *vmsg)
{
    int rc;
    uint8_t *p = (uint8_t *)vmsg;
    char control[CMSG_SPACE(VHOST_MEMORY_MAX_NREGIONS * sizeof(int))] = { };
    struct iovec iov = {
        .iov_base = (char *)vmsg,
        .iov_len = VHOST_USER_HDR_SIZE,
    };
    struct msghdr msg = {
        .msg_iov = &iov,
        .msg_iovlen = 1,
        .msg_control = control,
    };
    struct cmsghdr *cmsg;

    memset(control, 0, sizeof(control));
    assert(vmsg->fd_num <= VHOST_MEMORY_MAX_NREGIONS);
    if (vmsg->fd_num > 0) {
        size_t fdsize = vmsg->fd_num * sizeof(int);
        msg.msg_controllen = CMSG_SPACE(fdsize);
        cmsg = CMSG_FIRSTHDR(&msg);
        cmsg->cmsg_len = CMSG_LEN(fdsize);
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_RIGHTS;
        memcpy(CMSG_DATA(cmsg), vmsg->fds, fdsize);
    } else {
        msg.msg_controllen = 0;
    }

    do {
        rc = sendmsg(dev->sock_fd, &msg, 0);
    } while (rc < 0 && (errno == EINTR || errno == EAGAIN));

    if (vmsg->size) {
        do {
            if (vmsg->data) {
                rc = write(dev->sock_fd, vmsg->data, vmsg->size);
            } else {
                rc = write(dev->sock_fd, p + VHOST_USER_HDR_SIZE, vmsg->size);
            }
        } while (rc < 0 && (errno == EINTR || errno == EAGAIN));
    }

    if (rc <= 0) {
        EPRINT("Error while writing: %s", strerror(errno));
        return -EFAULT;
    }

    return 0;
}

static int
vhost_user_qti_send_reply(VhostUserQtiDev *dev, VhostUserMsg *vmsg)
{
    /* Set the version in the flags when sending the reply */
    vmsg->flags &= ~VHOST_USER_VERSION_MASK;
    vmsg->flags |= VHOST_USER_VERSION;
    vmsg->flags |= VHOST_USER_REPLY_MASK;

    return vhost_user_qti_message_write(dev, vmsg);
}

static int
vhost_user_qti_get_features_exec(VhostUserQtiDev *dev, VhostUserMsg *vmsg)
{
    int ret;

    vmsg->flags = 0;
    vmsg->size = sizeof(vmsg->payload.u64);
    vmsg->fd_num = 0;

    ret = vhost_kernel_get_features(dev->dev_fd, &vmsg->payload.u64);

    vmsg->payload.u64 |= (1 << VIRTIO_GPU_F_VENDOR);
    vmsg->payload.u64 |= (1 << VHOST_USER_F_PROTOCOL_FEATURES);

    DPRINT("Sending back to guest u64: 0x%016"PRIx64"\n", vmsg->payload.u64);

    return ret;
}

static int
vhost_user_qti_set_features_exec(VhostUserQtiDev *dev, VhostUserMsg *vmsg)
{
    DPRINT("u64: 0x%016"PRIx64"\n", vmsg->payload.u64);

    return vhost_kernel_set_features(dev->dev_fd, vmsg->payload.u64);
}

static int
vhost_user_qti_set_owner_exec(VhostUserQtiDev *dev, VhostUserMsg *vmsg)
{
    //return vhost_kernel_set_owner(dev->dev_fd);
    vhost_kernel_set_owner(dev->dev_fd);
    /* opsy virtio-gpu doesn't send VHOST_USER_SET_FEATURES, we need to do it */
    return vhost_kernel_set_features(dev->dev_fd, 0x130000000);
}

static int
vhost_user_qti_reset_device_exec(VhostUserQtiDev *dev, VhostUserMsg *vmsg)
{
    int ret = 0;

    /* VHOST_USER_PROTOCOL_F_STATUS supersedes the feature VHOST_USER_PROTOCOL_F_RESET_DEVICE
       so not do anything here now
    */
    //ret = vhost_kernel_reset_device(dev->dev_fd);
    //dev->need_setowner = 1;

    return ret;
}

static int
vhost_user_qti_set_mem_table_exec(VhostUserQtiDev *dev, VhostUserMsg *vmsg)
{
    int i;
    VhostUserMemory m = vmsg->payload.memory, *memory = &m;
    struct vhost_memory *k_mem;
    int ret;

    for (i = 0; i < dev->nregions; i++) {
        VhostUserQtiMemRegion *r = &dev->regions[i];
        void *m = (void *) (uintptr_t) r->mmap_addr;

        if (m) {
            munmap(m, r->size + r->mmap_offset);
        }
    }

    k_mem = calloc(1, offsetof(struct vhost_memory, regions) +
                       memory->nregions * sizeof(k_mem->regions[0]));
    if (NULL == k_mem) {
        EPRINT("failed to alloc vhost_memory, nregions: %d\n", memory->nregions);
        return -ENOMEM;
    }

    dev->nregions = memory->nregions;
    k_mem->nregions = memory->nregions;

    DPRINT("Nregions: %d\n", memory->nregions);
    for (i = 0; i < dev->nregions; i++) {
        void *mmap_addr;
        VhostUserMemoryRegion *msg_region = &memory->regions[i];
        VhostUserQtiMemRegion *dev_region = &dev->regions[i];

        DPRINT("Region %d\n", i);
        DPRINT("    guest_phys_addr: 0x%016"PRIx64"\n",
               msg_region->guest_phys_addr);
        DPRINT("    memory_size:     0x%016"PRIx64"\n",
               msg_region->memory_size);
        DPRINT("    userspace_addr   0x%016"PRIx64"\n",
               msg_region->userspace_addr);
        DPRINT("    mmap_offset      0x%016"PRIx64"\n",
               msg_region->mmap_offset);

        dev_region->gpa = msg_region->guest_phys_addr;
        dev_region->size = msg_region->memory_size;
        dev_region->hva = msg_region->userspace_addr;
        dev_region->mmap_offset = msg_region->mmap_offset;

        k_mem->regions[i].guest_phys_addr = msg_region->guest_phys_addr;
        k_mem->regions[i].memory_size = msg_region->memory_size;
        k_mem->regions[i].flags_padding = 0;

        /* We don't use offset argument of mmap() since the
         * mapped address has to be page aligned, and we use huge
         * pages.  */
        mmap_addr = mmap(0, dev_region->size + dev_region->mmap_offset,
                         PROT_READ | PROT_WRITE, MAP_SHARED,
                         vmsg->fds[i], 0);

        if (mmap_addr == MAP_FAILED) {
            EPRINT("region mmap error: %s", strerror(errno));
        } else {
            dev_region->mmap_addr = (uint64_t)(uintptr_t)mmap_addr;
            k_mem->regions[i].userspace_addr = (uint64_t)(uintptr_t)(mmap_addr + msg_region->mmap_offset);
            DPRINT("    mmap_addr:       0x%016"PRIx64"\n",
                   dev_region->mmap_addr);
        }

        close(vmsg->fds[i]);
    }

    ret = vhost_kernel_set_mem_table(dev->dev_fd, k_mem);

    free(k_mem);
    return ret;
}

static int
vhost_user_qti_set_vring_num_exec(VhostUserQtiDev *dev, VhostUserMsg *vmsg)
{
    DPRINT("State.index: %d\n", vmsg->payload.state.index);
    DPRINT("State.num:   %d\n", vmsg->payload.state.num);

    /* opsy virtio-gpu send us index 2 and 3 for vendor queues, we need to change them to 0 and 1 */
    vmsg->payload.state.index -= 2;
    return vhost_kernel_set_vring_num(dev->dev_fd, &vmsg->payload.state);
}

static int
vhost_user_qti_set_vring_addr_exec(VhostUserQtiDev *dev, VhostUserMsg *vmsg)
{
    struct vhost_vring_addr addr = vmsg->payload.addr, *vra = &addr, k_vra;

    DPRINT("vhost_vring_addr:\n");
    DPRINT("    index:  %d\n", vra->index);
    DPRINT("    flags:  %d\n", vra->flags);
    DPRINT("    desc_user_addr:   0x%016" PRIx64 "\n", vra->desc_user_addr);
    DPRINT("    used_user_addr:   0x%016" PRIx64 "\n", vra->used_user_addr);
    DPRINT("    avail_user_addr:  0x%016" PRIx64 "\n", vra->avail_user_addr);
    DPRINT("    log_guest_addr:   0x%016" PRIx64 "\n", vra->log_guest_addr);

    if (!(vra->desc_user_addr && vra->used_user_addr && vra->avail_user_addr)) {
        EPRINT("Invalid vring_addr message");
        return -EINVAL;
    }

    k_vra.index = vra->index - 2;
    k_vra.flags = vra->flags & (~(1 << VHOST_VRING_F_LOG));//we don't support log
    k_vra.desc_user_addr = (uint64_t)(uintptr_t)hva_to_va(dev, vra->desc_user_addr);
    k_vra.used_user_addr = (uint64_t)(uintptr_t)hva_to_va(dev, vra->used_user_addr);
    k_vra.avail_user_addr = (uint64_t)(uintptr_t)hva_to_va(dev, vra->avail_user_addr);
    k_vra.log_guest_addr = vra->log_guest_addr;

    DPRINT("Setting virtq addresses:\n");
    DPRINT("    vring_desc  at %p\n", k_vra.desc_user_addr);
    DPRINT("    vring_used  at %p\n", k_vra.used_user_addr);
    DPRINT("    vring_avail at %p\n", k_vra.avail_user_addr);

    return vhost_kernel_set_vring_addr(dev->dev_fd, &k_vra);
}

static int
vhost_user_qti_set_vring_base_exec(VhostUserQtiDev *dev, VhostUserMsg *vmsg)
{
    unsigned int index = vmsg->payload.state.index;
    unsigned int num = vmsg->payload.state.num;

    DPRINT("State.index: %d\n", index);
    DPRINT("State.num:   %d\n", num);

    vmsg->payload.state.index -= 2;
    return vhost_kernel_set_vring_base(dev->dev_fd, &vmsg->payload.state);
}

static int
vhost_user_qti_get_vring_base_exec(VhostUserQtiDev *dev, VhostUserMsg *vmsg)
{
    unsigned int index = vmsg->payload.state.index;

    DPRINT("State.index: %d\n", index);
    vmsg->size = sizeof(vmsg->payload.state);

    vmsg->payload.state.index -= 2;
    return vhost_kernel_get_vring_base(dev->dev_fd, &vmsg->payload.state);
}

static int
vhost_user_qti_set_vring_kick_exec(VhostUserQtiDev *dev, VhostUserMsg *vmsg)
{
    int index = vmsg->payload.u64 & VHOST_USER_VRING_IDX_MASK;
    struct vhost_vring_file k_vrf;

    DPRINT("u64: 0x%016"PRIx64"\n", vmsg->payload.u64);

    k_vrf.index = index - 2;
    k_vrf.fd = vmsg->fds[0];

    DPRINT("Got kick_fd: %d for vq: %d\n", vmsg->fds[0], index);

    return vhost_kernel_set_vring_kick(dev->dev_fd, &k_vrf);
}

static int
vhost_user_qti_set_vring_call_exec(VhostUserQtiDev *dev, VhostUserMsg *vmsg)
{
    int index = vmsg->payload.u64 & VHOST_USER_VRING_IDX_MASK;
    struct vhost_vring_file k_vrf;

    DPRINT("u64: 0x%016"PRIx64"\n", vmsg->payload.u64);

    k_vrf.index = index - 2;
    k_vrf.fd = vmsg->fds[0];

    DPRINT("Got call_fd: %d for vq: %d\n", vmsg->fds[0], index);

    return vhost_kernel_set_vring_call(dev->dev_fd, &k_vrf);
}

static int
vhost_user_qti_set_vring_err_exec(VhostUserQtiDev *dev, VhostUserMsg *vmsg)
{
    int index = vmsg->payload.u64 & VHOST_USER_VRING_IDX_MASK;
    struct vhost_vring_file k_vrf;

    DPRINT("u64: 0x%016"PRIx64"\n", vmsg->payload.u64);

    k_vrf.index = index - 2;
    k_vrf.fd = vmsg->fds[0];

    DPRINT("Got err_fd: %d for vq: %d\n", vmsg->fds[0], index);

    return vhost_kernel_set_vring_err(dev->dev_fd, &k_vrf);
}

static int
vhost_user_qti_get_queue_num_exec(VhostUserQtiDev *dev, VhostUserMsg *vmsg)
{
    vmsg_set_reply_u64(vmsg, dev->max_queues);

    return 0;
}

static int
vhost_user_qti_set_config_exec(VhostUserQtiDev *dev, VhostUserMsg *vmsg)
{
    struct vhost_config k_cfg;

    k_cfg.offset = vmsg->payload.config.offset;
    k_cfg.size = vmsg->payload.config.size;
    k_cfg.flags = vmsg->payload.config.flags;
    k_cfg.data = vmsg->payload.config.region;

    DPRINT("set_config: offset %d, size %d, flags 0x%x\n", k_cfg.offset, k_cfg.size, k_cfg.flags);

    return vhost_kernel_set_config(dev->dev_fd, &k_cfg);
}

static int
vhost_user_qti_get_protocol_features_exec(VhostUserQtiDev *dev, VhostUserMsg *vmsg)
{
    int ret = 0;

    vmsg->flags = 0;
    vmsg->size = sizeof(vmsg->payload.u64);
    vmsg->payload.u64 = 0;
    vmsg->fd_num = 0;

    /*
      VHOST_USER_PROTOCOL_F_STATUS supersedes the feature VHOST_USER_PROTOCOL_F_RESET_DEVICE
      so not do anything here now
    */
    //vmsg->payload.u64 |= (1 << VHOST_USER_PROTOCOL_F_RESET_DEVICE);
    vmsg->payload.u64 |= (1 << VHOST_USER_PROTOCOL_F_REPLY_ACK);
    vmsg->payload.u64 |= (1 << VHOST_USER_PROTOCOL_F_STATUS);
    DPRINT("Sending protocol feature back to guest u64: 0x%016"PRIx64"\n", vmsg->payload.u64);

    return ret;
}

static int
vhost_user_qti_set_protocol_features_exec(VhostUserQtiDev *dev, VhostUserMsg *vmsg)
{
    DPRINT("u64: 0x%016"PRIx64"\n", vmsg->payload.u64);
    return 0;
}

static int
vhost_user_qti_set_status_exec(VhostUserQtiDev *dev, VhostUserMsg *vmsg)
{
    int ret = 0;
    bool reply_supported = 0;

    DPRINT("u64: 0x%016"PRIx64"\n", vmsg->payload.u64);
    reply_supported = vmsg->flags & (1 << VHOST_USER_PROTOCOL_F_REPLY_ACK);

    if (0 == vmsg->payload.u64) {
        dev->driver_reset += 1;
    } else {
        dev->driver_reset = 0;
    }

    if (1 == dev->driver_reset) {
        ret = vhost_kernel_reset_device(dev->dev_fd);
        dev->need_setowner = 1;
    }

    if (reply_supported) {
    /*
      This indicates that the back-end MUST respond with a Payload VhostUserMsg indicating success
      or failure. The payload should be set to zero on success or non-zero on failure
    */
	if (ret)
		vmsg->payload.u64 = 1;
	else
		vmsg->payload.u64 = 0;
    }

    return ret;
}

static int
vhost_user_qti_process_message(VhostUserQtiDev *dev, VhostUserMsg *vmsg)
{
    int do_reply = 0;

    /* Print out generic part of the request. */
    DPRINT("================ Vhost user message ================\n");
    DPRINT("Request: %s (%d)\n", vhost_user_qti_request_to_string(vmsg->request),
           vmsg->request);
    DPRINT("Flags:   0x%x\n", vmsg->flags);
    DPRINT("Size:    %d\n", vmsg->size);

    if (vmsg->fd_num) {
        int i;
        DPRINT("Fds:");
        for (i = 0; i < vmsg->fd_num; i++) {
            DPRINT(" %d", vmsg->fds[i]);
        }
        DPRINT("\n");
    }

    if (dev->need_setowner) {
        vhost_user_qti_set_owner_exec(dev, vmsg);
        dev->need_setowner = 0;
    }

    switch (vmsg->request) {
    case VHOST_USER_GET_FEATURES:
        return vhost_user_qti_get_features_exec(dev, vmsg);
    case VHOST_USER_SET_FEATURES:
        return vhost_user_qti_set_features_exec(dev, vmsg);
    case VHOST_USER_SET_OWNER:
        return vhost_user_qti_set_owner_exec(dev, vmsg);
    case VHOST_USER_RESET_OWNER:
        return vhost_user_qti_reset_device_exec(dev, vmsg);
    case VHOST_USER_SET_MEM_TABLE:
        return vhost_user_qti_set_mem_table_exec(dev, vmsg);
    case VHOST_USER_SET_VRING_NUM:
        return vhost_user_qti_set_vring_num_exec(dev, vmsg);
    case VHOST_USER_SET_VRING_ADDR:
        return vhost_user_qti_set_vring_addr_exec(dev, vmsg);
    case VHOST_USER_SET_VRING_BASE:
        return vhost_user_qti_set_vring_base_exec(dev, vmsg);
    case VHOST_USER_GET_VRING_BASE:
        return vhost_user_qti_get_vring_base_exec(dev, vmsg);
    case VHOST_USER_SET_VRING_KICK:
        return vhost_user_qti_set_vring_kick_exec(dev, vmsg);
    case VHOST_USER_SET_VRING_CALL:
        return vhost_user_qti_set_vring_call_exec(dev, vmsg);
    case VHOST_USER_SET_VRING_ERR:
        return vhost_user_qti_set_vring_err_exec(dev, vmsg);
    case VHOST_USER_GET_QUEUE_NUM:
        return vhost_user_qti_get_queue_num_exec(dev, vmsg);
    case VHOST_USER_SET_CONFIG:
        return vhost_user_qti_set_config_exec(dev, vmsg);
    case VHOST_USER_GET_PROTOCOL_FEATURES:
        return vhost_user_qti_get_protocol_features_exec(dev, vmsg);
    case VHOST_USER_SET_PROTOCOL_FEATURES:
        return vhost_user_qti_set_protocol_features_exec(dev, vmsg);
    case VHOST_USER_RESET_DEVICE:
        return vhost_user_qti_reset_device_exec(dev, vmsg);
    case VHOST_USER_SET_STATUS:
	return vhost_user_qti_set_status_exec(dev, vmsg);
    default:
        vmsg_close_fds(vmsg);
        EPRINT("Unhandled request: %d", vmsg->request);
    }

    return -ENOTSUP;
}

static inline int msg_reply_requested(VhostUserMsg *vmsg)
{
    switch (vmsg->request) {
    case VHOST_USER_GET_FEATURES:
    case VHOST_USER_GET_PROTOCOL_FEATURES:
    case VHOST_USER_GET_VRING_BASE:
    case VHOST_USER_GET_QUEUE_NUM:
        return 1;
    case VHOST_USER_SET_STATUS:
    {
        if (vmsg->flags & (1 << VHOST_USER_PROTOCOL_F_REPLY_ACK))
            return 1;
        else
            return 0;
    }
    default:
        return 0;
    }
}

int
vhost_user_qti_dispatch(VhostUserQtiDev *dev)
{
    VhostUserMsg vmsg = { 0 };
    int ret;
    int success = 0;

    ret = vhost_user_qti_message_read(dev, &vmsg);
    if (ret) {
        EPRINT("vhost_user_qti_message_read failed, %d\n", ret);
        goto end;
    }

    ret = vhost_user_qti_process_message(dev, &vmsg);
    if (ret) {
        EPRINT("vhost_user_qti_process_message failed, request %d, ret %d\n", vmsg.request, ret);
        goto end;
    } else if (!msg_reply_requested(&vmsg)) {
        success = 1;
        goto end;
    }

    ret = vhost_user_qti_send_reply(dev, &vmsg);
    if (ret) {
        EPRINT("vhost_user_qti_send_reply failed, request %d, ret %d\n", vmsg.request, ret);
        goto end;
    }

    success = 1;

end:
    free(vmsg.data);
    return success;
}

static void print_usage(void)
{
    EPRINT("Usage:\n"
           "\tvhost-user-qti -s socket_path -d vhost_dev_path -q queue_number(0~65536)\n");
}

int main(int argc, char *argv[])
{
    int ret = 0;
    int c;
    char *dev_path = NULL;
    struct sockaddr_un un = { 0 }, un_peer = { 0 };
    socklen_t un_peer_len = sizeof(un_peer);
    VhostUserQtiDev dev = { 0 };
    int lsock;

    while ((c = getopt(argc, argv, "s:d:q:")) != -1) {
        switch (c) {
        case 's':
           dev.socket_path = optarg;
           break;
        case 'd':
           dev_path = optarg;
           break;
        case 'q':
           dev.max_queues = atoi(optarg);
           break;
        default:
           print_usage();
           exit(EXIT_FAILURE);
        }
    }

    if (!dev.socket_path || !dev_path || (dev.max_queues <= 0) || (dev.max_queues > 0xFFFF)) {
        print_usage();
        exit(EXIT_FAILURE);
    }

    dev.dev_fd = open(dev_path, O_RDWR);
    if (dev.dev_fd < 0) {
        EPRINT("failed to open vhost dev %s, %s\n", dev_path, strerror(errno));
        exit(EXIT_FAILURE);
    }

    DPRINT("open %s success\n", dev_path);

    lsock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (lsock < 0) {
        EPRINT("failed to open stream socket, %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    ret = unlink(dev.socket_path);
    if ((ret < 0) && (errno != ENOENT)) {
	EPRINT("failed to unlink socket %s, %d(%s)\n", dev.socket_path, ret, strerror(errno));
        exit(EXIT_FAILURE);
    }

    un.sun_family = AF_UNIX;
    strlcpy(un.sun_path, dev.socket_path, sizeof(un.sun_path));

    ret = bind(lsock, (struct sockaddr*)&un, sizeof(un));
    if (ret < 0) {
        EPRINT("failed to bind to %s, %d(%s)\n", dev.socket_path, ret, strerror(errno));
        exit(EXIT_FAILURE);
    }

    DPRINT("listening to socket %s ...\n", dev.socket_path);
    ret = listen(lsock, 1);
    if (ret < 0) {
        EPRINT("failed to listen to %s, %d(%s)\n", dev.socket_path, ret, strerror(errno));
        exit(EXIT_FAILURE);
    }

    dev.sock_fd = accept(lsock, (struct sockaddr*)&un_peer, &un_peer_len);
    if (dev.sock_fd < 0) {
        EPRINT("failed to accept on %s, %s\n", dev.socket_path, strerror(errno));
        exit(EXIT_FAILURE);
    }

    DPRINT("socket connected, fd=%d, path %s, family %d, peer_len=%d\n", dev.sock_fd, un_peer.sun_path, un_peer.sun_family, un_peer_len);

    close(lsock);

/* Based on the discussion with opsy, the vm name will be set by VHOST_USER_SET_CONFIG.
 * For now, just parse it from socket name
 */
#ifndef ENABLE_VM_NAME_IN_SET_CONFIG
    {
        struct vhost_hab_config hab_cfg;
        struct vhost_config k_cfg = {
            .offset = 0,
            .size = sizeof(hab_cfg),
            .flags = 0,
            .data = (__u8 *)&hab_cfg,
        };

        strlcpy(hab_cfg.vm_name, dev.socket_path, sizeof(hab_cfg.vm_name));

        ret = vhost_kernel_set_config(dev.dev_fd, &k_cfg);
        if (ret < 0) {
            EPRINT("failed to set config to kernel, vm name: %s\n", hab_cfg.vm_name );
            exit(EXIT_FAILURE);
        }
    }
#endif

    while (vhost_user_qti_dispatch(&dev));

    close(dev.sock_fd);
    close(dev.dev_fd);

    EPRINT("vhost-user-qti exit\n");
    return 0;
}
