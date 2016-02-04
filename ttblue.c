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
#include <sys/wait.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>
#include <bluetooth/l2cap.h>

#include <curl/curl.h>

#include <popt.h>

#include "bbatt.h"
#include "ttops.h"
#include "util.h"

const char *PLEASE_SETCAP_ME =
    "**********************************************************\n"
    "NOTE: This program lacks the permissions necessary for\n"
    "  manipulating the raw Bluetooth HCI socket, which\n"
    "  is required to set the minimum connection inverval\n"
    "  and speed up data transfer.\n\n"
    "  To fix this, run it as root or, better yet, set the\n"
    "  following capabilities on the ttblue executable:\n\n"
    "    # sudo setcap 'cap_net_raw,cap_net_admin+eip' ttblue\n\n"
    "  For gory details, see the BlueZ mailing list:\n"
    "    http://thread.gmane.org/gmane.linux.bluez.kernel/63778\n"
    "**********************************************************\n";

#define BARRAY(...) (const uint8_t[]){ __VA_ARGS__ }
#define GQF_GPS_URL "http://gpsquickfix.services.tomtom.com/fitness/sifgps.f2p3enc.ee?timestamp=%ld"
#define GQF_GLONASS_URL "http://gpsquickfix.services.tomtom.com/fitness/sifglo.f2p3enc.ee?timestamp=%ld"

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

int
save_buf_to_file(const char *filename, const char *mode, const void *fbuf, int length, int indent, int verbose)
{
    char istr[indent+1];
    memset(istr, ' ', indent);
    istr[indent] = 0;
    FILE *f;

    if ((f = fopen(filename, mode)) == NULL) {
        fprintf(stderr, "%sCould not open %s: %s (%d)\n", istr, filename, strerror(errno), errno);
        return -1;
    } else if (fwrite(fbuf, length, 1, f) != 1) {
        fclose(f);
        fprintf(stderr, "%sCould not save to %s: %s (%d)\n", istr, filename, strerror(errno), errno);
        return -2;
    } else {
        fclose(f);
        if (verbose)
            fprintf(stderr, "%sSaved %d bytes to %s\n", istr, length, filename);
        return 0;
    }
}

/****************************************************************************/

int debug=1;
int get_activities=0, set_time=0, update_gps=0, use_glonass=0, version=0, daemonize=0, new_pair=1;
int sleep_success=3600, sleep_fail=10;
uint32_t dev_code;
char *activity_store=".", *dev_address=NULL, *interface=NULL, *postproc=NULL;

struct poptOption options[] = {
    { "auto", 'a', POPT_ARG_NONE, NULL, 'a', "Same as --get-activities --update-gps --set-time --version" },
    { "get-activities", 0, POPT_ARG_NONE, &get_activities, 0, "Downloads and deletes .ttbin activity files from the watch" },
    { "set-time", 0, POPT_ARG_NONE, &set_time, 0, "Set time zone on the watch to match this computer" },
    { "activity-store", 's', POPT_ARG_STRING|POPT_ARGFLAG_SHOW_DEFAULT, &activity_store, 0, "Location to store .ttbin activity files", "PATH" },
    { "post", 'p', POPT_ARG_STRING, &postproc, 0, "Command to run (with .ttbin file as argument) for every activity file", "CMD" },
    { "update-gps", 0, POPT_ARG_NONE, NULL, 'G', "Download TomTom QuickFix update file and send it to the watch (if repeated, forces update even if not needed)" },
    { "glonass", 0, POPT_ARG_VAL, &use_glonass, 2, "Use GLONASS version of QuickFix update file." },
    { "device", 'd', POPT_ARG_STRING, &dev_address, 0, "Bluetooth MAC address of the watch (E4:04:39:__:__:__)", "MACADDR" },
    { "interface", 'i', POPT_ARG_STRING, &interface, 0, "Bluetooth HCI interface to use", "hciX" },
    { "code", 'c', POPT_ARG_INT, &dev_code, 'c', "6-digit pairing code for the watch (if already paired)", "NUMBER" },
    { "version", 'v', POPT_ARG_NONE, &version, 0, "Show watch firmware version and identifiers" },
    { "debug", 'D', POPT_ARG_NONE, 0, 'D', "Increase level of debugging output" },
    { "quiet", 'q', POPT_ARG_VAL, &debug, 0, "Suppress debugging output" },
    { "daemon", 0, POPT_ARG_NONE, &daemonize, 0, "Run as a daemon which will try to connect repeatedly" },
    { "wait-success", 'w', POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT, &sleep_success, 0, "Wait time after successful connection to watch", "SECONDS" },
    { "wait-fail", 'W', POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT, &sleep_fail, 10, "Wait time after failed connection to watch", "SECONDS" },
//    { "no-config", 'C', POPT_ARG_NONE, &config, 0, "Do not load or save settings from ~/.ttblue config file" },
    POPT_AUTOHELP
    POPT_TABLEEND
};

/****************************************************************************/

int main(int argc, const char **argv)
{
    int devid, dd, fd;
    bdaddr_t src_addr, dst_addr;
    int success;
    time_t last_qfg_update;
    int write_delay;

    // parse args
    char ch;
    poptContext optCon = poptGetContext(NULL, argc, argv, options, 0);

    while ((ch=poptGetNextOpt(optCon))>=0) {
        switch (ch) {
        case 'c': new_pair=false; break;
        case 'D': debug++; break;
        case 'a': get_activities = update_gps = set_time = version = true; break;
        case 'G': update_gps++; break;
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

    if (daemonize && new_pair) {
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
        if (!first) {
            term_title("ttblue: Sleeping");
            isleep(success ? sleep_success : sleep_fail, success || (debug>1));
        }
        term_title("ttblue: Connecting...");

        // setup HCI and L2CAP sockets
        dd = hci_open_dev(devid);
        if (dd < 0) {
            fprintf(stderr, "Can't open hci%d: %s (%d)\n", devid, strerror(errno), errno);
            goto preopen_fail;
        }

        // check for BLE support (see hciconfig.c cmd_features from Bluez)
        if (first) {
            uint8_t features[8];
            if (hci_read_local_ext_features(dd, 0, NULL, features, 1000) < 0) {
                fprintf(stderr, "Could not read hci%d features: %s (%d)", dd, strerror(errno), errno);
                goto preopen_fail;
            } else if ((features[4] & LMP_LE) == 0 || (features[6] & LMP_LE_BREDR) == 0) {
                fprintf(stderr, "Bluetooth interface hci%d doesn't support 4.0 (Bluetooth LE+BR/EDR)", dd);
                goto preopen_fail;
            }
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
            if (!daemonize || errno!=ENOTCONN || debug>1)
                fprintf(stderr, "Failed to connect: %s (%d)\n", strerror(errno), errno);
            goto fail;
        }

        // prompt for pairing code
        if (new_pair) {
            fprintf(stderr, "\n**************************************************\n"
                    "Enter 6-digit pairing code shown on device: ");
            if (scanf("%d%c", &dev_code, &ch) && !isspace(ch)) {
                fprintf(stderr, "Pairing code should be 6-digit number.\n");
                goto fail;
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

        do {
            result = hci_le_conn_update(dd, htobs(l2cci.hci_handle),
                                        0x0006 /* min_interval */,
                                        0x0006 /* max_interval */,
                                        0 /* latency */,
                                        200 /* supervision_timeout */,
                                        2000);
        } while (errno==ETIMEDOUT);
        if (result < 0) {
            if (errno==EPERM && first)
                fputs(PLEASE_SETCAP_ME, stderr);
            else {
                perror("hci_le_conn_update");
                goto fail;
            }
        }

        // figure out the maximum speed at which we can send packets to the device from
        // the Preferred Peripheral Connection Parameters
        struct { uint16_t min_interval, max_interval, slave_latency, timeout_mult; } __attribute__((packed)) ppcp;
        if (att_read(fd, 0x000b, &ppcp) < 0) {
            fprintf(stderr, "Could not read device PPCP (handle 0x000b): %s (%d)", strerror(errno), errno);
            goto fail;
        } else {
            ppcp.min_interval = btohs(ppcp.min_interval);
            ppcp.max_interval = btohs(ppcp.max_interval);
            ppcp.slave_latency = btohs(ppcp.slave_latency);
            ppcp.timeout_mult = btohs(ppcp.timeout_mult);
            write_delay = 1250 * ppcp.min_interval; // (microseconds)
            if (debug > 1) {
                fprintf(stderr, "Throttling file write to 1 packet every %d microseconds.\n", write_delay);
                fprintf(stderr, "min_interval=%d, max_interval=%d, slave_latency=%d, timeout_mult=%d\n", ppcp.max_interval, ppcp.min_interval, ppcp.slave_latency, ppcp.timeout_mult);
            }
        }

        // check that it's actually a TomTom device with compatible firmware version
        struct tt_dev_info { uint16_t handle; const char *name; char buf[BT_ATT_DEFAULT_LE_MTU-2]; int len; } info[] = {
            { 0x001e, "maker" },
            { 0x0016, "serial" },
            { 0x0003, "user_name" },
            { 0x0014, "model_name" },
            { 0x001a, "model_num" },
            { 0x001c, "firmware" },
            { 0 }
        };
        for (struct tt_dev_info *p = info; p->handle; p++) {
            p->len = att_read(fd, p->handle, p->buf);
            if (p->len < 0) {
                fprintf(stderr, "Could not read device information (handle 0x%04x, %s): %s (%d)", p->handle, p->name, strerror(errno), errno);
                goto fail;
            }
            p->buf[p->len] = 0;
        }

        if (strcmp(info[0].buf, EXPECTED_MAKER) != 0) {
            fprintf(stderr, "Maker is not %s but '%s', exiting!\n", EXPECTED_MAKER, info[1].buf);
            goto fail;
        } else if (strcmp(info[5].buf, OLDEST_TESTED_FIRMWARE) < 0) {
            fprintf(stderr, "Firmware v%s is too old; at least v%s is required\n"
                            "* TomTom firmware release notes:\n"
                            "\thttp://us.support.tomtom.com/app/release_notes/type/watches\n"
                            "* Use USB cable and ttwatch to update your firmware:\n"
                            "\thttp://github.com/ryanbinns/ttwatch\n",
                            info[5].buf, OLDEST_TESTED_FIRMWARE);
            goto fail;
        }

        if (first && strcmp(info[5].buf, NEWEST_TESTED_FIRMWARE) > 0)
            fprintf(stderr, "WARNING: Firmware v%s has not been tested with ttblue\n"
                            "  Please email dlenski@gmail.com and let me know if it works or not\n",
                            info[5].buf);
        if (first && !IS_TESTED_MODEL(info[4].buf))
            fprintf(stderr, "WARNING: Model number %s has not been tested with ttblue\n"
                            "  Please email dlenski@gmail.com and let me know if it works or not\n",
                            info[4].buf);

        // show device identifiers if --version
        fprintf(stderr, "Connected to %s.\n", info[1].buf);
        if (version && first) {
            for (struct tt_dev_info *p = info; p->handle; p++)
                fprintf(stderr, "  %-10.10s: %s\n", p->name, p->buf);
            int8_t rssi=0;
            if (hci_read_rssi(dd, htobs(l2cci.hci_handle), &rssi, 2000) >= 0)
                fprintf(stderr, "  %-10.10s: %d dB\n", "rssi", rssi);
        }

        // authorize with the device
        if (tt_authorize(fd, dev_code, new_pair) < 0) {
            fprintf(stderr, "Device didn't accept pairing code %d.\n", dev_code);
            goto fail;
        }

        term_title("ttblue: Connected");

        // set timeout to 20 seconds (delete and write operations can be slow)
        struct timeval to = {.tv_sec=20, .tv_usec=0};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &to, sizeof(to));
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &to, sizeof(to));

        // transfer files
        uint8_t *fbuf;
        FILE *f;

        fprintf(stderr, "Setting PHONE menu to '%s'.\n", hciname);
        tt_delete_file(fd, 0x00020002);
        tt_write_file(fd, 0x00020002, false, hciname, strlen(hciname), write_delay);

        if (debug > 1) {
            uint32_t fileno = 0x000f20000;
            fprintf(stderr, "Reading preference file 0x%08x from watch...\n", fileno);
            if ((length=tt_read_file(fd, fileno, 0, &fbuf)) < 0) {
                fprintf(stderr, "WARNING: Could not read preferences file 0x%08x from watch.\n", fileno);
            } else {
                char filetime[16], filename[strlen("12345678_20150101_010101.bin") + 1];
                time_t t = time(NULL);
                struct tm *tmp = localtime(&t);
                strftime(filetime, sizeof filetime, "%Y%m%d_%H%M%S", tmp);
                sprintf(filename, "%08x_%s.xml", fileno, filetime);

                save_buf_to_file(filename, "wxb", fbuf, length, 2, true);
                free(fbuf);
            }
        }

        if (set_time) {
            uint32_t fileno = 0x00850000;
            if ((length = tt_read_file(fd, fileno, 0, &fbuf)) < 0) {
                fprintf(stderr, "WARNING: Could not read settings manifest file 0x%08x from watch!\n", fileno);
            } else {
                // based on ttwatch/libttwatch/libttwatch.h, ttwatch/ttwatch/manifest_definitions.h
                int32_t *watch_timezone = NULL;
                for (int16_t *index = (int16_t*)(fbuf+4); index < (int16_t*)(fbuf+length); index += 3) {
                    if (btohl(*index) == 169) {
                        watch_timezone = (int32_t*)(index + 1);
                        break;
                    }
                }
                if (!watch_timezone) {
                    fprintf(stderr, "WARNING: Could not find watch timezone setting!\n");
                } else {
                    time_t t = time(NULL);
                    struct tm *lt = localtime(&t);

                    if (btohl(*watch_timezone) != lt->tm_gmtoff) {
                        fprintf(stderr, "Changing timezone from UTC%+d to UTC%+ld.\n", btohl(*watch_timezone), lt->tm_gmtoff);
                        *watch_timezone = htobl(lt->tm_gmtoff);
                        tt_delete_file(fd, 0x00850000);
                        tt_write_file(fd, 0x00850000, false, fbuf, length, write_delay);
                        att_write(fd, H_CMD_STATUS, BARRAY(0x05, 0x85, 0x00, 0x00), 4); // update magic?
                    }
                }
                free(fbuf);
            }
        }

        if (update_gps) {
            fputs("Updating QuickFixGPS...\n", stderr);
            term_title("ttblue: Updating QuickFixGPS");

            time_t last_qfg_update = 0;
            uint32_t fileno = 0x00020001;
            if (update_gps > 1) {
                /* forced update */
            } else if ((length=tt_read_file(fd, fileno, 0, &fbuf)) < 0) {
                fprintf(stderr, "WARNING: Could not read GPS status file 0x%08x from watch.\n", fileno);
            } else {
                struct tm tmp = { .tm_sec = 0, .tm_min = 0, .tm_hour = 0, .tm_mday = fbuf[0x05],
                                  .tm_mon = fbuf[0x04]-1, .tm_year = (((int)fbuf[0x02])<<8) + fbuf[0x03] - 1900 };
                last_qfg_update = timegm(&tmp);

                if (debug > 1) {
                    char filetime[16], filename[strlen("12345678_20150101_010101.bin") + 1];
                    time_t t = time(NULL);
                    struct tm *tmp = localtime(&t);
                    strftime(filetime, sizeof filetime, "%Y%m%d_%H%M%S", tmp);
                    sprintf(filename, "%08x_%s.bin", fileno, filetime);
                    save_buf_to_file(filename, "wxb", fbuf, length, 2, true);
                }
                free(fbuf);
            }

            if (time(NULL) - last_qfg_update < 24*3600) {
                fprintf(stderr, "  No update needed, last was less than %ld hours ago\n", (time(NULL) - last_qfg_update)/3600);
            } else {
                if (last_qfg_update)
                    fprintf(stderr, "  Last update was at %.24s.", ctime(&last_qfg_update));

                CURLcode res;
                char curlerr[CURL_ERROR_SIZE];
                CURL *curl = curl_easy_init();
                if (!curl) {
                    fputs("Could not start curl\n", stderr);
                    goto fail;
                } else {
                    char url[128];
                    sprintf(url, use_glonass ? GQF_GLONASS_URL : GQF_GPS_URL, (long)time(NULL));
                    fprintf(stderr, "  Downloading %s\n", url);

                    f = tmpfile();
                    curl_easy_setopt(curl, CURLOPT_URL, url);
                    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fwrite);
                    curl_easy_setopt(curl, CURLOPT_WRITEDATA, f);
                    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curlerr);
                    res = curl_easy_perform(curl);
                    curl_easy_cleanup(curl);
                    if (res != 0) {
                        fprintf(stderr, "WARNING: Download failed: %s\n", curlerr);
                    } else {
                        length = ftell(f);
                        fprintf(stderr, "  Sending update to watch (%d bytes)...\n", length);
                        fseek (f, 0, SEEK_SET);
                        fbuf = malloc(length);
                        if (fread (fbuf, 1, length, f) < length) {
                            fclose(f);
                            free(fbuf);
                            fputs("Could not read QuickFixGPS update.\n", stderr);
                            goto fail;
                        } else {
                            fclose (f);
                            tt_delete_file(fd, 0x00010100);
                            result = tt_write_file(fd, 0x00010100, debug, fbuf, length, write_delay);
                            free(fbuf);
                            if (result < 0) {
                                fputs("Failed to send QuickFixGPS update to watch.\n", stderr);
                                goto fail;
                            } else
                                att_write(fd, H_CMD_STATUS, BARRAY(0x05, 0x01, 0x00, 0x01), 4); // update magic?
                        }
                    }
                }
            }
        }

        if (get_activities) {
            uint16_t *list;
            int n_files = tt_list_sub_files(fd, 0x00910000, &list);
            char filetime[16], filename[strlen(activity_store) + strlen("/12345678_20150101_010101.ttbin") + 1];
            fprintf(stderr, "Found %d activity files on watch.\n", n_files);
            for (int ii=0; ii<n_files; ii++) {
                uint32_t fileno = 0x00910000 + list[ii];

                fprintf(stderr, "  Reading activity file 0x%08X ...\n", fileno);
                term_title("ttblue: Transferring activity %d/%d", ii+1, n_files);
                if ((length = tt_read_file(fd, fileno, debug, &fbuf)) < 0) {
                    fprintf(stderr, "Could not read activity file 0x%08X from watch!\n", fileno);
                    goto fail;
                } else {
                    time_t t = time(NULL);
                    struct tm *tmp = localtime(&t);
                    strftime(filetime, sizeof filetime, "%Y%m%d_%H%M%S", tmp);
                    sprintf(filename, "%s/%08X_%s.ttbin", activity_store, fileno, filetime);

                    int result = save_buf_to_file(filename, "wxb", fbuf, length, 4, true);
                    free(fbuf);
                    if (result < 0)
                        goto fail;
                    else {
                        fprintf(stderr, "    Deleting activity file 0x%08X ...\n", fileno);
                        tt_delete_file(fd, fileno);
                        if (postproc) {
                            fprintf(stderr, "    Postprocessing with %s ...", postproc);
                            fflush(stderr);

                            switch (fork()) {
                            case 0:
                                dup2(1, 2); // redirect stdout to stderr
                                execlp(postproc, postproc, filename, NULL);
                                exit(1); // if exec fails?
                            default:
                                wait(&result);
                                if (result==0)
                                    fputc('\n', stderr);
                                else
                                // Ridiculous syntax but I'm extremely proud of it :-P
                            case -1:
                                    fprintf(stderr, " FAILED\n");
                            }
                        }
                    }
                }
            }
        }

        if (debug > 1) {
            uint32_t fileno = 0x00020005;
            fprintf(stderr, "Reading file 0x%08x from watch...\n", fileno);
            if ((length=tt_read_file(fd, fileno, 0, &fbuf)) < 0) {
                fprintf(stderr, "Could not read file 0x%08x from watch.\n", fileno);
            } else {
                char filetime[16], filename[strlen("12345678_20150101_010101.bin") + 1];
                time_t t = time(NULL);
                struct tm *tmp = localtime(&t);
                strftime(filetime, sizeof filetime, "%Y%m%d_%H%M%S", tmp);
                sprintf(filename, "%08x_%s.bin", fileno, filetime);
                save_buf_to_file(filename, "wxb", fbuf, length, 2, true);
                free(fbuf);
            }
        }

        success = true;
        first = false;
        close(fd);
        hci_close_dev(dd);
        continue;

    fail:
        close(fd);
    preopen_fail:
        hci_close_dev(dd);
        success = false;
        if (first)
            return 1;
    }

    return 0;
}
