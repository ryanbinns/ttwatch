[![Build Status](https://api.travis-ci.org/dlenski/ttblue.png)](https://travis-ci.org/dlenski/ttblue)

Table of Contents
=================

  * [Introduction](#motivation)
  * [Building](#building)
    * [Requirements](#requirements)
    * [Compiling](#compiling)
  * [Use it](#use-it)
    * [Why so slow?](#why-so-slow)
  * [TODO](#todo)
  * [Protocol documentation](#protocol-documentation)
  * [Credits](#credits)
  * [License](#license)

# Introduction

The TomTom Multi-Sport and Runner are nice GPS watches and quite
affordable, but they suffer from subpar official software. There is no
official desktop app for interfacing *wirelessly* with the TomTom GPS watches,
(only for Android and iPhone).

Now you can use `ttblue` to download your activites wirelessly and
keep the QuickFix GPS ephemeris data up-to-date.

# Building

## Requirements

You need to be running a recent Linux kernel, with a Bluetooth **4.0** adapter
supporting [Bluetooth Low Energy](http://en.wikipedia.org/wiki/Bluetooth_low_energy).
Many newer PCs include built in Bluetooth 4.0 adapters; if you need one, I've had
good success with [this $6
dongle](http://www.amazon.com/ORICO-BTA-403-Bluetooth-Adapter-Windows/dp/B00ESBCT56),
which works out-of-the-box with the `btusb` driver from recent Linux
kernels.

The [`libbluetooth` (BlueZ)](http://www.bluez.org/),
[`libcurl`](http://curl.haxx.se/libcurl), and
[`popt`](http://directory.fsf.org/wiki/Popt) libraries are required.
On Debian/Ubuntu-based systems, these can be installed with:

```bash
$ sudo apt-get install libbluetooth-dev libcurl4-gnutls-dev libpopt-dev
```

## Compiling

Compilation with `gcc` should be straightforward:

```bash
$ make
$ make setcap # requires sudo/root access
```

To fix the issue with [very slow file transfers](#why-so-slow), the
most secure solution I've been able to come up with so far is to give
the binary elevated capabilities as discussed
[on StackExchange](http://unix.stackexchange.com/a/182559/58453):
`make setcap` will do this automatically or you can do it manually as
follows:

```bash
sudo setcap 'cap_net_raw,cap_net_admin+eip' ttblue
```

(Note that this is *more* secure than giving the binary
[setuid root](http://wikipedia.org/wiki/setuid) permissions, because
it only allows root-like privileges for these specific capabilities.)

# Use it

For initial pairing, you'll need to go to the **Phone|Pair New**
menu on the watch.

For subsequent reconnection, ensure that **Phone|Sync** is enabled,
and you may need to "wake up" the device's BLE radio by pressing a
few buttons.

Try the following command line:

```
./ttblue -a [-d <bluetooth-address>] [-c <pairing-code>] [-s <activity-store>]
```

* `bluetooth-address` is the MAC address of your TomTom GPS watch, for
  example `E4:04:39:17:62:B1`. If not specified, `ttblue` will attempt
  to scan for BLE devices, and try to connect to the first one
  matching TomTom's vendor ID (`E4:04:39`).

* The `pairing-code` is a previously-used pairing code (can be from one of
  the "official" TomTom mobile apps). If left blank, `ttblue` will try
  to create a new pairing.

* The `-a`/`--auto` option tells `ttblue` to download all activities and
  update QuickFixGPS.

* The `-s`/`--activity-store` option specifies a location for `.ttbin`
  activity files to be output (current directory is the default).

As invoked above, `ttblue` will download your activity files (saved as
`0091000n_YYYYMMDD_HHmmSS.ttbin`), and attempt to download the
QuickGPSFix update and send it to the watch. (You can then use
[`ttbincnv`](https://github.com/ryanbinns/ttwatch/tree/master/ttbincnv)
to convert the TTBIN files to GPX/TCX format.)

```none
$ ./ttblue -a -d E4:04:39:17:62:B1 -c 123456
Opening L2CAP LE connection on ATT channel:
	 src: 00:00:00:00:00:00
	dest: E4:04:39:17:62:B1
Connected to HC4354G00150.
  maker     : TomTom Fitness
  serial    : HC4354G00150
  user_name : Lenski
  model_name: Runner
  model_num : 1001
  firmware  : 1.8.42
  rssi      : -90 dB
Setting PHONE menu to 'dlenski-ultra-0'.
Found 1 activity files on watch.
  Reading activity file 00910000 ...
11: read 55000 bytes from watch (1807/sec)
    Saved 55000 bytes to ./00910000_20150801_123616.ttbin
    Deleting activity file 00910000 ...
Updating QuickFixGPS...
  Last update was at at Sat Aug 1 04:11:03 2015.
  Downloading http://gpsquickfix.services.tomtom.com/fitness/sifgps.f2p3enc.ee?timestamp=1439172006
  Sending update to watch (32150 bytes)...
7: wrote 32150 bytes to watch (1891/sec)
```

There's also a fairly rudimentary "daemon" mode wherein `ttblue` just
loops over and over (by default it waits an hour to retry after a
successful connection, but only 10 seconds after a failed one), and a
`-p`/`--post` option to specify a command to be run on each
successfully downloaded `.ttbin` file (see [`ttbin2strava.sh`](ttbin2strava.sh))
for an example):

```none
$ ./ttblue -a --daemon -d e4:04:39:17:62:b1 -c 123456 -s ~/ttbin -p ttbin2strava.sh
```

## Why so slow?

By default, Linux (as of 3.19.0) specifies a very intermittent connection interval for BLE devices. This makes sense for things like beacons and thermometers, but it is bad for devices that use BLE to transfer large files because the transfer rate is directly [limited by the BLE connection interval](https://www.safaribooksonline.com/library/view/getting-started-with/9781491900550/ch01.html#_data_throughput).
 
If you run as `root` or if you
[give the `ttblue` binary elevated capabilities](http://unix.stackexchange.com/a/182559/58453), it will attempt to set the minimum connection interval (7.5&nbsp;ms) and activity file downloads will proceed **much faster** (about 1800&nbsp;B/s
vs. 500&nbsp;B/s for me).

Unfortunately, elevated permissions are required to configure this feature of a BLE connection. For gory details, see [this thread on the BlueZ mailing list](http://thread.gmane.org/gmane.linux.bluez.kernel/63778).

# TODO

* More command line options?
* Real config file?
* Better daemon mode that actually puts itself in the background
  and writes output to a log file?
* Integrate with [`ttwatch`](http://github.com/ryanbinns/ttwatch)
  which already does all these things, but over USB?

# Protocol documentation

See [`tt_bluetooth.md`](tt_bluetooth.md) for reverse-engineered protocol documentation.

# Credits

[**@ryanbinns**](https://github.com/ryanbinns) did a lot of the heavy
lifting by writing his excellent
[`ttwatch`](http://github.com/ryanbinns/ttwatch) utility to sync with
TomTom GPS watches over *USB*, and in the process documenting the
`ttbin` binary format of the activity files, as well as many of the
internal data structures of the units.

[**@Grimler91**](https://github.com/Grimler91) for adding support for
TomTom GPS watches using the "v2" protocol. (Spark, Runner v2, etc.)

# License

I'd like to license it as **GPLv3 or later**, but it uses snippets from the BlueZ source which are **GPLv2** so... let's call it GPLv2 or later?

By Dan Lenski &lt;<dlenski@gmail.com>&gt; &copy; 2015
