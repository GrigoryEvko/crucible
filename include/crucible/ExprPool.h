#pragma once

#include <crucible/Arena.h>
#include <crucible/Expr.h>
#include <crucible/Ops.h>
#include <crucible/SwissCtrl.h>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ranges>
#include <span>
#include <utility>
#include <vector>

namespace crucible {

namespace detail {

// Hash all structural fields of an expression. Args are compared by pointer
// (since children are themselves interned), so we hash their addresses.
inline uint64_t expr_hash(
    Op op,
    int64_t payload,
    SymbolId symbol_id,
    uint16_t flags,
    const Expr* const* args,
    uint8_t nargs) {
  uint64_t h = 0x9E3779B97F4A7C15ULL; // golden ratio constant
  h ^= std::to_underlying(op) * 0x517CC1B727220A95ULL;
  h ^= fmix64(static_cast<uint64_t>(payload));
  h ^= static_cast<uint64_t>(symbol_id.raw()) * 0x6C62272E07BB0142ULL;
  h ^= static_cast<uint64_t>(flags) * 0x85EBCA6BULL;
  for (uint8_t i = 0; i < nargs; ++i) {
    h ^= fmix64(reinterpret_cast<uintptr_t>(args[i]));
    h *= 0x9E3779B97F4A7C15ULL;
  }
  return fmix64(h);
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

    default:
      return 0;
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
class ExprPool {
 public:
  explicit ExprPool(size_t initial_capacity = 1 << 16) : arena_() {
    // Round up to power of 2, minimum one full SIMD group
    capacity_ = detail::kGroupWidth;
    while (capacity_ < initial_capacity)
      capacity_ <<= 1;

    ctrl_ = static_cast<int8_t*>(std::malloc(capacity_));
    std::memset(ctrl_, 0x80, capacity_); // kEmpty = 0x80
    slots_ = static_cast<const Expr**>(std::calloc(capacity_, sizeof(const Expr*)));
    intern_count_ = 0;

    // Boolean singletons
    true_ = intern_node(Op::BOOL_TRUE, nullptr, 0, ExprFlags::IS_BOOLEAN, SymbolId{}, 0);
    false_ =
        intern_node(Op::BOOL_FALSE, nullptr, 0, ExprFlags::IS_BOOLEAN, SymbolId{}, 0);

    // Integer cache: -128..127 for O(1) access to common constants
    for (int64_t i = kIntCacheLow; i <= kIntCacheHigh; ++i)
      int_cache_[static_cast<size_t>(i - kIntCacheLow)] = make_integer(i);
  }

  ~ExprPool() {
    std::free(ctrl_);
    std::free(slots_);
  }

  ExprPool(const ExprPool&) = delete("ExprPool owns arena + Swiss table with interior pointers");
  ExprPool& operator=(const ExprPool&) = delete("ExprPool owns arena + Swiss table with interior pointers");
  ExprPool(ExprPool&&) = delete("interned Expr* pointers would dangle after arena move");
  ExprPool& operator=(ExprPool&&) = delete("interned Expr* pointers would dangle after arena move");

  // ---- Atom construction ----

  [[nodiscard]] const Expr* integer(int64_t val) {
    if (val >= kIntCacheLow && val <= kIntCacheHigh)
      return int_cache_[static_cast<size_t>(val - kIntCacheLow)];
    return make_integer(val);
  }

  [[nodiscard]] const Expr* float_(double val) {
    int64_t payload = std::bit_cast<int64_t>(val);
    uint16_t f =
        ExprFlags::IS_REAL | ExprFlags::IS_FINITE | ExprFlags::IS_NUMBER;
    if (val > 0)
      f |= ExprFlags::IS_POSITIVE | ExprFlags::IS_NONNEGATIVE;
    else if (val < 0)
      f |= ExprFlags::IS_NEGATIVE | ExprFlags::IS_NONPOSITIVE;
    else if (val == 0.0)
      f |= ExprFlags::IS_ZERO | ExprFlags::IS_NONNEGATIVE |
           ExprFlags::IS_NONPOSITIVE;
    return intern_node(Op::FLOAT, nullptr, 0, f, SymbolId{}, payload);
  }

  [[nodiscard]] const Expr* symbol(const char* name, SymbolId id, uint16_t assumption_flags) {
    if (id.raw() >= symbol_names_.size())
      symbol_names_.resize(id.raw() + 1, nullptr);
    if (symbol_names_[id.raw()] == nullptr) {
      size_t len = std::strlen(name) + 1;
      char* buf = static_cast<char*>(arena_.alloc(len, 1));
      std::memcpy(buf, name, len);
      symbol_names_[id.raw()] = buf;
    }
    int64_t payload = std::bit_cast<int64_t>(symbol_names_[id.raw()]);
    return intern_node(
        Op::SYMBOL, nullptr, 0,
        assumption_flags | ExprFlags::IS_SYMBOL, id, payload);
  }

  [[nodiscard]] const Expr* bool_true() const {
    return true_;
  }
  [[nodiscard]] const Expr* bool_false() const {
    return false_;
  }

  // ---- Arithmetic ----

  [[nodiscard]] const Expr* add(const Expr* a, const Expr* b) {
    // Constant folding
    if (a->op == Op::INTEGER && b->op == Op::INTEGER)
      return integer(a->payload + b->payload);
    if (a->op == Op::FLOAT && b->op == Op::FLOAT)
      return float_(a->as_float() + b->as_float());
    // Identity
    if (a->is_zero_int())
      return b;
    if (b->is_zero_int())
      return a;
    // Flatten + fold + sort
    const Expr* inputs[] = {a, b};
    return add_n(inputs);
  }

  [[nodiscard]] const Expr* mul(const Expr* a, const Expr* b) {
    // Constant folding
    if (a->op == Op::INTEGER && b->op == Op::INTEGER)
      return integer(a->payload * b->payload);
    if (a->op == Op::FLOAT && b->op == Op::FLOAT)
      return float_(a->as_float() * b->as_float());
    // Zero annihilation
    if (a->is_zero_int() || b->is_zero_int())
      return integer(0);
    // Identity
    if (a->is_one())
      return b;
    if (b->is_one())
      return a;
    // Flatten + fold + sort
    const Expr* inputs[] = {a, b};
    return mul_n(inputs);
  }

  [[nodiscard]] const Expr* pow(const Expr* base, const Expr* exp) {
    // x^0 → 1
    if (exp->is_zero_int())
      return integer(1);
    // x^1 → x
    if (exp->is_one())
      return base;
    // Concrete integer power (small exponents only to avoid overflow)
    if (base->op == Op::INTEGER && exp->op == Op::INTEGER &&
        exp->payload >= 0 && exp->payload <= 62) {
      int64_t result = 1, b = base->payload, e = exp->payload;
      for (int64_t i = 0; i < e; ++i)
        result *= b;
      return integer(result);
    }
    const Expr* args[] = {base, exp};
    uint16_t f = detail::composite_flags(Op::POW, args, 2);
    return intern_node(Op::POW, args, 2, f, SymbolId{}, 0);
  }

  // Canonical form: MUL(-1, x). No NEG nodes in output.
  [[nodiscard]] const Expr* neg(const Expr* a) {
    if (a->op == Op::INTEGER)
      return integer(-a->payload);
    if (a->op == Op::FLOAT)
      return float_(-a->as_float());
    return mul(integer(-1), a);
  }

  // ---- Relational ----

  [[nodiscard]] const Expr* eq(const Expr* a, const Expr* b) {
    if (a == b)
      return true_;
    if (a->op == Op::INTEGER && b->op == Op::INTEGER)
      return (a->payload == b->payload) ? true_ : false_;
    // Eq is commutative: canonical order by pointer
    if (a > b)
      std::swap(a, b);
    const Expr* args[] = {a, b};
    return intern_node(Op::EQ, args, 2, ExprFlags::IS_BOOLEAN, SymbolId{}, 0);
  }

  [[nodiscard]] const Expr* ne(const Expr* a, const Expr* b) {
    if (a == b)
      return false_;
    if (a->op == Op::INTEGER && b->op == Op::INTEGER)
      return (a->payload != b->payload) ? true_ : false_;
    if (a > b)
      std::swap(a, b);
    const Expr* args[] = {a, b};
    return intern_node(Op::NE, args, 2, ExprFlags::IS_BOOLEAN, SymbolId{}, 0);
  }

  [[nodiscard]] const Expr* lt(const Expr* a, const Expr* b) {
    if (a == b)
      return false_;
    if (a->op == Op::INTEGER && b->op == Op::INTEGER)
      return (a->payload < b->payload) ? true_ : false_;
    const Expr* args[] = {a, b};
    return intern_node(Op::LT, args, 2, ExprFlags::IS_BOOLEAN, SymbolId{}, 0);
  }

  [[nodiscard]] const Expr* le(const Expr* a, const Expr* b) {
    if (a == b)
      return true_;
    if (a->op == Op::INTEGER && b->op == Op::INTEGER)
      return (a->payload <= b->payload) ? true_ : false_;
    const Expr* args[] = {a, b};
    return intern_node(Op::LE, args, 2, ExprFlags::IS_BOOLEAN, SymbolId{}, 0);
  }

  [[nodiscard]] const Expr* gt(const Expr* a, const Expr* b) {
    if (a == b)
      return false_;
    if (a->op == Op::INTEGER && b->op == Op::INTEGER)
      return (a->payload > b->payload) ? true_ : false_;
    const Expr* args[] = {a, b};
    return intern_node(Op::GT, args, 2, ExprFlags::IS_BOOLEAN, SymbolId{}, 0);
  }

  [[nodiscard]] const Expr* ge(const Expr* a, const Expr* b) {
    if (a == b)
      return true_;
    if (a->op == Op::INTEGER && b->op == Op::INTEGER)
      return (a->payload >= b->payload) ? true_ : false_;
    const Expr* args[] = {a, b};
    return intern_node(Op::GE, args, 2, ExprFlags::IS_BOOLEAN, SymbolId{}, 0);
  }

  // ---- Logic ----

  [[nodiscard]] const Expr* and_(const Expr* a, const Expr* b) {
    if (a == false_ || b == false_)
      return false_;
    if (a == true_)
      return b;
    if (b == true_)
      return a;
    if (a == b)
      return a;
    const Expr* inputs[] = {a, b};
    return and_n(inputs);
  }

  [[nodiscard]] const Expr* or_(const Expr* a, const Expr* b) {
    if (a == true_ || b == true_)
      return true_;
    if (a == false_)
      return b;
    if (b == false_)
      return a;
    if (a == b)
      return a;
    const Expr* inputs[] = {a, b};
    return or_n(inputs);
  }

  [[nodiscard]] const Expr* not_(const Expr* a) {
    if (a == true_)
      return false_;
    if (a == false_)
      return true_;
    // Double negation elimination
    if (a->op == Op::NOT)
      return a->args[0];
    const Expr* args[] = {a};
    return intern_node(Op::NOT, args, 1, ExprFlags::IS_BOOLEAN, SymbolId{}, 0);
  }

  // ---- Division / Modular ----

  [[nodiscard]] const Expr* floor_div(const Expr* a, const Expr* b) {
    if (a->op == Op::INTEGER && b->op == Op::INTEGER && b->as_int() != 0) {
      int64_t av = a->as_int(), bv = b->as_int();
      int64_t q = av / bv, r = av % bv;
      if (r != 0 && ((r ^ bv) < 0)) --q;
      return integer(q);
    }
    if (a->is_zero_int()) return integer(0);
    if (b->is_one()) return a;
    if (b->is_neg_one()) return neg(a);
    if (a == b) return integer(1);
    // FloorDiv(FloorDiv(x, c1), c2) → FloorDiv(x, c1*c2)
    if (a->op == Op::FLOOR_DIV || a->op == Op::CLEAN_DIV)
      return floor_div(a->arg(0), mul(a->arg(1), b));
    // Extract divisible terms from ADD when divisor is constant
    if (a->op == Op::ADD && b->op == Op::INTEGER && b->as_int() != 0) {
      int64_t d = b->as_int();
      const Expr* quotients[256];
      const Expr* remainders[256];
      uint8_t nq = 0, nr = 0;
      for (uint8_t i = 0; i < a->nargs; ++i) {
        int64_t c = integer_coefficient_(a->arg(i));
        if (c != 0 && c % d == 0)
          quotients[nq++] = divide_coefficients_(a->arg(i), d);
        else
          remainders[nr++] = a->arg(i);
      }
      if (nq > 0) {
        const Expr* qsum = (nq == 1) ? quotients[0]
            : add_n(std::span{quotients, nq});
        if (nr == 0) return qsum;
        const Expr* rsum = (nr == 1) ? remainders[0]
            : add_n(std::span{remainders, nr});
        return add(qsum, floor_div(rsum, b));
      }
    }
    // Integer GCD cancellation
    {
      int64_t g = gcd_(integer_factor_(a), integer_factor_(b));
      if (g > 1)
        return floor_div(
            divide_coefficients_(a, g), divide_coefficients_(b, g));
    }
    const Expr* args[] = {a, b};
    uint16_t f = ExprFlags::IS_INTEGER;
    if (a->is_nonnegative() && b->is_positive())
      f |= ExprFlags::IS_NONNEGATIVE;
    return intern_node(Op::FLOOR_DIV, args, 2, f, SymbolId{}, 0);
  }

  [[nodiscard]] const Expr* clean_div(const Expr* a, const Expr* b) {
    return floor_div(a, b);
  }

  [[nodiscard]] const Expr* ceil_div(const Expr* a, const Expr* b) {
    if (a->op == Op::INTEGER && b->op == Op::INTEGER && b->as_int() != 0) {
      int64_t av = a->as_int(), bv = b->as_int();
      int64_t q = av / bv, r = av % bv;
      if (r != 0 && ((r ^ bv) > 0)) ++q;
      return integer(q);
    }
    // ceil(a/b) = floor((a + b - 1) / b) for positive b
    return floor_div(add(a, add(b, integer(-1))), b);
  }

  [[nodiscard]] const Expr* mod(const Expr* a, const Expr* b) {
    if (a->op == Op::INTEGER && b->op == Op::INTEGER && b->as_int() > 0)
      return integer(a->as_int() % b->as_int());
    if (a->is_zero_int() || a == b || b->is_one()) return integer(0);
    if (b->op == Op::INTEGER && b->as_int() == 2) {
      if (a->is_even()) return integer(0);
      if (a->is_odd()) return integer(1);
    }
    const Expr* args[] = {a, b};
    uint16_t f = ExprFlags::IS_INTEGER | ExprFlags::IS_NONNEGATIVE;
    return intern_node(Op::MOD, args, 2, f, SymbolId{}, 0);
  }

  [[nodiscard]] const Expr* python_mod(const Expr* a, const Expr* b) {
    if (a->op == Op::INTEGER && b->op == Op::INTEGER && b->as_int() != 0) {
      int64_t av = a->as_int(), bv = b->as_int();
      int64_t r = av % bv;
      if (r != 0 && ((r ^ bv) < 0)) r += bv;
      return integer(r);
    }
    if (a->is_zero_int() || a == b || b->is_one()) return integer(0);
    if (b->op == Op::INTEGER && b->as_int() == 2) {
      if (a->is_even()) return integer(0);
      if (a->is_odd()) return integer(1);
    }
    const Expr* args[] = {a, b};
    uint16_t f = ExprFlags::IS_INTEGER;
    return intern_node(Op::PYTHON_MOD, args, 2, f, SymbolId{}, 0);
  }

  [[nodiscard]] const Expr* modular_indexing(
      const Expr* base,
      const Expr* div,
      const Expr* modulus) {
    if (base->is_zero_int() || modulus->is_one()) return integer(0);
    // All concrete
    if (base->op == Op::INTEGER && div->op == Op::INTEGER &&
        modulus->op == Op::INTEGER && div->as_int() != 0 &&
        modulus->as_int() != 0) {
      int64_t bv = base->as_int(), dv = div->as_int(), mv = modulus->as_int();
      int64_t q = bv / dv, r = bv % dv;
      if (r != 0 && ((r ^ dv) < 0)) --q;
      int64_t m = q % mv;
      if (m < 0) m += mv;
      return integer(m);
    }
    // GCD on (base, divisor)
    if (!(div->op == Op::INTEGER && div->as_int() == 1)) {
      int64_t g = gcd_(integer_factor_(base), integer_factor_(div));
      if (g > 1)
        return modular_indexing(
            divide_coefficients_(base, g),
            divide_coefficients_(div, g), modulus);
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
          if (nk == 0) return integer(0);
          const Expr* nb =
              (nk == 1) ? kept[0] : add_n(std::span{kept, nk});
          return modular_indexing(nb, div, modulus);
        }
      }
    }
    // FloorDiv as base: ModIdx(x//a, d, m) → ModIdx(x, a*d, m)
    if (base->op == Op::FLOOR_DIV || base->op == Op::CLEAN_DIV)
      return modular_indexing(base->arg(0), mul(base->arg(1), div), modulus);

    const Expr* args[] = {base, div, modulus};
    uint16_t f = ExprFlags::IS_INTEGER | ExprFlags::IS_NONNEGATIVE;
    return intern_node(Op::MODULAR_INDEXING, args, 3, f, SymbolId{}, 0);
  }

  // ---- Conditional ----

  [[nodiscard]] const Expr* where(const Expr* cond, const Expr* t, const Expr* f) {
    if (cond == true_) return t;
    if (cond == false_) return f;
    if (t == f) return t;
    const Expr* args[] = {cond, t, f};
    uint16_t fl = t->flags & f->flags;
    return intern_node(Op::WHERE, args, 3, fl, SymbolId{}, 0);
  }

  // ---- Min / Max ----

  [[nodiscard]] const Expr* min_expr(const Expr* a, const Expr* b) {
    if (a == b) return a;
    if (a->op == Op::INTEGER && b->op == Op::INTEGER)
      return integer(std::min(a->as_int(), b->as_int()));
    const Expr* inputs[] = {a, b};
    return min_n(inputs);
  }

  [[nodiscard]] const Expr* max_expr(const Expr* a, const Expr* b) {
    if (a == b) return a;
    if (a->op == Op::INTEGER && b->op == Op::INTEGER)
      return integer(std::max(a->as_int(), b->as_int()));
    const Expr* inputs[] = {a, b};
    return max_n(inputs);
  }

  // ---- Generic construction ----
  // Dispatches to canonical constructors for ops that have them,
  // generic interning for everything else.
  [[nodiscard]] const Expr* make(
      Op op, std::span<const Expr* const> args) {
    switch (op) {
      case Op::ADD:
        return add_n(args);
      case Op::MUL:
        return mul_n(args);
      case Op::AND:
        return and_n(args);
      case Op::OR:
        return or_n(args);
      case Op::POW:
        return pow(args[0], args[1]);
      case Op::NEG:
        return neg(args[0]);
      case Op::EQ:
        return eq(args[0], args[1]);
      case Op::NE:
        return ne(args[0], args[1]);
      case Op::LT:
        return lt(args[0], args[1]);
      case Op::LE:
        return le(args[0], args[1]);
      case Op::GT:
        return gt(args[0], args[1]);
      case Op::GE:
        return ge(args[0], args[1]);
      case Op::NOT:
        return not_(args[0]);
      case Op::FLOOR_DIV:
        return floor_div(args[0], args[1]);
      case Op::CLEAN_DIV:
        return clean_div(args[0], args[1]);
      case Op::CEIL_DIV:
        return ceil_div(args[0], args[1]);
      case Op::MOD:
        return mod(args[0], args[1]);
      case Op::PYTHON_MOD:
        return python_mod(args[0], args[1]);
      case Op::MODULAR_INDEXING:
        return modular_indexing(args[0], args[1], args[2]);
      case Op::WHERE:
        return where(args[0], args[1], args[2]);
      case Op::MIN:
        return min_n(args);
      case Op::MAX:
        return max_n(args);
      default:
        break;
    }
    uint16_t f = detail::composite_flags(op, args.data(),
                                         static_cast<uint8_t>(args.size()));
    return intern_node(op, args.data(),
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
  [[nodiscard]] const char* symbol_name(SymbolId id) const {
    return (id.raw() < symbol_names_.size()) ? symbol_names_[id.raw()] : nullptr;
  }

 private:
  static constexpr int64_t kIntCacheLow = -128;
  static constexpr int64_t kIntCacheHigh = 127;
  static constexpr size_t kIntCacheSize =
      static_cast<size_t>(kIntCacheHigh - kIntCacheLow + 1);

  const Expr* make_integer(int64_t val) {
    return intern_node(
        Op::INTEGER, nullptr, 0, detail::integer_flags(val), SymbolId{}, val);
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

  // GCD of |integer coefficients| across all ADD terms
  int64_t integer_factor_(const Expr* e) const {
    if (e->op == Op::ADD) {
      int64_t g = 0;
      for (uint8_t i = 0; i < e->nargs; ++i) {
        int64_t c = integer_coefficient_(e->args[i]);
        c = (c < 0) ? -c : c;
        g = (g == 0) ? c : gcd_(g, c);
      }
      return (g == 0) ? 1 : g;
    }
    int64_t c = integer_coefficient_(e);
    return (c < 0) ? -c : c;
  }

  // Divide all integer coefficients in expression by g
  const Expr* divide_coefficients_(const Expr* e, int64_t g) {
    if (g <= 1) return e;
    if (e->op == Op::INTEGER) return integer(e->as_int() / g);
    if (e->op == Op::MUL) {
      for (uint8_t i = 0; i < e->nargs; ++i) {
        if (e->args[i]->op == Op::INTEGER) {
          int64_t new_coeff = e->args[i]->as_int() / g;
          if (new_coeff == 1 && e->nargs == 2)
            return e->args[1 - i];
          const Expr* buf[255];
          uint8_t n = 0;
          for (uint8_t j = 0; j < e->nargs; ++j)
            buf[n++] = (j == i) ? integer(new_coeff) : e->args[j];
          return mul_n(std::span{buf, n});
        }
      }
      return e; // no integer factor
    }
    if (e->op == Op::ADD) {
      const Expr* buf[255];
      for (uint8_t i = 0; i < e->nargs; ++i)
        buf[i] = divide_coefficients_(e->args[i], g);
      return add_n(std::span{buf, e->nargs});
    }
    return e;
  }

  // Flatten MIN/MAX + dedup + sort
  const Expr* min_n(std::span<const Expr* const> inputs) {
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
    return intern_node(Op::MIN, buf, m, f, SymbolId{}, 0);
  }

  const Expr* max_n(std::span<const Expr* const> inputs) {
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
    return intern_node(Op::MAX, buf, m, f, SymbolId{}, 0);
  }

  // Flatten ADD children, fold integer constants, combine like terms,
  // sort, intern. Term combining: ADD(MUL(a,b), MUL(3,a,b)) → ADD(MUL(4,a,b)).
  // Critical for expand(): (a+b)^n produces n+1 binomial terms, not 2^n.
  const Expr* add_n(std::span<const Expr* const> inputs) {
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
      return integer(int_sum);

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
          base = intern_node(Op::MUL, factors, nf, f, SymbolId{}, 0);
        }
      }
      terms[nt++] = {coeff, base};
    }

    // Phase 3: Sort by base pointer, merge adjacent same-base entries
    std::ranges::sort(
        std::span{terms, nt},
        [](const CoeffTerm& a, const CoeffTerm& b) {
          return a.base < b.base;
        });

    const Expr* collected[256];
    uint8_t cn = 0;
    uint8_t i = 0;
    while (i < nt) {
      int64_t total_coeff = terms[i].coeff;
      const Expr* base = terms[i].base;
      uint8_t j = i + 1;
      while (j < nt && terms[j].base == base) {
        total_coeff += terms[j].coeff;
        ++j;
      }

      if (total_coeff == 0) {
        // Terms cancelled out (e.g., a + (-a))
      } else if (total_coeff == 1) {
        collected[cn++] = base;
      } else {
        const Expr* mul_args[] = {integer(total_coeff), base};
        collected[cn++] = mul_n(mul_args);
      }
      i = j;
    }

    // Reattach integer sum (omit zero unless it's the only term)
    if (int_sum != 0 || cn == 0) {
      assert(cn < 255);
      collected[cn++] = integer(int_sum);
    }
    if (cn == 1)
      return collected[0];

    // Final sort for canonical ordering
    std::ranges::sort(std::span{collected, cn});
    uint16_t f = detail::composite_flags(Op::ADD, collected, cn);
    return intern_node(Op::ADD, collected, cn, f, SymbolId{}, 0);
  }

  // Flatten MUL children, fold integer constants, sort, intern.
  const Expr* mul_n(std::span<const Expr* const> inputs) {
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
      return integer(0);
    // Reattach integer product (omit 1 unless it's the only term)
    if (int_prod != 1 || n == 0) {
      assert(n < 255);
      buf[n++] = integer(int_prod);
    }
    if (n == 1)
      return buf[0];

    std::ranges::sort(std::span{buf, n});
    uint16_t f = detail::composite_flags(Op::MUL, buf, n);
    return intern_node(Op::MUL, buf, n, f, SymbolId{}, 0);
  }

  // Flatten AND children, short-circuit on FALSE, filter TRUE, sort, intern.
  const Expr* and_n(std::span<const Expr* const> inputs) {
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
    return intern_node(Op::AND, buf, n, ExprFlags::IS_BOOLEAN, SymbolId{}, 0);
  }

  // Flatten OR children, short-circuit on TRUE, filter FALSE, sort, intern.
  const Expr* or_n(std::span<const Expr* const> inputs) {
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
    return intern_node(Op::OR, buf, n, ExprFlags::IS_BOOLEAN, SymbolId{}, 0);
  }

  // Swiss table probe + insert. Returns existing interned node or creates new.
  //
  // Probing: SIMD-compare kGroupWidth H2 tags → bitmask → iterate matches.
  // H2 filters 127/128 candidates; full hash rejects the rest.
  // Expected structural comparisons per lookup: ~0.01 (virtually zero
  // false positives). Insert-only: no tombstones, empty-stop is sound.
  const Expr* intern_node(
      Op op,
      const Expr* const* args,
      uint8_t nargs,
      uint16_t flags,
      SymbolId symbol_id,
      int64_t payload) {
    // Load factor 87.5% (7/8). Swiss table tolerates higher load than
    // linear probing because SIMD amortizes the cost of denser groups.
    if (intern_count_ * 8 >= capacity_ * 7)
      rehash();

    uint64_t h =
        detail::expr_hash(op, payload, symbol_id, flags, args, nargs);
    int8_t tag = detail::h2_tag(h);

    size_t num_groups = capacity_ / detail::kGroupWidth;
    size_t group_mask = num_groups - 1;
    size_t g = h & group_mask; // Lower bits for group (independent from H2)
    size_t probe = 0;

    while (true) {
      size_t base = g * detail::kGroupWidth;
      auto group = detail::CtrlGroup::load(&ctrl_[base]);

      // Phase 1: Check H2 tag matches within the group.
      // SIMD produces a bitmask; iterate only the ~0.11 expected matches.
      auto matches = group.match(tag);
      while (matches) {
        size_t idx = base + matches.lowest();
        const Expr* slot = slots_[idx];
        // Full hash rejects remaining false positives (57 additional bits)
        if (slot->hash == h && slot->op == op && slot->nargs == nargs &&
            slot->payload == payload && slot->symbol_id == symbol_id &&
            slot->flags == flags) {
          bool eq = true;
          for (uint8_t i = 0; i < nargs; ++i) {
            if (slot->args[i] != args[i]) {
              eq = false;
              break;
            }
          }
          if (eq)
            return slot;
        }
        matches.clear_lowest();
      }

      // Phase 2: If any empty slot exists in this group, the entry
      // is definitively not in the table (insert-only, no tombstones).
      auto empties = group.match_empty();
      if (empties) {
        size_t idx = base + empties.lowest();

        Expr* e = arena_.alloc_obj<Expr>();
        e->op = op;
        e->nargs = nargs;
        e->flags = flags;
        e->symbol_id = symbol_id;
        e->hash = h;
        e->payload = payload;
        if (nargs > 0) {
          e->args = arena_.alloc_array<const Expr*>(nargs);
          std::memcpy(e->args, args, nargs * sizeof(const Expr*));
        } else {
          e->args = nullptr;
        }
        ctrl_[idx] = tag;
        slots_[idx] = e;
        ++intern_count_;
        return e;
      }

      // Triangular probing: visits all groups before repeating.
      // Sequence: g, g+1, g+3, g+6, g+10, ... (cumulative sums mod num_groups)
      ++probe;
      g = (g + probe) & group_mask;
    }
  }

  void rehash() {
    size_t old_cap = capacity_;
    int8_t* old_ctrl = ctrl_;
    const Expr** old_slots = slots_;

    capacity_ *= 2;
    ctrl_ = static_cast<int8_t*>(std::malloc(capacity_));
    std::memset(ctrl_, 0x80, capacity_);
    slots_ = static_cast<const Expr**>(
        std::calloc(capacity_, sizeof(const Expr*)));
    intern_count_ = 0;

    size_t num_groups = capacity_ / detail::kGroupWidth;
    size_t group_mask = num_groups - 1;

    for (size_t i = 0; i < old_cap; ++i) {
      if (old_ctrl[i] == detail::kEmpty)
        continue;

      const Expr* e = old_slots[i];
      int8_t tag = detail::h2_tag(e->hash);
      size_t g = e->hash & group_mask;
      size_t probe = 0;

      while (true) {
        size_t base = g * detail::kGroupWidth;
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
        g = (g + probe) & group_mask;
      }
    }
    std::free(old_ctrl);
    std::free(old_slots);
  }

  Arena arena_;
  int8_t* ctrl_;           // Control bytes: kEmpty (0x80) or H2 tag (0x00..0x7F)
  const Expr** slots_;     // Slot array: interned Expr* pointers
  size_t capacity_;        // Total slots (always power of 2, multiple of kGroupWidth)
  size_t intern_count_;    // Number of occupied slots
  std::vector<const char*> symbol_names_;

  const Expr* int_cache_[kIntCacheSize];
  const Expr* true_;
  const Expr* false_;
};

} // namespace crucible
