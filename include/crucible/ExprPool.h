#pragma once

#include <crucible/Arena.h>
#include <crucible/Expr.h>
#include <crucible/Ops.h>
#include <crucible/Platform.h>
#include <crucible/SwissTable.h>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <ranges>
#include <span>
#include <utility>
#include <vector>

namespace crucible {

namespace detail {

// Hash all structural fields of an expression. Args are compared by pointer
// (since children are themselves interned), so we hash their addresses.
//
// ─── Family-B (process-local) per Types.h taxonomy ─────────────────
// The returned value embeds `reinterpret_cast<uintptr_t>(args[i])` for
// compound nodes — arena pointers are ASLR-randomized per process, so
// the SAME structural input produces DIFFERENT bits in different
// processes.  This is INTENTIONAL: the Swiss-table intern path only
// needs process-local uniqueness at zero-cost probing, and within a
// single process, interning guarantees structural equality → pointer
// equality on children.
//
// MUST NOT be persisted, fed into any Cipher key, or mixed into any
// Family-A hash (content_hash / merkle_hash / Guard::hash / etc).
// If FORGE ever needs a cross-process stable Expr identity for L1
// federation (FORGE.md §18.6), add a separate `structural_content_hash`
// that walks children via their own structural hashes, not pointers.
//
// Uses wyhash-style 128-bit multiply mixing: each wymix() is a single
// mulq + xor on x86-64 (~3 cycles). Total cost for a binary node:
// 2 wymix calls = ~6 cycles ≈ 2ns. Previous fmix64 chain was ~15ns.
//
// ─── Why NOT reflect_hash on Expr? (REFL-4) ────────────────────────
//
// Two reasons this stays manual:
//
// (1) API shape: this hash is computed on the intern hot path BEFORE
//     an Expr exists.  Inputs are loose parameters (op, payload,
//     symbol_id, flags, args, nargs) — there's no Expr struct yet
//     to reflect on.  Building one to reflect would require an arena
//     allocation per probe attempt; the whole point of the intern
//     path is to avoid that allocation when the lookup hits.
//
// (2) Performance: the manual path is ~6 cycles for the common
//     binary-arg case (one wymix per arg).  reflect_hash builds a
//     multiplicative wymix-like chain over members + a final fmix64
//     — even on a synthesized struct that would be ~3-4× slower.
//     ExprPool::intern is the single hottest function in the bg
//     trace path; the perf budget here is unforgiving.
//
// If a separate cross-process structural hash is ever needed (FORGE.md
// §18.6 federation), it would be a SECOND function operating on an
// already-interned `const Expr&` and could cleanly use reflect_hash
// without the API or perf constraints listed above.
inline uint64_t expr_hash(
    Op op,
    int64_t payload,
    SymbolId symbol_id,
    uint16_t flags,
    const Expr* const* args,
    uint8_t nargs) {
  // Pack small fields (op, nargs, flags, symbol_id) into one 64-bit word.
  // This avoids separate mix operations for each tiny field.
  uint64_t packed = static_cast<uint64_t>(std::to_underlying(op))
                  | (static_cast<uint64_t>(nargs) << 8)
                  | (static_cast<uint64_t>(flags) << 16)
                  | (static_cast<uint64_t>(symbol_id.raw()) << 32);

  // Mix packed metadata with payload — one 128-bit multiply
  uint64_t h = wymix(packed ^ 0x9E3779B97F4A7C15ULL,
                      static_cast<uint64_t>(payload) ^ 0x517CC1B727220A95ULL);

  // Mix in child pointers. Common cases (0, 1, 2 args) are unrolled
  // to avoid loop overhead. Each child is already interned → unique
  // pointer → good entropy without additional mixing per-pointer.
  switch (nargs) {
    case 0:
      break;
    case 1:
      h = wymix(h, reinterpret_cast<uintptr_t>(args[0]));
      break;
    case 2:
      h = wymix(h ^ reinterpret_cast<uintptr_t>(args[0]),
                reinterpret_cast<uintptr_t>(args[1]));
      break;
    default:
      for (uint8_t i = 0; i < nargs; ++i)
        h = wymix(h, reinterpret_cast<uintptr_t>(args[i]));
      break;
  }
  return h;
}

constexpr uint16_t integer_flags(int64_t val) {
  uint16_t f = ExprFlags::IS_INTEGER | ExprFlags::IS_REAL |
               ExprFlags::IS_FINITE | ExprFlags::IS_NUMBER;
  if (val > 0)
    f |= ExprFlags::IS_POSITIVE | ExprFlags::IS_NONNEGATIVE;
  else if (val < 0)
    f |= ExprFlags::IS_NEGATIVE | ExprFlags::IS_NONPOSITIVE;
  else
    f |= ExprFlags::IS_ZERO | ExprFlags::IS_NONNEGATIVE |
         ExprFlags::IS_NONPOSITIVE;
  f |= (val % 2 == 0) ? ExprFlags::IS_EVEN : ExprFlags::IS_ODD;
  return f;
}

// Derive flags for a composite node from its op and children.
constexpr uint16_t composite_flags(
    Op op,
    const Expr* const* args,
    uint8_t nargs) {
  switch (op) {
    // Variadic: intersect numeric type flags of all children
    case Op::ADD:
    case Op::MUL:
    case Op::MIN:
    case Op::MAX: {
      uint16_t f = 0xFFFF;
      for (uint8_t i = 0; i < nargs; ++i)
        f &= args[i]->flags;
      return f &
          (ExprFlags::IS_INTEGER | ExprFlags::IS_REAL | ExprFlags::IS_FINITE |
           ExprFlags::IS_NUMBER);
    }

    case Op::POW:
      return (args[0]->flags & args[1]->flags) &
          (ExprFlags::IS_REAL | ExprFlags::IS_FINITE);

    // Always boolean
    case Op::EQ:
    case Op::NE:
    case Op::LT:
    case Op::LE:
    case Op::GT:
    case Op::GE:
    case Op::AND:
    case Op::OR:
    case Op::NOT:
      return ExprFlags::IS_BOOLEAN;

    // Always integer
    case Op::FLOOR_DIV:
    case Op::CLEAN_DIV:
    case Op::CEIL_DIV:
    case Op::MOD:
    case Op::PYTHON_MOD:
    case Op::MODULAR_INDEXING:
    case Op::LSHIFT:
    case Op::RSHIFT:
    case Op::CEIL_TO_INT:
    case Op::FLOOR_TO_INT:
    case Op::TRUNC_TO_INT:
    case Op::ROUND_TO_INT:
    case Op::BITWISE_AND:
    case Op::BITWISE_OR:
    case Op::POW_BY_NATURAL:
    case Op::IS_NON_OVERLAPPING_AND_DENSE:
      return ExprFlags::IS_INTEGER | ExprFlags::IS_REAL |
             ExprFlags::IS_FINITE | ExprFlags::IS_NUMBER;

    // Always real (float result)
    case Op::FLOAT_TRUE_DIV:
    case Op::INT_TRUE_DIV:
    case Op::TO_FLOAT:
    case Op::TRUNC_TO_FLOAT:
    case Op::FLOAT_POW:
    case Op::ROUND_DECIMAL:
    case Op::SQRT:
    case Op::COS:
    case Op::SIN:
    case Op::TAN:
    case Op::COSH:
    case Op::SINH:
    case Op::TANH:
    case Op::ASIN:
    case Op::ACOS:
    case Op::ATAN:
    case Op::EXP:
    case Op::LOG:
    case Op::ASINH:
    case Op::LOG2:
      return ExprFlags::IS_REAL | ExprFlags::IS_FINITE | ExprFlags::IS_NUMBER;

    case Op::WHERE:
      if (nargs >= 3)
        return (args[1]->flags & args[2]->flags) &
            (ExprFlags::IS_INTEGER | ExprFlags::IS_REAL |
             ExprFlags::IS_FINITE | ExprFlags::IS_NUMBER);
      return 0;

    case Op::IDENTITY:
      return (nargs >= 1) ? args[0]->flags : 0;

    case Op::NEG:
      if (nargs >= 1)
        return args[0]->flags &
            (ExprFlags::IS_INTEGER | ExprFlags::IS_REAL |
             ExprFlags::IS_FINITE | ExprFlags::IS_NUMBER);
      return 0;

    // ABS: propagates input's numeric kind AND guarantees non-negative.
    // Before this case existed, ABS hit default:return 0 and silently
    // lost its integer/real classification, breaking downstream
    // simplifiers that branch on is_integer/is_real.
    case Op::ABS:
      if (nargs >= 1)
        return (args[0]->flags &
                (ExprFlags::IS_INTEGER | ExprFlags::IS_REAL |
                 ExprFlags::IS_FINITE | ExprFlags::IS_NUMBER))
             | ExprFlags::IS_NONNEGATIVE;
      return 0;

    // BITWISE_XOR: integer like AND/OR.  Its absence from the old
    // switch meant BITWISE_XOR expressions lost IS_INTEGER / IS_REAL /
    // IS_FINITE / IS_NUMBER — a correctness hole in every simplifier
    // that consulted the flags.
    case Op::BITWISE_XOR:
      return ExprFlags::IS_INTEGER | ExprFlags::IS_REAL |
             ExprFlags::IS_FINITE | ExprFlags::IS_NUMBER;

    // ── Atoms reach composite_flags only under caller bug ──
    //
    // integer(), float_(), symbol(), bool_true(), bool_false() populate
    // flags directly via intern_node; composite_flags is invoked only
    // on composite (nargs>0) construction paths.  Receiving an atom op
    // here means a caller bypassed the dedicated constructor and fed
    // raw args through make()/intern_node with atom-kind — a bug.
    case Op::INTEGER:
    case Op::FLOAT:
    case Op::SYMBOL:
    case Op::BOOL_TRUE:
    case Op::BOOL_FALSE:
      std::unreachable();

    // ── NUM_OPS is the enum sentinel ──
    case Op::NUM_OPS:
      std::unreachable();

    // Required by -Werror=switch-default.  Every enumerator is handled
    // above.  Reaching this arm implies the op value was read from
    // out-of-range memory (cast from a corrupted uint8_t).  A new Op
    // added without a case here still fires -Werror=switch first.
    default:
      std::unreachable();
  }
}

} // namespace detail

// Arena-based expression factory with Swiss-table interning.
//
// All Expr nodes are allocated from an internal arena and deduplicated
// via a Swiss hash table (separate control-byte array + slot array).
// Same structure → same pointer, so equality is pointer comparison (~1ns).
// Thread-local: one per thread.
//
// The Swiss table uses SIMD to compare kGroupWidth control bytes (H2 tags)
// in parallel, eliminating the branch-per-slot pattern of linear probing.
// At 87.5% load with 32-byte groups (AVX2), ~98.5% of lookups resolve
// in the first group with ~0.22 expected structural comparisons.
//
// Construction methods (add, mul, etc.) perform eager canonicalization:
//   - Constant folding: add(3, 5) → integer(8)
//   - Identity elimination: add(x, 0) → x, mul(x, 1) → x
//   - Flattening: add(add(a, b), c) → add(a, b, c)
//   - Constant collection: add(a, 3, b, 5) → add(a, b, 8)
//   - Canonical ordering: add(b, a) → add(a, b) by pointer
//   - Term combining: add(a, 2a) → 3a (via coefficient decomposition)
class CRUCIBLE_OWNER ExprPool {
 public:
  // Default `initial_capacity` sized for real production graphs — ViT
  // forward+backward+optimizer is ~15k DAG ops, SD1.5 is ~30k. Each op
  // contributes 1-3 non-cached Exprs (shape polynomials, symbolic dims,
  // composites; concrete ints are served by the separate 256-entry
  // integer cache). 16384 slots holds 14k user entries at 87.5% load
  // → covers ViT-scale graphs with zero rehashes.
  //
  // Capacity math:
  //   16384 slots × 7/8 threshold  = 14336 entries max
  //   258 seeded by ctor           = 14078 user Exprs budget
  //
  // Memory cost: 144 KB (16384 ctrl + 16384*8 slots). This is the
  // first capacity above glibc's default 128 KB mmap threshold, so the
  // single backing_ allocation goes through mmap/munmap directly —
  // clean return to the OS on dtor (no heap-pool growth).
  //
  // Ctor cost (measured AVX2): ~30 µs for the two memsets + mmap + the
  // 258 initial inserts. Negligible for long-lived pools; bench harness
  // wall-cap keeps short-lived test scopes bounded.
  //
  // Callers with known bounded size call `reserve(n)` explicitly:
  //   - Production KernelCache: reserve(expected_kernel_count)
  //   - Large graphs (>14k exprs): reserve(approximate_final_size)
  //
  // History: original default was 1<<16 = 65536 (576 KB up-front, ~25
  // cold-page faults per ctor, 7M faults across bench_graph's 65k-iter
  // loop). First reduction went to 512 (optimal for tiny benches but
  // forced 5+ rehashes on real graphs). 16384 is the measured sweet
  // spot — covers the user-declared minimum baseline (10k+ node real
  // networks) while staying at a clean mmap-backed allocation size.
  static constexpr size_t kDefaultInitialCapacity = 16384;

  explicit ExprPool(fx::Alloc a,
                    size_t initial_capacity = kDefaultInitialCapacity) : arena_() {
    // Round up to power of 2, minimum one full SIMD group
    capacity_ = detail::kGroupWidth;
    while (capacity_ < initial_capacity)
      capacity_ <<= 1;

    alloc_tables_(capacity_);
    intern_count_ = 0;

    // Boolean singletons
    true_ = intern_node(a, Op::BOOL_TRUE, nullptr, 0, ExprFlags::IS_BOOLEAN, SymbolId{}, 0);
    false_ =
        intern_node(a, Op::BOOL_FALSE, nullptr, 0, ExprFlags::IS_BOOLEAN, SymbolId{}, 0);

    // Integer cache: -128..127 for O(1) access to common constants
    for (int64_t i = kIntCacheLow; i <= kIntCacheHigh; ++i)
      int_cache_[static_cast<size_t>(i - kIntCacheLow)] = make_integer(a, i);
  }

  ~ExprPool() {
    std::free(backing_);
  }

  ExprPool(const ExprPool&) = delete("ExprPool owns arena + Swiss table with interior pointers");
  ExprPool& operator=(const ExprPool&) = delete("ExprPool owns arena + Swiss table with interior pointers");
  ExprPool(ExprPool&&) = delete("interned Expr* pointers would dangle after arena move");
  ExprPool& operator=(ExprPool&&) = delete("interned Expr* pointers would dangle after arena move");

  // Pre-grow the Swiss table to hold at least `n_entries` without
  // triggering rehash during subsequent intern_node calls. No-op if the
  // table already has the capacity. Safe to call multiple times.
  //
  // A production KernelCache that will register ~10k sub-computations
  // calls `pool.reserve(10'000)` right after construction and skips the
  // ~5 doublings (256→512→1024→2048→4096→8192→16384) that would
  // otherwise land on its insertion path.
  void reserve(size_t n_entries) {
    // Need capacity such that n_entries * 8 <= capacity * 7 (87.5% LF).
    // Solve: capacity >= ceil(n_entries * 8 / 7).
    const size_t needed = (n_entries * 8 + 6) / 7;
    size_t target = detail::kGroupWidth;
    while (target < needed) target <<= 1;
    if (target > capacity_) grow_to_(target);
  }

  // ---- Atom construction ----

  [[nodiscard]] const Expr* integer(fx::Alloc a, int64_t val) {
    if (val >= kIntCacheLow && val <= kIntCacheHigh)
      return int_cache_[static_cast<size_t>(val - kIntCacheLow)];
    return make_integer(a, val);
  }

  [[nodiscard]] const Expr* float_(fx::Alloc a, double val) {
    int64_t payload = std::bit_cast<int64_t>(val);
    uint16_t f =
        ExprFlags::IS_REAL | ExprFlags::IS_FINITE | ExprFlags::IS_NUMBER;
    if (val > 0)
      f |= ExprFlags::IS_POSITIVE | ExprFlags::IS_NONNEGATIVE;
    else if (val < 0)
      f |= ExprFlags::IS_NEGATIVE | ExprFlags::IS_NONPOSITIVE;
    else if ((static_cast<uint64_t>(payload) << 1) == 0) {
      // ±0 but not NaN — shift-out-sign catches both signed zeros.
      f |= ExprFlags::IS_ZERO | ExprFlags::IS_NONNEGATIVE |
           ExprFlags::IS_NONPOSITIVE;
    }
    return intern_node(a, Op::FLOAT, nullptr, 0, f, SymbolId{}, payload);
  }

  [[nodiscard]] const Expr* symbol(fx::Alloc a, const char* name, SymbolId id, uint16_t assumption_flags) {
    if (id.raw() >= symbol_names_.size())
      symbol_names_.resize(id.raw() + 1, nullptr);
    if (symbol_names_[id.raw()] == nullptr) {
      size_t len = std::strlen(name) + 1;
      char* buf = static_cast<char*>(arena_.alloc(a,
          crucible::safety::Positive<size_t>{len},
          crucible::safety::PowerOfTwo<size_t>{1}));
      std::memcpy(buf, name, len);
      symbol_names_[id.raw()] = buf;
    }
    int64_t payload = std::bit_cast<int64_t>(symbol_names_[id.raw()]);
    return intern_node(
        a, Op::SYMBOL, nullptr, 0,
        assumption_flags | ExprFlags::IS_SYMBOL, id, payload);
  }

  [[nodiscard]] const Expr* bool_true() const {
    return true_;
  }
  [[nodiscard]] const Expr* bool_false() const {
    return false_;
  }

  // ---- Arithmetic ----

  [[nodiscard]] const Expr* add(fx::Alloc a, const Expr* lhs, const Expr* rhs) {
    // Fast path: two children that don't need canonicalization.
    // Excluded ops: ADD (needs flattening), MUL (needs coefficient
    // extraction for term combining), INTEGER/FLOAT (needs folding).
    // Symbols, POW, FLOOR_DIV, etc. go straight to intern.
    if (lhs->op != Op::ADD && rhs->op != Op::ADD &&
        lhs->op != Op::MUL && rhs->op != Op::MUL &&
        lhs->op != Op::INTEGER && rhs->op != Op::INTEGER &&
        lhs->op != Op::FLOAT && rhs->op != Op::FLOAT) [[likely]] {
      // Same base detection: a + a → 2a
      if (lhs == rhs) [[unlikely]]
        return mul(a, integer(a, 2), lhs);
      // Canonical ordering by pointer address
      if (lhs > rhs)
        std::swap(lhs, rhs);
      const Expr* args[] = {lhs, rhs};
      uint16_t f = detail::composite_flags(Op::ADD, args, 2);
      return intern_node(a, Op::ADD, args, 2, f, SymbolId{}, 0);
    }
    // Constant folding
    if (lhs->op == Op::INTEGER && rhs->op == Op::INTEGER)
      return integer(a, lhs->payload + rhs->payload);
    if (lhs->op == Op::FLOAT && rhs->op == Op::FLOAT)
      return float_(a, lhs->as_float() + rhs->as_float());
    // Identity
    if (lhs->is_zero_int())
      return rhs;
    if (rhs->is_zero_int())
      return lhs;
    // Slow path: flatten + fold + sort + coefficient combining
    const Expr* inputs[] = {lhs, rhs};
    return add_n(a, inputs);
  }

  [[nodiscard]] const Expr* mul(fx::Alloc a, const Expr* lhs, const Expr* rhs) {
    // Fast path: two non-constant, non-MUL children.
    // Skip the full mul_n() canonicalization (flatten, fold, sort).
    // Most symbolic expressions (x * y, a * b) hit this directly.
    if (lhs->op != Op::MUL && rhs->op != Op::MUL &&
        lhs->op != Op::INTEGER && rhs->op != Op::INTEGER &&
        lhs->op != Op::FLOAT && rhs->op != Op::FLOAT) [[likely]] {
      // Canonical ordering by pointer address
      if (lhs > rhs)
        std::swap(lhs, rhs);
      const Expr* args[] = {lhs, rhs};
      uint16_t f = detail::composite_flags(Op::MUL, args, 2);
      return intern_node(a, Op::MUL, args, 2, f, SymbolId{}, 0);
    }
    // Constant folding
    if (lhs->op == Op::INTEGER && rhs->op == Op::INTEGER)
      return integer(a, lhs->payload * rhs->payload);
    if (lhs->op == Op::FLOAT && rhs->op == Op::FLOAT)
      return float_(a, lhs->as_float() * rhs->as_float());
    // Zero annihilation
    if (lhs->is_zero_int() || rhs->is_zero_int())
      return integer(a, 0);
    // Identity
    if (lhs->is_one())
      return rhs;
    if (rhs->is_one())
      return lhs;
    // Slow path: flatten + fold + sort
    const Expr* inputs[] = {lhs, rhs};
    return mul_n(a, inputs);
  }

  [[nodiscard]] const Expr* pow(fx::Alloc a, const Expr* base, const Expr* exp) {
    // x^0 → 1
    if (exp->is_zero_int())
      return integer(a, 1);
    // x^1 → x
    if (exp->is_one())
      return base;
    // Concrete integer power (small exponents only to avoid overflow)
    if (base->op == Op::INTEGER && exp->op == Op::INTEGER &&
        exp->payload >= 0 && exp->payload <= 62) {
      int64_t result = 1, b = base->payload, e = exp->payload;
      for (int64_t i = 0; i < e; ++i)
        result *= b;
      return integer(a, result);
    }
    const Expr* args[] = {base, exp};
    uint16_t f = detail::composite_flags(Op::POW, args, 2);
    return intern_node(a, Op::POW, args, 2, f, SymbolId{}, 0);
  }

  // Canonical form: MUL(-1, x). No NEG nodes in output.
  [[nodiscard]] const Expr* neg(fx::Alloc a, const Expr* e) {
    if (e->op == Op::INTEGER)
      return integer(a, -e->payload);
    if (e->op == Op::FLOAT)
      return float_(a, -e->as_float());
    return mul(a, integer(a, -1), e);
  }

  // ---- Relational ----

  [[nodiscard]] const Expr* eq(fx::Alloc a, const Expr* lhs, const Expr* rhs) {
    if (lhs == rhs)
      return true_;
    if (lhs->op == Op::INTEGER && rhs->op == Op::INTEGER)
      return (lhs->payload == rhs->payload) ? true_ : false_;
    // Eq is commutative: canonical order by pointer
    if (lhs > rhs)
      std::swap(lhs, rhs);
    const Expr* args[] = {lhs, rhs};
    return intern_node(a, Op::EQ, args, 2, ExprFlags::IS_BOOLEAN, SymbolId{}, 0);
  }

  [[nodiscard]] const Expr* ne(fx::Alloc a, const Expr* lhs, const Expr* rhs) {
    if (lhs == rhs)
      return false_;
    if (lhs->op == Op::INTEGER && rhs->op == Op::INTEGER)
      return (lhs->payload != rhs->payload) ? true_ : false_;
    if (lhs > rhs)
      std::swap(lhs, rhs);
    const Expr* args[] = {lhs, rhs};
    return intern_node(a, Op::NE, args, 2, ExprFlags::IS_BOOLEAN, SymbolId{}, 0);
  }

  [[nodiscard]] const Expr* lt(fx::Alloc a, const Expr* lhs, const Expr* rhs) {
    if (lhs == rhs)
      return false_;
    if (lhs->op == Op::INTEGER && rhs->op == Op::INTEGER)
      return (lhs->payload < rhs->payload) ? true_ : false_;
    const Expr* args[] = {lhs, rhs};
    return intern_node(a, Op::LT, args, 2, ExprFlags::IS_BOOLEAN, SymbolId{}, 0);
  }

  [[nodiscard]] const Expr* le(fx::Alloc a, const Expr* lhs, const Expr* rhs) {
    if (lhs == rhs)
      return true_;
    if (lhs->op == Op::INTEGER && rhs->op == Op::INTEGER)
      return (lhs->payload <= rhs->payload) ? true_ : false_;
    const Expr* args[] = {lhs, rhs};
    return intern_node(a, Op::LE, args, 2, ExprFlags::IS_BOOLEAN, SymbolId{}, 0);
  }

  [[nodiscard]] const Expr* gt(fx::Alloc a, const Expr* lhs, const Expr* rhs) {
    if (lhs == rhs)
      return false_;
    if (lhs->op == Op::INTEGER && rhs->op == Op::INTEGER)
      return (lhs->payload > rhs->payload) ? true_ : false_;
    const Expr* args[] = {lhs, rhs};
    return intern_node(a, Op::GT, args, 2, ExprFlags::IS_BOOLEAN, SymbolId{}, 0);
  }

  [[nodiscard]] const Expr* ge(fx::Alloc a, const Expr* lhs, const Expr* rhs) {
    if (lhs == rhs)
      return true_;
    if (lhs->op == Op::INTEGER && rhs->op == Op::INTEGER)
      return (lhs->payload >= rhs->payload) ? true_ : false_;
    const Expr* args[] = {lhs, rhs};
    return intern_node(a, Op::GE, args, 2, ExprFlags::IS_BOOLEAN, SymbolId{}, 0);
  }

  // ---- Logic ----

  [[nodiscard]] const Expr* and_(fx::Alloc a, const Expr* lhs, const Expr* rhs) {
    if (lhs == false_ || rhs == false_)
      return false_;
    if (lhs == true_)
      return rhs;
    if (rhs == true_)
      return lhs;
    if (lhs == rhs)
      return lhs;
    const Expr* inputs[] = {lhs, rhs};
    return and_n(a, inputs);
  }

  [[nodiscard]] const Expr* or_(fx::Alloc a, const Expr* lhs, const Expr* rhs) {
    if (lhs == true_ || rhs == true_)
      return true_;
    if (lhs == false_)
      return rhs;
    if (rhs == false_)
      return lhs;
    if (lhs == rhs)
      return lhs;
    const Expr* inputs[] = {lhs, rhs};
    return or_n(a, inputs);
  }

  [[nodiscard]] const Expr* not_(fx::Alloc a, const Expr* e) {
    if (e == true_)
      return false_;
    if (e == false_)
      return true_;
    // Double negation elimination
    if (e->op == Op::NOT)
      return e->args[0];
    const Expr* args[] = {e};
    return intern_node(a, Op::NOT, args, 1, ExprFlags::IS_BOOLEAN, SymbolId{}, 0);
  }

  // ---- Division / Modular ----

  [[nodiscard]] const Expr* floor_div(fx::Alloc a, const Expr* lhs, const Expr* rhs) {
    if (lhs->op == Op::INTEGER && rhs->op == Op::INTEGER && rhs->as_int() != 0) {
      int64_t av = lhs->as_int(), bv = rhs->as_int();
      int64_t q = av / bv, r = av % bv;
      if (r != 0 && ((r ^ bv) < 0)) --q;
      return integer(a, q);
    }
    if (lhs->is_zero_int()) return integer(a, 0);
    if (rhs->is_one()) return lhs;
    if (rhs->is_neg_one()) return neg(a, lhs);
    if (lhs == rhs) return integer(a, 1);
    // FloorDiv(FloorDiv(x, c1), c2) → FloorDiv(x, c1*c2)
    if (lhs->op == Op::FLOOR_DIV || lhs->op == Op::CLEAN_DIV)
      return floor_div(a, lhs->arg(0), mul(a, lhs->arg(1), rhs));
    // Extract divisible terms from ADD when divisor is constant
    if (lhs->op == Op::ADD && rhs->op == Op::INTEGER && rhs->as_int() != 0) {
      int64_t d = rhs->as_int();
      const Expr* quotients[256];
      const Expr* remainders[256];
      uint8_t nq = 0, nr = 0;
      for (uint8_t i = 0; i < lhs->nargs; ++i) {
        int64_t c = integer_coefficient_(lhs->arg(i));
        if (c != 0 && c % d == 0)
          quotients[nq++] = divide_coefficients_(a, lhs->arg(i), d);
        else
          remainders[nr++] = lhs->arg(i);
      }
      if (nq > 0) {
        const Expr* qsum = (nq == 1) ? quotients[0]
            : add_n(a, std::span{quotients, nq});
        if (nr == 0) return qsum;
        const Expr* rsum = (nr == 1) ? remainders[0]
            : add_n(a, std::span{remainders, nr});
        return add(a, qsum, floor_div(a, rsum, rhs));
      }
    }
    // Integer GCD cancellation
    {
      int64_t g = gcd_(integer_factor_(lhs), integer_factor_(rhs));
      if (g > 1)
        return floor_div(a,
            divide_coefficients_(a, lhs, g), divide_coefficients_(a, rhs, g));
    }
    const Expr* args[] = {lhs, rhs};
    uint16_t f = ExprFlags::IS_INTEGER;
    if (lhs->is_nonnegative() && rhs->is_positive())
      f |= ExprFlags::IS_NONNEGATIVE;
    return intern_node(a, Op::FLOOR_DIV, args, 2, f, SymbolId{}, 0);
  }

  [[nodiscard]] const Expr* clean_div(fx::Alloc a, const Expr* lhs, const Expr* rhs) {
    return floor_div(a, lhs, rhs);
  }

  [[nodiscard]] const Expr* ceil_div(fx::Alloc a, const Expr* lhs, const Expr* rhs) {
    if (lhs->op == Op::INTEGER && rhs->op == Op::INTEGER && rhs->as_int() != 0) {
      int64_t av = lhs->as_int(), bv = rhs->as_int();
      int64_t q = av / bv, r = av % bv;
      if (r != 0 && ((r ^ bv) > 0)) ++q;
      return integer(a, q);
    }
    // ceil(a/b) = floor((a + b - 1) / b) for positive b
    return floor_div(a, add(a, lhs, add(a, rhs, integer(a, -1))), rhs);
  }

  [[nodiscard]] const Expr* mod(fx::Alloc a, const Expr* lhs, const Expr* rhs) {
    if (lhs->op == Op::INTEGER && rhs->op == Op::INTEGER && rhs->as_int() > 0)
      return integer(a, lhs->as_int() % rhs->as_int());
    if (lhs->is_zero_int() || lhs == rhs || rhs->is_one()) return integer(a, 0);
    if (rhs->op == Op::INTEGER && rhs->as_int() == 2) {
      if (lhs->is_even()) return integer(a, 0);
      if (lhs->is_odd()) return integer(a, 1);
    }
    const Expr* args[] = {lhs, rhs};
    uint16_t f = ExprFlags::IS_INTEGER | ExprFlags::IS_NONNEGATIVE;
    return intern_node(a, Op::MOD, args, 2, f, SymbolId{}, 0);
  }

  [[nodiscard]] const Expr* python_mod(fx::Alloc a, const Expr* lhs, const Expr* rhs) {
    if (lhs->op == Op::INTEGER && rhs->op == Op::INTEGER && rhs->as_int() != 0) {
      int64_t av = lhs->as_int(), bv = rhs->as_int();
      int64_t r = av % bv;
      if (r != 0 && ((r ^ bv) < 0)) r += bv;
      return integer(a, r);
    }
    if (lhs->is_zero_int() || lhs == rhs || rhs->is_one()) return integer(a, 0);
    if (rhs->op == Op::INTEGER && rhs->as_int() == 2) {
      if (lhs->is_even()) return integer(a, 0);
      if (lhs->is_odd()) return integer(a, 1);
    }
    const Expr* args[] = {lhs, rhs};
    uint16_t f = ExprFlags::IS_INTEGER;
    return intern_node(a, Op::PYTHON_MOD, args, 2, f, SymbolId{}, 0);
  }

  [[nodiscard]] const Expr* modular_indexing(
      fx::Alloc a,
      const Expr* base,
      const Expr* div,
      const Expr* modulus) {
    if (base->is_zero_int() || modulus->is_one()) return integer(a, 0);
    // All concrete
    if (base->op == Op::INTEGER && div->op == Op::INTEGER &&
        modulus->op == Op::INTEGER && div->as_int() != 0 &&
        modulus->as_int() != 0) {
      int64_t bv = base->as_int(), dv = div->as_int(), mv = modulus->as_int();
      int64_t q = bv / dv, r = bv % dv;
      if (r != 0 && ((r ^ dv) < 0)) --q;
      int64_t m = q % mv;
      if (m < 0) m += mv;
      return integer(a, m);
    }
    // GCD on (base, divisor)
    if (!(div->op == Op::INTEGER && div->as_int() == 1)) {
      int64_t g = gcd_(integer_factor_(base), integer_factor_(div));
      if (g > 1)
        return modular_indexing(a,
            divide_coefficients_(a, base, g),
            divide_coefficients_(a, div, g), modulus);
    }
    // Drop ADD terms divisible by modulus*divisor
    if (base->op == Op::ADD && modulus->op == Op::INTEGER &&
        div->op == Op::INTEGER) {
      int64_t md = modulus->as_int() * div->as_int();
      if (md > 0) {
        const Expr* kept[256];
        uint8_t nk = 0;
        bool dropped = false;
        for (uint8_t i = 0; i < base->nargs; ++i) {
          int64_t c = integer_coefficient_(base->arg(i));
          if (c != 0 && c % md == 0)
            dropped = true;
          else
            kept[nk++] = base->arg(i);
        }
        if (dropped) {
          if (nk == 0) return integer(a, 0);
          const Expr* nb =
              (nk == 1) ? kept[0] : add_n(a, std::span{kept, nk});
          return modular_indexing(a, nb, div, modulus);
        }
      }
    }
    // FloorDiv as base: ModIdx(x//a, d, m) → ModIdx(x, a*d, m)
    if (base->op == Op::FLOOR_DIV || base->op == Op::CLEAN_DIV)
      return modular_indexing(a, base->arg(0), mul(a, base->arg(1), div), modulus);

    const Expr* args[] = {base, div, modulus};
    uint16_t f = ExprFlags::IS_INTEGER | ExprFlags::IS_NONNEGATIVE;
    return intern_node(a, Op::MODULAR_INDEXING, args, 3, f, SymbolId{}, 0);
  }

  // ---- Conditional ----

  [[nodiscard]] const Expr* where(fx::Alloc a, const Expr* cond, const Expr* t, const Expr* f) {
    if (cond == true_) return t;
    if (cond == false_) return f;
    if (t == f) return t;
    const Expr* args[] = {cond, t, f};
    uint16_t fl = t->flags & f->flags;
    return intern_node(a, Op::WHERE, args, 3, fl, SymbolId{}, 0);
  }

  // ---- Min / Max ----

  [[nodiscard]] const Expr* min_expr(fx::Alloc a, const Expr* lhs, const Expr* rhs) {
    if (lhs == rhs) return lhs;
    if (lhs->op == Op::INTEGER && rhs->op == Op::INTEGER)
      return integer(a, std::min(lhs->as_int(), rhs->as_int()));
    const Expr* inputs[] = {lhs, rhs};
    return min_n(a, inputs);
  }

  [[nodiscard]] const Expr* max_expr(fx::Alloc a, const Expr* lhs, const Expr* rhs) {
    if (lhs == rhs) return lhs;
    if (lhs->op == Op::INTEGER && rhs->op == Op::INTEGER)
      return integer(a, std::max(lhs->as_int(), rhs->as_int()));
    const Expr* inputs[] = {lhs, rhs};
    return max_n(a, inputs);
  }

  // ---- Generic construction ----
  // Dispatches to canonical constructors for ops that have them,
  // generic interning for everything else.
  //
  // Variadic ops (ADD/MUL/AND/OR/MIN/MAX) take an n-ary args span.  When
  // size() == 2, dispatching to the binary helper (add/mul/and_/or_/
  // min_expr/max_expr) bypasses the *_n slow path's flatten + sort +
  // coefficient-combining work that the binary fast paths skip via
  // their early-return identity checks (e.g. add(x,0) -> x without
  // touching the intern table).  The binary helpers are inline and
  // hot — the compiler inlines them into make() naturally without
  // gnu::flatten (attempted initially, reverted: flatten also inlined
  // the bulky add_n/mul_n/and_n/... slow-path bodies into make(),
  // bloating the function to ~900 B of stack frame + icache pressure
  // and REGRESSING the hit-path benchmark from 138 ns to 690 ns).
  // Relying on default inlining keeps make() lean.
  [[nodiscard]] const Expr* make(
      fx::Alloc a, Op op, std::span<const Expr* const> args) {
    switch (op) {
      case Op::ADD:
        if (args.size() == 2) [[likely]] return add(a, args[0], args[1]);
        return add_n(a, args);
      case Op::MUL:
        if (args.size() == 2) [[likely]] return mul(a, args[0], args[1]);
        return mul_n(a, args);
      case Op::AND:
        if (args.size() == 2) [[likely]] return and_(a, args[0], args[1]);
        return and_n(a, args);
      case Op::OR:
        if (args.size() == 2) [[likely]] return or_(a, args[0], args[1]);
        return or_n(a, args);
      case Op::POW:
        return pow(a, args[0], args[1]);
      case Op::NEG:
        return neg(a, args[0]);
      case Op::EQ:
        return eq(a, args[0], args[1]);
      case Op::NE:
        return ne(a, args[0], args[1]);
      case Op::LT:
        return lt(a, args[0], args[1]);
      case Op::LE:
        return le(a, args[0], args[1]);
      case Op::GT:
        return gt(a, args[0], args[1]);
      case Op::GE:
        return ge(a, args[0], args[1]);
      case Op::NOT:
        return not_(a, args[0]);
      case Op::FLOOR_DIV:
        return floor_div(a, args[0], args[1]);
      case Op::CLEAN_DIV:
        return clean_div(a, args[0], args[1]);
      case Op::CEIL_DIV:
        return ceil_div(a, args[0], args[1]);
      case Op::MOD:
        return mod(a, args[0], args[1]);
      case Op::PYTHON_MOD:
        return python_mod(a, args[0], args[1]);
      case Op::MODULAR_INDEXING:
        return modular_indexing(a, args[0], args[1], args[2]);
      case Op::WHERE:
        return where(a, args[0], args[1], args[2]);
      case Op::MIN:
        if (args.size() == 2) [[likely]] return min_expr(a, args[0], args[1]);
        return min_n(a, args);
      case Op::MAX:
        if (args.size() == 2) [[likely]] return max_expr(a, args[0], args[1]);
        return max_n(a, args);

      // Atoms must be built via the dedicated constructors
      // (integer()/float()/symbol()/bool_true()/bool_false()); they
      // carry a payload, not child args, and the args.data() vector
      // would be silently ignored by intern_node.  make(atom, ...) is
      // a caller bug, not a runtime-dispatchable condition.
      case Op::INTEGER:
      case Op::FLOAT:
      case Op::SYMBOL:
      case Op::BOOL_TRUE:
      case Op::BOOL_FALSE:
        std::unreachable();

      // Sentinel: not a valid op value.  Reached only via corrupted
      // input or a caller passing a cast-from-int out-of-range value.
      case Op::NUM_OPS:
        std::unreachable();

      // Opaque math (SIN, COS, LOG, …), type conversions, bitwise,
      // shift, identity, and IS_NON_OVERLAPPING_AND_DENSE all share
      // the generic interning path — no canonical simplifier required.
      // Listed explicitly-as-fallthrough so adding a new op surfaces
      // here (via -Wswitch) rather than disappearing into a catch-all.
      case Op::INT_TRUE_DIV:
      case Op::FLOAT_TRUE_DIV:
      case Op::CEIL_TO_INT:
      case Op::FLOOR_TO_INT:
      case Op::TRUNC_TO_FLOAT:
      case Op::TRUNC_TO_INT:
      case Op::ROUND_TO_INT:
      case Op::ROUND_DECIMAL:
      case Op::TO_FLOAT:
      case Op::LSHIFT:
      case Op::RSHIFT:
      case Op::POW_BY_NATURAL:
      case Op::FLOAT_POW:
      case Op::IDENTITY:
      case Op::IS_NON_OVERLAPPING_AND_DENSE:
      case Op::SQRT:
      case Op::COS:
      case Op::COSH:
      case Op::SIN:
      case Op::SINH:
      case Op::TAN:
      case Op::TANH:
      case Op::ASIN:
      case Op::ACOS:
      case Op::ATAN:
      case Op::EXP:
      case Op::LOG:
      case Op::ASINH:
      case Op::LOG2:
      case Op::ABS:
      case Op::BITWISE_AND:
      case Op::BITWISE_OR:
      case Op::BITWISE_XOR:
        break;  // fall through to intern_node below

      // Required by -Wswitch-default even though every enumerator is
      // handled above.  Reaching this arm implies Op was read from
      // out-of-range memory (e.g. casting a corrupted uint8_t to Op).
      default:
        std::unreachable();
    }
    uint16_t f = detail::composite_flags(op, args.data(),
                                         static_cast<uint8_t>(args.size()));
    return intern_node(a, op, args.data(),
                       static_cast<uint8_t>(args.size()), f, SymbolId{}, 0);
  }

  // ---- Stats ----

  [[nodiscard]] size_t intern_size() const {
    return intern_count_;
  }
  [[nodiscard]] size_t intern_capacity() const {
    return capacity_;
  }
  [[nodiscard]] size_t arena_bytes() const {
    return arena_.total_allocated();
  }
  [[nodiscard]] const char* symbol_name(SymbolId id) const CRUCIBLE_LIFETIMEBOUND {
    return (id.raw() < symbol_names_.size()) ? symbol_names_[id.raw()] : nullptr;
  }

 private:
  static constexpr int64_t kIntCacheLow = -128;
  static constexpr int64_t kIntCacheHigh = 127;
  static constexpr size_t kIntCacheSize =
      static_cast<size_t>(kIntCacheHigh - kIntCacheLow + 1);

  const Expr* make_integer(fx::Alloc a, int64_t val) {
    return intern_node(
        a, Op::INTEGER, nullptr, 0, detail::integer_flags(val), SymbolId{}, val);
  }

  // ---- GCD / coefficient helpers for division rules ----

  static int64_t gcd_(int64_t a, int64_t b) {
    a = (a < 0) ? -a : a;
    b = (b < 0) ? -b : b;
    while (b) {
      int64_t t = b;
      b = a % b;
      a = t;
    }
    return a;
  }

  // Integer coefficient of a term: MUL(3,x,y) → 3, INTEGER(5) → 5, x → 1
  static int64_t integer_coefficient_(const Expr* e) {
    if (e->op == Op::INTEGER) return e->as_int();
    if (e->op == Op::MUL) {
      for (uint8_t i = 0; i < e->nargs; ++i)
        if (e->args[i]->op == Op::INTEGER) return e->args[i]->as_int();
    }
    return 1;
  }

  // GCD of |integer coefficients| across all ADD terms.
  //
  // Overflow trap: unary negation on INT64_MIN (-(-2^63)) is
  // undefined behavior (the positive value 2^63 doesn't fit in
  // int64_t).  Adversarial or corrupt ADD arms could carry
  // INT64_MIN coefficients; the GCD walk below would UB through
  // the negation.  Use bit-twiddle absolute value that treats
  // INT64_MIN → INT64_MAX (one off, acceptable for GCD — the loss
  // of 1 unit cannot change the resulting GCD since all other
  // coefficients are ≤ INT64_MAX anyway).
  static constexpr int64_t safe_abs_(int64_t c) noexcept {
    // For c == INT64_MIN: cast to uint64_t, negate (legal in
    // unsigned), cast back.  Result is INT64_MIN in two's comp
    // (still negative).  That would poison GCD; instead clamp to
    // INT64_MAX for the GCD walk.
    if (c == std::numeric_limits<int64_t>::min())
      return std::numeric_limits<int64_t>::max();
    return (c < 0) ? -c : c;
  }

  int64_t integer_factor_(const Expr* e) const {
    if (e->op == Op::ADD) {
      int64_t g = 0;
      for (uint8_t i = 0; i < e->nargs; ++i) {
        int64_t c = safe_abs_(integer_coefficient_(e->args[i]));
        g = (g == 0) ? c : gcd_(g, c);
      }
      return (g == 0) ? 1 : g;
    }
    return safe_abs_(integer_coefficient_(e));
  }

  // Divide all integer coefficients in expression by g
  const Expr* divide_coefficients_(fx::Alloc a, const Expr* e, int64_t g) {
    if (g <= 1) return e;
    if (e->op == Op::INTEGER) return integer(a, e->as_int() / g);
    if (e->op == Op::MUL) {
      for (uint8_t i = 0; i < e->nargs; ++i) {
        if (e->args[i]->op == Op::INTEGER) {
          int64_t new_coeff = e->args[i]->as_int() / g;
          if (new_coeff == 1 && e->nargs == 2)
            return e->args[1 - i];
          const Expr* buf[255];
          uint8_t n = 0;
          for (uint8_t j = 0; j < e->nargs; ++j)
            buf[n++] = (j == i) ? integer(a, new_coeff) : e->args[j];
          return mul_n(a, std::span{buf, n});
        }
      }
      return e; // no integer factor
    }
    if (e->op == Op::ADD) {
      const Expr* buf[255];
      for (uint8_t i = 0; i < e->nargs; ++i)
        buf[i] = divide_coefficients_(a, e->args[i], g);
      return add_n(a, std::span{buf, e->nargs});
    }
    return e;
  }

  // Flatten MIN/MAX + dedup + sort
  const Expr* min_n(fx::Alloc a, std::span<const Expr* const> inputs) {
    const Expr* buf[64];
    uint8_t n = 0;
    for (auto* e : inputs) {
      if (e->op == Op::MIN) {
        for (uint8_t i = 0; i < e->nargs; ++i)
          buf[n++] = e->arg(i);
      } else {
        buf[n++] = e;
      }
    }
    std::ranges::sort(std::span{buf, n});
    uint8_t m = 1;
    for (uint8_t i = 1; i < n; ++i)
      if (buf[i] != buf[m - 1]) buf[m++] = buf[i];
    if (m == 1) return buf[0];
    uint16_t f = detail::composite_flags(Op::MIN, buf, m);
    return intern_node(a, Op::MIN, buf, m, f, SymbolId{}, 0);
  }

  const Expr* max_n(fx::Alloc a, std::span<const Expr* const> inputs) {
    const Expr* buf[64];
    uint8_t n = 0;
    for (auto* e : inputs) {
      if (e->op == Op::MAX) {
        for (uint8_t i = 0; i < e->nargs; ++i)
          buf[n++] = e->arg(i);
      } else {
        buf[n++] = e;
      }
    }
    std::ranges::sort(std::span{buf, n});
    uint8_t m = 1;
    for (uint8_t i = 1; i < n; ++i)
      if (buf[i] != buf[m - 1]) buf[m++] = buf[i];
    if (m == 1) return buf[0];
    uint16_t f = detail::composite_flags(Op::MAX, buf, m);
    return intern_node(a, Op::MAX, buf, m, f, SymbolId{}, 0);
  }

  // Flatten ADD children, fold integer constants, combine like terms,
  // sort, intern. Term combining: ADD(MUL(a,b), MUL(3,a,b)) → ADD(MUL(4,a,b)).
  // Critical for expand(): (a+b)^n produces n+1 binomial terms, not 2^n.
  const Expr* add_n(fx::Alloc a, std::span<const Expr* const> inputs) {
    const Expr* buf[256];
    uint8_t n = 0;
    int64_t int_sum = 0;

    // Phase 1: Flatten nested ADD, separate integer constants
    for (auto* e : inputs) {
      if (e->op == Op::ADD) {
        for (uint8_t i = 0; i < e->nargs; ++i) {
          if (e->args[i]->op == Op::INTEGER)
            int_sum += e->args[i]->payload;
          else {
            assert(n < 255 && "too many ADD terms");
            buf[n++] = e->args[i];
          }
        }
      } else if (e->op == Op::INTEGER) {
        int_sum += e->payload;
      } else {
        assert(n < 255 && "too many ADD terms");
        buf[n++] = e;
      }
    }

    if (n == 0)
      return integer(a, int_sum);

    // Phase 2: Decompose each term into (coefficient, base).
    // MUL(3, a, b) → coeff=3, base=MUL(a,b)
    // MUL(a, b)    → coeff=1, base=MUL(a,b)   [same base!]
    // a            → coeff=1, base=a
    // The "base" is the coefficient-free interned form. Two terms with
    // the same base get their coefficients summed: a + 2a → 3a.
    struct CoeffTerm {
      int64_t coeff;
      const Expr* base;
    };
    CoeffTerm terms[256];
    uint8_t nt = 0;

    for (uint8_t j = 0; j < n; ++j) {
      int64_t coeff = 1;
      const Expr* base = buf[j];

      if (buf[j]->op == Op::MUL) {
        // Strip integer coefficient from MUL
        const Expr* factors[256];
        uint8_t nf = 0;
        for (uint8_t k = 0; k < buf[j]->nargs; ++k) {
          if (buf[j]->args[k]->op == Op::INTEGER)
            coeff = buf[j]->args[k]->payload;
          else
            factors[nf++] = buf[j]->args[k];
        }
        if (nf == 0) {
          // Pure integer MUL (shouldn't happen after phase 1, but be safe)
          int_sum += coeff;
          continue;
        } else if (nf == 1) {
          base = factors[0];
        } else {
          // Re-intern coefficient-free MUL as the grouping key.
          // factors[] are already sorted (came from a canonical MUL).
          uint16_t f = detail::composite_flags(Op::MUL, factors, nf);
          base = intern_node(a, Op::MUL, factors, nf, f, SymbolId{}, 0);
        }
      }
      terms[nt++] = {.coeff = coeff, .base = base};
    }

    // Phase 3: Sort by base pointer, merge adjacent same-base entries
    std::ranges::sort(
        std::span{terms, nt},
        [](const CoeffTerm& lhs, const CoeffTerm& rhs) {
          return lhs.base < rhs.base;
        });

    const Expr* collected[256];
    uint8_t cn = 0;
    uint8_t i = 0;
    while (i < nt) {
      int64_t total_coeff = terms[i].coeff;
      const Expr* base = terms[i].base;
      auto j = static_cast<uint8_t>(i + 1);
      while (j < nt && terms[j].base == base) {
        total_coeff += terms[j].coeff;
        ++j;
      }

      if (total_coeff == 0) {
        // Terms cancelled out (e.g., a + (-a))
      } else if (total_coeff == 1) {
        collected[cn++] = base;
      } else {
        const Expr* mul_args[] = {integer(a, total_coeff), base};
        collected[cn++] = mul_n(a, mul_args);
      }
      i = j;
    }

    // Reattach integer sum (omit zero unless it's the only term)
    if (int_sum != 0 || cn == 0) {
      assert(cn < 255);
      collected[cn++] = integer(a, int_sum);
    }
    if (cn == 1)
      return collected[0];

    // Final sort for canonical ordering
    std::ranges::sort(std::span{collected, cn});
    uint16_t f = detail::composite_flags(Op::ADD, collected, cn);
    return intern_node(a, Op::ADD, collected, cn, f, SymbolId{}, 0);
  }

  // Flatten MUL children, fold integer constants, sort, intern.
  const Expr* mul_n(fx::Alloc a, std::span<const Expr* const> inputs) {
    const Expr* buf[256];
    uint8_t n = 0;
    int64_t int_prod = 1;

    for (auto* e : inputs) {
      if (e->op == Op::MUL) {
        for (uint8_t i = 0; i < e->nargs; ++i) {
          if (e->args[i]->op == Op::INTEGER)
            int_prod *= e->args[i]->payload;
          else {
            assert(n < 255 && "too many MUL terms");
            buf[n++] = e->args[i];
          }
        }
      } else if (e->op == Op::INTEGER) {
        int_prod *= e->payload;
      } else {
        assert(n < 255 && "too many MUL terms");
        buf[n++] = e;
      }
    }

    if (int_prod == 0)
      return integer(a, 0);
    // Reattach integer product (omit 1 unless it's the only term)
    if (int_prod != 1 || n == 0) {
      assert(n < 255);
      buf[n++] = integer(a, int_prod);
    }
    if (n == 1)
      return buf[0];

    std::ranges::sort(std::span{buf, n});
    uint16_t f = detail::composite_flags(Op::MUL, buf, n);
    return intern_node(a, Op::MUL, buf, n, f, SymbolId{}, 0);
  }

  // Flatten AND children, short-circuit on FALSE, filter TRUE, sort, intern.
  const Expr* and_n(fx::Alloc a, std::span<const Expr* const> inputs) {
    const Expr* buf[64];
    uint8_t n = 0;

    for (auto* e : inputs) {
      if (e == false_)
        return false_;
      if (e == true_)
        continue;
      if (e->op == Op::AND) {
        for (uint8_t i = 0; i < e->nargs; ++i) {
          if (e->args[i] == false_)
            return false_;
          if (e->args[i] == true_)
            continue;
          assert(n < 64);
          buf[n++] = e->args[i];
        }
      } else {
        assert(n < 64);
        buf[n++] = e;
      }
    }

    if (n == 0)
      return true_;
    if (n == 1)
      return buf[0];
    std::ranges::sort(std::span{buf, n});
    return intern_node(a, Op::AND, buf, n, ExprFlags::IS_BOOLEAN, SymbolId{}, 0);
  }

  // Flatten OR children, short-circuit on TRUE, filter FALSE, sort, intern.
  const Expr* or_n(fx::Alloc a, std::span<const Expr* const> inputs) {
    const Expr* buf[64];
    uint8_t n = 0;

    for (auto* e : inputs) {
      if (e == true_)
        return true_;
      if (e == false_)
        continue;
      if (e->op == Op::OR) {
        for (uint8_t i = 0; i < e->nargs; ++i) {
          if (e->args[i] == true_)
            return true_;
          if (e->args[i] == false_)
            continue;
          assert(n < 64);
          buf[n++] = e->args[i];
        }
      } else {
        assert(n < 64);
        buf[n++] = e;
      }
    }

    if (n == 0)
      return false_;
    if (n == 1)
      return buf[0];
    std::ranges::sort(std::span{buf, n});
    return intern_node(a, Op::OR, buf, n, ExprFlags::IS_BOOLEAN, SymbolId{}, 0);
  }

  // Swiss table probe + insert. Returns existing interned node or creates new.
  //
  // Probing: SIMD-compare kGroupWidth H2 tags → bitmask → iterate matches.
  // H2 filters 127/128 candidates; full hash rejects the rest.
  // Expected structural comparisons per lookup: ~0.01 (virtually zero
  // false positives). Insert-only: no tombstones, empty-stop is sound.
  //
  // Hot path optimization: hash check (64-bit compare) is the primary
  // filter. After hash match (P(collision) ≈ 2^-57), we only need
  // args pointer comparison. The hash encodes op/nargs/flags/symbol_id/
  // payload, so re-checking those is redundant on a hash match.
  // We still verify all fields as a safety net — the compiler optimizes
  // the packed comparison into a single 64-bit op.
  CRUCIBLE_UNSAFE_BUFFER_USAGE
  CRUCIBLE_INLINE const Expr* intern_node(
      fx::Alloc a,
      Op op,
      const Expr* const* args,
      uint8_t nargs,
      uint16_t flags,
      SymbolId symbol_id,
      int64_t payload) {
    // Load factor 87.5% (7/8). Swiss table tolerates higher load than
    // linear probing because SIMD amortizes the cost of denser groups.
    if (intern_count_ * 8 >= capacity_ * 7) [[unlikely]]
      rehash();

    uint64_t h =
        detail::expr_hash(op, payload, symbol_id, flags, args, nargs);
    int8_t tag = detail::h2_tag(h);

    // Pack small fields for a single 64-bit comparison instead of
    // 4 separate branches. Same packing as expr_hash uses.
    uint64_t packed = static_cast<uint64_t>(std::to_underlying(op))
                    | (static_cast<uint64_t>(nargs) << 8)
                    | (static_cast<uint64_t>(flags) << 16)
                    | (static_cast<uint64_t>(symbol_id.raw()) << 32);

    // Operate directly on slot indices (base) instead of group indices.
    // Eliminates the g*kGroupWidth multiply on every probe iteration.
    size_t slot_mask = capacity_ - 1;
    size_t base = (h * detail::kGroupWidth) & slot_mask;
    size_t probe = 0;

    while (true) {
      auto group = detail::CtrlGroup::load(&ctrl_[base]);

      // Phase 1: Check H2 tag matches within the group.
      // SIMD produces a bitmask; iterate only the ~0.11 expected matches.
      auto matches = group.match(tag);
      while (matches) {
        size_t idx = base + matches.lowest();
        const Expr* slot = slots_[idx];
        // Full hash compare: rejects with P(false positive) ≈ 2^-57.
        // Packed metadata compare: catches the astronomically rare hash
        // collision where different (op,nargs,flags,symbol_id) produce
        // the same 64-bit hash.
        if (slot->hash == h && slot->payload == payload) [[likely]] {
          // Pack the slot's metadata the same way for single compare
          uint64_t slot_packed =
              static_cast<uint64_t>(std::to_underlying(slot->op))
              | (static_cast<uint64_t>(slot->nargs) << 8)
              | (static_cast<uint64_t>(slot->flags) << 16)
              | (static_cast<uint64_t>(slot->symbol_id.raw()) << 32);
          if (slot_packed == packed) [[likely]] {
            // Args comparison — specialized for common arities
            switch (nargs) {
              case 0:
                return slot;
              case 1:
                if (slot->args[0] == args[0])
                  return slot;
                break;
              case 2:
                if (slot->args[0] == args[0] && slot->args[1] == args[1])
                  return slot;
                break;
              case 3:
                if (slot->args[0] == args[0] && slot->args[1] == args[1] &&
                    slot->args[2] == args[2])
                  return slot;
                break;
              default: {
                bool eq = true;
                for (uint8_t i = 0; i < nargs; ++i) {
                  if (slot->args[i] != args[i]) {
                    eq = false;
                    break;
                  }
                }
                if (eq)
                  return slot;
                break;
              }
            }
          }
        }
        matches.clear_lowest();
      }

      // Phase 2: If any empty slot exists in this group, the entry
      // is definitively not in the table (insert-only, no tombstones).
      auto empties = group.match_empty();
      if (empties) [[likely]] {
        size_t idx = base + empties.lowest();

        // Copy args into the arena BEFORE constructing the Expr — the
        // Expr's args pointer is const, so it can only be set via the
        // constructor (not assigned later).  For nargs == 0 we pass
        // nullptr, matching the legacy null-args contract.
        const Expr** owned_args = nullptr;
        if (nargs > 0) {
          owned_args = arena_.alloc_array<const Expr*>(a, nargs);
          std::memcpy(owned_args, args, nargs * sizeof(const Expr*));
        }

        // Placement-new into arena storage via the full-args
        // constructor.  The const fields of Expr are initialized
        // in-place; no post-construction mutation is possible
        // (or desired — Expr is immutable by contract).
        void* storage = arena_.alloc_obj<Expr>(a);
        Expr* e = ::new (storage) Expr(op, nargs, flags, symbol_id,
                                       h, payload, owned_args);

        ctrl_[idx] = tag;
        slots_[idx] = e;
        ++intern_count_;
        return e;
      }

      // Triangular probing: visits all groups before repeating.
      // Sequence: base, base+G, base+3G, base+6G, ...
      ++probe;
      base = (base + probe * detail::kGroupWidth) & slot_mask;
    }
  }

  // Allocate `ctrl_` + `slots_` in a single contiguous backing buffer.
  // Layout:
  //   [ctrl_: `cap` bytes] [slots_: `cap * 8` bytes]
  // slots_ starts at offset `cap`, which is always a multiple of
  // kGroupWidth (≥ 16) → trivially 8-byte aligned for the pointer array.
  // Merging the two saves one malloc/free pair per ctor/rehash, reduces
  // fragmentation, and typically keeps ctrl_ and the first slots on
  // adjacent cache lines (better prefetch behavior on the insert path).
  void alloc_tables_(size_t cap) {
    const size_t slot_bytes = cap * sizeof(const Expr*);
    const size_t total      = cap + slot_bytes;
    // aligned_alloc requires size to be a multiple of alignment.
    const size_t rounded    = (total + 63) & ~size_t(63);
    backing_ = std::aligned_alloc(64, rounded);
    if (!backing_) [[unlikely]] std::abort();

    ctrl_  = static_cast<int8_t*>(backing_);
    slots_ = reinterpret_cast<const Expr**>(
        static_cast<char*>(backing_) + cap);
    std::memset(ctrl_, 0x80, cap);          // kEmpty = 0x80
    std::memset(slots_, 0, slot_bytes);     // null-init slot pointers
  }

  CRUCIBLE_UNSAFE_BUFFER_USAGE
  void rehash() { grow_to_(capacity_ * 2); }

  // Core resize: allocate new ctrl_/slots_ of `new_capacity`, re-insert
  // every live entry at its new home, free the old buffer. Called both
  // by rehash() (implicit doubling at 87.5% load) and reserve() (caller-
  // directed pre-growth). new_capacity must be a power of 2 and a
  // multiple of kGroupWidth — the public entrypoints enforce that.
  CRUCIBLE_UNSAFE_BUFFER_USAGE
  void grow_to_(size_t new_capacity) {
    size_t old_cap = capacity_;
    int8_t* old_ctrl = ctrl_;
    const Expr** old_slots = slots_;
    void* old_backing = backing_;

    capacity_ = new_capacity;
    alloc_tables_(capacity_);
    intern_count_ = 0;

    size_t slot_mask = capacity_ - 1;

    for (size_t i = 0; i < old_cap; ++i) {
      if (old_ctrl[i] == detail::kEmpty)
        continue;

      const Expr* e = old_slots[i];
      int8_t tag = detail::h2_tag(e->hash);
      size_t base = (e->hash * detail::kGroupWidth) & slot_mask;
      size_t probe = 0;

      while (true) {
        auto group = detail::CtrlGroup::load(&ctrl_[base]);
        auto empties = group.match_empty();
        if (empties) {
          size_t idx = base + empties.lowest();
          ctrl_[idx] = tag;
          slots_[idx] = e;
          ++intern_count_;
          break;
        }
        ++probe;
        base = (base + probe * detail::kGroupWidth) & slot_mask;
      }
    }
    std::free(old_backing);
  }

  Arena arena_;
  void*   backing_ = nullptr;   // One aligned buffer holds ctrl_ + slots_.
  int8_t* ctrl_;                // Points into backing_ at offset 0.
  const Expr** slots_;          // Points into backing_ at offset capacity_.
  size_t capacity_;             // Total slots (always power of 2, multiple of kGroupWidth)
  size_t intern_count_;         // Number of occupied slots
  std::vector<const char*> symbol_names_;

  const Expr* int_cache_[kIntCacheSize];
  const Expr* true_;
  const Expr* false_;
};

} // namespace crucible
