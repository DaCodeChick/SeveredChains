#pragma once
/// @file game.h
/// @brief Foundation game layer (namespace game): a lod::App subclass with
/// the game's subsystem wiring. M1 scope: lifecycle verification only —
/// gameplay arrives in M2+ (roadmap.md).

#include "lod/app.h"

#include <cstdint>

namespace game
{

/// @brief The game application: owns the engine App, counts sim ticks.
class GameApp final : public lod::App
{
public:
  using lod::App::App;

  /// @brief Number of sim ticks executed so far (timing contract, §3).
  std::uint64_t SimFrameCount() const { return simFrames_; }

  void Tick(std::uint64_t simFrame) override;

private:
  std::uint64_t simFrames_ = 0;
};

} // namespace game
