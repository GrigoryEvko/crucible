// An element of a temporary log ends with the full expression, so a temporary
// log gives no element.

#include <fixy/Mutation.h>

int main() { return fixy::mint_append_only<int>()[0]; }
