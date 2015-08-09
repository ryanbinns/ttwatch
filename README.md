By Dan Lenski &lt;<dlenski@gmail.com>&gt; &copy; 2015

See [`tt_bluetooth.md`](tt_bluetooth.md) for reverse-engineered protocol documentation.

# Compile it

Requires [BlueZ](http://www.bluez.org/) `libbluetooth` and
[`libcurl`](http://curl.haxx.se/libcurl/) to build, and Linux with a
Bluetooth **4.0** (BLE-capable) Bluetooth adapter:

```bash
$ make
```

# Run it

```
    ./ttblue <bluetooth-address> <pairing-code>
OR  ./ttblue <bluetooth-address> pair
```

Where `bluetooth-address` is the twelve-digit address of your TomTom
GPS watch (`E4:04:39:__:__:__`) and pairing-code is either the a
established code used to pair with one of the TomTom mobile apps, or
the string `pair` to create a new pairing.

* For initial pairing, you'll need to go to the **Phone|Pair New** menu.
* For subsequent reconnection, ensure that **Phone|Sync** is enabled,
  and you may need to "wake up" the device's BLE radio by fiddling
  with a few buttons.

It will download your `preferences.xml` (file `0x00F20000` on the
watch), your activity files (saved as `0x0091000n_YYYYMMDD_HHmmSS.ttbin`), and attempt to download the
QuickGPSFix update and send it to the watch. (You can then use
[`ttbincnv`](https://github.com/ryanbinns/ttwatch/tree/master/ttbincnv) to convert
the TTBIN files to GPX/TCX format.)

```none
$ ./ttblue E4:04:39:17:62:B1 123456

Opening L2CAP LE connection on ATT channel:
	 src: 00:00:00:00:00:00
	dest: E4:04:39:17:62:B1
Setting minimum BLE connection interval... Could not set (transfer will be slow!)
Connecting to device... Done
Connected device name: Lenski
Setting peer name to 'dlenski-ultra-0'...
Reading preferences.xml ...
1: read 886 bytes from watch (886/sec)
 Saved 886 bytes to preferences.xml
Found 1 activity files on watch.
 Reading activity file 0x00910000 ...
11: read 55000 bytes from watch (500/sec)
  Saved 55000 bytes to 0x00910000_20150801_123616.ttbin
  Deleting activity file 0x00910000 ...
Downloading QuickFixGPS update...
 http://gpsquickfix.services.tomtom.com/fitness/sifgps.f2p3enc.ee?timestamp=1439019376
Sending QuickFixGPS update (32150 bytes)...
7: wrote 32150 bytes to watch (846/sec)
```

# Why so slow?

By default, Linux (as of 3.19.0) specifies a very intermitten connection interval for BLE devices. This makes sense for things like beacons and thermometers, but it is bad for devices that use BLE to transfer large files because the transfer rate is directly [limited by the BLE connection interval](https://www.safaribooksonline.com/library/view/getting-started-with/9781491900550/ch01.html#_data_throughput).
 
If you run as `root` or if you
[give the `ttblue` binary elevated capabilities](http://unix.stackexchange.com/a/182559/58453), it will attempt to set the minimum connection interval (7.5&nbsp;ms) and activity file downloads will proceed **much faster** (about 1800&nbsp;B/s
vs. 500&nbsp;B/s for me).

Unfortunately, elevated permissions are required to configure this feature of a BLE connection. If you think this needs to be fixed, please [chime in on this thread on the BlueZ mailing list](http://thread.gmane.org/gmane.linux.bluez.kernel/63778) :-D

# TODO

* Command line options, config file, etc.

# Credits

[**@ryanbinns**](http://github.com/ryanbinns) did a lot of the heavy
lifting by writing his excellent
[`ttwatch`](http://github.com/ryanbinns/ttwatch) utility to sync with
TomTom GPS watches over *USB*, and in the process documenting the
`ttbin` binary format of the activity files, as well as many of the
internal data structures of the units.

# License

Uses snippets from the BlueZ codebase, which are GPLv2. Rest is
licensed as GPLv3 or later.
