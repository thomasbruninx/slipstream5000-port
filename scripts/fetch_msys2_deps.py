#!/usr/bin/env python3
"""Downloads MSYS2's prebuilt MinGW-w64 packages (the ones `pacman -S mingw-w64-x86_64-fluidsynth` would install) and unpacks them into a sysroot,
so that the cross compiler can link FluidSynth and the DLLs can be shipped. Needs network, python3 and zstd (brew install zstd / apt install zstd).

usage: fetch_msys2_deps.py SYSROOT [package ...]      (default package: fluidsynth; names without the mingw-w64-x86_64- prefix)
The packages end up in SYSROOT/mingw64/{bin,lib,include,...}; downloads are cached in SYSROOT/.cache."""
import os, re, subprocess, sys, tarfile, urllib.request, io

REPO = "https://repo.msys2.org/mingw/mingw64"
PREFIX = "mingw-w64-x86_64-"

def get(url, dest=None):
    if dest and os.path.exists(dest):
        return dest
    with urllib.request.urlopen(url) as r:
        data = r.read()
    if dest:
        with open(dest, "wb") as f:
            f.write(data)
        return dest
    return data

def untar_zst(path, out):
    p = subprocess.run(["zstd", "-dc", path], stdout=subprocess.PIPE, check=True)
    with tarfile.open(fileobj=io.BytesIO(p.stdout)) as t:
        members = [m for m in t.getmembers() if not m.name.startswith(".")]  # skip .PKGINFO / .MTREE / .BUILDINFO
        t.extractall(out, members=members)
    return p

def main():
    root = os.path.abspath(sys.argv[1])
    wanted = [PREFIX + n if not n.startswith("mingw-w64-") else n for n in (sys.argv[2:] or ["fluidsynth"])]
    cache = os.path.join(root, ".cache")
    os.makedirs(cache, exist_ok=True)
    dbfile = get(REPO + "/mingw64.db", os.path.join(cache, "mingw64.db"))
    pkgs, provides = {}, {}
    p = subprocess.run(["zstd", "-dc", dbfile], stdout=subprocess.PIPE)
    raw = p.stdout if p.returncode == 0 and p.stdout else open(dbfile, "rb").read()  # the database is a gzip or zstd tarball
    with tarfile.open(fileobj=io.BytesIO(raw)) as t:
        for m in t.getmembers():
            if not m.name.endswith("/desc"):
                continue
            text = t.extractfile(m).read().decode()
            f = {}
            for sec in re.split(r"\n\n", text.strip()):
                lines = sec.split("\n")
                f[lines[0].strip("%")] = lines[1:]
            name = f["NAME"][0]
            pkgs[name] = {"file": f["FILENAME"][0], "deps": [re.split(r"[<>=]", d)[0] for d in f.get("DEPENDS", [])]}
            provides[name] = name
            for pr in f.get("PROVIDES", []):
                provides.setdefault(re.split(r"[<>=]", pr)[0], name)
    todo, seen = list(wanted), []
    while todo:
        n = todo.pop()
        n = provides.get(n, n)
        if n in seen:
            continue
        if n not in pkgs:
            sys.exit("package not found in the MSYS2 repository: " + n)
        seen.append(n)
        todo += pkgs[n]["deps"]
    for n in sorted(seen):
        fn = pkgs[n]["file"]
        print("  " + fn)
        path = get(REPO + "/" + fn, os.path.join(cache, fn))
        untar_zst(path, root)
    print("%d packages unpacked into %s" % (len(seen), root))

main()
