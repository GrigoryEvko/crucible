#pragma once

// The primitive families in namespace fixy.
//
// The families live in include/foundation/core/, in namespace
// foundation::core, because the foundation layers use them too.  This
// header puts each public name of the families into namespace fixy, so
// code outside the base spells fixy::Option and fixy::none.  Each line
// is a using-declaration of the one entity, so the fixy name and the
// foundation name are the same entity and not a second type.
//
// A specialization of a family template, for example of
// foundation::core::niche, names the template in namespace
// foundation::core.  A specialization through a using-declaration is not
// valid C++.

#include <foundation/core/Algorithms.h>
#include <foundation/core/Arithmetic.h>
#include <foundation/core/Array.h>
#include <foundation/core/Atomic.h>
#include <foundation/core/Bits.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Compare.h>
#include <foundation/core/Convert.h>
#include <foundation/core/Format.h>
#include <foundation/core/Index.h>
#include <foundation/core/Number.h>
#include <foundation/core/OneOf.h>
#include <foundation/core/Parse.h>
#include <foundation/core/Record.h>
#include <foundation/core/Ref.h>
#include <foundation/core/Region.h>
#include <foundation/core/Report.h>
#include <foundation/core/Scalar.h>
#include <foundation/core/Text.h>

namespace fixy {

using ::foundation::core::fatal;
using ::foundation::core::Fmt;
using ::foundation::core::format;
using ::foundation::core::Formattable;
using ::foundation::core::parse;
using ::foundation::core::ParseError;
using ::foundation::core::ParseErrorKind;
using ::foundation::core::report;
using ::foundation::core::Sink;
using ::foundation::core::Site;
using ::foundation::core::Truncated;
using ::foundation::core::unreachable;

using ::foundation::core::CText;
using ::foundation::core::FixedText;
using ::foundation::core::TextView;

using ::foundation::core::niche_key;
using ::foundation::core::none;
using ::foundation::core::NoValue;
using ::foundation::core::Option;
using ::foundation::core::some;

using ::foundation::core::Err;
using ::foundation::core::err;
using ::foundation::core::Result;
using ::foundation::core::Unit;

using ::foundation::core::OneOf;

using ::foundation::core::Pack;
using ::foundation::core::pack_apply_t;
using ::foundation::core::pack_at_t;
using ::foundation::core::pack_concat_t;
using ::foundation::core::pack_contains_v;
using ::foundation::core::pack_index_of_v;
using ::foundation::core::pack_size_v;
using ::foundation::core::pack_unique_v;
using ::foundation::core::Record;

using ::foundation::core::Box;
using ::foundation::core::mint_box;
using ::foundation::core::Address;
using ::foundation::core::address_of;
using ::foundation::core::Ref;
using ::foundation::core::same_object;
using ::foundation::core::to;

using ::foundation::core::Atomic;
using ::foundation::core::AtomicBits;
using ::foundation::core::AtomicCount;
using ::foundation::core::AtomicValue;
using ::foundation::core::CacheLine;
using ::foundation::core::CasRefusal;
using ::foundation::core::mint_sole_writer;
using ::foundation::core::SoleWriter;
using ::foundation::core::Tally;

// The scans all_of and any_of of the Region family stay in foundation::core:
// fixy::all_of and fixy::any_of are the combinators of the refinement
// predicates of fixy/Predicates.h.
using ::foundation::core::ByteImageable;
using ::foundation::core::bytes;
using ::foundation::core::copy;
using ::foundation::core::count_if;
using ::foundation::core::dynamic_extent;
using ::foundation::core::ends_with;
using ::foundation::core::equal;
using ::foundation::core::fill;
using ::foundation::core::find;
using ::foundation::core::find_if;
using ::foundation::core::HeapArray;
using ::foundation::core::Index;
using ::foundation::core::index;
using ::foundation::core::IndexCount;
using ::foundation::core::IndexRange;
using ::foundation::core::LengthMismatch;
using ::foundation::core::lower_bound_by_key;
using ::foundation::core::mint_heap_array;
using ::foundation::core::starts_with;
using ::foundation::core::StaticIndex;
using ::foundation::core::View;
// FixedArray of the Region family stays in foundation::core until the merge of
// fixy/FixedArray.h, which declares a fixy::FixedArray of its own.

using ::foundation::core::decode;
using ::foundation::core::enum_from;

using ::foundation::core::Arithmetic;
using ::foundation::core::fits_each_value;
using ::foundation::core::Float;
using ::foundation::core::Integer;
using ::foundation::core::SignedInteger;
using ::foundation::core::UnsignedInteger;

using ::foundation::core::bits_of;
using ::foundation::core::epsilon_of;
using ::foundation::core::infinity_of;
using ::foundation::core::max_of;
using ::foundation::core::min_normal_of;
using ::foundation::core::min_of;
using ::foundation::core::min_subnormal_of;
using ::foundation::core::quiet_nan_of;
using ::foundation::core::signaling_nan_of;
using ::foundation::core::value_bits_of;

// The three arithmetic sets checked_*, wrapping_* and saturating_* stay out
// of namespace fixy, because fixy/Checked.h declares the same names there
// with the same parameters.
using ::foundation::core::gcd;
using ::foundation::core::WideProduct;
using ::foundation::core::widening_mul;

using ::foundation::core::abs_diff;
using ::foundation::core::Bounds;
using ::foundation::core::bounds;
using ::foundation::core::clamp;
using ::foundation::core::compare;
using ::foundation::core::fits;
using ::foundation::core::max;
using ::foundation::core::min;
using ::foundation::core::unsigned_abs;

using ::foundation::core::narrow;
using ::foundation::core::saturating_narrow;
using ::foundation::core::widen;
using ::foundation::core::wrapping_narrow;

using ::foundation::core::bit_ceil;
using ::foundation::core::bit_floor;
using ::foundation::core::bit_width;
using ::foundation::core::byteswap;
using ::foundation::core::countl_one;
using ::foundation::core::countl_zero;
using ::foundation::core::countr_one;
using ::foundation::core::countr_zero;
using ::foundation::core::has_single_bit;
using ::foundation::core::low_mask;
using ::foundation::core::popcount;
using ::foundation::core::rotl;
using ::foundation::core::rotr;

}  // namespace fixy
