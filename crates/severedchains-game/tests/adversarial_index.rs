//! Adversarial input for the cache index: arbitrary pointer graphs, checked against a walk that
//! does not share `Index::resolve`'s mechanism.
//!
//! This is the index half of what `psxrt-import/tests/adversarial_input.rs` does for containers and
//! codecs. It moved with the reader, because the thing under test is the cache convention rather
//! than a PSX format.
//!
//! Two properties, both about *terminating* rather than about correctness of the answer: a chain
//! that never ends is the failure mode this reader was written to avoid, and the original walked
//! these chains with an unbounded loop, so a generated index containing a cycle is the input that
//! would hang a faithful port. Termination is asserted by running the walk; the answer is compared
//! against [`walk`], which decides "cycle" with a visited set while
//! [`Index::resolve`](severedchains_game::tooling::cache::Index::resolve) decides it by running out
//! of iterations. A property test that reimplemented the same bound would prove nothing.
//!
//! It is a second net, not the contract. The named cases that must stay fixed are in
//! `tests/index_round_trip.rs`.

use proptest::collection::vec;
use proptest::prelude::*;
use severedchains_game::tooling::cache::Index;

/// Walk a pointer chain with a visited set.
///
/// Deliberately a *different mechanism* from `Index::resolve`, which decides "cycle" by running
/// out of iterations. The two agree only if that bound is exactly right, so comparing them tests
/// the bound itself: too small and real chains get reported as cycles, too large and a cyclic
/// index hangs instead of answering. A test that reimplemented the same bound would prove nothing.
///
/// `Err(())` means the chain revisited a slot, which is a cycle and must be *reported* by the
/// reader rather than answered with no data.
fn walk(target_of: &[Option<usize>], start: usize) -> Result<Option<usize>, ()> {
    let Some(mut current) = target_of[start] else {
        return Ok(None);
    };
    if current == start {
        return Ok(None);
    }

    let mut seen = vec![false; target_of.len()];
    loop {
        if seen[current] {
            return Err(());
        }
        seen[current] = true;

        match target_of[current] {
            // The chain ran out into a slot with no line: no owner, and not damage either, since
            // gaps are legal in this format.
            None => return Ok(None),
            // A slot naming itself owns its own bytes, so it is the answer.
            Some(next) if next == current => return Ok(Some(current)),
            Some(next) => current = next,
        }
    }
}

proptest! {
    #![proptest_config(ProptestConfig::with_cases(4096))]

    #[test]
    fn arbitrary_index_text_terminates_and_agrees(
        lines in vec((any::<usize>(), any::<usize>(), any::<usize>(), any::<bool>()), 0..12),
    ) {
        const SLOTS: usize = 40;

        let mut text = String::new();
        let mut pairs: Vec<(usize, Option<usize>)> = Vec::new();
        for (slot, target, size, blank) in &lines {
            // `;` is the separator, not `:`. `FileMap.addFile` emits `name=target;size\n`, so a
            // generator using `:` produces genuinely malformed lines and the reader was right to
            // refuse them — which is exactly what it did when this file first used a colon.
            text.push_str(&format!(
                "{}={};{}\n",
                slot % SLOTS,
                if *blank { String::new() } else { (target % SLOTS).to_string() },
                size % 300,
            ));
            pairs.push((slot % SLOTS, if *blank { None } else { Some(target % SLOTS) }));
        }

        let index = Index::parse(&text)
            .map_err(|error| TestCaseError::fail(format!("{text}\nrefused: {error}")))?;

        // A later line naming the same slot replaces the earlier one, so the graph is assembled
        // from the text rather than assumed from the generated list.
        let mut target_of = vec![None; SLOTS];
        for (slot, target) in &pairs {
            target_of[*slot] = *target;
        }

        for slot in 0..SLOTS {
            let expected = walk(&target_of, slot);
            let got = index.resolve(slot);

            match expected {
                Err(()) => {
                    // A cycle has to be *reported*. Returning `None` would also terminate, but it
                    // would terminate by quietly producing no data for a file that has bytes, and
                    // that is the silent-wrong-answer failure this file exists to catch.
                    prop_assert!(
                        got.is_err(),
                        "slot {slot} sits in a cycle but resolve() returned {got:?}\n{text}"
                    );
                }
                Ok(want) => {
                    let got = got.map_err(|error| {
                        TestCaseError::fail(format!(
                            "slot {slot} is acyclic but resolve() failed: {error}\n{text}"
                        ))
                    })?;
                    prop_assert_eq!(
                        got, want,
                        "slot {} resolved differently than an independent walk\n{}",
                        slot, text
                    );
                    if let Some(owner) = want {
                        prop_assert!(owner < SLOTS, "owner {owner} is outside the index");
                        prop_assert_ne!(owner, slot, "a slot may not be its own owner");
                    }
                }
            }
        }
    }
}
