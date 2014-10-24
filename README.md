ttwatch
=======

Linux TomTom GPS Watch Utilities

Provides two programs for communicating with TomTom GPS watches and processing
the data they collect.

1. ttwatch - USB communications program for performing various operations
             with the watch, including downloading activity data, updating
             GPS data, and updating firmware.
2. ttbincnv - Post-processor allowing conversion of the ttbin file formats
              to either (currently) csv, gpx or kml files, using broadly
              similar formats to the official TomTom file formats.

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

Note: if you leave out the -a option on usermod, you will remove your user
      from every group except "usb", rather than just adding "usb" to the
      list of groups the user belongs to, so be careful...

Daemon Mode
===========

The ttwatch program supports running as a daemon, which will wait for a watch
to be connected, then automatically perform 3 operations:

1. Download the activity files and store them, including converting them to
   other file formats as specified in the watch preferences downloaded from
   the watch.
2. Updates the GPSQuickFix information in the watch from the internet.
3. Checks for firmware updates, and updates the firmware in the watch if
   newer firmware is found.

The daemon must be started as root (run by init or sudo), but the --runas
parameter can be specified to provide an alternative user (and optionally
a group - such as the usb group mentioned above) to run as.

Multiple Watches
================

The ttwatch program has rudimentary support for multiple watches. When running
from the command line (non-daemon mode), a list of available watches can be
displayed using the --devices option. A particular watch can be selected using
a 0-based index with the --device (or just -d) option.

When running as a daemon, the ttwatch program will automatically process any
watch that is connected, and place and activities downloaded from them into
separate directories by watch name, as per the official TomTom software. Note
that the --device (or -d) option cannot be used when running as a daemon, as
the list of available devices will change over time, so the device index just
doesn't make sense. Future updates may allow selecting watches based on watch
name or serial number, rather than device index.

Unsafe Functions
================

There are various options that can be given to the ttwatch program that read
and write raw data to/from the watch. Used incorrectly, these could destroy
the contents of the watch. For this reason, they are disabled by default. To
enable these options, run configure with the --with-unsafe option. Note that
I don't guarantee what will happen if you use these options without really
knowing what you are doing.

