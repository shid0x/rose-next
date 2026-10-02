"""Little-endian binary reader/writer shared by every map codec.

Strings stay `bytes` end to end. Map files are EUC-KR/CP949 (and CP932 in
Jrose-origin data), and decoding then re-encoding is lossy for bytes that are
invalid in the guessed code page, which would break byte-exact round-trips.
Decode only for display.

Two string encodings occur in map files (docs/mapgen/FORMATS.md):

* bstr: u8 length + bytes. What the client and server read almost everywhere.
* pstr: the client's ReadPascalString (cfilesystemtriggervfs.cpp:326-356).
  1 byte if the length is < 0x80, else 2 bytes: (len & 0x7f) | 0x80, len >> 7.
  The same as LEB128 below 16384, and the same as bstr below 128.
"""

import struct

import numpy as np


class FormatError(Exception):
    """The bytes do not match the layout the codec expects."""


class Reader:
    def __init__(self, data, pos=0, end=None):
        self.data = data
        self.pos = pos
        self.end = len(data) if end is None else end

    def remaining(self):
        return self.end - self.pos

    def _take(self, n):
        if n < 0 or self.pos + n > self.end:
            raise FormatError("read of %d bytes at %d overruns end %d" % (n, self.pos, self.end))
        b = self.data[self.pos:self.pos + n]
        self.pos += n
        return b

    def raw(self, n):
        return bytes(self._take(n))

    def rest(self):
        return self.raw(self.end - self.pos)

    def u8(self):
        return self._take(1)[0]

    def i16(self):
        return struct.unpack("<h", self._take(2))[0]

    def i32(self):
        return struct.unpack("<i", self._take(4))[0]

    def f32(self):
        return struct.unpack("<f", self._take(4))[0]

    def f32s(self, n):
        return struct.unpack("<%df" % n, self._take(4 * n))

    def i32s(self, n):
        return struct.unpack("<%di" % n, self._take(4 * n))

    def bstr(self):
        return self.raw(self.u8())

    def pstr(self):
        first = self.u8()
        if first & 0x80:
            n = (self.u8() << 7) | (first - 0x80)
        else:
            n = first
        return self.raw(n)

    def array(self, dtype, count):
        """numpy array straight from the bytes: float bit patterns survive exactly."""
        dt = np.dtype(dtype)
        b = self._take(dt.itemsize * count)
        return np.frombuffer(b, dtype=dt, count=count).copy()


class Writer:
    def __init__(self):
        self.buf = bytearray()

    def tell(self):
        return len(self.buf)

    def getvalue(self):
        return bytes(self.buf)

    def raw(self, b):
        self.buf += b

    def u8(self, v):
        self.buf += struct.pack("<B", v)

    def i16(self, v):
        self.buf += struct.pack("<h", v)

    def i32(self, v):
        self.buf += struct.pack("<i", v)

    def f32(self, v):
        self.buf += struct.pack("<f", v)

    def f32s(self, vs):
        self.buf += struct.pack("<%df" % len(vs), *vs)

    def i32s(self, vs):
        self.buf += struct.pack("<%di" % len(vs), *vs)

    def bstr(self, b):
        if len(b) > 0xFF:
            raise FormatError("bstr longer than 255 bytes: %d" % len(b))
        self.u8(len(b))
        self.buf += b

    def pstr(self, b):
        n = len(b)
        if n < 0x80:
            self.u8(n)
        elif n < 0x8000:
            self.u8((n & 0x7F) | 0x80)
            self.u8(n >> 7)
        else:
            raise FormatError("pstr longer than 32767 bytes: %d" % n)
        self.buf += b

    def array(self, arr):
        self.buf += np.ascontiguousarray(arr).tobytes()
