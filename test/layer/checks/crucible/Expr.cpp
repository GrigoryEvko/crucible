// The compile-time checks of crucible/Expr.h.

#include <crucible/Expr.h>

namespace crucible {

static_assert(sizeof(Expr) == 32, "Expr must be exactly 32 bytes");

// Ties the ceiling to the field it comes from. Widening `nargs` without
// raising kMaxArgs would leave every ExprPool scratch buffer short.
static_assert(Expr::kMaxArgs == static_cast<uint8_t>(~static_cast<uint8_t>(0)),
              "Expr::kMaxArgs must be the largest value Expr::nargs can hold");

static_assert(
    std::is_same_v<decltype(std::declval<Expr>().hash), const ::fixy::Tagged<std::uint64_t, hash_family::FamilyB>>,
    "Expr::hash must stay a const Tagged<uint64_t, hash_family::FamilyB>");
static_assert(sizeof(::fixy::Tagged<std::uint64_t, hash_family::FamilyB>) == sizeof(std::uint64_t),
              "Tagged<uint64_t, hash_family::FamilyB> must stay the width of its payload, or the Expr "
              "layout changes");

}  // namespace crucible
