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

#include "util.h"
#include "ttops.h"
#include "version.h"

/****************************************************************************/

struct tt_handles v1_handles = { .ppcp=0x0b, .passcode=0x32, .magic=0x35, .cmd_status=0x25, .length=0x28, .transfer=0x2b, .check=0x2e };
struct tt_handles v2_handles = { .ppcp=0,    .passcode=0x82, .magic=0x85, .cmd_status=0x72, .length=0x75, .transfer=0x78, .check=0x7b };

struct tt_files v1_files = {
    .hostname       = 0x00020002,
    .manifest       = 0x000f20000,
    .activity_start = 0x00910000,
    .gps_status     = 0x00020001,
    .quickgps       = 0x00010100
};

struct tt_files v2_files = {
    .hostname       = 0x00020003,
    .manifest       = 0x000f20000,
    .activity_start = 0x00910000,
    .gps_status     = 0x00020001,
    .quickgps       = 0x00010100
};

struct ble_dev_info v1_info[] = {
    { 0x001e, "maker" },
    { 0x0016, "serial" },
    { 0x0003, "user_name" },
    { 0x0014, "model_name" },
    { 0x001a, "model_num" },
    { 0x001c, "firmware" },
    { 0 }
};

struct ble_dev_info v2_info[] = {
    // from @drkingpo's btsnoop_hci.log: these are all the same as the v1 identifiers (+ 0x30)
    { 0x004e, "maker" },
    { 0x0046, "serial" },
    { 0x0016, "user_name" }, // from gatttool, sent by @drkingpo
    { 0x0044, "model_name" },
    { 0x004a, "model_num" },
    { 0x004c, "firmware" },
    { 0x0042, "system_id" } // Seems to be set to 0.
};

#define EXPECTED_MAKER "TomTom Fitness"
const char *tested_models_v1[] = {"1001","1002","1003","1004",NULL};
const char *tested_models_v2[] = {"2005","2006","2008",NULL};

const char *FIRMWARE_TOO_OLD =
    "Firmware v%s is too old; at least v%s is required\n"
    "* TomTom firmware release notes for your device:\n"
    "\t%s\n"
    "* Use USB cable and ttwatch to update your firmware:\n"
    "\thttp://github.com/ryanbinns/ttwatch\n";

const char *FIRMWARE_NOTES_v1 = "https://us.support.tomtom.com/app/release_notes/type/watches";
const char *FIRMWARE_NOTES_v2 = "https://us.support.tomtom.com/app/release_notes/type/watch2015";

const char *FIRMWARE_UNTESTED =
    "WARNING: Firmware v%s has not been tested with ttblue\n"
    "  Please email dlenski@gmail.com and let me know if it works or not\n";

const char *MODEL_UNTESTED =
    "WARNING: Model number %s has not been tested with ttblue\n"
    "  Please email dlenski@gmail.com and let me know if it works or not\n";

TTDEV *
tt_device_init(int protocol_version, int fd) {
    TTDEV *d = malloc(sizeof(struct ttdev));
    if (!d)
        return NULL;

    d->fd = fd;
    d->protocol_version = protocol_version;

    switch (protocol_version) {
    case 1:
        d->h = &v1_handles;
        d->info = v1_info;
        d->oldest_tested_firmware = VERSION_TUPLE(1,8,34);
        d->newest_tested_firmware = VERSION_TUPLE(1,8,46);
        d->tested_models = tested_models_v1;
        d->files = &v1_files;
        break;
    case 2:
        d->h = &v2_handles;
        d->info = v2_info;
        d->oldest_tested_firmware = VERSION_TUPLE(1,1,19);
        d->newest_tested_firmware = VERSION_TUPLE(1,7,64);
        // @drkingpo confirmed v1.2.0 works now (see issue #5)
        // @Grimler91 tested 1.7.62 and 1.7.64.
        d->tested_models = tested_models_v2;
        d->files = &v2_files;
        break;
    default:
        return NULL;
    };
    return d;
}

bool
tt_device_done(TTDEV *d) {
    free(d);
    return true;
}

struct ble_dev_info *
tt_check_device_version(TTDEV *d, bool warning)
{
    struct ble_dev_info *info = d->info;

    for (struct ble_dev_info *p = info; p->handle; p++) {
        p->len = att_read(d->fd, p->handle, p->buf);
        if (p->len < 0) {
            fprintf(stderr, "Could not read device information (handle 0x%04x, %s): %s (%d)\n", p->handle, p->name, strerror(errno), errno);
            return NULL;
        }
        p->buf[p->len] = 0;
    }

    if (d->protocol_version == 2) {
        // Maker field always seems to be blank for v2 devices
        // @drkingpo's logs show a malformed packet when this handle is read
    } else {
        if (strcmp(info[0].buf, EXPECTED_MAKER) != 0) {
            fprintf(stderr, "Maker is not %s but '%s', exiting!\n", EXPECTED_MAKER, info[1].buf);
            return NULL;
        }
    }

    struct version_tuple fw_ver;
    if (parse_version(info[5].buf, &fw_ver, ".") < 0) {
        fprintf(stderr, "Could not parse firmware version string: %s\n", info[5].buf);
        return NULL;
    }

    if (compare_versions(&fw_ver, &d->oldest_tested_firmware) < 0) {
        fprintf(stderr, FIRMWARE_TOO_OLD, info[5].buf, str_version(&d->oldest_tested_firmware,'.'),
                d->protocol_version==1 ? FIRMWARE_NOTES_v1 : FIRMWARE_NOTES_v2);
        return NULL;
    }

    if (warning) {
        if (compare_versions(&fw_ver, &d->newest_tested_firmware) > 0)
            fprintf(stderr, FIRMWARE_UNTESTED, info[5].buf);

        const char **m;
        for (m=d->tested_models; *m; m++) {
            if (!strcmp(*m, info[4].buf))
                break;
        }
        if (!*m)
            fprintf(stderr, MODEL_UNTESTED, info[4].buf);
    }

    return info;
}

/****************************************************************************/

int
tt_authorize(TTDEV *d, uint32_t code, bool new_code)
{
    // authorize with the device
    const uint16_t auth_one = btohs(0x0001);
    uint32_t bcode = htobl(code);
    const uint8_t *magic_bytes;

    switch (d->protocol_version) {
    case 1:
        // TomTom MySports 2.1.13-a3eb49a for Android
        magic_bytes = BARRAY( 0x01, 0x16, 0, 0, 0x01, 0x29, 0, 0 );
        if (new_code) {
            att_write(d->fd, 0x0033, &auth_one, sizeof auth_one);
            att_write(d->fd, 0x0026, &auth_one, sizeof auth_one);
            att_write(d->fd, 0x002f, &auth_one, sizeof auth_one);
            att_write(d->fd, 0x0029, &auth_one, sizeof auth_one);
            att_write(d->fd, 0x002c, &auth_one, sizeof auth_one);
            att_wrreq(d->fd, d->h->magic, magic_bytes, 8);
            att_wrreq(d->fd, d->h->passcode, &bcode, sizeof bcode);
        } else {
            att_write(d->fd, 0x0033, &auth_one, sizeof auth_one);
            att_wrreq(d->fd, d->h->magic, magic_bytes, sizeof magic_bytes);
            att_write(d->fd, 0x0026, &auth_one, sizeof auth_one);
            att_wrreq(d->fd, d->h->passcode, &bcode, sizeof bcode);

            int res = EXPECT_uint8(d, d->h->passcode, 1);
            if (res < 0)
                return res;

            att_write(d->fd, 0x002f, &auth_one, sizeof auth_one);
            att_write(d->fd, 0x0029, &auth_one, sizeof auth_one);
            att_write(d->fd, 0x002c, &auth_one, sizeof auth_one);
            att_wrreq(d->fd, d->h->magic, magic_bytes, 8);
            att_wrreq(d->fd, d->h->passcode, &bcode, sizeof bcode);
        }
        return EXPECT_uint8(d, d->h->passcode, 1);
    case 2:
        // Android software, from @drkingpo's log, updated by @Grimler91 for 1.7.64
        magic_bytes = BARRAY( 0x01, 0x19, 0, 0, 0x01, 0x17, 0, 0 );
        if (new_code) {
            att_wrreq(d->fd, 0x0083, &auth_one, sizeof auth_one); // (v1 + 0x50)
            att_wrreq(d->fd, 0x0088, &auth_one, sizeof auth_one);
            att_wrreq(d->fd, 0x0073, &auth_one, sizeof auth_one); // (v1 + 0x4d)
            att_wrreq(d->fd, 0x007c, &auth_one, sizeof auth_one); // (v1 + 0x4d)
            att_wrreq(d->fd, 0x0076, &auth_one, sizeof auth_one); // (v1 + 0x4d)
            att_wrreq(d->fd, 0x0079, &auth_one, sizeof auth_one); // (v1 + 0x4d)
            att_wrreq(d->fd, d->h->magic, magic_bytes, sizeof magic_bytes); // (v1 + 0x50)
            att_wrreq(d->fd, d->h->passcode, &bcode, sizeof bcode); //  (v1 + 0x50)
        } else {
            // based on btsnoop_hci.log from @drkingpo
            att_wrreq(d->fd, 0x0083, &auth_one, sizeof auth_one); // (v1 + 0x50)
            att_wrreq(d->fd, d->h->magic, magic_bytes, sizeof magic_bytes); // (v1 + 0x50)
            att_wrreq(d->fd, 0x0073, &auth_one, sizeof auth_one); // (v1 + 0x4d)
            att_wrreq(d->fd, d->h->passcode, &bcode, sizeof bcode); // (v1 + 0x50)

            int res = EXPECT_uint8(d, d->h->passcode, 1);
            if (res < 0)
                return res;

            att_wrreq(d->fd, 0x007c, &auth_one, sizeof auth_one); // (v1 + 0x4d)
            att_wrreq(d->fd, 0x0076, &auth_one, sizeof auth_one); // (v1 + 0x4d)
            att_wrreq(d->fd, 0x0079, &auth_one, sizeof auth_one); // (v1 + 0x4d)
            att_wrreq(d->fd, d->h->magic, magic_bytes, sizeof magic_bytes); // (v1 + 0x50)
            att_wrreq(d->fd, d->h->passcode, &bcode, sizeof bcode); //  (v1 + 0x50)
        }
        return EXPECT_uint8(d, d->h->passcode, 1);
    }
    return -2;
}

int
tt_reboot(TTDEV *d)
{
    // ... then overwhelm the device with a torrent of zeros to the status register
    uint32_t bork = 0;
    for (int ii=1; ii<=1000; ii++) {
        if (att_wrreq(d->fd, d->h->cmd_status, &bork, 4) < 0)
            return ii;
    }
    return -1;
}

int
tt_read_file(TTDEV *d, uint32_t fileno, int debug, uint8_t **buf)
{
    *buf = NULL;
    if (fileno>>24)
        return -EINVAL;

    uint8_t cmd[] = {1, (fileno>>16)&0xff, fileno&0xff, (fileno>>8)&0xff};
    att_wrreq(d->fd, d->h->cmd_status, cmd, sizeof cmd);
    if (EXPECT_uint32(d, d->h->cmd_status, 1) < 0)
        goto prealloc_fail;

    int flen = EXPECT_LENGTH(d);
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
            int rlen = EXPECT_BYTES(d, optr);
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
        att_write(d->fd, d->h->check, &c, sizeof c);
        if (debug) {
            time_t current = time(NULL);
            int rate = current-startat ? (optr-*buf)/(current-startat) : 9999;
            if (optr<end)
                fprintf(stderr, "%d: read %d/%d bytes so far (%d/sec)%c", counter, (int)(optr-*buf), (int)(end-*buf), rate, debug<=2 ? '\r' : '\n');
            else
                fprintf(stderr, "%d: read %d bytes from watch (%d/sec)      \n", counter, flen, rate);
            fflush(stdout);
        }
    }

    uint32_t status;
    if (EXPECT_ANY_uint32(d, d->h->cmd_status, &status) < 0)
        goto fail;
    else if (status!=0)
        fprintf(stderr, "tt_read_file: status=0x%08x (please send log to dlenski@gmail.com)\n", status);

    return optr-*buf;

fail:
    free(*buf);
    fprintf(stderr, "File read failed at byte position %d of %d\n", (int)(optr-*buf), flen);
    perror("fail");
prealloc_fail:
    return -1;
}

int
tt_write_file(TTDEV *d, uint32_t fileno, int debug, const uint8_t *buf, uint32_t length, uint32_t write_delay)
{
    if (fileno>>24)
        return -EINVAL;

    uint8_t cmd[] = {0, (fileno>>16)&0xff, fileno&0xff, (fileno>>8)&0xff};
    att_wrreq(d->fd, d->h->cmd_status, cmd, sizeof cmd);
    if (EXPECT_uint32(d, d->h->cmd_status, 1) < 0)
       return -1;

    uint32_t flen = htobl(length);
    att_write(d->fd, d->h->length, &flen, sizeof flen);

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

            if (att_write(d->fd, d->h->transfer, out, (wlen<20) ? wlen : 20) < 0)
                goto fail_write;
            if (wlen>20)
                if (att_write(d->fd, d->h->transfer, out+20, wlen-20) < 0)
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

        if (EXPECT_uint32(d, d->h->check, ++counter) < 0) // didn't get expected counter
            goto fail_write;
        if (debug) {
            time_t current = time(NULL);
            int rate = current-startat ? (iptr-buf)/(current-startat) : 9999;
            if (iptr<end)
                fprintf(stderr, "%d: wrote %d/%d bytes so far (%d/sec)%c", counter, (int)(iptr-buf), (int)(end-buf), rate, debug<=2 ? '\r' : '\n');
            else
                fprintf(stderr, "%d: wrote %d bytes to watch (%d/sec)       \n", counter, length, rate);
            fflush(stdout);
        }
    }

    uint32_t status;
    if (EXPECT_ANY_uint32(d, d->h->cmd_status, &status) < 0)
        return -1;
    else if (status!=0)
        fprintf(stderr, "tt_write_file: status=0x%08x (please send log to dlenski@gmail.com)\n", status);

    return iptr-buf;

fail_write:
    fprintf(stderr, "File write failed at byte position %d of %d\n", (int)(iptr-buf), length);
    perror("fail");
    return -1;
}

int
tt_delete_file(TTDEV *d, uint32_t fileno)
{
    if (fileno>>24)
        return -EINVAL;

    uint8_t cmd[] = {4, (fileno>>16)&0xff, fileno&0xff, (fileno>>8)&0xff};
    att_wrreq(d->fd, d->h->cmd_status, cmd, sizeof cmd);
    if (EXPECT_uint32(d, d->h->cmd_status, 1) < 0)
        return -1;

    // discard H_TRANSFER packets which I don't understand until we get H_cmd_status<-0
    uint16_t handle;
    union { uint8_t buf[BT_ATT_DEFAULT_LE_MTU]; uint32_t out; } r;
    int rlen;
    for (;;) {
        rlen = att_read_not(d->fd, &handle, r.buf);
        if (handle==d->h->cmd_status && rlen==4 && r.out==0)
            return 0;
        else if (handle!=d->h->transfer)
            return -1;
    }
}

int
tt_list_sub_files(TTDEV *d, uint32_t fileno, uint16_t **outlist)
{
    *outlist = NULL;
    if (fileno>>24)
        return -EINVAL;

    uint8_t cmd[] = {3, (fileno>>16)&0xff, fileno&0xff, (fileno>>8)&0xff};
    att_wrreq(d->fd, d->h->cmd_status, cmd, sizeof cmd);
    if (EXPECT_uint32(d, d->h->cmd_status, 1) < 0)
        return -1;

    // read first packet (normally there's only one)
    union { uint8_t buf[BT_ATT_DEFAULT_LE_MTU]; uint16_t vals[0]; } r;
    int rlen = EXPECT_BYTES(d, r.buf);
    if (rlen<2)
        return -1;
    int n_files = btohs(r.vals[0]);
    uint16_t *list = *outlist = calloc(sizeof(uint16_t), n_files);
    void *optr = mempcpy(list, r.vals+1, rlen-2);

    // read rest of packets (if we have a long file list?)
    for (; optr < (void *)(list+n_files); optr += rlen) {
        rlen=EXPECT_BYTES(d, optr);
        if (rlen<0)
            goto fail;
    }

    // fix endianness
    for (int ii=0; ii<n_files; ii++)
        list[ii] = btohs(list[ii]);

    if (EXPECT_uint32(d, d->h->cmd_status, 0) < 0)
        goto fail;

    return n_files;

fail:
    free(list);
    return -1;
}
