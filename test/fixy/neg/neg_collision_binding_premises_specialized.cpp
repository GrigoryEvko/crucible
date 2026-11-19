// The verdict cell of a binding holds the premises of the binding, and no
// other cache holds them.  The variable template binding_premises_v was such
// a cache, and a unit could specialize it to give a refused pack the
// premises of an admitted pack.  No such template exists, so a forgery has
// nothing to name: this file names the old cache, and the name does not
// resolve.

#include <fixy/Reject.h>

using fixy::collision::binding_premises_v;

int main() { return 0; }
