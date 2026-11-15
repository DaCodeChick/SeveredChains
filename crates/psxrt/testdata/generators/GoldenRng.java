// Golden-vector generator for the three RNG families (planning/06 rows R3, R4, CL-GLP-02).
//
// ORACLE, not implementation. The integer-LCG streams reproduce the two reference LCGs;
// the 48-bit stream is taken from a real java.util.Random instance, so the fixture is
// independent of the Rust code under test.
//
//   simpleRand (LCG16):   seed = seed*9 + 0x3711;  out = (seed / 2) & 0xffff
//                         (signed 32-bit wrap; `/ 2` truncates toward zero, it is NOT >> 1;
//                          the low 12 bits of the state feed the host-clock reseed branch)
//   rand       (LCG15):   seed = seed*0x41c64e6d + 0x3039;  out = (seed >>> 16) & 0x7fff
//   Random48            : java.util.Random 48-bit LCG, multiplier 0x5DEECE66D, addend 0xB,
//                         setSeed = (seed ^ 0x5DEECE66D) & mask, bounded nextInt uses the
//                         power-of-two shortcut or the modulo-bias rejection loop.
import java.io.*;
import java.nio.file.*;
import java.util.Random;

public class GoldenRng {
  static final int N = 12_000;

  public static void main(String[] args) throws Exception {
    Path dir = Path.of(args.length > 0 ? args[0] : ".");
    Files.createDirectories(dir);

    // --- LCG16, two variants: pure chain, and a chain where the low-12-bit reseed branch fires.
    StringBuilder sb = new StringBuilder();
    sb.append("# seed_before output reseeded(0|1)\n");
    long seed = 3;
    long vsync = 0x1234;                       // injected stand-in for the host vsync counter
    int reseeds = 0;
    for (int i = 0; i < N; i++) {
      boolean reseeded = (seed & 0xfff) == 0;
      long base = reseeded ? ((vsync & 0xffffffffL) * 9) : seed;
      long s = (base * 9 + 0x3711);
      s &= 0xffffffffL;
      int signed = (int)s;                       // Java int semantics
      int out = (signed / 2) & 0xffff;           // truncating signed division, then mask
      if (reseeded) reseeds++;
      sb.append(Long.toHexString(seed & 0xffffffffL)).append(' ')
        .append(out).append(' ').append(reseeded ? 1 : 0).append('\n');
      seed = signed & 0xffffffffL;
    }
    Files.writeString(dir.resolve("rng_lcg16.txt"), sb.toString());

    // --- LCG15
    sb = new StringBuilder();
    sb.append("# seed_before output\n");
    long s15 = 0x24040001L;
    for (int i = 0; i < N; i++) {
      long next = (s15 * 0x41c64e6dL + 0x3039L) & 0xffffffffL;
      int out = (int)((next >>> 16) & 0x7fff);
      sb.append(Long.toHexString(s15)).append(' ').append(out).append('\n');
      s15 = next;
    }
    Files.writeString(dir.resolve("rng_lcg15.txt"), sb.toString());

    // --- Random48: captured from a real java.util.Random.
    // First block: nextInt(bound) over a cycle of bounds that exercises both the
    // power-of-two shortcut and the rejection loop.
    int[] bounds = {1, 2, 3, 5, 7, 10, 101, 321, 401, 1000, 2049, 4097, 3, 17, 257, 65536};
    sb = new StringBuilder();
    sb.append("# kind arg value   (kind: i=nextInt(bound), f=nextFloat(), F=nextFloat(bound), d=nextDouble())\n");
    Random r = new Random(0x1234_5678_9ABCL);
    for (int i = 0; i < N; i++) {
      int b = bounds[i % bounds.length];
      sb.append("i ").append(b).append(' ').append(r.nextInt(b)).append('\n');
    }
    for (int i = 0; i < N; i++) {
      long bits = Float.floatToRawIntBits(r.nextFloat());
      sb.append("f 0 ").append(Long.toHexString(bits & 0xffffffffL)).append('\n');
    }
    for (int i = 0; i < N; i++) {
      float bound = Float.intBitsToFloat(0x40c9_0000 | (i & 0x3f));   // ~6.28-ish spread
      long bits = Float.floatToRawIntBits(r.nextFloat(bound));
      sb.append("F ").append(Long.toHexString(Float.floatToRawIntBits(bound) & 0xffffffffL))
        .append(' ').append(Long.toHexString(bits & 0xffffffffL)).append('\n');
    }
    for (int i = 0; i < N; i++) {
      long bits = Double.doubleToRawLongBits(r.nextDouble());
      sb.append("d 0 ").append(Long.toHexString(bits)).append('\n');
    }
    Files.writeString(dir.resolve("rng_random48.txt"), sb.toString());
    System.out.println("lcg16 reseeds=" + reseeds + "; fixtures written");
  }
}
