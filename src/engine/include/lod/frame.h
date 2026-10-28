#pragma once
/// @file frame.h
/// @brief Per-frame platform event snapshot (engine-design.md §2:
/// `Platform::pollEvents()` drains the OS queue once per frame). Declared in
/// its own header because it is part of the Subsystem lifecycle contract
/// (`Subsystem::PollFrame`) and must not drag the platform layer into it.
/// This header is SDL-free by design: subsystems translate OS event types
/// into this value type at their own interop boundary (cpp.md §9.1).

namespace lod
{

/// @brief Per-frame platform events drained from the OS queue.
///
/// A *frame summary*, not a queue: the platform layer collapses every OS
/// event drained during one poll into this snapshot — `resized`/`focusChanged`
/// report "at least one such event happened this frame" and `width`/`height`
/// carry the **latest** size seen, which is the only size a renderer can act
/// on. Intermediate sizes of a drag-resize are deliberately discarded (a
/// swapchain is recreated once per frame, not once per OS event).
///
/// @note Thread: produced on the main thread by the frame loop; never crosses
///       a thread boundary.
struct FrameEvents
{
  bool QuitRequested = false; ///< Window close / app quit.
  bool Resized = false;       ///< Window resized this frame.
  int Width = 0;              ///< New width (when resized; 0 = unknown).
  int Height = 0;             ///< New height (when resized; 0 = unknown).
  bool FocusChanged = false;  ///< Focus gained/lost this frame.
  bool Focused = true;        ///< Focus state (when focusChanged).
};

/// @brief Fold one subsystem's frame summary into the merged frame summary.
///
/// Merge rules (documented because they define what the loop guarantees):
/// `quitRequested` and `resized`/`focusChanged` are logical ORs (sticky);
/// `width`/`height` and `focused` take the newest **valid** contribution
/// (a subsystem reporting `resized` with a non-positive size — e.g. an
/// iconify — is recorded as a resize but does not corrupt the last known
/// size).
///
/// @param into Accumulator (merged result so far); mutated.
/// @param add  Contribution from one subsystem.
/// @note Thread: main thread only (frame loop).
void MergeFrameEvents(FrameEvents& into, const FrameEvents& add);

} // namespace lod
