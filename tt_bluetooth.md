By Dan Lenski &lt;<dlenski@gmail.com>&gt; &copy; 2015

Table of Contents
=================

  * [Motivation](#motivation)
    * [Models](#models)
  * [Reverse engineered BLE protocol](#reverse-engineered-ble-protocol)
      * [Notation](#notation)
      * [Authorization service and characteristics](#authorization-service-and-characteristics)
      * [File transfer service](#file-transfer-service)
        * [Standard GATT services](#standard-gatt-services)
    * [Authorization sequence](#authorization-sequence)
      * [Initial pairing](#initial-pairing)
      * [Subsequent reconnection](#subsequent-reconnection)
    * [Data transfer commands](#data-transfer-commands)
        * [Delete file](#delete-file)
        * [Write to file](#write-to-file)
        * [List files](#list-files)
        * [Read from file](#read-from-file)
    * [Normal operation](#normal-operation)
  * [Mysteries](#mysteries)
    * [File 0x00020001](#file-0x00020001)
  * [Acknowledgments](#acknowledgments)

# Motivation

The TomTom Multi-Sport and Runner are nice GPS watches and quite
affordable, but they suffer from subpar software.

I got a TomTom Runner watch to replace an older Garmin 405 and was
looking forward to an easier way to sync activities thanks to the
Bluetooth LE support, but was disappointed in the very low quality of
TomTom's official apps:

* USB sync is *only* supported through the official Windows/Mac
  app. It is pretty fast and somewhat more reliable than the mobile
  apps, but still finicky and annoying.
* Bluetooth LE sync is *only* supported through the official
  [Android](http://play.google.com/store/apps/details?id=com.tomtom.mysports)
  or iOS apps. The Android app at least is terrible, combining a heavy
  yet almost feature-free GUI with an extremely unreliable backend
  that regularly loses its connection to the watch and requires an
  infinite amount of constant fiddling on both the watch and the phone
  to get them to sync.
* TomTom is very heavy-handed in pushing users to uploads to their own
  MySports social fitness site—in fact the official apps won't
  function without being logged into it. Making this even more
  tedious, the mobile app frequently gets confused about the synced
  account, but only the desktop app can write the small XML file on
  the watch (`0x00f20000`) which stores account information.

So I wanted something better, including the ability to sync to a
desktop computer via Bluetooth LE.

Fortunately, [**@ryanbinns**](http://github.com/ryanbinns) had already
done a lot of the heavy lifting by writing his excellent
[`ttwatch`](http://github.com/ryanbinns/ttwatch) utility to sync with
TomTom GPS watches over *USB*, and in the process documenting the
`ttbin` binary format of the activity files, as well as many of the
internal data structures of the units.

## Models

Here are TomTom GPS watches that should be compatible with the protocol described below:

* Multi-Sport (product ID `1002` or `0xEA030000`): has running,
  cycling, and swimming features
* Runner (**what I have**, ID `1001` or `0xE9030000`): firmware is byte-identical to
  Multi-Sport, hardware appears identical too except for a different
  product ID stored in non-volatile memory. I think the cycling and
  swimming features are just software-disabled for market segmentation
  purposes.
* Multi-Sport Cardio and Runner Cardio: versions of the above with
  built-in wrist-based heart rate monitor.

# Reverse engineered BLE protocol

As with all Bluetooth LE devices, these transfer data exclusively via
the
[ATT and GATT protocol stack](http://epxx.co/artigos/bluetooth_gatt.php). The
protocols were designed with lots of
[IoT](https://en.wikipedia.org/wiki/Internet_of_Things)-ish
features like devices which can advertise a dynamically changing list
of capabilities.

The TomTom BLE interface is basically that of a glorified serial
port, with packets sent *to* the device using the standard ATT write
command, and received *from* the device as asynchronous notification
commands. In practice, the order of received packets is 100%
predictable in normal operation, so there's nothing asynchronous about
their usage in these devices.

### Notation

* All 2- and 4-byte integers numbers are little-endian.
* Sequences of bytes (in hexadecimal) look like this: `de ad be ef`
* ASCII-ish string literals, with embedded hex escape sequences and nulls, look like this: `'\xfe\0product_name\0'`
* `HANDLE -> data`: device sends `data` to host by way of asynchronous notification to the specified ATT handle (opcode `0x1b`)
* `HANDLE <- data`: host sends `data` to device using the ATT write-no-response command (opcode `0x52`)
* `HANDLE <-- data`: host sends `data` to device using the ATT write-request command (opcode `0x12`)

### Authorization service and characteristics

```
Service: UUID=b993bf91-81e1-11e4-b4a9-0800200c9a66, handles=0x30 to 0xffff
  Char: UUID=b993bf92-81e1-11e4-b4a9-0800200c9a66, handle=0x32
    properties => NOTIFY,WRITE NO RESPONSE,WRITE
  Char: UUID=b993bf93-81e1-11e4-b4a9-0800200c9a66, handle=0x35
    properties => WRITE NO RESPONSE,WRITE
```

* `CH_PASSCODE` (handle `0x32`): used to transfer the 6-digit
  passcode generated in the pairing process between the host and the
  device.
* Handles `0x26`, `0x29`, `0x2c`, `0x2f`, `0x33`, `0x35`: these are
  used in the initial pairing and subsequent reconnection processes. I
  don't really understand their purpose but it doesn't matter since it
  suffices to replay the sequence of reads and writes used by the
  official mobile app.
  * Other than `0x35`, these are **hidden**.
  * Their existence can only be inferred by Bluetooth snooping.
  * They are not revealed by GATT service/characteristic enumeration, e.g.
    `gatttool --characteristics -t random -b 'E4:04:39:17:62:B1'`


### File transfer service

```
Service: UUID=b993bf90-81e1-11e4-b4a9-0800200c9a66, handles=0x23 to 0x2f
  Char: UUID=170d0d31-4213-11e3-aa6e-0800200c9a66, handle=0x25
    properties => NOTIFY,READ,WRITE NO RESPONSE,WRITE
  Char: UUID=170d0d32-4213-11e3-aa6e-0800200c9a66, handle=0x28
    properties => NOTIFY,READ,WRITE NO RESPONSE
  Char: UUID=170d0d33-4213-11e3-aa6e-0800200c9a66, handle=0x2b
    properties => NOTIFY,READ,WRITE NO RESPONSE
  Char: UUID=170d0d34-4213-11e3-aa6e-0800200c9a66, handle=0x2e
    properties => NOTIFY,READ,WRITE NO RESPONSE
```

* `CH_CMD_STATUS` (handle `0x25`: used to send commands to the
  device and for the device to signal successful start/finish.
* `CH_LENGTH ` (handle `0x28`): used to indicate the size in bytes of files transferred to/from the device.
* `CH_TRANSFER` (handle `0x2B`): used to transfer bulk data (file contents) to/from the device.
* `CH_CHECK` (handle `0x2E`): used to acknowledge successful receipt of data by the watch or host, depending on direction of data transfer.

#### Standard GATT services

The TomTom devices support the standard [Generic Attribute](https://www.bluetooth.org/en-us/specification/assigned-numbers/generic-attribute-profile) service (service UUID `0x1800`, handles `0x0001` to` 0x000b`), with the following characteristics:

UUID   | Handle | Name | Properties | Read value (for me)
-------|--------|------|------------|-----------
`2a00` | `0003` | Device Name | READ | `'Lenski'`
`2a01` | `0005` | Appearance | READ | `11 00` ([not a well-known value](https://developer.bluetooth.org/gatt/characteristics/Pages/CharacteristicViewer.aspx?u=org.bluetooth.characteristic.gap.appearance.xml))
`2a02` | `0007` | Peripheral Privacy Flag | READ | `00` (no privacy mode)
`2a03` | `0009` | Reconnection Address | WRITE |
`2a04` | `000b` | Peripheral Preferred Connection Parameters | READ | `50 00 a0 00 00 00 e8 03`


They also support the standard [Device Information](https://developer.bluetooth.org/gatt/services/Pages/ServiceViewer.aspx?u=org.bluetooth.service.device_information.xml) service (service UUID=`0x180a`, handles `0x0010` to `0x0022`):

UUID   | Handle | Name | Properties | Read value (for me)
-------|--------|------|------------|--------------------
`180a` | `0012` | System ID | READ | `00 00 00 00 00 00 00 00`
`2a24` | `0014` | Model Number String | READ | `'Runner\0\0\0\0'`
`2a25` | `0016` | Serial Number String | READ | `'HC4354G00150'`
`2a26` | `0018` | Firmware Revision String | READ | `'Firmware Revision\0'`
`2a27` | `001a` | Hardware Revision String | READ | `'1001\0\0\0\0\0\0'`
`2a28` | `001c` | Software Revision String | READ | `'1.8.42\0\0\0\0'`
`2a29` | `001e` | Manufacturer Name String | READ | `'TomTom Fitness\0'`
`2a2a` | `0020` | IEEE 11073-20601 Regulatory Cert. Data List | READ | `'\xfe\x00experimental'`
`2a50` | `0022` | [PnP ID](https://developer.bluetooth.org/gatt/characteristics/Pages/CharacteristicViewer.aspx?u=org.bluetooth.characteristic.pnp_id.xml) | READ | `01 0d 00 00 00 10 01`<br/>[vendor `0x000D`](https://www.bluetooth.org/en-us/specification/assigned-numbers/company-identifiers) &rArr; [Texas Instruments' BLE IC](http://www.ti.com/lsds/ti/wireless_connectivity/bluetooth_bluetooth-ble/products.page#p3049=Bluetooth%20Smart%20%28Bluetooth%20low%20energy%29)?

## Authorization sequence

__Note:__ I'm only concerned with the ATT-level process
here. Bluetooth connection and encryption negotiation can be
automatically handled by lower levels of the protocol stack.

### Initial pairing

1. User goes to __PHONE | PAIR NEW__ menu on the watch.
2. Watch displays random 6-digit pairing code once the host initiates a
   Bluetooth LE connection with the watch (`hcitool lecc --random ADDRESS ` will do it).
3. A sequence of writes (`HANDLE <- data`) and asynchronous notifications (`HANDLE -> data`)
   follows:

        33 <-  01 00
        26 <-  01 00
        2f <-  01 00
        29 <-  01 00
        2c <-  01 00
        35 <-- 01 13 00 00 01 12 00 00
        32 <-  [6-digit code as 32-bit LE integer]
        32 ->  01                  (if successful)

4. Device screen shows __Connected__.
5. Host should remember the successful pairing code since it will be
   reused for subsequent connections.

### Subsequent reconnection

1. The last 5 pairing codes generated by the watch (I think) are
   stored in the `0x0002000f` file on the watch; **any** of these will
   be accepted for future authentication.
2. Sequence:

        33 <-  01 00
        35 <-- 01 13 00 00 01 12 00 00
        26 <-  01 00
        32 <-- [6-digit code as LE 32-bit integer]
        32 ->  01                  (if successful)

## Data transfer commands

Data transfer uses the
[`modbus`](http://stackoverflow.com/questions/19347685/calculating-modbus-rtu-crc-16)
16-bit [CRC](http://en.wikipedia.org/wiki/Cyclic_redundancy_check) to
verify data integrity.

Files on the TomTom devices have `uint32` identifiers in which the top
byte is always `00`. A
[partial list can be found in `libttwatch.h`](https://github.com/ryanbinns/ttwatch/blob/master/libttwatch/libttwatch.h#L141-L154). The
byte-ordering of the file numbers is scrambled in a strange way in the
Bluetooth protocol: `fileno_bytes = (fileno&0xff0000) ,
((fileno&0xff00)>>8) , ((fileno&0xff)<<8)` (e.g. file `0x00910001`
becomes `91 01 00`).

#### Delete file

* Sequence:

        25 <-- 04 fileno_bytes
        25 ->  01 00 00 00     (0 if command is not accepted)
        Possibly repeated {
          2b ->  response bytes
        }
        25 ->  00 00 00 00

* **The meaning of the returned bytes is currently unknown to me. _Any guesses?_**
* There can be long delays before the response, when deleting a large file such as a GPS activity; a timeout of 20&nbsp;s seems to work for me.

#### Write to file

* Existing file **must be deleted prior to writing**.
* Sequence:

        25 <-- 00 fileno_bytes
        25 ->  01 00 00 00     (0 if command is not accepted)
        28 <-  [length of file in bytes, uint32_le]
        Repeat until entire file has been sent {
          Repeat up to 255 times: {
            2b <-  [up to 20 bytes of file contents]
          }
          2b <-  [18 bytes of file contents] [CRC16 of data bytes since reset, uint16_le]
          2e ->  ack_counter (uint32_le)
          ack_counter := ack_counter + 1
        }
        25 ->  00 00 00 00

* The host needs to compute the CRC16 as data is sent, and send
  correct CRC16 at the points shown (at the end of every 256 data
  packets, or fraction thereof at the end).
  * The device will prematurely end receipt (with `25 <- 00 00 00 00`)
    if an incorrect checksum is received.
  * After a correct checksum is received, the device sends the
    `ack_counter`, a sequentially increasing integer sent by the
    device: 1 after the first 256 packets (or fraction thereof), 2
    after the second batch, etc.

#### List files

* This is mainly used to get the list of TTBIN activity files on the
  watch, which are stored in file numbers `0x00910000`, `0x00910001`,
  etc. (and can be converted to TCX or GPX or CSV with
  [`ttbincnv`](https://github.com/ryanbinns/ttwatch/tree/master/ttbincnv)).
* Here only the first non-zero byte of the file number is used as input (e.g. `91`).
* Sequence:

        25 <-- 03 fileno_byte1 00 00
        25 ->  01 00 00 00     (0 if command is not accepted)
        Repeat {
          2b -> bytes
        }
        25 ->  00 00 00 00

* The bytes returned are an array of `uint16_le`:
  * The first entry is the number of subsequent values
  * Subsequent values are the *last* two bytes of the file numbers
  * For example, listing all files with `fileno_byte` of `91` might
    return `02 00 00 00 01 00`, which indicates that there are two
    activity files available, `0x00910000` and `0x00910001`.

#### Read from file

* This is the same as the write sequence with the direction of
  reads/writes to handles `0x28`, `0x2b`, and `0x2e` reversed.
* The host should compute the CRC16 of bytes as they are received and
  send a sequentially increasing `ack_counter` (0, 1, 2) at the end of
  each batch of 256 packets, or partial fraction thereof at the end.
  * The rate of data transmission will become *extremely slow*
     (~1&nbsp;packet/s) if the counter is not received.
  * The device will prematurely end transmission (with `25 <- 00 00 00
    00`) if an out-of-sequence `ack_counter` value is received.

* Sequence:

        25 <-- 01 fileno_bytes
        25 ->  01 00 00 00     (0 if command is not accepted)
        28 ->  [length of file in bytes, uint32_le]
        Repeat until entire file has been read {
          *Reset CRC16 checksum to 0xFFFF`
          Repeat up to 255 times: {
            2b ->  [up to 20 bytes of file contents]
          }
          2b ->  [18 bytes of file contents] [CRC16 of data bytes since reset, uint16_le]
          2e <-  ack_counter (uint32_le)
        }
        25 ->  00 00 00 00

## Normal operation

Here is what the Android app does in normal operation:

1. BLE connection and SMP setup (connection security)
2. Authorization (by either the [initial pairing](#initial-pairing) or
   [subsequent reconnection](#subsequent-reconnection) sequences shown
   above.
3. App reads the device information profile characteristics (handles
  `0x0012`, `0x0014`, `0x0016`, `0x001a`, `0x001c`, `0x001e` in that
  order).
4. App deletes then writes the file `0x00020002` with a short string
  identifying the host device (the Bluetooth adapter device name),
  which is then shown at the bottom of the device screen in the
  __PHONE | SYNC__ menu.
5. App reads the XML-ish preferences file `0x000f2000` from the
   device; among other tidbits, this file contains information on the
   MySports online account to which the device is linked.
6. The part that actually matters to end users:
   * App lists the `0x91****` files (TTBIN activity files),
   * … then reads and deletes them one-by-one.
7. App reads the file `0x00020005`; this is some kind of device
   description file which is mostly binary but contains one
   identifiable ASCII string: the watch serial "number"
   (e.g. `HC4354G00150`).
8. App reads the file `0x00020001`; this represents the [status
   of the GPS firmware](#file-0x00020001) somehow, and contains
   a couple of ASCIIZ strings that
   appear to be related to the GPS firmware revision strings that also
   appear in the TTBIN header: e.g. `5xp__5.5.116-
   R32+5xpt_5.5.116-R32` and `EGSD5xp` for my watch.
9. App deletes then writes the file `0x00010100`, which is about
   32&nbsp;KiB long and is a GPSQuickFix update file (GPS ephemeris
   data). This always comes from
   `gpsquickfix.services.tomtom.com/fitness/sifgps.f2p3enc.ee` for my
   device, although the
   [JSON config file referred to in the XML preferences file](https://mysports.tomtom.com/service/config/config.json)
   shows that a separate version exists for use with
   [GLONASS](https://en.wikipedia.org/wiki/GLONASS) satellites instead
   of GPS.
10. At this point…
    * Sometimes the device abruptly "hangs up" at this point and
      closes the connection.
    * Sometimes the device ends the command normally (`25 -> 00 00 00
      00`).
    * On at least one occasion that I have logged, the host sends a
      command (`25 <-- 05 01 00 01`) that seems to hint at some
      further processing of the ephemeris data file, then reads the
      file `0x00020001` (again!) before closing the connection.

# Mysteries

Hardware:

* Why is the speed of file download *so darn slow*? I get about
  600&nbsp;B/s typically while downloading the TTBIN activity files,
  although
  [this book says 15625&nbsp;B/s of user data throughput](https://www.safaribooksonline.com/library/view/getting-started-with/9781491900550/ch01.html#_data_throughput)
  should be possible with BLE.
* My TomTom Runner appears to wake up its BLE hardware and send out
  BLE advertising packets only for 10 seconds every 10 minutes, or
  when I fiddle with the buttons excessively. Is there a way to
  convince it to wake up more often?

BLE protocol and firmware:

* What is the meaning of the unknown, short packets of data returned
  by the [file deletion command](#delete-file)?
* What is the meaning of the command beginning with `05` as the first
  byte?
* Is it possible to tell the device to reset itself over BLE?
* Is it possible to read or write files starting at arbitrary
  positions, rather than starting at the beginning? (Would be useful
  for quickly previewing activities)
* Is it possible to upgrade the device firmware over BLE?
* When I try to write the device manifest file (`0x00850000`) in order
  to update settings that should be user-visible on the watch (such as
  the [local-to-UTC time offset](
  https://github.com/ryanbinns/ttwatch/blob/master/ttwatch/manifest.txt#L102)),
  nothing appears to happen for a few minutes until the watch suddenly
  "notices" the change. Is it possible to cause the device to reload
  its own settings immediately?

## File 0x00020001

Partially decoded structure of this file: it appears to encode the UTC
date of the last update to the QuickFixGPS file. Sending the GLONASS
version of the update rather than the GPS version does not appear to
affect anything other than the timestamp.

Perhaps this can be used to avoid re-updating the QFG file
unnecessarily on every connection... but I'm not really sure how to
determine when the QFG file *expires*.

    00: 03 00

    Aha! This one seems to be the date when the QFG was
    last UPDATED:
      02: 07 df = 2015 (int16_be)
      04: 08 = month 8
      05: 11 = day 17

    I've seen this change to 00 01 right after an update:
      06: 00 00

    Next 6 bytes (but usually only 2 bytes?) change every time GPS is
    activated:
      08: 39 6d
      0a: 00 00
      0c: 00 00

    I think this part represents a UTC time, since it's close to the
    current UTC time right after an update. It also updates after
    using the watch for a GPS activity. Perhaps it's the timestamp of the
    last GPS fix?
        0e: 2d = 45 (year - 1970?)
        0f: 07 = month - 1? (as in POSIX struct timeval)
        10: 11 = day 17
        11: 06 2a 04 = 06:42:04 (hour minute second)

    14: 06 00
    16: 50 00
    18: 02 00
    1a: 05 05 74 00
    1e: + ASCIIZ firmware string (34 bytes w/null)
    40: + ASCIIZ firmware string (8 bytes w/null)

# Acknowledgments

* [Wireshark](http://wireshark.org) rocks!
* [**@ryanbinns**](http://github.com/ryanbinns)
  [`ttwatch`](http://github.com/ryanbinns/ttwatch) utility for syncing
  with TomTom GPS watches over *USB*
* [Lammert Bies's handy CRC page](http://www.lammertbies.nl/comm/info/crc-calculation.html)
  helped me figure out the correct CRC algorithm used by these
  devices.
* The [Bluetooth snooping developer feature of Android
  4.4](http://www.nowsecure.com/blog/2014/02/07/bluetooth-packet-capture-on-android-4-4/)!
