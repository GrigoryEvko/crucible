#pragma once

// ── crucible::safety::witness::arch — platform tags (FIXY-G9) ──────────
//
// Empty phantom tag types naming the supported microarchitectures.
// Used by safety::witness::PlatformBounded<W, Platforms...> to narrow
// a witness type to a specific arch.  A binding produced under
// PlatformBounded<Tested<id>, X86_64> claims test coverage only on
// x86-64 fleets; the same binding flowing to an AArch64 consumer
// triggers a witness-floor diagnostic before the value is admitted.
//
// ── Axiom coverage ──────────────────────────────────────────────────
//
//   InitSafe   — all tags are empty structs; no member to leave uninit.
//   TypeSafe   — phantom tags are non-convertible.
//   NullSafe   — zero state; no pointers.
//   MemSafe    — sizeof == 1 each; EBO-collapses to 0 bytes when used
//                as [[no_unique_address]] member.
//   ThreadSafe — pure compile-time material; no runtime state.
//
// ── Runtime cost ────────────────────────────────────────────────────
//
// Zero.  Empty types; the current_arch_tag alias resolves at compile
// time via the platform's predefined macros (CLAUDE.md §XIV pins the
// supported arch list).
//
// ── References ──────────────────────────────────────────────────────
//
//   CLAUDE.md §XIV         — platform-assumption table (x86-64 + ARM)
//   safety/witness/Witness.h — PlatformBounded<W, Platforms...>

#include <crucible/Platform.h>

namespace crucible::safety::witness::arch {

// ── Phantom tags ────────────────────────────────────────────────────

struct X86_64 final {};
struct AArch64 final {};
struct RISCV final {};

static_assert(sizeof(X86_64) == 1);
static_assert(sizeof(AArch64) == 1);
static_assert(sizeof(RISCV) == 1);

// ── Compile-time arch resolution ────────────────────────────────────
//
// Resolves to the tag matching the host's predefined macros.  Crucible
// targets x86-64 + aarch64 today (CLAUDE.md §XIV).  RISC-V is reserved
// for future ports; an attempt to compile on an unsupported arch fires
// the static_assert below before any platform-bounded witness can be
// instantiated.

#if defined(__x86_64__)
using current_arch_tag = X86_64;
#elif defined(__aarch64__)
using current_arch_tag = AArch64;
#elif defined(__riscv)
using current_arch_tag = RISCV;
#else
#error "crucible::safety::witness: unsupported architecture — supported tags are X86_64 / AArch64 / RISCV (see CLAUDE.md §XIV)."
#endif

}  // namespace crucible::safety::witness::arch
