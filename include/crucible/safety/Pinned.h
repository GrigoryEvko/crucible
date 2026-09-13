#pragma once

// The two mixins impose the same prohibition and differ only in why it is
// imposed, which is what the separate names record: inherit Pinned when the
// object's address is its identity, as it is for an atomic another thread
// reaches, a pointer into the object's own storage, or a thread identity taken
// at construction; inherit NonMovable when the object holds a resource that
// must not be duplicated, and a moved-from shell would read as a live handle.
//
// They are templates on the derived type so that a rejected copy or move names
// that type.  A plain empty base would report the base instead.

#include <crucible/Platform.h>

namespace crucible::safety {

template <typename T>
class Pinned {
public:
    Pinned() = default;
    ~Pinned() = default;

    Pinned(const Pinned&) =
        delete("Pinned<T>: stable address — address-as-identity or interior pointers into own storage");
    Pinned(Pinned&&) =
        delete("Pinned<T>: stable address — move would invalidate references held by another thread or by self");
    Pinned& operator=(const Pinned&) = delete("Pinned<T>: stable address");
    Pinned& operator=(Pinned&&) = delete("Pinned<T>: stable address");
};

template <typename T>
class NonMovable {
public:
    NonMovable() = default;
    ~NonMovable() = default;

    NonMovable(const NonMovable&) =
        delete("NonMovable<T>: exclusive ownership — copy would duplicate a singleton resource");
    NonMovable(NonMovable&&) = delete(
        "NonMovable<T>: exclusive ownership — move would leave a moved-from shell that callers may mistake for valid");
    NonMovable& operator=(const NonMovable&) = delete("NonMovable<T>: exclusive ownership");
    NonMovable& operator=(NonMovable&&) = delete("NonMovable<T>: exclusive ownership");
};

}  // namespace crucible::safety
