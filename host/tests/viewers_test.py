# Connects to machost (--testrom --ram 128) over VNC (raw + hextile) and the browser WebSocket,
# decodes the screen each way and checks it against the test pattern (byte i = i & 255).
import socket, struct, base64, os, time, sys

W, H = 512, 342
expect = bytes(i & 255 for i in range(W // 8 * H))

def recvn(s, n):
    b = b''
    while len(b) < n:
        c = s.recv(n - len(b))
        if not c: raise EOFError
        b += c
    return b

def pix(fb, x, y): return (fb[y * 64 + x // 8] >> (7 - x % 8)) & 1

def vnc(encs):
    s = socket.create_connection(('127.0.0.1', 5900))
    assert recvn(s, 12) == b'RFB 003.008\n'
    s.sendall(b'RFB 003.008\n'); n = recvn(s, 1)[0]; recvn(s, n); s.sendall(b'\x01'); recvn(s, 4)
    s.sendall(b'\x01'); w, h = struct.unpack('>HH', recvn(s, 4)); recvn(s, 16)
    recvn(s, struct.unpack('>I', recvn(s, 4))[0])
    assert (w, h) == (W, H)
    # 8bpp true colour (bgr233) to exercise pixel formats
    s.sendall(b'\x00\x00\x00\x00' + bytes([8, 8, 0, 1, 0, 7, 0, 7, 0, 3, 0, 3, 6, 0, 0, 0]))
    s.sendall(struct.pack('>BBH', 2, 0, len(encs)) + b''.join(struct.pack('>i', e) for e in encs))
    s.sendall(struct.pack('>BBHHHH', 3, 0, 0, 0, W, H))
    img = [[None] * W for _ in range(H)]
    got = 0
    while got < W * H:
        t = recvn(s, 1)[0]
        assert t == 0, t
        recvn(s, 1); nr = struct.unpack('>H', recvn(s, 2))[0]
        for _ in range(nr):
            x, y, w, h, e = struct.unpack('>HHHHi', recvn(s, 12))
            if e == 0:
                d = recvn(s, w * h)
                for j in range(h):
                    for i in range(w): img[y + j][x + i] = d[j * w + i]
            else:
                bg = fg = None
                for ty in range(y, y + h, 16):
                    for tx in range(x, x + w, 16):
                        tw, th = min(16, x + w - tx), min(16, y + h - ty)
                        f = recvn(s, 1)[0]
                        if f & 1:
                            d = recvn(s, tw * th)
                            for j in range(th):
                                for i in range(tw): img[ty + j][tx + i] = d[j * tw + i]
                            continue
                        if f & 2: bg = recvn(s, 1)[0]
                        if f & 4: fg = recvn(s, 1)[0]
                        for j in range(th):
                            for i in range(tw): img[ty + j][tx + i] = bg
                        if f & 8:
                            for _ in range(recvn(s, 1)[0]):
                                c = fg if not (f & 16) else recvn(s, 1)[0]
                                a, b = recvn(s, 2)
                                sx, sy, sw, sh = a >> 4, a & 15, (b >> 4) + 1, (b & 15) + 1
                                for j in range(sh):
                                    for i in range(sw): img[ty + sy + j][tx + sx + i] = c
            got += w * h
    s.close()
    white = 0xFF  # bgr233 white
    bad = sum(1 for yy in range(H) for xx in range(W) if (img[yy][xx] == white) == bool(pix(expect, xx, yy)))
    return bad

def ws():
    s = socket.create_connection(('127.0.0.1', 5981))
    key = base64.b64encode(os.urandom(16)).decode()
    s.sendall(('GET / HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n'
               'Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n' % key).encode())
    resp = b''
    while b'\r\n\r\n' not in resp: resp += s.recv(1)
    import hashlib
    acc = base64.b64encode(hashlib.sha1((key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').encode()).digest())
    assert b'101' in resp and acc in resp, resp
    def send(p):
        m = os.urandom(4)
        hdr = bytes([0x82, 0x80 | len(p)]) + m
        s.sendall(hdr + bytes(p[i] ^ m[i & 3] for i in range(len(p))))
    def recvframe():
        a, b = recvn(s, 2); n = b & 127
        if n == 126: n = struct.unpack('>H', recvn(s, 2))[0]
        return recvn(s, n)
    send(b'a\x01')
    fb = bytearray(64 * 352)
    tiles = 0; sound = 0
    t0 = time.time()
    s.settimeout(2)   # silence isn't sent, so stop when nothing more arrives
    while tiles < 32 * 22 and time.time() - t0 < 5:
        try: f = recvframe()
        except socket.timeout: break
        if f[0:1] == b'T':
            n = struct.unpack('>H', f[1:3])[0]; p = 3
            for _ in range(n):
                tx, ty = f[p], f[p + 1]; p += 2
                for r in range(16):
                    fb[(ty * 16 + r) * 64 + tx * 2] = f[p]; fb[(ty * 16 + r) * 64 + tx * 2 + 1] = f[p + 1]; p += 2
                tiles += 1
            send(b'A')
        elif f[0:1] == b'S':
            sound += 1
    s.close()
    return bytes(fb[:64 * H]) == expect, tiles, sound

print('vnc raw: bad pixels', vnc([0]))
print('vnc hextile: bad pixels', vnc([5, 0]))
print('websocket: screen ok, tiles, sound messages =', ws())
