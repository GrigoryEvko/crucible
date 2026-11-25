#pragma once

// The read of an annotation of an entity (P3394).
//
//   carries_annotation(entity, type)   true when the entity carries an
//                                      annotation of the type
//
// The marks of the base are annotations, and the walks of the base ask this of
// each class that they read.  The query gives a list, and the read asks only
// for its size.  A read through a constant array of the list makes one entity
// for each array, and it costs about four times as many compile instructions.
// The header includes only <meta>, so each layer above it can read a mark at
// no cost of a different header.

#include <meta>

namespace foundation::reflect {

// True when the entity carries an annotation of the type.  The entity is
// complete.  Complexity: linear in the number of annotations of the entity.
[[nodiscard]] consteval bool carries_annotation(std::meta::info entity, std::meta::info annotation_type) {
    return std::meta::annotations_of_with_type(entity, annotation_type).size() != 0;
}

}  // namespace foundation::reflect
