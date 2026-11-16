#pragma once

// The family of the deterministic reductions, scans and sorts in namespace
// fixy.
//
// The library lives in foundation/core/Reduce.h, Scan.h and Sort.h, in
// namespace foundation::core.  This header puts each public name of the three
// headers into namespace fixy, so code outside the base spells
// fixy::sum_by_tree.  Each line is a using-declaration of the one entity, as
// in fixy/Core.h.  The parallel forms live in fixy/concurrent/ParallelTree.h,
// in namespace fixy::concurrent, and that header includes this one.  The
// layer rule keeps this header from including a header of fixy/concurrent.
//
// fixy/Core.h does not include this header, so a unit that uses no
// reduction, scan or sort does not parse the library.

#include <foundation/core/Reduce.h>
#include <foundation/core/Scan.h>
#include <foundation/core/Sort.h>

namespace fixy {

// The tree reduce and the tree sums (foundation/core/Reduce.h).
using ::foundation::core::check_segment_offsets;
using ::foundation::core::count_tree_depth;
using ::foundation::core::count_tree_grains;
using ::foundation::core::evaluate_tree_plan;
using ::foundation::core::max_plan_leaves;
using ::foundation::core::plan_tree;
using ::foundation::core::PlanError;
using ::foundation::core::PlanNode;
using ::foundation::core::reduce_by_tree;
using ::foundation::core::reduce_grains_by_tree;
using ::foundation::core::reduce_segments_by_tree;
using ::foundation::core::RunsError;
using ::foundation::core::SegmentError;
using ::foundation::core::sum_by_tree;
using ::foundation::core::sum_by_tree_in_lanes;
using ::foundation::core::sum_grains_by_tree;
using ::foundation::core::sum_runs_by_tree;
using ::foundation::core::sum_segments_by_tree;
using ::foundation::core::TreeCombine;
using ::foundation::core::TreeGrain;
using ::foundation::core::TreeStep;
using ::foundation::core::TreeSumElement;
using ::foundation::core::TreeValue;
using ::foundation::core::write_tree_plan;

// The scans (foundation/core/Scan.h).
using ::foundation::core::count_exclusive_scan_depth;
using ::foundation::core::count_inclusive_scan_depth;
using ::foundation::core::count_scan_plan_steps;
using ::foundation::core::empty_plan_node;
using ::foundation::core::evaluate_scan_plan;
using ::foundation::core::scan_exclusive;
using ::foundation::core::scan_exclusive_grains;
using ::foundation::core::scan_exclusive_sum_grains;
using ::foundation::core::scan_exclusive_sums;
using ::foundation::core::scan_exclusive_sums_in_lanes;
using ::foundation::core::scan_inclusive;
using ::foundation::core::scan_inclusive_grains;
using ::foundation::core::scan_inclusive_sum_grains;
using ::foundation::core::scan_inclusive_sums;
using ::foundation::core::scan_inclusive_sums_in_lanes;
using ::foundation::core::ScanKind;
using ::foundation::core::write_scan_plan;

// The sorts and the ordered searches (foundation/core/Sort.h).
using ::foundation::core::Ascending;
using ::foundation::core::AscendingElement;
using ::foundation::core::Comparator;
using ::foundation::core::encode_total_order_key;
using ::foundation::core::find_equivalent;
using ::foundation::core::find_lower_bound;
using ::foundation::core::NetworkCount;
using ::foundation::core::plan_sort_network;
using ::foundation::core::sort_by_network;
using ::foundation::core::sort_network_size;
using ::foundation::core::sort_stably;
using ::foundation::core::SortElement;
using ::foundation::core::stable_sort_radix_count;
using ::foundation::core::stable_sort_run;
using ::foundation::core::StrictOrder;

}  // namespace fixy
