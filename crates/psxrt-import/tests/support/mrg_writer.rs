//! An MRG container writer.
//!
//! Takes `(offset, size)` pairs and writes the container bytes around them. There is deliberately
//! no option for the sector-aligned layout, because that is not a property of the container: it is
//! a property of the *file name* the container was read from, and the bytes on disk are the same
//! either way. A writer that took a flag here would be inviting a caller to think the choice is
//! recorded in the file. It is not — see `psxrt_import::container::mrg::OffsetScale`, which is
//! where the choice is made, and which this module does not import.
//!
//! This is a test oracle, not a packing tool. Nothing in this project writes MRG containers for
//! the game to read; the writer exists so the table parser can be checked against bytes built from
//! the format description rather than from the parser's own output.

/// The container's leading word. Bytes on disk are `4D 52 47 1A`.
///
/// Duplicated from `psxrt_import::container::mrg::MAGIC` rather than imported, because the writer
/// must not depend on the reader it is tested against. A round-trip test compares the two values,
/// so the duplication cannot quietly drift.
pub const MAGIC: u32 = 0x1a47_524d;

/// Write a container from raw table entries.
///
/// Offsets are written exactly as given. Under the sector-aligned layout they are sector *numbers*,
/// and a caller that means byte offsets must not divide them first — the reader multiplies, and it
/// is the file name that decides whether it does.
#[must_use]
pub fn write(entries: &[(u32, u32)]) -> Vec<u8> {
    let mut table = MAGIC.to_le_bytes().to_vec();
    table.extend_from_slice(&(entries.len() as u32).to_le_bytes());
    for &(offset, size) in entries {
        table.extend_from_slice(&offset.to_le_bytes());
        table.extend_from_slice(&size.to_le_bytes());
    }
    table
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The header is the magic word and the count, then eight bytes per entry.
    #[test]
    fn the_layout_is_header_then_pairs() {
        let table = write(&[(1, 2), (3, 4)]);
        assert_eq!(&table[..4], &[0x4d, 0x52, 0x47, 0x1a]);
        assert_eq!(&table[4..8], &[2, 0, 0, 0]);
        assert_eq!(table.len(), 8 + 16);
    }

    /// An empty table is still a container, and a reader has to tell that apart from damage.
    #[test]
    fn an_empty_table_is_still_a_container() {
        assert_eq!(write(&[]).len(), 8);
    }
}
