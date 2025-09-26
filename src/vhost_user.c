/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2010-2018 Intel Corporation
 */

/* Changes from Qualcomm Technologies, Inc. are provided under the following license:
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "vhost_user.h"
#include "vu_common.h"
#include "vu_socket.h"
#include "vhost_user_vmm.h"

static int log_info = 1;
static int log_debug;

typedef struct vhost_message_handler {
    const char *description;
    int (*callback)(struct vhost_user_dev *dev, struct vhu_msg_context *ctx);
    bool accepts_fd;
} vhost_message_handler_t;

extern vhost_user_q_state vhost_user_qti_state;

static void store_fd(int *fd_array, int fd)
{
    int i = 0;

    for (; i < MAX_FD_NR; i++)
        if (fd_array[i] == -1) {
            fd_array[i] = fd;
            break;
        }
}

static void free_fds(int *fd_array)
{
    int i = 0;

    for (; i < MAX_FD_NR; i++)
        if (fd_array[i] != -1) {
            close(fd_array[i]);
            fd_array[i] = -1;
        }
}

/*
 * The features that we support are requested.
 */
static int
vhost_user_get_features(struct vhost_user_dev *dev,
            struct vhu_msg_context *ctx)
{
    uint64_t features = 0;
    int ret = -1;

    if (dev->kernel_ops->get_features)
        ret = dev->kernel_ops->get_features(dev->vdev, &features);

    if (ret < 0) {
        pr_err("failed to get config!\n");
        return VHOST_MSG_RESULT_ERR;
    }

#ifndef CONFIG_HGY_PLATFORM
    ctx->msg.payload.u64 |= (1 << VIRTIO_GPU_F_VENDOR);
    ctx->msg.payload.u64 |= (1 << VHOST_USER_F_PROTOCOL_FEATURES);
#endif

    ctx->msg.size = sizeof(ctx->msg.payload.u64);
    ctx->fd_num = 0;
    pr_debug("get features %lx \n", ctx->msg.payload.u64);

    return VHOST_MSG_RESULT_REPLY;
}

static int
vhost_user_set_features(struct vhost_user_dev *dev,
            struct vhu_msg_context *ctx)
{
    uint64_t features = ctx->msg.payload.u64;
    int ret = -1;

    if (dev->kernel_ops->set_features)
        ret = dev->kernel_ops->set_features(dev->vdev, features);

    if (ret < 0) {
        pr_err("failed to set config!\n");
        return VHOST_MSG_RESULT_ERR;
    }

    pr_debug("set features: 0x%lx\n", features);

    return VHOST_MSG_RESULT_OK;
}

/*
 * The config that we support are requested.
 */
static int
vhost_user_get_config(struct vhost_user_dev *dev,
            struct vhu_msg_context *ctx)
{
    int ret;

    uint32_t offset = ctx->msg.payload.config.offset;
    uint32_t size = ctx->msg.payload.config.size;
    char *payload = &(ctx->msg.payload.config.payload);

    if (dev->kernel_ops->get_config) {
        ret = dev->kernel_ops->get_config(dev->vdev, offset, size, payload);

        if (ret < 0) {
            pr_err("failed to get config! offset: 0x%x size: 0x%x \n", offset, size);
            /* vhost-user back-end uses zero length of payload to indicate
            an error to the vhost-user front-end.*/
            ctx->msg.payload.config.size = 0;
        }
    } else {
        pr_err("set config is not support!\n");
    }

    pr_debug("get config, offset: 0x%x size: 0x%x \n", offset, size);

    return VHOST_MSG_RESULT_REPLY;
}

static int
vhost_user_set_config(struct vhost_user_dev *dev,
            struct vhu_msg_context *ctx)
{
    int ret;
    uint32_t offset = ctx->msg.payload.config.offset;
    uint32_t size = ctx->msg.payload.config.size;
    uint32_t flag = ctx->msg.payload.config.flag;
    char *payload = &(ctx->msg.payload.config.payload);

    if (dev->kernel_ops->set_config) {
            ret = dev->kernel_ops->set_config(dev->vdev, offset, size, flag, payload);
        if (ret < 0) {
            pr_err("failed to set config! offset: 0x%x size: 0x%x flag: 0x%x\n",
                offset, size, flag);
            return VHOST_MSG_RESULT_ERR;
        }
    } else {
        pr_err("set config is not support!\n");
    }

    pr_debug("set config, offset: 0x%x size: 0x%x flag: 0x%x\n", offset, size, flag);

    return VHOST_MSG_RESULT_OK;
}

static int
vhost_user_set_owner(struct vhost_user_dev *dev,
            struct vhu_msg_context *ctx)
{
    int ret = -1;

    if (dev->vdev->valid) {
        pr_err("failed to set owner valid %d\n", dev->vdev->valid);
        return VHOST_MSG_RESULT_ERR;
    }

    if (dev->kernel_ops->set_owner)
        ret = dev->kernel_ops->set_owner(dev->vdev);
    else
        ret = -1;

    if (ret < 0) {
        pr_err("failed to set owner\n");
        return VHOST_MSG_RESULT_ERR;
    }

    dev->vdev->valid = 1;
    pr_debug("success set owner\n");

#ifndef CONFIG_HGY_PLATFORM
    /* opsy virtio-gpu doesn't send VHOST_USER_SET_FEATURES, we need to do it */
    if (dev->kernel_ops->set_features)
        ret = dev->kernel_ops->set_features(dev->vdev, 0x130000000);

    if (ret < 0) {
        pr_err("failed to set config!\n");
        return VHOST_MSG_RESULT_ERR;
    }

    pr_debug("set features: 0x%lx\n", features);
#endif

    return VHOST_MSG_RESULT_OK;
}

static int
vhost_user_reset_owner(struct vhost_user_dev *dev,
            struct vhu_msg_context *ctx)
{
    int ret = -1;

#ifdef CONFIG_HGY_PLATFORM
    /* We have to stop the queue (virtio) if it is running. */
    if (dev->vdev->valid) {
        pr_debug("start to send VHOST_RESET_OWNER cmd\n");
        vhost_user_qti_state = VHOST_USER_QTI_RESETING_VHOST_DEV;

        if (dev->kernel_ops->reset_owner)
            ret = dev->kernel_ops->reset_owner(dev->vdev);
        else
            ret = -1;

        if (ret < 0) {
            pr_err("failed to reset owner\n");
            return VHOST_MSG_RESULT_ERR;
        }
        dev->vdev->valid = 0;
    }

    pr_info("success reset owner\n");
#else
    /* VHOST_USER_PROTOCOL_F_STATUS supersedes the feature VHOST_USER_PROTOCOL_F_RESET_DEVICE
       so not do anything here now */
#endif

    return VHOST_MSG_RESULT_OK;
}

static int
vhost_user_mmap_region(struct vhost_user_dev *dev,
        struct vhost_user_mem_region *region,
        uint64_t mmap_offset)
{
    void *mmap_addr;
    uint64_t mmap_size;

    /* Check for memory_size + mmap_offset overflow */
    if (mmap_offset >= -region->size) {
        pr_err("mmap_offset (0x%lx) and memory_size (0x%lx) overflow\n",
            mmap_offset, region->size);
        return -1;
    }

    mmap_size = region->size + mmap_offset;

    mmap_addr = mmap(NULL, mmap_size, PROT_READ | PROT_WRITE,
            MAP_SHARED, region->fd, 0);

    if (mmap_addr == MAP_FAILED) {
        pr_err("mmap failed (%s).\n", strerror(errno));
        return -1;
    }

    region->mmap_addr = mmap_addr;
    region->mmap_size = mmap_size;
    region->host_user_addr = (uint64_t)(uintptr_t)mmap_addr + mmap_offset;

    pr_debug("guest memory region size: 0x%lx\n", region->size);
    pr_debug("\t guest physical addr: 0x%lx\n", region->guest_phys_addr);
    pr_debug("\t guest virtual  addr: 0x%lx\n", region->guest_user_addr);
    pr_debug("\t host  virtual  addr: 0x%lx\n", region->host_user_addr);
    pr_debug("\t mmap addr : 0x%lx\n", (uint64_t)(uintptr_t)mmap_addr);
    pr_debug("\t mmap size : 0x%lx\n", mmap_size);
    pr_debug("\t mmap off  : 0x%lx\n", mmap_offset);

    return VHOST_MSG_RESULT_OK;
}

static int
vhost_user_set_mem_table(struct vhost_user_dev *dev,
            struct vhu_msg_context *ctx)
{
    struct VhostUserMemory *memory = &ctx->msg.payload.memory;
    struct vhost_user_mem_region *reg;
    uint64_t mmap_offset;
    uint32_t i;
    int ret = -1;

    if (memory->nregions > VHOST_MEMORY_MAX_NREGIONS) {
        pr_err("too many memory regions (%u)\n",
            memory->nregions);
            return -1;
    }
    if (ctx->fd_num != memory->nregions) {
        pr_err("fd number is not consistent with nregions\n");
        return -1;
    }
    dev->mem = calloc(sizeof(struct vhost_user_mem) + sizeof(struct vhost_user_mem_region) * memory->nregions, 1);
    if (dev->mem == NULL) {
        pr_err("failed to allocate memory\n");
        return -1;
    }
    for (i = 0; i < memory->nregions; i++) {
        reg = &dev->mem->regions[i];

        reg->guest_phys_addr = memory->regions[i].guest_phys_addr;
        reg->guest_user_addr = memory->regions[i].userspace_addr;
        reg->size            = memory->regions[i].memory_size;
        reg->fd              = ctx->fds[i];

        /*
         * Assign invalid file descriptor value to avoid double
         * closing on error path.
         */

        mmap_offset = memory->regions[i].mmap_offset;

        if (vhost_user_mmap_region(dev, reg, mmap_offset) < 0) {
            pr_err("failed to mmap region %u\n", i);
            return -1;
        }

        dev->mem->nregions++;
    }

    if (dev->kernel_ops->set_memory_table)
        ret = dev->kernel_ops->set_memory_table(dev->vdev, dev->mem);
    else
        ret = -1;

    if (ret < 0) {
        pr_err("failed to set_memory_table!\n");
        return VHOST_MSG_RESULT_ERR;
    }

    return 0;
}

/*
 * The virtio device sends us the size of the descriptor ring.
 */
static int
vhost_user_set_vring_num(struct vhost_user_dev *dev,
            struct vhu_msg_context *ctx)
{
    int ret = -1;

    if (ctx->msg.payload.state.num > 32768) {
        pr_err("invalid virtqueue size %u\n",
            ctx->msg.payload.state.num);
        return VHOST_MSG_RESULT_ERR;
    }

    pr_debug("State.index: %d\n", ctx->msg.payload.state.index);
    pr_debug("State.num:   %d\n", ctx->msg.payload.state.num);

#ifndef CONFIG_HGY_PLATFORM
    /* opsy virtio-gpu send us index 2 and 3 for vendor queues, we need to change them to 0 and 1 */
    ctx->msg.payload.state.index -= 2;
#endif

    if (dev->kernel_ops->set_vring_num)
        ret = dev->kernel_ops->set_vring_num(dev->vdev, &ctx->msg.payload.state);
    else
        ret = -1;

    if (ret < 0) {
        pr_err("failed to set_vring_num!\n");
        return VHOST_MSG_RESULT_ERR;
    }

    return VHOST_MSG_RESULT_OK;
}

/* Converts VMM virtual address to Vhost virtual address. */
static uint64_t
qva_to_vva(struct vhost_user_dev *dev, uint64_t qva)
{
    struct vhost_user_mem_region *r;
    uint32_t i;

    if (!dev || !dev->mem)
        return 0;

    /* Find the region where the address lives. */
    for (i = 0; i < dev->mem->nregions; i++) {
        r = &dev->mem->regions[i];

        if (qva >= r->guest_user_addr &&
            qva <  r->guest_user_addr + r->size) {
            return qva - r->guest_user_addr +
                   r->host_user_addr;
        }
    }

    return 0;
}

static int
setup_ring_addr(struct vhost_user_dev *dev, struct vhost_vring_addr *msg_addr,
    struct vhost_vring_addr *vhost_kernel_addr)
{
#ifdef CONFIG_HGY_PLATFORM
    vhost_kernel_addr->index = msg_addr->index;
#else
    vhost_kernel_addr->index = msg_addr->index - 2;
#endif
    /* we don't support log */
    vhost_kernel_addr->flags = msg_addr->flags & (~(1 << VHOST_VRING_F_LOG));

    vhost_kernel_addr->desc_user_addr =
                qva_to_vva(dev, msg_addr->desc_user_addr);
    if (vhost_kernel_addr->desc_user_addr == 0) {
        pr_err("failed to map desc ring.\n");
        return -1;
    }

    vhost_kernel_addr->avail_user_addr =
                qva_to_vva(dev, msg_addr->avail_user_addr);
    if (vhost_kernel_addr->avail_user_addr == 0) {
        pr_err("failed to map avail ring.\n");
        return -1;
    }

    vhost_kernel_addr->used_user_addr =
                qva_to_vva(dev, msg_addr->used_user_addr);
    if (vhost_kernel_addr->used_user_addr == 0) {
        pr_err("failed to map used ring.\n");
        return -1;
    }

    vhost_kernel_addr->log_guest_addr = msg_addr->log_guest_addr;

    pr_debug("mapped address desc: %llx\n", vhost_kernel_addr->desc_user_addr);
    pr_debug("mapped address avail: %llx\n", vhost_kernel_addr->avail_user_addr);
    pr_debug("mapped address used: %llx\n", vhost_kernel_addr->used_user_addr);

    return 0;
}


/*
 * The virtio device sends us the desc, used and avail ring addresses.
 * This function then converts these to our address space.
 */
static int
vhost_user_set_vring_addr(struct vhost_user_dev *dev,
            struct vhu_msg_context *ctx)
{
    struct vhost_vring_addr *msg_addr = &ctx->msg.payload.addr;
    struct vhost_vring_addr vhost_kernel_addr;
    int ret = -1;

    if (dev->mem == NULL)
        return VHOST_MSG_RESULT_ERR;

    ret = setup_ring_addr(dev, msg_addr, &vhost_kernel_addr);
    if (ret < 0) {
        pr_err("failed to setup ring address.\n");
        return VHOST_MSG_RESULT_ERR;
    }

    if (dev->kernel_ops->set_vring_addr)
        ret = dev->kernel_ops->set_vring_addr(dev->vdev, &vhost_kernel_addr);
    else
        ret = -1;

    if (ret < 0) {
        pr_err("failed to set_vring_addr!\n");
        return VHOST_MSG_RESULT_ERR;
    }

    return VHOST_MSG_RESULT_OK;
}

/*
 * The virtio device sends us the available ring last used index.
 */
static int
vhost_user_set_vring_base(struct vhost_user_dev *dev,
            struct vhu_msg_context *ctx)
{
    int ret = -1;

    pr_debug("vring base idx:%u idx:%u\n",
        ctx->msg.payload.state.index, ctx->msg.payload.state.num);

#ifndef CONFIG_HGY_PLATFORM
    ctx->msg.payload.state.index -= 2;
#endif

    if (dev->kernel_ops->set_vring_base)
        ret = dev->kernel_ops->set_vring_base(dev->vdev, &ctx->msg.payload.state);
    else
        ret = -1;

    if (ret < 0) {
        pr_err("failed to set_vring_base!\n");
        return VHOST_MSG_RESULT_ERR;
    }

    return VHOST_MSG_RESULT_OK;
}

static int
vhost_user_set_vring_state(struct vhost_user_dev *dev, struct vhost_vring_file *file)
{
    int ret = 0;

    if (dev->kernel_ops->set_vring_kick)
        ret = dev->kernel_ops->set_vring_kick(dev->vdev, file);
    else
        ret = -1;

    if (ret < 0) {
        pr_err("failed to set_vring_kick!\n");
        return VHOST_MSG_RESULT_ERR;
    }

    return ret;
}

/*
 * when virtio is stopped, VMM will send us the GET_VRING_BASE message.
 */
static int
vhost_user_get_vring_base(struct vhost_user_dev *dev,
            struct vhu_msg_context *ctx)
{
    int ret = -1;

/**
 * 1. Reset owner may blocking at the vhost hab driver. But VHOST_USER_GET_VRING_BASE is a sync message.
 *    If we call vhost reset_owner at the vhost-user VHOST_USER_GET_VRING_BASE, the qcrosvm may blocking and cannot exit, thenVMM may wait forever.
 *
 * 2. vhost RESET_OWNER is per device but vhost-user VHOST_USER_GET_VRING_BASE is per queue, this is a gap.
 */
#if 0
    /* We have to stop the queue (virtio) if it is running. */
    if (dev->vdev->valid) {
        if (dev->kernel_ops->reset_owner)
            ret = dev->kernel_ops->reset_owner(dev->vdev);
        else
            ret = -1;

        if (ret < 0) {
            pr_err("failed to reset owner!\n");
            return VHOST_MSG_RESULT_ERR;
        }
        dev->vdev->valid = 0;
    }
#endif

#ifndef CONFIG_HGY_PLATFORM
    ctx->msg.payload.state.index -= 2;
#endif

    if (dev->kernel_ops->get_vring_base)
        ret = dev->kernel_ops->get_vring_base(dev->vdev, &ctx->msg.payload.state);
    else
        ret = -1;

    if (ret < 0) {
        pr_err("failed to get_vring_base!\n");
        return VHOST_MSG_RESULT_ERR;
    }

    pr_debug("vring base idx:%d file:%d\n",
        ctx->msg.payload.state.index, ctx->msg.payload.state.num);

    ctx->msg.size = sizeof(ctx->msg.payload.state);
    ctx->fd_num = 0;

    return VHOST_MSG_RESULT_REPLY;
}

static int
vhost_user_set_vring_kick(struct vhost_user_dev *dev,
            struct vhu_msg_context *ctx)
{
    struct vhost_vring_file file;

    file.index = ctx->msg.payload.u64 & VHOST_USER_VRING_IDX_MASK;
    if (ctx->msg.payload.u64 & VHOST_USER_VRING_NOFD_MASK)
        file.fd = VIRTIO_INVALID_EVENTFD;
    else
        file.fd = ctx->fds[0];
    pr_debug("vring kick idx:%d file:%d\n", file.index, file.fd);

    if (file.fd != VIRTIO_INVALID_EVENTFD) {
        store_fd(dev->kick_fd, file.fd);
    }

#ifndef CONFIG_HGY_PLATFORM
    file.index -= 2;
#endif

    if (vhost_user_set_vring_state(dev, &file) < 0) {
        pr_err("failed to enable the vring!! \n");
        return VHOST_MSG_RESULT_ERR;
    }

    return VHOST_MSG_RESULT_OK;
}

static int
vhost_user_set_vring_call(struct vhost_user_dev *dev,
            struct vhu_msg_context *ctx)
{
    struct vhost_vring_file file;
    int ret = -1;

    file.index = ctx->msg.payload.u64 & VHOST_USER_VRING_IDX_MASK;

    /**
     * This solution is intended to support poll mode.
     * However, HAB currently does not support this approach.
     */
    if (ctx->msg.payload.u64 & VHOST_USER_VRING_NOFD_MASK)
        file.fd = VIRTIO_INVALID_EVENTFD;
    else
        file.fd = ctx->fds[0];
    pr_debug("vring call idx:%d file:%d\n", file.index, file.fd);

    if (file.fd != VIRTIO_INVALID_EVENTFD) {
        store_fd(dev->call_fd, file.fd);
    }

#ifndef CONFIG_HGY_PLATFORM
    file.index -= 2;
#endif

    if (dev->kernel_ops->set_vring_call)
        ret = dev->kernel_ops->set_vring_call(dev->vdev, &file);
    else
        ret = -1;

    if (ret < 0) {
        pr_err("failed to set_vring_call!\n");
        return VHOST_MSG_RESULT_ERR;
    }

    return VHOST_MSG_RESULT_OK;
}

static int
vhost_user_set_vring_err(struct vhost_user_dev *dev,
            struct vhu_msg_context *ctx)
{
    struct vhost_vring_file file;
    int ret = -1;

    file.index = ctx->msg.payload.u64 & VHOST_USER_VRING_IDX_MASK;
    if (ctx->msg.payload.u64 & VHOST_USER_VRING_NOFD_MASK)
        file.fd = VIRTIO_INVALID_EVENTFD;
    else
        file.fd = ctx->fds[0];
    pr_debug("vring err idx:%d file:%d\n", file.index, file.fd);

#ifndef CONFIG_HGY_PLATFORM
    file.index -= 2;
#endif

    if (dev->kernel_ops->set_vring_call)
        ret = dev->kernel_ops->set_vring_err(dev->vdev, &file);
    else
        ret = -1;

    if (ret < 0) {
        pr_err("failed to set_vring_err!\n");
        return VHOST_MSG_RESULT_ERR;
    }

    return VHOST_MSG_RESULT_OK;
}

/*
 * The protocol features that we support are requested.
 */
static int
vhost_user_get_protocol_features(struct vhost_user_dev *dev,
            struct vhu_msg_context *ctx)
{
    uint64_t features = 0;

#ifdef CONFIG_HGY_PLATFORM
    features = (1 << VHOST_USER_PROTOCOL_F_RESET_DEVICE);
#endif
    features |= (1 << VHOST_USER_PROTOCOL_F_REPLY_ACK);

    ctx->msg.payload.u64 |= features;
    ctx->msg.size = sizeof(ctx->msg.payload.u64);
    ctx->fd_num = 0;
    pr_debug("get protocol features %lx \n", ctx->msg.payload.u64);

    return VHOST_MSG_RESULT_REPLY;
}

static int
vhost_user_set_protocol_features(struct vhost_user_dev *dev,
            struct vhu_msg_context *ctx)
{
    uint64_t features = ctx->msg.payload.u64;

    pr_debug("set protocol features: 0x%lx\n", features);
    return VHOST_MSG_RESULT_OK;
}

static int
vhost_user_set_vring_enable(struct vhost_user_dev *dev,
        struct vhu_msg_context *ctx)
{
    /* we do not maintain the enbale and disable status for vring, just ignore this.
    * the vring is started and enabled when VHOST_USER_SET_VRING_KICK is processed.
    * the vring is stopped and disabled when VHOST_USER_GET_VRING_BASE is processed.
    * If there is more complicated case, we will add support.
    */
    return VHOST_MSG_RESULT_OK;
}

#define VHOST_MESSAGE_HANDLER(id, handler, accepts_fd) \
    [id] = { #id, handler, accepts_fd },

#define VHOST_MESSAGE_HANDLERS \
VHOST_MESSAGE_HANDLER(VHOST_USER_NONE, NULL, false) \
VHOST_MESSAGE_HANDLER(VHOST_USER_GET_FEATURES, vhost_user_get_features, false) \
VHOST_MESSAGE_HANDLER(VHOST_USER_SET_FEATURES, vhost_user_set_features, false) \
VHOST_MESSAGE_HANDLER(VHOST_USER_SET_OWNER, vhost_user_set_owner, false) \
VHOST_MESSAGE_HANDLER(VHOST_USER_RESET_OWNER, vhost_user_reset_owner, false) \
VHOST_MESSAGE_HANDLER(VHOST_USER_SET_MEM_TABLE, vhost_user_set_mem_table, true) \
VHOST_MESSAGE_HANDLER(VHOST_USER_SET_LOG_BASE, NULL, true) \
VHOST_MESSAGE_HANDLER(VHOST_USER_SET_LOG_FD, NULL, true) \
VHOST_MESSAGE_HANDLER(VHOST_USER_SET_VRING_NUM, vhost_user_set_vring_num, false) \
VHOST_MESSAGE_HANDLER(VHOST_USER_SET_VRING_ADDR, vhost_user_set_vring_addr, false) \
VHOST_MESSAGE_HANDLER(VHOST_USER_SET_VRING_BASE, vhost_user_set_vring_base, false) \
VHOST_MESSAGE_HANDLER(VHOST_USER_GET_VRING_BASE, vhost_user_get_vring_base, false) \
VHOST_MESSAGE_HANDLER(VHOST_USER_SET_VRING_KICK, vhost_user_set_vring_kick, true) \
VHOST_MESSAGE_HANDLER(VHOST_USER_SET_VRING_CALL, vhost_user_set_vring_call, true) \
VHOST_MESSAGE_HANDLER(VHOST_USER_SET_VRING_ERR, vhost_user_set_vring_err, true) \
VHOST_MESSAGE_HANDLER(VHOST_USER_GET_PROTOCOL_FEATURES, vhost_user_get_protocol_features, false) \
VHOST_MESSAGE_HANDLER(VHOST_USER_SET_PROTOCOL_FEATURES, vhost_user_set_protocol_features, false) \
VHOST_MESSAGE_HANDLER(VHOST_USER_GET_QUEUE_NUM, NULL, false) \
VHOST_MESSAGE_HANDLER(VHOST_USER_SET_VRING_ENABLE, vhost_user_set_vring_enable, false) \
VHOST_MESSAGE_HANDLER(VHOST_USER_SEND_RARP, NULL, false) \
VHOST_MESSAGE_HANDLER(VHOST_USER_NET_SET_MTU, NULL, false) \
VHOST_MESSAGE_HANDLER(VHOST_USER_SET_BACKEND_REQ_FD, NULL, true) \
VHOST_MESSAGE_HANDLER(VHOST_USER_IOTLB_MSG, NULL, false) \
VHOST_MESSAGE_HANDLER(VHOST_USER_SET_VRING_ENDIAN, NULL, false) \
VHOST_MESSAGE_HANDLER(VHOST_USER_GET_CONFIG, vhost_user_get_config, false) \
VHOST_MESSAGE_HANDLER(VHOST_USER_SET_CONFIG, vhost_user_set_config, false) \
VHOST_MESSAGE_HANDLER(VHOST_USER_CRYPTO_CREATE_SESS, NULL, false) \
VHOST_MESSAGE_HANDLER(VHOST_USER_CRYPTO_CLOSE_SESS, NULL, false) \
VHOST_MESSAGE_HANDLER(VHOST_USER_POSTCOPY_ADVISE, NULL, false) \
VHOST_MESSAGE_HANDLER(VHOST_USER_POSTCOPY_LISTEN, NULL, false) \
VHOST_MESSAGE_HANDLER(VHOST_USER_POSTCOPY_END, NULL, false) \
VHOST_MESSAGE_HANDLER(VHOST_USER_GET_INFLIGHT_FD, NULL, false) \
VHOST_MESSAGE_HANDLER(VHOST_USER_SET_INFLIGHT_FD, NULL, true) \
VHOST_MESSAGE_HANDLER(VHOST_USER_GPU_SET_SOCKET, NULL, true) \
VHOST_MESSAGE_HANDLER(VHOST_USER_RESET_DEVICE, NULL, false) \
VHOST_MESSAGE_HANDLER(VHOST_USER_VRING_KICK, NULL, false) \
VHOST_MESSAGE_HANDLER(VHOST_USER_GET_MAX_MEM_SLOTS, NULL, false) \
VHOST_MESSAGE_HANDLER(VHOST_USER_ADD_MEM_REG, NULL, true) \
VHOST_MESSAGE_HANDLER(VHOST_USER_REM_MEM_REG, NULL, false) \
VHOST_MESSAGE_HANDLER(VHOST_USER_SET_STATUS, NULL, false) \
VHOST_MESSAGE_HANDLER(VHOST_USER_GET_STATUS, NULL, false)


static vhost_message_handler_t vhost_message_handlers[] = {
    VHOST_MESSAGE_HANDLERS
};
#undef VHOST_MESSAGE_HANDLER

static int
send_vhost_reply(struct vhost_user_dev *dev, int sockfd, struct vhu_msg_context *ctx)
{
    if (!ctx)
        return 0;

    ctx->msg.flags &= ~VHOST_USER_VERSION_MASK;
    ctx->msg.flags &= ~VHOST_USER_NEED_REPLY;
    ctx->msg.flags |= VHOST_USER_VERSION;
    ctx->msg.flags |= VHOST_USER_REPLY_MASK;

    return send_fd_message(sockfd, (char *)&ctx->msg,
        VHOST_USER_HDR_SIZE + ctx->msg.size, ctx->fds, ctx->fd_num);
}

static void
close_msg_fds(struct vhu_msg_context *ctx)
{
    int i;

    for (i = 0; i < ctx->fd_num; i++) {
        int fd = ctx->fds[i];

        if (fd == -1)
            continue;

        ctx->fds[i] = -1;
        close(fd);
    }
}

static int
read_vhost_message(struct vhost_user_dev *dev, int sockfd, struct  vhu_msg_context *ctx)
{
    int ret;

    ret = read_fd_message(sockfd, (char *)&ctx->msg, VHOST_USER_HDR_SIZE,
        ctx->fds, VHOST_MEMORY_MAX_NREGIONS, &ctx->fd_num);
    if (ret <= 0)
        goto out;

    if (ret != VHOST_USER_HDR_SIZE) {
        pr_err("Unexpected header size read\n");
        ret = -1;
        goto out;
    }

    if ((ctx->msg.flags & VHOST_USER_VERSION_MASK) != VHOST_USER_VERSION) {
        pr_err("Unmatched version, wish %d, got %d \n", VHOST_USER_VERSION,
                ctx->msg.flags & VHOST_USER_VERSION_MASK);
        ret = -1;
        goto out;
    }
    if (ctx->msg.size) {
        if (ctx->msg.size > sizeof(ctx->msg.payload)) {
            pr_err("invalid msg size: %d\n",
                ctx->msg.size);
            ret = -1;
            goto out;
        }
        ret = read(sockfd, &ctx->msg.payload, ctx->msg.size);
        if (ret <= 0)
            goto out;
        if (ret != (int)ctx->msg.size) {
            pr_err("read control message failed\n");
            ret = -1;
            goto out;
        }
    }

out:
    if (ret <= 0)
        close_msg_fds(ctx);

    return ret;
}

static int
vhost_user_msg_handler(struct vhost_user_dev *dev, uint32_t fd)
{
    struct vhu_msg_context ctx;
    vhost_message_handler_t *msg_handler;
    int msg_result = VHOST_MSG_RESULT_OK;
    int ret;
    bool handled;
    uint32_t request;

    if (dev == NULL || fd == 0)
        return -1;

    ctx.msg.request.frontend = VHOST_USER_NONE;
    ret = read_vhost_message(dev, fd, &ctx);
    if (ret <= 0) {
        pr_info("vhost peer closed\n");
        return -1;
    }

    request = ctx.msg.request.frontend;
    if (request > VHOST_USER_NONE && request < VHOST_USER_MAX)
        msg_handler = &vhost_message_handlers[request];
    else
        msg_handler = NULL;

    if (msg_handler != NULL && msg_handler->description != NULL) {
        pr_debug("read message %s\n",
            msg_handler->description);
    }

    handled = false;

    if (msg_handler == NULL || msg_handler->callback == NULL)
        goto err;

    msg_result = msg_handler->callback(dev, &ctx);


    switch (msg_result) {
    case VHOST_MSG_RESULT_ERR:
        pr_err("process failed.\n");
        handled = true;
        break;
    case VHOST_MSG_RESULT_OK:
        pr_debug("process succeeded.\n");
        handled = true;
        break;
    case VHOST_MSG_RESULT_REPLY:
        pr_debug("processing succeeded and needs reply.\n");
        send_vhost_reply(dev, fd, &ctx);
        handled = true;
        break;
    default:
        break;
    }
err:
    /* If message was not handled at this stage, treat it as an error */
    if (!handled) {
        pr_err("vhost message (req: %d) was not handled.\n",
            request);
        close_msg_fds(&ctx);
        msg_result = VHOST_MSG_RESULT_ERR;
    }

    if (msg_result == VHOST_MSG_RESULT_ERR) {
        pr_err("vhost message handling failed.\n");
        return -1;
    }

    return 0;
}

int
vhost_user_init_device(struct vhost_user_dev *dev, char *dev_path)
{
    struct virtio_user_dev *vdev;
    int ret;

    if (!dev) {
        pr_err("please provide structure dev\n");
        return -1;
    }

    vdev = malloc(sizeof(struct virtio_user_dev));
    if (!vdev) {
        pr_err("failed to allocate memory for virtio_user_dev\n");
        return -1;
    }

    snprintf(vdev->path, PATH_MAX, "%s", dev_path);

    dev->vdev = vdev;
    dev->kernel_ops = &virtio_ops_kernel;

    if (dev->kernel_ops->setup)
        ret = dev->kernel_ops->setup(dev->vdev);
    else
        ret = -1;

    if (ret < 0) {
        pr_err("failed to setup vhost kernel!\n");
        goto err;
    }

    memset(dev->kick_fd, -1, sizeof(int) * MAX_FD_NR);
    memset(dev->call_fd, -1, sizeof(int) * MAX_FD_NR);

    return 0;

err:
    free(vdev);
    return -1;
}

static void
free_mem_region(struct vhost_user_dev *dev)
{
    uint32_t i;
    struct vhost_user_mem_region *reg;

    if (!dev || !dev->mem)
        return;

    for (i = 0; i < dev->mem->nregions; i++) {
        reg = &dev->mem->regions[i];
        if (reg->host_user_addr) {
            munmap(reg->mmap_addr, reg->mmap_size);
            close(reg->fd);
        }
    }
}

void
vhost_user_deinit_device(struct vhost_user_dev *dev)
{
    uint32_t i;
    int ret;

    if (dev->vsocket.socket_fd)
        close(dev->vsocket.socket_fd);

    if (dev->vdev->valid) {
        pr_debug("start to send VHOST_RESET_OWNER cmd\n");
        vhost_user_qti_state = VHOST_USER_QTI_RESETING_VHOST_DEV;

        if (dev->kernel_ops->reset_owner)
            ret = dev->kernel_ops->reset_owner(dev->vdev);
        else
            ret = -1;

        if (ret < 0) {
            pr_err("failed to reset owner\n");
        } else {
            dev->vdev->valid = 0;
            pr_info("success reset owner\n");
        }
    }

    if (dev->kernel_ops->destroy)
        ret = dev->kernel_ops->destroy(dev->vdev);
    else
        ret = -1;

    if (ret < 0) {
        pr_err("failed to destroy vhost kernel!\n");
    }

    free_fds(dev->call_fd);
    free_fds(dev->kick_fd);

    if (dev->mem) {
        free_mem_region(dev);
        free(dev->mem);
        dev->mem = NULL;
    }

    free(dev->vdev);
    pr_debug("deinit device\n");
}

int
vhost_user_wait_for_connect(struct vhost_user_dev *dev, char *socket_path)
{
    if (create_unix_socket(&dev->vsocket, socket_path) < 0) {
        pr_err("failed to create socket\n");
        return -1;
    }
    if (vhost_user_start_server(&dev->vsocket) < 0) {
        pr_err("failed to setup connection \n");
        return -1;
    }
    return 0;
}

int
vhost_user_start_loop(struct vhost_user_dev *dev)
{
    int ret;

    do {
        ret = vhost_user_msg_handler(dev, dev->vsocket.socket_fd);
        if (ret < 0)
            break;
    } while(1);

    return ret;
}
