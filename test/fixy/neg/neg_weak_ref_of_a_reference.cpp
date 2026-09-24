// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A WeakRef holds a pointer to an object, so its element type must be an
// object type.  A reference type has no pointer to form.  The
// requires-clause of WeakRef refuses it where the type is named, before
// any member is instantiated.  The pointer below instantiates no class.
//
// Expected diagnostic: the template constraint of WeakRef is not
// satisfied, and the note names is_object_v<T>.

#include <fixy/Borrowed.h>

int main() {
    [[maybe_unused]] ::fixy::WeakRef<int&>* slot = nullptr;
    return 0;
}
