import java.io.PrintWriter;
import java.nio.file.*;

/**
 * Oracle for the frame-cadence matrix. Every expression below is transcribed from the reference
 * at revision 71ce130364ed00431d420891370d5314a4e3d4c4:
 *
 *   Scus94491BpeSegment.gameLoop:
 *     gameState_800babc8.timestamp_a0 += vsyncMode_8007a3b8;                  // when advancesTime()
 *     final int frames = Math.max(1, vsyncMode_8007a3b8);
 *     final int hz = 60 / frames * Config.getGameSpeedMultiplier();
 *     RENDERER.window().setFpsLimit(hz);
 *     PLATFORM.setInputTickRate(hz);
 *
 *   Menus.java:218:
 *     if (tickCount_800bb0fc % Math.max(1, 3 - vsyncMode_8007a3b8) == 0) { ... }
 *
 * The two consumers of vsyncMode_8007a3b8 are emitted separately on purpose: the rate clamps
 * the count at one, the clock increment does not clamp at all.
 *
 * Nothing here reads Rust code. Column order: vsync, speed, hz, clock_increment.
 */
public class GoldenTiming {
  public static void main(String[] args) throws Exception {
    final int[] vsyncModes = {-2, -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 12, 15, 20, 24, 30, 40, 60, 61, 70, 120};
    try (PrintWriter out = new PrintWriter(Paths.get(args[0]).toFile())) {
      out.println("# vsync speed hz clock_increment  (oracle: transcribed reference expressions)");
      for(int vsync: vsyncModes) {
        for(int speed = 1; speed <= 16; speed++) {
          final int frames = Math.max(1, vsync);
          final int hz = 60 / frames * speed;
          // timestamp_a0 += vsyncMode_8007a3b8, with no clamp and no speed term.
          out.printf("%d %d %d %d%n", vsync, speed, hz, vsync);
        }
      }
    }

    // Script gate: the reference tests a global frame counter, incremented once per frame loop
    // iteration after the tick, against Math.max(1, 3 - vsync). Emitted as the tick bitmap for
    // 48 frames at each divisor the reference can reach.
    try (PrintWriter out = new PrintWriter(Paths.get(args[1]).toFile())) {
      out.println("# divisor -> tick bitmap over 48 frames, frame 0 first (1 = logic runs)");
      for(int vsync: new int[] {1, 2, 3}) {
        final int divisor = Math.max(1, 3 - vsync);
        final StringBuilder bits = new StringBuilder();
        for(int frame = 0; frame < 48; frame++) {
          // tickCount_800bb0fc holds the number of frame-loop iterations already completed when
          // the menu code tests it.
          bits.append(frame % divisor == 0 ? '1' : '0');
        }
        out.printf("%d %s%n", divisor, bits);
      }
    }
  }
}
