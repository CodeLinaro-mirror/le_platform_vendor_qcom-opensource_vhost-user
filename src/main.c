/* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include "vhost_user.h"

typedef struct VhostUserQtiDev {
    int dev_fd;
    int sock_fd;
    char *socket_path;
    struct vhost_user_dev dev;
    int max_queues;
} VhostUserQtiDev;

static void print_usage(void)
{
    pr_err("Usage:\n"
           "\tvhost-user-qti -s socket_path -d vhost_dev_path -q queue_number(0~65536)\n");
}

int main(int argc, char *argv[])
{
    int ret = 0;
    int c;
    char *dev_path = NULL;
    VhostUserQtiDev dev = { 0 };

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

    do {
        pr_info("vhost-user-qti start init\n");

        if (vhost_user_init_device(&dev.dev, dev_path) < 0) {
            pr_err("failed to init device\n");
            continue;
        }

        if (vhost_user_wait_for_connect(&dev.dev, dev.socket_path) < 0) {
            vhost_user_deinit_device(&dev.dev);
            pr_err("failed to wait for connect\n");
            continue;
        }

        ret = vhost_user_start_loop(&dev.dev);

        vhost_user_deinit_device(&dev.dev);
        pr_info("vhost-user-qti deinit done\n");

    } while(1);

    pr_info("vhost-user-qti exit\n");
    return 0;
}
