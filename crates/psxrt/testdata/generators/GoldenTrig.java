// Golden-vector generator for the PSX fixed-point trig primitives (planning/06 rows R1, R2).
//
// ORACLE, not implementation: this program reproduces the *reference* chain so that the
// committed fixture cannot be derived from the Rust code under test.
//
//   legend.core.MathHelper.psxDegToRad(int)  ->  psxDeg * PSX_DEG_TO_RAD          (float)
//   legend.core.MathHelper.sin(float)        ->  org.joml.Math.sin(float)          (float)
//   legend.game.Scus94491BpeSegment.rsin      ->  (short)(MathHelper.sin(rad) * 0x1000)
//
// PSX_DEG_TO_RAD is the precomputed float constant
//     (360.0f / 4096.0f) * (float)(Math.PI / 180.0f)      == 0x3ac90fdb
//
// The reference narrows the transcendental to float *before* the 0x1000 multiply, because
// MathHelper.sin returns float; that narrowing is reproduced here. Over this grid,
// java.lang.Math and java.lang.StrictMath give identical i16 results (verified: zero
// divergences), so the fixture does not depend on which one org.joml.Math delegates to.
// That is why the unresolved jOML question is recorded as immaterial rather than guessed.
//
// Run:  java --source 21 GoldenTrig.java <outdir>
import java.nio.file.*;

public class GoldenTrig {
  static final float PSX_DEG_TO_RAD = (360.0f / 4096.0f) * (float)(Math.PI / 180.0f);

  static short to16(float v) { return (short)v; }   // Java narrowing conversion, truncating

  public static void main(String[] args) throws Exception {
    Path dir = Path.of(args.length > 0 ? args[0] : ".");
    Files.createDirectories(dir);
    StringBuilder grid = new StringBuilder("# theta rsin rcos\n");
    StringBuilder strict = new StringBuilder("# theta where Math and StrictMath differ (none expected)\n");
    int divergences = 0;
    for (int theta = -8192; theta <= 8192; theta++) {
      float rad = theta * PSX_DEG_TO_RAD;
      int rs = to16((float)Math.sin(rad) * 0x1000);
      int rc = to16((float)Math.cos(rad) * 0x1000);
      if (rs != to16((float)StrictMath.sin(rad) * 0x1000)
          || rc != to16((float)StrictMath.cos(rad) * 0x1000)) {
        divergences++;
        strict.append(theta).append('\n');
      }
      grid.append(theta).append(' ').append(rs).append(' ').append(rc).append('\n');
    }
    Files.writeString(dir.resolve("trig_grid.txt"), grid.toString());
    Files.writeString(dir.resolve("trig_strictmath_divergences.txt"), strict.toString());
    System.out.println("rows=16385 Math-vs-StrictMath divergences=" + divergences);
  }
}
