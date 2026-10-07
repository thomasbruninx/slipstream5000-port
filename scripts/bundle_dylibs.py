#!/usr/bin/env python3
"""Copies the non-system dylibs a Mach-O binary needs (recursively) into a Frameworks folder and rewrites the install
names so the app is self-contained: python3 bundle_dylibs.py <binary> <Frameworks dir>
Libraries inside the bundle reference each other through @loader_path, the executable through @rpath."""
import os, shutil, subprocess, sys

def deps(path):
    out = subprocess.check_output(['otool', '-L', path], text=True).splitlines()[1:]
    res = []
    for l in out:
        name = l.strip().split(' (')[0]
        if name.startswith(('/usr/lib/', '/System/')) or name == path: continue
        res.append(name)
    return res

def real(p):
    return os.path.realpath(p)

def main(binary, fw):
    os.makedirs(fw, exist_ok=True)
    done = {}  # original path -> bundled path
    queue = [(binary, True)]
    seen_targets = {}
    while queue:
        cur, is_exe = queue.pop(0)
        target = cur if is_exe else done[cur]
        for d in deps(target if is_exe else done[cur]):
            src = d
            if d.startswith('@'):
                continue  # already relative (bundled earlier)
            base = os.path.basename(real(src))
            # keep the soname-style name (libfoo.3.dylib) used in the load command
            base = os.path.basename(src)
            dst = os.path.join(fw, base)
            if src not in done:
                if not os.path.exists(dst):
                    shutil.copy2(real(src), dst)
                    os.chmod(dst, 0o755)
                done[src] = dst
                subprocess.check_call(['install_name_tool', '-id', '@loader_path/' + base, dst], stderr=subprocess.DEVNULL)
                queue.append((src, False))
            new = ('@rpath/' if is_exe else '@loader_path/') + base
            subprocess.check_call(['install_name_tool', '-change', d, new, target], stderr=subprocess.DEVNULL)
    if os.path.isfile(binary):
        subprocess.call(['install_name_tool', '-add_rpath', '@executable_path/../Frameworks', binary], stderr=subprocess.DEVNULL)
    print('bundled %d libraries into %s' % (len(done), fw))

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
