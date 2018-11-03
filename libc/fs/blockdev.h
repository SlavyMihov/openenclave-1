// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#ifndef _FS_BLOCKDEV_H
#define _FS_BLOCKDEV_H

#include <stdint.h>

typedef struct _fs_block_dev fs_block_dev_t;

struct _fs_block_dev
{
    int (*get)(fs_block_dev_t* dev, uint32_t blkno, void* data);

    int (*put)(fs_block_dev_t* dev, uint32_t blkno, const void* data);

    int (*add_ref)(fs_block_dev_t* dev);

    int (*release)(fs_block_dev_t* dev);
};

int oe_open_host_block_dev(const char* device_name, fs_block_dev_t** block_dev);

int oe_open_ram_block_dev(size_t size, fs_block_dev_t** block_dev);

int fs_block_dev_read(
    fs_block_dev_t* dev,
    size_t blkno,
    void* data,
    size_t size);

int fs_block_dev_write(
    fs_block_dev_t* dev,
    size_t blkno,
    const void* data,
    size_t size);

#endif /* _FS_BLOCKDEV_H */
