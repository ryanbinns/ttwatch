/******************************************************************************\
** ttwatchd.c                                                                 **
** Main implementation file for the TomTom watch linux daemon                 **
\******************************************************************************/

#include "firmware.h"
#include "get_activities.h"
#include "libttwatch.h"
#include "log.h"
#include "options.h"
#include "update_gps.h"
#include "misc.h"
#include "set_time.h"

#include <getopt.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <grp.h>
#include <pwd.h>
#include <unistd.h>
#include "sys/stat.h"
#include "sys/types.h"

/*****************************************************************************/
void daemon_watch_operations(TTWATCH *watch, OPTIONS *options)
{
    char name[32];
    OPTIONS *new_options = copy_options(options);

    /* make a copy of the options, and load any overriding
       ones from the watch-specific conf file */
    if (ttwatch_get_watch_name(watch, name, sizeof(name)) != TTWATCH_NoError)
    {
        char *filename = (char*)malloc(strlen(new_options->activity_store) + 1 + strlen(name) + 1 + 12 + 1);
        sprintf(filename, "%s/%s/ttwatch.conf", new_options->activity_store, name);
        load_conf_file(filename, new_options, LoadDaemonOperations);
        free(filename);
    }

    /* perform the activities the user has requested */
    if (new_options->get_activities)
    {
        uint32_t formats = get_configured_formats(watch);
        if (!new_options->set_formats)
            formats |= new_options->formats;
        do_get_activities(watch, new_options, formats);
    }

    if (new_options->update_gps)
        do_update_gps(watch);

    if (new_options->update_firmware)
        do_update_firmware(watch, 0);

    if (new_options->set_time)
        do_set_time(watch);

    free_options(new_options);
}

/*****************************************************************************/
int hotplug_attach_callback(struct libusb_context *ctx, struct libusb_device *dev,
    libusb_hotplug_event event, void *user_data)
{
    OPTIONS *options = (OPTIONS*)user_data;
    TTWATCH *watch = 0;

    write_log(0, "Watch connected...\n");

    if (ttwatch_open_device(dev, options->select_device ? options->device : 0, &watch) == TTWATCH_NoError)
    {
        daemon_watch_operations(watch, options);

        write_log(0, "Finished watch operations\n");

        ttwatch_close(watch);
    }
    else
        write_log(0, "Watch not processed - does not match user selection\n");
    return 0;
}

/*****************************************************************************/
void daemonise(const char *user)
{
    pid_t pid, sid;
    struct passwd *pwd;
    struct group *grp;
    FILE *file;
    char *group;

    /* we must be run as root */
    if (getuid() != 0)
    {
        write_log(1, "This daemon must be run as root. The --runas=[USER[:GROUP]] parameter can be\n");
        write_log(1, "used to run as an unprivileged user after daemon initialisation\n");
        exit(1);
    }

    /* find the group in the user name */
    group = 0;
    if (user)
        group = (char*)strchr(user, ':');
    if (group)
        *(group++) = 0;

    /* perform the first fork */
    pid = fork();
    if (pid < 0)
        exit(1);
    if (pid > 0)
        exit(0);

    /* create a new session with no controlling terminal */
    sid = setsid();
    if (sid < 0)
        _exit(1);

    /* perform the second fork */
    pid = fork();
    if (pid)
        _exit(0);

    /* chdir to the root directory to prevent issues with removing inuse directories */
    if (chdir("/") < 0)
        _exit(1);

    /* set umask to 0 since we don't know what it was before */
    umask(0);

    /* create the pid file */
    file = fopen("/var/run/ttwatch.pid", "w");
    if (file)
    {
        fprintf(file, "%d\n", getpid());
        fclose(file);
    }

    /* retrieve information about the user we want to run as */
    if (user)
    {
        pwd = getpwnam(user);
        if (!pwd)
        {
            perror("getpwnam");
            _exit(1);
        }
    }
    if (group)
    {
        grp = getgrnam(group);
        if (!grp)
        {
            perror("getgrnam");
            _exit(1);
        }
        /* rewrite the ':' so when we do a ps, we see the original command line... */
        *(group - 1) = ':';
    }

    /* make the log directory... */
    mkdir("/var/log/ttwatch", 0755);
    /* and make our user the owner */
    if (user)
        chown("/var/log/ttwatch", pwd->pw_uid, pwd->pw_gid);

    /* drop privileges to that of the specified user */
    if ((getuid() == 0) && user)
    {
        if (group)
            setgid(grp->gr_gid);
        else
            setgid(pwd->pw_gid);
        setuid(pwd->pw_uid);
    }
}

/*****************************************************************************/
int register_callback(uint32_t product_id, OPTIONS *options)
{
    int result = libusb_hotplug_register_callback(NULL, LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED,
        LIBUSB_HOTPLUG_ENUMERATE, TOMTOM_VENDOR_ID, product_id,
        LIBUSB_HOTPLUG_MATCH_ANY, hotplug_attach_callback, options, NULL);
    if (result)
        write_log(1, "Unable to register hotplug callback: %d\n", result);
    return result;
}

/*****************************************************************************/
void help(char *argv[])
{
    int i;
    write_log(0, "Usage: %s [OPTION]...\n", argv[0]);
    write_log(0, "Daemon for automatic processing when a TomTom GPS watch is attached.\n");
    write_log(0, "\n");
    write_log(0, "Mandatory arguments to long options are mandatory for short options too.\n");
    write_log(0, "  -h, --help                 Print this help\n");
    write_log(0, "  -s, --activity-store=PATH Specify an alternate place for storing\n");
    write_log(0, "                               downloaded ttbin activity files\n");
    write_log(0, "  -a, --auto                 Same as \"--update-fw --update-gps --get-activities --set-time\"\n");
    write_log(0, "  -d, --device=STRING        Specify which device to use (see below)\n");
    write_log(0, "      --get-activities       Downloads and deletes any activity records\n");
    write_log(0, "                               currently stored on the watch\n");
    write_log(0, "      --packets              Displays the packets being sent/received\n");
    write_log(0, "                               to/from the watch. Only used for debugging\n");
    write_log(0, "      --runas=USER[:GROUP]   Run the daemon as the specified user, and\n");
    write_log(0, "                               optionally as the specified group\n");
    write_log(0, "      --set-time             Updates the time on the watch\n");
    write_log(0, "      --update-fw            Checks for available firmware updates from\n");
    write_log(0, "                               Tomtom's website and updates the watch if\n");
    write_log(0, "                               newer firmware is found\n");
    write_log(0, "      --update-gps           Updates the GPSQuickFix data on the watch\n");
    write_log(0, "\n");
    write_log(0, "The --device (-d) option takes a string. The string can match either the\n");
    write_log(0, "serial number or the name of the watch.\n");
    write_log(0, "\n");
    write_log(0, "If --activity-store is not specified, \"~/ttwatch\" is used for storing\n");
    write_log(0, "downloaded activity data, with subdirectories of the watch name and\n");
    write_log(0, "activity date, as per the official TomTom software.\n");
    write_log(0, "\n");
    write_log(0, "The program must be run as root initially, but the --runas parameter can\n");
    write_log(0, "be used to specify a user (and optionally a group) that the program will\n");
    write_log(0, "drop privileges to after it has finished initialising. Without this\n");
    write_log(0, "parameter specified, the program will continue to run as root, which is\n");
    write_log(0, "not recommended for security reasons. Note that this unprivileged user\n");
    write_log(0, "must have access to the USB devices, and write access to the activity\n");
    write_log(0, "store location.\n");
}

/*****************************************************************************/
int main(int argc, char *argv[])
{
    int opt;
    int option_index = 0;
    int result;

    TTWATCH *watch = 0;

    OPTIONS *options = alloc_options();

    /* load the system-wide options */
    load_conf_file("/etc/ttwatch.conf", options, LoadAll);

    struct option long_options[] = {
        { "update-fw",      no_argument,       &options->update_firmware, 1 },
        { "update-gps",     no_argument,       &options->update_gps,      1 },
        { "set-time",       no_argument,       &options->set_time,        1 },
        { "get-activities", no_argument,       &options->get_activities,  1 },
        { "packets",        no_argument,       &options->show_packets,    1 },
        { "runas",          required_argument, 0, 3   },
        { "auto",           no_argument,       0, 'a' },
        { "help",           no_argument,       0, 'h' },
        { "device",         required_argument, 0, 'd' },
        { "activity-store", required_argument, 0, 's' },
        {0}
    };

    /* check the command-line options */
    while ((opt = getopt_long(argc, argv, "ahd:s:", long_options, &option_index)) != -1)
    {
        switch (opt)
        {
        case 3:     /* set daemon user */
            options->run_as = 1;
            if (options->run_as_user)
                free(options->run_as_user);
            options->run_as_user = strdup(optarg);
            break;
        case 'a':   /* auto mode */
            options->update_firmware = 1;
            options->update_gps      = 1;
            options->get_activities  = 1;
            options->set_time        = 1;
            break;
        case 'd':   /* select device */
            options->select_device = 1;
            if (optarg)
            {
                if (options->device)
                    free(options->device);
                options->device = strdup(optarg);
            }
            break;
        case 's':   /* activity store */
            if (optarg)
            {
                if (options->activity_store)
                    free(options->activity_store);
                options->activity_store = strdup(optarg);
            }
            break;
        case 'h': /* help */
            help(argv);
            free_options(options);
            return 0;
        }
    }

    if (optind < argc)
    {
        write_log(0, "Invalid parameter specified: %s\n", argv[optind]);
        free_options(options);
        return 1;
    }

    if (!options->activity_store)
    {
        /* find the user's home directory, either from $HOME or from
           looking at the system password database */
        char *home = getenv("HOME");
        if (!home)
        {
            struct passwd *pwd = getpwuid(getuid());
            home = pwd->pw_dir;
        }
        options->activity_store = (char*)malloc(strlen(home) + 9);
        if (options->activity_store)
            sprintf(options->activity_store, "%s/ttwatch", home);
    }

    /* reload the daemon operations from the config file. We don't load a per-user file */
    load_conf_file("/etc/ttwatch", options, LoadDaemonOperations);

    /* we have to include some useful functions, otherwise there's no point... */
    if (!options->update_firmware && !options->update_gps && !options->get_activities && !options->set_time)
    {
        write_log(1, "You must include one or more of:\n");
        write_log(1, "    --update-fw\n");
        write_log(1, "    --update-gps\n");
        write_log(1, "    --get-activities\n");
        write_log(1, "    --set-time\n");
        write_log(1, "    --auto (OR -a)\n");
        free_options(options);
        return 1;
    }

    /* become a daemon */
    daemonise(options->run_as ? options->run_as_user : NULL);

    /* we're a daemon, so open the log file and report that we have started */
    set_log_location(LOG_VAR_LOG);
    write_log(0, "Starting daemon.\n");

    libusb_init(NULL);

    /* setup hot-plug detection so we know when a watch is plugged in */
    if (!libusb_has_capability(LIBUSB_CAP_HAS_HOTPLUG))
    {
        write_log(0, "System does not support hotplug notification\n");
        libusb_exit(NULL);
        free_options(options);
        _exit(1);
    }

    if (register_callback(TOMTOM_MULTISPORT_PRODUCT_ID, options) ||
        register_callback(TOMTOM_SPARK_CARDIO_PRODUCT_ID, options) ||
        register_callback(TOMTOM_SPARK_MUSIC_PRODUCT_ID, options))
    {
        libusb_exit(NULL);
        free_options(options);
        _exit(1);
    }

    /* infinite loop - handle events every 10 seconds */
    while (1)
    {
        libusb_handle_events_completed(NULL, NULL);
        usleep(10000);
    }

    return 0;   /* should never get here */
}

