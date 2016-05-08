#ifndef __TTOPS_H__
#define __TTOPS_H__

#include "version.h"
#include "bbatt.h"

struct tt_handles { uint16_t ppcp, passcode, magic, cmd_status, length, transfer, check; };

struct ble_dev_info {
    uint16_t handle;
    const char *name;
    char buf[BT_ATT_DEFAULT_LE_MTU-2];
    int len;
};

typedef struct ttdev {
    int fd;
    int protocol_version;
    struct tt_handles *h;
    struct ble_dev_info *info; // stuff from UUID=180a (Device Information)

    struct version_tuple oldest_tested_firmware, newest_tested_firmware;
    const char **tested_models;
} TTDEV;

#include "util.h"

TTDEV *tt_device_init(int protocol_version, int fd);
bool tt_device_done(TTDEV *d);
struct ble_dev_info *tt_check_device_version(TTDEV *d, bool warning);
int tt_authorize(TTDEV *d, uint32_t code, bool new_code);
int tt_read_file(TTDEV *d, uint32_t fileno, int debug, uint8_t **buf);
int tt_write_file(TTDEV *d, uint32_t fileno, int debug, const uint8_t *buf, uint32_t length, uint32_t write_delay);
int tt_delete_file(TTDEV *d, uint32_t fileno);
int tt_list_sub_files(TTDEV *d, uint32_t fileno, uint16_t **outlist);
int tt_reboot(TTDEV *d);

static inline int
EXPECT_BYTES(TTDEV *d, uint8_t *buf)
{
    uint16_t handle;
    int length = att_read_not(d->fd, &handle, buf);
    if (length < 0)
        return length;
    else if (handle != d->h->transfer) {
        fprintf(stderr, "Expected 0x%04x <- BYTES, but got:\n   0x%04x <- ", d->h->transfer, handle);
        hexlify(stderr, buf, length, true);
        return -1;
    }
    return (int)length;
}

static inline int
EXPECT_LENGTH(TTDEV *d)
{
    union { uint8_t buf[BT_ATT_DEFAULT_LE_MTU]; uint32_t out; } r;
    uint16_t handle;
    int length = att_read_not(d->fd, &handle, r.buf);
    if (length < 0)
        return length;
    else if ((handle != d->h->length) || (length != 4)) {
        fprintf(stderr, "Expected 0x%04x <- (uint32_t)LENGTH, but got:\n  0x%04x <- ", d->h->length, handle);
        hexlify(stderr, r.buf, length, true);
        return -1;
    }
    return btohl(r.out);
}

static inline int
EXPECT_ANY_uint32(TTDEV *d, uint16_t handle, uint32_t *val)
{
    union { uint8_t buf[BT_ATT_DEFAULT_LE_MTU]; uint32_t out; } r;
    uint16_t h;
    int length = att_read_not(d->fd, &h, r.buf);
    if (length < 0)
        return length;
    else if ((h != handle) || (length != 4)) {
        fprintf(stderr, "Expected 0x%04x <- (uint32_t), but got:\n  0x%04x <- ", handle, h);
        hexlify(stderr, r.buf, length, true);
        return -1;
    }
    if (val)
        *val = btohl(r.out);
    return 0;
}

static inline int
EXPECT_uint32(TTDEV *d, uint16_t handle, uint32_t val)
{
    union { uint8_t buf[BT_ATT_DEFAULT_LE_MTU]; uint32_t out; } r;
    uint16_t h;
    int length = att_read_not(d->fd, &h, r.buf);
    if (length < 0)
        return length;
    else if ((h != handle) || (length != 4) || (btohl(r.out)!=val)) {
        fprintf(stderr, "Expected 0x%04x <- (uint32_t)0x%08x, but got:\n  0x%04x <- ", handle, val, h);
        hexlify(stderr, r.buf, length, true);
        return -1;
    }
    return 0;
}

static inline int
EXPECT_uint8(TTDEV *d, uint16_t handle, uint8_t val)
{
    uint8_t buf[BT_ATT_DEFAULT_LE_MTU];
    uint16_t h;
    int length = att_read_not(d->fd, &h, buf);
    if (length < 0)
        return length;
    else if ((h != handle) || (length != 1) || (*buf!=val)) {
        fprintf(stderr, "Expected 0x%04x <- (uint8_t)0x%02x, but got:\n  0x%02x <- ", handle, val, h);
        hexlify(stderr, buf, length, true);
        return -1;
    }
    return 0;
}

#endif /* #ifndef __TTOPS_H__ */
