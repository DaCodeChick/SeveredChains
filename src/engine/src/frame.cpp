/// @file frame.cpp
/// @brief Frame-event merge rules (frame.h documents the semantics; this is the
/// single implementation shared by App's loop and any later layer that folds
/// several event sources together).
#include "lod/frame.h"

namespace lod
{

void MergeFrameEvents(FrameEvents& into, const FrameEvents& add)
{
  into.QuitRequested = into.QuitRequested || add.QuitRequested;

  if (add.Resized)
  {
    into.Resized = true;
    // Newest valid size wins; a 0x0 report (iconify) is recorded as a resize
    // but must not overwrite the last usable dimensions.
    if (add.Width > 0 && add.Height > 0)
    {
      into.Width = add.Width;
      into.Height = add.Height;
    }
  }

  if (add.FocusChanged)
  {
    into.FocusChanged = true;
    into.Focused = add.Focused;
  }
}

} // namespace lod
