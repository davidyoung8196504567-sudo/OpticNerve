#!/usr/bin/env python3
"""Check a QNX SDP installation against the baseline pinned in this folder.

    python sdp/check_sdp.py [SDP_DIR]            check (read-only), exit 0 = matches
    python sdp/check_sdp.py [SDP_DIR] --record   rewrite the baseline files from SDP_DIR

SDP_DIR defaults to $QNX_SDP_DIR, then ~/qnx800_2.

The check does not trust the QNX Software Center. It reads the package
descriptors that QNX ships inside the SDP (.packages/metadata/<id>/<version>/
AssetDescriptor.json), which give the size and SHA-512 of every installed file,
and compares them with the files on disk and with packages.lock. The Software
Center's own package list is compared too, as a cross-check, when it is found.
Nothing is installed, updated or deleted.
"""
import argparse, base64, hashlib, json, os, platform, subprocess, sys, tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
LOCK = HERE / "packages.lock"
PATCHSET = HERE / "qsc-patchset.txt"
BASELINE = HERE / "BASELINE.txt"
HELLO = '#include <stdio.h>\nint main(void){ printf("hello from QNX\\n"); return 0; }\n'
TARGETS = {"gcc_ntoaarch64le": 0xB7, "gcc_ntox86_64": 0x3E}  # qcc variant -> ELF e_machine


def host_os():
    # QNX SDP 8.0 build hosts are Windows and Linux only.
    return {"Windows": "win32", "Linux": "linux"}.get(platform.system(), platform.system())


def digest(path, algo):
    h = hashlib.new(algo)
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h


def read_packages(sdp):
    """{(id, version): descriptor path} from the metadata QNX ships in the SDP."""
    meta = sdp / ".packages" / "metadata"
    return {(d.parent.name, d.name): d / "AssetDescriptor.json"
            for d in sorted(meta.glob("*/*")) if (d / "AssetDescriptor.json").is_file()}


def qsc_command():
    home = os.environ.get("QSC_HOME")
    for c in ([Path(home)] if home else []) + [Path("C:/qnx/QNX Software Center"), Path.home() / "qnx/qnxsoftwarecenter"]:
        for name in ("qnxsoftwarecenter_clt.bat", "qnxsoftwarecenter_clt"):
            if (c / name).is_file():
                return (["cmd", "/c"] if name.endswith(".bat") else []) + [str(c / name)]
    return None


def qsc_export(sdp):
    """The Software Center's own list: {(id, version): osgi filter}, plus the raw text. None if no CLI."""
    cmd = qsc_command()
    if not cmd:
        return None, None
    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp) / "patchset.txt"
        subprocess.run(cmd + ["-offline", "-destination", str(sdp), "-exportPatchSetText", str(out)],
                       capture_output=True, text=True)
        if not out.is_file():
            return None, None
        raw = out.read_text().replace("\r\n", "\n")
    pk = {}
    for line in raw.splitlines():
        f = line.split("/")
        if len(f) >= 4 and f[0].startswith("com."):
            pk[(f[0], f[1])] = "/".join(f[2:-1])
    return pk, raw


def check_files(sdp, packages):
    """Compare every file listed by every package with the disk. Returns (stats, problems, fingerprint)."""
    case_blind = (sdp.parent / sdp.name.swapcase()).exists()
    listed, owners, links = {}, {}, {}
    for (pid, ver), desc in packages.items():
        for a in json.loads(desc.read_text(encoding="utf-8", errors="replace"))["assets"]:
            listed[a["name"]] = (int(a["size"]), a["digestSHA512"])
            owners[a["name"]] = pid
            if "linkto" in a:
                links[a["name"]] = a["linkto"]
    folded = {}
    for name in listed:
        folded.setdefault(name.lower(), []).append(name)
    problems, collisions, nbytes = [], 0, 0
    for name, (size, want) in sorted(listed.items()):
        # Two packaged paths that differ only by case share one file on a case-blind disk.
        clash = case_blind and len(folded[name.lower()]) > 1
        p = sdp / name
        if name in links:
            # A symbolic link. Hosts without symlinks (Windows) install a copy of the target instead.
            target = p.parent / links[name]
            if p.is_symlink():
                ok = os.readlink(p) == links[name]
            elif p.is_file() and target.is_file():
                ok = digest(p, "sha512").digest() == digest(target, "sha512").digest()
            else:
                ok = p.exists() and target.exists()
            if not ok and not clash:
                problems.append(f"bad link  {name} -> {links[name]}  ({owners[name]})")
            continue
        if not p.is_file():
            problems.append(f"missing   {name}  ({owners[name]})")
            continue
        nbytes += size
        ok = p.stat().st_size == size and base64.b64encode(digest(p, "sha512").digest()).decode() == want
        if not ok and clash:
            collisions += 1
        elif not ok:
            problems.append(f"modified  {name}  ({owners[name]})")
    on_disk = {str(p.relative_to(sdp)).replace("\\", "/") for p in sdp.rglob("*") if p.is_file()}
    known = set(folded) if case_blind else set(listed)
    # A package does not list its own descriptor or signature; the lock pins the descriptor's hash.
    unlisted = {f".packages/metadata/{pid}/{ver}/{f}" for pid, ver in packages for f in ("AssetDescriptor.json", "package.sig")}
    for name in sorted(on_disk - unlisted):
        if (name.lower() if case_blind else name) not in known and not name.endswith(".pyc"):
            problems.append(f"extra     {name}")
    fp = hashlib.sha256("".join(f"{n} {s} {d}\n" for n, (s, d) in sorted(listed.items())).encode()).hexdigest()
    return {"files": len(listed), "bytes": nbytes, "case_collisions": collisions}, problems, fp


def check_toolchain(sdp):
    sub = {"win32": "win64/x86_64", "linux": "linux/x86_64"}[host_os()]
    env = dict(os.environ, QNX_HOST=(sdp / "host" / sub).as_posix(), QNX_TARGET=(sdp / "target/qnx").as_posix())
    qcc = sdp / "host" / sub / "usr/bin" / ("qcc.exe" if host_os() == "win32" else "qcc")
    problems = []
    with tempfile.TemporaryDirectory() as tmp:
        (Path(tmp) / "hello.c").write_text(HELLO)
        for variant, machine in TARGETS.items():
            r = subprocess.run([str(qcc), f"-V{variant}", "-o", variant, "hello.c"], cwd=tmp, env=env,
                               capture_output=True, text=True)
            out = Path(tmp) / variant
            if r.returncode or not out.is_file():
                problems.append(f"qcc -V{variant} failed (rc={r.returncode}): {r.stderr.strip()[:200]}")
            else:
                head = out.read_bytes()[:20]
                if head[:4] != b"\x7fELF" or int.from_bytes(head[18:20], "little") != machine:
                    problems.append(f"qcc -V{variant} did not produce the expected ELF")
    return problems


def read_lock():
    lock = {}
    for line in LOCK.read_text().splitlines():
        if line.strip() and not line.startswith("#"):
            pid, ver, sha, *rest = line.split()
            lock[(pid, ver)] = (sha, rest[0] if rest else "")
    return lock


def host_tag(filt):
    for os_name in ("win32", "linux"):
        if f"osgi.os={os_name}" in (filt or ""):
            return os_name
    return ""


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("sdp", nargs="?", default=os.environ.get("QNX_SDP_DIR", str(Path.home() / "qnx800_2")))
    ap.add_argument("--record", action="store_true", help="rewrite the baseline files from this installation")
    args = ap.parse_args()
    sdp = Path(args.sdp).resolve()
    packages = read_packages(sdp)
    if not packages:
        sys.exit(f"no package metadata under {sdp}")
    print(f"SDP: {sdp}  ({len(packages)} packages, host {host_os()})")
    shas = {k: digest(d, "sha256").hexdigest() for k, d in packages.items()}
    qsc, raw = qsc_export(sdp)
    failures = []

    print("1. Software Center list vs package metadata on disk ...", flush=True)
    if qsc is None:
        print("   skipped: Software Center CLI not found (set QSC_HOME)")
    elif set(qsc) != set(packages):
        failures += [f"Software Center lists {k[0]}/{k[1]} but its metadata is not on disk" for k in sorted(set(qsc) - set(packages))]
        failures += [f"on disk but unknown to the Software Center: {k[0]}/{k[1]}" for k in sorted(set(packages) - set(qsc))]
    else:
        print(f"   agree: {len(qsc)} packages")

    print("2. Every packaged file: present, size, SHA-512 ...", flush=True)
    stats, problems, fingerprint = check_files(sdp, packages)
    print(f"   {stats['files']} files, {stats['bytes'] / 2**30:.2f} GiB, {len(problems)} problems, "
          f"{stats['case_collisions']} case collisions; fingerprint {fingerprint[:16]}")
    failures += problems

    print("3. Toolchain: qcc builds hello.c for aarch64le and x86_64 ...", flush=True)
    tc = check_toolchain(sdp)
    print("   ok" if not tc else "   FAILED")
    failures += tc

    if args.record:
        if failures:
            print("\nNot recording: the installation itself has problems:")
            print("\n".join("  " + f for f in failures))
            sys.exit(1)
        lines = ["# QNX SDP package baseline. Written by check_sdp.py --record; do not edit by hand.",
                 "# <package id> <version> <sha256 of its AssetDescriptor.json> [host OS it is limited to]"]
        lines += [f"{pid} {ver} {shas[(pid, ver)]} {host_tag((qsc or {}).get((pid, ver)))}".rstrip()
                  for pid, ver in sorted(packages)]
        LOCK.write_text("\n".join(lines) + "\n", newline="\n")
        if raw:
            PATCHSET.write_text(raw, newline="\n")
        BASELINE.write_text(
            f"packages: {len(packages)}\nfiles: {stats['files']}\nbytes: {stats['bytes']}\n"
            f"fingerprint: {fingerprint}\ncase_collisions: {stats['case_collisions']}\n"
            f"recorded_on_host: {host_os()}\n", newline="\n")
        print(f"\nRecorded {len(packages)} packages to {LOCK.name}, {PATCHSET.name}, {BASELINE.name}. Review with git diff.")
        return

    print("4. Package set vs packages.lock ...", flush=True)
    lock = read_lock()
    here = host_os()
    base = dict(l.split(": ") for l in BASELINE.read_text().splitlines())
    same_host = base["recorded_on_host"] == here
    drift = []
    for k in sorted(set(lock) | set(packages)):
        name = f"{k[0]} {k[1]}"
        if k not in packages:
            if not lock[k][1] or lock[k][1] == here:
                drift.append(f"not installed: {name}")
        elif k not in lock:
            drift.append(f"not in the lock: {name}")
        elif lock[k][0] != shas[k]:
            drift.append(f"same version, different contents: {name}")
    if same_host and fingerprint != base["fingerprint"]:
        drift.append(f"file fingerprint {fingerprint[:16]} differs from the baseline {base['fingerprint'][:16]}")
    print(f"   {len(drift)} differences" + ("" if same_host else f" (baseline was recorded on {base['recorded_on_host']})"))
    failures += drift

    if failures:
        print(f"\nDOES NOT MATCH THE BASELINE: {len(failures)} problems")
        print("\n".join("  " + f for f in failures[:60]))
        if len(failures) > 60:
            print(f"  ... and {len(failures) - 60} more")
        sys.exit(1)
    print("\nMATCHES THE BASELINE")


if __name__ == "__main__":
    main()
