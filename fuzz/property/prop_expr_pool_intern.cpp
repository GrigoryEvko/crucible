// ═══════════════════════════════════════════════════════════════════
// prop_expr_pool_intern — ExprPool Swiss-table intern invariant.
//
// ExprPool's load-bearing contract:
//   pool.make(op, args)  =>  SAME pointer for structurally-equal inputs,
//                            DIFFERENT pointer for anything that differs.
//
// The claim spans four concrete sub-properties the pool must never
// violate, no matter how many rehashes / grows happen en route:
//
//   (P1) Integer atom identity.   integer(N) == integer(N) always.
//                                 N1 != N2  =>  different pointer.
//                                 The int cache (−128..127) and the
//                                 generic intern path must agree.
//
//   (P2) Symbol atom identity.    symbol(S) == symbol(S) always.
//                                 S1 != S2  =>  different pointer.
//
//   (P3) Composite identity.      Same (op, canonical-arg-tuple) =>
//                                 same pointer across independent calls.
//                                 Children that are already-interned
//                                 atoms guarantee this downstream.
//
//   (P4) Structural inequality.   Any (op, args) that differ AFTER the
//                                 canonicalization the constructor
//                                 performs produce DIFFERENT pointers.
//
//   (P5) Arena monotonicity.      arena_bytes() never decreases across
//                                 a sequence of intern calls — the
//                                 backing Arena is bump-pointer.
//
// ─── Swiss-table bug classes this is the trip-wire for ─────────────
//
//   - SIMD control-byte group probe off-by-one (match() vs match_empty())
//   - Rehash losing an entry, or duplicating one under a new tag
//   - wymix / expr_hash weakness that collides semantically-distinct
//     inputs in the H2-tag + hash + args-compare funnel
//   - composite_flags drift causing two "same" composites to carry
//     distinct flag words, producing distinct hash + distinct slot
//   - int cache / generic intern-path divergence (two paths for
//     cached vs uncached integers must both uphold identity)
//   - Arena reuse after a grow that silently reassigns the same bytes
//     to a new entry, clobbering a live interned Expr
//
// ─── Why restrict composites to ADD/MUL of pure symbols? ───────────
//
// The pool aggressively canonicalizes ADD/MUL: flatten, fold integer
// constants, sort by pointer, combine coefficients.  Feeding mixed
// (int, composite, symbol) args would produce outputs whose
// "canonical equality" is non-trivial to re-derive in this harness.
// Pure-symbol binary composites hit the fast path at ExprPool.h:419
// (ADD) and :452 (MUL), which simply sorts by pointer and interns —
// that's the minimal surface the Swiss-table invariants must uphold.
// Canonicalization correctness is test_expr_pool's job; this fuzzer
// is specifically a stress test on the intern Swiss table.
//
// ─── Why the check body lives in a free function, not the lambda ───
//
// GCC 16's contracts implementation (P2900R14) rejects `pre()` clauses
// that reference non-static data members when the enclosing function
// is instantiated inside a lambda body (observed with Arena::
// total_allocated's `pre(offset_ <= end_offset_)`).  Moving the
// invocation into a named function at namespace scope sidesteps that
// parser path entirely.  The named function is called from a thin
// lambda passed to run() — no behavioral change, just parse-site relief.
// ═══════════════════════════════════════════════════════════════════

#include "property_runner.h"

#include <crucible/Effects.h>
#include <crucible/Expr.h>
#include <crucible/ExprPool.h>
#include <crucible/Ops.h>
#include <crucible/SymbolTable.h>
#include <crucible/Types.h>

#include <algorithm>
#include <array>
#include <cstdint>

namespace {

// Per-iteration input: a sequence of "build these expressions" actions.
// Capped at N=24 expressions to keep the O(N²) iff check cheap.  The
// pool starts at the smallest capacity the ctor allows (kGroupWidth =
// 16/32 depending on SIMD), so 24 entries + the seeded singletons
// (~258 = 1 true + 1 false + 256 int cache) straddles at least one
// rehash on the intern path for composites.
constexpr unsigned kNumExprs = 24;

// Restrict composite ops to commutative binary ones with the simplest
// canonicalization: ADD/MUL fast-path with two non-const symbol args.
// (The fast path just sorts by pointer and interns.)
enum class CompositeOp : uint8_t { Add, Mul };

// Atom kinds the generator can emit.
enum class Kind : uint8_t { Int, Sym, Composite };

struct ExprSpec {
    Kind        kind    = Kind::Int;
    int64_t     int_val = 0;                 // Kind::Int
    uint32_t    sym_id  = 0;                 // Kind::Sym — SymbolId raw value
    CompositeOp cop     = CompositeOp::Add;  // Kind::Composite
    uint8_t     lhs_ix  = 0;                 // index into the expr plan, < self
    uint8_t     rhs_ix  = 0;                 // index into the expr plan, < self
};

struct Plan {
    std::array<ExprSpec, kNumExprs> specs{};
    uint8_t                         nsyms = 0;  // symbols to register
};

// Canonical semantic key: what an interned Expr "is" after interning.
// Two specs yield the same pointer iff their canonical keys compare
// equal.  Composites store children as already-interned pointers so
// recursive structural equality collapses to pointer equality.
struct Resolved {
    // Discriminator: 0=int, 1=sym, 2=composite
    uint8_t                       tag  = 0;
    int64_t                       ival = 0;                 // tag==0
    uint32_t                      sid  = 0;                 // tag==1
    CompositeOp                   cop  = CompositeOp::Add;  // tag==2
    const crucible::Expr*         lhs  = nullptr;           // tag==2 (post-intern)
    const crucible::Expr*         rhs  = nullptr;           // tag==2 (post-intern)
};

// Generate one plan.  Integer values span both the int-cache range
// (−128..127) and the generic-intern path (|N| up to ~2^30) so both
// code paths in ExprPool::integer() get exercised.  SymbolIds are
// drawn from a small set to maximize re-intern opportunities.
[[nodiscard]] Plan gen_plan(crucible::fuzz::prop::Rng& rng) noexcept {
    Plan p{};
    p.nsyms = static_cast<uint8_t>(rng.next_below(6) + 2);  // [2, 7]

    for (uint8_t i = 0; i < kNumExprs; ++i) {
        ExprSpec& s = p.specs[i];
        // Expr 0..1 MUST be atoms (no predecessors for composites).
        const bool allow_composite = (i >= 2);
        const uint32_t roll = rng.next_below(allow_composite ? 3 : 2);
        s.kind = static_cast<Kind>(roll);

        switch (s.kind) {
            case Kind::Int: {
                // 50% small (−128..127, cache-hit), 50% large.
                if (rng.next_below(2) == 0) {
                    s.int_val = static_cast<int64_t>(
                        static_cast<int32_t>(rng.next_below(256)) - 128);
                } else {
                    // Bit-cast random 32 bits to widen the signed range
                    // across both positive and negative halves.
                    s.int_val = static_cast<int64_t>(
                        static_cast<int32_t>(rng.next32()));
                }
                break;
            }
            case Kind::Sym:
                s.sym_id = rng.next_below(p.nsyms);
                break;
            case Kind::Composite: {
                s.cop = static_cast<CompositeOp>(rng.next_below(2));
                // Children MUST resolve to SYMBOLS (not integers, not
                // earlier composites).  ExprPool's canonicalization
                // collapses many cases the harness's structural key
                // can't track:
                //   add(x, x)         -> 2*x   (changes ADD to MUL)
                //   add(0, x)         -> x     (drops the ADD)
                //   add(2*x, -2*x)    -> 0     (drops to INT_0)
                //   mul(0, x)         -> 0     (drops to INT_0)
                //   mul(1, x)         -> x     (drops the MUL)
                //   add_n(2*x, x)     -> 3*x   (coefficient combining)
                // Restricting children to plain symbols sidesteps every
                // one of these paths — the resulting binary ADD/MUL
                // hits the fast path at ExprPool.h:419/452 and produces
                // a clean canonical (op, sorted-pair) form whose
                // structural identity the harness CAN reproduce.
                int8_t lhs = -1, rhs = -1;
                for (uint8_t k = 0; k < i; ++k) {
                    if (p.specs[k].kind == Kind::Sym) {
                        if (lhs < 0) lhs = static_cast<int8_t>(k);
                        else         rhs = static_cast<int8_t>(k);
                    }
                }
                if (lhs < 0) {
                    // No prior symbol exists — demote to Sym (always safe).
                    s.kind   = Kind::Sym;
                    s.sym_id = rng.next_below(p.nsyms);
                } else {
                    if (rhs < 0) rhs = lhs;  // single-sym: lhs == rhs is OK,
                    // but pool.add(x, x) -> 2*x changes the op, so demote
                    // composites with identical children to a plain Sym.
                    if (rhs == lhs) {
                        s.kind   = Kind::Sym;
                        s.sym_id = rng.next_below(p.nsyms);
                    } else {
                        s.lhs_ix = static_cast<uint8_t>(lhs);
                        s.rhs_ix = static_cast<uint8_t>(rhs);
                    }
                }
                break;
            }
            default:
                std::unreachable();
        }
    }
    return p;
}

[[nodiscard]] bool sem_eq(const Resolved& a, const Resolved& b) noexcept {
    if (a.tag != b.tag) return false;
    switch (a.tag) {
        case 0: return a.ival == b.ival;
        case 1: return a.sid == b.sid;
        case 2: {
            if (a.cop != b.cop) return false;
            // Commutative canonicalization: compare as unordered pair
            // of child pointers.  ExprPool's fast path (lhs/rhs ptr
            // sort) implements exactly this equivalence.
            const crucible::Expr* al = a.lhs;
            const crucible::Expr* ar = a.rhs;
            if (al > ar) std::swap(al, ar);
            const crucible::Expr* bl = b.lhs;
            const crucible::Expr* br = b.rhs;
            if (bl > br) std::swap(bl, br);
            return al == bl && ar == br;
        }
        default:
            std::unreachable();
    }
}

// Core invariant check.  Pulled out of the runner lambda to keep GCC
// 16's contract parser off the lambda-instantiation path (see header
// comment for the full rationale).
[[nodiscard]] bool check_plan(const Plan& p) noexcept {
    using namespace crucible;
    fx::Test test{};
    // Smallest legal capacity forces the pool to rehash while interning
    // even a tiny user set (the ~258 preseeded int singletons already
    // push the table past its 87.5% load factor).  Rehash-during-insert
    // is the stressful path.
    ExprPool pool{test.alloc, /*initial_capacity=*/1};
    SymbolTable syms{};

    // Register symbols so the assumption flags the pool stamps onto
    // SYMBOL Exprs are consistent across re-interning the same
    // SymbolId.  Without a stable flag word the expr_hash would differ
    // between calls and the same logical symbol would intern twice.
    std::array<SymbolId, 8> sym_ids{};
    char name_buf[2] = {'a', 0};
    for (uint8_t i = 0; i < p.nsyms; ++i) {
        sym_ids[i] = syms.add(
            SymKind::SIZE, ExprFlags::IS_INTEGER, /*is_backed=*/true);
    }

    std::array<const Expr*, kNumExprs> ptrs{};
    std::array<Resolved,    kNumExprs> keys{};

    // P5 (arena monotonicity) is dropped from this harness:
    // pool.arena_bytes() routes through Arena::total_allocated which
    // carries pre(offset_ <= end_offset_).  Under GCC 16 (P2900R14)
    // that contract is rejected with "contract condition is not
    // constant" when the call site is reached through this fuzzer's
    // template-instantiation chain (lambda → free function → pool
    // wrapper → arena query).  Moving the call into a free function
    // — as the harness header documents — does not avoid the parser
    // path for total_allocated specifically.  The arena monotonicity
    // invariant is already covered by prop_arena_alloc_invariants,
    // which calls Arena::total_allocated directly and compiles cleanly.
    // This fuzzer keeps its full intern-identity (P1–P4) coverage.

    for (uint8_t i = 0; i < kNumExprs; ++i) {
        const ExprSpec& s = p.specs[i];
        const Expr* e = nullptr;
        Resolved    k{};
        switch (s.kind) {
            case Kind::Int:
                e = pool.integer(test.alloc, s.int_val);
                k.tag  = 0;
                k.ival = s.int_val;
                break;
            case Kind::Sym: {
                name_buf[0] = static_cast<char>('a' + s.sym_id);
                const SymbolId sid = sym_ids[s.sym_id];
                e = pool.symbol(
                    test.alloc, name_buf, sid, syms.expr_flags(sid));
                k.tag = 1;
                k.sid = s.sym_id;
                break;
            }
            case Kind::Composite: {
                const Expr* lhs = ptrs[s.lhs_ix];
                const Expr* rhs = ptrs[s.rhs_ix];
                // add()/mul() dispatch their own canonical form.  If a
                // child is an INTEGER/ADD/MUL the call falls into the
                // slow path (add_n/mul_n); the iff invariant holds
                // there too because both paths produce the unique
                // canonical form for the given inputs.
                e = (s.cop == CompositeOp::Add)
                    ? pool.add(test.alloc, lhs, rhs)
                    : pool.mul(test.alloc, lhs, rhs);
                k.tag = 2;
                k.cop = s.cop;
                k.lhs = lhs;
                k.rhs = rhs;
                break;
            }
            default:
                std::unreachable();
        }

        // P0: never null.
        if (e == nullptr) return false;

        ptrs[i] = e;
        keys[i] = k;
    }

    // Integer canonicalization collapses coefficient combining (a + a
    // → 2a, etc.), so composites whose CHILDREN are both ints with
    // identity triggers can return the same pointer the harness's
    // "sem_eq compares child pointers" rule doesn't classify as equal.
    // Only run the iff check on composites whose children are both
    // symbols or both already-composite (their canonical form tracks
    // child-pointer identity exactly via the fast path / mul_n sort).
    // Atoms are always safe.
    auto safe_for_iff = [&](uint8_t i) noexcept {
        const auto& k = keys[i];
        if (k.tag != 2) return true;
        // Reject composites whose children are integer atoms — those
        // pass through constant-folding / zero-annihilation /
        // identity-elimination that the harness's structural key
        // cannot pre-predict without duplicating all canonicalizers.
        auto child_kind_safe = [&](const Expr* c) noexcept {
            return c->op != Op::INTEGER && c->op != Op::FLOAT;
        };
        return child_kind_safe(k.lhs) && child_kind_safe(k.rhs);
    };

    // Pairwise iff invariant: (ptrs[i] == ptrs[j]) iff
    // (semantic_equal(keys[i], keys[j])).  Quadratic but N=24 keeps
    // the check to ~576 comparisons per iteration.
    for (uint8_t i = 0; i < kNumExprs; ++i) {
        if (!safe_for_iff(i)) continue;
        for (uint8_t j = 0; j < kNumExprs; ++j) {
            if (!safe_for_iff(j)) continue;
            const bool eq_ptrs = (ptrs[i] == ptrs[j]);
            const bool eq_sem  = sem_eq(keys[i], keys[j]);
            if (eq_ptrs != eq_sem) return false;
        }
    }

    // Re-intern atoms a second time in reverse order — the canonical
    // form must be stable across insertion order, and pointer identity
    // with the first round's result must match exactly for atoms
    // (composites are skipped because the symbolic input depends on
    // ptrs[] from round 1, which is tautological at this point).
    for (int8_t i = static_cast<int8_t>(kNumExprs - 1); i >= 0; --i) {
        const uint8_t ui = static_cast<uint8_t>(i);
        const ExprSpec& s = p.specs[ui];
        const Expr* e = nullptr;
        switch (s.kind) {
            case Kind::Int:
                e = pool.integer(test.alloc, s.int_val);
                break;
            case Kind::Sym: {
                name_buf[0] = static_cast<char>('a' + s.sym_id);
                const SymbolId sid = sym_ids[s.sym_id];
                e = pool.symbol(
                    test.alloc, name_buf, sid, syms.expr_flags(sid));
                break;
            }
            case Kind::Composite:
                continue;  // see comment above
            default:
                std::unreachable();
        }
        if (e != ptrs[ui]) return false;
    }

    return true;
}

}  // namespace

int main(int argc, char** argv) {
    using namespace crucible::fuzz::prop;
    Config cfg = parse_args(argc, argv);
    // Inner check is O(N²) over kNumExprs; cap iterations accordingly.
    if (cfg.iterations > 5'000) cfg.iterations = 5'000;

    return run("ExprPool intern iff structural equal", cfg,
        [](Rng& rng) { return gen_plan(rng); },
        [](const Plan& p) { return check_plan(p); });
}
