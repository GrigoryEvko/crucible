#pragma once

#include <crucible/Arena.h>
#include <crucible/Expr.h>
#include <crucible/Ops.h>
#include <crucible/Platform.h>
#include <crucible/Saturate.h>
#include <crucible/SwissTable.h>
#include <crucible/fixy/Source.h>
#include <crucible/fixy/Wrap.h>
#include <crucible/safety/Decide.h>
#include <crucible/safety/Post.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <ranges>
#include <span>
#include <utility>
#include <vector>

namespace crucible {

namespace detail {

// Hashes the structural fields of an expression.  Children are themselves
// interned, so equal children are the same pointer and their addresses are
// what gets hashed.
//
// This value is process-local.  Arena addresses are randomized per process,
// so the same structural input hashes differently in two processes.  That is
// intentional: the intern table only needs uniqueness within one process.
// Never persist this value, never use it as a durable cache key, and never
// mix it into a content or merkle hash.  A cross-process structural identity
// would need a separate function that recurses through the children's own
// structural hashes instead of their addresses.
//
// The hash stays hand-written rather than reflection-generated because it is
// computed before any Expr exists.  The inputs are loose parameters, so
// reflecting would mean constructing an Expr per probe attempt, and avoiding
// exactly that allocation on a lookup hit is the point of interning.
[[nodiscard, gnu::pure]] inline uint64_t expr_hash(Op op, int64_t payload, SymbolId symbol_id, uint16_t flags,
                                                   const Expr* const* args, uint8_t nargs) {
    // Packing the four small fields into one word avoids a separate mix per
    // field.
    uint64_t packed_metadata = static_cast<uint64_t>(std::to_underlying(op)) | (static_cast<uint64_t>(nargs) << 8)
                             | (static_cast<uint64_t>(flags) << 16) | (static_cast<uint64_t>(symbol_id.raw()) << 32);

    uint64_t mixed_hash = detail::fmix64(packed_metadata ^ 0x9E3779B97F4A7C15ULL
                                         ^ (static_cast<uint64_t>(payload) ^ 0x517CC1B727220A95ULL));

    // Each child is already interned, so its address is unique and carries
    // enough entropy on its own.  The zero, one and two argument cases are
    // unrolled because they dominate.
    //
    // Two arguments take two chained steps rather than one mix over both.
    // A single fmix64 is xor-symmetric, so folding both addresses into one
    // step would hash sub(a, b) and sub(b, a) alike.  The chain distinguishes
    // them because the accumulator has been through fmix64 and the argument
    // has not.  This is a bucket index that a real equality compare confirms,
    // so a collision would cost a probe rather than correctness — but it
    // costs nothing to keep, and the unroll still skips the loop.
    switch (nargs) {
        case 0:
            break;
        case 1:
            mixed_hash = detail::combine_ids(mixed_hash, std::bit_cast<uintptr_t>(args[0]));
            break;
        case 2:
            mixed_hash = detail::combine_ids(detail::combine_ids(mixed_hash, std::bit_cast<uintptr_t>(args[0])),
                                             std::bit_cast<uintptr_t>(args[1]));
            break;
        default:
            for (uint8_t i = 0; i < nargs; ++i)
                mixed_hash = detail::combine_ids(mixed_hash, std::bit_cast<uintptr_t>(args[i]));
            break;
    }
    return mixed_hash;
}

[[nodiscard]] constexpr uint16_t integer_flags(int64_t val) {
    uint16_t flag_bits = ExprFlags::IS_INTEGER | ExprFlags::IS_REAL | ExprFlags::IS_FINITE | ExprFlags::IS_NUMBER;
    if (val > 0)
        flag_bits |= ExprFlags::IS_POSITIVE | ExprFlags::IS_NONNEGATIVE;
    else if (val < 0)
        flag_bits |= ExprFlags::IS_NEGATIVE | ExprFlags::IS_NONPOSITIVE;
    else
        flag_bits |= ExprFlags::IS_ZERO | ExprFlags::IS_NONNEGATIVE | ExprFlags::IS_NONPOSITIVE;
    flag_bits |= (val % 2 == 0) ? ExprFlags::IS_EVEN : ExprFlags::IS_ODD;
    return flag_bits;
}

[[nodiscard]] constexpr uint16_t composite_flags(Op op, const Expr* const* args, uint8_t nargs) {
    switch (op) {
        // Variadic: intersect the numeric type flags of every child.
        case Op::ADD:
        case Op::MUL:
        case Op::MIN:
        case Op::MAX: {
            uint16_t intersected_flags = 0xFFFF;
            for (uint8_t i = 0; i < nargs; ++i)
                intersected_flags &= args[i]->flags;
            return intersected_flags
                 & (ExprFlags::IS_INTEGER | ExprFlags::IS_REAL | ExprFlags::IS_FINITE | ExprFlags::IS_NUMBER);
        }

        case Op::POW:
            return (args[0]->flags & args[1]->flags) & (ExprFlags::IS_REAL | ExprFlags::IS_FINITE);

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
            return ExprFlags::IS_INTEGER | ExprFlags::IS_REAL | ExprFlags::IS_FINITE | ExprFlags::IS_NUMBER;

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
                return (args[1]->flags & args[2]->flags)
                     & (ExprFlags::IS_INTEGER | ExprFlags::IS_REAL | ExprFlags::IS_FINITE | ExprFlags::IS_NUMBER);
            return 0;

        case Op::IDENTITY:
            return (nargs >= 1) ? args[0]->flags : 0;

        case Op::NEG:
            if (nargs >= 1)
                return args[0]->flags
                     & (ExprFlags::IS_INTEGER | ExprFlags::IS_REAL | ExprFlags::IS_FINITE | ExprFlags::IS_NUMBER);
            return 0;

        // Propagates the input's numeric kind and adds non-negativity.
        case Op::ABS:
            if (nargs >= 1)
                return (args[0]->flags
                        & (ExprFlags::IS_INTEGER | ExprFlags::IS_REAL | ExprFlags::IS_FINITE | ExprFlags::IS_NUMBER))
                     | ExprFlags::IS_NONNEGATIVE;
            return 0;

        case Op::BITWISE_XOR:
            return ExprFlags::IS_INTEGER | ExprFlags::IS_REAL | ExprFlags::IS_FINITE | ExprFlags::IS_NUMBER;

        // Atoms set their flags directly at construction and never reach
        // here.  An atom op arriving means a caller bypassed the dedicated
        // constructor and pushed raw args through the composite path.
        case Op::INTEGER:
        case Op::FLOAT:
        case Op::SYMBOL:
        case Op::BOOL_TRUE:
        case Op::BOOL_FALSE:
            std::unreachable();

        // The enum sentinel, never a real op.
        case Op::NUM_OPS:
            std::unreachable();

        // Required by -Werror=switch-default even though every enumerator is
        // handled.  Reaching it means the op was read from out-of-range
        // memory.  A newly added op still trips -Werror=switch first.
        default:
            std::unreachable();
    }
}

// The arity an operation admits, inclusive at both ends.
//
// An empty band, where `min` is above `max`, admits no argument list at all.
// The atoms carry a payload rather than children and have no way through the
// generic factory, so that is their band.
struct ArityBand {
    uint8_t min = 1;
    uint8_t max = Expr::kMaxArgs;
};

// The bound each operation places on its own argument list.
//
// Two separate limits meet here.  The upper end of a variadic band is the
// structural ceiling of the IR, because Expr::nargs cannot name a 256th
// child.  A fixed band is what the constructor for that operation reads: the
// WHERE arm indexes args[2], so a two-element list would read past the span.
//
// The default arm is not dead.  An operation byte read from out-of-range
// memory lands there, and an empty band turns that into a rejection at the
// boundary instead of a walk off the end of the argument list.
[[nodiscard, gnu::const]] constexpr ArityBand op_arity_band(Op op) noexcept {
    constexpr ArityBand kNone{.min = 1, .max = 0};
    switch (op) {
        // Variadic.  One operand is the floor because none of these has a
        // useful nullary reading through a factory that names the operation.
        case Op::ADD:
        case Op::MUL:
        case Op::AND:
        case Op::OR:
        case Op::MIN:
        case Op::MAX:
        case Op::IS_NON_OVERLAPPING_AND_DENSE:
            return ArityBand{.min = 1, .max = Expr::kMaxArgs};

        // Exactly one child.
        case Op::NOT:
        case Op::NEG:
        case Op::IDENTITY:
        case Op::ABS:
        case Op::CEIL_TO_INT:
        case Op::FLOOR_TO_INT:
        case Op::TRUNC_TO_FLOAT:
        case Op::TRUNC_TO_INT:
        case Op::ROUND_TO_INT:
        case Op::TO_FLOAT:
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
            return ArityBand{.min = 1, .max = 1};

        // Exactly two children.
        case Op::POW:
        case Op::EQ:
        case Op::NE:
        case Op::LT:
        case Op::LE:
        case Op::GT:
        case Op::GE:
        case Op::FLOOR_DIV:
        case Op::CLEAN_DIV:
        case Op::CEIL_DIV:
        case Op::INT_TRUE_DIV:
        case Op::FLOAT_TRUE_DIV:
        case Op::MOD:
        case Op::PYTHON_MOD:
        case Op::ROUND_DECIMAL:
        case Op::LSHIFT:
        case Op::RSHIFT:
        case Op::POW_BY_NATURAL:
        case Op::FLOAT_POW:
        case Op::BITWISE_AND:
        case Op::BITWISE_OR:
        case Op::BITWISE_XOR:
            return ArityBand{.min = 2, .max = 2};

        // Exactly three children.
        case Op::MODULAR_INDEXING:
        case Op::WHERE:
            return ArityBand{.min = 3, .max = 3};

        // Atoms set their payload through a dedicated constructor.  The
        // generic factory has no payload parameter, so it can only build a
        // payload-less impostor of one.
        case Op::INTEGER:
        case Op::FLOAT:
        case Op::SYMBOL:
        case Op::BOOL_TRUE:
        case Op::BOOL_FALSE:
            return kNone;

        // The enum sentinel, never a real op.
        case Op::NUM_OPS:
            return kNone;

        default:
            return kNone;
    }
}

}  // namespace detail

// Arena-based expression factory with Swiss-table interning.
//
// Every node is allocated from the internal arena and deduplicated through
// the table, so equal structure means an equal pointer and equality is a
// pointer comparison.  One pool per thread.
//
// The construction methods canonicalize eagerly:
//   - Constant folding: add(3, 5) → integer(8)
//   - Identity elimination: add(x, 0) → x, mul(x, 1) → x
//   - Flattening: add(add(a, b), c) → add(a, b, c)
//   - Constant collection: add(a, 3, b, 5) → add(a, b, 8)
//   - Canonical ordering: add(b, a) → add(a, b) by pointer
//   - Term combining: add(a, 2a) → 3a, by coefficient decomposition
class CRUCIBLE_OWNER ExprPool {
public:
    static constexpr int64_t kIntCacheLow = -128;
    static constexpr int64_t kIntCacheHigh = 127;
    static constexpr size_t kIntCacheSize = static_cast<size_t>(kIntCacheHigh - kIntCacheLow + 1);

    using IntCacheLiteral = fixy::wrap::Refined<fixy::wrap::in_range<kIntCacheLow, kIntCacheHigh>, int64_t>;
    using IntCacheIndex = fixy::wrap::Refined<fixy::wrap::bounded_above<kIntCacheSize - 1>, size_t>;
    using Capacity = fixy::wrap::PowerOfTwo<size_t>;
    using InternCount = fixy::wrap::Monotonic<size_t>;
    using InternedExpr = fixy::wrap::Tagged<const Expr*, fixy::tags::source::Interned>;
    using PureInternedExpr = fixy::wrap::det_safe::Pure<InternedExpr>;

    static_assert(sizeof(IntCacheLiteral) == sizeof(int64_t));
    static_assert(sizeof(IntCacheIndex) == sizeof(size_t));
    static_assert(sizeof(Capacity) == sizeof(size_t));
    static_assert(sizeof(InternCount) == sizeof(size_t));
    static_assert(sizeof(InternedExpr) == sizeof(const Expr*));
    static_assert(sizeof(PureInternedExpr) == sizeof(const Expr*));

    // Sized so a real network's graph never rehashes:
    //   16384 slots at the 7/8 load threshold = 14336 entries
    //   258 of those are seeded by the constructor
    //
    // The backing allocation is 144 KB, the first size above glibc's default
    // 128 KB mmap threshold, so it goes through mmap and munmap directly and
    // returns to the operating system when the pool dies instead of growing
    // the heap pool.
    //
    // A caller that knows its bound calls reserve(n) instead.
    static constexpr size_t kDefaultInitialCapacity = 16384;

    // The probe computes slot_mask as capacity minus one, which requires a
    // power of two.  Past 1 << 30 the single backing allocation no longer
    // fits the address-space budget.
    static_assert(::crucible::decide::is_power_of_two_le<std::size_t>(kDefaultInitialCapacity, std::size_t{1} << 30),
                  "kDefaultInitialCapacity must be a power of two ≤ 1<<30");

    explicit ExprPool(effects::Alloc a, size_t initial_capacity = kDefaultInitialCapacity)
        pre(initial_capacity <= (std::size_t{1} << 30))
        : arena_(), capacity_{rounded_capacity_(initial_capacity)}, intern_count_{0} {
        alloc_tables_(capacity_.value());

        // Boolean singletons
        true_ = intern_node(a, Op::BOOL_TRUE, nullptr, 0, ExprFlags::IS_BOOLEAN, SymbolId{}, 0);
        false_ = intern_node(a, Op::BOOL_FALSE, nullptr, 0, ExprFlags::IS_BOOLEAN, SymbolId{}, 0);

        // Integer cache: -128..127 for O(1) access to common constants
        for (int64_t i = kIntCacheLow; i <= kIntCacheHigh; ++i) {
            const IntCacheLiteral literal{i};
            int_cache_[raw_int_cache_index(int_cache_index(literal))] = make_integer(a, i);
        }
    }

    ~ExprPool() = default;

    ExprPool(const ExprPool&) = delete("ExprPool owns arena + Swiss table with interior pointers");
    ExprPool& operator=(const ExprPool&) = delete("ExprPool owns arena + Swiss table with interior pointers");
    ExprPool(ExprPool&&) = delete("interned Expr* pointers would dangle after arena move");
    ExprPool& operator=(ExprPool&&) = delete("interned Expr* pointers would dangle after arena move");

    // Grows the table so that `n_entries` insertions fit without a rehash.
    // A no-op when the capacity already suffices, and safe to repeat.
    void reserve(size_t n_entries) pre(n_entries <= (((std::size_t{1} << 30) * 7) / 8)) {
        // The load threshold is n_entries * 8 <= capacity * 7, so the
        // capacity needed is ceil(n_entries * 8 / 7).
        const size_t needed = (n_entries * 8 + 6) / 7;
        size_t target = detail::group_width();
        while (target < needed)
            target <<= 1;
        if (target > capacity_.value()) grow_to_(target);
    }

    // ---- Atom construction ----

    [[nodiscard]] const Expr* integer(effects::Alloc a, int64_t val) {
        if (val >= kIntCacheLow && val <= kIntCacheHigh) return cached_integer(IntCacheLiteral{val});
        return make_integer(a, val);
    }

    [[nodiscard]] const Expr* float_(effects::Alloc a, double val) {
        int64_t bit_payload = std::bit_cast<int64_t>(val);
        uint16_t assumption_flags_combined = ExprFlags::IS_REAL | ExprFlags::IS_FINITE | ExprFlags::IS_NUMBER;
        if (val > 0)
            assumption_flags_combined |= ExprFlags::IS_POSITIVE | ExprFlags::IS_NONNEGATIVE;
        else if (val < 0)
            assumption_flags_combined |= ExprFlags::IS_NEGATIVE | ExprFlags::IS_NONPOSITIVE;
        else if ((static_cast<uint64_t>(bit_payload) << 1) == 0) {
            // Shifting the sign bit out catches both signed zeros, and only
            // those: a NaN has a non-zero payload left after the shift.
            assumption_flags_combined |= ExprFlags::IS_ZERO | ExprFlags::IS_NONNEGATIVE | ExprFlags::IS_NONPOSITIVE;
        }
        return intern_node(a, Op::FLOAT, nullptr, 0, assumption_flags_combined, SymbolId{}, bit_payload);
    }

    [[nodiscard]] const Expr* symbol(effects::Alloc a, const char* name, SymbolId id, uint16_t assumption_flags) {
        if (id.raw() >= symbol_names_.size()) symbol_names_.resize(id.raw() + 1, nullptr);
        if (symbol_names_[id.raw()] == nullptr) {
            size_t name_len_with_null = std::strlen(name) + 1;
            char* name_buf =
                static_cast<char*>(arena_.alloc(a, crucible::fixy::wrap::Positive<size_t>{name_len_with_null},
                                                crucible::fixy::wrap::PowerOfTwo<size_t>{1}));
            std::memcpy(name_buf, name, name_len_with_null);
            symbol_names_[id.raw()] = name_buf;
        }
        int64_t name_ptr_payload = std::bit_cast<int64_t>(symbol_names_[id.raw()]);
        const Expr* result =
            intern_node(a, Op::SYMBOL, nullptr, 0, assumption_flags | ExprFlags::IS_SYMBOL, id, name_ptr_payload);

        if (id.raw() >= symbol_exprs_.size()) symbol_exprs_.resize(id.raw() + 1, nullptr);
        symbol_exprs_[id.raw()] = result;

        return result;
    }

    // Returns the interned Expr for `sid` when a prior symbol() call
    // registered it, and nullptr otherwise.  A caller that gets nullptr must
    // fall back to symbol() to register first.
    [[nodiscard, gnu::hot, gnu::pure]] const Expr* fast_symbol(SymbolId sid) const noexcept {
        if (sid.raw() < symbol_exprs_.size() && symbol_exprs_[sid.raw()] != nullptr) [[likely]]
            return symbol_exprs_[sid.raw()];
        return nullptr;
    }

    [[nodiscard]] const Expr* bool_true() const { return true_; }
    [[nodiscard]] const Expr* bool_false() const { return false_; }

    // ---- Arithmetic ----

    [[nodiscard]] const Expr* add(effects::Alloc a, const Expr* lhs, const Expr* rhs) {
        // ADD needs flattening, MUL needs coefficient extraction for term
        // combining, and the two constant kinds need folding.  Everything
        // else can go straight to the intern table.
        if (lhs->op != Op::ADD && rhs->op != Op::ADD && lhs->op != Op::MUL && rhs->op != Op::MUL
            && lhs->op != Op::INTEGER && rhs->op != Op::INTEGER && lhs->op != Op::FLOAT && rhs->op != Op::FLOAT)
            [[likely]] {
            if (lhs == rhs) [[unlikely]]
                return mul(a, integer(a, 2), lhs);
            // Canonical ordering by address, so add(b, a) interns as add(a, b).
            if (lhs > rhs) std::swap(lhs, rhs);
            const Expr* args[] = {lhs, rhs};
            uint16_t composite_flag_bits = detail::composite_flags(Op::ADD, args, 2);
            return intern_node(a, Op::ADD, args, 2, composite_flag_bits, SymbolId{}, 0);
        }
        // Folding saturates rather than wrapping.  Both answers are outside
        // the integers, but a saturated one is the same on every target and
        // in every build mode, and a wrapped one is undefined behaviour that
        // only the -fno-strict-overflow flag currently tames.
        if (lhs->op == Op::INTEGER && rhs->op == Op::INTEGER)
            return integer(a, ::crucible::sat::add_sat(lhs->payload, rhs->payload));
        if (lhs->op == Op::FLOAT && rhs->op == Op::FLOAT) return float_(a, lhs->as_float() + rhs->as_float());
        if (lhs->is_zero_int()) return rhs;
        if (rhs->is_zero_int()) return lhs;
        const Expr* binary_args[] = {lhs, rhs};
        return add_n(a, binary_args);
    }

    [[nodiscard]] const Expr* mul(effects::Alloc a, const Expr* lhs, const Expr* rhs) {
        // Two non-constant, non-MUL children need none of the flatten, fold
        // and sort work below.
        if (lhs->op != Op::MUL && rhs->op != Op::MUL && lhs->op != Op::INTEGER && rhs->op != Op::INTEGER
            && lhs->op != Op::FLOAT && rhs->op != Op::FLOAT) [[likely]] {
            // Canonical ordering by address, so mul(b, a) interns as mul(a, b).
            if (lhs > rhs) std::swap(lhs, rhs);
            const Expr* args[] = {lhs, rhs};
            uint16_t composite_flag_bits = detail::composite_flags(Op::MUL, args, 2);
            return intern_node(a, Op::MUL, args, 2, composite_flag_bits, SymbolId{}, 0);
        }
        if (lhs->op == Op::INTEGER && rhs->op == Op::INTEGER)
            return integer(a, ::crucible::sat::mul_sat(lhs->payload, rhs->payload));
        if (lhs->op == Op::FLOAT && rhs->op == Op::FLOAT) return float_(a, lhs->as_float() * rhs->as_float());
        if (lhs->is_zero_int() || rhs->is_zero_int()) return integer(a, 0);
        if (lhs->is_one()) return rhs;
        if (rhs->is_one()) return lhs;
        const Expr* binary_args[] = {lhs, rhs};
        return mul_n(a, binary_args);
    }

    [[nodiscard]] const Expr* pow(effects::Alloc a, const Expr* base, const Expr* exp) {
        if (exp->is_zero_int()) return integer(a, 1);
        if (exp->is_one()) return base;
        // Only small exponents fold, to keep the repeated product bounded.
        if (base->op == Op::INTEGER && exp->op == Op::INTEGER && exp->payload >= 0 && exp->payload <= 62) {
            int64_t accumulated_product = 1;
            int64_t base_value = base->payload;
            int64_t exponent_value = exp->payload;
            for (int64_t i = 0; i < exponent_value; ++i)
                accumulated_product = ::crucible::sat::mul_sat(accumulated_product, base_value);
            return integer(a, accumulated_product);
        }
        const Expr* args[] = {base, exp};
        uint16_t composite_flag_bits = detail::composite_flags(Op::POW, args, 2);
        return intern_node(a, Op::POW, args, 2, composite_flag_bits, SymbolId{}, 0);
    }

    // The canonical form of a negation is MUL(-1, x).  No NEG node ever
    // reaches the intern table.
    [[nodiscard]] const Expr* neg(effects::Alloc a, const Expr* expr) {
        // Negating the most negative int64 has no result in the type, so the
        // subtraction saturates at the top instead.
        if (expr->op == Op::INTEGER) return integer(a, ::crucible::sat::sub_sat(int64_t{0}, expr->payload));
        if (expr->op == Op::FLOAT) return float_(a, -expr->as_float());
        return mul(a, integer(a, -1), expr);
    }

    // ---- Relational ----

    [[nodiscard]] const Expr* eq(effects::Alloc a, const Expr* lhs, const Expr* rhs) {
        if (lhs == rhs) return true_;
        if (lhs->op == Op::INTEGER && rhs->op == Op::INTEGER) return (lhs->payload == rhs->payload) ? true_ : false_;
        // Equality is commutative, so order the operands canonically.
        if (lhs > rhs) std::swap(lhs, rhs);
        const Expr* args[] = {lhs, rhs};
        return intern_node(a, Op::EQ, args, 2, ExprFlags::IS_BOOLEAN, SymbolId{}, 0);
    }

    [[nodiscard]] const Expr* ne(effects::Alloc a, const Expr* lhs, const Expr* rhs) {
        if (lhs == rhs) return false_;
        if (lhs->op == Op::INTEGER && rhs->op == Op::INTEGER) return (lhs->payload != rhs->payload) ? true_ : false_;
        if (lhs > rhs) std::swap(lhs, rhs);
        const Expr* args[] = {lhs, rhs};
        return intern_node(a, Op::NE, args, 2, ExprFlags::IS_BOOLEAN, SymbolId{}, 0);
    }

    [[nodiscard]] const Expr* lt(effects::Alloc a, const Expr* lhs, const Expr* rhs) {
        if (lhs == rhs) return false_;
        if (lhs->op == Op::INTEGER && rhs->op == Op::INTEGER) return (lhs->payload < rhs->payload) ? true_ : false_;
        const Expr* args[] = {lhs, rhs};
        return intern_node(a, Op::LT, args, 2, ExprFlags::IS_BOOLEAN, SymbolId{}, 0);
    }

    [[nodiscard]] const Expr* le(effects::Alloc a, const Expr* lhs, const Expr* rhs) {
        if (lhs == rhs) return true_;
        if (lhs->op == Op::INTEGER && rhs->op == Op::INTEGER) return (lhs->payload <= rhs->payload) ? true_ : false_;
        const Expr* args[] = {lhs, rhs};
        return intern_node(a, Op::LE, args, 2, ExprFlags::IS_BOOLEAN, SymbolId{}, 0);
    }

    [[nodiscard]] const Expr* gt(effects::Alloc a, const Expr* lhs, const Expr* rhs) {
        if (lhs == rhs) return false_;
        if (lhs->op == Op::INTEGER && rhs->op == Op::INTEGER) return (lhs->payload > rhs->payload) ? true_ : false_;
        const Expr* args[] = {lhs, rhs};
        return intern_node(a, Op::GT, args, 2, ExprFlags::IS_BOOLEAN, SymbolId{}, 0);
    }

    [[nodiscard]] const Expr* ge(effects::Alloc a, const Expr* lhs, const Expr* rhs) {
        if (lhs == rhs) return true_;
        if (lhs->op == Op::INTEGER && rhs->op == Op::INTEGER) return (lhs->payload >= rhs->payload) ? true_ : false_;
        const Expr* args[] = {lhs, rhs};
        return intern_node(a, Op::GE, args, 2, ExprFlags::IS_BOOLEAN, SymbolId{}, 0);
    }

    // ---- Logic ----

    [[nodiscard]] const Expr* and_(effects::Alloc a, const Expr* lhs, const Expr* rhs) {
        if (lhs == false_ || rhs == false_) return false_;
        if (lhs == true_) return rhs;
        if (rhs == true_) return lhs;
        if (lhs == rhs) return lhs;
        const Expr* binary_args[] = {lhs, rhs};
        return and_n(a, binary_args);
    }

    [[nodiscard]] const Expr* or_(effects::Alloc a, const Expr* lhs, const Expr* rhs) {
        if (lhs == true_ || rhs == true_) return true_;
        if (lhs == false_) return rhs;
        if (rhs == false_) return lhs;
        if (lhs == rhs) return lhs;
        const Expr* binary_args[] = {lhs, rhs};
        return or_n(a, binary_args);
    }

    [[nodiscard]] const Expr* not_(effects::Alloc a, const Expr* expr) {
        if (expr == true_) return false_;
        if (expr == false_) return true_;
        if (expr->op == Op::NOT) return expr->args[0];
        const Expr* args[] = {expr};
        return intern_node(a, Op::NOT, args, 1, ExprFlags::IS_BOOLEAN, SymbolId{}, 0);
    }

    // ---- Division / Modular ----

    [[nodiscard]] const Expr* floor_div(effects::Alloc a, const Expr* lhs, const Expr* rhs) {
        if (lhs->op == Op::INTEGER && rhs->op == Op::INTEGER && is_foldable_division_(lhs->as_int(), rhs->as_int())) {
            int64_t dividend = lhs->as_int();
            int64_t divisor = rhs->as_int();
            int64_t quotient = dividend / divisor;
            int64_t remainder = dividend % divisor;
            // Floor adjustment: C truncates toward zero; floor() rounds toward
            // -inf when the remainder has the opposite sign of the divisor.
            // The decrement cannot run off the bottom: a quotient at the
            // minimum needs a divisor of one, which leaves no remainder.
            if (remainder != 0 && ((remainder ^ divisor) < 0)) --quotient;
            return integer(a, quotient);
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
            int64_t divisor = rhs->as_int();
            const Expr* quotients[kScratchArgs];
            const Expr* remainders[kScratchArgs];
            std::size_t num_quotients = 0;
            std::size_t num_remainders = 0;
            for (uint8_t i = 0; i < lhs->nargs; ++i) {
                int64_t coeff = integer_coefficient_(lhs->arg(i));
                // Every term goes to exactly one of the two, and lhs is an
                // interned node, so the two counts together never pass the
                // ceiling either buffer is sized to.
                CRUCIBLE_FATAL_INVARIANT(num_quotients < kScratchArgs && num_remainders < kScratchArgs);
                // The divisor cannot be zero here, and the remainder of the
                // most negative int64 by minus one is undefined, so that pair
                // takes the remainder branch instead of folding.
                if (coeff != 0 && is_foldable_division_(coeff, divisor) && coeff % divisor == 0)
                    quotients[num_quotients++] = divide_coefficients_(a, lhs->arg(i), divisor);
                else
                    remainders[num_remainders++] = lhs->arg(i);
            }
            if (num_quotients > 0) {
                const Expr* quotient_sum =
                    (num_quotients == 1) ? quotients[0] : add_n(a, std::span{quotients, num_quotients});
                if (num_remainders == 0) return quotient_sum;
                const Expr* remainder_sum =
                    (num_remainders == 1) ? remainders[0] : add_n(a, std::span{remainders, num_remainders});
                return add(a, quotient_sum, floor_div(a, remainder_sum, rhs));
            }
        }
        // Integer GCD cancellation
        {
            int64_t common_divisor = gcd_(integer_factor_(lhs), integer_factor_(rhs));
            if (common_divisor > 1)
                return floor_div(a, divide_coefficients_(a, lhs, common_divisor),
                                 divide_coefficients_(a, rhs, common_divisor));
        }
        const Expr* args[] = {lhs, rhs};
        uint16_t composite_flag_bits = ExprFlags::IS_INTEGER;
        if (lhs->is_nonnegative() && rhs->is_positive()) composite_flag_bits |= ExprFlags::IS_NONNEGATIVE;
        return intern_node(a, Op::FLOOR_DIV, args, 2, composite_flag_bits, SymbolId{}, 0);
    }

    [[nodiscard]] const Expr* clean_div(effects::Alloc a, const Expr* lhs, const Expr* rhs) {
        return floor_div(a, lhs, rhs);
    }

    [[nodiscard]] const Expr* ceil_div(effects::Alloc a, const Expr* lhs, const Expr* rhs) {
        if (lhs->op == Op::INTEGER && rhs->op == Op::INTEGER && is_foldable_division_(lhs->as_int(), rhs->as_int())) {
            int64_t dividend = lhs->as_int();
            int64_t divisor = rhs->as_int();
            int64_t quotient = dividend / divisor;
            int64_t remainder = dividend % divisor;
            // Ceil adjustment: when the remainder shares the divisor's sign,
            // C truncation already rounded down; bump up to round toward +inf.
            // The increment cannot run off the top: a quotient at the maximum
            // needs a divisor of one, which leaves no remainder.
            if (remainder != 0 && ((remainder ^ divisor) > 0)) ++quotient;
            return integer(a, quotient);
        }
        // ceil(a/b) = floor((a + b - 1) / b) for positive b
        return floor_div(a, add(a, lhs, add(a, rhs, integer(a, -1))), rhs);
    }

    [[nodiscard]] const Expr* mod(effects::Alloc a, const Expr* lhs, const Expr* rhs) {
        if (lhs->op == Op::INTEGER && rhs->op == Op::INTEGER && rhs->as_int() > 0)
            return integer(a, lhs->as_int() % rhs->as_int());
        if (lhs->is_zero_int() || lhs == rhs || rhs->is_one()) return integer(a, 0);
        if (rhs->op == Op::INTEGER && rhs->as_int() == 2) {
            if (lhs->is_even()) return integer(a, 0);
            if (lhs->is_odd()) return integer(a, 1);
        }
        const Expr* args[] = {lhs, rhs};
        uint16_t composite_flag_bits = ExprFlags::IS_INTEGER | ExprFlags::IS_NONNEGATIVE;
        return intern_node(a, Op::MOD, args, 2, composite_flag_bits, SymbolId{}, 0);
    }

    [[nodiscard]] const Expr* python_mod(effects::Alloc a, const Expr* lhs, const Expr* rhs) {
        if (lhs->op == Op::INTEGER && rhs->op == Op::INTEGER && is_foldable_division_(lhs->as_int(), rhs->as_int())) {
            int64_t dividend = lhs->as_int();
            int64_t divisor = rhs->as_int();
            int64_t remainder = dividend % divisor;
            // Python modulo: result has the same sign as the divisor; C truncation
            // gives the wrong sign when remainder and divisor disagree.  The
            // sum stays inside the type, because the two have opposite signs.
            if (remainder != 0 && ((remainder ^ divisor) < 0)) remainder = ::crucible::sat::add_sat(remainder, divisor);
            return integer(a, remainder);
        }
        if (lhs->is_zero_int() || lhs == rhs || rhs->is_one()) return integer(a, 0);
        if (rhs->op == Op::INTEGER && rhs->as_int() == 2) {
            if (lhs->is_even()) return integer(a, 0);
            if (lhs->is_odd()) return integer(a, 1);
        }
        const Expr* args[] = {lhs, rhs};
        uint16_t composite_flag_bits = ExprFlags::IS_INTEGER;
        return intern_node(a, Op::PYTHON_MOD, args, 2, composite_flag_bits, SymbolId{}, 0);
    }

    [[nodiscard]] const Expr* modular_indexing(effects::Alloc a, const Expr* base, const Expr* div,
                                               const Expr* modulus) {
        if (base->is_zero_int() || modulus->is_one()) return integer(a, 0);
        // Two divisions fold here, and each needs its own guard: the second
        // takes the quotient of the first, which can itself be the most
        // negative int64 against a modulus of minus one.
        if (base->op == Op::INTEGER && div->op == Op::INTEGER && modulus->op == Op::INTEGER
            && is_foldable_division_(base->as_int(), div->as_int())) {
            int64_t base_value = base->as_int();
            int64_t divisor_value = div->as_int();
            int64_t modulus_value = modulus->as_int();
            int64_t quotient = base_value / divisor_value;
            int64_t remainder = base_value % divisor_value;
            // Floor adjustment for negative dividend (matches Python //).
            if (remainder != 0 && ((remainder ^ divisor_value) < 0)) --quotient;
            if (is_foldable_division_(quotient, modulus_value)) {
                int64_t mod_result = quotient % modulus_value;
                // The two have opposite signs here, so the sum stays inside
                // the type.
                if (mod_result < 0) mod_result = ::crucible::sat::add_sat(mod_result, modulus_value);
                return integer(a, mod_result);
            }
        }
        // GCD on (base, divisor)
        if (!(div->op == Op::INTEGER && div->as_int() == 1)) {
            int64_t common_divisor = gcd_(integer_factor_(base), integer_factor_(div));
            if (common_divisor > 1)
                return modular_indexing(a, divide_coefficients_(a, base, common_divisor),
                                        divide_coefficients_(a, div, common_divisor), modulus);
        }
        // Drop ADD terms divisible by modulus*divisor
        if (base->op == Op::ADD && modulus->op == Op::INTEGER && div->op == Op::INTEGER) {
            // A saturated product is still positive, so the branch below
            // still admits it, and a coefficient can never be a multiple of a
            // saturated bound unless it is that bound.
            int64_t mod_div_product = ::crucible::sat::mul_sat(modulus->as_int(), div->as_int());
            if (mod_div_product > 0) {
                const Expr* kept_terms[kScratchArgs];
                std::size_t num_kept = 0;
                bool any_dropped = false;
                for (uint8_t i = 0; i < base->nargs; ++i) {
                    int64_t coeff = integer_coefficient_(base->arg(i));
                    // base is interned, so its child count is inside the
                    // ceiling the buffer is sized to.
                    CRUCIBLE_FATAL_INVARIANT(num_kept < kScratchArgs);
                    if (coeff != 0 && coeff % mod_div_product == 0)
                        any_dropped = true;
                    else
                        kept_terms[num_kept++] = base->arg(i);
                }
                if (any_dropped) {
                    if (num_kept == 0) return integer(a, 0);
                    const Expr* reduced_base =
                        (num_kept == 1) ? kept_terms[0] : add_n(a, std::span{kept_terms, num_kept});
                    return modular_indexing(a, reduced_base, div, modulus);
                }
            }
        }
        // FloorDiv as base: ModIdx(x//a, d, m) → ModIdx(x, a*d, m)
        if (base->op == Op::FLOOR_DIV || base->op == Op::CLEAN_DIV)
            return modular_indexing(a, base->arg(0), mul(a, base->arg(1), div), modulus);

        const Expr* args[] = {base, div, modulus};
        uint16_t composite_flag_bits = ExprFlags::IS_INTEGER | ExprFlags::IS_NONNEGATIVE;
        return intern_node(a, Op::MODULAR_INDEXING, args, 3, composite_flag_bits, SymbolId{}, 0);
    }

    // ---- Conditional ----

    [[nodiscard]] const Expr* where(effects::Alloc a, const Expr* cond, const Expr* then_branch,
                                    const Expr* else_branch) {
        if (cond == true_) return then_branch;
        if (cond == false_) return else_branch;
        if (then_branch == else_branch) return then_branch;
        const Expr* args[] = {cond, then_branch, else_branch};
        uint16_t intersected_flags = then_branch->flags & else_branch->flags;
        return intern_node(a, Op::WHERE, args, 3, intersected_flags, SymbolId{}, 0);
    }

    // ---- Min / Max ----

    [[nodiscard]] const Expr* min_expr(effects::Alloc a, const Expr* lhs, const Expr* rhs) {
        if (lhs == rhs) return lhs;
        if (lhs->op == Op::INTEGER && rhs->op == Op::INTEGER) return integer(a, std::min(lhs->as_int(), rhs->as_int()));
        const Expr* binary_args[] = {lhs, rhs};
        return min_n(a, binary_args);
    }

    [[nodiscard]] const Expr* max_expr(effects::Alloc a, const Expr* lhs, const Expr* rhs) {
        if (lhs == rhs) return lhs;
        if (lhs->op == Op::INTEGER && rhs->op == Op::INTEGER) return integer(a, std::max(lhs->as_int(), rhs->as_int()));
        const Expr* binary_args[] = {lhs, rhs};
        return max_n(a, binary_args);
    }

    // Dispatches to a canonical constructor where one exists, and interns
    // generically otherwise.  A two-element span routes to the binary helper
    // so the n-ary flatten, sort and coefficient-combining work is skipped
    // entirely by the binary early returns.
    //
    // gnu::flatten is deliberately not applied here.  It also inlines the
    // bulky n-ary bodies, which inflates the stack frame and the instruction
    // footprint enough to make the hit path several times slower.  Default
    // inlining already pulls in the small binary helpers.
    [[nodiscard]] PureInternedExpr make(effects::Alloc a, Op op, std::span<const Expr* const> args) {
        return PureInternedExpr{InternedExpr{make_raw_(a, op, args)}};
    }

private:
    // The one boundary every generically built node crosses.
    //
    // The arity band is checked before the dispatch below, because each arm
    // reads a fixed set of positions out of `args` and the variadic arms
    // copy the list into a scratch buffer sized from Expr::kMaxArgs.  A list
    // outside the band is a caller defect with no defined result, and
    // continuing past it reads or writes out of bounds, so the check holds in
    // every build mode rather than only where contracts are enforced.
    [[nodiscard]] const Expr* make_raw_(effects::Alloc a, Op op, std::span<const Expr* const> args) {
        const detail::ArityBand band = detail::op_arity_band(op);
        CRUCIBLE_FATAL_INVARIANT(args.size() >= static_cast<std::size_t>(band.min));
        CRUCIBLE_FATAL_INVARIANT(args.size() <= static_cast<std::size_t>(band.max));

        switch (op) {
            case Op::ADD:
                if (args.size() == 2) [[likely]]
                    return add(a, args[0], args[1]);
                return add_n(a, args);
            case Op::MUL:
                if (args.size() == 2) [[likely]]
                    return mul(a, args[0], args[1]);
                return mul_n(a, args);
            case Op::AND:
                if (args.size() == 2) [[likely]]
                    return and_(a, args[0], args[1]);
                return and_n(a, args);
            case Op::OR:
                if (args.size() == 2) [[likely]]
                    return or_(a, args[0], args[1]);
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
                if (args.size() == 2) [[likely]]
                    return min_expr(a, args[0], args[1]);
                return min_n(a, args);
            case Op::MAX:
                if (args.size() == 2) [[likely]]
                    return max_expr(a, args[0], args[1]);
                return max_n(a, args);

            // Atoms carry a payload rather than child args, so the args
            // span would be silently dropped.  They must be built through
            // their dedicated constructors instead.
            case Op::INTEGER:
            case Op::FLOAT:
            case Op::SYMBOL:
            case Op::BOOL_TRUE:
            case Op::BOOL_FALSE:
                std::unreachable();

            // The enum sentinel, never a real op.
            case Op::NUM_OPS:
                std::unreachable();

            // These have no canonical simplifier and share the generic
            // intern path.  They are listed rather than folded into the
            // default arm so a newly added op surfaces here under -Wswitch
            // instead of silently taking this route.
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
                break;

            // Required by -Wswitch-default even though every enumerator is
            // handled.  Reaching it means the op was read from out-of-range
            // memory.
            default:
                std::unreachable();
        }
        uint16_t f = detail::composite_flags(op, args.data(), static_cast<uint8_t>(args.size()));
        return intern_node(a, op, args.data(), static_cast<uint8_t>(args.size()), f, SymbolId{}, 0);
    }

public:
    // ---- Stats ----

    [[nodiscard]] size_t intern_size() const { return intern_count_.get(); }
    [[nodiscard]] size_t intern_capacity() const { return capacity_.value(); }
    [[nodiscard]] size_t arena_bytes() const { return arena_.total_allocated(); }
    [[nodiscard]] const char* symbol_name(SymbolId id) const CRUCIBLE_LIFETIMEBOUND {
        return (id.raw() < symbol_names_.size()) ? symbol_names_[id.raw()] : nullptr;
    }

private:
    // The cache bounds are inclusive on both ends, so the range size is a
    // power of two only while both bounds move together.  Shifting one by an
    // odd offset breaks it and loosens the bounds on the direct-index
    // lookup.
    static_assert(::crucible::decide::is_power_of_two_le<std::size_t>(kIntCacheSize, std::size_t{1024}),
                  "kIntCacheSize must be a power of two ≤ 1024");

    // Every n-ary constructor collects its operands in a stack buffer of this
    // many entries.  The size comes from the IR ceiling rather than a round
    // number, so a buffer that is full is exactly a node that cannot be
    // named: the next operand would need an nargs of 256.
    //
    // A flatten that would pass the ceiling keeps the child node whole
    // instead.  Flattening is a canonicalization, so min(min(a, b), c) and
    // min(a, b, c) name the same value and the fallback costs one level of
    // nesting, not correctness.
    static constexpr std::size_t kScratchArgs = Expr::kMaxArgs;

    // add_n and mul_n reattach the folded constant after collecting their
    // terms, so their collection buffer holds one more entry than the
    // ceiling.  The extra slot is never interned; the reattach path nests
    // instead when the terms alone already fill the ceiling.
    static constexpr std::size_t kScratchCollect = kScratchArgs + 1;

    // C++ leaves the division and the remainder of the most negative int64
    // by minus one undefined, because the quotient is one past the top of
    // the type.  A fold that meets the pair declines to fold, which leaves
    // the expression symbolic instead of producing a target-dependent value.
    [[nodiscard, gnu::const]] static constexpr bool is_foldable_division_(int64_t dividend, int64_t divisor) noexcept {
        return divisor != 0 && !(dividend == std::numeric_limits<int64_t>::min() && divisor == -1);
    }

    // Rounds up to a power-of-two capacity holding at least one control
    // group.  The constructor precondition caps the input at 1 << 30, so the
    // shift cannot overflow for an admitted caller.
    [[nodiscard, gnu::const]] static constexpr size_t rounded_capacity_(size_t initial_capacity) noexcept {
        size_t cap = detail::group_width();
        while (cap < initial_capacity)
            cap <<= 1;
        return cap;
    }

    [[nodiscard, gnu::const]] static constexpr IntCacheIndex int_cache_index(IntCacheLiteral literal) noexcept {
        return IntCacheIndex{static_cast<size_t>(literal.value() - kIntCacheLow)};
    }

    [[nodiscard, gnu::const]] static constexpr size_t raw_int_cache_index(IntCacheIndex index) noexcept {
        return index.value();
    }

    [[nodiscard]] const Expr* cached_integer(IntCacheLiteral literal) const noexcept {
        return int_cache_[raw_int_cache_index(int_cache_index(literal))];
    }

    const Expr* make_integer(effects::Alloc a, int64_t val) {
        return intern_node(a, Op::INTEGER, nullptr, 0, detail::integer_flags(val), SymbolId{}, val);
    }

    [[nodiscard]] static int64_t gcd_(int64_t a, int64_t b) {
        // Every caller already routes its operands through safe_abs_, so the
        // most negative int64 does not arrive here.  Taking the absolute
        // value through the same clamp rather than a bare negation makes that
        // a property of this function instead of a property of its callers.
        a = safe_abs_(a);
        b = safe_abs_(b);
        while (b) {
            int64_t t = b;
            b = a % b;
            a = t;
        }
        return a;
    }

    // MUL(3, x, y) → 3, INTEGER(5) → 5, x → 1.
    [[nodiscard, gnu::pure]] static int64_t integer_coefficient_(const Expr* expr) {
        if (expr->op == Op::INTEGER) return expr->as_int();
        if (expr->op == Op::MUL) {
            for (uint8_t i = 0; i < expr->nargs; ++i)
                if (expr->args[i]->op == Op::INTEGER) return expr->args[i]->as_int();
        }
        return 1;
    }

    // Negating INT64_MIN is undefined, because 2^63 does not fit in an
    // int64_t, and a corrupt or adversarial ADD arm can carry that
    // coefficient.  Clamping it to INT64_MAX instead is off by one, which
    // cannot change the resulting GCD: every other coefficient is already at
    // most INT64_MAX.
    [[nodiscard]] static constexpr int64_t safe_abs_(int64_t value) noexcept {
        if (value == std::numeric_limits<int64_t>::min()) return std::numeric_limits<int64_t>::max();
        return (value < 0) ? -value : value;
    }

    [[nodiscard, gnu::pure]] int64_t integer_factor_(const Expr* expr) const {
        if (expr->op == Op::ADD) {
            int64_t accumulated_gcd = 0;
            for (uint8_t i = 0; i < expr->nargs; ++i) {
                int64_t abs_coeff = safe_abs_(integer_coefficient_(expr->args[i]));
                accumulated_gcd = (accumulated_gcd == 0) ? abs_coeff : gcd_(accumulated_gcd, abs_coeff);
            }
            return (accumulated_gcd == 0) ? 1 : accumulated_gcd;
        }
        return safe_abs_(integer_coefficient_(expr));
    }

    // Divide all integer coefficients in expression by `divisor`.
    const Expr* divide_coefficients_(effects::Alloc a, const Expr* expr, int64_t divisor) {
        if (divisor <= 1) return expr;
        if (expr->op == Op::INTEGER) return integer(a, expr->as_int() / divisor);
        if (expr->op == Op::MUL) {
            for (uint8_t i = 0; i < expr->nargs; ++i) {
                if (expr->args[i]->op == Op::INTEGER) {
                    int64_t new_coeff = expr->args[i]->as_int() / divisor;
                    // A binary MUL whose coefficient collapsed to one is just
                    // its other factor.
                    if (new_coeff == 1 && expr->nargs == 2) return expr->args[1 - i];
                    const Expr* rebuilt_factors[kScratchArgs];
                    std::size_t num_rebuilt = 0;
                    for (uint8_t j = 0; j < expr->nargs; ++j) {
                        CRUCIBLE_FATAL_INVARIANT(num_rebuilt < kScratchArgs);
                        rebuilt_factors[num_rebuilt++] = (j == i) ? integer(a, new_coeff) : expr->args[j];
                    }
                    return mul_n(a, std::span{rebuilt_factors, num_rebuilt});
                }
            }
            return expr;
        }
        if (expr->op == Op::ADD) {
            const Expr* divided_terms[kScratchArgs];
            CRUCIBLE_FATAL_INVARIANT(expr->nargs <= kScratchArgs);
            for (uint8_t i = 0; i < expr->nargs; ++i)
                divided_terms[i] = divide_coefficients_(a, expr->args[i], divisor);
            return add_n(a, std::span{divided_terms, expr->nargs});
        }
        return expr;
    }

    // MIN and MAX have no identity element, so an empty operand list names no
    // value and the dedup pass below would return a slot that was never
    // written.  make_raw_ rejects the empty list at the boundary; the check
    // here is the second of the two and holds in every build mode, because
    // the read it guards is the return value.
    const Expr* min_n(effects::Alloc a, std::span<const Expr* const> inputs) {
        CRUCIBLE_FATAL_INVARIANT(!inputs.empty());
        CRUCIBLE_FATAL_INVARIANT(inputs.size() <= kScratchArgs);

        const Expr* scratch_buf[kScratchArgs];
        std::size_t num_args = 0;
        for (std::size_t k = 0; k < inputs.size(); ++k) {
            const Expr* input_expr = inputs[k];
            // What the inputs after this one need at a minimum, one slot
            // each.  Flattening is declined when it would eat that room,
            // which keeps the buffer bound an inequality over the whole loop
            // rather than a test at each write.
            const std::size_t reserved = inputs.size() - k - 1;
            if (input_expr->op == Op::MIN && num_args + input_expr->nargs + reserved <= kScratchArgs) {
                for (uint8_t i = 0; i < input_expr->nargs; ++i)
                    scratch_buf[num_args++] = input_expr->arg(i);
            } else {
                CRUCIBLE_FATAL_INVARIANT(num_args + reserved < kScratchArgs);
                scratch_buf[num_args++] = input_expr;
            }
        }
        std::ranges::sort(std::span{scratch_buf, num_args});
        std::size_t num_unique = 1;
        for (std::size_t i = 1; i < num_args; ++i)
            if (scratch_buf[i] != scratch_buf[num_unique - 1]) scratch_buf[num_unique++] = scratch_buf[i];
        if (num_unique == 1) return scratch_buf[0];
        CRUCIBLE_FATAL_INVARIANT(num_unique <= kScratchArgs);
        const auto arg_count = static_cast<uint8_t>(num_unique);
        uint16_t composite_flag_bits = detail::composite_flags(Op::MIN, scratch_buf, arg_count);
        return intern_node(a, Op::MIN, scratch_buf, arg_count, composite_flag_bits, SymbolId{}, 0);
    }

    const Expr* max_n(effects::Alloc a, std::span<const Expr* const> inputs) {
        CRUCIBLE_FATAL_INVARIANT(!inputs.empty());
        CRUCIBLE_FATAL_INVARIANT(inputs.size() <= kScratchArgs);

        const Expr* scratch_buf[kScratchArgs];
        std::size_t num_args = 0;
        for (std::size_t k = 0; k < inputs.size(); ++k) {
            const Expr* input_expr = inputs[k];
            const std::size_t reserved = inputs.size() - k - 1;
            if (input_expr->op == Op::MAX && num_args + input_expr->nargs + reserved <= kScratchArgs) {
                for (uint8_t i = 0; i < input_expr->nargs; ++i)
                    scratch_buf[num_args++] = input_expr->arg(i);
            } else {
                CRUCIBLE_FATAL_INVARIANT(num_args + reserved < kScratchArgs);
                scratch_buf[num_args++] = input_expr;
            }
        }
        std::ranges::sort(std::span{scratch_buf, num_args});
        std::size_t num_unique = 1;
        for (std::size_t i = 1; i < num_args; ++i)
            if (scratch_buf[i] != scratch_buf[num_unique - 1]) scratch_buf[num_unique++] = scratch_buf[i];
        if (num_unique == 1) return scratch_buf[0];
        CRUCIBLE_FATAL_INVARIANT(num_unique <= kScratchArgs);
        const auto arg_count = static_cast<uint8_t>(num_unique);
        uint16_t composite_flag_bits = detail::composite_flags(Op::MAX, scratch_buf, arg_count);
        return intern_node(a, Op::MAX, scratch_buf, arg_count, composite_flag_bits, SymbolId{}, 0);
    }

    // Flattens nested ADD, folds integer constants, combines like terms,
    // sorts and interns.  Term combining is what keeps expansion tractable:
    // (a+b)^n yields n+1 binomial terms instead of 2^n unmerged products.
    const Expr* add_n(effects::Alloc a, std::span<const Expr* const> inputs) {
        CRUCIBLE_FATAL_INVARIANT(inputs.size() <= kScratchArgs);

        const Expr* term_scratch_buf[kScratchArgs];
        std::size_t num_args = 0;
        int64_t int_sum = 0;

        for (std::size_t k = 0; k < inputs.size(); ++k) {
            const Expr* arg_expr = inputs[k];
            // An over-estimate, because an integer input folds into the sum
            // and takes no slot.
            const std::size_t reserved = inputs.size() - k - 1;
            if (arg_expr->op == Op::ADD && num_args + arg_expr->nargs + reserved <= kScratchArgs) {
                for (uint8_t i = 0; i < arg_expr->nargs; ++i) {
                    if (arg_expr->args[i]->op == Op::INTEGER)
                        int_sum = ::crucible::sat::add_sat(int_sum, arg_expr->args[i]->payload);
                    else {
                        CRUCIBLE_FATAL_INVARIANT(num_args < kScratchArgs);
                        term_scratch_buf[num_args++] = arg_expr->args[i];
                    }
                }
            } else if (arg_expr->op == Op::INTEGER) {
                int_sum = ::crucible::sat::add_sat(int_sum, arg_expr->payload);
            } else {
                CRUCIBLE_FATAL_INVARIANT(num_args + reserved < kScratchArgs);
                term_scratch_buf[num_args++] = arg_expr;
            }
        }

        if (num_args == 0) return integer(a, int_sum);

        // Decompose each term into a coefficient and a base:
        //   MUL(3, a, b) → coeff 3, base MUL(a, b)
        //   MUL(a, b)    → coeff 1, base MUL(a, b)
        //   a            → coeff 1, base a
        // The base is the coefficient-free interned form, so the first two
        // share one.  Two terms with the same base sum their coefficients,
        // which is how a + 2a becomes 3a.
        struct CoeffTerm {
            int64_t coeff;
            const Expr* base;
        };
        CoeffTerm decomposed_terms[kScratchArgs];
        std::size_t num_decomposed = 0;

        for (std::size_t j = 0; j < num_args; ++j) {
            int64_t combined_coefficient = 1;
            const Expr* base = term_scratch_buf[j];

            if (term_scratch_buf[j]->op == Op::MUL) {
                const Expr* mul_factors[kScratchArgs];
                std::size_t num_factors = 0;
                for (uint8_t k = 0; k < term_scratch_buf[j]->nargs; ++k) {
                    if (term_scratch_buf[j]->args[k]->op == Op::INTEGER)
                        combined_coefficient = term_scratch_buf[j]->args[k]->payload;
                    else {
                        // An interned node carries at most Expr::kMaxArgs
                        // children, so its factors always fit.  The check is
                        // what makes that a guarantee rather than a belief.
                        CRUCIBLE_FATAL_INVARIANT(num_factors < kScratchArgs);
                        mul_factors[num_factors++] = term_scratch_buf[j]->args[k];
                    }
                }
                if (num_factors == 0) {
                    // A MUL of integers only.  The flattening above should
                    // have folded it already.
                    int_sum = ::crucible::sat::add_sat(int_sum, combined_coefficient);
                    continue;
                } else if (num_factors == 1) {
                    base = mul_factors[0];
                } else {
                    // The coefficient-free MUL is the grouping key.  Its
                    // factors came out of a canonical MUL, so they are
                    // already sorted.
                    const auto factor_count = static_cast<uint8_t>(num_factors);
                    uint16_t composite_flag_bits = detail::composite_flags(Op::MUL, mul_factors, factor_count);
                    base = intern_node(a, Op::MUL, mul_factors, factor_count, composite_flag_bits, SymbolId{}, 0);
                }
            }
            CRUCIBLE_FATAL_INVARIANT(num_decomposed < kScratchArgs);
            decomposed_terms[num_decomposed++] = {.coeff = combined_coefficient, .base = base};
        }

        // Sorting by base brings equal bases adjacent, so one linear pass
        // merges them.
        std::ranges::sort(std::span{decomposed_terms, num_decomposed},
                          [](const CoeffTerm& lhs, const CoeffTerm& rhs) { return lhs.base < rhs.base; });

        const Expr* collected_terms[kScratchCollect];
        std::size_t num_collected = 0;
        std::size_t i = 0;
        while (i < num_decomposed) {
            int64_t total_coeff = decomposed_terms[i].coeff;
            const Expr* base = decomposed_terms[i].base;
            std::size_t j = i + 1;
            while (j < num_decomposed && decomposed_terms[j].base == base) {
                total_coeff = ::crucible::sat::add_sat(total_coeff, decomposed_terms[j].coeff);
                ++j;
            }

            if (total_coeff == 0) {
                // The terms cancelled, as in a + (-a).
            } else if (total_coeff == 1) {
                CRUCIBLE_FATAL_INVARIANT(num_collected < kScratchCollect);
                collected_terms[num_collected++] = base;
            } else {
                const Expr* mul_args[] = {integer(a, total_coeff), base};
                CRUCIBLE_FATAL_INVARIANT(num_collected < kScratchCollect);
                collected_terms[num_collected++] = mul_n(a, mul_args);
            }
            i = j;
        }

        // Reattach the integer sum, omitting a zero unless it is the only
        // term left.
        if (int_sum != 0 || num_collected == 0) {
            if (num_collected == kScratchArgs) [[unlikely]] {
                // The terms alone already fill the ceiling, so there is no
                // slot left to name the constant as a sibling.  One level of
                // nesting names the same value, ADD(ADD(t...), c), and both
                // of its nodes are inside the ceiling.
                //
                // The outer node is built here rather than through add(),
                // which would flatten the inner one straight back into this
                // same state.
                std::ranges::sort(std::span{collected_terms, num_collected});
                return nest_folded_constant_(a, Op::ADD, collected_terms, int_sum);
            }
            CRUCIBLE_FATAL_INVARIANT(num_collected < kScratchCollect);
            collected_terms[num_collected++] = integer(a, int_sum);
        }
        if (num_collected == 1) return collected_terms[0];

        std::ranges::sort(std::span{collected_terms, num_collected});
        CRUCIBLE_FATAL_INVARIANT(num_collected <= kScratchArgs);
        const auto arg_count = static_cast<uint8_t>(num_collected);
        uint16_t composite_flag_bits = detail::composite_flags(Op::ADD, collected_terms, arg_count);
        return intern_node(a, Op::ADD, collected_terms, arg_count, composite_flag_bits, SymbolId{}, 0);
    }

    // Interns a full-width operand list and pairs it with the folded constant
    // one level up.  Used by add_n and mul_n when the terms alone reach the
    // arity ceiling, which leaves the constant no sibling slot.
    const Expr* nest_folded_constant_(effects::Alloc a, Op op, const Expr* const* terms, int64_t folded_constant) {
        const uint16_t inner_flags = detail::composite_flags(op, terms, Expr::kMaxArgs);
        const Expr* inner = intern_node(a, op, terms, Expr::kMaxArgs, inner_flags, SymbolId{}, 0);
        const Expr* constant = integer(a, folded_constant);
        // Address order, the same canonical ordering the binary builders use.
        const Expr* outer_args[] = {inner, constant};
        if (outer_args[0] > outer_args[1]) std::swap(outer_args[0], outer_args[1]);
        const uint16_t outer_flags = detail::composite_flags(op, outer_args, 2);
        return intern_node(a, op, outer_args, 2, outer_flags, SymbolId{}, 0);
    }

    const Expr* mul_n(effects::Alloc a, std::span<const Expr* const> inputs) {
        CRUCIBLE_FATAL_INVARIANT(inputs.size() <= kScratchArgs);

        const Expr* factor_scratch_buf[kScratchCollect];
        std::size_t num_args = 0;
        int64_t int_prod = 1;

        for (std::size_t k = 0; k < inputs.size(); ++k) {
            const Expr* arg_expr = inputs[k];
            const std::size_t reserved = inputs.size() - k - 1;
            if (arg_expr->op == Op::MUL && num_args + arg_expr->nargs + reserved <= kScratchArgs) {
                for (uint8_t i = 0; i < arg_expr->nargs; ++i) {
                    if (arg_expr->args[i]->op == Op::INTEGER)
                        int_prod = ::crucible::sat::mul_sat(int_prod, arg_expr->args[i]->payload);
                    else {
                        CRUCIBLE_FATAL_INVARIANT(num_args < kScratchArgs);
                        factor_scratch_buf[num_args++] = arg_expr->args[i];
                    }
                }
            } else if (arg_expr->op == Op::INTEGER) {
                int_prod = ::crucible::sat::mul_sat(int_prod, arg_expr->payload);
            } else {
                CRUCIBLE_FATAL_INVARIANT(num_args + reserved < kScratchArgs);
                factor_scratch_buf[num_args++] = arg_expr;
            }
        }

        if (int_prod == 0) return integer(a, 0);
        // Reattach the integer product, omitting a one unless it is the only
        // factor left.
        if (int_prod != 1 || num_args == 0) {
            if (num_args == kScratchArgs) [[unlikely]] {
                // As in add_n: the factors alone fill the ceiling, so the
                // constant gets a level of its own.
                std::ranges::sort(std::span{factor_scratch_buf, num_args});
                return nest_folded_constant_(a, Op::MUL, factor_scratch_buf, int_prod);
            }
            CRUCIBLE_FATAL_INVARIANT(num_args < kScratchCollect);
            factor_scratch_buf[num_args++] = integer(a, int_prod);
        }
        if (num_args == 1) return factor_scratch_buf[0];

        std::ranges::sort(std::span{factor_scratch_buf, num_args});
        CRUCIBLE_FATAL_INVARIANT(num_args <= kScratchArgs);
        const auto arg_count = static_cast<uint8_t>(num_args);
        uint16_t composite_flag_bits = detail::composite_flags(Op::MUL, factor_scratch_buf, arg_count);
        return intern_node(a, Op::MUL, factor_scratch_buf, arg_count, composite_flag_bits, SymbolId{}, 0);
    }

    // Keeping an AND child whole when its operands do not fit drops no
    // short-circuit opportunity.  A constant operand never survives into an
    // interned AND: the loop below returns the false singleton on one and
    // skips the true one, so a child that is already interned holds neither.
    const Expr* and_n(effects::Alloc a, std::span<const Expr* const> inputs) {
        CRUCIBLE_FATAL_INVARIANT(inputs.size() <= kScratchArgs);

        const Expr* operand_scratch_buf[kScratchArgs];
        std::size_t num_operands = 0;

        for (std::size_t k = 0; k < inputs.size(); ++k) {
            const Expr* input_expr = inputs[k];
            if (input_expr == false_) return false_;
            if (input_expr == true_) continue;
            // An over-estimate, because a constant operand takes no slot.
            // Declining a flatten one step early costs a level of nesting,
            // never correctness.
            const std::size_t reserved = inputs.size() - k - 1;
            if (input_expr->op == Op::AND && num_operands + input_expr->nargs + reserved <= kScratchArgs) {
                for (uint8_t i = 0; i < input_expr->nargs; ++i) {
                    if (input_expr->args[i] == false_) return false_;
                    if (input_expr->args[i] == true_) continue;
                    CRUCIBLE_FATAL_INVARIANT(num_operands < kScratchArgs);
                    operand_scratch_buf[num_operands++] = input_expr->args[i];
                }
            } else {
                CRUCIBLE_FATAL_INVARIANT(num_operands + reserved < kScratchArgs);
                operand_scratch_buf[num_operands++] = input_expr;
            }
        }

        if (num_operands == 0) return true_;
        if (num_operands == 1) return operand_scratch_buf[0];
        std::ranges::sort(std::span{operand_scratch_buf, num_operands});
        CRUCIBLE_FATAL_INVARIANT(num_operands <= kScratchArgs);
        const auto arg_count = static_cast<uint8_t>(num_operands);
        return intern_node(a, Op::AND, operand_scratch_buf, arg_count, ExprFlags::IS_BOOLEAN, SymbolId{}, 0);
    }

    const Expr* or_n(effects::Alloc a, std::span<const Expr* const> inputs) {
        CRUCIBLE_FATAL_INVARIANT(inputs.size() <= kScratchArgs);

        const Expr* operand_scratch_buf[kScratchArgs];
        std::size_t num_operands = 0;

        for (std::size_t k = 0; k < inputs.size(); ++k) {
            const Expr* input_expr = inputs[k];
            if (input_expr == true_) return true_;
            if (input_expr == false_) continue;
            const std::size_t reserved = inputs.size() - k - 1;
            if (input_expr->op == Op::OR && num_operands + input_expr->nargs + reserved <= kScratchArgs) {
                for (uint8_t i = 0; i < input_expr->nargs; ++i) {
                    if (input_expr->args[i] == true_) return true_;
                    if (input_expr->args[i] == false_) continue;
                    CRUCIBLE_FATAL_INVARIANT(num_operands < kScratchArgs);
                    operand_scratch_buf[num_operands++] = input_expr->args[i];
                }
            } else {
                CRUCIBLE_FATAL_INVARIANT(num_operands + reserved < kScratchArgs);
                operand_scratch_buf[num_operands++] = input_expr;
            }
        }

        if (num_operands == 0) return false_;
        if (num_operands == 1) return operand_scratch_buf[0];
        std::ranges::sort(std::span{operand_scratch_buf, num_operands});
        CRUCIBLE_FATAL_INVARIANT(num_operands <= kScratchArgs);
        const auto arg_count = static_cast<uint8_t>(num_operands);
        return intern_node(a, Op::OR, operand_scratch_buf, arg_count, ExprFlags::IS_BOOLEAN, SymbolId{}, 0);
    }

    // Probes the table and inserts on miss, returning the interned node
    // either way.
    //
    // The probe compares a whole group of control bytes at once and iterates
    // only the tag matches.  The table is insert-only and has no tombstones,
    // so stopping at the first empty slot in a group is sound: an entry that
    // hashed here would have been placed before that empty slot.
    //
    // The full-hash compare is the real filter.  The hash already encodes op,
    // nargs, flags, symbol_id and payload, so re-checking them on a hash
    // match is redundant.  They are checked anyway, packed into one word so
    // the check is a single comparison.
    CRUCIBLE_UNSAFE_BUFFER_USAGE CRUCIBLE_INLINE const Expr* intern_node(effects::Alloc a, Op op,
                                                                         const Expr* const* args, uint8_t nargs,
                                                                         uint16_t flags, SymbolId symbol_id,
                                                                         int64_t payload) {
        // The load factor is 7/8.  Group-at-a-time probing tolerates a
        // denser table than linear probing does.
        if (intern_count_.get() * 8 >= capacity_.value() * 7) [[unlikely]]
            rehash();

        // Read after the rehash, never before.  A rehash replaces ctrl_ and
        // slots_ with tables of twice the capacity and re-homes every entry
        // across the whole of the new range.  A slot mask still derived from
        // the old capacity confines the probe below to the lower half, so a
        // node that moved into the upper half is not found and is interned a
        // second time.  Two nodes then describe one structure, and pointer
        // equality, which is what every Expr comparison rests on, is broken.
        const size_t cap = capacity_.value();

        uint64_t expr_full_hash = detail::expr_hash(op, payload, symbol_id, flags, args, nargs);
        int8_t slot_match_tag = detail::h2_tag(expr_full_hash);

        // Same packing expr_hash uses, so one comparison replaces four
        // branches.
        uint64_t query_packed_metadata = static_cast<uint64_t>(std::to_underlying(op))
                                       | (static_cast<uint64_t>(nargs) << 8) | (static_cast<uint64_t>(flags) << 16)
                                       | (static_cast<uint64_t>(symbol_id.raw()) << 32);

        // Working in slot indices rather than group indices removes a
        // multiply from every probe iteration.
        size_t slot_mask = cap - 1;
        size_t probe_base_slot = (expr_full_hash * detail::group_width()) & slot_mask;
        size_t probe_iteration = 0;

        while (true) {
            // The probe base stays group-aligned, because the hash is scaled
            // by the group width before the mask and the step below is a
            // group multiple.  One check therefore covers the group load, the
            // slot reads inside the match loop and the slot write on the
            // insert path, since every index derived from the base adds less
            // than one group width to it.
            //
            // This holds in every build mode.  A base outside the range means
            // the mask disagrees with the live table, and each of the three
            // accesses would then run off the end of the allocation.
            CRUCIBLE_FATAL_INVARIANT(probe_base_slot + detail::group_width() <= cap);

            auto group = detail::CtrlGroup::load(&ctrl_[probe_base_slot]);

            auto matches = group.match(slot_match_tag);
            while (matches) {
                size_t match_slot_index = probe_base_slot + matches.lowest();
                const Expr* existing_expr = slots_[match_slot_index];
                // The packed-metadata compare catches the case where two
                // different field sets hash to the same 64-bit value.
                if (existing_expr->hash.value() == expr_full_hash && existing_expr->payload == payload) [[likely]] {
                    // Pack the existing expr's metadata the same way for single compare
                    uint64_t existing_packed_metadata = static_cast<uint64_t>(std::to_underlying(existing_expr->op))
                                                      | (static_cast<uint64_t>(existing_expr->nargs) << 8)
                                                      | (static_cast<uint64_t>(existing_expr->flags) << 16)
                                                      | (static_cast<uint64_t>(existing_expr->symbol_id.raw()) << 32);
                    if (existing_packed_metadata == query_packed_metadata) [[likely]] {
                        switch (nargs) {
                            case 0:
                                return existing_expr;
                            case 1:
                                if (existing_expr->args[0] == args[0]) return existing_expr;
                                break;
                            case 2:
                                if (existing_expr->args[0] == args[0] && existing_expr->args[1] == args[1])
                                    return existing_expr;
                                break;
                            case 3:
                                if (existing_expr->args[0] == args[0] && existing_expr->args[1] == args[1]
                                    && existing_expr->args[2] == args[2])
                                    return existing_expr;
                                break;
                            default: {
                                bool all_args_match = true;
                                for (uint8_t i = 0; i < nargs; ++i) {
                                    if (existing_expr->args[i] != args[i]) {
                                        all_args_match = false;
                                        break;
                                    }
                                }
                                if (all_args_match) return existing_expr;
                                break;
                            }
                        }
                    }
                }
                matches.clear_lowest();
            }

            // An empty slot anywhere in this group proves the entry is not
            // in the table.
            auto empties = group.match_empty();
            if (empties) [[likely]] {
                size_t match_slot_index = probe_base_slot + empties.lowest();

                // The args must be copied into the arena before the Expr is
                // constructed: the Expr's args pointer is const and can only
                // be set through the constructor.  A node with no args gets
                // a null pointer.
                const Expr** arena_owned_args = nullptr;
                if (nargs > 0) {
                    arena_owned_args = arena_.alloc_array<const Expr*>(a, nargs);
                    std::memcpy(arena_owned_args, args, nargs * sizeof(const Expr*));
                }

                // Placement-new into arena storage, because the const fields
                // of Expr can only be initialized in place.
                void* arena_expr_storage = arena_.alloc_obj<Expr>(a);
                Expr* interned_expr = ::new(arena_expr_storage)
                    Expr(op, nargs, flags, symbol_id, expr_full_hash, payload, arena_owned_args);

                ctrl_[match_slot_index] = slot_match_tag;
                slots_[match_slot_index] = interned_expr;
                intern_count_.bump();
                return interned_expr;
            }

            // Triangular probing visits every group before it repeats.  The
            // step sequence is +G, +3G, +6G and so on.
            ++probe_iteration;
            probe_base_slot = (probe_base_slot + probe_iteration * detail::group_width()) & slot_mask;

            // The prefetch is issued only here, after the decision to
            // iterate, so the dominant first-probe hit never pays for it.
            //
            // The locality hint is zero because the table is far larger than
            // the cache and retaining a speculatively probed group would
            // evict something more useful.
            __builtin_prefetch(&ctrl_[probe_base_slot], 0, 0);
        }
    }

    // Both arrays live in one contiguous buffer laid out as
    //   [ctrl_: `cap` bytes] [slots_: `cap * 8` bytes]
    // The slot array starts at offset `cap`, which is always a multiple of
    // the group width and therefore at least 16, so the pointer array is
    // 8-byte aligned without any padding.
    //
    // ctrl_ and slots_ stay as raw projections so each probe reads them with
    // a single load and no indirection through the owning buffer.
    void alloc_tables_(size_t cap) {
        const size_t slot_bytes = cap * sizeof(const Expr*);
        backing_ = ::crucible::fixy::wrap::SwissTableBuffer<const Expr*>::allocate(cap);
        ctrl_ = backing_.ctrl();
        slots_ = backing_.slots();
        std::memset(ctrl_, 0x80, cap);  // 0x80 is the empty control byte.
        std::memset(slots_, 0, slot_bytes);
    }

    CRUCIBLE_UNSAFE_BUFFER_USAGE void rehash() { grow_to_(capacity_.value() * 2); }

    // Allocates fresh tables at `new_capacity`, re-inserts every live entry
    // at its new home and frees the old buffer.
    //
    // Both preconditions are load-bearing for the probe.  It masks with
    // capacity minus one, which a non-power-of-two capacity corrupts, and it
    // loads a whole control group at a time, which walks off the end of a
    // buffer narrower than one group.
    CRUCIBLE_UNSAFE_BUFFER_USAGE void grow_to_(size_t new_capacity)
        pre(::crucible::decide::is_power_of_two_le<std::size_t>(new_capacity, std::size_t{1} << 30))
            pre(new_capacity >= detail::group_width()) {
        size_t old_capacity = capacity_.value();
        size_t old_count = intern_count_.get();
        // The local keeps the old buffer alive for the re-insert walk below
        // and frees it when this function returns.
        auto old_backing = std::move(backing_);
        int8_t* old_ctrl = old_backing.ctrl();
        const Expr** old_slots = old_backing.slots();

        capacity_ = Capacity{new_capacity};
        alloc_tables_(capacity_.value());

        size_t slot_mask = capacity_.value() - 1;
        size_t reinserted = 0;

        for (size_t i = 0; i < old_capacity; ++i) {
            if (old_ctrl[i] == detail::kEmpty) continue;

            const Expr* existing_expr = old_slots[i];
            const std::uint64_t existing_hash_raw = existing_expr->hash.value();
            int8_t slot_match_tag = detail::h2_tag(existing_hash_raw);
            size_t probe_base_slot = (existing_hash_raw * detail::group_width()) & slot_mask;
            size_t probe_iteration = 0;

            while (true) {
                // The same group-alignment bound the insert path carries.
                CRUCIBLE_FATAL_INVARIANT(probe_base_slot + detail::group_width() <= capacity_.value());

                auto group = detail::CtrlGroup::load(&ctrl_[probe_base_slot]);
                auto empties = group.match_empty();
                if (empties) {
                    size_t insert_slot_index = probe_base_slot + empties.lowest();
                    ctrl_[insert_slot_index] = slot_match_tag;
                    slots_[insert_slot_index] = existing_expr;
                    ++reinserted;
                    break;
                }
                ++probe_iteration;
                probe_base_slot = (probe_base_slot + probe_iteration * detail::group_width()) & slot_mask;
            }
        }
        CRUCIBLE_POST(0, reinserted == old_count);
        intern_count_.advance(reinserted);
    }

    Arena arena_;
    ::crucible::fixy::wrap::SwissTableBuffer<const Expr*> backing_;
    int8_t* ctrl_;  // Points into backing_ at offset 0.
    const Expr** slots_;  // Points into backing_ at offset capacity_.
    Capacity capacity_;  // Total slots, a power of two and a group multiple.
    InternCount intern_count_;  // Occupied slots.
    std::vector<const char*> symbol_names_;

    // Runs parallel to symbol_names_.  Once symbol() has registered a
    // SymbolId, that id alone identifies the interned Expr, so a lookup by
    // id needs no table probe at all.
    std::vector<const Expr*> symbol_exprs_;

    std::array<const Expr*, kIntCacheSize> int_cache_{};
    const Expr* true_;
    const Expr* false_;
};

}  // namespace crucible
