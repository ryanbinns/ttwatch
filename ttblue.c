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
#include <ctype.h>

#include <sys/time.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>
#include <bluetooth/l2cap.h>

#include <curl/curl.h>

#include <popt.h>

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

    if (connect(sock, (struct sockaddr *) &dstaddr, sizeof(dstaddr)) < 0) {
        close(sock);
        return -2;
    }

    return sock;
}

/****************************************************************************/

int debug=1;
int get_activities=0, update_gps=0, version=0, daemonize=0, new_pair=1;
uint32_t dev_code;
char *activity_store=".", *dev_address=NULL, *interface=NULL;

struct poptOption options[] = {
    { "auto", 'a', POPT_ARG_NONE, NULL, 'a', "Same as --get-activities --update-gps --version" },
    { "get-activities", 0, POPT_ARG_NONE, &get_activities, 0, "Downloads and deletes .ttbin activity files from the watch" },
    { "activity-store", 's', POPT_ARG_STRING|POPT_ARGFLAG_SHOW_DEFAULT, &activity_store, 0, "Location to store .ttbin activity files", "PATH" },
    { "update-gps", 0, POPT_ARG_NONE, &update_gps, 0, "Download TomTom QuickFixGPS update file and send it to the watch" },
    { "device", 'd', POPT_ARG_STRING, &dev_address, 0, "Bluetooth MAC address of the watch (E4:04:39:__:__:__)", "MACADDR" },
    { "interface", 'i', POPT_ARG_STRING, &interface, 0, "Bluetooth HCI interface to use", "hciX" },
    { "code", 'c', POPT_ARG_INT, &dev_code, 'c', "6-digit pairing code for the watch (if already paired)", "NUMBER" },
    { "daemon", 0, POPT_ARG_NONE, &daemonize, 0, "Run as a daemon which will try to connect every 10 seconds" },
    { "version", 'v', POPT_ARG_NONE, &version, 0, "Show watch firmware version and identifiers" },
    { "debug", 'D', POPT_ARG_NONE, 0, 'D', "Increase level of debugging output" },
    { "quiet", 'q', POPT_ARG_NONE, 0, 'q', "Suppress debugging output" },
//    { "no-config", 'C', POPT_ARG_NONE, &config, 0, "Do not load or save settings from ~/.ttblue config file" },
    POPT_AUTOHELP
    POPT_TABLEEND
};

/****************************************************************************/

int main(int argc, const char **argv)
{
    int devid, dd, fd;
    bdaddr_t src_addr, dst_addr;
    int sleept = 0;

    // parse args
    char ch;
    poptContext optCon = poptGetContext(NULL, argc, argv, options, 0);

    while ((ch=poptGetNextOpt(optCon))>=0) {
        switch (ch) {
        case 'c': new_pair=false; break;
        case 'D': debug++; break;
        case 'q': debug = 0; break;
        case 'a': get_activities = update_gps = version = true; break;
        }
    }
    if (ch<-1) {
        fprintf(stderr, "%s: %s\n",
                poptBadOption(optCon, POPT_BADOPTION_NOALIAS),
                poptStrerror(ch));
        return 2;
    }
    if (dev_address==NULL) {
        fprintf(stderr, "Bluetooth MAC address of device must be specified (-d)\n");
        return 2;
    } else if (str2ba(dev_address, &dst_addr) < 0) {
        fprintf(stderr, "Could not understand Bluetooth device address: %s\n", dev_address);
        return 2;
    }
    if (interface != NULL && (devid = hci_devid(interface)) < 0) {
        fprintf(stderr, "Invalid Bluetooth interface: %s\n", interface);
        return 2;
    } else if ((devid = hci_get_route(NULL)) < 0)
        devid = 0;

    if (daemonize && new_pair)  {
        fprintf(stderr,
                "Daemon mode cannot be used together with initial pairing\n"
                "Please specify existing pairing code, or run this first to pair:\n"
                "\t%s -d %s\n", argv[0], dev_address);
        return 2;
    }

    // prompt user to put device in pairing mode
    if (new_pair) {
        fputs("****************************************************************\n"
              "Please put device in pairing mode (MENU -> PHONE -> PAIR NEW)...\n"
              "****************************************************************\n"
              "Press Enter to continue: ",
              stderr);
        getchar();
        fputs("\n", stderr);
    }

    for (bool first=true; first || daemonize; ) {
        // setup HCI and L2CAP sockets
        dd = hci_open_dev(devid);
        if (dd < 0) {
            fprintf(stderr, "Can't open hci%d: %s (%d)\n", devid, strerror(errno), errno);
            goto preopen_fail;
        }

        // get host name and address
        char hciname[64];
        struct hci_dev_info hci_info;
        if (hci_read_local_name(dd, sizeof(hciname), hciname, 1000) < 0
            || hci_devba(devid, &src_addr) < 0) {
            fprintf(stderr, "Can't get hci%d info: %s (%d)\n", devid, strerror(errno), errno);
            hci_close_dev(dd);
            goto preopen_fail;
        }

        // create L2CAP socket connected to watch
        fd = l2cap_le_att_connect(&src_addr, &dst_addr, BDADDR_LE_RANDOM, BT_SECURITY_MEDIUM, first);
        if (fd < 0) {
            if (!daemonize || errno!=ENOTCONN)
                fprintf(stderr, "Failed to connect: %s (%d)\n", strerror(errno), errno);
            goto fail;
        }

        // prompt for pairing code
        if (new_pair) {
            fprintf(stderr, "\n**************************************************\n"
                    "Enter 6-digit pairing code shown on device: ");
            if (scanf("%d%c", &dev_code, &ch) && !isspace(ch)) {
                fprintf(stderr, "Pairing code should be 6-digit number.\n");
                goto fatal;
            }
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
        for (struct tt_dev_info *p = info; p->handle; p++)
            p->len = att_read(fd, p->handle, p->buf);
        if (version && first) {
            fprintf(stderr, "Device information:\n");
            for (struct tt_dev_info *p = info; p->handle; p++)
                fprintf(stderr, "  %-10.10s: %.*s\n", p->name, p->len, p->buf);
            int8_t rssi=0;
            if (hci_read_rssi(dd, htobs(l2cci.hci_handle), &rssi, 2000) >= 0)
                fprintf(stderr, "  %-10.10s: %d dB\n", "rssi", rssi);
        }

        // check that it's actually a TomTom device
        if (strcmp(info[0].buf, "TomTom Fitness") != 0) {
            fprintf(stderr, "Not a TomTom device, exiting!\n");
            goto fatal;
        }

        // authorize with the device
        if (tt_authorize(fd, dev_code, new_pair) < 0) {
            fprintf(stderr, "Device didn't accept pairing code %d.\n", dev_code);
            goto fatal;
        }

        // set timeout to 20 seconds (delete and write operations can be slow)
        struct timeval to = {.tv_sec=20, .tv_usec=0};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &to, sizeof(to));
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &to, sizeof(to));

        // transfer files
        uint8_t *fbuf;
        FILE *f;

        fprintf(stderr, "Setting PHONE menu to '%s'.\n", hciname);
        tt_delete_file(fd, 0x00020002);
        tt_write_file(fd, 0x00020002, false, hciname, strlen(hciname));

        if (debug > 1) {
            fputs("Reading preferences file 0x000f20000 from watch...\n", stderr);
            if ((length=tt_read_file(fd, 0x00f20000, 0, &fbuf)) < 0) {
                fputs("Could not read preferences file 0x00f20000 from watch.", stderr);
            } else {
                if ((f = fopen("0x00f20000.xml", "wb")) == NULL) {
                    fprintf(stderr, "Could not open 00f20000.xml: %s (%d)\n", strerror(errno), errno);
                } else {
                    if (fwrite(fbuf, 1, length, f) < length)
                        fprintf(stderr, "Could not save to 00f20000.xml: %s (%d)\n", strerror(errno), errno);
                    else
                        fprintf(stderr, "  Saved %d bytes to 00f20000.xml\n", length);
                    fclose(f);
                    free(fbuf);
                }
            }
        }

        if (get_activities) {
            uint16_t *list;
            int n_files = tt_list_sub_files(fd, 0x00910000, &list);
            char filetime[16], filename[strlen(activity_store) + strlen("/0910000_20150101_010101.ttbin") + 1];
            fprintf(stderr, "Found %d activity files on watch.\n", n_files);
            for (int ii=0; ii<n_files; ii++) {
                uint32_t fileno = 0x00910000 + list[ii];

                fprintf(stderr, "  Reading activity file 0x%08X ...\n", fileno);
                if ((length = tt_read_file(fd, fileno, debug, &fbuf)) < 0) {
                    fprintf(stderr, "Could not read activity file 0x%08X from watch!\n", fileno);
                    goto fail;
                } else {
                    time_t t = time(NULL);
                    struct tm *tmp = localtime(&t);
                    strftime(filetime, sizeof filetime, "%Y%m%d_%H%M%S", tmp);
                    sprintf(filename, "%s/%08X_%s.ttbin", activity_store, fileno, filetime);

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
        }

        if (debug > 1) {
            fputs("Reading 00020005.bin from watch...\n", stderr);
            if ((length=tt_read_file(fd, 0x00020005, 0, &fbuf)) < 0) {
                fputs("Could not read file 0x00020005 from watch.", stderr);
            } else {
                if ((f = fopen("00020005.bin", "wb")) == NULL) {
                    fprintf(stderr, "Could not open 00020005.bin: %s (%d)\n", strerror(errno), errno);
                } else {
                    if (fwrite(fbuf, 1, length, f) < length)
                        fprintf(stderr, "Could not save to 00020005.bin: %s (%d)\n", strerror(errno), errno);
                    else
                        fprintf(stderr, "  Saved %d bytes to 00020005.bin\n", length);
                    fclose(f);
                    free(fbuf);
                }
            }

            fputs("Reading 00020001.bin from watch...\n", stderr);
            if ((length=tt_read_file(fd, 0x00020001, 0, &fbuf)) < 0) {
                fputs("Could not read file 0x00020001 from watch.", stderr);
            } else {
                if ((f = fopen("00020001.bin", "wb")) == NULL) {
                    fprintf(stderr, "Could not open 00020001.bin: %s (%d)\n", strerror(errno), errno);
                } else {
                    if (fwrite(fbuf, 1, length, f) < length)
                        fprintf(stderr, "Could not save to 00020001.bin: %s (%d)\n", strerror(errno), errno);
                    else
                        fprintf(stderr, "  Saved %d bytes to 00020001.bin\n", length);
                    fclose(f);
                    free(fbuf);
                }
            }
        }

        if (update_gps) {
            CURLcode res;
            char curlerr[CURL_ERROR_SIZE];
            CURL *curl = curl_easy_init();
            if (!curl) {
                fputs("Could not start curl\n", stderr);
                goto fail;
            } else {
                char url[128]="http://gpsquickfix.services.tomtom.com/fitness/sifgps.f2p3enc.ee?timestamp=";
                sprintf(url+strlen(url), "%ld", (long)time(NULL));
                fprintf(stderr, "Updating QuickFixGPS...\n"
                                "  Downloading %s\n", url);

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
                    if (tt_write_file(fd, 0x00010100, debug, fbuf, length) < 0) {
                        fputs("Failed to send QuickFixGPS update to watch.\n", stderr);
                        goto fail;
                    } else
                        att_write(fd, H_CMD_STATUS, BARRAY(0x05, 0x01, 0x00, 0x01), 4); // update magic?
                }
            }
        }

        fputs("Sleeping for 120 s...\n", stderr);
        sleept = 120;
        first = false;
        goto cleanup;
    fail:
        sleept = 10;
    cleanup:
        close(fd);
    preopen_fail:
        hci_close_dev(dd);
        sleep(sleept);
    }

    return 0;

fatal:
    close(fd);
    hci_close_dev(dd);
    return 1;
}
