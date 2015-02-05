ttwatch
=======

Linux TomTom GPS Watch Utilities

Provides programs for communicating with TomTom GPS watches and processing
the data they collect.

1. ttwatch - USB communications program for performing various operations
             with the watch, including downloading activity data, updating
             GPS data, and updating firmware.
2. ttbincnv - Post-processor allowing conversion of the ttbin file formats
              to either (currently) csv, gpx, kml or tcx  files, using broadly
              similar formats to the official TomTom file formats.
3. ttbinmod - Post-processor allowing modifications to be made to the ttbin
              file. Currently, adding/modifying lap markers and truncating the
              file at the end of the workout (last lap, goal completion etc)
              are supported.
4. ttbin2mysports.sh - script that enabled uploading to a MapMyFitness account
                       that is linked to a MySports account. Automatically
                       converts the ttbin file to a TCX file before uploading.

System Requirements
===================

This program requires the following libraries to be compiled and installed
before attempting to build it.

1. openssl (tested against version 1.0.1f, other versions may work).
   Available from http://www.openssl.org, or with your linux distribution
1. curl (tested against version 7.38.0, other versions may work).
   Available from http://curl.haxx.se/download.html
2. libusb 1.0.16 or later (tested against version 1.0.19).
   Available from http://sourceforge.net/projects/libusb/

Build Instructions
==================

```
$ ./configure
$ make
$ sudo make install
```

Setup for unprivileged access
=============================

In order to have permission to access the USB devices when running as anyone
other than root, a udev rule must be set up to allow access for unprivileged
users. The rule I have set up is:

```
$ cat /etc/udev/rules.d/99-tomtom.rules
SUBSYSTEMS=="usb", ATTRS{idVendor}=="1390", ATTRS{idProduct}=="7474", SYMLINK+="tomtom", GROUP="usb", MODE="660"
```

This basically gives access to USB devices to members of the "usb" group.
Create the "usb" group and add yourself to it using:

```
$ sudo addgroup usb
$ sudo usermod -a -Gusb <your_username>
```

Note: If you leave out the -a option on usermod, you will remove your user
      from every group except "usb", rather than just adding "usb" to the
      list of groups the user belongs to, so be careful...
Note: You will have to log out and then log back in to see the change in
      group membership.

Daemon Mode
===========

The ttwatch program supports running as a daemon, which will wait for a watch
to be connected, then automatically perform whichever operations are specified
on the command line. The following four operations are supported, and at least
one of them must be specified to start the daemon:

1. `--get-activities`: Download the activity files and store them, including
   converting them to other file formats as specified in the watch preferences
   downloaded from the watch.
2. `--update-gps`: Updates the GPSQuickFix information in the watch from the
   internet.
3. `--update-fw`: Checks for firmware updates, and updates the firmware in the
   watch if newer firmware is found.
4. `--set-time`: Sets the time on the watch to match the local system time.

All four options can be specified with the `-a` (or `--auto`) option

The daemon must be started as root (run by init or sudo), but the `--runas`
parameter can be specified to provide an alternative user (and optionally
a group - such as the usb group mentioned above) to run as.

Multiple Watches
================

The ttwatch program has support for multiple watches. When running from the
command line a list of available watches can be displayed using the `--devices`
option. A particular watch can be selected using the `-d` option with three
different parameters possible:

1. a 0-based index into the device list
2. a string that matches the watch serial number
3. a string that matches the watch name

All three pieces of information are displayed when listing available watches
with the `--devices` option.

When running as a daemon the device cannot be specifed by index, as the list
of devices will change over time, so this index is meaningless. However, if
the watch serial number or name are specified, the daemon will only process
that particular watch. This can be used to store the activities from multiple
watches in different users' home areas by starting multiple instances of the
daemon running as different users, specifying different watches.

Unsafe Functions
================

There are various options that can be given to the ttwatch program that read
and write raw data to/from the watch. Used incorrectly, these could destroy
the contents of the watch. For this reason, they are disabled by default. To
enable these options, run `configure` with the `--with-unsafe` option. Note that
I don't guarantee what will happen if you use these options without really
knowing what you are doing.

Config Files
============

The `ttwatch` program supports loading some settings from config files. Three
config files can be used: global, per-user, and per-watch. They are located
in the following locations:

1. `/etc/ttwatch.conf`
2. `~/.ttwatch`
3. `[activity-store-location]/[watch-name]/ttwatch.conf`

This means that some settings can be overridden by specific users or by which
watch is being used. Note that the per-watch settings are used either by the
daemon (when a watch is connected), or when downloading activities from the
command-line, not for any other operations. The per-user config file is not
used when being run as root.

The config files are very simple, and are just lines in a "option = value"
format. '#' is used to denote a comment; anything after a '#' is ignored.
Applicable options (*not* case sensitive) and their values are as follows:

1. ActivityStore: specifies an absolute path to the place where activities
                  are stored. Relative paths (and paths such as ~) cannot be
                  used. This is a string value.
2. PostProcessor: specifies a script or executable that is executed for every
                  activity that is downloaded from the watch, with the
                  filename of the ttbin file as the only argument. The
                  executable is run from the directory that the ttbin file is
                  stored in. Note that for security reasons, this executable
                  is *not* called if the program is running as root. This is
                  a string value.
3. RunAsUser: this can only be specified in the global `/etc/ttwatch.conf`
              file, and indicates which user (and optionally which group) the
              daemon runs as, similarly to the command-line argument. An
              error is shown if this option is specified in a non-global
              config file. This is a string value.
4. SkipElevation: tells the program to skip downloaded elevation data from
                  the internet for each downloaded activity. This is a
                  boolean value.
5. Device: specifies which device to use, as per the `-d` (`--device`)
           command-line parameter. Note that only one device can be specified
           at the moment (if anyone wants to modify the code to work with
           multiple device names here, feel free to send me a patch). This is
           a string value.

The following options only take effect when running as a daemon:

1. UpdateFirmware: tells the daemon to check and update the firmware of any
                   watch that is connected. This is a boolean value.
2. UpdateGPS: tells the daemon to update the QuickGPSFix data of any watch
              that is connected. This is a boolean value.
3. SetTime: tells the daemon to update the time of any watch that is
            connected. This is a boolean value.
4. GetActivities: tells the daemon to download any activities from any watch
                  that is connected. This is a boolean value.

Boolean values can have a value of ('y', 'yes', 'true', 'n', 'no' or 'false').
These values are *not* case-sensitive.

