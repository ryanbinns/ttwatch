Table of Contents
=================

  * [Requirements](#requirements)
  * [Compiling it](#compiling-it)
  * [Run it](#run-it)
    * [Why so slow?](#why-so-slow)
  * [TODO](#todo)
  * [Protocol documentation](#protocol-documentation)
  * [Credits](#credits)
  * [License](#license)

# Requirements

You need to be running a recent Linux kernel, with a Bluetooth **4.0** adapter
supporting [Bluetooth Low Energy](http://en.wikipedia.org/wiki/Bluetooth_low_energy).
Many newer PCs include built in Bluetooth 4.0 adapters; if you need one, I've had
good success with [this $6
dongle](http://www.amazon.com/ORICO-BTA-403-Bluetooth-Adapter-Windows/dp/B00ESBCT56),
which works out-of-the-box with the `btusb` driver from recent Linux
kernels.

The [`libbluetooth` (BlueZ)](http://www.bluez.org/) and
[`libcurl`](http://curl.haxx.se/libcurl) libraries are required.

# Compiling it

On Debian/Ubuntu-based systems, you can install the required libraries
with:

```bash
$ sudo apt-get install libbluetooth-dev libcurl4-gnutls-dev
```

Compilation with `gcc` should be straightforward:

```bash
$ make
```

To fix the issue with [very slow file transfers](#why-so-slow), the
most secure solution I've been able to come up with so far is to give the binary elevated capabilities as discussed [on StackExchange](http://unix.stackexchange.com/a/182559/58453):

```bash
sudo setcap 'cap_net_raw,cap_net_admin+eip' ttblue
```

# Run it

```
    ./ttblue <bluetooth-address> <pairing-code>
OR  ./ttblue <bluetooth-address> pair
```

Where `bluetooth-address` is the twelve-digit MAC address of your
TomTom GPS watch (`E4:04:39:__:__:__`) and pairing-code is either a
previously-used pairing code (can be from one of the "official" TomTom
mobile apps), or the string `pair` to create a new pairing.

For the time being, you can use `sudo hcitool lescan` to find your
device's BLE MAC address.

* For initial pairing, you'll need to go to the **Phone|Pair New**
  menu on the watch.
* For subsequent reconnection, ensure that **Phone|Sync** is enabled,
  and you may need to "wake up" the device's BLE radio by fiddling
  with a few buttons.

`ttblue` will download your activity files (saved as
`0091000n_YYYYMMDD_HHmmSS.ttbin`), and attempt to download the
QuickGPSFix update and send it to the watch. (You can then use
[`ttbincnv`](https://github.com/ryanbinns/ttwatch/tree/master/ttbincnv)
to convert the TTBIN files to GPX/TCX format.)

```none
$ ./ttblue E4:04:39:17:62:B1 123456

Opening L2CAP LE connection on ATT channel:
	 src: 00:00:00:00:00:00
	dest: E4:04:39:17:62:B1
Connecting to device... Done

Connected device information:
  maker     : TomTom Fitness
  model_name: Runner
  model_num : 1001
  firmware  : 1.8.42
  serial    : HC4354G00150
  user_name : Lenski

Setting PHONE menu to 'dlenski-ultra'.

Found 1 activity files on watch.
  Reading activity file 00910000 ...
11: read 55000 bytes from watch (1807/sec)
    Saved 55000 bytes to 00910000_20150801_123616.ttbin
    Deleting activity file 00910000 ...

Updating QuickFixGPS...
  Downloading http://gpsquickfix.services.tomtom.com/fitness/sifgps.f2p3enc.ee?timestamp=1439172006
  Sending update to watch (32150 bytes)...
7: wrote 32150 bytes to watch (1891/sec)
```

## Why so slow?

By default, Linux (as of 3.19.0) specifies a very intermittent connection interval for BLE devices. This makes sense for things like beacons and thermometers, but it is bad for devices that use BLE to transfer large files because the transfer rate is directly [limited by the BLE connection interval](https://www.safaribooksonline.com/library/view/getting-started-with/9781491900550/ch01.html#_data_throughput).
 
If you run as `root` or if you
[give the `ttblue` binary elevated capabilities](http://unix.stackexchange.com/a/182559/58453), it will attempt to set the minimum connection interval (7.5&nbsp;ms) and activity file downloads will proceed **much faster** (about 1800&nbsp;B/s
vs. 500&nbsp;B/s for me).

Unfortunately, elevated permissions are required to configure this feature of a BLE connection. For gory details, see [this thread on the BlueZ mailing list](http://thread.gmane.org/gmane.linux.bluez.kernel/63778).

# TODO

* Command line options, config file, etc.

# Protocol documentation

See [`tt_bluetooth.md`](tt_bluetooth.md) for reverse-engineered protocol documentation.

# Credits

[**@ryanbinns**](http://github.com/ryanbinns) did a lot of the heavy
lifting by writing his excellent
[`ttwatch`](http://github.com/ryanbinns/ttwatch) utility to sync with
TomTom GPS watches over *USB*, and in the process documenting the
`ttbin` binary format of the activity files, as well as many of the
internal data structures of the units.

# License

I'd like to license it as **GPLv3 or later**, but it uses snippets from the BlueZ source which are **GPLv2** so... let's call it GPLv2 or later?

By Dan Lenski &lt;<dlenski@gmail.com>&gt; &copy; 2015
