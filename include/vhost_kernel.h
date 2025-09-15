/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2010-2016 Intel Corporation
 */

/* Changes from Qualcomm Technologies, Inc. are provided under the following license:
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#ifndef _VIRTIO_USER_VHOST_H
#define _VIRTIO_USER_VHOST_H

#include <stdint.h>
#include <linux/types.h>
#include <linux/ioctl.h>
#include <linux/vhost.h>

#ifndef VHOST_BACKEND_F_IOTLB_MSG_V2
#define VHOST_BACKEND_F_IOTLB_MSG_V2 1
#endif

#ifndef VHOST_BACKEND_F_IOTLB_BATCH
#define VHOST_BACKEND_F_IOTLB_BATCH 2
#endif

#define PATH_MAX        4096	/* # chars in a path name including nul */

struct virtio_user_dev {
	int vhostfd;
	char path[PATH_MAX];
	int valid;
};

struct vhost_kernel_ops {
	int (*setup)(struct virtio_user_dev *dev);
	int (*destroy)(struct virtio_user_dev *dev);
	int (*get_backend_features)(uint64_t *features);
	int (*set_owner)(struct virtio_user_dev *dev);
	int (*reset_owner)(struct virtio_user_dev *dev);
	int (*get_features)(struct virtio_user_dev *dev, uint64_t *features);
	int (*set_features)(struct virtio_user_dev *dev, uint64_t features);
	int (*set_memory_table)(struct virtio_user_dev *dev, struct vhost_user_mem *reg);
	int (*set_vring_num)(struct virtio_user_dev *dev, struct vhost_vring_state *state);
	int (*set_vring_base)(struct virtio_user_dev *dev, struct vhost_vring_state *state);
	int (*get_vring_base)(struct virtio_user_dev *dev, struct vhost_vring_state *state);
	int (*set_vring_call)(struct virtio_user_dev *dev, struct vhost_vring_file *file);
	int (*set_vring_kick)(struct virtio_user_dev *dev, struct vhost_vring_file *file);
	int (*set_vring_err)(struct virtio_user_dev *dev, struct vhost_vring_file *file);
	int (*set_vring_addr)(struct virtio_user_dev *dev, struct vhost_vring_addr *addr);
	int (*get_status)(struct virtio_user_dev *dev, uint8_t *status);
	int (*set_status)(struct virtio_user_dev *dev, uint8_t status);
	int (*get_config)(struct virtio_user_dev *dev, uint32_t offset, uint32_t size, char *payload);
	int (*set_config)(struct virtio_user_dev *dev, uint32_t offset, uint32_t size, uint32_t flag,
			char *payload);
};

extern struct vhost_kernel_ops virtio_ops_kernel;

#endif
