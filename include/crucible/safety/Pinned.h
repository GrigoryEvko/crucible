#pragma once

// ── crucible::safety::Pinned<T> / NonMovable<T> ─────────────────────
//
// CRTP mixins that delete copy and move, with named reasons.  Encodes
// "this object has stable address" as a compile-time property rather
// than a convention-only comment.
//
//   Axiom coverage: BorrowSafe, MemSafe.
//   Runtime cost:   zero.  The deleted operations are compile-time
//                   forbids; no vtable or runtime check.
//
// Distinction:
//   Pinned<T>      — stable address AND stable contents.  Used for
//                    types containing atomics, mutexes, thread
//                    identifiers, or self-referential pointers where
//                    the address IS the identity (SPSC rings,
//                    allocator bases, Vigil/Cipher instances).
//   NonMovable<T>  — stable address only.  Copies would create two
//                    owners of an exclusive resource; moves would
//                    leave dangling interior pointers.  Contents may
//                    otherwise mutate freely (e.g. through members).
//
// These are orthogonal: Pinned subsumes NonMovable semantically, but
// the two names are kept so audit grep distinguishes the intent.
//
// Usage:
//   struct MyAtomicRing : crucible::safety::Pinned<MyAtomicRing> { ... };
//   struct MyOwner      : crucible::safety::NonMovable<MyOwner>   { ... };

#include <crucible/Platform.h>

namespace crucible::safety {

// Pinned<T>: no copy, no move. Address is stable for the object's
// entire lifetime.  Intended for types containing state that MUST NOT
// be relocated:
//   - std::atomic<...> on cross-thread handoff paths
//   - self-referential pointers (interior pointers into own buffer)
//   - thread identifiers captured at construction
//   - lock-free rings where consumer holds a pointer-into-storage
//
// Why CRTP: inheritance by value-types produces cleaner diagnostics
// than inheritance by empty non-template base — the deleted op= error
// names the derived type, not a generic "Pinned base".
template <typename T>
class Pinned {
public:
    Pinned()  = default;
    ~Pinned() = default;

    Pinned(const Pinned&)            = delete("Pinned<T>: stable address — address-as-identity or interior pointers into own storage");
    Pinned(Pinned&&)                 = delete("Pinned<T>: stable address — move would invalidate references held by another thread or by self");
    Pinned& operator=(const Pinned&) = delete("Pinned<T>: stable address");
    Pinned& operator=(Pinned&&)      = delete("Pinned<T>: stable address");
};

// NonMovable<T>: no copy, no move. Distinct from Pinned only in
// intent: a copy would duplicate an exclusive resource (fd, thread
// handle, arena backing pointer), a move would leave a valid-but-
// empty shell that later users might mistake for an owned handle.
//
// Use NonMovable when the resource ownership is the reason for the
// prohibition; use Pinned when the identity (address) is the reason.
template <typename T>
class NonMovable {
public:
    NonMovable()  = default;
    ~NonMovable() = default;

    NonMovable(const NonMovable&)            = delete("NonMovable<T>: exclusive ownership — copy would duplicate a singleton resource");
    NonMovable(NonMovable&&)                 = delete("NonMovable<T>: exclusive ownership — move would leave a moved-from shell that callers may mistake for valid");
    NonMovable& operator=(const NonMovable&) = delete("NonMovable<T>: exclusive ownership");
    NonMovable& operator=(NonMovable&&)      = delete("NonMovable<T>: exclusive ownership");
};

// Zero-cost: Empty-base optimization guarantees the mixin adds no
// bytes when the derived class has other members.  Verified by the
// consumer types' own sizeof static_asserts.

} // namespace crucible::safety
