
import struct

hex_data = """
c301 e3a0 3c02 e28c 2000 e593 1822 e002
2000 e3a0 0a02 e211 017c 1543 fffe 1aff
2004 e282 0001 e211 00b8 114c 0022 1a00
2004 e282 0080 e211 001f 1a00 2004 e282
0040 e211 001c 1a00 2004 e282 0002 e211
0019 1a00 2004 e282 0004 e211 0016 1a00
2004 e282 0008 e211 0013 1a00 2004 e282
0010 e211 0010 1a00 2004 e282 0020 e211
000d 1a00 2004 e282 0c01 e211 000a 1a00
2004 e282 0c02 e211 0007 1a00 2004 e282
0b01 e211 0004 1a00 2004 e282 0b02 e211
0001 1a00 2004 e282 0a01 e211 00b2 e1c3
10bc e59f 1002 e081 0000 e591 ff10 e12f
c301 e3a0 3c02 e28c 2000 e593 1822 e002
2000 e3a0 0a02 e211 017c 1543 fffe 1aff
2004 e282 0001 e211 00b8 114c 0019 1a00
"""

# Remove newlines and spaces
hex_data = hex_data.replace('\n', '').replace(' ', '')

# Convert to bytes (little endian is assumed by the display, but let's check)
# The display was likely byte-by-byte.
# "c301 e3a0" -> c3 01 e3 a0.
# If it's 32-bit words, it's 0xe3a001c3? No, usually dumps are byte order.
# Let's assume the dump is byte stream.
data = bytes.fromhex(hex_data)

with open('isr.bin', 'wb') as f:
    f.write(data)
