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

#include <curl/curl.h>

#include "bbatt.h"
#include "ttops.h"

#define BARRAY(...) (const uint8_t[]){ __VA_ARGS__ }

/**
 * taken from bluez/tools/btgatt-client.c
 *
 */

#define ATT_CID 4
static int l2cap_le_att_connect(bdaddr_t *src, bdaddr_t *dst, uint8_t dst_type,
                                int sec, int verbose)
{
    int sock, result;
    struct sockaddr_l2 srcaddr, dstaddr;
    struct bt_security btsec;

    if (verbose) {
        char srcaddr_str[18], dstaddr_str[18];

        ba2str(src, srcaddr_str);
        ba2str(dst, dstaddr_str);

        fprintf(stderr, "Opening L2CAP LE connection on ATT "
                        "channel:\n\t src: %s\n\tdest: %s\n",
                srcaddr_str, dstaddr_str);
    }

    sock = socket(PF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_L2CAP);
    if (sock < 0) {
        fprintf(stderr, "Failed to create L2CAP socket: %s (%d)\n", strerror(errno), errno);
        return -1;
    }

    /* Set up source address */
    memset(&srcaddr, 0, sizeof(srcaddr));
    srcaddr.l2_family = AF_BLUETOOTH;
    srcaddr.l2_cid = htobs(ATT_CID);
    srcaddr.l2_bdaddr_type = 0;
    bacpy(&srcaddr.l2_bdaddr, src);

    if (bind(sock, (struct sockaddr *)&srcaddr, sizeof(srcaddr)) < 0) {
        fprintf(stderr, "Failed to bind L2CAP socket: %s (%d)\n", strerror(errno), errno);
        close(sock);
        return -1;
    }

    /* Set the security level */
    memset(&btsec, 0, sizeof(btsec));
    btsec.level = sec;
    if (setsockopt(sock, SOL_BLUETOOTH, BT_SECURITY, &btsec,
                            sizeof(btsec)) != 0) {
        fprintf(stderr, "Failed to set L2CAP security level: %s (%d)\n", strerror(errno), errno);
        close(sock);
        return -1;
    }

    /* Set up destination address */
    memset(&dstaddr, 0, sizeof(dstaddr));
    dstaddr.l2_family = AF_BLUETOOTH;
    dstaddr.l2_cid = htobs(ATT_CID);
    dstaddr.l2_bdaddr_type = dst_type;
    bacpy(&dstaddr.l2_bdaddr, dst);

    fputs("Connecting to device...", stderr);
    fflush(stdout);

    if (connect(sock, (struct sockaddr *) &dstaddr, sizeof(dstaddr)) < 0) {
        fprintf(stderr, " Failed to connect: %s (%d)\n", strerror(errno), errno);
        close(sock);
        return -1;
    }

    fprintf(stderr, " Done\n");

    return sock;
}

/****************************************************************************/

int main(int argc, const char **argv)
{
    int devid, dd, fd;
    char ch;

    // parse args
    if (argc!=3) {
        fputs("Need two arguments:\n"
              "          ttblue <bluetooth-address> <pairing-code>\n"
              "    OR    ttblue <bluetooth-address> pair\n\n"
              "Where bluetooth-address is the twelve-digit address\n"
              "of your TomTom GPS watch (E4:04:39:__:__:__) and\n"
              "pairing-code is either the previously established\n"
              "code used to pair a phone, or the string \"pair\"\n"
              "to create a new pairing.\n", stderr);
        return 1;
    }

    uint32_t code;
    bdaddr_t dst_addr;
    if (str2ba(argv[1], &dst_addr) < 0) {
        fprintf(stderr, "could not understand Bluetooth device address: %s\n", argv[1]);
        goto preopen_fail;
    }
    if (!strcasecmp(argv[2], "pair")) {
        code = 0xffff;
    } else if (sscanf(argv[2], "%6d%c", &code, &ch)!=1) {
        fprintf(stderr, "Pairing code should be 6-digit number, not %s\n", argv[2]);
        goto preopen_fail;
    }

    // prompt user to put device in pairing mode
    if (code == 0xffff)
        fputs("****************************************************************\n"
              "Please put device in pairing mode (MENU -> PHONE -> PAIR NEW)...\n"
              "****************************************************************\n\n",
              stderr);

    // setup HCI and L2CAP sockets
    devid = hci_get_route(NULL);
    dd = hci_open_dev(devid);
    if (dd < 0) {
        fprintf(stderr, "Can't open hci%d: %s (%d)\n", devid, strerror(errno), errno);
        goto preopen_fail;
    }

    // get host name
    char hciname[64];
    if (hci_read_local_name(dd, sizeof(hciname), hciname, 1000) < 0) {
        fprintf(stderr, "Can't get hci%d name: %s (%d)\n", devid, strerror(errno), errno);
        hci_close_dev(dd);
        goto preopen_fail;
    }

    // create L2CAP socket
    fd = l2cap_le_att_connect(BDADDR_ANY, &dst_addr, BDADDR_LE_RANDOM, BT_SECURITY_MEDIUM, true);
    if (fd < 0) {
        goto fail;
    }

    // request minimum connection interval
    struct l2cap_conninfo l2cci;
    int length = sizeof l2cci;
    int result = getsockopt(fd, SOL_L2CAP, L2CAP_CONNINFO, &l2cci, &length);
    if (result < 0) {
        perror("getsockopt");
        goto fail;
    }

    result = hci_le_conn_update(dd, htobs(l2cci.hci_handle),
                                0x0006 /* min_interval */,
                                0x0006 /* max_interval */,
                                0 /* latency */,
                                200 /* supervision_timeout */,
                                2000);
    if (result < 0) {
        if (errno==EPERM) {
            fputs("**********************************************************\n"
                  "NOTE: This program lacks the permissions necessary for\n"
                  "  manipulating the raw Bluetooth HCI socket, which\n"
                  "  is required to set the minimum connection inverval\n"
                  "  and speed up data transfer.\n\n"
                  "  To fix this, run it as root or, better yet, set the\n"
                  "  following capabilities on the ttblue executable:\n\n"
                  "    # sudo setcap 'cap_net_raw,cap_net_admin+eip' ttblue\n\n"
                  "  For gory details, see the BlueZ mailing list:\n"
                  "    http://thread.gmane.org/gmane.linux.bluez.kernel/63778\n"
                  "**********************************************************\n",
                  stderr);
        } else {
            perror("hci_le_conn_update");
            goto fail;
        }
    }

    // show device identifiers
    struct tt_dev_info { uint16_t handle; const char *name; char buf[BT_ATT_DEFAULT_LE_MTU]; int len; } info[] = {
        { 0x001e, "maker" },
        { 0x0014, "model_name" },
        { 0x001a, "model_num" },
        { 0x001c, "firmware" },
        { 0x0016, "serial" },
        { 0x0003, "user_name" },
        { 0 }
    };
    fprintf(stderr, "\nConnected device information:\n");
    for (struct tt_dev_info *p = info; p->handle; p++) {
        p->len = att_read(fd, p->handle, p->buf);
        fprintf(stderr, "  %-10.10s: %.*s\n", p->name, p->len, p->buf);
        int8_t rssi=0;
        if (hci_read_rssi(dd, htobs(l2cci.hci_handle), &rssi, 2000) >= 0)
            fprintf(stderr, "  %-10.10s: %d dB\n", "rssi", rssi);
    }

    // check that it's actually a TomTom device
    if (strcmp(info[0].buf, "TomTom Fitness") != 0) {
        fprintf(stderr, "Not a TomTom device, exiting!\n");
        goto fail;
    }

    // authorize with the device
    const uint16_t auth_one = btohs(0x0001);
    const uint8_t auth_magic[] = { 0x01, 0x13, 0, 0, 0x01, 0x12, 0, 0 };
    if (code != 0xffff) {
        uint32_t bcode = htobl(code);
        att_write(fd, 0x0033, &auth_one, sizeof auth_one);
        att_wrreq(fd, H_MAGIC, auth_magic, sizeof auth_magic);
        att_write(fd, 0x0026, &auth_one, sizeof auth_one);
        att_wrreq(fd, H_PASSCODE, &bcode, sizeof bcode);
    } else {
        fprintf(stderr, "\n**************************************************\n"
                        "Enter 6-digit pairing code shown on device: ");
        if (scanf("%d%c", &code, &ch) != 1) {
            fprintf(stderr, "Pairing code should be 6-digit number.\n");
            goto fail;
        }
        uint32_t bcode = htobl(code);
        att_write(fd, 0x0033, &auth_one, sizeof auth_one);
        att_write(fd, 0x0026, &auth_one, sizeof auth_one);
        att_write(fd, 0x0029, &auth_one, sizeof auth_one);
        att_write(fd, 0x003c, &auth_one, sizeof auth_one);
        att_write(fd, 0x002c, &auth_one, sizeof auth_one);
        att_wrreq(fd, H_MAGIC, auth_magic, sizeof auth_magic);
        att_wrreq(fd, H_PASSCODE, &bcode, sizeof bcode);
    }
    if (EXPECT_uint8(fd, H_PASSCODE, 1) < 0) {
        fprintf(stderr, "Device didn't accept pairing code %d.\n", code);
        goto fail;
    }

    // set timeout to 20 seconds (delete and write operations can be slow)
    struct timeval to = {.tv_sec=20, .tv_usec=0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &to, sizeof(to));
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &to, sizeof(to));

    // transfer files
    uint8_t *fbuf;
    FILE *f;

    fprintf(stderr, "\nSetting PHONE menu to '%s'.\n", hciname);
    tt_delete_file(fd, 0x00020002);
    tt_write_file(fd, 0x00020002, false, hciname, strlen(hciname));

    uint16_t *list;
    int n_files = tt_list_sub_files(fd, 0x00910000, &list);
    fprintf(stderr, "\nFound %d activity files on watch.\n", n_files);
    for (int ii=0; ii<n_files; ii++) {
        uint32_t fileno = 0x00910000 + list[ii];

        fprintf(stderr, "  Reading activity file 0x%08X ...\n", fileno);
        if ((length = tt_read_file(fd, fileno, 1, &fbuf)) < 0) {
            fprintf(stderr, "Could not read file 0x%08X on watch!\n", fileno);
            goto fail;
        } else {
            char filename[32], filetime[16];
            time_t t = time(NULL);
            struct tm *tmp = localtime(&t);
            strftime(filetime, sizeof filetime, "%Y%m%d_%H%M%S", tmp);
            sprintf(filename, "%08X_%s.ttbin", fileno, filetime);

            if ((f = fopen(filename, "wxb")) == NULL) {
                fprintf(stderr, "Could not open %s: %s (%d)\n", filename, strerror(errno), errno);
                goto fail;
            } else {
                if (fwrite(fbuf, 1, length, f) < length) {
                    fclose(f);
                    fprintf(stderr, "Could not save to %s: %s (%d)\n", filename, strerror(errno), errno);
                    goto fail;
                } else {
                    fclose(f);
                    free(fbuf);
                    fprintf(stderr, "    Saved %d bytes to %s\n", length, filename);
                    fprintf(stderr, "    Deleting activity file 0x%08X ...\n", fileno);
                    tt_delete_file(fd, fileno);
                }
            }
        }
    }

    CURLcode res;
    char curlerr[CURL_ERROR_SIZE];
    CURL *curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "Could not start curl");
        goto fail;
    } else {
        char url[128]="http://gpsquickfix.services.tomtom.com/fitness/sifgps.f2p3enc.ee?timestamp=";
        sprintf(url+strlen(url), "%ld", (long)time(NULL));
        fprintf(stderr, "\nUpdating QuickFixGPS...\n  Downloading %s\n", url);

        f = tmpfile();
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fwrite);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, f);
        curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curlerr);
        res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
        if (res != 0) {
            fprintf(stderr, "Download failed: %s\n", curlerr);
        } else {
            length = ftell(f);
            fprintf(stderr, "  Sending update to watch (%d bytes)...\n", length);
            fseek (f, 0, SEEK_SET);
            fbuf = malloc(length);
            if (fread (fbuf, 1, length, f) < length)
                goto fail;
            fclose (f);

            tt_delete_file(fd, 0x00010100);
            if (tt_write_file(fd, 0x00010100, 1, fbuf, length) < 0)
                fputs("Failed to send QuickFixGPS update to watch.\n", stderr);
            else
                att_write(fd, H_CMD_STATUS, BARRAY(0x05, 0x01, 0x00, 0x01), 4); // update magic?
        }
    }

    close(fd);
    return 0;

fail:
    hci_close_dev(dd);
    close(fd);
preopen_fail:
    return 1;
}
