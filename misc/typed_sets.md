# Crucible — typed-set primitives

*Three faces of one algebraic object: a typed set over a finite tag universe, at three different points on the runtime/compile-time spectrum.*

This note documents the relationship between Crucible's three typed-set primitives so future contributors don't rediscover the pattern from scratch. The primitives compose orthogonally — none knows about the others — but they ARE algebraic duals.

## The triad

| Primitive | Layer | Storage | Algebra | Role |
|---|---|---|---|---|
| `safety::Bits<E>` | runtime value | `sizeof(underlying)` | bitwise (mask-encoded enums) | record flag set on each instance |
| `effects::Row<Es...>` | type-only | `0` (sometimes 1 for empty class) | consteval set ops | declare effect signature on a function |
| `safety::proto::PermSet<Tags...>` | type-only | `1` (EBO-collapsible) | consteval set ops | track CSL permission tag set as a session evolves |
| `effects::EffectMask` | runtime value | `sizeof(uint8_t)` | bitwise (position-encoded enums) | runtime dual of `Row<Es...>` for telemetry |

The first three were built independently. The fourth (EffectMask) is the bridge dual that emerged when we tried to project `Row<Es...>` to a runtime form for Augur drift detection — see §3.

Headers:
- `include/crucible/safety/Bits.h` — `Bits<EnumType>` newtype.
- `include/crucible/effects/EffectRow.h` — `Row<Es...>` template.
- `include/crucible/permissions/PermSet.h` — `PermSet<Tags...>` template.
- `include/crucible/effects/EffectRowProjection.h` — `EffectMask` + `bits_from_row<R>()` bridge.

## What "typed set" means here

All four primitives represent **a finite set drawn from a fixed universe**:
- `Bits<NodeFlags>`: subset of `{DEAD, VISITED, FUSED, REALIZED}` (the NodeFlags enumerators).
- `Row<Effect::Bg, Effect::Alloc>`: subset of `{Alloc, IO, Block, Bg, Init, Test}` (the Effect enumerators).
- `PermSet<Tag1, Tag2>`: subset of the universe of CSL permission tag types.
- `EffectMask`: same universe as `Row<Es...>` — the Effect enumerators.

They differ in **where the set lives** (runtime vs compile-time) and **what the elements are** (enum values vs types):

```
                  enum-value elements         type elements
                  --------------------         -------------
runtime           safety::Bits<E>              (no analog — types
                  effects::EffectMask           don't survive to runtime)

compile-time      effects::Row<Es...>          safety::proto::PermSet<Tags...>
```

`PermSet` has no runtime dual because its elements are types — types don't survive type erasure to runtime. `Bits` and `EffectMask` are the runtime forms; `Row` and `PermSet` are the type-level forms.

## Bridges

### Bits ↔ Row

**Implemented**: `effects::bits_from_row<R>() -> EffectMask` projects a type-level row to a runtime `EffectMask` value. See `effects/EffectRowProjection.h`.

**Why this matters**: Augur per-axis drift attribution (FOUND-K01..K05), Cipher row-keying for federation, and any cross-process row announcement need a runtime-recordable form of what the function signature declared as a type-level row. The bridge is the load-bearing piece.

**Why EffectMask, not `Bits<Effect>`**: `Effect` is a position-encoded enum (`Alloc=0, IO=1, ... Test=5`); `Bits<E>::set(e)` treats `e` as a pre-shifted mask and does `bits_ |= e` directly. Naive `Bits<Effect>::set(Effect::Bg)` would do `bits |= 3` (binary `0b11`), setting Alloc and IO instead of Bg. EffectMask does the position → mask shift internally (`1u << position`), maintaining the same mental model as Bits but purpose-built for position-encoded enums.

This encoding mismatch is a real design issue with `Bits<E>` worth noting (see §5).

### Bits ↔ PermSet — NOT BRIDGEABLE

PermSet's tags are types, not enum values. There's no way to project arbitrary types to a runtime integer. PermSet stays type-only.

### Row ↔ PermSet — NOT BRIDGEABLE

Different element kinds at the type-only level (Effect enum values vs CSL permission tag types). Conceivable in principle (a `Row` of permission-tag-types) but no production motivation today.

## Convergence — currently NOT done, deliberately

The three primitives use different operation names:

| Operation | Bits<E> | Row<Es...> | PermSet<Tags...> |
|---|---|---|---|
| size | `popcount()` | `row_size_v<R>` | `perm_set_size_v<S>` (planned) |
| contains | `test(e)` | `row_contains_v<R, E>` | `perm_set_contains_v<S, T>` |
| insert | `set(e)` | `row_union_t<R, Row<E>>` | `perm_set_insert_t<S, T>` |
| remove | `unset(e)` | `row_difference_t<R, Row<E>>` | `perm_set_remove_t<S, T>` |
| subset | (none) | `Subrow<R1, R2>` concept | `perm_set_subset_v<A, B>` |
| union | `\|` | `row_union_t<R1, R2>` | `perm_set_union_t<A, B>` |
| equality | `==` | `std::is_same_v<R1, R2>` | `perm_set_equal_v<A, B>` |

A unified vocabulary (`set_contains_v` / `set_insert_t` / `set_subset_v`) would let generic algorithms work over "any typed set" without per-primitive adapters. Cost: ~50 sites touched across Crucible. Benefit: when somebody writes such a generic algorithm, no friction.

**Decision**: defer convergence until either (a) somebody writes such a generic algorithm and feels the friction, or (b) the substrate reaches a point where the naming inconsistency is documented as a clear blocker. Documented as deliberate-not-yet-done so future contributors know it's a known asymmetry, not an oversight.

## The encoding problem with `Bits<E>`

`safety::Bits<E>` assumes **mask-encoded enums** — the enum values ARE the bit positions:

```cpp
enum class NodeFlags : std::uint8_t {
    DEAD     = 0x01,    // 1 << 0
    VISITED  = 0x02,    // 1 << 1
    FUSED    = 0x04,    // 1 << 2
    REALIZED = 0x08,    // 1 << 3
};
// Bits<NodeFlags>::set(NodeFlags::DEAD) does `bits_ |= 0x01` — correct.
```

But many enums in Crucible are **position-encoded** — values are bit POSITIONS, not pre-shifted masks:

```cpp
enum class Effect : std::uint8_t {
    Alloc = 0,
    IO    = 1,
    Block = 2,
    Bg    = 3,
    Init  = 4,
    Test  = 5,
};
// Bits<Effect>::set(Effect::Bg) would do `bits_ |= 3` (binary 0b11) —
// silently sets Alloc and IO instead of Bg.  WRONG.
```

When position-encoded enums need a Bits-shaped wrapper, define a dedicated newtype that shifts internally — see `effects::EffectMask` for the canonical example. Future cleanup option: extend `Bits<E, Encoding>` with a template parameter distinguishing the two encodings; not in v1 scope.

## The naming clash with `BitsBudget`

`algebra::lattices::BitsBudgetLattice` (FOUND-G63) is a memory-budget-in-bits axis (uint64_t bytes the value claims it has consumed). It's a graded lattice for the Budgeted wrapper, NOT a flag-set primitive.

The names look confusing in a wrapper-catalog scan, but they're namespace-isolated (`safety::Bits` vs `algebra::lattices::BitsBudgetLattice`). Different concepts, different namespaces. No runtime collision. Documented here for awareness.

## See also

- `safety/Bits.h` — `Bits<EnumType>` runtime value-level set.
- `effects/EffectRow.h` — `Row<Es...>` type-level set.
- `permissions/PermSet.h` — `PermSet<Tags...>` type-level set of tag types.
- `effects/EffectRowProjection.h` — `EffectMask` + `bits_from_row<R>()` bridge.
- `safety/IsBits.h`, `safety/IsBorrowed.h`, `safety/IsBorrowedRef.h` — concept-gate trait helpers.
- `algebra/lattices/BitsBudgetLattice.h` — UNRELATED memory-budget axis (named similarly).
- `misc/28_04_2026_effects.md` — Met(X) superpowers spec where the row/cache federation story lives.
