/*
 * Copyright (c) 2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2013 Virtual Open Systems Sarl.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 *See the NOTICE file in the top-level directory.
 */

#ifndef VHOST_USER_QTI_H
#define VHOST_USER_QTI_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/poll.h>
#include <linux/vhost.h>
#include <linux/virtio_ring.h>
#include <syslog.h>

#ifdef QEMU_DESKTOP
#include "contrib/libvhost-user/libvhost-user.h"
#include "../linux/hab_ioctl.h"
#else
#include "libvhost-user.h"
#include <linux/hab_ioctl.h>
#endif

#define LIBVHOST_USER_DEBUG 1

#define SYS_LOG

#define __FILENAME__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)

#ifdef SYS_LOG
#define EPRINT(fmt, ...)  \
        syslog(LOG_ERR, \
            "[%s:%s():%d] " fmt "\n", \
            __FILENAME__, __FUNCTION__, __LINE__, ##__VA_ARGS__)

#define IPRINT(fmt, ...)  \
        syslog(LOG_INFO, \
            "[%s:%s():%d] " fmt "\n", \
            __FILENAME__, __FUNCTION__, __LINE__, ##__VA_ARGS__)

#define DPRINT(fmt, ...)  \
    do {                                           \
        if (LIBVHOST_USER_DEBUG) {                 \
            syslog(LOG_INFO, \
                "[%s:%s():%d] " fmt "\n", \
                __FILENAME__, __FUNCTION__, __LINE__, ##__VA_ARGS__); \
        } else {                                    \
            syslog(LOG_DEBUG, \
                "[%s:%s():%d] " fmt "\n", \
                __FILENAME__, __FUNCTION__, __LINE__, ##__VA_ARGS__); \
        }                                           \
    } while (0)
#else
#define DPRINT(...)                             \
    do {                                        \
        if (LIBVHOST_USER_DEBUG) {              \
            fprintf(stderr, __VA_ARGS__);        \
        }                                       \
    } while (0)

#define EPRINT(...)                             \
    do {                                        \
        fprintf(stderr, __VA_ARGS__);        \
    } while (0)
#endif
/* vhost kernel ioctl wrappers */
int vhost_kernel_memslots_limit(int fd);
int vhost_kernel_set_log_base(int fd, uint64_t base);
int vhost_kernel_set_mem_table(int fd,
                                      struct vhost_memory *mem);
int vhost_kernel_set_vring_addr(int fd,
                                       struct vhost_vring_addr *addr);
int vhost_kernel_set_vring_endian(int fd,
                                         struct vhost_vring_state *ring);
int vhost_kernel_set_vring_num(int fd,
                                      struct vhost_vring_state *ring);
int vhost_kernel_set_vring_base(int fd,
                                       struct vhost_vring_state *ring);
int vhost_kernel_get_vring_base(int fd,
                                       struct vhost_vring_state *ring);
int vhost_kernel_set_vring_kick(int fd,
                                       struct vhost_vring_file *file);
int vhost_kernel_set_vring_call(int fd,
                                       struct vhost_vring_file *file);
int vhost_kernel_set_vring_err(int fd,
                                       struct vhost_vring_file *file);
int vhost_kernel_set_vring_busyloop_timeout(int fd,
                                                   struct vhost_vring_state *s);
int vhost_kernel_set_features(int fd,
                                     uint64_t features);
int vhost_kernel_get_features(int fd,
                                     uint64_t *features);
int vhost_kernel_set_owner(int fd);
int vhost_kernel_reset_device(int fd);
int vhost_kernel_set_config(int fd, struct vhost_config *config);

#endif /* VHOST_USER_QTI_H */
