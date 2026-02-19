#pragma once

// SymbolTable: per-symbol metadata storage.
//
// Maps symbol_id → (kind, hint, range, flags). Compact vector layout
// indexed by the same uint32_t id used in Expr::symbol_id.
//
// Designed to hold everything the Simplifier needs for range-based
// rewriting without calling back into Python.

#include <crucible/Ops.h>

#include <bit>
#include <cstdint>
#include <limits>
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
  int64_t hint;         // concrete value from tracing run (valid only if HAS_HINT)
  int64_t range_lower;  // lower bound (kIntNegInf = unknown)
  int64_t range_upper;  // upper bound (kIntPosInf = unknown)
  SymKind kind;         // 1 byte
  uint8_t sym_flags;    // SymFlags bitfield
  uint16_t expr_flags;  // ExprFlags to stamp on Expr nodes (IS_INTEGER, IS_POSITIVE, etc.)
  // 4 bytes padding to align to 32 bytes (3 × int64_t + 4 × uint8/16)
  uint32_t _pad;
};

static_assert(sizeof(SymbolEntry) == 32, "SymbolEntry should be 32 bytes");

class SymbolTable {
 public:
  // Sentinel values for integer ranges.
  // INT64_MIN is reserved as "no hint" sentinel, so -int_oo uses MIN+1.
  static constexpr int64_t kIntPosInf = INT64_MAX;
  static constexpr int64_t kIntNegInf = INT64_MIN + 1;
  static constexpr int64_t kNoHint = INT64_MIN;

  SymbolTable() = default;

  // Register a new symbol. Returns the assigned ID.
  // Caller provides assumptions as ExprFlags bits (IS_INTEGER, IS_POSITIVE, etc).
  uint32_t add(SymKind kind, uint16_t expr_flags, bool is_backed = true) {
    uint32_t id = static_cast<uint32_t>(entries_.size());
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
    }

    entries_.push_back(e);
    return id;
  }

  // Set the concrete hint for a backed symbol.
  void set_hint(uint32_t id, int64_t hint) {
    entries_[id].hint = hint;
    entries_[id].sym_flags |= SymFlags::HAS_HINT;
  }

  // Set float hint (bitcast to int64_t).
  void set_hint_float(uint32_t id, double hint) {
    entries_[id].hint = bitcast_double(hint);
    entries_[id].sym_flags |= SymFlags::HAS_HINT;
  }

  // Tighten the integer range. Only narrows, never widens.
  void tighten_range(uint32_t id, int64_t lower, int64_t upper) {
    auto& e = entries_[id];
    if (lower > e.range_lower)
      e.range_lower = lower;
    if (upper < e.range_upper)
      e.range_upper = upper;
  }

  void set_size_like(uint32_t id) {
    entries_[id].sym_flags |= SymFlags::IS_SIZE_LIKE;
  }

  // ---- Queries ----

  const SymbolEntry& operator[](uint32_t id) const {
    return entries_[id];
  }

  bool has_hint(uint32_t id) const {
    return entries_[id].sym_flags & SymFlags::HAS_HINT;
  }

  int64_t hint(uint32_t id) const {
    return entries_[id].hint;
  }

  double hint_float(uint32_t id) const {
    return bitcast_to_double(entries_[id].hint);
  }

  int64_t lower(uint32_t id) const {
    return entries_[id].range_lower;
  }

  int64_t upper(uint32_t id) const {
    return entries_[id].range_upper;
  }

  bool is_size_like(uint32_t id) const {
    return entries_[id].sym_flags & SymFlags::IS_SIZE_LIKE;
  }

  bool is_backed(uint32_t id) const {
    return entries_[id].sym_flags & SymFlags::IS_BACKED;
  }

  SymKind kind(uint32_t id) const {
    return entries_[id].kind;
  }

  uint16_t expr_flags(uint32_t id) const {
    return entries_[id].expr_flags;
  }

  // Range check: is value guaranteed to be in [lo, hi]?
  bool range_contains(uint32_t id, int64_t lo, int64_t hi) const {
    const auto& e = entries_[id];
    return e.range_lower >= lo && e.range_upper <= hi;
  }

  // Is the symbol guaranteed positive (lower bound > 0)?
  bool is_positive(uint32_t id) const {
    return entries_[id].range_lower > 0;
  }

  // Is the symbol guaranteed nonnegative (lower bound >= 0)?
  bool is_nonnegative(uint32_t id) const {
    return entries_[id].range_lower >= 0;
  }

  size_t size() const {
    return entries_.size();
  }

 private:
  static int64_t bitcast_double(double d) {
    return std::bit_cast<int64_t>(d);
  }

  static double bitcast_to_double(int64_t v) {
    return std::bit_cast<double>(v);
  }

  std::vector<SymbolEntry> entries_;
};

} // namespace crucible
