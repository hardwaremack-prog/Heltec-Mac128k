# machost --testsound --ram 128: the test program writes a sawtooth (step 7) to the sound buffer.
import socket, struct, base64, os
s = socket.create_connection(('127.0.0.1', 5981))
key = base64.b64encode(os.urandom(16)).decode()
s.sendall(('GET / HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n' % key).encode())
r = b''
while b'\r\n\r\n' not in r: r += s.recv(1)
def recvn(n):
    b = b''
    while len(b) < n: b += s.recv(n - len(b))
    return b
m = os.urandom(4); p = b'a\x01'
s.sendall(bytes([0x82, 0x82]) + m + bytes(p[i] ^ m[i & 3] for i in range(2)))
got = 0
while got < 5:
    a, b = recvn(2); n = b & 127
    if n == 126: n = struct.unpack('>H', recvn(2))[0]
    f = recvn(n)
    if f[:1] == b'S':
        got += 1
        if got == 5:
            vol, smp = f[1], f[2:]
            print('volume', vol, 'samples', len(smp), 'first', list(smp[:6]),
                  'sawtooth ok', all(smp[i] == (i * 7) & 255 for i in range(370)))
