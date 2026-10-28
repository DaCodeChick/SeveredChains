#include "game/game.h"

namespace game
{

void GameApp::Tick(std::uint64_t simFrame)
{
  lod::App::Tick(simFrame); // engine diagnostics hook
  simFrames_ = simFrame + 1;
  // M2+: gameLoop order lands here (engine-design.md §2). Foundation only
  // verifies that the loop drives ticks deterministically.
}

} // namespace game
