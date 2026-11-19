// Each grade reader of the premises in fixy/Collision.h is a concept or a
// function that is not a template.  W002 reads whether a wait burns the core
// through the concept BurnsTheCore.  This file tries to say that a kernel
// block burns the core, so that W002 refuses a binding that blocks: it
// writes a specialization of the reader.  A concept has no specialization,
// so the file does not compile.

#include <fixy/Collision.h>

template <>
inline constexpr bool fixy::collision::detail::BurnsTheCore<fixy::atom::sync::block> = true;

int main() { return 0; }
