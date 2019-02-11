ttwatch
=======

Linux TomTom GPS Watch Utilities

Provides programs for communicating with TomTom GPS watches and processing
the data they collect.

1. `ttwatch`  - USB communications program for performing various operations
                with the watch, including downloading activity data, updating
                GPS data, and updating firmware.
2. `ttwatchd` - Daemon program that automatically performs specified functions
                when a watch is connected to the PC.
3. `ttbincnv` - Post-processor allowing conversion of the ttbin file formats
                to either (currently) csv, gpx, kml or tcx  files, using broadly
                similar formats to the official TomTom file formats.
4. `ttbinmod` - Post-processor allowing modifications to be made to the ttbin
                file. Currently, adding/modifying lap markers and truncating the
                file at the end of the workout (last lap, goal completion etc)
                are supported.
5. `ttbin2mysports.sh` - script that enabled uploading to a MapMyFitness account
                         that is linked to a MySports account. Automatically
                         converts the ttbin file to a TCX file before uploading.

System Requirements
===================

This program requires the following libraries to be compiled and installed
before attempting to build it.

1. `cmake` (required version 2.8 or higher).
   Available from https://cmake.org/
2. `openssl` (tested against version 1.0.1f, other versions may work).
   Available from http://www.openssl.org
3. `curl` (tested against version 7.38.0, other versions may work).
   Available from http://curl.haxx.se/download.html
4. `libusb` 1.0.16 or later (tested against version 1.0.19).
   Available from http://sourceforge.net/projects/libusb/
5. `protobuf` 3.6.0 or later (tested against version 3.6.0)
   Available from https://github.com/protocolbuffers/protobuf
6. `protobuf-c` 1.3.0 or later (tested against version 1.3.1)
   Available from https://github.com/protobuf-c/protobuf-c

Prebuilt packages should be available for most systems using the system's
built in package manager (dpkg, yum, apt, rpm etc...). Make sure that the
`-dev` version of the packages (eg. `libssl-dev`, `libcurl-dev`, `libusb-1.0-0-dev`)
are installed so that the headers are available.

Build Instructions
==================

The `ttwatch` binaries are built using cmake. Both in-source or out-of-source
builds are supported. A simple in-source build is done as follows:
```
$ cmake .
$ make
$ sudo make install
```
An out-of-source build can be done as follows:
```
$ mkdir build
$ cd build && cmake ..
$ make
$ sudo make install
```
The advantage of the out-of-source build is that to do a clean, you just do
`rm -rf build` to remove the build tree so you can start again.

Setup for unprivileged access
=============================

In order to have permission to access the USB devices when running as anyone
other than root, a udev rule must be set up to allow access for unprivileged
users. The rule I have set up is:

```
$ cat /etc/udev/rules.d/99-tomtom.rules
SUBSYSTEMS=="usb", ATTRS{idVendor}=="1390", ATTRS{idProduct}=="7474", SYMLINK+="tomtom", GROUP="usb", MODE="660"
SUBSYSTEMS=="usb", ATTRS{idVendor}=="1390", ATTRS{idProduct}=="7475", SYMLINK+="tomtom", GROUP="usb", MODE="660"
SUBSYSTEMS=="usb", ATTRS{idVendor}=="1390", ATTRS{idProduct}=="7477", SYMLINK+="tomtom", GROUP="usb", MODE="660"
```

The value for `idProduct` depends on the model of watch that you use. For
original models, `7474` is correct. For Spark watches, the `idProduct` value is
`7477`, although `7475` has also been found. Please check `dmesg` output, for
the correct value.

The `ttwatch` distribution includes a `99-tomtom.rules` file as above. To use
this file, copy it to the udev rules folder as follows:

```
$ sudo cp 99-tomtom.rules /etc/udev/rules.d
```

After creating the udev rule, you need to reload the rules to make udev aware
of them, by running:

```
$ sudo udevadm control --reload-rules
```

The above udev line basically gives access to USB devices to members of the
"usb" group. Some systems already have a "usbuser" group, and feel free to
reuse that one in the udev line.

If you do not reuse an existing group, then you need to create the "usb" group
and add yourself to it using:

```
$ sudo addgroup usb
$ sudo usermod -a -Gusb <your_username>
```

Note: If you leave out the -a option on usermod, you will remove your user
      from every group except "usb", rather than just adding "usb" to the
      list of groups the user belongs to, so be careful...
Note: You will have to log out and then log back in to see the change in
      group membership.

The makefile includes a special rule (`install_udev`) that will perform all
these steps to make installation easier. It will associate the user that
runs make (as printed by the `logname` command) to the usb group. Simply run:

```
$ sudo make install_udev
```

If your system uses `devfs` instead of `udev` (such as FreeBSD), configure as follows.
Firstly, add the following lines to `/etc/devfs.rules`:

```
[usb_devices=10]
add path 'usb/*' mode 0660 group usb
```

Next, add the following lines to `/etc/rc.conf`:

```
devfs_system_ruleset="usb_devices"
```

Lastly, create the `usb` group and add the required user to it (these commands
must be run as `root`):

```
# pw groupadd usb
# pw groupmod usb -m <your_username>
```

Initial Setup
=============

Before being able to use most of the commands the program provides, the watch
needs to be set up, similar to the initial setup routine that the Windows client
does. This is done by running

```
ttwatch --initial-setup
```

Doing this will create a default XML preferences file on the watch, as well as
create default races for the different activities. Most of the functions rely on
the XML preferences file existing, so this must be done first.

Daemon Mode
===========

The ttwatchd program runs as a daemon, which will wait for a watch to be
connected, then automatically perform whichever operations are specified on the
command line. The following four operations are supported, and at least one of
them must be specified to start the daemon:

1. `--get-activities`: Download the activity files and store them, including
   converting them to other file formats as specified in the watch preferences
   downloaded from the watch.
2. `--update-gps`: Updates the GPSQuickFix information in the watch from the
   internet.
3. `--update-fw`: Checks for firmware updates, and updates the firmware in the
   watch if newer firmware is found.
4. `--set-time`: Sets the time on the watch to match the local system time.

All four options can be specified with the `-a` (or `--auto`) option

The daemon must be started as root (run by `init` or `sudo`), but the `--runas`
parameter can be specified to provide an alternative user (and optionally
a group - such as the usb group mentioned above) to run as. Note that if the
default group for the user does not have access to the USB devices, then group
*must* be specified on the command line, For example, if user `fred` has a default
group of `fred` (which is usual for most linux systems), but the group with access
to the USB devices is called `usb` as above, then the parameter will need to
be `--runas fred:usb`, otherwise there will be permissions errors trying to
communicate with the watch. Note that in this case `fred` will also need to
be a member of the `usb` group, otherwise there will be permissions errors also.

Note: The daemon is not supported under FreeBSD as the FreeBSD version of
      libusb does not support hot-plug detection and causes compilation
      errors. To resolve this, run `cmake -Ddaemon=off` to force the
      compilation to remove the daemon support.

Multiple Watches
================

The ttwatch program has support for multiple watches. When running from the
command line a list of available watches can be displayed using the `--devices`
option. A particular watch can be selected using the `-d` option with two
different parameters possible:

1. a string that matches the watch serial number
2. a string that matches the watch name

Both pieces of information are displayed when listing available watches
with the `--devices` option.

When running as a daemon and the watch serial number or name are specified,
the daemon will only process that particular watch. This can be used to store
the activities from multiple watches in different users' home areas by
starting multiple instances of the daemon running as different users,
specifying different watches.

Activity vs History Data
========================

Many people have wondered why they are not getting activity files downloaded
to the computer even though they can see history entries using the
`--list-history` option. Put simply, the two are almost unrelated. The activity
data is logged every second and contains all the information collected during
the activity. The history data is a summary of the activity data that is
generated when the activity is completed. The history data is small, and is
retained on the watch permanently (unless manually deleted) to support the race
function and to view past activity details on the watch itself. The activity data
is large, and is deleted from the watch as soon as it is successfully downloaded
to free up space on the watch for new activities. This means that each activity
can only be downloaded once. If it is subsequently deleted from the computer,
*it cannot be recovered* (unless it is backed up separately).

Unsafe Functions
================

There are various options that can be given to the ttwatch program that read
and write raw data to/from the watch. Used incorrectly, these could destroy
the contents of the watch. For this reason, they are disabled by default. To
enable these options, run `cmake` with the `-Dunsafe=on` option. Note that
I don't guarantee what will happen if you use these options without really
knowing what you are doing.

Config Files
============

The ttwatch programs supports loading some settings from config files. Three
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
6. Formats: specifies a list of file formats that should be created when an
            activity file is downloaded. The supported file formats are listed
            by the help command (`-h` or `--help` command line options). This
            list can be either space- or comma-separated, or a combination
            of the two. This is a string value.
7. Ephemeris7Days: specifies that a 7-day GPS ephemeris should be uploaded
                   to the watch, rather than the default 3-day ephemeris.
                   This is a boolean value.

The following options only take effect when running the `ttwatchd` daemon:

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

An example global config file to set the activity store location, default daemon
user and group, and normal activities when a watch is connected could be:
```
ActivityStore = /mnt/data/watch
RunAsUser = jsmith:usb
GetActivities = true
UpdateFirmware = true
UpdateGPS = true
SetTime = true
```
A per-user config file could be added to specify a list of file formats to make:
```
Formats = csv,gpx,tcx
```

Recovery / Older Firmware
=========================

It *may* be possible to reset a watch with damaged firmware or file structure
using the Recovery Mode, which requires TomTom's official MySports Connect
software (Windows or Mac only): [information from TomTom support]
(http://us.support.tomtom.com/app/answers/detail/a_id/17394).

Watches with extremely old firmware (prior to 1.8.x) may not work with the
`ttwatch` software. In this case, the solution is to use the official MySports
Connect software to perform a firmware update. After this, the `ttwatch`
software will work.

Third-party Applications
====

Note that I have not tested these, and do not endorse or provide support for
them, nor guarantee their functionality or safety. This list is for
information only.

[TT Watch Synchronizer - UI for ttwatch to manage your watch and tracks and optionaly upload to strava](https://github.com/Dica-Developer/ttws)

