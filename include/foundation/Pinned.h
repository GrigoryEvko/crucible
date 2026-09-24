#pragma once

// One prohibition under two names, and the names record why it is
// imposed: inherit Pinned when the object's address is its identity, as
// it is for an atomic another thread reaches, a pointer into the
// object's own storage, or a thread identity taken at construction;
// inherit NonMovable when the object holds a resource that must not be
// duplicated, and a moved-from shell would read as a live handle.
//
// It is a template on the derived type so that a rejected copy or move
// names that type.  A plain empty base would report the base instead.

#include <foundation/Platform.h>

namespace foundation {

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
using NonMovable = Pinned<T>;

}  // namespace foundation
