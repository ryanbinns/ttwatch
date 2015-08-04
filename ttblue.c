/**
 *
 */

#define _BSD_SOURCE
#include <endian.h>

#include <errno.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdio.h>

#include <sys/socket.h>
#include <sys/types.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>
#include <bluetooth/l2cap.h>
#include "att-types.h"

/**
 * taken from bluez/tools/btgatt-client.c
 *
 */

#define ATT_CID 4
static int l2cap_le_att_connect(bdaddr_t *src, bdaddr_t *dst, uint8_t dst_type,
                                int sec, int verbose)
{
    int sock;
    struct sockaddr_l2 srcaddr, dstaddr;
    struct bt_security btsec;

    if (verbose) {
        char srcaddr_str[18], dstaddr_str[18];

        ba2str(src, srcaddr_str);
        ba2str(dst, dstaddr_str);

        printf("btgatt-client: Opening L2CAP LE connection on ATT "
                    "channel:\n\t src: %s\n\tdest: %s\n",
                    srcaddr_str, dstaddr_str);
    }

    sock = socket(PF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_L2CAP);
    if (sock < 0) {
        perror("Failed to create L2CAP socket");
        return -1;
    }

    /* Set up source address */
    memset(&srcaddr, 0, sizeof(srcaddr));
    srcaddr.l2_family = AF_BLUETOOTH;
    srcaddr.l2_cid = htobs(ATT_CID);
    srcaddr.l2_bdaddr_type = 0;
    bacpy(&srcaddr.l2_bdaddr, src);

    if (bind(sock, (struct sockaddr *)&srcaddr, sizeof(srcaddr)) < 0) {
        perror("Failed to bind L2CAP socket");
        close(sock);
        return -1;
    }

    /* Set the security level */
    memset(&btsec, 0, sizeof(btsec));
    btsec.level = sec;
    if (setsockopt(sock, SOL_BLUETOOTH, BT_SECURITY, &btsec,
                            sizeof(btsec)) != 0) {
        fprintf(stderr, "Failed to set L2CAP security level\n");
        close(sock);
        return -1;
    }

    /* Set up destination address */
    memset(&dstaddr, 0, sizeof(dstaddr));
    dstaddr.l2_family = AF_BLUETOOTH;
    dstaddr.l2_cid = htobs(ATT_CID);
    dstaddr.l2_bdaddr_type = dst_type;
    bacpy(&dstaddr.l2_bdaddr, dst);

    printf("Connecting to device...");
    fflush(stdout);

    if (connect(sock, (struct sockaddr *) &dstaddr, sizeof(dstaddr)) < 0) {
        perror(" Failed to connect");
        close(sock);
        return -1;
    }

    printf(" Done\n");

    return sock;
}

/* ATT protocol opcodes from bluez/src/shared/att-types.h */

#define BT_ATT_OP_ERROR_RSP			0x01
#define BT_ATT_OP_MTU_REQ			0x02
#define BT_ATT_OP_MTU_RSP			0x03
#define BT_ATT_OP_FIND_INFO_REQ			0x04
#define BT_ATT_OP_FIND_INFO_RSP			0x05
#define BT_ATT_OP_FIND_BY_TYPE_VAL_REQ		0x06
#define BT_ATT_OP_FIND_BY_TYPE_VAL_RSP		0x07
#define BT_ATT_OP_READ_BY_TYPE_REQ		0x08
#define BT_ATT_OP_READ_BY_TYPE_RSP		0x09
#define BT_ATT_OP_READ_REQ			0x0a
#define BT_ATT_OP_READ_RSP			0x0b
#define BT_ATT_OP_READ_BLOB_REQ			0x0c
#define BT_ATT_OP_READ_BLOB_RSP			0x0d
#define BT_ATT_OP_READ_MULT_REQ			0x0e
#define BT_ATT_OP_READ_MULT_RSP			0x0f
#define BT_ATT_OP_READ_BY_GRP_TYPE_REQ		0x10
#define BT_ATT_OP_READ_BY_GRP_TYPE_RSP		0x11
#define BT_ATT_OP_WRITE_REQ			0x12
#define BT_ATT_OP_WRITE_RSP			0x13
#define BT_ATT_OP_WRITE_CMD			0x52
#define BT_ATT_OP_SIGNED_WRITE_CMD		0xD2
#define BT_ATT_OP_PREP_WRITE_REQ		0x16
#define BT_ATT_OP_PREP_WRITE_RSP		0x17
#define BT_ATT_OP_EXEC_WRITE_REQ		0x18
#define BT_ATT_OP_EXEC_WRITE_RSP		0x19
#define BT_ATT_OP_HANDLE_VAL_NOT		0x1B
#define BT_ATT_OP_HANDLE_VAL_IND		0x1D
#define BT_ATT_OP_HANDLE_VAL_CONF		0x1E

/****************************************************************************/

/* manually assemble ATT packets */
ssize_t
att_send(int fd, uint8_t opcode, uint16_t handle, const uint8_t *buf, size_t length)
{
    struct {
        uint8_t opcode;
        uint16_t handle;
        uint8_t buf[length];
    }  __attribute__((packed)) pkt;

    pkt.opcode = opcode;
    pkt.handle = htobs(handle);
    memcpy(pkt.buf, buf, length);
    return send(fd, &pkt, sizeof(pkt), 0);
}

int
att_read(int fd, uint16_t handle, uint8_t *buf)
{
    uint8_t rbuf[BT_ATT_DEFAULT_LE_MTU];
    int result;

    result = att_send(fd, BT_ATT_OP_READ_REQ, handle, NULL, 0);
    if (result<0)
        return result;

    memset(rbuf, 0, sizeof rbuf);
    result = recv(fd, rbuf, sizeof(rbuf), 0);
    struct { uint8_t opcode; uint8_t buf[]; } __attribute__((packed)) *rpkt = (void *)rbuf;

    if (result<0)
        return result;
    else if (rpkt->opcode != BT_ATT_OP_READ_RSP)
        return -2;
    else {
        memcpy(buf, rpkt->buf, result-1);
    }
    return result-1;
}

int
att_write(int fd, uint16_t handle, uint8_t *buf, size_t length)
{
    int result;
    uint8_t conf;

    result = att_send(fd, BT_ATT_OP_WRITE_CMD, handle, buf, length);
    if (result<0)
        return result;
    return length;
}

int
att_wrreq(int fd, uint16_t handle, uint8_t *buf, size_t length)
{
    int result;
    uint8_t conf;

    result = att_send(fd, BT_ATT_OP_WRITE_REQ, handle, buf, length);
    if (result<0)
        return result;

    result = recv(fd, &conf, 1, 0);
    if (conf != BT_ATT_OP_WRITE_RSP)
        return -2;

    return length;
}

int
att_read_not(int fd, size_t *length, uint8_t *buf)
{
    uint8_t rbuf[BT_ATT_DEFAULT_LE_MTU];
    int result, handle;

    memset(rbuf, 0, sizeof rbuf);
    result = recv(fd, rbuf, sizeof(rbuf), 0);
    struct { uint8_t opcode; uint16_t handle; uint8_t buf[]; } __attribute__((packed)) *rpkt = (void *)rbuf;

    if (result<0)
        return result;
    else if (rpkt->opcode != BT_ATT_OP_HANDLE_VAL_NOT)
        return -2;
    else {
        *length = result-3;
        memcpy(buf, rpkt->buf, *length);
        return rpkt->handle;
    }
}

/****************************************************************************/

int main(int argc, const char **argv)
{
    int fd;

    uint8_t dst_type = BDADDR_LE_RANDOM;
    bdaddr_t src_addr, dst_addr;
    int sec = BT_SECURITY_MEDIUM;

    /* also from btgatt-client.c */
    str2ba(argv[1], &dst_addr);
    bacpy(&src_addr, BDADDR_ANY);
    fd = l2cap_le_att_connect(&src_addr, &dst_addr, dst_type, sec, true);
    if (fd < 0)
        return 1;

    struct timeval to = {.tv_sec=2, .tv_usec=0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &to, sizeof(to));
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &to, sizeof(to));

    uint8_t rbuf[BT_ATT_DEFAULT_LE_MTU];
    size_t length;
    int handle;

    length = att_read(fd, 0x0003, rbuf);
    printf("Device name: %.*s\n", (int)length, rbuf);
    att_write(fd, 0x0033, (uint8_t[]){0x01, 0}, 2);
    att_wrreq(fd, 0x0035, (uint8_t[]){0x01, 0x13, 0, 0, 0x01, 0x12, 0, 0}, 8);
    att_write(fd, 0x0026, (uint8_t[]){0x01, 0}, 2);
    uint32_t code = htobl( atoi( argv[2] ) );
    att_wrreq(fd, 0x0032, (uint8_t*)&code, sizeof code);

    for(int ii=0; ii<3; ii++) {
        handle = att_read_not(fd, &length, rbuf);
        if (handle<0) {
            perror("recv");
        } else {
            printf("notify %4.4X: ", handle);
            for(int jj=0; jj<length; jj++)
                printf("%2.2X", rbuf[jj]);
            printf("\n");
        }
    }

    close(fd);
    return 0;
}
