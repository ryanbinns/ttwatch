/**
 *
 * Very simple "bit-banging" library for the following ATT packet types:
 *   att_read: send BT_ATT_OP_READ_REQ, await BT_ATT_OP_READ_RSP)
 *   att_write and att_wrreq: send BT_ADD_OP_WRITE_CMD,
 *                         or send BT_ADD_OP_WRITE_REQ and await BT_ADD_OP_WRITE_RSP
 *   att_read_not: await BT_ATT_OP_HANDLE_VAL_NOT)
 */


#include <string.h>
#include <sys/socket.h>
#include "bbatt.h"

int
att_read(int fd, uint16_t handle, void *buf)
{
    int result;

    struct { uint8_t opcode; uint16_t handle; } __attribute__((packed)) pkt = { BT_ATT_OP_READ_REQ, handle };
    result = send(fd, &pkt, sizeof(pkt), 0);
    if (result<0)
        return result;

    struct { uint8_t opcode; uint8_t buf[BT_ATT_DEFAULT_LE_MTU]; } __attribute__((packed)) rpkt = {0};
    result = recv(fd, &rpkt, sizeof rpkt, 0);
    if (result<0)
        return result;
    else if (rpkt.opcode != BT_ATT_OP_READ_RSP)
        return -2;
    else {
        int length = result-1;
        memcpy(buf, rpkt.buf, length);
        return length;
    }
}

int
att_write(int fd, uint16_t handle, const void *buf, int length)
{
    struct { uint8_t opcode; uint16_t handle; uint8_t buf[length]; } __attribute__((packed)) pkt;
    pkt.opcode = BT_ATT_OP_WRITE_CMD;
    pkt.handle = handle;

    if (sizeof pkt > BT_ATT_DEFAULT_LE_MTU)
        return -1;
    memcpy(pkt.buf, buf, length);

    int result = send(fd, &pkt, sizeof(pkt), 0);
    if (result<0)
        return result;

    return length;
}

int
att_wrreq(int fd, uint16_t handle, const void *buf, int length)
{
    struct { uint8_t opcode; uint16_t handle; uint8_t buf[length]; } __attribute__((packed)) pkt;
    pkt.opcode = BT_ATT_OP_WRITE_REQ;
    pkt.handle = handle;

    if (sizeof pkt > BT_ATT_DEFAULT_LE_MTU)
        return -1;
    memcpy(pkt.buf, buf, length);

    int result = send(fd, &pkt, sizeof(pkt), 0);
    if (result<0)
        return result;

    uint8_t conf = 0;
    result = recv(fd, &conf, 1, 0);
    if (result < 0)
        return result;
    else if (conf != BT_ATT_OP_WRITE_RSP) {
        return -2;
    }

    return length;
}

int
att_read_not(int fd, uint16_t *handle, void *buf)
{
    struct { uint8_t opcode; uint16_t handle; uint8_t buf[BT_ATT_DEFAULT_LE_MTU]; } __attribute__((packed)) rpkt;
    int result = recv(fd, &rpkt, sizeof rpkt, 0);

    if (result<0)
        return result;
    else if (rpkt.opcode != BT_ATT_OP_HANDLE_VAL_NOT)
        return -2;
    else {
        int length = result-3;
        *handle = rpkt.handle;
        memcpy(buf, rpkt.buf, length);
        return length;
    }
}
