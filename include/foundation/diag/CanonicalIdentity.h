#pragma once

// The statement that a type takes the identity of a second type in a row
// hash.  A type states it in its own definition:
//
//     using canonical_identity = foundation::diag::canonical_identity_of<Owner, Target>;
//
// lattice_canonical_id of foundation/diag/RowHash.h reads the member of the
// type itself.  No other file can add a member to a class, so no unit can
// merge two cache slots from outside.  The statement names its owner, so a
// class that derives from Owner and inherits the member keeps an identity
// of its own.  The reader reads only the two arguments of the statement, so
// a specialization of this template changes no identity.

namespace foundation::diag {

template <class Owner, class Target>
struct canonical_identity_of {};

}  // namespace foundation::diag
