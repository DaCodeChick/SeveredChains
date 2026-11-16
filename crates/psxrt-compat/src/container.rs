//! Writers for the container and codec layer.
//!
//! Three modules, all of them test oracles:
//!
//! * [`bpe`] produces BPE payloads that `psxrt_import::container::bpe::decode` accepts.
//! * [`mrg`] produces MRG container bytes that `psxrt_import::container::mrg::Table::parse`
//!   accepts.
//! * [`index`] produces `files/` cache index text that `psxrt_import::cache::Index::parse` accepts.
//!
//! They live here rather than beside the readers so that a round trip crosses a crate boundary
//! instead of being a function and its own mirror image inside one module. That is a weaker
//! guarantee than it looks — the same hand wrote both halves — so the value has to be stated
//! precisely: the encoder is written from the *format description*, not from the decoder's control
//! flow, and it is not permitted to import from `psxrt-import`. A disagreement between the two
//! therefore surfaces as a failing test rather than as a bug that reproduces itself. The encoder
//! also cannot import the decoder even by accident: a `dev-dependency` cycle between packages is
//! rejected by the build, so the direction of this test edge is enforced rather than merely
//! intended.
//!
//! The exception worth knowing about is [`index`], whose reader *does* have a writer sitting next to
//! it, inside `psxrt-import::cache`. That module exists to be a second opinion, not to be the
//! writer of record.

pub mod bpe;
pub mod index;
pub mod mrg;
