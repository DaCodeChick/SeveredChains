//! Container and codec readers.
//!
//! "Container" and "codec" are two different questions and this directory keeps them apart. A
//! container says *these bytes are several files, here is the table that says where*; a codec
//! says *these bytes are one file that has been made smaller*. A real payload can be either,
//! both, or neither, and the order in which the two questions get asked changes the answer — see
//! [`bpe::Codec`] and the note there about where the decision is made.
//!
//! Both readers here take borrowed bytes and never open a file. Where the bytes came from — a
//! disc image read in place, an unpacked tree, a cached extraction — is `space`, which does not
//! exist yet; `planning/08` §D-2 has the proposal that has to be approved first.

pub mod bpe;
pub mod mrg;

/// Read a little-endian 32-bit word, or nothing if the word is not fully present.
///
/// Every multi-byte field in these formats is little-endian, which is not a convention this
/// project chose: the platform is little-endian and the reference reads these words with a
/// `ByteBuffer` left in its default `LITTLE_ENDIAN` order. The reference does not bounds-check
/// these reads — it reads off the end of the array and lets the exception happen — so the
/// "nothing" case here is a *narrowing*, not a change of behaviour: both paths refuse the input,
/// only this one says which offset it gave up at.
pub(crate) fn read_u32_le(data: &[u8], at: usize) -> Option<u32> {
    let bytes: [u8; 4] = data.get(at..at + 4)?.try_into().ok()?;
    Some(u32::from_le_bytes(bytes))
}
