/*
 * Exception-model macro probe (build-system.md §2, verification item 3).
 *
 * Fails to compile unless the toolchain reports C++ exception handling as
 * *disabled* for this translation unit. This is the signal MSVC offers: cl.exe
 * documents /EH as describing how to clean up after a throw rather than
 * whether one is allowed, and it keeps accepting `throw` even with /EHs-c-
 * (clang-cl, by contrast, maps that flag to -fno-exceptions and rejects it).
 * Asking cl.exe to reject a throw therefore fails a correctly working
 * compiler; asking whether its own exception-model macro went down tests the
 * thing that actually changes.
 *
 * Non-vacuity is checked by compiling this same source *without* the flags
 * (cmake/ScToolchain.cmake): there the macros are defined and the #error
 * fires. A compiler that never defined any of them would prove nothing, and
 * that case is reported rather than counted as a pass.
 */
#if defined(_CPPUNWIND) || defined(__EXCEPTIONS) || defined(__cpp_exceptions)
#error "exception handling is still enabled for this translation unit"
#endif

int main(void) { return 0; }