#!/usr/bin/python2
# -*- coding: utf-8 -*-
import btle, struct, collections, random, time, sys
import requests
import cStringIO as StringIO
from binascii import hexlify, unhexlify
from crc16_modbus import crc16_modbus as tt_crc16_streamer, _crc16_modbus as tt_crc16

class MyDelegate(btle.DefaultDelegate):
    __slots__ = ('handle','data','idata')
    def __init__(self):
        btle.DefaultDelegate.__init__(self)
        self.handle = self.data = self.idata = None
    def handleNotification(self, cHandle, data):
        l2s = {1:'B',2:'H',4:'L',8:'Q'}
        self.handle = cHandle
        self.data = data
        self.idata = struct.unpack('<'+l2s[len(data)], data)[0] if len(data) in l2s else None

h_d_id = collections.namedtuple('h_d_id', 'handle data idata')

def hnone(n, d=4):
    return ("0x%%0%dx" % d % n) if n is not None else None

def rda(p, handle=None, data=None, idata=None, timeout=1.0):
    p.delegate.handle, p.delegate.data, p.delegate.idata = None, None, None
    p.waitForNotifications(timeout)
    h,d,id = p.delegate.handle, p.delegate.data, p.delegate.idata
    if handle not in (None, h) or data not in (None, d) or idata not in (None, id):
        raise AssertionError, "expected (%s,%s,%s) got (%s,%s,%s)" % (hnone(handle), repr(data), hnone(idata), hnone(h), repr(d), hnone(id))
    return h_d_id(h,d,id)

########################################

def tt_send_command(p, cmdno):
    for tries in range(0,10):
        p.wr(0x25, cmdno, True)
        h, d, id = rda(p)
        if h==0x25 and id==1:
            return tries
        else:
            print "command %02x%02x%02x%02x failed %d times with %s, will retry" % (cmdno[0], cmdno[1], cmdno[2], cmdno[3], tries+1, ((hnone(h),repr(d),hnone(id))))
            time.sleep(1)
    return None

def tt_read_file(p, fileno, outf, limit=None, debug=False):
    # strange ordering: file 0x001234ab (ttwatch naming) becomes 0012ab34
    assert (fileno>>24)==0
    cmdno = bytearray((1, (fileno>>16)&0xff, fileno&0xff, (fileno>>8)&0xff))

    tt_send_command(p, cmdno)
    l = rda(p, 0x28).idata      # 0x28 = length in/out

    checker = tt_crc16_streamer()
    left = l
    packets = counter = 0
    checkit = done = False
    startat = time.time()
    while not done:
        h,d,id = rda(p)

        if h!=0x2b: raise AssertionError, (hnone(h),d,hnone(id))
        elif h==0x2b:
            packets += 1
            out = d[:-2]
            if packets%256==0:
                out = d[:-2]
                left -= len(d)-2
                if debug:
                    print "bytes left to read: %d of %d (%d/sec)" % (left, l, (packets*20) // (time.time()-startat))
                checkit = True
            elif left==0: # 1-2 straggler checksum byte(s)
                out = ''
                checkit = done = True
            elif left==len(d)-2: # remaining data + 2 checksum bytes
                out = d[:-2]
                left = 0
                checkit = done = True
            elif left==len(d)-1: # remaining data + 1 checksum byte to follow
                out = d[:-1]
                left = 0
            else:
                out = d
                left -= len(d)

            if debug>1:
                print "%s %s %d %d %d" % (hexlify(d), repr(d), left, checkit, done)
            outf.write(out)
            checker.update(d)

        if checkit:
            if checker.digest()!=0:
                raise AssertionError, checker.hexdigest()
            checker.reset()
            checkit = False
            if limit is not None and l-left>=limit:
                if debug:
                    print "prematurely ending file read after %d bytes" % (l-left)
                p.wr(0x2e, '\0\0\0\0', False)
                done = True
            else:
                counter += 1 # some sort of counter/pause added every 256 packets (otherwise a mega-slowdown occurs)
                if debug:
                    print "inserting counter %d -> handle 0x2e" % counter
                p.wr(0x2e, struct.pack('<L', counter), False)

    rda(p, 0x25, idata=0)

def tt_write_file(p, fileno, buf, expect_end=True, debug=False):
    # strange ordering: file 0x001234ab (ttwatch naming) becomes 0012ab34
    assert (fileno>>24)==0
    cmdno = bytearray((0, (fileno>>16)&0xff, fileno&0xff, (fileno>>8)&0xff))

    tt_send_command(p, cmdno)

    l = len(buf)
    p.wr(0x28, struct.pack('<L', len(buf)))     # 0x28 = length in/out

    builder = tt_crc16_streamer()
    pos = packets = counter = 0
    checkit = done = False
    startat = time.time()
    while not done:
        packets += 1
        if packets%256==0:
            out = buf[pos:pos+18]
            pos += 18
            if debug:
                print "bytes left to write: %d of %d (%d/sec)" % (l-pos, l, (packets*20) // (time.time()-startat))
            checkit = True
        elif pos+20>=l:
            out = buf[pos:]
            pos = l
            checkit = done = True
        else:
            out = buf[pos:pos+20]
            pos += 20

        builder.update(out)
        if checkit==True:
            out += struct.pack('<H', builder.digest())
            builder.reset()

        if debug>1:
            print "%s %s %d %d %d" % (hexlify(out), repr(out), pos, checkit, done)
        p.wr(0x2b, out[:20])
        if len(out)>20: # what if last "out" has length>18?
            p.wr(0x2b, out[20:])

        if checkit==True:
            counter += 1
            checkit = False
            if debug:
                print "expecting counter %d <- handle 0x2e (pos=%d, done=%s)" % (counter, pos, done)
            if not done or expect_end:
                rda(p, 0x2e, idata=counter, timeout=20)

    if expect_end:
        rda(p, 0x25, idata=0)

def tt_list_sub_files(p, fileno):
    # strange ordering: file 0x001234ab (ttwatch naming) becomes 0012ab34
    assert (fileno>>24)==0
    cmdno = bytearray((3, (fileno>>16)&0xff, fileno&0xff, (fileno>>8)&0xff))

    tt_send_command(p, cmdno)
    buf = bytearray()
    while True:
        h, d, id = rda(p)
        if h==0x2b: buf.extend(d)
        elif h==0x25 and id==0: break
        else: raise AssertionError, ("0x%02x"%h,d,id)

    # first uint16 is length, subsequent are file numbers offset from base
    subfiles = struct.unpack('<%dH'%(len(buf)/2), buf)
    assert subfiles[0]+1==len(subfiles)
    return tuple(fileno&0x00ff0000 + sf for sf in subfiles[1:])

def tt_delete_file(p, fileno):
    # strange ordering: file 0x001234ab (ttwatch naming) becomes 0012ab34
    assert (fileno>>24)==0
    cmdno = bytearray((4, (fileno>>16)&0xff, fileno&0xff, (fileno>>8)&0xff))

    tt_send_command(p, cmdno)
    buf = bytearray()
    while True:
        h, d, id = rda(p, timeout=10)
        if h==0x2b: buf.extend(d)
        elif h==0x25 and id==0: break
        else: raise AssertionError, (hnone(h),d,hnone(id))

    return buf

########################################

if len(sys.argv)!=3:
    print '''Need two arguments:
          ttblue.py <bluetooth-address> <pairing-code>
    OR    ttblue.py <bluetooth-address> pair

    Where bluetooth-address is the twelve-digit address
    of your TomTom GPS (E4:04:39:__:__:__) and
    pairing-code is either the previously established
    code used to pair a phone, or the string "pair"
    to create a new pairing.'''
    raise SystemExit

p = None
while p is None:
    try:
        p=btle.Peripheral(sys.argv[1], btle.ADDR_TYPE_RANDOM)
    except btle.BTLEException as e:
        print e
        time.sleep(1)
d = MyDelegate()
p.setDelegate(d)
p.wr = p.writeCharacteristic

try:
    # magic initialization/authentication sequence...
    # codes that are listed in file 0x0002000F should work

    if sys.argv[2]=="pair":
        code = int(raw_input("Code? "))
        newpair = True
    else:
        code = int(sys.argv[2])
        newpair = False
    code = struct.pack('<L', code)

    if newpair:
        # Android app uses this version RIGHT after pairing... but is it needed?
        p.wr(0x33, '\x01\0')
        p.wr(0x26, '\x01\0')
        p.wr(0x2f, '\x01\0')
        p.wr(0x29, '\x01\0')
        p.wr(0x2c, '\x01\0')
        p.wr(0x35, '\x01\x13\0\0\x01\x12\0\0', True)
        p.wr(0x32, code, True)
        response = rda(p, 0x32).idata
    else:
        p.wr(0x33, '\x01\0')
        p.wr(0x35, '\x01\x13\0\0\x01\x12\0\0', True)
        p.wr(0x26, '\x01\0')
        p.wr(0x32, code, True) # <-- pairing security code?
        response = rda(p, 0x32).idata

    if response == 1:
        print "Paired using code %s." % hexlify(code)
    else:
       raise RuntimeError, "Failed to pair with provided code"


    if 1:
        tt_delete_file(p, 0x00020002)
        tt_write_file(p, 0x00020002, 'Syncing…')

    if 1:
        print "Reading XML preferences (file 0x00f20000) ..."
        with open('preferences.xml', 'wb') as f:
            tt_read_file(p, 0x00f20000, f)
            print "Got %d bytes" % f.tell()

    if 1:
        print "Checking activity file status..."
        files = tt_list_sub_files(p, 0x00910000)
        print "Got %d activities: %s" % (len(files), files)

        for ii,fn in enumerate(files):
            tt_delete_file(p, 0x00020002)
            tt_write_file(p, 0x00020002, 'Activity %d/%d…' % (ii+1, len(files)))

            print "Saving activity file 0x%08x.ttbin..." % fn
            with open('0x%08x.ttbin'%fn, 'wb') as f:
                tt_read_file(p, fn, f, debug=True)
                print "  got %d bytes." % f.tell()
            print "  saved to %s" % f.name

            print "Deleting activity file 0x%08x..." % fn
            print repr( tt_delete_file(p, fn) )

            tt_delete_file(p, 0x00020002)
            tt_write_file(p, 0x00020002, '%d/%d synced.' % (ii+1, len(files)))

    if 1:
        gqf = requests.get('http://gpsquickfix.services.tomtom.com/fitness/sifgps.f2p3enc.ee').content
        print "Updating QuickGPSFix..."
        tt_delete_file(p, 0x00020002)
        tt_write_file(p, 0x00020002, 'QuickGPSFix-ing')
        tt_delete_file(p, 0x00010100)
        tt_write_file(p, 0x00010100, gqf, debug=True, expect_end=True)
#        p.disconnect()

    if 1:
        tt_delete_file(p, 0x00020002)
        tt_write_file(p, 0x00020002, 'ttblue, yo!')
except KeyboardInterrupt:
    pass
finally:
    p.disconnect()
