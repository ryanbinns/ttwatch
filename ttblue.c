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
    "  is required for scanning and for setting the minimum\n"
    "  connection inverval to speed up data transfer.\n\n"
    "  To fix this, run it as root or, better yet, set the\n"
    "  following capabilities on the ttblue executable:\n\n"
    "    # sudo setcap 'cap_net_raw,cap_net_admin+eip' ttblue\n\n"
    "  For gory details, see the BlueZ mailing list:\n"
    "    http://thread.gmane.org/gmane.linux.bluez.kernel/63778\n"
    "**********************************************************\n";

const char *PAIRING_MODE_PROMPT =
    "****************************************************************\n"
    "Please put device in pairing mode (MENU -> PHONE -> PAIR NEW)...\n"
    "****************************************************************\n"
    "Press Enter to continue: ";

const char *PAIRING_CODE_PROMPT =
    "\n**************************************************\n"
    "Enter 6-digit pairing code shown on device: ";

#define GQF_GPS_URL "https://gpsquickfix.services.tomtom.com/fitness/sifgps.f2p3enc.ee?timestamp=%ld"
#define GQF_GLONASS_URL "https://gpsquickfix.services.tomtom.com/fitness/sifglo.f2p3enc.ee?timestamp=%ld"
// Found an alternate source for the ephemeris file: https://github.com/felixge/node-ar-drone/issues/74#issuecomment-25722745
// This one is nice because the number of days is selectable (3, 7, etc.) although TomTom seems only to
// accept 3-day version
#define GQF_GPS_ALT_URL "https://download.parrot.com/ephemerides/packedDifference.f2p3enc.ee?timestamp=%ld"

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

        fprintf(stderr, "Opening L2CAP LE connection on ATT "
                        "channel:\n\t src: %s\n\tdest: %s (%s)\n",
                srcaddr_str, dstaddr_str, addr_type_name(dst_type));
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

/**
 * based on bluez/tools/hcitool.c
 *
 * If dst is set to all zeros (BDADDRY_ANY), then it returns the
 * first TomTom device address seen, otherwise it waits for the
 * exact matching address.
 *
 */

static void
nullhandler(int signal) {}

static int
hci_tt_scan(int dd, bdaddr_t *dst, uint8_t *dst_type, int verbose)
{
    unsigned char buf[HCI_MAX_EVENT_SIZE];
    struct hci_filter nf, of;
    int len;
    socklen_t olen = sizeof(of);
    char addr_str[18];
    le_advertising_info *info;

    hci_le_set_scan_enable(dd, 0, 0, 10000); // disable in case already enabled
    if (hci_le_set_scan_parameters(dd, /* passive */ 0x00, htobs(0x10), htobs(0x10), LE_PUBLIC_ADDRESS, 0x00, 10000) < 0) {
        fprintf(stderr, "Failed to set BLE scan parameters: %s (%d)\n", strerror(errno), errno);
        return -1;
    }
    if (hci_le_set_scan_enable(dd, 0x01, /* include dupes */ 0x00, 10000) < 0) {
        fprintf(stderr, "Failed to enable BLE scan: %s (%d)\n", strerror(errno), errno);
        return -1;
    }

    // save HCI filter and set it to capture all LE events
    if (getsockopt(dd, SOL_HCI, HCI_FILTER, &of, &olen) < 0)
        return -1;

    hci_filter_clear(&nf);
    hci_filter_set_ptype(HCI_EVENT_PKT, &nf);
    hci_filter_set_event(EVT_LE_META_EVENT, &nf);

    if (setsockopt(dd, SOL_HCI, HCI_FILTER, &nf, sizeof(nf)) < 0)
        return -1;

    struct sigaction sa = { .sa_handler = nullhandler };
    sigaction(SIGINT, &sa, NULL);

    for (;;) {
        if ((len = read(dd, buf, sizeof(buf))) < 0) {
            if (errno == EAGAIN)
                continue;
            else
                goto done;
        }

        evt_le_meta_event *meta = (void *)(buf + HCI_EVENT_HDR_SIZE + 1);
        if (meta->subevent == EVT_LE_ADVERTISING_REPORT) {
            info = (void *)(meta->data + 1);
            ba2str(&info->bdaddr, addr_str);
            if (!strncmp(addr_str, "E4:04:39", 8)) {
                fprintf(stderr, "Saw a TomTom device (%s)    \r", addr_str);
                if (!bacmp(dst, BDADDR_ANY))
                    goto gotcha;
            } else if (verbose)
                fprintf(stderr, "Saw a non-TomTom device (%s)\r", addr_str);
            if (!bacmp(dst, &info->bdaddr))
                goto gotcha;
        }
    }

gotcha:
    /**
     * confusion alert: Bluez defines these constants as
     * BDADDR_LE_RANDOM=0x02 and BDADDR_LE_PUBLIC=0x01,
     * ... but in the le_advertising_info wire packets:
     * 0 means _PUBLIC and non-0 means _RANDOM
     * (see bluez/emulator/bthost.c
     *
     */
    *dst_type = (info->bdaddr_type==0 ? BDADDR_LE_PUBLIC : BDADDR_LE_RANDOM);
    bacpy(dst, &info->bdaddr);

done:
    fputc('\n', stderr);
    signal(SIGINT, NULL);
    if (setsockopt(dd, SOL_HCI, HCI_FILTER, &of, sizeof(of)) < 0)
        return -1;
    if (hci_le_set_scan_enable(dd, 0x00, 1, 10000) < 0)
        return -1;
    if (len < 0)
        return -1;

    return 0;
}

time_t
read_gqf_status(TTDEV *ttd, int debug)
{
    time_t last_update = 0;
    uint8_t *fbuf;
    int length;
    if ((length=tt_read_file(ttd, ttd->files->gps_status, debug, &fbuf)) < 0) {
        fprintf(stderr, "WARNING: Could not read GPS status file 0x%08x from watch.\n", ttd->files->gps_status);
        last_update = -1;
    } else {
#ifdef DUMP_0x00020001
        save_buf_to_file(make_tt_filename(ttd->files->gps_status, "bin"), "wxb", fbuf, length, 2, true);
#endif
        if (length > 6 && (fbuf[0x02] | fbuf[0x03] | fbuf[0x04] | fbuf[0x05]) != 0) {
            struct tm tmp = { .tm_mday = fbuf[0x05], .tm_mon = fbuf[0x04]-1, .tm_year = (((int)fbuf[0x02])<<8) + fbuf[0x03] - 1900 };
            last_update = timegm(&tmp);
        }
        free(fbuf);
    }
    return last_update;
}

const char *
make_tt_filename(uint32_t fileno, char *ext)
{
    char filetime[16];
    static char filename[32];
    time_t t = time(NULL);
    struct tm *tmp = localtime(&t);
    strftime(filetime, sizeof filetime, "%Y%m%d_%H%M%S", tmp);
    sprintf(filename, "%08x_%s.%s", fileno, filetime, ext);
    return filename;
}

static int
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
int get_activities=0, set_time=0, update_gps=0, version=0, daemonize=0, new_pair=1;
int sleep_success=3600, sleep_fail=10;
uint32_t dev_code;
char *activity_store=".", *dev_address=NULL, *interface=NULL, *postproc=NULL, *gqf_url=GQF_GPS_URL;

struct poptOption options[] = {
    { "auto", 'a', POPT_ARG_NONE, NULL, 'a', "Same as --get-activities --update-gps --set-time --version" },
    { "get-activities", 0, POPT_ARG_NONE, &get_activities, 0, "Downloads and deletes .ttbin activity files from the watch" },
    { "set-time", 0, POPT_ARG_NONE, &set_time, 0, "Set time zone on the watch to match this computer" },
    { "activity-store", 's', POPT_ARG_STRING|POPT_ARGFLAG_SHOW_DEFAULT, &activity_store, 0, "Location to store .ttbin activity files", "PATH" },
    { "post", 'p', POPT_ARG_STRING, &postproc, 0, "Command to run (with .ttbin file as argument) for every activity file", "CMD" },
    { "update-gps", 0, POPT_ARG_NONE, NULL, 'G', "Download TomTom QuickFix update file and send it to the watch (if repeated, forces update even if not needed)" },
    { "glonass", 0, POPT_ARG_NONE, NULL, 'g', "Use TomTom's GLONASS version of QuickFix update file." },
    { "qf-url", 0, POPT_ARG_STRING, &gqf_url, 0, "Alternate URL for QuickFix update (ephemeris) file." },
    { "device", 'd', POPT_ARG_STRING, &dev_address, 0, "Bluetooth MAC address of the watch (E4:04:39:__:__:__); will use first TomTom device if unspecified", "MACADDR" },
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
    bdaddr_t src_addr, dst_addr = {0};
    uint8_t dst_bdaddr_type = 0; /* suppress gcc 4.8.x warning; not actual a valid value */
    int needs_reboot = false, success = false;
    int write_delay;
    TTDEV *ttd;

    // parse args
    int ch;
    char dangling;
    poptContext optCon = poptGetContext(NULL, argc, argv, options, 0);

    while ((ch=poptGetNextOpt(optCon))>=0) {
        switch (ch) {
        case 'c': new_pair=false; break;
        case 'D': debug++; break;
        case 'a': get_activities = update_gps = set_time = version = true; break;
        case 'G': update_gps++; break;
        case 'g': gqf_url = GQF_GLONASS_URL; break;
        }
    }
    if (ch<-1) {
        fprintf(stderr, "%s: %s\n\n",
                poptBadOption(optCon, POPT_BADOPTION_NOALIAS),
                poptStrerror(ch));
        poptPrintUsage(optCon, stderr, 0);
        return 2;
    }
    if (dev_address != NULL && str2ba(dev_address, &dst_addr) < 0) {
        fprintf(stderr, "Could not understand Bluetooth device address: %s\n"
                        "It should be a TomTom MAC address like E4:04:39:__:__:__\n\n", dev_address);
        poptPrintUsage(optCon, stderr, 0);
        return 2;
    }
    if (interface != NULL && (devid = hci_devid(interface)) < 0) {
        fprintf(stderr, "Invalid Bluetooth interface: %s\n\n", interface);
        poptPrintUsage(optCon, stderr, 0);
        return 2;
    } else if ((devid = hci_get_route(NULL)) < 0)
        devid = 0;

    if (daemonize && (new_pair || !dev_address)) {
        fprintf(stderr,
                "Daemon mode cannot be used together with initial pairing,\n"
                "and Bluetooth device address must be specified.\n"
                "Please specify pairing code (-c) and device address (-d).\n\n");
        poptPrintUsage(optCon, stderr, 0);
        return 2;
    }

    // get hostname
    char hostname[32];
    gethostname(hostname, sizeof hostname);

    // prompt user to put device in pairing mode
    if (new_pair) {
        fputs(PAIRING_MODE_PROMPT, stderr);
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
            goto pre_fatal;
        }

        // check for BLE support (see hciconfig.c cmd_features from Bluez)
        if (first) {
            uint8_t features[8];
            if (hci_read_local_ext_features(dd, 0, NULL, features, 1000) < 0) {
                fprintf(stderr, "Could not read hci%d features: %s (%d)", devid, strerror(errno), errno);
                goto pre_fatal;
            } else if ((features[4] & LMP_LE) == 0 || (features[6] & LMP_LE_BREDR) == 0) {
                fprintf(stderr, "Bluetooth interface hci%d doesn't support 4.0 (Bluetooth LE+BR/EDR)", devid);
                goto pre_fatal;
            }
        }

        // get host Bluetooth address
        if (hci_devba(devid, &src_addr) < 0) {
            fprintf(stderr, "Can't get hci%d info: %s (%d)\n", devid, strerror(errno), errno);
            goto pre_fatal;
        }

        // scan for TomTom devices
        if (dev_address)
            fprintf(stderr, "Scanning for TomTom BLE device %s...\n", dev_address);
        else
            fprintf(stderr, "Scanning for TomTom BLE devices...\n");

        if (hci_tt_scan(dd, &dst_addr, &dst_bdaddr_type, debug) < 0) {
            if (errno==EPERM)
                fputs(PLEASE_SETCAP_ME, stderr);
            else
                fprintf(stderr, "BLE scan failed: %s (%d)\n", strerror(errno), errno);
            goto pre_fatal;
        }

        // create L2CAP socket connected to watch
        fd = l2cap_le_att_connect(&src_addr, &dst_addr, dst_bdaddr_type, BT_SECURITY_MEDIUM, debug>1);
        if (fd < 0) {
            if (errno!=ENOTCONN || debug>1)
                fprintf(stderr, "Failed to connect: %s (%d)\n", strerror(errno), errno);
            goto fail_connect;
        }

        // initialize device
        ttd = tt_device_init(dst_bdaddr_type==BDADDR_LE_RANDOM ? 1 : 2, fd);
        if (!ttd)
            goto fatal;

        // we need the hci_handle too
        struct l2cap_conninfo l2cci;
        socklen_t sl = sizeof l2cci;
        int result = getsockopt(fd, SOL_L2CAP, L2CAP_CONNINFO, &l2cci, &sl);
        if (result < 0) {
            perror("getsockopt");
            goto fail;
        }

        time_t now = time(NULL);
        fprintf(stderr, "Connected to v%d device at %.24s.\n", ttd->protocol_version, ctime(&now));

        if (ttd->h->ppcp != 0) {
            // request minimum connection interval
            do {
                result = hci_le_conn_update(dd, htobs(l2cci.hci_handle),
                                            0x0006 /* min_interval */,
                                            0x0006 /* max_interval */,
                                            0 /* latency */,
                                            200 /* supervision_timeout */,
                                            2000);
            } while (errno==ETIMEDOUT);
            if (result < 0) {
                if (errno==EPERM && first) {
                    fputs(PLEASE_SETCAP_ME, stderr);
                    write_delay = 0; // we couldn't speed up the connection, so no write_delay
                } else {
                    perror("hci_le_conn_update");
                    goto fail;
                }
            } else {
                // we successfully decreased the connection interval, but the device can't
                // actually handle packets being written to it this quickly, so we
                // figure out the maximum safe speed at which we can send packets to the device from
                // the Preferred Peripheral Connection Parameters
                struct { uint16_t min_interval, max_interval, slave_latency, timeout_mult; } __attribute__((packed)) ppcp;
                if (att_read(ttd->fd, ttd->h->ppcp, &ppcp) < 0) {
                    fprintf(stderr, "Could not read device PPCP (handle 0x%04x): %s (%d)", ttd->h->ppcp, strerror(errno), errno);
                    if (first) goto fatal; else goto fail;
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
            }
        } else {
            // With no attempt to speed up the connection, we shouldn't need any write_delay
            write_delay = 0;
        }

        // check that it's actually a TomTom device with compatible firmware version
        struct ble_dev_info *info = tt_check_device_version(ttd, first);
        if (!info) {
            if (first) goto fatal; else goto fail;
        }

        // show device identifiers if --version
        if (version && first) {
            for (struct ble_dev_info *p = info; p->handle; p++)
                fprintf(stderr, "  %-10.10s: %s\n", p->name, p->buf);
        }

        // prompt for pairing code
        if (new_pair) {
            fputs(PAIRING_CODE_PROMPT, stderr);
            if (!(scanf("%d%c", &dev_code, &dangling) && isspace(dangling))) {
                fprintf(stderr, "Pairing code should be 6-digit number.\n");
                goto fatal;
            }
        }

        // authorize with the device
        if (tt_authorize(ttd, dev_code, new_pair) < 0) {
            fprintf(stderr, "Device didn't accept pairing code %d.\n", dev_code);
            if (first) goto fatal; else goto fail;
        }

        term_title("ttblue: Connected");

        // set timeout to 20 seconds (delete and write operations can be slow)
        struct timeval to = {.tv_sec=20, .tv_usec=0};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &to, sizeof(to));

        // transfer files
        uint8_t *fbuf;
        FILE *f;
        int length;

        fprintf(stderr, "Setting PHONE menu to '%s'.\n", hostname);
        tt_delete_file(ttd, ttd->files->hostname);
        tt_write_file(ttd, ttd->files->hostname, false, (uint8_t*)hostname, strlen(hostname), write_delay);

#ifdef DUMP_0x000f20000
        fprintf(stderr, "Reading preference file 0x%08x from watch...\n", ttd->files->preference);
        if ((length=tt_read_file(ttd, ttd->files->preference, debug, &fbuf)) < 0) {
            fprintf(stderr, "WARNING: Could not read preferences file 0x%08x from watch.\n", ttd->files->preference);
        } else {
            save_buf_to_file(make_tt_filename(ttd->files->preference, "xml"), "wxb", fbuf, length, 2, true);
            free(fbuf);
        }
#endif

        if (set_time) {
            fprintf(stderr, "Checking watch settings manifest file 0x%08x...\n", ttd->files->manifest);
            if ((length = tt_read_file(ttd, ttd->files->manifest, debug, &fbuf)) < 0) {
                fprintf(stderr, "WARNING: Could not read settings manifest file 0x%08x from watch!\n", ttd->files->manifest);
            } else {
                // based on: https://github.com/ryanbinns/ttwatch/tree/master/manifest
                // the position of the UTC-offset in the manifest is 169 in all known firmware versions, so lazily hard-coded here for now
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
                        fprintf(stderr, "  Changing timezone from UTC%+d to UTC%+ld.\n", btohl(*watch_timezone), lt->tm_gmtoff);
                        *watch_timezone = htobl(lt->tm_gmtoff);
                        tt_delete_file(ttd, ttd->files->manifest);
                        tt_write_file(ttd, ttd->files->manifest, false, fbuf, length, write_delay);
                        needs_reboot = true;
                    }
                }
                free(fbuf);
            }
        }

        if (get_activities) {
            uint16_t *list;
            int n_files = tt_list_sub_files(ttd, ttd->files->activity_start, &list);

            if (n_files < 0) {
                fprintf(stderr, "Could not list activity files on watch!\n");
                goto fail;
            }
            fprintf(stderr, "Found %d activity files on watch.\n", n_files);
            for (int ii=0; ii<n_files; ii++) {
                uint32_t fileno = ttd->files->activity_start + list[ii];

                fprintf(stderr, "  Reading activity file 0x%08X ...\n", fileno);
                term_title("ttblue: Transferring activity %d/%d", ii+1, n_files);
                if ((length = tt_read_file(ttd, fileno, debug, &fbuf)) < 0) {
                    fprintf(stderr, "Could not read activity file 0x%08X from watch!\n", fileno);
                    goto fail;
                } else {
                    char filename[strlen(activity_store) + strlen("/12345678_20150101_010101.ttbin") + 1];
                    sprintf(filename, "%s/%s", activity_store, make_tt_filename(fileno, "ttbin"));

                    int result = save_buf_to_file(filename, "wxb", fbuf, length, 4, true);
                    free(fbuf);
                    if (result < 0)
                        goto fail;
                    else {
                        if (postproc) {
                            fprintf(stderr, "    Postprocessing with %s ...\n", postproc);
                            fflush(stderr);

                            switch (fork()) {
                            case 0:
                                dup2(1, 2); // redirect stdout to stderr
                                execlp(postproc, postproc, filename, NULL);
                                exit(1); // if exec fails?
                            case -1:
                                fprintf(stderr, "Could not fork: %s (%d)\n", strerror(errno), errno);
                                goto fatal;
                            }
                        }
                        fprintf(stderr, "    Deleting activity file 0x%08X ...\n", fileno);
                        tt_delete_file(ttd, fileno);
                    }
                }
            }
        }

        if (update_gps) {
            fputs("Updating QuickFixGPS...\n", stderr);
            term_title("ttblue: Updating QuickFixGPS");

            time_t last_gqf_update = read_gqf_status(ttd, debug-1);
            if (update_gps <= 1 && time(NULL) - last_gqf_update < 24*3600) {
                fprintf(stderr, "  No GPS update needed, last was less than %ld hours ago\n", (time(NULL) - last_gqf_update)/3600);
            } else {
                if (last_gqf_update != -1 && last_gqf_update != 0)
                    fprintf(stderr, "  Last GPS update was at %.24s.\n", ctime(&last_gqf_update));
                else
                    fprintf(stderr, "  Last GPS update unknown.\n");

                CURLcode res;
                char curlerr[CURL_ERROR_SIZE];
                CURL *curl = curl_easy_init();
                if (!curl) {
                    fputs("Could not start curl\n", stderr);
                    goto fail;
                } else {
                    char url[128];
                    sprintf(url, gqf_url, (long)time(NULL));
                    fprintf(stderr, "  Downloading %s\n", url);

                    f = tmpfile();
                    curl_easy_setopt(curl, CURLOPT_URL, url);
                    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fwrite);
                    curl_easy_setopt(curl, CURLOPT_WRITEDATA, f);
                    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curlerr);
                    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10); // connection phase
                    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60);        // transfer phase
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
                            tt_delete_file(ttd, ttd->files->quickgps);
                            result = tt_write_file(ttd, ttd->files->quickgps, debug, fbuf, length, write_delay);
                            free(fbuf);
                            if (result < 0) {
                                fputs("Failed to send QuickFixGPS update to watch.\n", stderr);
                                goto fail;
                            } else {
                                // official TomTom Android app seems to only issue this
                                // "magic" update command when the GPS is brand new or
                                // after a factory reset, or with 3x --update-gps
                                att_wrreq(ttd->fd, ttd->h->cmd_status, BARRAY(0x05, 0x01, 0x00, 0x01), 4);

                                time_t last_gqf_update = read_gqf_status(ttd, debug-1);
                                if (last_gqf_update != -1 && last_gqf_update != 0)
                                    fprintf(stderr, "  Last GPS update is now %.24s.\n", ctime(&last_gqf_update));
                                else
                                    fprintf(stderr, "  Could not re-read GPS update time.\n");
                            }
                        }
                    }
                }
            }
        }

#ifdef DUMP_0x00020005
        if (debug > 1) {
            uint32_t fileno = 0x00020005;
            fprintf(stderr, "Reading file 0x%08x from watch...\n", fileno);
            if ((length=tt_read_file(ttd, fileno, debug, &fbuf)) < 0) {
                fprintf(stderr, "Could not read file 0x%08x from watch.\n", fileno);
            } else {
                save_buf_to_file(make_tt_filename(fileno, "bin"), "wxb", fbuf, length, 2, true);
                free(fbuf);
            }
        }
#endif

        success = true;
        if(needs_reboot) {
            fprintf(stderr, "Rebooting watch...\n");
            tt_reboot(ttd);
        }
        first = false;
        needs_reboot = false;
        tt_device_done(ttd);
        close(fd);
        hci_close_dev(dd);
        continue;
    fail:
        tt_device_done(ttd);
    fail_connect:
        close(fd);
        hci_close_dev(dd);
        success = false;
        fprintf(stderr, "Communication with watch failed...\n");

        // wait for any child processes to finish
        int child_status;
        pid_t child_pid;
        while ((child_pid = wait(&child_status)) >= 0)
            if (child_status != 0)
                fprintf(stderr, "WARNING: postprocess failed (pid %d, status %d)\n", child_pid, child_status);
    }

    return 0;

fatal:
    close(fd);
pre_fatal:
    hci_close_dev(dd);
    fprintf(stderr, "Fatal error, exiting.\n");
    return 1;
}
