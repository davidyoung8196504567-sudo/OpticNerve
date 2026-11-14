"""RFB tap: a TCP proxy that logs the input messages a VNC client sends.

    python3 rfb_tap.py LISTEN_PORT SERVER_HOST SERVER_PORT LOG

Passes bytes through unchanged in both directions. From the client-to-server stream it writes one line per
KeyEvent, PointerEvent and other message to LOG (flushed per line). Assumes RFB 3.8 with VNC password
authentication (security type 2), one client at a time.
"""
import socket, struct, sys, threading

listen_port, host, port, log_path = int(sys.argv[1]), sys.argv[2], int(sys.argv[3]), sys.argv[4]
log = open(log_path, "a", buffering=1)


def pump(src, dst):
    try:
        while True:
            data = src.recv(65536)
            if not data:
                break
            dst.sendall(data)
    except OSError:
        pass
    for s in (src, dst):
        try:
            s.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass


class Reader:
    def __init__(self, src, dst):
        self.src, self.dst = src, dst

    def read(self, n):
        buf = b""
        while len(buf) < n:
            chunk = self.src.recv(n - len(buf))
            if not chunk:
                raise EOFError
            self.dst.sendall(chunk)
            buf += chunk
        return buf


def client_to_server(client, server):
    r = Reader(client, server)
    try:
        log.write("version %r\n" % r.read(12))
        log.write("security type chosen %d\n" % r.read(1)[0])
        r.read(16)  # VNC authentication response
        log.write("ClientInit shared=%d\n" % r.read(1)[0])
        while True:
            t = r.read(1)[0]
            if t == 0:
                b = r.read(19)
                bpp, depth, big, true, rm, gm, bm, rs, gs, bs = struct.unpack(">BBBBHHHBBB", b[3:16])
                log.write(f"SetPixelFormat bpp={bpp} depth={depth} max={rm},{gm},{bm} shift={rs},{gs},{bs}\n")
            elif t == 2:
                n = struct.unpack(">xH", r.read(3))[0]
                encs = struct.unpack(">%di" % n, r.read(4 * n))
                log.write(f"SetEncodings {list(encs)}\n")
            elif t == 3:
                inc, x, y, w, h = struct.unpack(">BHHHH", r.read(9))
                log.write(f"FramebufferUpdateRequest inc={inc} {w}x{h} at {x},{y}\n")
            elif t == 4:
                down, key = struct.unpack(">BxxI", r.read(7))
                log.write(f"KeyEvent {'down' if down else 'up'} 0x{key:04x}\n")
            elif t == 5:
                mask, x, y = struct.unpack(">BHH", r.read(5))
                log.write(f"PointerEvent mask={mask} {x},{y}\n")
            elif t == 6:
                n = struct.unpack(">xxxi", r.read(7))[0]
                if n < 0:  # extended clipboard message: the length is negated
                    log.write(f"ClientCutText extended, {-n} bytes: {r.read(-n).hex()}\n")
                else:
                    log.write(f"ClientCutText {r.read(n)!r}\n")
            elif t == 150:
                r.read(9)
                log.write("EnableContinuousUpdates\n")
            elif t == 248:
                b = r.read(8)
                r.read(b[7])
                log.write("ClientFence\n")
            elif t == 251:
                b = r.read(7)
                r.read(16 * b[5])
                log.write("SetDesktopSize\n")
            else:
                log.write(f"UNKNOWN message type {t}; passing the rest through unparsed\n")
                pump(client, server)
                return
    except (EOFError, OSError):
        log.write("client closed\n")


srv = socket.socket()
srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind(("127.0.0.1", listen_port))
srv.listen(1)
while True:
    client, _ = srv.accept()
    server = socket.create_connection((host, port))
    log.write("--- connection\n")
    threading.Thread(target=pump, args=(server, client), daemon=True).start()
    client_to_server(client, server)
    for s in (client, server):
        try:
            s.close()
        except OSError:
            pass
