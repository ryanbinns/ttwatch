#ifndef __TTOPS_H__
#define __TTOPS_H__

#include "bbatt.h"

#define EXPECTED_MAKER "TomTom Fitness"
#define OLDEST_TESTED_FIRMWARE "1.8.34"
#define NEWEST_TESTED_FIRMWARE "1.8.46"
#define IS_TESTED_MODEL(model) (strcmp((model),"1001")==0 || strcmp((model),"1002")==0 || strcmp((model),"1003")==0 || strcmp((model),"1004")==0)

#define H_PASSCODE 0x0032
#define H_MAGIC 0x0035
#define H_CMD_STATUS 0x0025
#define H_LENGTH 0x0028
#define H_TRANSFER 0x002b
#define H_CHECK 0x002e

static inline int
EXPECT_BYTES(int fd, uint8_t *buf)
{
    uint16_t handle;
    int length = att_read_not(fd, &handle, buf);
    if (length < 0)
        return length;
    else if (handle != H_TRANSFER)
        return -EBADMSG;
    return (int)length;
}

static inline int
EXPECT_LENGTH(int fd)
{
    uint8_t buf[BT_ATT_DEFAULT_LE_MTU];
    uint16_t handle;
    int length = att_read_not(fd, &handle, buf);
    if (length < 0)
        return length;
    else if ((handle != H_LENGTH) || (length != 4))
        return -EBADMSG;
    return btohl(*((uint32_t*)buf));
}

static inline int
EXPECT_uint32(int fd, uint16_t handle, uint32_t val)
{
    uint8_t buf[BT_ATT_DEFAULT_LE_MTU];
    uint16_t h;
    int length = att_read_not(fd, &h, buf);
    if (length < 0)
        return length;
    else if ((h != handle) || (length != 4) || (btohl(*((uint32_t*)buf))!=val))
        return -EBADMSG;
    return 0;
}

static inline int
EXPECT_uint8(int fd, uint16_t handle, uint8_t val)
{
    uint8_t buf[BT_ATT_DEFAULT_LE_MTU];
    uint16_t h;
    int length = att_read_not(fd, &h, buf);
    if (length < 0)
        return length;
    else if ((h != handle) || (length != 1) || (*buf!=val))
        return -EBADMSG;
    return 0;
}

void hexlify(FILE *where, const uint8_t *buf, size_t len, bool newl);

int tt_authorize(int fd, uint32_t code, bool new_code);
int tt_read_file(int fd, uint32_t fileno, int debug, uint8_t **buf);
int tt_write_file(int fd, uint32_t fileno, int debug, const uint8_t *buf, uint32_t length, uint32_t write_delay);
int tt_delete_file(int fd, uint32_t fileno);
int tt_list_sub_files(int fd, uint32_t fileno, uint16_t **outlist);

#endif /* #ifndef __TTOPS_H__ */
