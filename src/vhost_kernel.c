/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2016 Intel Corporation
 */

/* Changes from Qualcomm Technologies, Inc. are provided under the following license:
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>

#include <linux/vhost.h>
#include "vhost_user.h"

/* vhost kernel ioctls */
#define VHOST_VIRTIO 0xAF
#define VHOST_GET_FEATURES _IOR(VHOST_VIRTIO, 0x00, __u64)
#define VHOST_SET_FEATURES _IOW(VHOST_VIRTIO, 0x00, __u64)
#define VHOST_SET_OWNER _IO(VHOST_VIRTIO, 0x01)
#define VHOST_RESET_OWNER _IO(VHOST_VIRTIO, 0x02)
#define VHOST_SET_MEM_TABLE _IOW(VHOST_VIRTIO, 0x03, struct vhost_memory)
#define VHOST_SET_LOG_BASE _IOW(VHOST_VIRTIO, 0x04, __u64)
#define VHOST_SET_LOG_FD _IOW(VHOST_VIRTIO, 0x07, int)
#define VHOST_SET_VRING_NUM _IOW(VHOST_VIRTIO, 0x10, struct vhost_vring_state)
#define VHOST_SET_VRING_ADDR _IOW(VHOST_VIRTIO, 0x11, struct vhost_vring_addr)
#define VHOST_SET_VRING_BASE _IOW(VHOST_VIRTIO, 0x12, struct vhost_vring_state)
#define VHOST_GET_VRING_BASE _IOWR(VHOST_VIRTIO, 0x12, struct vhost_vring_state)
#define VHOST_SET_VRING_KICK _IOW(VHOST_VIRTIO, 0x20, struct vhost_vring_file)
#define VHOST_SET_VRING_CALL _IOW(VHOST_VIRTIO, 0x21, struct vhost_vring_file)
#define VHOST_SET_VRING_ERR _IOW(VHOST_VIRTIO, 0x22, struct vhost_vring_file)

static uint64_t max_regions = 64;

static void
get_vhost_kernel_max_regions(void)
{
	int fd;
	char buf[20] = {'\0'};

	fd = open("/sys/module/vhost/parameters/max_mem_regions", O_RDONLY);
	if (fd < 0)
		return;

	if (read(fd, buf, sizeof(buf) - 1) > 0)
		max_regions = strtoull(buf, NULL, 10);

	close(fd);
}

static int
vhost_kernel_ioctl(int fd, uint64_t request, void *arg)
{
	int ret;

	ret = ioctl(fd, request, arg);
	if (ret) {
		pr_err("Vhost-kernel ioctl \"PRI%lu\" failed (%s)",
				request, strerror(errno));
		return -1;
	}

	return 0;
}

static int
vhost_kernel_set_owner(struct virtio_user_dev *dev)
{
	int ret;

	ret = vhost_kernel_ioctl(dev->vhostfd, VHOST_SET_OWNER, NULL);
	if (ret < 0)
		return ret;

	return 0;
}

static int
vhost_kernel_reset_owner(struct virtio_user_dev *dev)
{
	int ret;

	ret = vhost_kernel_ioctl(dev->vhostfd, VHOST_RESET_OWNER, NULL);
	if (ret < 0)
		return ret;

	return 0;
}

static int
vhost_kernel_get_features(struct virtio_user_dev *dev, uint64_t *features)
{
	int ret;

	ret = vhost_kernel_ioctl(dev->vhostfd, VHOST_GET_FEATURES, features);
	if (ret < 0) {
		pr_err("Failed to get features");
		return -1;
	}

	return 0;
}

static int
vhost_kernel_set_features(struct virtio_user_dev *dev, uint64_t features)
{
	int ret;

	ret = vhost_kernel_ioctl(dev->vhostfd, VHOST_SET_FEATURES, &features);
	if (ret < 0)
		return ret;

	return 0;
}

/* By default, vhost kernel module allows 64 regions, but DPDK may
 * have much more memory regions. Below function will treat each
 * contiguous memory space reserved by DPDK as one region.
 */
static int
vhost_kernel_set_memory_table(struct virtio_user_dev *dev, struct vhost_user_mem *mem)
{
	uint32_t i;
	struct vhost_memory *vm;
	struct vhost_user_mem_region *reg;
	int ret;

	if (mem->nregions > max_regions) {
		pr_err("Too many memory regions %u, max %u",
				mem->nregions, max_regions);
		goto err;
	}

	vm = malloc(sizeof(struct vhost_memory) +
			mem->nregions *
			sizeof(struct vhost_memory_region));
	if (!vm)
		goto err;

	vm->nregions = mem->nregions;
	vm->padding = 0;

	for (i = 0; i < mem->nregions; i++) {
		reg = &mem->regions[i];

		vm->regions[i].guest_phys_addr = reg->guest_phys_addr;
		vm->regions[i].memory_size = reg->size;
		vm->regions[i].userspace_addr = reg->host_user_addr;
		vm->regions[i].flags_padding = 0;
	}

	ret = vhost_kernel_ioctl(dev->vhostfd, VHOST_SET_MEM_TABLE, vm);
	if (ret < 0)
		goto err_free;

	free(vm);

	return 0;
err_free:
	free(vm);
err:
	pr_err("Failed to set memory table");
	return -1;
}

static int
vhost_kernel_set_vring(struct virtio_user_dev *dev, uint64_t req, struct vhost_vring_state *state)
{
	int ret;

	ret = vhost_kernel_ioctl(dev->vhostfd, req, state);
	if (ret < 0) {
		pr_err("Failed to set vring (request \"%lu\")", req);
		return -1;
	}

	return 0;
}

static int
vhost_kernel_set_vring_num(struct virtio_user_dev *dev, struct vhost_vring_state *state)
{
	return vhost_kernel_set_vring(dev, VHOST_SET_VRING_NUM, state);
}

static int
vhost_kernel_set_vring_base(struct virtio_user_dev *dev, struct vhost_vring_state *state)
{
	return vhost_kernel_set_vring(dev, VHOST_SET_VRING_BASE, state);
}

static int
vhost_kernel_get_vring_base(struct virtio_user_dev *dev, struct vhost_vring_state *state)
{
	return vhost_kernel_set_vring(dev, VHOST_GET_VRING_BASE, state);
}

static int
vhost_kernel_set_vring_file(struct virtio_user_dev *dev, uint64_t req,
		struct vhost_vring_file *file)
{
	int ret;

	ret = vhost_kernel_ioctl(dev->vhostfd, req, file);
	if (ret < 0) {
		pr_err("Failed to set vring file (request \"%lu\")", req);
		return -1;
	}

	return 0;
}

static int
vhost_kernel_set_vring_kick(struct virtio_user_dev *dev, struct vhost_vring_file *file)
{
	return vhost_kernel_set_vring_file(dev, VHOST_SET_VRING_KICK, file);
}

static int
vhost_kernel_set_vring_call(struct virtio_user_dev *dev, struct vhost_vring_file *file)
{
	return vhost_kernel_set_vring_file(dev, VHOST_SET_VRING_CALL, file);
}

static int
vhost_kernel_set_vring_err(struct virtio_user_dev *dev, struct vhost_vring_file *file)
{
	return vhost_kernel_set_vring_file(dev, VHOST_SET_VRING_ERR, file);
}

static int
vhost_kernel_set_vring_addr(struct virtio_user_dev *dev, struct vhost_vring_addr *addr)
{
	int ret;

	ret = vhost_kernel_ioctl(dev->vhostfd, VHOST_SET_VRING_ADDR, addr);
	if (ret < 0) {
		pr_err("Failed to set vring address");
		return -1;
	}

	return 0;
}

static int
vhost_kernel_get_status(struct virtio_user_dev *dev, uint8_t *status)
{
	return -ENOTSUP;
}

static int
vhost_kernel_set_status(struct virtio_user_dev *dev, uint8_t status)
{
	return -ENOTSUP;
}

/**
 * Set up environment to talk with a vhost kernel backend.
 *
 * @return
 *   - (-1) if fail to set up;
 *   - (>=0) if successful.
 */
static int
vhost_kernel_setup(struct virtio_user_dev *dev)
{
	get_vhost_kernel_max_regions();

	dev->vhostfd = open(dev->path, O_RDWR);
	if (dev->vhostfd < 0) {
		pr_err("fail to open %s, %s", dev->path, strerror(errno));
		return -1;
	}

	return 0;
}

static int
vhost_kernel_destroy(struct virtio_user_dev *dev)
{
	if (dev->vhostfd >= 0) {
		close(dev->vhostfd);
		dev->vhostfd = -1;
	}

	return 0;
}

/* We reserve those function because of hab design */
struct vhost_kernel_ops virtio_ops_kernel = {
	.setup = vhost_kernel_setup,
	.destroy = vhost_kernel_destroy,
	.set_owner = vhost_kernel_set_owner,
	.reset_owner = vhost_kernel_reset_owner,
	.get_features = vhost_kernel_get_features,
	.set_features = vhost_kernel_set_features,
	.set_memory_table = vhost_kernel_set_memory_table,
	.set_vring_num = vhost_kernel_set_vring_num,
	.set_vring_base = vhost_kernel_set_vring_base,
	.get_vring_base = vhost_kernel_get_vring_base,
	.set_vring_call = vhost_kernel_set_vring_call,
	.set_vring_kick = vhost_kernel_set_vring_kick,
	.set_vring_err = vhost_kernel_set_vring_err,
	.set_vring_addr = vhost_kernel_set_vring_addr,
	.get_status = vhost_kernel_get_status,
	.set_status = vhost_kernel_set_status,
	/* todo */
	.get_config = NULL,
	.set_config = NULL,
};
