#!/usr/bin/env python3
"""Project the measured host zone use onto 32-bit ARM.

Not a measurement on ARM -- arithmetic on two things that WERE measured: the
per-site byte totals and call counts from the instrumented zone, and the struct
sizes from a probe compiled for thumbv8m.main. Every site whose payload is a
known struct array is modelled and CHECKED against the host bytes; a model that
does not reproduce the host number is printed as a failure rather than used.
Sites with no model keep their payload and only lose the smaller zone header,
which is the conservative direction.

Usage: project32.py <wad> <headless-doom-binary> <sizes.json>
"""
import bisect, json, re, struct, subprocess, sys

wad, binary = sys.argv[1], sys.argv[2]
sz = json.load(open(sys.argv[3]))
HDR_HOST, HDR_ARM, ALIGN = 40, 28, 8

run = subprocess.run([binary, '-iwad', wad, '-warp', '1', '1'],
                     capture_output=True, text=True, env={'DG_TICKS': '600', 'PATH': '/usr/bin:/bin'})
out = run.stdout
ref = int(re.search(r'REFADDR Z_Malloc=0x([0-9a-f]+)', out).group(1), 16)
peak = int(re.search(r'nonpurge_peak=(\d+)', out).group(1))

# Read the symbol table out of the binary being run, never from a file passed
# in: a stale symbol table silently renames every site and the run still looks
# clean. This cost a wrong attribution once already.
table = []
for line in subprocess.run(['nm', '-n', binary], capture_output=True,
                           text=True).stdout.splitlines():
    p = line.split()
    if len(p) == 3 and p[1] in 'tT':
        table.append((int(p[0], 16), p[2]))
table.sort()
slide = ref - dict((n, a) for a, n in table)['_Z_Malloc']
addrs = [a for a, _ in table]

d = open(wad, 'rb').read()
_, numlumps, ofs = struct.unpack('<4sII', d[:12])
L = {}
for i in range(numlumps):
    pos, size, raw = struct.unpack('<II8s', d[ofs+i*16:ofs+i*16+16])
    L.setdefault(raw.rstrip(b'\0').decode('latin1').upper(), (pos, size))

def cnt(lump, stride):
    return L[lump][1] // stride

MODEL = {
    '_P_LoadSegs':       ('seg_t',       lambda c: cnt('SEGS', 12)),
    '_P_LoadLineDefs':   ('line_t',      lambda c: cnt('LINEDEFS', 14)),
    '_P_LoadSideDefs':   ('side_t',      lambda c: cnt('SIDEDEFS', 30)),
    '_P_LoadNodes':      ('node_t',      lambda c: cnt('NODES', 28)),
    '_P_LoadSectors':    ('sector_t',    lambda c: cnt('SECTORS', 26)),
    '_P_LoadSubsectors': ('subsector_t', lambda c: cnt('SSECTORS', 4)),
    '_P_LoadVertexes':   ('vertex_t',    lambda c: cnt('VERTEXES', 4)),
    '_P_SpawnMobj':      ('mobj_t',      lambda c: c),
}

def blk(payload, calls, hdr):
    return ((payload + hdr + ALIGN - 1)//ALIGN)*ALIGN if calls == 1 else payload + calls*hdr

rows, th, ta, failed = [], 0, 0, []
for m in re.finditer(r'SITE 0x([0-9a-f]+)\s+(\d+) in\s+(\d+) call', out):
    a = int(m.group(1), 16) - slide
    host, calls = int(m.group(2)), int(m.group(3))
    i = bisect.bisect_right(addrs, a) - 1
    name = table[i][1]
    if name in MODEL:
        st, f = MODEL[name]
        c = f(calls)
        pred = blk(c*sz[st][0], calls, HDR_HOST)
        arm = blk(c*sz[st][1], calls, HDR_ARM)
        ok = pred == host
        if not ok:
            failed.append((name, host, pred))
            arm = host - calls*(HDR_HOST-HDR_ARM)   # fall back to conservative
        basis = f'{c} x {st} {sz[st][0]}->{sz[st][1]}' + ('' if ok else '  MODEL FAILED, not used')
    else:
        arm = host - calls*(HDR_HOST-HDR_ARM)
        basis = 'no model; header only'
    th += host; ta += arm
    rows.append((host, arm, name, calls, basis))

print(f'{"site":<24}{"host":>9}{"arm32":>9}  basis')
for host, arm, name, calls, basis in rows:
    print(f'{name[1:]:<24}{host:>9,}{arm:>9,}  {basis}')
print(f'{"TOTAL (top sites)":<24}{th:>9,}{ta:>9,}')
print()
print(f'models that failed their control: {len(failed)} {failed}')
print(f'measured host non-purgeable peak : {peak:,} bytes')
print(f'ratio over modelled sites        : {ta/th:.3f}')
print(f'PROJECTED arm32 peak             : {peak*ta/th:,.0f} bytes = {peak*ta/th/1024:.0f} KiB')
