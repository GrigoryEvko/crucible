// A001: a no-allocation grade x Alloc in the row.
//
// The Stack grade of AllocClass claims that no allocator ran.  The atom
// with<Alloc> puts Alloc in the row of the binding.  The row is the one
// source of the fact, so the band claims more than the row, and the pack
// trips A001 alone.

#include <fixy/Bands.h>
#include <fixy/Fn.h>

int main() {
    // The size instantiates the class, and a band payload has no default
    // constructor.  So the size and not a default binding is the use, and
    // the refusal of the class body is the only error.
    static_cast<void>(
        sizeof(::fixy::fn<::fixy::alloc_class::Stack<int>, ::fixy::atom::with<::foundation::effects::Effect::Alloc>>));
    return 0;
}
