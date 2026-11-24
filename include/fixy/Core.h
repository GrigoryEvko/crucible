#pragma once

// The primitive families in namespace fixy.
//
// The families live in include/foundation/core/, in namespace
// foundation::core, because the foundation layers use them too.  The list
// of types Pack lives in include/foundation/reflect/TypeList.h, in
// namespace foundation::reflect.  This header puts each public name of the
// families into namespace fixy, so
// code outside the base spells fixy::Option and fixy::none.  Each line
// is a using-declaration of the one entity, so the fixy name and the
// foundation name are the same entity and not a second type.
//
// A specialization of a family template, for example of
// foundation::core::niche or foundation::core::atomic_word, names the
// template in namespace foundation::core.  A specialization through a
// using-declaration is not valid C++.

#include <foundation/alloc/Layout.h>
#include <foundation/core/Algorithms.h>
#include <foundation/core/Arithmetic.h>
#include <foundation/core/Array.h>
#include <foundation/core/Atomic.h>
#include <foundation/core/AtomicCells.h>
#include <foundation/core/BitSet.h>
#include <foundation/core/Bits.h>
#include <foundation/core/Branded.h>
#include <foundation/core/BumpArena.h>
#include <foundation/core/CallRef.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Compare.h>
#include <foundation/core/Convert.h>
#include <foundation/core/DataAddress.h>
#include <foundation/core/FixedHeapArray.h>
#include <foundation/core/Float.h>
#include <foundation/core/Format.h>
#include <foundation/core/Frame.h>
#include <foundation/core/Grow.h>
#include <foundation/core/Id.h>
#include <foundation/core/Index.h>
#include <foundation/core/Link.h>
#include <foundation/core/List.h>
#include <foundation/core/Number.h>
#include <foundation/core/OneOf.h>
#include <foundation/core/Owner.h>
#include <foundation/core/PackedWord.h>
#include <foundation/core/Parse.h>
#include <foundation/core/Philox.h>
#include <foundation/core/Quantity.h>
#include <foundation/core/Record.h>
#include <foundation/core/Ref.h>
#include <foundation/core/Region.h>
#include <foundation/core/Report.h>
#include <foundation/core/RunSet.h>
#include <foundation/core/Scalar.h>
#include <foundation/core/Side.h>
#include <foundation/core/SlotMap.h>
#include <foundation/core/SmallList.h>
#include <foundation/core/StorageFloat.h>
#include <foundation/core/Strided.h>
#include <foundation/core/Text.h>
#include <foundation/core/Values.h>
#include <foundation/reflect/TypeList.h>

namespace fixy {

using ::foundation::core::Align;
using ::foundation::core::append_hex;
using ::foundation::core::fatal;
using ::foundation::core::Fixed;
using ::foundation::core::Fmt;
using ::foundation::core::format;
using ::foundation::core::Formattable;
using ::foundation::core::Hex;
using ::foundation::core::HexBytes;
using ::foundation::core::Padded;
using ::foundation::core::parse;
using ::foundation::core::ParseError;
using ::foundation::core::ParseErrorKind;
using ::foundation::core::report;
using ::foundation::core::SignMark;
using ::foundation::core::Sink;
using ::foundation::core::Site;
using ::foundation::core::unreachable;

// The door foreign_c_str of a CText stays in foundation::core.  A call stands
// only in an ABI region, and the full name shows the reader the escape.
using ::foundation::core::CText;
using ::foundation::core::enum_name_text;
using ::foundation::core::enum_words_text;
using ::foundation::core::identifier_text;
using ::foundation::core::enumerator_name_text;
using ::foundation::core::enumerator_named;
using ::foundation::core::for_each_enumerator_text;
using ::foundation::core::has_enumerator_named;
using ::foundation::core::unknown_enum_text;
using ::foundation::core::FixedText;
using ::foundation::core::ForeignText;
using ::foundation::core::freeze;
using ::foundation::core::GrowText;
using ::foundation::core::HeapText;
using ::foundation::core::mint_c_text;
using ::foundation::core::mint_foreign_text;
using ::foundation::core::mint_grow_text;
using ::foundation::core::mint_static_text;
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

using ::foundation::core::Record;
using ::foundation::reflect::Pack;
using ::foundation::reflect::pack_apply_t;
using ::foundation::reflect::pack_at_t;
using ::foundation::reflect::pack_concat_t;
using ::foundation::reflect::pack_contains_v;
using ::foundation::reflect::pack_index_of_v;
using ::foundation::reflect::pack_size_v;
using ::foundation::reflect::pack_unique_v;

// The lower part of the Protocol family: the gates of equality and order and
// the marks of a class and of a member.  equal of a View, of a text and of a
// value is one overload set, as is order.  fixy/Protocol.h gives the upper
// part: hash, encode, decode and format of a value.
using ::foundation::core::derive_protocol;
using ::foundation::core::Equatable;
using ::foundation::core::local_part;
using ::foundation::core::order;
using ::foundation::core::Orderable;
using ::foundation::core::pad_part;

// The core of an owner of storage, and the marker that its state carries.
// An owner of each layer derives from the core (foundation/core/Owner.h).
using ::foundation::core::OwnerCore;
using ::foundation::core::OwnerState;
using ::foundation::core::OwnerStateMark;

using ::foundation::core::Box;
using ::foundation::core::box_word_key;
using ::foundation::core::BoxWord;
using ::foundation::core::ArenaBlockSize;
using ::foundation::core::ArenaPosition;
using ::foundation::core::ArenaRun;
using ::foundation::core::RunStart;
using ::foundation::core::BumpArena;
using ::foundation::core::EpochArena;
using ::foundation::core::EpochOf;
using ::foundation::core::mint_bump_arena;
using ::foundation::core::mint_epoch_arena;
using ::foundation::core::with_bump_arena;
using ::foundation::core::CallRef;
using ::foundation::core::claim_box;
using ::foundation::core::Hole;
using ::foundation::core::mint_box;
using ::foundation::core::mint_hole;
using ::foundation::core::Address;
using ::foundation::core::address_of;
using ::foundation::core::as_ref;
using ::foundation::core::AddressKey;
using ::foundation::core::AddressKeyTarget;
using ::foundation::core::mint_address_key;
using ::foundation::core::prefetch;
using ::foundation::core::prefetch_byte;
using ::foundation::core::PrefetchAccess;
using ::foundation::core::Link;
using ::foundation::core::Ref;
using ::foundation::core::RefMut;
using ::foundation::core::erase_brand;
using ::foundation::core::ErasableBrand;
using ::foundation::core::same_object;
using ::foundation::core::same_target;
using ::foundation::core::to;

// The door foreign_pointer of a RunAddress stays in foundation::core, as
// foreign_c_str does.
using ::foundation::core::DataAddress;
using ::foundation::core::DeviceIdentity;
using ::foundation::core::mint_foreign_run_address;
using ::foundation::core::mint_run_address;
using ::foundation::core::RunAddress;

using ::foundation::core::Atomic;
using ::foundation::core::AtomicBits;
using ::foundation::core::AtomicCell;
using ::foundation::core::AtomicCount;
using ::foundation::core::AtomicValue;
using ::foundation::core::atomic_word_key;
using ::foundation::core::AtomicWordProtocol;
using ::foundation::core::CacheLine;
using ::foundation::core::CasRefusal;
using ::foundation::core::Counter;
using ::foundation::core::fence_seq_cst;
using ::foundation::core::mint_sole_writer;
using ::foundation::core::share_atomic_cells;
using ::foundation::core::SoleWriter;
using ::foundation::core::Tally;

// The scans of a run are the terminals any_of and all_of of a walk:
// fixy::all_of and fixy::any_of are the combinators of the refinement
// predicates of fixy/Predicates.h.
using ::foundation::core::BitId;
using ::foundation::core::BitSet;
using ::foundation::core::BrandedIndex;
using ::foundation::core::BrandedView;
using ::foundation::core::ByteImageable;
using ::foundation::core::bytes;
using ::foundation::core::classify_overlap;
using ::foundation::core::copy;
using ::foundation::core::dynamic_extent;
using ::foundation::core::ends_with;
using ::foundation::core::equal;
using ::foundation::core::fill;
using ::foundation::core::find;
using ::foundation::core::find_if;
using ::foundation::core::find_lower_bound_by_key;
using ::foundation::core::for_each_chunk;
using ::foundation::core::for_each_chunk_pair;
using ::foundation::core::FixedArray;
using ::foundation::core::FixedHeapArray;
using ::foundation::core::FixedList;
using ::foundation::core::ForeignElement;
using ::foundation::core::Full;
using ::foundation::core::HeapArray;
using ::foundation::core::HeapBitSet;
using ::foundation::core::Index;
using ::foundation::core::index;
using ::foundation::core::IndexCount;
using ::foundation::core::IndexRange;
using ::foundation::core::is_window_of;
using ::foundation::core::LengthMismatch;
using ::foundation::core::MaskedArray;
using ::foundation::core::mint_branded_scope;
using ::foundation::core::mint_foreign_view;
using ::foundation::core::offset_of;
using ::foundation::core::mint_fixed_heap_array;
using ::foundation::core::mint_heap_array;
using ::foundation::core::mint_heap_array_copy;
using ::foundation::core::mint_heap_array_with;
using ::foundation::core::mint_heap_array_written;
using ::foundation::core::mint_heap_array_zeroed;
using ::foundation::core::mint_bit_set_from_bits;
using ::foundation::core::mint_heap_bit_set;
using ::foundation::core::mint_masked_array;
using ::foundation::core::mint_slot_map;
using ::foundation::core::mint_small_list;
using ::foundation::core::Ranged;
using ::foundation::core::reborrow;
using ::foundation::core::RunOf;
using ::foundation::core::RunOverlap;
using ::foundation::core::RunSet;
using ::foundation::core::SlotKey;
using ::foundation::core::SlotMap;
using ::foundation::core::SmallList;
using ::foundation::core::split_at;
using ::foundation::core::split_chunks;
using ::foundation::core::starts_with;
using ::foundation::core::StaticIndex;
using ::foundation::core::take_prefix_up_to;
using ::foundation::core::take_window;
using ::foundation::core::View;
using ::foundation::core::ViewMut;
using ::foundation::core::ViewMutChunks;
using ::foundation::core::Walk;
using ::foundation::core::with_mut;
using ::foundation::core::write_each;
using ::foundation::core::WindowPartText;
using ::foundation::core::WindowWholeText;
using ::foundation::core::WriteLease;

using ::foundation::core::BrandedStridedView;
using ::foundation::core::dynamic_rank;
using ::foundation::core::max_strided_count;
using ::foundation::core::max_strided_rank;
using ::foundation::core::mint_strided_scope;
using ::foundation::core::mint_strided_view;
using ::foundation::core::mint_strided_view_mut;
using ::foundation::core::ReshapeError;
using ::foundation::core::ShapeMismatch;
using ::foundation::core::StridedIndex;
using ::foundation::core::StridedView;
using ::foundation::core::StridedViewMut;
using ::foundation::core::walk;

using ::foundation::core::bit_cast;
using ::foundation::core::BitValid;
using ::foundation::core::DecodableFrom;
using ::foundation::core::decode;
using ::foundation::core::enum_at;
using ::foundation::core::enum_from;
using ::foundation::core::enum_from_field;
using ::foundation::core::Enumerated;
using ::foundation::core::FullField;
using ::foundation::core::for_each_base;
using ::foundation::core::for_each_field;

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

using ::foundation::core::checked_abs;
using ::foundation::core::checked_add;
using ::foundation::core::checked_div;
using ::foundation::core::checked_mul;
using ::foundation::core::checked_mul_div;
using ::foundation::core::checked_neg;
using ::foundation::core::checked_rem;
using ::foundation::core::checked_shl;
using ::foundation::core::checked_shr;
using ::foundation::core::checked_sub;
using ::foundation::core::div_ceil;
using ::foundation::core::gcd;
using ::foundation::core::mul_div_floor;
using ::foundation::core::saturating_add;
using ::foundation::core::saturating_div;
using ::foundation::core::saturating_mul;
using ::foundation::core::saturating_sub;
using ::foundation::core::Strict;
using ::foundation::core::strict_add;
using ::foundation::core::strict_div;
using ::foundation::core::strict_mul;
using ::foundation::core::strict_rem;
using ::foundation::core::strict_shl;
using ::foundation::core::strict_shr;
using ::foundation::core::strict_sub;
using ::foundation::core::WideProduct;
using ::foundation::core::widening_mul;
using ::foundation::core::wrapping_add;
using ::foundation::core::wrapping_mul;
using ::foundation::core::wrapping_neg;
using ::foundation::core::wrapping_shl;
using ::foundation::core::wrapping_shr;
using ::foundation::core::wrapping_sub;

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

// The storage formats of a floating value, and their rounds from float.
// widen above gives their value as a float or a double.
using ::foundation::core::bf16;
using ::foundation::core::f16;
using ::foundation::core::fp8_e4m3;
using ::foundation::core::fp8_e5m2;
using ::foundation::core::round_nonsaturating;
using ::foundation::core::round_saturating;
using ::foundation::core::StorageFloat;
using ::foundation::core::StorageFloatOf;

using ::foundation::core::align_up;
using ::foundation::core::bit_ceil;
using ::foundation::core::bit_floor;
using ::foundation::core::bit_width;
using ::foundation::core::byteswap;
using ::foundation::core::countl_one;
using ::foundation::core::countl_zero;
using ::foundation::core::countr_one;
using ::foundation::core::countr_zero;
using ::foundation::core::has_single_bit;
using ::foundation::core::ilog2;
using ::foundation::core::low_mask;
using ::foundation::core::popcount;
using ::foundation::core::rotl;
using ::foundation::core::rotr;
using ::foundation::core::strict_align_up;

// A word of named bit fields (foundation/core/PackedWord.h).
using ::foundation::core::BitField;
using ::foundation::core::FieldTag;
using ::foundation::core::NamesOneField;
using ::foundation::core::PackedLayout;
using ::foundation::core::PackedWord;

using ::foundation::core::as_const;
using ::foundation::core::exchange;
using ::foundation::core::Exchangeable;

using ::foundation::core::Digest;
using ::foundation::core::Id;

// The role of an argument as a type (foundation/core/Side.h).  The roles of
// the base keep their namespace side.
using ::foundation::core::Side;
using ::foundation::core::side_as;
using ::foundation::core::SidePayload;
using ::foundation::core::SideTag;
namespace side = ::foundation::core::side;

using ::foundation::core::Philox4x32;

// The arithmetic of a Quantity is a set of overloads in foundation::core,
// which argument-dependent lookup finds: checked_*, wrapping_* and
// saturating_* for an Integer Rep, and add, subtract, multiply and divide for
// a Float Rep.  The units, Product and Quotient keep their namespace unit.
using ::foundation::core::ByteCount;
using ::foundation::core::convert;

// The layout of an allocation: a count of bytes above zero at an Alignment
// that is a power of two (the layout of foundation/alloc/Layout.h).
using ::foundation::alloc::Alignment;
using ::foundation::alloc::alignment_of;
using ::foundation::alloc::bytes_of;
using ::foundation::alloc::Layout;
using ::foundation::alloc::LayoutError;
using ::foundation::alloc::LayoutResult;
using ::foundation::alloc::mint_alignment;
using ::foundation::alloc::mint_layout;

// The 64 size classes of the allocator of the base, their slabs and their
// bins (the size classes of foundation/alloc/Layout.h).
using ::foundation::alloc::bin_capacity;
using ::foundation::alloc::class_for;
using ::foundation::alloc::is_size_class;
using ::foundation::alloc::kSizeClassCount;
using ::foundation::alloc::SizeClass;
using ::foundation::alloc::slab_kind_of;
using ::foundation::alloc::slab_slots;
using ::foundation::alloc::SlabKind;
using ::foundation::alloc::slot_bytes;

using ::foundation::core::ElementCount;
using ::foundation::core::Quantity;
using ::foundation::core::QuantityUnit;
using ::foundation::core::round_to_float;
namespace unit = ::foundation::core::unit;

// The object swap stays in an inline namespace, because classes of namespace
// fixy, such as fixy::Qtt, declare a hidden friend named swap.
inline namespace values {
using ::foundation::core::swap;
}  // namespace values

// The operations of a floating value keep their namespace fp.
namespace fp {
using ::foundation::core::fp::abs;
using ::foundation::core::fp::canonicalize_nan;
using ::foundation::core::fp::copy_sign;
using ::foundation::core::fp::fma;
using ::foundation::core::fp::is_finite;
using ::foundation::core::fp::is_inf;
using ::foundation::core::fp::is_nan;
using ::foundation::core::fp::is_normal;
using ::foundation::core::fp::sign_bit;
using ::foundation::core::fp::sqrt;
using ::foundation::core::fp::total_key;
}  // namespace fp

}  // namespace fixy
