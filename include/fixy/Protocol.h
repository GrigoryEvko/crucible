#pragma once

// The upper part of the Protocol family in namespace fixy: the hash, the
// byte codec and the format of a value, derived from the parts of its type
// (foundation/core/Protocol.h).  fixy/Core.h gives the lower part: equal,
// order, and the marks derive_protocol, local_part and pad_part.
//
// Only a unit that hashes, encodes, decodes or formats a value of the family
// includes this header, so each other unit of the tree does not parse the
// walks of the upper part.
//
// decode of the bits of a value (fixy/Core.h) and decode of an image are one
// overload set, as are format of a Fmt and format of a value.  A
// using-declaration names only the overloads that the unit has seen at its
// place, so each using-declaration here comes after the include of the
// family and names the overloads of the two headers.

#include <fixy/Core.h>
#include <foundation/core/Protocol.h>

namespace fixy {

using ::foundation::core::Decodable;
using ::foundation::core::decode;
using ::foundation::core::DecodeFault;
using ::foundation::core::DecodeRefusal;
using ::foundation::core::Encodable;
using ::foundation::core::encode;
using ::foundation::core::encoded_size_v;
using ::foundation::core::format;
using ::foundation::core::hash;
using ::foundation::core::Hashable;

}  // namespace fixy
