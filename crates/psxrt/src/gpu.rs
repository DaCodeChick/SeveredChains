//! GPU-side mechanisms: the things a draw command has to know about pixels and packing.
//!
//! This module holds *addressing*, not a device. Nothing here owns VRAM, submits a command, or
//! decides what a draw is worth — it answers "which bits of this word are the colour, and which
//! are the page and the translucency code", which is a question the rasteriser has to ask several
//! million times a second and therefore cannot answer by string-parsing a document.
//!
//! The sibling of this module, when it arrives, is the TPage/draw-flag decoder scheduled by
//! `planning/07` F5. It belongs here rather than at the crate root for the same reason
//! [`pixel`] does: a draw reads depth, translucency and page coordinates out of one 16-bit word,
//! and splitting that word across two modules means the two halves can disagree without either
//! being wrong on its own.

pub mod pixel;
