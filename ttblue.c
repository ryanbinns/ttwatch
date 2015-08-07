/**
 *
 */

#define _GNU_SOURCE
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdio.h>
#include <time.h>

#include <sys/time.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>
#include <bluetooth/l2cap.h>
#include "att-types.h"

/**
 * taken from bluez/tools/btgatt-client.c
 *   added hci LE interval customization
 *
 */

#define ATT_CID 4
static int l2cap_le_att_connect_fast(bdaddr_t *src, bdaddr_t *dst, uint8_t dst_type,
                                     int sec, int verbose)
{
    int devid, dd, sock, result;
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

    // setup HCI BLE socket
    devid = hci_get_route(NULL);
    if (devid < 0) {
        perror("Failed to get default hci device");
        return -1;
    }
    dd = hci_open_dev(devid);
    if (dd < 0) {
        perror("Failed to open hci device");
        return -1;
    }

    // customize HCI socket to connect tocoax this thing to connect more frequently
    result = hci_le_create_conn(dd, htobs(0x0004) /*scan interval*/,  htobs(0x0004) /*scan window*/,
                                0 /*initiator_filter, use peer address*/,
                                LE_RANDOM_ADDRESS /*peer_bdaddr_type*/, *dst /*bdaddr*/,
                                LE_PUBLIC_ADDRESS /*own_bdaddr_type*/,
                                htobs(0x0006) /*min_interval / 1.25 ms*/, htobs(0x0006) /*max_interval / 1.25ms*/,
                                htobs(0) /*latency*/, htobs(200) /*supervision_timeout*/,
                                htobs(0x0001) /*min_ce_length*/, htobs(0x0001) /*max_ce_length*/, NULL, 25000);
    if (result < 0 && verbose) {
        printf("Could not customize LE connection interval; transfer will be slow!\n");
    }

    sock = socket(PF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_L2CAP);
    hci_close_dev(dd);
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

#define BARRAY(...) (const uint8_t[]){ __VA_ARGS__ }

/* manually assemble ATT packets */
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
        memcpy(buf, rpkt.buf, result-1);
    }
    return result-1;
}

int
att_write(int fd, uint16_t handle, const void *buf, size_t length)
{
    struct { uint8_t opcode; uint16_t handle; uint8_t buf[length]; } __attribute__((packed)) pkt;
    pkt.opcode = BT_ATT_OP_WRITE_CMD;
    pkt.handle = handle;
    memcpy(pkt.buf, buf, length);

    int result = send(fd, &pkt, sizeof(pkt), 0);
    if (result<0)
        return result;

    return length;
}

int
att_wrreq(int fd, uint16_t handle, const void *buf, size_t length)
{
    struct { uint8_t opcode; uint16_t handle; uint8_t buf[length]; } __attribute__((packed)) pkt;
    pkt.opcode = BT_ATT_OP_WRITE_REQ;
    pkt.handle = handle;
    memcpy(pkt.buf, buf, length);

    int result = send(fd, &pkt, sizeof(pkt), 0);
    if (result<0)
        return result;

    uint8_t conf;
    result = recv(fd, &conf, 1, 0);
    if (conf != BT_ATT_OP_WRITE_RSP)
        return -2;

    return length;
}

int
att_read_not(int fd, size_t *length, void *buf)
{
    struct { uint8_t opcode; uint16_t handle; uint8_t buf[BT_ATT_DEFAULT_LE_MTU]; } __attribute__((packed)) rpkt;
    int result = recv(fd, &rpkt, sizeof rpkt, 0);

    if (result<0)
        return result;
    else if (rpkt.opcode != BT_ATT_OP_HANDLE_VAL_NOT)
        return -2;
    else {
        *length = result-3;
        memcpy(buf, rpkt.buf, *length);
        return rpkt.handle;
    }
}

/****************************************************************************/

static uint32_t
crc16(uint8_t *buf, size_t len, uint32_t start)
{
    uint32_t crc = start;		        // should be 0xFFFF first time
    for (size_t pos = 0; pos < len; pos++) {
        crc ^= (uint32_t)buf[pos];          // XOR byte into least sig. byte of crc

        for (int i = 8; i != 0; i--) {  // Loop over each bit
            if ((crc & 0x0001) != 0) {  // If the LSB is set
                crc >>= 1;              // Shift right and XOR 0xA001
                crc ^= 0xA001;
            }
            else                        // Else LSB is not set
                crc >>= 1;              // Just shift right
        }
    }
    return crc;
}

void
hexlify(FILE *where, const uint8_t *buf, size_t len, bool newl)
{
    while (len--) {
        fprintf(where, "%2.2x", (int)*buf++);
    }
    if (newl)
        fputc('\n', where);
}

static inline int
EXPECT_BYTES(int fd, uint8_t *buf)
{
    size_t length;
    uint16_t handle = att_read_not(fd, &length, buf);
    if (handle < 0)
        return handle;
    else if (handle != 0x2b)
        return -EBADMSG;
    return (int)length;
}

static inline int
EXPECT_LENGTH(int fd)
{
    size_t length;
    uint8_t buf[BT_ATT_DEFAULT_LE_MTU];
    uint16_t handle = att_read_not(fd, &length, buf);
    if (handle < 0)
        return handle;
    else if ((handle != 0x28) || (length != 4))
        return -EBADMSG;
    return btohl(*((uint32_t*)buf));
}

static inline int
EXPECT_uint32(int fd, uint16_t handle, uint32_t val)
{
    size_t length;
    uint8_t buf[BT_ATT_DEFAULT_LE_MTU];
    uint16_t h = att_read_not(fd, &length, buf);
    if (h < 0)
        return h;
    else if ((h != handle) || (length != 4) || (btohl(*((uint32_t*)buf))!=val))
        return -EBADMSG;
    return 0;
}

static inline int
EXPECT_uint8(int fd, uint16_t handle, uint8_t val)
{
    size_t length;
    uint8_t buf[BT_ATT_DEFAULT_LE_MTU];
    uint16_t h = att_read_not(fd, &length, buf);
    if (h < 0)
        return h;
    else if ((h != handle) || (length != 1) || (*buf!=val))
        return -EBADMSG;
    return 0;
}

int
tt_read_file(int fd, uint32_t fileno, int debug, uint8_t **buf)
{
    *buf = NULL;
    if (fileno>>24)
        return -EINVAL;

    uint8_t cmd[] = {1, (fileno>>16)&0xff, fileno&0xff, (fileno>>8)&0xff};
    att_wrreq(fd, 0x0025, cmd, sizeof cmd);
    if (EXPECT_uint32(fd, 0x0025, 1) < 0)
        return -EBADMSG;

    int flen = EXPECT_LENGTH(fd);
    if (flen < 0)
        return -EBADMSG;

    uint8_t *optr = *buf = malloc(flen + BT_ATT_DEFAULT_LE_MTU);
    uint8_t *end = optr+flen;
    uint8_t *checkpoint;
    int counter = 0;

    time_t startat, current;
    time(&startat);

    while (optr < end) {
        // checkpoint occurs every (256*20-2) data bytes and at EOF
        checkpoint = optr + (256*20-2);
        if (checkpoint>end)
            checkpoint = end;

        // checkpoint is followed by 2 bytes for CRC16_modbus
        uint32_t check = 0xffff;
        while (optr < checkpoint+2) {
            int rlen = EXPECT_BYTES(fd, optr);
            if (rlen < 0)
                return -EBADMSG;
            check = crc16(optr, rlen, check); // update CRC

            if (debug>1) {
                printf("%04x: ", (int)(optr-*buf));
                hexlify(stdout, optr, rlen, true);
            }

            optr += rlen;
        }
        optr = checkpoint; // trim CRC bytes from output

        if (check!=0) {
            if (debug)
                printf("wrong crc16 sum: expected 0, got 0x%04x\n", check);
            return -EBADMSG;
        }

        uint32_t c = htobl(++counter);
        att_write(fd, 0x002e, &c, sizeof c);
        if (debug) {
            time(&current);
            int rate = current-startat ? (optr-*buf)/(current-startat) : 9999;
            printf("%d: read %d/%d bytes so far (%d/sec)\n", counter, (int)(optr-*buf), (int)(end-*buf), rate);
        }
    }

    if (EXPECT_uint32(fd, 0x25, 0) < 0)
        return -EBADMSG;
    return optr-*buf;
}

int
tt_list_sub_files(int fd, uint32_t fileno, uint16_t **outlist)
{
    *outlist = NULL;
    if (fileno>>24)
        return -EINVAL;

    uint8_t cmd[] = {3, (fileno>>16)&0xff, fileno&0xff, (fileno>>8)&0xff};
    att_wrreq(fd, 0x0025, cmd, sizeof cmd);
    if (EXPECT_uint32(fd, 0x0025, 1) < 0)
        return -EBADMSG;

    // read first packet (normally there's only one)
    uint8_t rbuf[BT_ATT_DEFAULT_LE_MTU];
    int rlen = EXPECT_BYTES(fd, rbuf);
    if (rlen<2)
        return -EBADMSG;
    int n_files = btohs(*(uint16_t*)rbuf);
    uint16_t *list = *outlist = calloc(sizeof(uint16_t), n_files);
    void *optr = mempcpy(list, rbuf+2, rlen-2);

    // read rest of packets (if we have a long file list?)
    for (; optr < (void *)(list+n_files); optr += rlen) {
        rlen=EXPECT_BYTES(fd, optr);
        hexlify(stdout, optr, rlen, true);
        if (rlen<0)
            return -EBADMSG;
    }

    // fix endianness
    for (int ii; ii<n_files; ii++)
        list[ii] = btohs(list[ii]);

    if (EXPECT_uint32(fd, 0x0025, 0) < 0)
        return -EBADMSG;

    return n_files;
}

/****************************************************************************/

int main(int argc, const char **argv)
{
    int did, dd, fd;

    uint8_t dst_type = BDADDR_LE_RANDOM;
    bdaddr_t src_addr, dst_addr;
    int sec = BT_SECURITY_MEDIUM;

    // source and dest addresses
    str2ba(argv[1], &dst_addr);
    bacpy(&src_addr, BDADDR_ANY);

    // create L2CAP socket with minimum connection interval
    str2ba(argv[1], &dst_addr);
    bacpy(&src_addr, BDADDR_ANY);
    fd = l2cap_le_att_connect_fast(&src_addr, &dst_addr, dst_type, sec, true);
    if (fd < 0)
        goto fail;

    // set timeout to 2 seconds
    struct timeval to = {.tv_sec=2, .tv_usec=0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &to, sizeof(to));
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &to, sizeof(to));

    // authorize with the device
    uint8_t rbuf[BT_ATT_DEFAULT_LE_MTU];
    int handle;

    int length = att_read(fd, 0x0003, rbuf);
    printf("Device name: %.*s\n", length, rbuf);
    att_write(fd, 0x0033, BARRAY(0x01, 0), 2);
    att_wrreq(fd, 0x0035, BARRAY(0x01, 0x13, 0, 0, 0x01, 0x12, 0, 0), 8);
    att_write(fd, 0x0026, BARRAY(0x01, 0), 2);
    uint32_t code = htobl( atoi( argv[2] ) );
    att_wrreq(fd, 0x0032, &code, sizeof code);
    if (EXPECT_uint8(fd, 0x0032, 1) < 0) {
        printf("Device didn't accept auth code %d.\n", code);
        goto fail;
    }

    // transfer files
    uint8_t *fbuf;
    FILE *f;

    printf("Reading 0x00f20000.xml ...\n");
    length = tt_read_file(fd, 0x00f20000, 2, &fbuf);
    f = fopen("0x00f20000.xml", "w");
    fwrite(fbuf, 1, length, f);
    fclose(f);
    printf("saved %d bytes to 0x00f20000.xml\n", length);
    free(fbuf);

    uint16_t *list;
    int n_files = tt_list_sub_files(fd, 0x00910000, &list);
    char fn[] = "0x000910000.ttbin";
    printf("Found %d activity files.\n", n_files);
    for (int ii=0; ii<n_files; ii++) {
        uint32_t fileno = 0x00910000 + list[ii];
        printf("Reading activity file 0x%08X ...\n", fileno);
        length = tt_read_file(fd, fileno, 1, &fbuf);

        sprintf(fn, "0x%08X.ttbin", fileno);
        f = fopen(fn, "w");
        fwrite(fbuf, 1, length, f);
        fclose(f);
        printf("saved %d bytes to %s\n", length, fn);
        free(fbuf);
    }

    close(fd);
    return 0;

fail:
    return 1;
}
