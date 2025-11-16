#!/usr/bin/env python3
import re
import subprocess
from pathlib import Path

infile = Path(r"C:\Temp\themis_wsl_failed_list.txt")
out_dir = Path(r"C:\Temp")

if not infile.exists():
    print("Input file missing:", infile)
    raise SystemExit(1)

lines = [l.strip() for l in infile.read_text(encoding='utf-8').splitlines() if l.strip()]
# sanitize: remove trailing timing like ' (123 ms)'
clean = []
for l in lines:
    cl = re.sub(r"\s*\(.*?ms\)\s*$", "", l)
    cl = cl.strip()
    if cl:
        clean.append(cl)
# dedupe preserving order
seen = set(); tests = []
for t in clean:
    if t not in seen:
        seen.add(t); tests.append(t)

print(f"Will run {len(tests)} tests in WSL")

for idx, test in enumerate(tests, start=1):
    safe = re.sub(r"[^0-9A-Za-z_.-]", "_", test)
    out = out_dir / f"themis_wsl_run_{idx:02d}_{safe}.txt"
    print(f"[{idx}/{len(tests)}] Running: {test} -> {out}")
    bash_cmd = f"cd /mnt/c/VCC/themis && ./build-wsl/themis_tests --gtest_filter='{test}'"
    cmd = ["wsl", "--", "bash", "-lc", bash_cmd]
    try:
        with out.open('w', encoding='utf-8') as f:
            proc = subprocess.run(cmd, stdout=f, stderr=subprocess.STDOUT, check=False, text=True)
        print(f" -> exit {proc.returncode}")
    except Exception as e:
        print(f"Failed to run {test}: {e}")

print("Done. Output files in:")
for p in sorted(out_dir.glob('themis_wsl_run_*.txt')):
    print(' -', p)
