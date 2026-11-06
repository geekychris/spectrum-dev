"""Line-level profile: like profile.py, but each sampled label is mapped to
the C line before it in zsdcc's --c-code-in-asm output. Usage:
lineprof.py NEX frames skip file.asm [file.asm...]"""
import collections
import re
import subprocess
import sys

nex, frames, skip, asms = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4:]
lab2line = {}
for a in asms:
    cur = None
    for line in open(a):
        m = re.match(r";(src/\S+:\d+):\s*(.*)", line)
        if m:
            cur = m.group(1) + " " + m.group(2)[:60]
            continue
        m = re.match(r"(\w+):", line)
        if m and cur:
            lab2line[m.group(1)] = cur
out = subprocess.run([sys.executable, __file__.replace("lineprof", "profile"), nex, frames, skip, "--raw"],
                     capture_output=True, text=True).stdout
lines = collections.Counter()
for l in out.splitlines():
    pct, name = l.split(None, 1)
    lines[lab2line.get(name, name)] += float(pct.rstrip("%"))
for n, c in lines.most_common(45):
    print(f"{c:5.1f}% {n}")
