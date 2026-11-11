#!/usr/bin/env python3
"""live.py NETDIMM_IP ADDR [fps [SECONDS] | log | reset]: the NAOMI build's live figures through the NetDIMM.

Runs ON the netbooter Raspberry (WiPi), with DragonMinded's netdimm.py in /sbin/piforce/dm_netboot, while the game
runs (naomi/naomi_dimmcomms.c answers the peeks). ADDR is the address of `nm_naomi_live`: the console's line
"NAOMI: NetDIMM peek/poke on; live figures at 0x8c......", or `sh-elf-nm daytona_native_naomi.elf | grep live`.

  fps [N]   one line a second for N seconds (default 30): fps, worst second, frame and render times
  log       the console's ring (the same lines TEST shows, all of them)
  reset     start the worst second over (poke 0)

Example: python3 live.py 10.0.0.4 0x8c2a1b40 fps 60
"""
import struct, sys, time
sys.path.append('/sbin/piforce/dm_netboot')
from netdimm import NetDimm, PeekPokeTypeEnum as T

FIELDS = ('magic version seconds fps vbls fps_worst frames_total frame_us render_us dimm_requests '
          'log_lines log_count log_width log_next heap_left').split()


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    dimm, base = NetDimm(sys.argv[1]), int(sys.argv[2], 0)
    what = sys.argv[3] if len(sys.argv) > 3 else 'fps'
    off = lambda a: a & 0x01ffffff  # firmware 3.17: an offset into the 32 MB of RAM
    peek = lambda a: dimm.peek(off(a), T.TYPE_LONG)
    read = lambda: dict(zip(FIELDS, (peek(base + 4 * i) for i in range(len(FIELDS)))))
    live = read()
    if live['magic'] != 0x5453454e:
        sys.exit('no live block at 0x%08x (magic %08x): wrong address, or the game is not running' % (base, live['magic']))
    if what == 'reset':
        dimm.poke(off(base + 0x14), T.TYPE_LONG, 0)
        print('worst second reset')
    elif what == 'log':
        n, width, written = live['log_count'], live['log_width'], peek(live['log_next'])
        for i in range(max(0, written - n), written + 1):
            at = live['log_lines'] + (i % n) * width
            raw = b''.join(struct.pack('<I', peek(at + 4 * k)) for k in range(width // 4))
            line = raw.split(b'\0', 1)[0].decode('ascii', 'replace')
            if line:
                print(line)
    else:
        seconds = int(sys.argv[4]) if len(sys.argv) > 4 else 30
        last = None
        for _ in range(seconds * 4):
            live = read()
            if live['seconds'] != last:
                last = live['seconds']
                print('%5ds  %2d fps (%d vblanks)  worst %2d  frame %5.2f ms  render %5.2f ms  heap left %d KB' % (
                    live['seconds'], live['fps'], live['vbls'], live['fps_worst'], live['frame_us'] / 1000,
                    live['render_us'] / 1000, live['heap_left'] // 1024), flush=True)
            time.sleep(0.25)


if __name__ == '__main__':
    main()
