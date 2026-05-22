// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-220 HS14 fixture #2 of 2 for
// fixy::contract::MemberMintCtxRequired: UNREGISTERED-PAIR rejection —
// a (Class, MintName) pair that has no corresponding
// `member_mint_required_ctx<Class, MintName>` specialization must
// reject via the primary template's INCOMPLETE status (SFINAE
// rejection inside the concept's requires-expression).
//
// Violation: `member_mint_required_ctx<Class, MintName>` is declared
// as an INCOMPLETE primary template — only the 8 registered (Class,
// MintName) pairs ship explicit specializations.  Probing an
// unregistered pair via `::template admits<Ctx>()` triggers a
// SUBSTITUTION FAILURE (use of incomplete type as nested-name-
// specifier), which the concept's outer `requires { ... }` catches
// and reports as concept-unsatisfied.
//
// Distinct from fixture #1 (wrong-ctx rejection):
//   * Fixture #1 — REGISTERED pair, WRONG ctx.  Spec exists; admits
//     returns false; concept rejects.
//   * Fixture #2 — UNREGISTERED pair.  Primary template incomplete;
//     substitution fails; concept rejects via SFINAE.
// Two distinct rejection axes ⇒ HS14 floor satisfied.
//
// Background (Agent 8 Part 10 #13).  An UNREGISTERED pair is the
// "missing registration" failure mode — a maintainer adds a new
// member-function mint to a host class but forgets to add the
// corresponding `member_mint_required_ctx<Class, mint_name::new_tag>`
// specialization to fixy/Contract.h.  Production call sites that
// drop the static_assert with the new pair would red at compile time,
// surfacing the missing registration before the mint ships.
//
// This fixture uses `int` as the Class — `int` is provably not a
// registered host class (the 6 registered classes are Cipher,
// CKernelTable, SchemaTable, PoolAllocator, CrucibleContext,
// ReplayEngine).  Pairing `int` with any mint_name::* tag triggers
// the unregistered-pair axis cleanly.
//
// Expected diagnostic: static assertion failed mentioning
// MemberMintCtxRequired / `int` / open_view (the concept is
// unsatisfied because the primary template is incomplete for `int`).

#include <crucible/fixy/Contract.h>

int main() {
    // `int` is not one of the 6 registered host classes
    // (Cipher / CKernelTable / SchemaTable / PoolAllocator /
    // CrucibleContext / ReplayEngine).  Probing
    // `member_mint_required_ctx<int, mint_name::open_view>` returns
    // the INCOMPLETE primary template; the inner
    // `::template admits<Ctx>()` substitution is ill-formed; the
    // requires-expression SFINAE-rejects; the concept evaluates to
    // false; the static_assert fires.  If the primary template ever
    // grew a SFINAE-safe default body (e.g. `static consteval bool
    // admits() { return false; }` instead of being incomplete), this
    // fixture would silently compile.
    static_assert(::crucible::fixy::contract::MemberMintCtxRequired<
                      int,
                      ::crucible::fixy::contract::mint_name::open_view,
                      ::crucible::effects::BgDrainCtx>,
        "FIXY-V-220 fixture #2: int is not a registered host class — "
        "MemberMintCtxRequired must reject via incomplete-primary SFINAE.");
    return 0;
}
