#pragma once

// SymbolTable: per-symbol metadata storage.
//
// Maps SymbolId → (kind, hint, range, flags). Compact vector layout
// indexed by the same SymbolId used in Expr::symbol_id.
//
// Designed to hold everything the Simplifier needs for range-based
// rewriting without calling back into Python.

#include <crucible/Ops.h>
#include <crucible/Platform.h>
#include <crucible/Types.h>

#include <bit>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace crucible {

// Symbol origin type, mirrors torch.utils._sympy.symbol.SymT.
// Determines naming prefix and default assumptions.
enum class SymKind : uint8_t {
  SIZE,           // s0, s1, ...; integer, typically positive, ≥ 2
  FLOAT,          // zf0, zf1, ...; real
  UNBACKED_INT,   // u0, u1, ...; integer, no concrete hint
  UNBACKED_FLOAT, // zuf0, zuf1, ...; real, no concrete hint
};

// Per-symbol flags (separate from ExprFlags on Expr nodes).
struct SymFlags {
  static constexpr uint8_t IS_SIZE_LIKE = 1 << 0; // can assume ≥ 2 in size-oblivious mode
  static constexpr uint8_t HAS_HINT     = 1 << 1; // hint field is valid
  static constexpr uint8_t IS_BACKED    = 1 << 2; // symbol originates from real tensor metadata
};

struct SymbolEntry {
  int64_t hint = INT64_MIN;        // concrete value from tracing run (INT64_MIN = no hint)
  int64_t range_lower = 0;        // lower bound (kIntNegInf = unknown)
  int64_t range_upper = 0;        // upper bound (kIntPosInf = unknown)
  SymKind kind = SymKind::SIZE;   // 1 byte
  uint8_t sym_flags = 0;          // SymFlags bitfield
  uint16_t expr_flags = 0;        // ExprFlags to stamp on Expr nodes (IS_INTEGER, IS_POSITIVE, etc.)
  // 4 bytes padding to align to 32 bytes (3 × int64_t + 4 × uint8/16)
  uint32_t _pad = 0;
};

static_assert(sizeof(SymbolEntry) == 32, "SymbolEntry should be 32 bytes");
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(SymbolEntry);

class CRUCIBLE_OWNER SymbolTable {
 public:
  // Sentinel values for integer ranges.
  // INT64_MIN is reserved as "no hint" sentinel, so -int_oo uses MIN+1.
  static constexpr int64_t kIntPosInf = INT64_MAX;
  static constexpr int64_t kIntNegInf = INT64_MIN + 1;
  static constexpr int64_t kNoHint = INT64_MIN;

  SymbolTable() = default;

  // Register a new symbol. Returns the assigned ID.
  // Caller provides assumptions as ExprFlags bits (IS_INTEGER, IS_POSITIVE, etc).
  // gnu::cold: startup-only, invoked once per symbol at DAG build time.
  [[nodiscard, gnu::cold]] SymbolId add(SymKind kind, uint16_t expr_flags, bool is_backed = true) {
    auto id = SymbolId{static_cast<uint32_t>(entries_.size())};
    SymbolEntry e{};
    e.hint = kNoHint;
    e.kind = kind;
    e.expr_flags = expr_flags;
    e.sym_flags = is_backed ? SymFlags::IS_BACKED : 0;

    // Default ranges based on kind
    switch (kind) {
      case SymKind::SIZE:
        // Backed sizes default to [2, +inf) (specialize_zero_one=True default)
        e.range_lower = 2;
        e.range_upper = kIntPosInf;
        break;
      case SymKind::UNBACKED_INT:
        e.range_lower = kIntNegInf;
        e.range_upper = kIntPosInf;
        break;
      case SymKind::FLOAT:
      case SymKind::UNBACKED_FLOAT:
        // Float ranges: store as bitcast doubles
        e.range_lower = bitcast_double(
            -std::numeric_limits<double>::infinity());
        e.range_upper = bitcast_double(
            std::numeric_limits<double>::infinity());
        break;
      default:
        std::unreachable();
    }

    entries_.push_back(e);
    return id;  // SymbolId
  }

  // Set the concrete hint for a backed symbol.
  void set_hint(SymbolId id, int64_t hint)
      pre (id.raw() < entries_.size())
  {
    entries_[id.raw()].hint = hint;
    entries_[id.raw()].sym_flags |= SymFlags::HAS_HINT;
  }

  // Set float hint (bitcast to int64_t).
  void set_hint_float(SymbolId id, double hint)
      pre (id.raw() < entries_.size())
  {
    entries_[id.raw()].hint = bitcast_double(hint);
    entries_[id.raw()].sym_flags |= SymFlags::HAS_HINT;
  }

  // Tighten the integer range. Only narrows, never widens.
  //
  // Narrowing invariant: after the call, the stored range is a subset of
  // [lower, upper] — specifically, range_lower ≥ lower and
  // range_upper ≤ upper.  Neither bound ever relaxes outward, so a caller
  // that wants to observe "the symbol is in [a, b]" only needs to call
  // this once with [a, b]; subsequent calls can only further constrain.
  //
  // const on value params is required by P2900R14 to use them in post().
  void tighten_range(const SymbolId id, const int64_t lower, const int64_t upper)
      pre (id.raw() < entries_.size())
      post (entries_[id.raw()].range_lower >= lower)
      post (entries_[id.raw()].range_upper <= upper)
  {
    auto& e = entries_[id.raw()];
    if (lower > e.range_lower)
      e.range_lower = lower;
    if (upper < e.range_upper)
      e.range_upper = upper;
  }

  void set_size_like(SymbolId id)
      pre (id.raw() < entries_.size())
  {
    entries_[id.raw()].sym_flags |= SymFlags::IS_SIZE_LIKE;
  }

  // ---- Queries ----

  [[nodiscard]] const SymbolEntry& operator[](SymbolId id) const CRUCIBLE_LIFETIMEBOUND {
    return entries_[id.raw()];
  }

  [[nodiscard, gnu::pure]] bool has_hint(SymbolId id) const noexcept {
    return entries_[id.raw()].sym_flags & SymFlags::HAS_HINT;
  }

  [[nodiscard, gnu::pure]] int64_t hint(SymbolId id) const noexcept {
    return entries_[id.raw()].hint;
  }

  [[nodiscard, gnu::pure]] double hint_float(SymbolId id) const noexcept {
    return bitcast_to_double(entries_[id.raw()].hint);
  }

  [[nodiscard, gnu::pure]] int64_t lower(SymbolId id) const noexcept {
    return entries_[id.raw()].range_lower;
  }

  [[nodiscard, gnu::pure]] int64_t upper(SymbolId id) const noexcept {
    return entries_[id.raw()].range_upper;
  }

  [[nodiscard, gnu::pure]] bool is_size_like(SymbolId id) const noexcept {
    return entries_[id.raw()].sym_flags & SymFlags::IS_SIZE_LIKE;
  }

  [[nodiscard, gnu::pure]] bool is_backed(SymbolId id) const noexcept {
    return entries_[id.raw()].sym_flags & SymFlags::IS_BACKED;
  }

  [[nodiscard, gnu::pure]] SymKind kind(SymbolId id) const noexcept {
    return entries_[id.raw()].kind;
  }

  [[nodiscard, gnu::pure]] uint16_t expr_flags(SymbolId id) const noexcept {
    return entries_[id.raw()].expr_flags;
  }

  // Range check: is value guaranteed to be in [lo, hi]?
  [[nodiscard, gnu::pure]] bool range_contains(SymbolId id, int64_t lo, int64_t hi) const noexcept {
    const auto& e = entries_[id.raw()];
    return e.range_lower >= lo && e.range_upper <= hi;
  }

  // Is the symbol guaranteed positive (lower bound > 0)?
  [[nodiscard, gnu::pure]] bool is_positive(SymbolId id) const noexcept {
    return entries_[id.raw()].range_lower > 0;
  }

  // Is the symbol guaranteed nonnegative (lower bound >= 0)?
  [[nodiscard, gnu::pure]] bool is_nonnegative(SymbolId id) const noexcept {
    return entries_[id.raw()].range_lower >= 0;
  }

  [[nodiscard, gnu::pure]] size_t size() const noexcept {
    return entries_.size();
  }

 private:
  [[nodiscard]] static int64_t bitcast_double(double d) {
    return std::bit_cast<int64_t>(d);
  }

  [[nodiscard]] static double bitcast_to_double(int64_t v) {
    return std::bit_cast<double>(v);
  }

  std::vector<SymbolEntry> entries_;
};

} // namespace crucible
