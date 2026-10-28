// build-system.md §2 probe: asserts the no-RTTI flags are actually applied.
// Compiled by try_compile under -fno-rtti //GR-; if the flags were silently
// dropped, the #error below fires and configuration fails.
#if defined(__GXX_RTTI)
#  error "RTTI probe: __GXX_RTTI defined — no-RTTI flags not in effect"
#endif
#if defined(_MSC_VER) && defined(_CPPRTTI)
#  error "RTTI probe: _CPPRTTI defined — no-RTTI flags not in effect"
#endif
struct Poly
{
  virtual ~Poly() = default;
};
int main()
{
  Poly p;
  (void)p;
  return 0;
}
