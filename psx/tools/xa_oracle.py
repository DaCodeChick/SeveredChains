#!/usr/bin/env python3
"""Generate psx/testdata/xa_adpcm.bin — differential goldens for the XA decoder.

The expected PCM here is NOT hand-written and NOT produced by the C decoder. It
is produced by the untouched Java reference (`legend.core.spu.XaAdpcm`) taken
from the Severed-Chains reference repository, run through a throwaway harness
under a scratch directory. That is what makes `xa.byte_exact_against_oracle`
evidence rather than a tautology: if the C transcription drifts from the
reference, this file disagrees with it.

Inputs are synthetic (a documented LCG plus a few hand-built shapes), so no
proprietary game data is read or shipped. The generator deliberately produces
control bytes whose shift nibble exceeds 12 — malformed for a real encoder, but
exactly the territory where Java's masked shift and C's undefined shift part
company, so the agreement there is pinned rather than assumed.

Usage:
  python3 psx/tools/xa_oracle.py <reference-repo-root> <out.bin>

Requirements: JDK 21+ and a JDK-bundled fastutil jar, because XaAdpcm uses
fastutil's ShortArrayList. If either is missing the script exits non-zero with a
message; it never writes a partial file. The committed .bin stays usable
without Java — this tool only regenerates it.
"""

import os
import random
import shutil
import struct
import subprocess
import sys
import tempfile

MAGIC = b"PSXXAFIX"
VERSION = 1
SECTOR = 2352

JAVA_DRIVER = '''
import legend.core.spu.XaAdpcm;
import java.nio.*;
import java.nio.file.*;

public class XaDrv {
  public static void main(String[] args) throws Exception {
    byte[] sector = Files.readAllBytes(Path.of(args[0]));
    byte coding = Integer.decode(args[1]).byteValue();
    short[] pcm = XaAdpcm.decode(sector, coding);
    ByteBuffer bb = ByteBuffer.allocate(pcm.length * 2).order(ByteOrder.LITTLE_ENDIAN);
    for (short s : pcm) { bb.putShort(s); }
    Files.write(Path.of(args[2]), bb.array());
  }
}
'''

SHIM_SHORTLIST = '''
package it.unimi.dsi.fastutil.shorts;
import java.util.List;
public interface ShortList extends List<Short> {
  short getShort(int i);
  int size();
  boolean add(short s);
  boolean addAll(ShortList l);
  short[] toShortArray();
}
'''

SHIM_ARRAYLIST = '''
package it.unimi.dsi.fastutil.shorts;
import java.util.AbstractList;

/// Harness-only stand-in, equal to fastutil's ShortArrayList for the four
/// operations XaAdpcm performs (append, append-list, index, copy). It exists
/// because the JDK ships no fastutil; it is not shipped and not linked into
/// anything the product builds.
public class ShortArrayList extends AbstractList<Short> implements ShortList {
  private short[] a = new short[64];
  private int n = 0;
  private void grow(int need) {
    int cap = a.length;
    while (cap < need) { cap *= 2; }
    short[] b = new short[cap];
    System.arraycopy(a, 0, b, 0, n);
    a = b;
  }
  public short getShort(int i) {
    if (i < 0 || i >= n) { throw new IndexOutOfBoundsException(); }
    return a[i];
  }
  public Short get(int i) { return getShort(i); }
  public int size() { return n; }
  public boolean add(short s) { grow(n + 1); a[n++] = s; return true; }
  public boolean add(Short s) { return add(s.shortValue()); }
  public boolean addAll(ShortList l) {
    int m = l.size();
    grow(n + m);
    for (int i = 0; i < m; i++) { a[n++] = l.getShort(i); }
    return m != 0;
  }
  public short[] toShortArray() {
    short[] r = new short[n];
    System.arraycopy(a, 0, r, 0, n);
    return r;
  }
}
'''


def find_fastutil():
    """Locate a fastutil jar that actually carries ShortArrayList."""
    roots = [os.path.expanduser("~/.gradle"), os.path.expanduser("~/.m2"), "/opt/homebrew"]
    for root in roots:
        if not os.path.isdir(root):
            continue
        for dirpath, _dirs, files in os.walk(root):
            for name in files:
                if name.startswith("fastutil") and name.endswith(".jar"):
                    path = os.path.join(dirpath, name)
                    import zipfile
                    try:
                        with zipfile.ZipFile(path) as zf:
                            if "it/unimi/dsi/fastutil/shorts/ShortArrayList.class" in zf.namelist():
                                return path
                    except Exception:
                        continue
    return None


def control_byte(shift_nibble, filter_index):
    return (shift_nibble & 0x0F) | ((filter_index & 0x03) << 4)


def build_cases():
    """(name, coding, sector bytes) — synthetic, deterministic, no game data."""
    cases = []

    def ctl_only(pattern):
        s = [0] * SECTOR
        for p in range(18):
            for b in range(4):
                for k in range(2):
                    s[24 + p * 128 + 4 + b * 2 + k] = pattern(p, b, k)
        return s

    # Silence: proves the zero-input path and gives a cheap canary.
    cases.append(("silence_stereo", 0x05, [0] * SECTOR))
    cases.append(("silence_mono", 0x04, [0] * SECTOR))

    # Control nibbles 13..15 -> shift = 12 - nibble is negative. Java masks the
    # shift count to 5 bits; plain C would be undefined. Pinned deliberately.
    for i, nib in enumerate((13, 14, 15)):
        cases.append(("badshift%d" % nib, 0x05,
                      ctl_only(lambda p, b, k, n=nib: control_byte(n, 3))))

    # Every filter index against a saturating payload: the clamp rails are where
    # a reformulated prediction term would first disagree.
    for f in range(4):
        s = ctl_only(lambda p, b, k: control_byte(2, f))
        for p in range(18):
            for blk in range(4):
                for i in range(28):
                    s[24 + p * 128 + 16 + blk + i * 4] = 0x77
        cases.append(("saturate_f%d" % f, 0x05, s))

    # Structured and pseudo-random payloads, stereo and mono. 0x05 = 4-bit
    # stereo, 0x04 = 4-bit mono; both inside the implemented range.
    rng = random.Random(20261029)
    for i in range(4):
        body = [rng.randrange(256) for _ in range(SECTOR)]
        cases.append(("random_stereo%d" % i, 0x05, list(body)))
        cases.append(("random_mono%d" % i, 0x04, list(body)))

    return cases


def main():
    if len(sys.argv) != 3:
        print("usage: xa_oracle.py <reference-repo-root> <out.bin>", file=sys.stderr)
        return 2

    ref_root, out_path = sys.argv[1], sys.argv[2]

    xa_java = os.path.join(ref_root, "src/main/java/legend/core/spu/XaAdpcm.java")
    ref_java = os.path.join(ref_root, "src/main/java/legend/core/memory/Ref.java")
    for f in (xa_java, ref_java):
        if not os.path.isfile(f):
            print("reference source not found: %s" % f, file=sys.stderr)
            print("pass the Severed-Chains reference repository root", file=sys.stderr)
            return 1

    java = shutil.which("java")
    javac = shutil.which("javac")
    if not (java and javac):
        print("no JDK on PATH; goldens cannot be regenerated (the committed "
              "fixture still works)", file=sys.stderr)
        return 1

    with tempfile.TemporaryDirectory(prefix="sc-xa-oracle-") as work:
        src = os.path.join(work, "src")
        for d in ("legend/core/spu", "legend/core/memory", "it/unimi/dsi/fastutil/shorts"):
            os.makedirs(os.path.join(src, d), exist_ok=True)
        shutil.copy(xa_java, os.path.join(src, "legend/core/spu/XaAdpcm.java"))
        # Ref.java is copied with its javax.annotation import stripped: that
        # package is not in the JDK and the annotation has no runtime meaning.
        ref_text = open(ref_java, encoding="utf-8").read()
        ref_text = ref_text.replace("import javax.annotation.Nullable;\n", "")
        ref_text = ref_text.replace("  @Nullable\n", "")
        with open(os.path.join(src, "legend/core/memory/Ref.java"), "w", encoding="utf-8") as fh:
            fh.write(ref_text)
        with open(os.path.join(src, "it/unimi/dsi/fastutil/shorts/ShortList.java"), "w") as fh:
            fh.write(SHIM_SHORTLIST)
        with open(os.path.join(src, "it/unimi/dsi/fastutil/shorts/ShortArrayList.java"), "w") as fh:
            fh.write(SHIM_ARRAYLIST)
        with open(os.path.join(src, "XaDrv.java"), "w") as fh:
            fh.write(JAVA_DRIVER)

        cp = find_fastutil()
        classes = os.path.join(work, "classes")
        os.makedirs(classes)
        # The shim is only used when no real fastutil jar is available.
        compile_cp = []
        if cp:
            compile_cp = ["-cp", cp]
            shutil.rmtree(os.path.join(src, "it/unimi/dsi/fastutil/shorts"))
        rc = subprocess.run([javac, "-nowarn", "-d", classes] + compile_cp
                            + ["-sourcepath", src]
                            + [os.path.join(dp, f) for dp, _d, fs in os.walk(src)
                               for f in fs if f.endswith(".java")],
                            capture_output=True, text=True)
        if rc.returncode != 0:
            print("oracle compile failed:\n%s" % rc.stderr, file=sys.stderr)
            return 1

        records = []
        for name, coding, sector in build_cases():
            sec_path = os.path.join(work, "case.sec")
            out_pcm = os.path.join(work, "case.pcm")
            with open(sec_path, "wb") as fh:
                fh.write(bytes(sector))
            coding_arg = "0x%02X" % coding
            run = subprocess.run([java, "-cp", classes, "XaDrv", sec_path, coding_arg, out_pcm],
                                 capture_output=True, text=True)
            if run.returncode != 0:
                print("oracle rejected case %s: %s" % (name, run.stderr.strip()),
                      file=sys.stderr)
                return 1
            pcm = open(out_pcm, "rb").read()
            if len(pcm) == 0:
                print("oracle produced no samples for %s" % name, file=sys.stderr)
                return 1
            print("  %-18s coding=0x%02X -> %d samples" % (name, coding, len(pcm) // 2))
            records.append((name, coding, bytes(sector), pcm))

    # Fixture container (little-endian, read byte by byte by the test):
    #   "PSXXAFIX" u32 version u32 case_count
    #   then per case: u8 name_len, name, u8 coding, u32 sector_len,
    #                  u32 pcm_len, sector bytes, pcm bytes
    # Names are carried so a failure prints which shape broke, not just an index.
    blob = bytearray()
    blob += MAGIC + struct.pack("<II", VERSION, len(records))
    for name, coding, sector, pcm in records:
        tag = name.encode("ascii")
        blob += struct.pack("<B", len(tag)) + tag
        blob += struct.pack("<BII", coding, len(sector), len(pcm))
        blob += sector + pcm
    with open(out_path, "wb") as fh:
        fh.write(blob)
    print("wrote %s: %d cases, %d bytes" % (out_path, len(records), len(blob)))
    return 0


if __name__ == "__main__":
    sys.exit(main())