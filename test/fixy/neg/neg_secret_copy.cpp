// Copying a Secret<T> would duplicate a classified value without an
// audit trail.  The copy constructor is deleted with a reason, and the
// compiler names it and repeats that reason.

#include <fixy/Secret.h>

#include <utility>

int main() {
    fixy::Secret<int> key = fixy::mint_secret<int>(7);
    fixy::Secret<int> duplicate = key;
    return std::move(duplicate).declassify<fixy::tags::secret_policy::HashForCompare>();
}
