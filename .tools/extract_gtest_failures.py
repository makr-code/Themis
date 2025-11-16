#!/usr/bin/env python3
import re
from pathlib import Path

def extract_failed_tests(path: Path):
    if not path.exists():
        return []
    pattern = re.compile(r'^\s*\[\s*FAILED\s*\]\s*(.+)$')
    found = []
    with path.open('r', encoding='utf-8', errors='replace') as f:
        for line in f:
            m = pattern.match(line)
            if m:
                name = m.group(1).strip()
                if name:
                    found.append(name)
    return sorted(set(found))


def write_list(lst, path: Path):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('w', encoding='utf-8') as f:
        for item in lst:
            f.write(item + '\n')


def main():
    wsl_file = Path(r"C:\Temp\themis_wsl_full_tests.txt")
    win_file = Path(r"C:\Temp\themis_windows_full_tests.txt")

    out_wsl = Path(r"C:\Temp\themis_wsl_failed_list.txt")
    out_win = Path(r"C:\Temp\themis_win_failed_list.txt")
    out_only_wsl = Path(r"C:\Temp\themis_only_in_wsl.txt")
    out_only_win = Path(r"C:\Temp\themis_only_in_win.txt")
    out_common = Path(r"C:\Temp\themis_common_failures.txt")

    wsl_tests = extract_failed_tests(wsl_file)
    win_tests = extract_failed_tests(win_file)

    write_list(wsl_tests, out_wsl)
    write_list(win_tests, out_win)

    set_wsl = set(wsl_tests)
    set_win = set(win_tests)

    only_wsl = sorted(set_wsl - set_win)
    only_win = sorted(set_win - set_wsl)
    common = sorted(set_wsl & set_win)

    write_list(only_wsl, out_only_wsl)
    write_list(only_win, out_only_win)
    write_list(common, out_common)

    print(f"WSL tests extracted: {len(wsl_tests)}")
    print(f"Win tests extracted: {len(win_tests)}")
    print(f"Only in WSL: {len(only_wsl)}")
    print(f"Only in Win: {len(only_win)}")
    print(f"Common failures: {len(common)}")
    print("Wrote files:")
    for p in [out_wsl, out_win, out_only_wsl, out_only_win, out_common]:
        print(f" - {p}")
    if wsl_tests:
        print('\nWSL extracted tests:')
        for t in wsl_tests:
            print(' - ' + t)
    if win_tests:
        print('\nWin extracted tests:')
        for t in win_tests:
            print(' - ' + t)

if __name__ == '__main__':
    main()
