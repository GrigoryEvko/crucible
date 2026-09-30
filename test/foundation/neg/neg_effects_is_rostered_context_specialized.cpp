// IsContext asks is_rostered_context whether a type is one of the three
// contexts of the roster.  This file tries to make each type a context: it
// writes an explicit specialization of is_rostered_context.
// is_rostered_context is a function at namespace scope that is not a
// template, so no specialization matches it.

#include <foundation/effects/Effect.h>

#include <meta>

template <>
consteval bool foundation::effects::is_rostered_context(std::meta::info) {
    return true;
}

int main() { return 0; }
