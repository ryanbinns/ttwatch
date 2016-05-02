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

/****************************************************************************/

const char *FIRMWARE_TOO_OLD =
    "Firmware v%s is too old; at least v%s is required\n"
    "* TomTom firmware release notes:\n"
    "\thttp://us.support.tomtom.com/app/release_notes/type/watches\n"
    "* Use USB cable and ttwatch to update your firmware:\n"
    "\thttp://github.com/ryanbinns/ttwatch\n";

const char *FIRMWARE_UNTESTED =
    "WARNING: Firmware v%s has not been tested with ttblue\n"
    "  Please email dlenski@gmail.com and let me know if it works or not\n";

const char *MODEL_UNTESTED =
    "WARNING: Model number %s has not been tested with ttblue\n"
    "  Please email dlenski@gmail.com and let me know if it works or not\n";

struct ble_dev_info info[] = {
    // from @drkingpo's btsnoop_hci.log: these are all the same as the v1 identifiers (+ 0x30)
    { 0x004e, "maker" },
    { 0x0046, "serial" },
    { 0x0016, "user_name" }, // from gatttool, sent by @drkingpo
    { 0x0044, "model_name" },
    { 0x004a, "model_num" },
    { 0x004c, "firmware" },
    { 0 }
};

struct ble_dev_info *
tt_check_device_version(int fd, bool warning)
{
    for (struct ble_dev_info *p = info; p->handle; p++) {
        p->len = att_read(fd, p->handle, p->buf);
        if (p->len < 0) {
            fprintf(stderr, "Could not read device information (handle 0x%04x, %s): %s (%d)\n", p->handle, p->name, strerror(errno), errno);
            return NULL;
        }
        p->buf[p->len] = 0;
    }


    // Maker field always seems to be blank for v2 devices
    /* if (strcmp(info[0].buf, EXPECTED_MAKER) != 0) {
        fprintf(stderr, "Maker is not %s but '%s', exiting!\n", EXPECTED_MAKER, info[1].buf);
        return NULL;
    } else */
    if (strcmp(info[5].buf, OLDEST_TESTED_FIRMWARE) < 0) {
        fprintf(stderr, FIRMWARE_TOO_OLD, info[5].buf, OLDEST_TESTED_FIRMWARE);
        return NULL;
    }

    if (warning && strcmp(info[5].buf, NEWEST_TESTED_FIRMWARE) > 0)
        fprintf(stderr, FIRMWARE_UNTESTED, info[5].buf);

    if (warning && !IS_TESTED_MODEL(info[4].buf))
        fprintf(stderr, MODEL_UNTESTED, info[4].buf);

    return info;
}

/****************************************************************************/

int
tt_authorize(int fd, uint32_t code, bool new_code)
{
    // authorize with the device
    const uint16_t auth_one = btohs(0x0001);
    const uint8_t magic_bytes[] = { 0x01, 0x15, 0, 0, 0x01, 0x1f, 0, 0 }; // from @drkingpo's btsnoop_hci.log
    uint32_t bcode = htobl(code);

    if (new_code) {
        // not seen in @drkingpo's logs -- educated guess
        att_write(fd, 0x0083, &auth_one, sizeof auth_one);
        att_wrreq(fd, 0x0073, &auth_one, sizeof auth_one);
        att_write(fd, 0x007c, &auth_one, sizeof auth_one);
        att_write(fd, 0x0076, &auth_one, sizeof auth_one);
        att_write(fd, 0x0079, &auth_one, sizeof auth_one);
        att_wrreq(fd, H_MAGIC, magic_bytes, sizeof magic_bytes);
        att_wrreq(fd, H_PASSCODE, &bcode, sizeof bcode);
    } else {
        // based on btsnoop_hci.log from @drkingpo
        att_write(fd, 0x0083, &auth_one, sizeof auth_one); // from @drkingpo's btsnoop_hci.log (v1 + 0x50)
        att_wrreq(fd, H_MAGIC, magic_bytes, sizeof magic_bytes); // from @drkingpo's btsnoop_hci.log (v1 + 0x50)
        att_wrreq(fd, 0x0073, &auth_one, sizeof auth_one); // from @drkingpo's btsnoop_hci.log (v1 + 0x4d, and CHANGED FROM WRITE TO WRREQ
        att_wrreq(fd, H_PASSCODE, &bcode, sizeof bcode); // from @drkingpo's btsnoop_hci.log (v1 + 0x50)

        int res = EXPECT_uint8(fd, H_PASSCODE, 1);
        if (res < 0)
            return res;

        att_write(fd, 0x007c, &auth_one, sizeof auth_one); // from @drkingpo's btsnoop_hci.log (v1 + 0x4d)
        att_write(fd, 0x0076, &auth_one, sizeof auth_one); // from @drkingpo's btsnoop_hci.log (v1 + 0x4d)
        att_write(fd, 0x0079, &auth_one, sizeof auth_one); // from @drkingpo's btsnoop_hci.log (v1 + 0x4d)
        att_wrreq(fd, H_MAGIC, magic_bytes, sizeof magic_bytes); // from @drkingpo's btsnoop_hci.log (v1 + 0x50)
        att_wrreq(fd, H_PASSCODE, &bcode, sizeof bcode); // from @drkingpo's btsnoop_hci.log (v1 + 0x50)
    }

    return EXPECT_uint8(fd, H_PASSCODE, 1);
}

int
tt_reboot(int fd)
{
    // ... then overwhelm the device with a torrent of zeros to the status register
    uint32_t bork = 0;
    for (int ii=1; ii<=1000; ii++) {
        if (att_wrreq(fd, H_CMD_STATUS, &bork, 4) < 0)
            return ii;
    }
    return -1;
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
        goto prealloc_fail;

    int flen = EXPECT_LENGTH(fd);
    if (flen < 0)
        goto prealloc_fail;

    uint8_t *optr = *buf = malloc(flen + BT_ATT_DEFAULT_LE_MTU);
    const uint8_t *end = optr+flen;
    const uint8_t *checkpoint;
    int counter = 0;

    time_t startat=time(NULL);
    struct timeval now;
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
                goto fail;
            check = crc16(optr, rlen, check); // update CRC

            if (debug>2) {
                gettimeofday(&now, NULL);
                fprintf(stderr, "%010ld.%06ld: %04x: ", now.tv_sec, now.tv_usec, (int)(optr-*buf));
                hexlify(stderr, optr, rlen, true);
            }

            optr += rlen;
        }
        optr = (void*)checkpoint; // trim CRC bytes from output position

        if (check!=0) {
            if (debug)
                fprintf(stderr, "wrong crc16 sum: expected 0, got 0x%04x\n", check);
            goto fail;
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
        goto fail;
    return optr-*buf;

fail:
    free(*buf);
    fprintf(stderr, "File read failed at byte position %d of %d\n", (int)(optr-*buf), flen);
    perror("fail");
prealloc_fail:
    return -EBADMSG;
}

int
tt_write_file(int fd, uint32_t fileno, int debug, const uint8_t *buf, uint32_t length, uint32_t write_delay)
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
            int wlen;
            uint8_t *out;

            if (iptr+20 < checkpoint) {
                wlen = 20;
                out = (void*)iptr;
                check = crc16(iptr, wlen, check); // update CRC with data bytes
            } else {
                wlen = checkpoint-iptr;
                out = temp;
                check = crc16(iptr, wlen, check); // update CRC with data bytes

                uint16_t c = btohs(check);
                memcpy( mempcpy(out, iptr, wlen),
                        &c, sizeof c); // output is data bytes + CRC16
                wlen += 2;
            }

            if (att_write(fd, H_TRANSFER, out, (wlen<20) ? wlen : 20) < 0)
                goto fail_write;
            if (wlen>20)
                if (att_write(fd, H_TRANSFER, out+20, wlen-20) < 0)
                    goto fail_write;

            iptr += wlen;

            // wait between packets, because the devices don't like having them spit out
            // at max speed with min connection interval
            gettimeofday(&now, NULL);

            if (debug>2) {
                fprintf(stderr, "%010ld.%06ld: %04x: ", now.tv_sec, now.tv_usec, (int)(iptr-buf));
                hexlify(stderr, out, wlen, true);
            }

            useconds_t elapsed_usec = ((now.tv_sec-lastpkt.tv_sec)*1000000 + now.tv_usec-lastpkt.tv_usec);
            if (elapsed_usec < write_delay) usleep(write_delay - elapsed_usec);
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
                fprintf(stderr, "%d: wrote %d bytes to watch (%d/sec)       \n", counter, length, rate);
            fflush(stdout);
        }
    }

    if (EXPECT_uint32(fd, H_CMD_STATUS, 0) < 0)
        return -EBADMSG;
    return iptr-buf;

fail_write:
    fprintf(stderr, "File write failed at byte position %d of %d\n", (int)(iptr-buf), length);
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
        if (rlen<0) {
            free(list);
            return -EBADMSG;
        }
    }

    // fix endianness
    for (int ii; ii<n_files; ii++)
        list[ii] = btohs(list[ii]);

    if (EXPECT_uint32(fd, H_CMD_STATUS, 0) < 0) {
        free(list);
        return -EBADMSG;
    }

    return n_files;
}
