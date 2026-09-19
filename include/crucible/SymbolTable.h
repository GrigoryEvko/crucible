#pragma once

// Per-symbol metadata, held in a vector indexed by the same identifier an
// expression node carries. It holds everything a range-based rewrite needs,
// so the rewrite never has to ask the frontend.

#include <crucible/Ops.h>
#include <crucible/Platform.h>
#include <crucible/Types.h>
#include <crucible/fixy/Source.h>
#include <crucible/fixy/Wrap.h>
#include <crucible/safety/_Decide.h>
#include <crucible/safety/_Pre.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace crucible {

// Where a symbol came from, which fixes its default assumptions.
enum class SymKind : uint8_t {
    SIZE,  // an integer, ordinarily at least two
    FLOAT,
    UNBACKED_INT,  // an integer with no concrete value behind it
    UNBACKED_FLOAT,
};

enum class SymFlags : std::uint8_t {
    IS_SIZE_LIKE = 1 << 0,  // may be assumed at least two where sizes are treated abstractly
    HAS_HINT = 1 << 1,  // the hint field holds a value
    IS_BACKED = 1 << 2,  // the symbol came from real tensor metadata
};

struct SymbolEntry {
    int64_t hint = INT64_MIN;  // a value observed while tracing; the minimum means none
    int64_t range_lower = 0;
    int64_t range_upper = 0;
    SymKind kind = SymKind::SIZE;
    fixy::wrap::Bits<SymFlags> sym_flags{};
    uint16_t expr_flags = 0;  // the assumption bits to stamp on an expression node
    uint32_t _pad = 0;
};

static_assert(sizeof(SymbolEntry) == 32, "SymbolEntry should be 32 bytes");
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(SymbolEntry);

// An identifier this table hands out is known to index this table. One that
// arrives from a file or across a language boundary is not, and has to be
// validated before it reaches the accessors below.
using InternalSymbolId = ::crucible::fixy::wrap::Tagged<SymbolId, ::crucible::fixy::tags::source::FromInternal>;
static_assert(sizeof(InternalSymbolId) == sizeof(SymbolId));

class CRUCIBLE_OWNER SymbolTable {
public:
    // The smallest int64 is taken as the no-hint marker, so the unbounded
    // lower end of a range is one above it.
    static constexpr int64_t kIntPosInf = INT64_MAX;
    static constexpr int64_t kIntNegInf = INT64_MIN + 1;
    static constexpr int64_t kNoHint = INT64_MIN;

    SymbolTable() = default;

    [[nodiscard, gnu::cold]] InternalSymbolId add(SymKind kind, uint16_t expr_flags, bool is_backed = true) {
        auto id = SymbolId{static_cast<uint32_t>(entries_.size())};
        SymbolEntry e{};
        e.hint = kNoHint;
        e.kind = kind;
        e.expr_flags = expr_flags;
        if (is_backed) e.sym_flags.set(SymFlags::IS_BACKED);

        switch (kind) {
            case SymKind::SIZE:
                // A size starts at two, not at zero: the values zero and one
                // are specialized rather than left symbolic.
                e.range_lower = 2;
                e.range_upper = kIntPosInf;
                break;
            case SymKind::UNBACKED_INT:
                e.range_lower = kIntNegInf;
                e.range_upper = kIntPosInf;
                break;
            case SymKind::FLOAT:
            case SymKind::UNBACKED_FLOAT:
                // A real-valued range is stored as the bits of its doubles.
                e.range_lower = bitcast_double(-std::numeric_limits<double>::infinity());
                e.range_upper = bitcast_double(std::numeric_limits<double>::infinity());
                break;
            default:
                std::unreachable();
        }

        entries_.push_back(e);
        return InternalSymbolId{id};
    }

    void set_hint(SymbolId id, int64_t hint) {
        auto& e = entry_at_mut(id);
        e.hint = hint;
        e.sym_flags.set(SymFlags::HAS_HINT);
    }

    void set_hint_float(SymbolId id, double hint) {
        auto& e = entry_at_mut(id);
        e.hint = bitcast_double(hint);
        e.sym_flags.set(SymFlags::HAS_HINT);
    }

    // Neither bound ever moves outward, so one call establishes a constraint
    // that no later call can loosen.
    //
    // The value parameters are const because a postcondition may only read a
    // parameter that is. The postconditions index the vector directly rather
    // than calling the accessor, which would re-evaluate that accessor's own
    // contract inside the check.
    void tighten_range(const SymbolId id, const int64_t lower, const int64_t upper)
        post(entries_[id.raw()].range_lower >= lower) post(entries_[id.raw()].range_upper <= upper) {
        auto& e = entry_at_mut(id);
        if (lower > e.range_lower) e.range_lower = lower;
        if (upper < e.range_upper) e.range_upper = upper;
    }

    void set_size_like(SymbolId id) { entry_at_mut(id).sym_flags.set(SymFlags::IS_SIZE_LIKE); }

    [[nodiscard]] const SymbolEntry& operator[](SymbolId id) const CRUCIBLE_LIFETIMEBOUND { return entry_at(id); }

    [[nodiscard, gnu::pure]] bool has_hint(SymbolId id) const noexcept {
        return entry_at(id).sym_flags.test(SymFlags::HAS_HINT);
    }

    [[nodiscard, gnu::pure]] int64_t hint(SymbolId id) const noexcept { return entry_at(id).hint; }

    [[nodiscard, gnu::pure]] double hint_float(SymbolId id) const noexcept {
        return bitcast_to_double(entry_at(id).hint);
    }

    [[nodiscard, gnu::pure]] int64_t lower(SymbolId id) const noexcept { return entry_at(id).range_lower; }

    [[nodiscard, gnu::pure]] int64_t upper(SymbolId id) const noexcept { return entry_at(id).range_upper; }

    [[nodiscard, gnu::pure]] bool is_size_like(SymbolId id) const noexcept {
        return entry_at(id).sym_flags.test(SymFlags::IS_SIZE_LIKE);
    }

    [[nodiscard, gnu::pure]] bool is_backed(SymbolId id) const noexcept {
        return entry_at(id).sym_flags.test(SymFlags::IS_BACKED);
    }

    [[nodiscard, gnu::pure]] SymKind kind(SymbolId id) const noexcept { return entry_at(id).kind; }

    [[nodiscard, gnu::pure]] uint16_t expr_flags(SymbolId id) const noexcept { return entry_at(id).expr_flags; }

    // True when the symbol's whole range lies inside the one given.
    [[nodiscard, gnu::pure]] bool range_contains(SymbolId id, int64_t lo, int64_t hi) const noexcept {
        const auto& e = entry_at(id);
        return e.range_lower >= lo && e.range_upper <= hi;
    }

    [[nodiscard, gnu::pure]] bool is_positive(SymbolId id) const noexcept { return entry_at(id).range_lower > 0; }

    [[nodiscard, gnu::pure]] bool is_nonnegative(SymbolId id) const noexcept { return entry_at(id).range_lower >= 0; }

    [[nodiscard, gnu::pure]] size_t size() const noexcept { return entries_.size(); }

private:
    [[nodiscard]] static int64_t bitcast_double(double d) { return std::bit_cast<int64_t>(d); }

    [[nodiscard]] static double bitcast_to_double(int64_t v) { return std::bit_cast<double>(v); }

    // Every read and every write of the vector goes through these two, which
    // is what keeps the guards below to two sites rather than fifteen.
    //
    // The validity check rejects the default identifier, which is what an
    // expression node carries when it is not a symbol at all. Passing that
    // straight through would index the vector at the largest uint32.
    //
    // The empty check is not redundant with the range check: on an empty
    // table the subtraction wraps to the largest size_t and the range check
    // then admits every index.

    [[nodiscard, gnu::pure]] const SymbolEntry& entry_at(SymbolId id) const noexcept pre(id.is_valid()) {
        CRUCIBLE_PRE(!entries_.empty());
        CRUCIBLE_PRE(::crucible::decide::in_range<std::size_t>(static_cast<std::size_t>(id.raw()), std::size_t{0},
                                                               entries_.size() - std::size_t{1}));
        return entries_[id.raw()];
    }

    [[nodiscard]] SymbolEntry& entry_at_mut(SymbolId id) noexcept pre(id.is_valid()) {
        CRUCIBLE_PRE(!entries_.empty());
        CRUCIBLE_PRE(::crucible::decide::in_range<std::size_t>(static_cast<std::size_t>(id.raw()), std::size_t{0},
                                                               entries_.size() - std::size_t{1}));
        return entries_[id.raw()];
    }

    std::vector<SymbolEntry> entries_;
};

}  // namespace crucible
