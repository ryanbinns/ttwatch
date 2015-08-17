#define _GNU_SOURCE
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/time.h>

#include <bluetooth/bluetooth.h>

#include "ttops.h"

static uint32_t
crc16(const uint8_t *buf, size_t len, uint32_t start)
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

int
tt_authorize(int fd, uint32_t code, bool new_code)
{
    // authorize with the device
    const uint16_t auth_one = btohs(0x0001);
    const uint8_t auth_magic[] = { 0x01, 0x13, 0, 0, 0x01, 0x12, 0, 0 };
    uint32_t bcode = htobl(code);

    if (new_code) {
        att_write(fd, 0x0033, &auth_one, sizeof auth_one);
        att_write(fd, 0x0026, &auth_one, sizeof auth_one);
        att_write(fd, 0x0029, &auth_one, sizeof auth_one);
        att_write(fd, 0x003c, &auth_one, sizeof auth_one);
        att_write(fd, 0x002c, &auth_one, sizeof auth_one);
        att_wrreq(fd, H_MAGIC, auth_magic, sizeof auth_magic);
        att_wrreq(fd, H_PASSCODE, &bcode, sizeof bcode);
    } else {
        att_write(fd, 0x0033, &auth_one, sizeof auth_one);
        att_wrreq(fd, H_MAGIC, auth_magic, sizeof auth_magic);
        att_write(fd, 0x0026, &auth_one, sizeof auth_one);
        att_wrreq(fd, H_PASSCODE, &bcode, sizeof bcode);
    }

    return EXPECT_uint8(fd, H_PASSCODE, 1);
}

int
tt_read_file(int fd, uint32_t fileno, int debug, uint8_t **buf)
{
    *buf = NULL;
    if (fileno>>24)
        return -EINVAL;

    uint8_t cmd[] = {1, (fileno>>16)&0xff, fileno&0xff, (fileno>>8)&0xff};
    att_wrreq(fd, H_CMD_STATUS, cmd, sizeof cmd);
    if (EXPECT_uint32(fd, H_CMD_STATUS, 1) < 0)
        return -EBADMSG;

    int flen = EXPECT_LENGTH(fd);
    if (flen < 0)
        return -EBADMSG;

    uint8_t *optr = *buf = malloc(flen + BT_ATT_DEFAULT_LE_MTU);
    const uint8_t *end = optr+flen;
    const uint8_t *checkpoint;
    int counter = 0;

    time_t startat=time(NULL);
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
                fprintf(stderr, "%04x: ", (int)(optr-*buf));
                hexlify(stderr, optr, rlen, true);
            }

            optr += rlen;
        }
        optr = (void*)checkpoint; // trim CRC bytes from output position

        if (check!=0) {
            if (debug)
                fprintf(stderr, "wrong crc16 sum: expected 0, got 0x%04x\n", check);
            return -EBADMSG;
        }

        uint32_t c = htobl(++counter);
        att_write(fd, H_CHECK, &c, sizeof c);
        if (debug) {
            time_t current = time(NULL);
            int rate = current-startat ? (optr-*buf)/(current-startat) : 9999;
            if (optr<end)
                fprintf(stderr, "%d: read %d/%d bytes so far (%d/sec)\r", counter, (int)(optr-*buf), (int)(end-*buf), rate);
            else
                fprintf(stderr, "%d: read %d bytes from watch (%d/sec)      \n", counter, flen, rate);
            fflush(stdout);
        }
    }

    if (EXPECT_uint32(fd, H_CMD_STATUS, 0) < 0)
        return -EBADMSG;
    return optr-*buf;
}

int
tt_write_file(int fd, uint32_t fileno, int debug, const uint8_t *buf, uint32_t length)
{
    if (fileno>>24)
        return -EINVAL;

    uint8_t cmd[] = {0, (fileno>>16)&0xff, fileno&0xff, (fileno>>8)&0xff};
    att_wrreq(fd, H_CMD_STATUS, cmd, sizeof cmd);
    if (EXPECT_uint32(fd, H_CMD_STATUS, 1) < 0)
       return -EBADMSG;

    uint32_t flen = htobl(length);
    att_wrreq(fd, H_LENGTH, &flen, sizeof flen);

    const uint8_t *iptr = buf;
    const uint8_t *end = iptr+length;
    const uint8_t *checkpoint;
    uint8_t temp[22];
    int counter = 0;

    time_t startat = time(NULL);
    struct timeval now, lastpkt = { -1, -1 }; //yes, that's a fake/invalid time-of-day
    while (iptr < end) {
        // checkpoint occurs every (256*20-2) data bytes and at EOF
        checkpoint = iptr + (256*20-2);
        if (checkpoint>end)
            checkpoint = end;

        // checkpoint is followed by 2 bytes for CRC16_modbus
        uint32_t check = 0xffff;
        while (iptr < checkpoint) {
            int wlen = (iptr+20 < checkpoint) ? 20 : (checkpoint-iptr);
            check = crc16(iptr, wlen, check); // update CRC with data bytes

            uint8_t *out;
            if (wlen==20) {
                out = (void*)iptr;
            } else {
                uint16_t c = btohs(check);
                memcpy( mempcpy(out=temp, iptr, wlen),
                        &c, sizeof c); // output is data bytes + CRC16
                wlen += 2;
            }

            if (att_write(fd, H_TRANSFER, out, (wlen<20) ? wlen : 20) < 0)
                goto fail_write;
            if (wlen>20)
                if (att_write(fd, H_TRANSFER, out+20, wlen-20) < 0)
                    goto fail_write;

            if (debug>1) {
                fprintf(stderr, "%04x: ", (int)(iptr-buf));
                hexlify(stderr, out, wlen, true);
            }

            iptr += wlen;

            // wait at least 20 ms between packets, because the devices
            // don't like having them spit out at max speed with min
            // connection interval
            gettimeofday(&now, NULL);
            if (now.tv_sec==lastpkt.tv_sec && now.tv_usec-lastpkt.tv_usec < 20000)
                usleep(20000);
            memcpy(&lastpkt, &now, sizeof now);
        }
        iptr = checkpoint; // trim CRC bytes from input position

        if (EXPECT_uint32(fd, H_CHECK, ++counter) < 0) // didn't get expected counter
            goto fail_write;
        if (debug) {
            time_t current = time(NULL);
            int rate = current-startat ? (iptr-buf)/(current-startat) : 9999;
            if (iptr<end)
                fprintf(stderr, "%d: wrote %d/%d bytes so far (%d/sec)\r", counter, (int)(iptr-buf), (int)(end-buf), rate);
            else
                fprintf(stderr, "%d: wrote %d bytes to watch (%d/sec)       \n", counter, flen, rate);
            fflush(stdout);
        }
    }

    if (EXPECT_uint32(fd, H_CMD_STATUS, 0) < 0)
        return -EBADMSG;
    return iptr-buf;

fail_write:
    fprintf(stderr, "at file position 0x%04x\n", (int)(iptr-buf));
    perror("fail");
    return -EBADMSG;
}

int
tt_delete_file(int fd, uint32_t fileno)
{
    if (fileno>>24)
        return -EINVAL;

    uint8_t cmd[] = {4, (fileno>>16)&0xff, fileno&0xff, (fileno>>8)&0xff};
    att_wrreq(fd, H_CMD_STATUS, cmd, sizeof cmd);
    if (EXPECT_uint32(fd, H_CMD_STATUS, 1) < 0)
        return -EBADMSG;

    // discard H_TRANSFER packets which I don't understand until we get H_CMD_STATUS<-0
    uint16_t handle;
    uint8_t rbuf[BT_ATT_DEFAULT_LE_MTU];
    int rlen;
    for (;;) {
        rlen = att_read_not(fd, &handle, rbuf);
        if (handle==H_CMD_STATUS && rlen==4 && (*(uint32_t*)rbuf==0))
            return 0;
        else if (handle!=H_TRANSFER)
            return -EBADMSG;
    }
}

int
tt_list_sub_files(int fd, uint32_t fileno, uint16_t **outlist)
{
    *outlist = NULL;
    if (fileno>>24)
        return -EINVAL;

    uint8_t cmd[] = {3, (fileno>>16)&0xff, fileno&0xff, (fileno>>8)&0xff};
    att_wrreq(fd, H_CMD_STATUS, cmd, sizeof cmd);
    if (EXPECT_uint32(fd, H_CMD_STATUS, 1) < 0)
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

    if (EXPECT_uint32(fd, H_CMD_STATUS, 0) < 0)
        return -EBADMSG;

    return n_files;
}
