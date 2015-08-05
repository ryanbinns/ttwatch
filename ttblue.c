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
#include <time.h>

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
int
att_read(int fd, uint16_t handle, uint8_t *buf)
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
att_write(int fd, uint16_t handle, uint8_t *buf, size_t length)
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
att_wrreq(int fd, uint16_t handle, uint8_t *buf, size_t length)
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
att_read_not(int fd, size_t *length, uint8_t *buf)
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
        crc ^= (uint)buf[pos];          // XOR byte into least sig. byte of crc

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
hexlify(FILE *where, uint8_t *buf, size_t len)
{
    while (len--) {
        fprintf(where, "%2.2x", (int)*buf++);
    }
    fputc('\n', where);
}

int
tt_read_file(int fd, uint32_t fileno, int debug, uint8_t **buf)
{
    *buf = NULL;
    if (fileno>>24)
        return -1;

    uint8_t rbuf[BT_ATT_DEFAULT_LE_MTU*2];
    size_t rlen;
    uint8_t cmd[4] = {1, (fileno>>16)&0xff, fileno&0xff, (fileno>>8)&0xff};

    att_wrreq(fd, 0x0025, cmd, sizeof cmd);
    int handle = att_read_not(fd, &rlen, rbuf);
    if (rlen!=4 || handle != 0x0025 || btohl(*((uint32_t*)(rbuf)))!=1) {
        if (debug) {
            printf("expected 0x25->01000000: got 0x%04x->", handle);
            hexlify(stdout, rbuf, rlen);
        }
        return -1;
    }

    handle = att_read_not(fd, &rlen, rbuf);
    if (rlen!=4 || handle != 0x0028) {
        if (debug) {
            printf("expected 0x28->uint32_t: got 0x%04x->", handle);
            hexlify(stdout, rbuf, rlen);
        }
        return -1;
    }

    uint32_t flen = btohl(*((uint32_t*)(rbuf)));
    uint8_t *obuf = *buf = malloc(flen);

    int counter = 0;
    time_t startat, current; time(&startat);
    uint32_t check = 0xffff;
    int end;

    for (int ii=0; ii<flen; ii+=256*20-2) {
        // read up to 256*20-2 data bytes in 20B chunks
        end = ii+256*20-2;
        if (end>flen)
            end=flen;

        for(int jj=ii; jj<end; jj+=20) {
            handle = att_read_not(fd, &rlen, rbuf);
            if (handle != 0x002b) {
                if (debug) {
                    printf("expected 0x2b->uint8_t[]: got 0x%04x->", handle);
                    hexlify(stdout, rbuf, rlen);
                }
                return -1;
            }

            if ((end-jj-rlen)==-1 || (end-jj-rlen)==0){
                // tack on CRC16 straggler byte(s)
                size_t rlen2;
                handle = att_read_not(fd, &rlen2, rbuf+rlen);
                if (handle != 0x002b) {
                    if (debug) {
                        printf("expected 0x2b->uint8_t[]: got 0x%04x->", handle);
                        hexlify(stdout, rbuf, rlen);
                    }
                    return -1;
                }
                rlen += rlen2;
            }

            memcpy(obuf+jj, rbuf, (rlen<end-jj) ? rlen : (end-jj));
            check = crc16(rbuf, rlen, check);

            if (debug>1) {
                printf("%04x: ", jj);
                hexlify(stdout, rbuf, rlen);
            }
        }

        if (check!=0) {
            if (debug)
                printf("wrong crc16 sum: expected 0, got 0x%04x\n", check);
            return -1;
        }
        check = 0xffff;

        uint32_t c = htobl(++counter);
        att_write(fd, 0x002e, (uint8_t*)&c, sizeof c);
        if (debug) {
            time(&current);
            printf("%d: read %d/%d bytes so far (%d/sec)\n", counter, end, flen, (int)(end / (current-startat)) );
        }
    }

    handle = att_read_not(fd, &rlen, rbuf);
    if (rlen!=4 || handle != 0x0025 || btohl(*((uint32_t*)(rbuf)))!=0) {
        if (debug) {
            printf("expected 0x25->00000000: got 0x%04x->", handle);
            hexlify(stdout, rbuf, rlen);
        }
        return -1;
    }

    return end;
}

/****************************************************************************/

#define BARRAY(...) (uint8_t[]){ __VA_ARGS__ }

int main(int argc, const char **argv)
{
    int did, dd, fd;

    uint8_t dst_type = BDADDR_LE_RANDOM;
    bdaddr_t src_addr, dst_addr;
    int sec = BT_SECURITY_MEDIUM;

    // source and dest addresses
    str2ba(argv[1], &dst_addr);
    bacpy(&src_addr, BDADDR_ANY);

    // setup HCI BLE socket
    did = hci_get_route(NULL);
    if (did < 0) {
        perror("hci_get_route");
        return 1;
    }
    dd = hci_open_dev(did);
    if (dd < 0) {
        perror("hci_open_dev");
        return 1;
    }

    // modeled after how hciconfig does it
    // try to coax this thing to connect more frequently
    /************************************************************************/
    /* lecup <handle> <min> <max> <latency> <timeout>                       */
    /* Options:                                                             */
    /*     -H, --handle <0xXXXX>  LE connection handle                      */
    /*     -m, --min <interval>   Range: 0x0006 to 0x0C80                   */
    /*     -M, --max <interval>   Range: 0x0006 to 0x0C80                   */
    /*     -l, --latency <range>  Slave latency. Range: 0x0000 to 0x03E8    */
    /*     -t, --timeout  <time>  N * 10ms. Range: 0x000A to 0x0C80         */
    /************************************************************************/
    uint16_t hci_handle;
    int result = hci_le_create_conn(dd, htobs(0x0004) /*interval*/,  htobs(0x0004) /*window*/,
                                    0 /*initiator_filter, use peer address*/,
                                    LE_RANDOM_ADDRESS /*peer_bdaddr_type*/, dst_addr /*bdaddr*/,
                                    LE_PUBLIC_ADDRESS /*own_bdaddr_type*/,
                                    htobs(0x0006) /*min_interval*/, htobs(0x0006) /*max_interval*/,
                                    htobs(0) /*latency*/, htobs(200) /*supervision_timeout*/,
                                    htobs(0x0001) /*min_ce_length*/, htobs(0x0001) /*max_ce_length*/, &hci_handle, 25000);
    if (result < 0) {
        perror("hci_le_create_conn");
        printf("connection may be slow!\n");
    }

    // create "normal" L2CAP socket
    str2ba(argv[1], &dst_addr);
    bacpy(&src_addr, BDADDR_ANY);
    fd = l2cap_le_att_connect(&src_addr, &dst_addr, dst_type, sec, true);
    if (fd < 0)
        return 1;

    // set timeout to 2 seconds
    struct timeval to = {.tv_sec=2, .tv_usec=0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &to, sizeof(to));
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &to, sizeof(to));

    // communicate with the device
    uint8_t rbuf[BT_ATT_DEFAULT_LE_MTU];
    int handle;

    length = att_read(fd, 0x0003, rbuf);
    printf("Device name: %.*s\n", length, rbuf);
    att_write(fd, 0x0033, BARRAY(0x01, 0), 2);
    att_wrreq(fd, 0x0035, BARRAY(0x01, 0x13, 0, 0, 0x01, 0x12, 0, 0), 8);
    att_write(fd, 0x0026, BARRAY(0x01, 0), 2);
    uint32_t code = htobl( atoi( argv[2] ) );
    att_wrreq(fd, 0x0032, (uint8_t*)&code, sizeof code);

    handle = att_read_not(fd, (size_t*)&length, rbuf);
    if (handle<0) {
        perror("recv");
        return -1;
    }

    uint8_t *fbuf;
    length = tt_read_file(fd, 0x00910000, 1, &fbuf);
    FILE *f = fopen("0x00910000.ttbin", "w");
    fwrite(fbuf, 1, length, f);
    fclose(f);
    printf("saved %d bytes to file\n", length);
    free(fbuf);

    close(fd);
    return 0;
}
