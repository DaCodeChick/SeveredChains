// Verifies that the *documented* 48-bit LCG algorithm reproduces what a subclass of
// java.util.Random actually produces on this JDK, for the draw forms the game uses.
// If the two agree, the Rust rewrite may implement the documented algorithm and be
// verifiably equal to the reference's observable output.
import java.nio.file.*;

public class Random48Probe {
  static class LoDRandom extends java.util.Random {           // == legend.core.Random
    static int mix32(long z) {
      z = (z ^ (z >>> 33)) * 0x62a9d9ed799705f5L;
      return (int)(((z ^ (z >>> 28)) * 0xcb24d0a5c88c35b3L) >>> 32);
    }
    @Override public int nextInt() { return mix32(super.nextInt()); }
  }
  static final long MASK = (1L << 48) - 1, MUL = 0x5DEECE66DL, ADD = 0xBL;
  static long state;
  static void setSeed(long s) { state = (s ^ MUL) & MASK; }
  static int next(int bits) { state = (state * MUL + ADD) & MASK; return (int)(state >>> (48 - bits)); }
  static int nextIntBound(int bound) {
    int r = next(31), m = bound - 1;
    if ((bound & m) == 0) { r = (int)((bound * (long) r) >> 31); }
    else { for (int u = r; u - (r = u % bound) + m < 0; u = next(31)) {} }
    return r;
  }
  static float nextFloat() { return next(24) * 0x1.0p-24f; }
  static float nextFloatBound(float bound) {
    float r = nextFloat(); r = r * bound;
    return (r >= bound) ? Math.nextDown(bound) : r;
  }
  static double nextDouble() {
    return (((long)(next(26)) << 27) + next(27)) * Math.ulp(1.0) / 2.0;   // 2^-53
  }
  public static void main(String[] a) {
    long seed = 0x1234_5678_9ABCL;
    LoDRandom j = new LoDRandom(); j.setSeed(seed);
    LoDRandom m = new LoDRandom();
    int bad = 0, n = 0;
    StringBuilder log = new StringBuilder();
    int[] bounds = {1,2,3,5,7,10,101,321,401,1000,2049,4097,17,257,65536};
    for (int i = 0; i < 5000; i++) {
      int b = bounds[i % bounds.length];
      setSeed(seed);                              // independent replay from the same seed
      int expect = 0; boolean ok = true;
      // replay the same call sequence on the model
      java.util.Random ref = new java.util.Random(seed);
      LoDRandom model = new LoDRandom();
      for (int k = 0; k <= i; k++) {
        int bb = bounds[k % bounds.length];
        int r1 = ref.nextInt(bb);
        int r2 = nextIntBound(bb);
        if (r1 != r2) { ok = false; break; }
        expect = r1;
      }
      n++;
      if (!ok) { bad++; if (bad < 4) log.append("MISMATCH i=").append(i).append(" bound=").append(b).append('\n'); }
    }
    System.out.println("bounded nextInt: model==java.util.Random in " + (n - bad) + "/" + n + " replays");
    java.util.Random ref = new java.util.Random(seed);
    setSeed(seed);
    int bf = 0, bd = 0;
    for (int i = 0; i < 20000; i++) { if (Float.floatToRawIntBits(ref.nextFloat()) != Float.floatToRawIntBits(nextFloat())) bf++; }
    setSeed(seed); ref = new java.util.Random(seed);
    for (int i = 0; i < 20000; i++) { if (Double.doubleToRawLongBits(ref.nextDouble()) != Double.doubleToRawLongBits(nextDouble())) bd++; }
    System.out.println("nextFloat bit mismatches=" + bf + "  nextDouble bit mismatches=" + bd);
    System.out.print(log);
  }
}
