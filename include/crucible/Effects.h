#pragma once

// ── Crucible Effect System ──────────────────────────────────────────
//
// Compile-time capability tokens that enforce side-effect boundaries.
//
// C++ has no built-in effect system. Haskell has IO, Koka has full
// effect inference, Rust has unsafe/Send/Sync. We build our own using
// zero-size capability tokens with private constructors.
//
// The key insight: effects are permissions. If a function can allocate,
// it needs PERMISSION to allocate. Make that permission a function
// parameter. No parameter → no permission → compile error.
//
// ── How it works ────────────────────────────────────────────────────
//
//   1. Capability tokens (fx::Alloc, fx::IO, fx::Block) have private
//      default constructors. Only authorized context types can create them.
//
//   2. Functions that perform side effects take the relevant token
//      by const reference:
//
//        void* Arena::alloc(fx::Alloc, size_t n);
//
//   3. Foreground hot-path code never holds any tokens, so it CAN'T
//      call effectful functions — the compiler rejects it.
//
//   4. Background thread creates fx::Bg (which holds all tokens) and
//      passes them through the call chain.
//
// ── Zero cost ───────────────────────────────────────────────────────
//
// Empty structs are 1 byte in C++, but when passed by value (not by
// reference), the compiler elides them entirely in inlined code.
// Since our hot-path functions are CRUCIBLE_INLINE, the token parameter
// generates zero machine instructions. It exists only in the type system.
//
// ── Effects tracked ─────────────────────────────────────────────────
//
//   fx::Alloc  — heap allocation (malloc, arena alloc, vector push)
//   fx::IO     — file/network I/O (fprintf, fopen, send, recv)
//   fx::Block  — blocking operations (mutex, sleep, futex, spin-wait)
//
// ── Contexts ────────────────────────────────────────────────────────
//
//   fx::Bg    — background thread: can alloc, do I/O, block
//   fx::Init  — initialization: can alloc, do I/O (constructors, setup)
//   fx::Test  — tests: all capabilities (unrestricted)
//
// Foreground hot-path functions take no context and hold no tokens.
// That's the point — purity by absence.
//
// ── Example ─────────────────────────────────────────────────────────
//
//   // Background thread entry point:
//   void bg_main(Arena& arena) {
//       fx::Bg bg;
//       auto* node = arena.alloc_obj<RegionNode>(bg.alloc);  // OK
//   }
//
//   // Hot path — compile error if it tries to allocate:
//   bool try_append(const Entry& e) {
//       // arena.alloc_obj<X>(???);  // ERROR: no fx::Alloc available
//       entries_[head_ & MASK] = e;  // OK: pure memory write
//       return true;
//   }
//

#include <concepts>

namespace crucible::fx {

// ── Forward declarations ────────────────────────────────────────────
struct Bg;
struct Init;
struct Test;

// ── Capability tokens ───────────────────────────────────────────────
//
// Zero-size proof types. Private default constructor ensures only
// authorized contexts can create them. Passed by value (not reference)
// so the compiler can elide them entirely in inlined code.

// noexcept on every ctor/assign: capability tokens are empty structs and
// must never throw.  Explicit noexcept documents intent AND guards
// against a future body being added that calls a throwing expression
// (the compiler catches the contradiction).
struct Alloc {
  Alloc(const Alloc&) noexcept = default;
  Alloc& operator=(const Alloc&) noexcept = default;
 private:
  constexpr Alloc() noexcept = default;
  friend struct Bg;
  friend struct Init;
  friend struct Test;
};

struct IO {
  IO(const IO&) noexcept = default;
  IO& operator=(const IO&) noexcept = default;
 private:
  constexpr IO() noexcept = default;
  friend struct Bg;
  friend struct Init;
  friend struct Test;
};

struct Block {
  Block(const Block&) noexcept = default;
  Block& operator=(const Block&) noexcept = default;
 private:
  constexpr Block() noexcept = default;
  friend struct Bg;
  friend struct Test;
  // Note: Init does NOT get Block — initialization should not block.
};

// ── Concepts ────────────────────────────────────────────────────────
//
// Use these to constrain template functions that need capabilities.
//
//   template<fx::CanAlloc Ctx>
//   void build_graph(Ctx& ctx, Arena& arena) { ... }
//
// A context satisfies CanAlloc if it has an `alloc` member of type Alloc.

template<typename Ctx>
concept CanAlloc = requires(Ctx ctx) {
  { ctx.alloc } -> std::convertible_to<Alloc>;
};

template<typename Ctx>
concept CanIO = requires(Ctx ctx) {
  { ctx.io } -> std::convertible_to<IO>;
};

template<typename Ctx>
concept CanBlock = requires(Ctx ctx) {
  { ctx.block } -> std::convertible_to<Block>;
};

// Pure = no effects at all. Useful for asserting a function is clean.
template<typename Ctx>
concept Pure = !CanAlloc<Ctx> && !CanIO<Ctx> && !CanBlock<Ctx>;

// ── Contexts ────────────────────────────────────────────────────────

// Background thread: can allocate, do I/O, and block.
// Created once in bg_thread_main(), tokens passed to callees.
struct Bg {
  [[no_unique_address]] Alloc alloc{};
  [[no_unique_address]] IO    io{};
  [[no_unique_address]] Block block{};
};

// Initialization: can allocate and do I/O, but not block.
// Used in constructors and setup code.
struct Init {
  [[no_unique_address]] Alloc alloc{};
  [[no_unique_address]] IO    io{};
};

// Tests: unrestricted. All capabilities available.
struct Test {
  [[no_unique_address]] Alloc alloc{};
  [[no_unique_address]] IO    io{};
  [[no_unique_address]] Block block{};
};

static_assert(sizeof(Bg)   == 1, "Bg context should be 1 byte (3 empty members, no_unique_address)");
static_assert(sizeof(Init) == 1, "Init context should be 1 byte");
static_assert(sizeof(Test) == 1, "Test context should be 1 byte");

// Concept checks on contexts
static_assert(CanAlloc<Bg>  && CanIO<Bg>  && CanBlock<Bg>);
static_assert(CanAlloc<Init> && CanIO<Init> && !CanBlock<Init>);
static_assert(CanAlloc<Test> && CanIO<Test> && CanBlock<Test>);

// int has no members — Pure
static_assert(Pure<int>);
// Bg has effects — not Pure
static_assert(!Pure<Bg>);

} // namespace crucible::fx
