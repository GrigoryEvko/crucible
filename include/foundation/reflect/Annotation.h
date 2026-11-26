#pragma once

// The read of an annotation of an entity (P3394).
//
//   carries_annotation(entity, type)   true when the entity carries an
//                                      annotation of the type
//   has_annotations(entity)            true when the entity carries an
//                                      annotation
//   template_annotations_of(entity, class_template)
//                                      the count of the annotations whose type
//                                      is a specialization of the template,
//                                      and the type of the first one
//   argument_bits_of(specialization)   the value arguments of type
//                                      std::size_t below 64, one bit each
//
// The language gives the annotations and the template arguments only as a
// std::vector of a constant evaluation.  This header holds the loops over
// them, so a header above it names no std entity to read a mark.
//
// The marks of the base are annotations, and the walks of the base ask this of
// each class that they read.  The query gives a list, and the read asks only
// for its size.  A read through a constant array of the list makes one entity
// for each array, and it costs about four times as many compile instructions.
// A walk that asks for many marks of one class reads has_annotations first.
// The header includes only <meta>, so each layer above it can read a mark at
// no cost of a different header.

#include <meta>

namespace foundation::reflect {

// True when the entity carries an annotation of a type.  The entity is
// complete.  Complexity: linear in the number of annotations of the entity.
[[nodiscard]] consteval bool has_annotations(std::meta::info entity) {
    return std::meta::annotations_of(entity).size() != 0;
}

// True when the entity carries an annotation of the type.  The entity is
// complete.  Complexity: linear in the number of annotations of the entity.
[[nodiscard]] consteval bool carries_annotation(std::meta::info entity, std::meta::info annotation_type) {
    return std::meta::annotations_of_with_type(entity, annotation_type).size() != 0;
}

// The annotations of an entity whose type is a specialization of one class
// template, such as the positions of by_arguments of foundation/Transfer.h.
struct TemplateAnnotations {
    std::size_t count = 0;
    std::meta::info first_type{};
};

// The annotations of the entity whose type is a specialization of the class
// template.  The entity is complete.  Complexity: linear in the number of
// annotations of the entity.
[[nodiscard]] consteval TemplateAnnotations template_annotations_of(std::meta::info entity,
                                                                    std::meta::info class_template) {
    TemplateAnnotations found{};
    for (const std::meta::info annotation : std::meta::annotations_of(entity)) {
        const std::meta::info type = std::meta::dealias(std::meta::remove_cv(std::meta::type_of(annotation)));
        if (!std::meta::has_template_arguments(type) || std::meta::template_of(type) != class_template) continue;
        if (found.count == 0) found.first_type = type;
        ++found.count;
    }
    return found;
}

// The annotations of an entity whose type is one class, such as the
// statements of refers_deep of foundation/Transfer.h: their count, and the
// first one, whose value std::meta::extract reads.
struct TypedAnnotations {
    std::size_t count = 0;
    std::meta::info first{};
};

// The annotations of the entity whose type is the class.  The read goes
// through a constant array, so it calls no member of the container that the
// query gives, as is_open_extension of foundation/OpenExtension.h does.  The
// entity is complete.  Complexity: linear in the number of annotations of the
// entity.
[[nodiscard]] consteval TypedAnnotations typed_annotations_of(std::meta::info entity, std::meta::info annotation_type) {
    const std::meta::info found =
        std::meta::reflect_constant_array(std::meta::annotations_of_with_type(entity, annotation_type));
    const std::meta::info found_type = std::meta::type_of(found);
    if (!std::meta::is_array_type(found_type) || std::meta::extent(found_type) == 0) return TypedAnnotations{};
    return TypedAnnotations{std::meta::extent(found_type), *std::meta::extract<std::meta::info const*>(found)};
}

// The value arguments of the specialization whose type is std::size_t and
// whose value is below 64, one bit for each value.  Each other argument gives
// no bit.  Complexity: linear in the number of template arguments.
[[nodiscard]] consteval unsigned long long argument_bits_of(std::meta::info specialization) {
    unsigned long long bits = 0;
    for (const std::meta::info argument : std::meta::template_arguments_of(specialization)) {
        if (!std::meta::is_value(argument)) continue;
        if (!std::meta::is_same_type(std::meta::type_of(argument), ^^std::size_t)) continue;
        const std::size_t value = std::meta::extract<std::size_t>(argument);
        if (value < 64) bits |= 1ULL << value;
    }
    return bits;
}

}  // namespace foundation::reflect
