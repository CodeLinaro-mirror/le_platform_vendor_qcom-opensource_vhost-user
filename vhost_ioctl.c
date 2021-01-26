/*
 * Copyright (c) 2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2013 Virtual Open Systems Sarl.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 *See the NOTICE file in the top-level directory.
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <stdarg.h>
#include <errno.h>
#include <linux/vhost.h>
#include <sys/ioctl.h>
#include "vhost_user_q.h"

static int vhost_kernel_call(int fd, unsigned long int request,
                             void *arg)
{
    int ret;

    DPRINT("vhost_kernel_call: fd %d, ioctl 0x%lx\n", fd, request);

    ret = ioctl(fd, request, arg);

    if (ret)
        EPRINT("vhost_kernel_call: fd %d, ioctl 0x%lx return %d, errno %d\n", fd, request, ret, errno);
    else
        DPRINT("vhost_kernel_call: fd %d, ioctl 0x%lx return %d\n", fd, request, ret);

    return ret;
}

int vhost_kernel_memslots_limit(int fd)
{
    int limit = 64;
    char *s;
    FILE *fp = fopen("/sys/module/vhost/parameters/max_mem_regions", "r");

    if (fp) {
        uint64_t val;
        if (fscanf(fp, "%lu", &val) == 1) {
            if (!((val == UINT64_MAX || !val) && errno)) {
                limit = val;
            }
        }
        fclose(fp);
    }

    return limit;
}

int vhost_kernel_set_log_base(int fd, uint64_t base)
{
    return vhost_kernel_call(fd, VHOST_SET_LOG_BASE, &base);
}

int vhost_kernel_set_mem_table(int fd,
                                      struct vhost_memory *mem)
{
    return vhost_kernel_call(fd, VHOST_SET_MEM_TABLE, mem);
}

int vhost_kernel_set_vring_addr(int fd,
                                       struct vhost_vring_addr *addr)
{
    return vhost_kernel_call(fd, VHOST_SET_VRING_ADDR, addr);
}

int vhost_kernel_set_vring_endian(int fd,
                                         struct vhost_vring_state *ring)
{
    return vhost_kernel_call(fd, VHOST_SET_VRING_ENDIAN, ring);
}

int vhost_kernel_set_vring_num(int fd,
                                      struct vhost_vring_state *ring)
{
    return vhost_kernel_call(fd, VHOST_SET_VRING_NUM, ring);
}

int vhost_kernel_set_vring_base(int fd,
                                       struct vhost_vring_state *ring)
{
    return vhost_kernel_call(fd, VHOST_SET_VRING_BASE, ring);
}

int vhost_kernel_get_vring_base(int fd,
                                       struct vhost_vring_state *ring)
{
    return vhost_kernel_call(fd, VHOST_GET_VRING_BASE, ring);
}

int vhost_kernel_set_vring_kick(int fd,
                                       struct vhost_vring_file *file)
{
    return vhost_kernel_call(fd, VHOST_SET_VRING_KICK, file);
}

int vhost_kernel_set_vring_call(int fd,
                                       struct vhost_vring_file *file)
{
    return vhost_kernel_call(fd, VHOST_SET_VRING_CALL, file);
}

int vhost_kernel_set_vring_err(int fd,
                                       struct vhost_vring_file *file)
{
    return vhost_kernel_call(fd, VHOST_SET_VRING_ERR, file);
}

int vhost_kernel_set_vring_busyloop_timeout(int fd,
                                                   struct vhost_vring_state *s)
{
    return vhost_kernel_call(fd, VHOST_SET_VRING_BUSYLOOP_TIMEOUT, s);
}

int vhost_kernel_set_features(int fd,
                                     uint64_t features)
{
    return vhost_kernel_call(fd, VHOST_SET_FEATURES, &features);
}

int vhost_kernel_get_features(int fd,
                                     uint64_t *features)
{
    return vhost_kernel_call(fd, VHOST_GET_FEATURES, features);
}

int vhost_kernel_set_owner(int fd)
{
    return vhost_kernel_call(fd, VHOST_SET_OWNER, NULL);
}

int vhost_kernel_reset_device(int fd)
{
    return vhost_kernel_call(fd, VHOST_RESET_OWNER, NULL);
}

int vhost_kernel_set_config(int fd, struct vhost_config *config)
{
    return vhost_kernel_call(fd, VHOST_SET_CONFIG, config);
}

