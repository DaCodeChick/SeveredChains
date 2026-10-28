/*
 * Exception-policy probe TU (build-system.md §2, verification items 1-2).
 *
 * Must fail to compile under the "no exceptions" flags and must compile
 * without them; the second half is what makes the first half meaningful
 * (ScToolchain probe 5). `-fno-exceptions` rejects this on GCC and Clang, and
 * clang-cl rejects it too because it maps /EHs-c- onto -fno-exceptions.
 * cl.exe accepts it — see eh_macro_probe.cpp for the check that covers MSVC.
 *
 * `return 0` exists only so the throw is not the whole function body; neither
 * statement is ever executed, this file is never linked or run.
 */
int main(void) { throw 1; return 0; }
