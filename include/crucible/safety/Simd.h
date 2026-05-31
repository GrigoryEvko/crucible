#pragma once

// ═══════════════════════════════════════════════════════════════════
// crucible::simd — portable SIMD facade over GCC vector extensions
//
// Backend: GCC `[[gnu::vector_size]]` builtins + manual lane folds.
// NOT std::simd — libstdc++ 16 gates <simd> behind __SSE2__, so it is
// an empty header on aarch64 (NEON/SVE).  The vector-extension backend
// compiles identically on x86 (SSE2/AVX2/AVX-512) and ARM (NEON), with
// zero library gating, no <experimental/simd> API divergence, and no
// runtime dispatch (CLAUDE.md §VIII bans target_clones).
//
// ─── Width is a throughput knob, not a result knob ──────────────────
//
// Every Crucible SIMD path is integer element-wise ops + integer
// reductions (dim-hash xor-fold, content-hash, FEC GF-arithmetic,
// Philox).  For that class the RESULT is width-independent:
//
//   * element-wise int ops (+ * ^ & << >> < ==) give bit-identical
//     lanes whether the CPU runs them as 1×AVX-512, 2×AVX2, or 4×NEON;
//   * integer reductions with xor/add/and/or/min/max are associative
//     AND commutative, so the folded scalar is identical regardless of
//     how lanes are grouped or how wide the register is.
//
// So a width-pinned LOGICAL lane count (i64x8 == 8 logical lanes) is
// declared once; the BUILD's -march lowers it to the widest native
// instruction available — 1× zmm / 2× ymm / 4× NEON-q — and GCC does
// the "128-bit partition + concat" for you.  FP reductions are
// forbidden (IEEE rounding is non-associative → diverges with width);
// the DetSafeSimd concept enforces integer-only at reduction boundaries.
//
// ─── native_lane_count<T> — auto-select widest, at compile time ─────
//
// Streaming kernels chunk by native_lane_count<T> (derived from the
// -march target macros) + scalar tail.  The widest-available selection
// is the build's microarch; the per-chunk integer op is bit-exact and
// the final fold is associative, so the whole-buffer result is stable
// across ISAs.  Witnessed by the cross-vendor numerics CI (MIMIC.md
// §41): same IR on x86 (AVX2/512) and ARM (NEON), byte-compared.
//
// ─── DetSafe contract (CLAUDE.md §II.8) ─────────────────────────────
//
//   1. Integer reductions only — enforced by DetSafeSimd<V>.
//   2. Stable lane order — operator[] returns lane i regardless of the
//      hardware width; never rely on lane identity after a shuffle in
//      a BITEXACT recipe.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/Platform.h>

#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <utility>

namespace crucible::simd {

// ── Compile-time native width (widest the -march target provides) ───

#if defined(__AVX512F__)
inline constexpr std::size_t native_bytes = 64;
#elif defined(__AVX2__)
inline constexpr std::size_t native_bytes = 32;
#else  // SSE2 (x86 floor), NEON (arm floor), or scalar emulation (still 128-bit logical)
inline constexpr std::size_t native_bytes = 16;
#endif

static_assert(native_bytes >= 16,
    "Crucible assumes a >=128-bit SIMD floor (x86 AVX2 / ARM NEON per CLAUDE.md §XIV)");

// Widest lane count for element type T on this build.  Streaming kernels
// loop in native_lane_count<T> chunks + scalar tail; the build picks the
// width, the integer op per chunk is bit-exact across ISAs.
template <typename ElementType>
inline constexpr int native_lane_count =
    static_cast<int>(native_bytes / sizeof(ElementType));

// ── Raw builtin vector typedefs ─────────────────────────────────────
//
// vector_size on a DEPENDENT alias type is silently dropped by GCC, so
// the raw vectors must be CONCRETE per-(T,N) typedefs behind a trait.
// Enumerate the (element, lane) combos the project actually uses.

namespace detail {

template <typename ElementType, int Lanes>
struct raw_vector;  // primary left undefined — only listed combos exist

#define CRUCIBLE_SIMD_RAW(ElementType, Lanes)                                  \
  template <>                                                                  \
  struct raw_vector<ElementType, Lanes> {                                      \
    typedef ElementType type                                                   \
        __attribute__((__vector_size__(Lanes * sizeof(ElementType))));         \
  }

CRUCIBLE_SIMD_RAW(std::int64_t,   4);
CRUCIBLE_SIMD_RAW(std::int64_t,   8);
CRUCIBLE_SIMD_RAW(std::uint64_t,  4);
CRUCIBLE_SIMD_RAW(std::uint64_t,  8);
CRUCIBLE_SIMD_RAW(std::int32_t,   8);
CRUCIBLE_SIMD_RAW(std::int32_t,  16);
CRUCIBLE_SIMD_RAW(std::uint32_t,  8);
CRUCIBLE_SIMD_RAW(std::uint32_t, 16);
CRUCIBLE_SIMD_RAW(std::uint8_t,  16);
CRUCIBLE_SIMD_RAW(std::uint8_t,  32);
CRUCIBLE_SIMD_RAW(std::int8_t,   16);
CRUCIBLE_SIMD_RAW(std::int8_t,   32);
// Floating-point lanes exist for element-wise ops and as the canonical
// "NOT DetSafe" witnesses (FP reductions are forbidden — DetSafeSimd
// rejects these because their value_type is non-integral).
CRUCIBLE_SIMD_RAW(float,          8);
CRUCIBLE_SIMD_RAW(float,         16);
CRUCIBLE_SIMD_RAW(double,         4);
CRUCIBLE_SIMD_RAW(double,         8);

#undef CRUCIBLE_SIMD_RAW

// Signed-integer companion element a GCC vector comparison yields as its
// mask (all-ones / zero per lane), matched to the lane byte-width of T.
template <typename ElementType> struct mask_element;
template <> struct mask_element<std::int64_t>  { using type = std::int64_t; };
template <> struct mask_element<std::uint64_t> { using type = std::int64_t; };
template <> struct mask_element<std::int32_t>  { using type = std::int32_t; };
template <> struct mask_element<std::uint32_t> { using type = std::int32_t; };
template <> struct mask_element<std::uint8_t>  { using type = std::int8_t;  };
template <> struct mask_element<std::int8_t>   { using type = std::int8_t;  };
template <> struct mask_element<float>         { using type = std::int32_t; };
template <> struct mask_element<double>        { using type = std::int64_t; };

}  // namespace detail

// ── mask<MaskElement, Lanes> ────────────────────────────────────────
//
// Per-lane boolean produced by a vec comparison.  All-ones lane = true,
// zero lane = false (GCC vector-comparison convention).  Deliberately
// carries NO value_type — so DetSafeSimd<mask> is false (a mask is not
// a reducible integer vector).

template <typename MaskElement, int Lanes>
struct mask {
  using raw_type = typename detail::raw_vector<MaskElement, Lanes>::type;
  raw_type m_{};

  constexpr mask() noexcept = default;
  constexpr mask(raw_type raw) noexcept : m_{raw} {}

  [[nodiscard]] static constexpr std::size_t size() noexcept {
    return static_cast<std::size_t>(Lanes);
  }
  // Lane index is `int` (the std::simd / GCC-vector subscript convention),
  // not size_t — lane counts are small and bounded, and this matches the
  // raw builtin's own index type so no conversion is needed.
  [[nodiscard]] constexpr bool operator[](int lane) const noexcept {
    return m_[lane] != 0;
  }

  // Per-lane logical combinators (elementwise; NOT short-circuiting —
  // both operands are already-evaluated masks).
  [[nodiscard]] friend constexpr mask operator&(mask a, mask b) noexcept { return mask{a.m_ & b.m_}; }
  [[nodiscard]] friend constexpr mask operator|(mask a, mask b) noexcept { return mask{a.m_ | b.m_}; }
  [[nodiscard]] friend constexpr mask operator^(mask a, mask b) noexcept { return mask{a.m_ ^ b.m_}; }
  [[nodiscard]] friend constexpr mask operator~(mask a)         noexcept { return mask{~a.m_}; }
  [[nodiscard]] friend constexpr mask operator&&(mask a, mask b) noexcept { return mask{a.m_ & b.m_}; }
  [[nodiscard]] friend constexpr mask operator||(mask a, mask b) noexcept { return mask{a.m_ | b.m_}; }
};

// ── Horizontal mask reductions (namespace scope: qualified + ADL) ────

template <typename MaskElement, int Lanes>
[[nodiscard]] constexpr bool any_of(mask<MaskElement, Lanes> m) noexcept {
  bool acc = false;
  for (int lane = 0; lane < Lanes; ++lane) acc = acc || (m.m_[lane] != 0);
  return acc;
}
template <typename MaskElement, int Lanes>
[[nodiscard]] constexpr bool all_of(mask<MaskElement, Lanes> m) noexcept {
  bool acc = true;
  for (int lane = 0; lane < Lanes; ++lane) acc = acc && (m.m_[lane] != 0);
  return acc;
}
template <typename MaskElement, int Lanes>
[[nodiscard]] constexpr bool none_of(mask<MaskElement, Lanes> m) noexcept {
  return !any_of(m);
}

// ── vec<ElementType, Lanes> ─────────────────────────────────────────
//
// Zero-overhead wrapper over the raw builtin vector.  sizeof(vec) ==
// sizeof(raw) == Lanes*sizeof(T); all ops inline to builtin vector ops.

template <typename ElementType, int Lanes>
struct vec {
  using value_type = ElementType;
  using raw_type   = typename detail::raw_vector<ElementType, Lanes>::type;
  using mask_type  =
      mask<typename detail::mask_element<ElementType>::type, Lanes>;

  raw_type v_{};

  [[nodiscard]] static constexpr std::size_t size() noexcept {
    return static_cast<std::size_t>(Lanes);
  }

  constexpr vec() noexcept = default;
  constexpr vec(raw_type raw) noexcept : v_{raw} {}

  // All lane-populating ctors brace-initialize the raw builtin via a
  // pack expansion rather than subscript-assigning (`v_[i] = x`).
  // Subscript-write on a [[gnu::vector_size]] builtin is NOT a constant
  // expression in GCC 16, so iota_v<V>() (constexpr) would otherwise be
  // unusable in a constant-evaluated context; brace-init is constexpr-
  // legal and compiles to the same set/broadcast instructions.

  // Broadcast a scalar to every lane: vec(0), vec(count).
  explicit constexpr vec(ElementType scalar) noexcept
      : v_([&]<int... Lane>(std::integer_sequence<int, Lane...>) noexcept
               -> raw_type {
          return raw_type{((void)Lane, scalar)...};
        }(std::make_integer_sequence<int, Lanes>{})) {}

  // Generator: vec(gen) with gen(integral_constant<int, lane>) -> T.
  // Excluded for scalar args via the invocable constraint, so it never
  // collides with the broadcast ctor.
  template <typename Generator>
    requires std::is_invocable_v<Generator&&, std::integral_constant<int, 0>>
  explicit constexpr vec(Generator&& gen) noexcept
      : v_([&]<int... Lane>(std::integer_sequence<int, Lane...>)
               -> raw_type {
          return raw_type{static_cast<ElementType>(
              gen(std::integral_constant<int, Lane>{}))...};
        }(std::make_integer_sequence<int, Lanes>{})) {}

  // Lane-preserving value conversion from vec<U, Lanes> (e.g. i64x8 ->
  // u64x8 in the dim-hash).  Explicit — never an implicit narrowing.
  template <typename OtherElement>
    requires(!std::is_same_v<OtherElement, ElementType>)
  explicit constexpr vec(vec<OtherElement, Lanes> other) noexcept
      : v_([&]<int... Lane>(std::integer_sequence<int, Lane...>) noexcept
               -> raw_type {
          return raw_type{static_cast<ElementType>(other.v_[Lane])...};
        }(std::make_integer_sequence<int, Lanes>{})) {}

  // Lane index is `int` (std::simd / GCC-vector subscript convention).
  [[nodiscard]] constexpr ElementType operator[](int lane) const noexcept {
    return v_[lane];
  }

  // ── element-wise arithmetic / bitwise (return vec) ──────────────
  [[nodiscard]] friend constexpr vec operator+(vec a, vec b) noexcept { return vec{a.v_ + b.v_}; }
  [[nodiscard]] friend constexpr vec operator-(vec a, vec b) noexcept { return vec{a.v_ - b.v_}; }
  [[nodiscard]] friend constexpr vec operator*(vec a, vec b) noexcept { return vec{a.v_ * b.v_}; }
  [[nodiscard]] friend constexpr vec operator&(vec a, vec b) noexcept { return vec{a.v_ & b.v_}; }
  [[nodiscard]] friend constexpr vec operator|(vec a, vec b) noexcept { return vec{a.v_ | b.v_}; }
  [[nodiscard]] friend constexpr vec operator^(vec a, vec b) noexcept { return vec{a.v_ ^ b.v_}; }
  [[nodiscard]] friend constexpr vec operator~(vec a)        noexcept { return vec{~a.v_}; }
  [[nodiscard]] friend constexpr vec operator-(vec a)        noexcept { return vec{-a.v_}; }
  [[nodiscard]] friend constexpr vec operator<<(vec a, int s) noexcept { return vec{a.v_ << s}; }
  [[nodiscard]] friend constexpr vec operator>>(vec a, int s) noexcept { return vec{a.v_ >> s}; }
  [[nodiscard]] friend constexpr vec operator<<(vec a, vec s) noexcept { return vec{a.v_ << s.v_}; }
  [[nodiscard]] friend constexpr vec operator>>(vec a, vec s) noexcept { return vec{a.v_ >> s.v_}; }

  // ── comparisons (return mask) ───────────────────────────────────
  [[nodiscard]] friend constexpr mask_type operator==(vec a, vec b) noexcept { return mask_type{a.v_ == b.v_}; }
  [[nodiscard]] friend constexpr mask_type operator!=(vec a, vec b) noexcept { return mask_type{a.v_ != b.v_}; }
  [[nodiscard]] friend constexpr mask_type operator< (vec a, vec b) noexcept { return mask_type{a.v_ <  b.v_}; }
  [[nodiscard]] friend constexpr mask_type operator<=(vec a, vec b) noexcept { return mask_type{a.v_ <= b.v_}; }
  [[nodiscard]] friend constexpr mask_type operator> (vec a, vec b) noexcept { return mask_type{a.v_ >  b.v_}; }
  [[nodiscard]] friend constexpr mask_type operator>=(vec a, vec b) noexcept { return mask_type{a.v_ >= b.v_}; }
};

// ── Width-pinned aliases ────────────────────────────────────────────
//
// Lane counts Crucible uses (TensorMeta ndim ≤ 8 → i64x8 is the natural
// dim-hash size).  These are LOGICAL lane counts; the build lowers them
// to the widest native instruction.

using i64x4  = vec<std::int64_t,  4>;
using i64x8  = vec<std::int64_t,  8>;
using u64x4  = vec<std::uint64_t, 4>;
using u64x8  = vec<std::uint64_t, 8>;
using i32x8  = vec<std::int32_t,  8>;
using i32x16 = vec<std::int32_t, 16>;
using u32x8  = vec<std::uint32_t, 8>;
using u32x16 = vec<std::uint32_t,16>;
using u8x16  = vec<std::uint8_t, 16>;
using u8x32  = vec<std::uint8_t, 32>;

using i64x8_mask = i64x8::mask_type;
using u64x8_mask = u64x8::mask_type;
using u32x8_mask = u32x8::mask_type;

// ── DetSafeSimd<V> concept ──────────────────────────────────────────
//
// V is a vec whose integral lane type may take part in a BITEXACT-
// recipe reduction.  Masks fail it (no value_type) — exactly the
// guard the reduction helpers below carry.

template <typename V>
concept DetSafeSimd =
    requires { typename V::value_type; } &&
    std::integral<typename V::value_type>;

// ── loads / stores ──────────────────────────────────────────────────
//
// memcpy is the portable, bit-exact, strict-aliasing-safe load/store;
// GCC lowers it to one native vector move (movdqu / vld1q / ...).  The
// _aligned variants hint alignment for the aligned move form.

template <typename V>
[[nodiscard]] CRUCIBLE_INLINE V load(const typename V::value_type* ptr) noexcept {
  typename V::raw_type raw;
  std::memcpy(&raw, ptr, sizeof(raw));
  return V{raw};
}

template <typename V>
[[nodiscard]] CRUCIBLE_INLINE V load_aligned(const typename V::value_type* ptr) noexcept {
  const auto* aligned = static_cast<const typename V::value_type*>(
      __builtin_assume_aligned(ptr, alignof(typename V::raw_type)));
  typename V::raw_type raw;
  std::memcpy(&raw, aligned, sizeof(raw));
  return V{raw};
}

// Load the first `count` lanes from ptr; remaining lanes are zero.
// The "process the first N lanes" partial form (count <= V::size()).
template <typename V>
[[nodiscard]] CRUCIBLE_INLINE V partial_load(const typename V::value_type* ptr,
                                             int count) noexcept {
  V result{};
  for (int lane = 0; lane < count; ++lane) result.v_[lane] = ptr[lane];
  return result;
}

template <typename V>
CRUCIBLE_INLINE void store(V value, typename V::value_type* ptr) noexcept {
  std::memcpy(ptr, &value.v_, sizeof(value.v_));
}

template <typename V>
CRUCIBLE_INLINE void store_aligned(V value, typename V::value_type* ptr) noexcept {
  auto* aligned = static_cast<typename V::value_type*>(
      __builtin_assume_aligned(ptr, alignof(typename V::raw_type)));
  std::memcpy(aligned, &value.v_, sizeof(value.v_));
}

// ── select(mask, on_true, on_false) ─────────────────────────────────
//
// Branchless per-lane blend.  The mask raw (all-ones/zero, same lane
// byte-width as T) bit-casts onto T's raw for the (a & m) | (b & ~m)
// blend — no shuffle, one native op pair.

template <typename ElementType, int Lanes>
[[nodiscard]] CRUCIBLE_INLINE vec<ElementType, Lanes>
select(typename vec<ElementType, Lanes>::mask_type selector,
       vec<ElementType, Lanes> on_true,
       vec<ElementType, Lanes> on_false) noexcept {
  using RawType = typename vec<ElementType, Lanes>::raw_type;
  const RawType bits = std::bit_cast<RawType>(selector.m_);
  return vec<ElementType, Lanes>{(on_true.v_ & bits) | (on_false.v_ & ~bits)};
}

// ── reductions — DetSafe integer folds (bit-exact across widths) ─────
//
// Manual lane fold: associative+commutative integer ops give the same
// scalar regardless of width or grouping (proven across AVX-512 / AVX2
// / NEON / scalar).  GCC compiles the small horizontal tail to a
// shuffle-tree.  Masked xor folds masked-out lanes to identity 0.

template <typename V>
  requires DetSafeSimd<V>
[[nodiscard]] CRUCIBLE_INLINE typename V::value_type reduce_xor(V value) noexcept {
  typename V::value_type acc = value.v_[0];
  for (int lane = 1; lane < static_cast<int>(V::size()); ++lane)
    acc = static_cast<typename V::value_type>(acc ^ value.v_[lane]);
  return acc;
}

template <typename V>
  requires DetSafeSimd<V>
[[nodiscard]] CRUCIBLE_INLINE typename V::value_type reduce_add(V value) noexcept {
  typename V::value_type acc = value.v_[0];
  for (int lane = 1; lane < static_cast<int>(V::size()); ++lane)
    acc = static_cast<typename V::value_type>(acc + value.v_[lane]);
  return acc;
}

template <typename V>
  requires DetSafeSimd<V>
[[nodiscard]] CRUCIBLE_INLINE typename V::value_type reduce_and(V value) noexcept {
  typename V::value_type acc = value.v_[0];
  for (int lane = 1; lane < static_cast<int>(V::size()); ++lane)
    acc = static_cast<typename V::value_type>(acc & value.v_[lane]);
  return acc;
}

template <typename V>
  requires DetSafeSimd<V>
[[nodiscard]] CRUCIBLE_INLINE typename V::value_type reduce_or(V value) noexcept {
  typename V::value_type acc = value.v_[0];
  for (int lane = 1; lane < static_cast<int>(V::size()); ++lane)
    acc = static_cast<typename V::value_type>(acc | value.v_[lane]);
  return acc;
}

template <typename V>
  requires DetSafeSimd<V>
[[nodiscard]] CRUCIBLE_INLINE typename V::value_type reduce_min(V value) noexcept {
  typename V::value_type acc = value.v_[0];
  for (int lane = 1; lane < static_cast<int>(V::size()); ++lane)
    acc = value.v_[lane] < acc ? value.v_[lane] : acc;
  return acc;
}

template <typename V>
  requires DetSafeSimd<V>
[[nodiscard]] CRUCIBLE_INLINE typename V::value_type reduce_max(V value) noexcept {
  typename V::value_type acc = value.v_[0];
  for (int lane = 1; lane < static_cast<int>(V::size()); ++lane)
    acc = value.v_[lane] > acc ? value.v_[lane] : acc;
  return acc;
}

// Masked xor fold — masked-out lanes contribute identity 0.
template <typename V>
  requires DetSafeSimd<V>
[[nodiscard]] CRUCIBLE_INLINE typename V::value_type
reduce_xor(V value, typename V::mask_type valid) noexcept {
  using ElementType = typename V::value_type;
  constexpr int Lanes = static_cast<int>(V::size());
  return reduce_xor(
      select<ElementType, Lanes>(valid, value, V{static_cast<ElementType>(0)}));
}

// Masked add fold — masked-out lanes contribute identity 0.
template <typename V>
  requires DetSafeSimd<V>
[[nodiscard]] CRUCIBLE_INLINE typename V::value_type
reduce_add(V value, typename V::mask_type valid) noexcept {
  using ElementType = typename V::value_type;
  constexpr int Lanes = static_cast<int>(V::size());
  return reduce_add(
      select<ElementType, Lanes>(valid, value, V{static_cast<ElementType>(0)}));
}

// ── element-wise min / max (return vec) ─────────────────────────────

template <typename V>
[[nodiscard]] CRUCIBLE_INLINE V min(V a, V b) noexcept {
  return select<typename V::value_type, static_cast<int>(V::size())>(a < b, a, b);
}
template <typename V>
[[nodiscard]] CRUCIBLE_INLINE V max(V a, V b) noexcept {
  return select<typename V::value_type, static_cast<int>(V::size())>(a > b, a, b);
}

// ── iota_v<V>() — lane[i] == i ──────────────────────────────────────

template <typename V>
[[nodiscard, gnu::const]] constexpr V iota_v() noexcept {
  using ElementType = typename V::value_type;
  return V([](auto lane) noexcept -> ElementType {
    return static_cast<ElementType>(decltype(lane)::value);
  });
}

// ── prefix_mask<V>(count) — lanes [0, count) set ────────────────────

template <typename V>
[[nodiscard, gnu::const]] CRUCIBLE_INLINE
typename V::mask_type prefix_mask(int count) noexcept {
  using ElementType = typename V::value_type;
  return iota_v<V>() < V(static_cast<ElementType>(count));
}

// ── Microarch detection (compile-time) ──────────────────────────────

#if defined(__AVX512F__)
inline constexpr bool kAvx512Available = true;
#else
inline constexpr bool kAvx512Available = false;
#endif

#if defined(__AVX2__)
inline constexpr bool kAvx2Available = true;
#else
inline constexpr bool kAvx2Available = false;
#endif

#if defined(__SSE4_2__)
inline constexpr bool kSse42Available = true;
#else
inline constexpr bool kSse42Available = false;
#endif

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
inline constexpr bool kNeonAvailable = true;
#else
inline constexpr bool kNeonAvailable = false;
#endif

// ── Microarch detection (runtime) ───────────────────────────────────

#if defined(__x86_64__) || defined(__i386__)
[[nodiscard, gnu::pure]] inline bool runtime_supports_avx512() noexcept {
  return __builtin_cpu_supports("avx512f");
}
[[nodiscard, gnu::pure]] inline bool runtime_supports_avx2() noexcept {
  return __builtin_cpu_supports("avx2");
}
[[nodiscard, gnu::pure]] inline bool runtime_supports_sse42() noexcept {
  return __builtin_cpu_supports("sse4.2");
}
#else
[[nodiscard]] constexpr bool runtime_supports_avx512() noexcept { return false; }
[[nodiscard]] constexpr bool runtime_supports_avx2()   noexcept { return false; }
[[nodiscard]] constexpr bool runtime_supports_sse42()  noexcept { return false; }
#endif

}  // namespace crucible::simd
