import btle
import sys

def describe_uuid(uuid):
    bv = str(uuid)
    if bv[8:]=='-0000-1000-8000-00805f9b34fb':
        if bv[:4]=='0000':
            bv = bv[4:8]
        else:
            bv = bv[:8]
    if uuid.getCommonName() == str(uuid):
        return bv
    else:
        return "%s (%s)" % (bv, uuid.getCommonName())

def prop_list(c):
    return [v for k,v in c.propNames.iteritems() if c.properties & k]

p = None
while p is None:
    try:
        p=btle.Peripheral(sys.argv[1], btle.ADDR_TYPE_RANDOM)
    except btle.BTLEException as e:
        print e, vars(e)

ss = p.getServices()
h2c = {}
for s in ss:
    print "Service: UUID=%s, handles=0x%02x to 0x%02x" % (describe_uuid(s.uuid), s.hndStart, s.hndEnd)
    cc = s.getCharacteristics()
    for c in cc:
        print "  Char: UUID=%s, handle=0x%02x" % (describe_uuid(c.uuid), c.getHandle())
        print "    properties =>", ','.join(prop_list(c))
        if c.supportsRead():
            print "    read =>", repr(bytearray(c.read()))
