/* Copyright (c) 2020, 2021, Oracle and/or its affiliates.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License, version 2.0,
   as published by the Free Software Foundation.

   This program is also distributed with certain software (including
   but not limited to OpenSSL) that is licensed under separate terms,
   as designated in a particular file or component or in included license
   documentation.  The authors of MySQL hereby grant you an additional
   permission to link the program and your derivative works with the
   separately licensed software that they have included with MySQL.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License, version 2.0, for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

#include "sql/join_optimizer/access_path.h"

#include "sql/filesort.h"
#include "sql/item_sum.h"
#include "sql/iterators/basic_row_iterators.h"
#include "sql/iterators/bka_iterator.h"
#include "sql/iterators/composite_iterators.h"
#include "sql/iterators/hash_join_iterator.h"
#include "sql/iterators/ref_row_iterators.h"
#include "sql/iterators/sorting_iterator.h"
#include "sql/iterators/timing_iterator.h"
#include "sql/iterators/window_iterators.h"
#include "sql/join_optimizer/bit_utils.h"
#include "sql/join_optimizer/cost_model.h"
#include "sql/join_optimizer/estimate_selectivity.h"
#include "sql/join_optimizer/relational_expression.h"
#include "sql/join_optimizer/walk_access_paths.h"
#include "sql/range_optimizer/geometry_index_range_scan.h"
#include "sql/range_optimizer/group_index_skip_scan.h"
#include "sql/range_optimizer/group_index_skip_scan_plan.h"
#include "sql/range_optimizer/index_merge.h"
#include "sql/range_optimizer/index_range_scan.h"
#include "sql/range_optimizer/index_skip_scan.h"
#include "sql/range_optimizer/index_skip_scan_plan.h"
#include "sql/range_optimizer/range_optimizer.h"
#include "sql/range_optimizer/reverse_index_range_scan.h"
#include "sql/range_optimizer/rowid_ordered_retrieval.h"
#include "sql/sql_optimizer.h"
#include "sql/table.h"

#include <vector>

using pack_rows::TableCollection;
using std::vector;

AccessPath *NewSortAccessPath(THD *thd, AccessPath *child, Filesort *filesort,
                              bool count_examined_rows) {
  AccessPath *path = new (thd->mem_root) AccessPath;
  path->type = AccessPath::SORT;
  path->count_examined_rows = count_examined_rows;
  path->sort().child = child;
  path->sort().filesort = filesort;

  if (filesort->using_addon_fields()) {
    path->sort().tables_to_get_rowid_for = 0;
  } else {
    if (filesort->tables.size() == 1 &&
        filesort->tables[0]->pos_in_table_list == nullptr) {
      // This can happen if we sort a single temporary table
      // which is not in the table list (e.g., one that was
      // specifically created for us). Filesort has special-casing
      // to always get the row ID in this case.
      path->sort().tables_to_get_rowid_for = 0;
    } else {
      FindTablesToGetRowidFor(path);
    }
  }

  return path;
}

AccessPath *NewDeleteRowsAccessPath(THD *thd, AccessPath *child,
                                    table_map delete_tables,
                                    table_map immediate_tables) {
  assert(IsSubset(immediate_tables, delete_tables));
  AccessPath *path = new (thd->mem_root) AccessPath;
  path->type = AccessPath::DELETE_ROWS;
  path->delete_rows().child = child;
  path->delete_rows().tables_to_delete_from = delete_tables;
  path->delete_rows().immediate_tables = immediate_tables;
  return path;
}

static AccessPath *FindSingleAccessPathOfType(AccessPath *path,
                                              AccessPath::Type type) {
  AccessPath *found_path = nullptr;

  auto func = [type, &found_path](AccessPath *subpath, const JOIN *) {
#ifdef NDEBUG
    constexpr bool fast_exit = true;
#else
    constexpr bool fast_exit = false;
#endif
    if (subpath->type == type) {
      assert(found_path == nullptr);
      found_path = subpath;
      // If not in debug mode, stop as soon as we find the first one.
      if (fast_exit) {
        return true;
      }
    }
    return false;
  };
  // Our users generally want to stop at STREAM or MATERIALIZE nodes,
  // since they are table-oriented and those nodes have their own tables.
  WalkAccessPaths(path, /*join=*/nullptr,
                  WalkAccessPathPolicy::STOP_AT_MATERIALIZATION, func);
  return found_path;
}

static RowIterator *FindSingleIteratorOfType(AccessPath *path,
                                             AccessPath::Type type) {
  AccessPath *found_path = FindSingleAccessPathOfType(path, type);
  if (found_path == nullptr) {
    return nullptr;
  } else {
    return found_path->iterator->real_iterator();
  }
}

table_map GetUsedTableMap(const AccessPath *path, bool include_pruned_tables) {
  table_map tmap = 0;
  WalkTablesUnderAccessPath(
      const_cast<AccessPath *>(path),
      [&tmap](TABLE *table) {
        if (table->pos_in_table_list == nullptr) {
          // Materialization within a JOIN (e.g., for sorting). The table won't
          // have a map, so the caller will need to find the table manually.
          tmap |= RAND_TABLE_BIT;
        } else {
          tmap |= table->pos_in_table_list->map();
        }
        return false;
      },
      include_pruned_tables);
  return tmap;
}

static Prealloced_array<TABLE *, 4> GetUsedTables(AccessPath *child,
                                                  bool include_pruned_tables) {
  Prealloced_array<TABLE *, 4> tables{PSI_NOT_INSTRUMENTED};
  WalkTablesUnderAccessPath(
      child,
      [&tables](TABLE *table) {
        tables.push_back(table);
        return false;
      },
      include_pruned_tables);
  return tables;
}

Mem_root_array<TABLE *> CollectTables(THD *thd, AccessPath *root_path) {
  Mem_root_array<TABLE *> tables(thd->mem_root);
  WalkTablesUnderAccessPath(
      root_path, [&tables](TABLE *table) { return tables.push_back(table); },
      /*include_pruned_tables=*/true);
  return tables;
}

// Mirrors QEP_TAB::pfs_batch_update(), with one addition:
// If there is more than one table, batch mode will be handled by the join
// iterators on the probe side, so joins will return false.
bool ShouldEnableBatchMode(AccessPath *path) {
  switch (path->type) {
    case AccessPath::TABLE_SCAN:
    case AccessPath::INDEX_SCAN:
    case AccessPath::REF:
    case AccessPath::REF_OR_NULL:
    case AccessPath::PUSHED_JOIN_REF:
    case AccessPath::FULL_TEXT_SEARCH:
    case AccessPath::DYNAMIC_INDEX_RANGE_SCAN:
      return true;
    case AccessPath::FILTER:
      if (path->filter().condition->has_subquery()) {
        return false;
      } else {
        return ShouldEnableBatchMode(path->filter().child);
      }
    case AccessPath::SORT:
      return ShouldEnableBatchMode(path->sort().child);
    case AccessPath::EQ_REF:
    case AccessPath::CONST_TABLE:
      // These can read only one row per scan, so batch mode will never be a
      // win (fall through).
    default:
      // All others, in particular joins.
      return false;
  }
}

bool FinalizeMaterializedSubqueries(THD *thd, JOIN *join, AccessPath *path) {
  if (path->type != AccessPath::FILTER ||
      !path->filter().materialize_subqueries) {
    return false;
  }
  return WalkItem(
      path->filter().condition, enum_walk::POSTFIX, [thd, join](Item *item) {
        if (!IsItemInSubSelect(item)) {
          return false;
        }
        Item_in_subselect *item_subs = down_cast<Item_in_subselect *>(item);
        Query_block *subquery_block = item_subs->unit->first_query_block();
        if (!item_subs->subquery_allows_materialization(thd, subquery_block,
                                                        join->query_block)) {
          return false;
        }
        if (item_subs->finalize_materialization_transform(
                thd, subquery_block->join)) {
          return true;
        }
        item_subs->create_iterators(thd);
        return false;
      });
}

unique_ptr_destroy_only<RowIterator> CreateIteratorFromAccessPath(
    THD *thd, MEM_ROOT *mem_root, AccessPath *path, JOIN *join,
    bool eligible_for_batch_mode) {
  if (join != nullptr) {
    assert(!join->needs_finalize);
  }
  if (FinalizeMaterializedSubqueries(thd, join, path)) {
    return nullptr;
  }

  unique_ptr_destroy_only<RowIterator> iterator;

  ha_rows *examined_rows = nullptr;
  if (path->count_examined_rows && join != nullptr) {
    examined_rows = &join->examined_rows;
  }

  switch (path->type) {
    case AccessPath::TABLE_SCAN: {
      const auto &param = path->table_scan();
      iterator = NewIterator<TableScanIterator>(
          thd, mem_root, param.table, path->num_output_rows, examined_rows);
      break;
    }
    case AccessPath::INDEX_SCAN: {
      const auto &param = path->index_scan();
      if (param.reverse) {
        iterator = NewIterator<IndexScanIterator<true>>(
            thd, mem_root, param.table, param.idx, param.use_order,
            path->num_output_rows, examined_rows);
      } else {
        iterator = NewIterator<IndexScanIterator<false>>(
            thd, mem_root, param.table, param.idx, param.use_order,
            path->num_output_rows, examined_rows);
      }
      break;
    }
    case AccessPath::REF: {
      const auto &param = path->ref();
      if (param.reverse) {
        iterator = NewIterator<RefIterator<true>>(
            thd, mem_root, param.table, param.ref, param.use_order,
            path->num_output_rows, examined_rows);
      } else {
        iterator = NewIterator<RefIterator<false>>(
            thd, mem_root, param.table, param.ref, param.use_order,
            path->num_output_rows, examined_rows);
      }
      break;
    }
    case AccessPath::REF_OR_NULL: {
      const auto &param = path->ref_or_null();
      iterator = NewIterator<RefOrNullIterator>(
          thd, mem_root, param.table, param.ref, param.use_order,
          path->num_output_rows, examined_rows);
      break;
    }
    case AccessPath::EQ_REF: {
      const auto &param = path->eq_ref();
      iterator =
          NewIterator<EQRefIterator>(thd, mem_root, param.table, param.ref,
                                     param.use_order, examined_rows);
      break;
    }
    case AccessPath::PUSHED_JOIN_REF: {
      const auto &param = path->pushed_join_ref();
      iterator = NewIterator<PushedJoinRefIterator>(
          thd, mem_root, param.table, param.ref, param.use_order,
          param.is_unique, examined_rows);
      break;
    }
    case AccessPath::FULL_TEXT_SEARCH: {
      const auto &param = path->full_text_search();
      iterator = NewIterator<FullTextSearchIterator>(
          thd, mem_root, param.table, param.ref, param.ft_func, param.use_order,
          param.use_limit, examined_rows);
      break;
    }
    case AccessPath::CONST_TABLE: {
      const auto &param = path->const_table();
      iterator = NewIterator<ConstIterator>(thd, mem_root, param.table,
                                            param.ref, examined_rows);
      break;
    }
    case AccessPath::MRR: {
      const auto &param = path->mrr();
      const auto &bka_param = param.bka_path->bka_join();
      iterator = NewIterator<MultiRangeRowIterator>(
          thd, mem_root, param.table, param.ref, param.mrr_flags,
          bka_param.join_type,
          GetUsedTables(bka_param.outer, /*include_pruned_tables=*/true),
          bka_param.store_rowids, bka_param.tables_to_get_rowid_for);
      break;
    }
    case AccessPath::FOLLOW_TAIL: {
      const auto &param = path->follow_tail();
      iterator = NewIterator<FollowTailIterator>(
          thd, mem_root, param.table, path->num_output_rows, examined_rows);
      break;
    }
    case AccessPath::INDEX_RANGE_SCAN: {
      const auto &param = path->index_range_scan();
      TABLE *table = param.used_key_part[0].field->table;
      if (param.geometry) {
        iterator = NewIterator<GeometryIndexRangeScanIterator>(
            thd, mem_root, table, examined_rows, path->num_output_rows,
            param.index, param.need_rows_in_rowid_order, param.reuse_handler,
            mem_root, param.mrr_flags, param.mrr_buf_size,
            Bounds_checked_array{param.ranges, param.num_ranges});
      } else if (param.reverse) {
        iterator = NewIterator<ReverseIndexRangeScanIterator>(
            thd, mem_root, table, examined_rows, path->num_output_rows,
            param.index, mem_root, param.mrr_flags,
            Bounds_checked_array{param.ranges, param.num_ranges},
            param.using_extended_key_parts);
      } else {
        iterator = NewIterator<IndexRangeScanIterator>(
            thd, mem_root, table, examined_rows, path->num_output_rows,
            param.index, param.need_rows_in_rowid_order, param.reuse_handler,
            mem_root, param.mrr_flags, param.mrr_buf_size,
            Bounds_checked_array{param.ranges, param.num_ranges});
      }
      break;
    }
    case AccessPath::INDEX_MERGE: {
      const auto &param = path->index_merge();
      unique_ptr_destroy_only<RowIterator> pk_quick_select;
      Mem_root_array<unique_ptr_destroy_only<RowIterator>> children(mem_root);
      children.reserve(param.children->size());
      for (AccessPath *range_scan : *param.children) {
        unique_ptr_destroy_only<RowIterator> child_iterator =
            CreateIteratorFromAccessPath(thd, range_scan, join,
                                         /*eligible_for_batch_mode=*/false);
        if (param.table->file->primary_key_is_clustered() &&
            range_scan->index_range_scan().index ==
                param.table->s->primary_key) {
          assert(pk_quick_select == nullptr);
          pk_quick_select = std::move(child_iterator);
        } else {
          children.push_back(std::move(child_iterator));
        }
      }

      iterator = NewIterator<IndexMergeIterator>(
          thd, mem_root, mem_root, param.table, std::move(pk_quick_select),
          std::move(children));
      break;
    }
    case AccessPath::ROWID_INTERSECTION: {
      const auto &param = path->rowid_intersection();
      Mem_root_array<unique_ptr_destroy_only<RowIterator>> children(mem_root);
      children.reserve(param.children->size());
      for (AccessPath *range_scan : *param.children) {
        children.push_back(
            CreateIteratorFromAccessPath(thd, range_scan, join,
                                         /*eligible_for_batch_mode=*/false));
      }

      unique_ptr_destroy_only<RowIterator> cpk_child;
      if (param.cpk_child != nullptr) {
        cpk_child = CreateIteratorFromAccessPath(
            thd, param.cpk_child, join, /*eligible_for_batch_mode=*/false);
      }
      iterator = NewIterator<RowIDIntersectionIterator>(
          thd, mem_root, mem_root, param.table, param.retrieve_full_rows,
          param.need_rows_in_rowid_order, std::move(children),
          std::move(cpk_child));
      break;
    }
    case AccessPath::ROWID_UNION: {
      const auto &param = path->rowid_union();
      Mem_root_array<unique_ptr_destroy_only<RowIterator>> children(mem_root);
      children.reserve(param.children->size());
      for (AccessPath *range_scan : *param.children) {
        children.push_back(
            CreateIteratorFromAccessPath(thd, range_scan, join,
                                         /*eligible_for_batch_mode=*/false));
      }

      iterator = NewIterator<RowIDUnionIterator>(
          thd, mem_root, mem_root, param.table, std::move(children));
      break;
    }
    case AccessPath::INDEX_SKIP_SCAN: {
      const IndexSkipScanParameters *param = path->index_skip_scan().param;
      iterator = NewIterator<IndexSkipScanIterator>(
          thd, mem_root, path->index_skip_scan().table, param->index_info,
          path->index_skip_scan().index, param->eq_prefix_len,
          param->eq_prefix_key_parts, param->eq_prefixes,
          path->index_skip_scan().num_used_key_parts, mem_root,
          param->has_aggregate_function, param->min_range_key,
          param->max_range_key, param->min_search_key, param->max_search_key,
          param->range_cond_flag, param->range_key_len);
      break;
    }
    case AccessPath::GROUP_INDEX_SKIP_SCAN: {
      const GroupIndexSkipScanParameters *param =
          path->group_index_skip_scan().param;
      iterator = NewIterator<GroupIndexSkipScanIterator>(
          thd, mem_root, path->group_index_skip_scan().table,
          &param->min_functions, &param->max_functions,
          param->have_agg_distinct, param->min_max_arg_part,
          param->group_prefix_len, param->group_key_parts,
          param->real_key_parts, param->max_used_key_length, param->index_info,
          path->group_index_skip_scan().index, param->key_infix_len, mem_root,
          param->is_index_scan, &param->prefix_ranges, &param->key_infix_ranges,
          &param->min_max_ranges);
      break;
    }
    case AccessPath::DYNAMIC_INDEX_RANGE_SCAN: {
      const auto &param = path->dynamic_index_range_scan();
      iterator = NewIterator<DynamicRangeIterator>(
          thd, mem_root, param.table, param.qep_tab, examined_rows);
      break;
    }
    case AccessPath::TABLE_VALUE_CONSTRUCTOR: {
      assert(join != nullptr);
      Query_block *query_block = join->query_block;
      iterator = NewIterator<TableValueConstructorIterator>(
          thd, mem_root, examined_rows, *query_block->row_value_list,
          query_block->join->fields);
      break;
    }
    case AccessPath::FAKE_SINGLE_ROW:
      iterator =
          NewIterator<FakeSingleRowIterator>(thd, mem_root, examined_rows);
      break;
    case AccessPath::ZERO_ROWS: {
      const auto &param = path->zero_rows();
      unique_ptr_destroy_only<RowIterator> child;
      if (param.child != nullptr) {
        child = CreateIteratorFromAccessPath(thd, mem_root, param.child, join,
                                             /*eligible_for_batch_mode=*/false);
        if (child == nullptr) {
          return nullptr;
        }
      }
      iterator = NewIterator<ZeroRowsIterator>(thd, mem_root, std::move(child));
      break;
    }
    case AccessPath::ZERO_ROWS_AGGREGATED:
      iterator = NewIterator<ZeroRowsAggregatedIterator>(thd, mem_root, join,
                                                         examined_rows);
      break;
    case AccessPath::MATERIALIZED_TABLE_FUNCTION: {
      const auto &param = path->materialized_table_function();
      unique_ptr_destroy_only<RowIterator> table_iterator =
          CreateIteratorFromAccessPath(thd, mem_root, param.table_path, join,
                                       eligible_for_batch_mode);
      if (table_iterator == nullptr) {
        return nullptr;
      }
      iterator = NewIterator<MaterializedTableFunctionIterator>(
          thd, mem_root, param.table_function, param.table,
          std::move(table_iterator));
      break;
    }
    case AccessPath::UNQUALIFIED_COUNT:
      iterator = NewIterator<UnqualifiedCountIterator>(thd, mem_root, join);
      break;
    case AccessPath::NESTED_LOOP_JOIN: {
      const auto &param = path->nested_loop_join();
      unique_ptr_destroy_only<RowIterator> outer = CreateIteratorFromAccessPath(
          thd, mem_root, param.outer, join, /*eligible_for_batch_mode=*/false);
      if (outer == nullptr) {
        return nullptr;
      }
      unique_ptr_destroy_only<RowIterator> inner = CreateIteratorFromAccessPath(
          thd, mem_root, param.inner, join, eligible_for_batch_mode);
      if (inner == nullptr) {
        return nullptr;
      }
      iterator = NewIterator<NestedLoopIterator>(
          thd, mem_root, std::move(outer), std::move(inner), param.join_type,
          param.pfs_batch_mode);
      break;
    }
    case AccessPath::NESTED_LOOP_SEMIJOIN_WITH_DUPLICATE_REMOVAL: {
      const auto &param = path->nested_loop_semijoin_with_duplicate_removal();
      unique_ptr_destroy_only<RowIterator> outer = CreateIteratorFromAccessPath(
          thd, mem_root, param.outer, join, /*eligible_for_batch_mode=*/false);
      if (outer == nullptr) {
        return nullptr;
      }
      unique_ptr_destroy_only<RowIterator> inner = CreateIteratorFromAccessPath(
          thd, mem_root, param.inner, join, eligible_for_batch_mode);
      if (inner == nullptr) {
        return nullptr;
      }
      iterator = NewIterator<NestedLoopSemiJoinWithDuplicateRemovalIterator>(
          thd, mem_root, std::move(outer), std::move(inner), param.table,
          param.key, param.key_len);
      break;
    }
    case AccessPath::BKA_JOIN: {
      const auto &param = path->bka_join();
      AccessPath *mrr_path =
          FindSingleAccessPathOfType(param.inner, AccessPath::MRR);
      mrr_path->mrr().bka_path = path;

      unique_ptr_destroy_only<RowIterator> outer = CreateIteratorFromAccessPath(
          thd, mem_root, param.outer, join, /*eligible_for_batch_mode=*/false);
      if (outer == nullptr) {
        return nullptr;
      }
      unique_ptr_destroy_only<RowIterator> inner = CreateIteratorFromAccessPath(
          thd, mem_root, param.inner, join, /*eligible_for_batch_mode=*/false);
      if (inner == nullptr) {
        return nullptr;
      }
      MultiRangeRowIterator *mrr_iterator = down_cast<MultiRangeRowIterator *>(
          mrr_path->iterator->real_iterator());
      iterator = NewIterator<BKAIterator>(
          thd, mem_root, std::move(outer),
          GetUsedTables(param.outer, /*include_pruned_tables=*/true),
          std::move(inner), thd->variables.join_buff_size,
          param.mrr_length_per_rec, param.rec_per_key, param.store_rowids,
          param.tables_to_get_rowid_for, mrr_iterator, param.join_type);
      break;
    }
    case AccessPath::HASH_JOIN: {
      const auto &param = path->hash_join();
      const JoinPredicate *join_predicate = param.join_predicate;
      unique_ptr_destroy_only<RowIterator> outer = CreateIteratorFromAccessPath(
          thd, mem_root, param.outer, join, eligible_for_batch_mode);
      if (outer == nullptr) {
        return nullptr;
      }
      unique_ptr_destroy_only<RowIterator> inner = CreateIteratorFromAccessPath(
          thd, mem_root, param.inner, join, /*eligible_for_batch_mode=*/true);
      if (inner == nullptr) {
        return nullptr;
      }
      vector<HashJoinCondition> conditions;
      for (Item_func_eq *cond : join_predicate->expr->equijoin_conditions) {
        conditions.emplace_back(HashJoinCondition(cond, thd->mem_root));
      }
      const bool probe_input_batch_mode =
          eligible_for_batch_mode && ShouldEnableBatchMode(param.outer);
      double estimated_build_rows = param.inner->num_output_rows;
      if (param.inner->num_output_rows < 0.0) {
        // Not all access paths may propagate their costs properly.
        // Choose a fairly safe estimate (it's better to be too large
        // than too small).
        estimated_build_rows = 1048576.0;
      }
      JoinType join_type{JoinType::INNER};
      switch (join_predicate->expr->type) {
        case RelationalExpression::INNER_JOIN:
        case RelationalExpression::STRAIGHT_INNER_JOIN:
          join_type = JoinType::INNER;
          break;
        case RelationalExpression::LEFT_JOIN:
          join_type = JoinType::OUTER;
          break;
        case RelationalExpression::ANTIJOIN:
          join_type = JoinType::ANTI;
          break;
        case RelationalExpression::SEMIJOIN:
          join_type =
              param.rewrite_semi_to_inner ? JoinType::INNER : JoinType::SEMI;
          break;
        case RelationalExpression::TABLE:
        default:
          assert(false);
      }
      // See if we can allow the hash table to keep its contents across Init()
      // calls.
      //
      // The old optimizer will sometimes push join conditions referring
      // to outer tables (in the same query block) down in under the hash
      // operation, so without analysis of each filter and join condition, we
      // cannot say for sure, and thus have to turn it off. But the hypergraph
      // optimizer is strictly modular, and will never do this -- so it's safe.
      //
      // Regardless of optimizer, we can push outer references down in under the
      // hash, but join->hash_table_generation will increase whenever we need to
      // recompute the query block (in JOIN::clear_hash_tables()).
      uint64_t *hash_table_generation = thd->lex->using_hypergraph_optimizer
                                            ? &join->hash_table_generation
                                            : nullptr;

      iterator = NewIterator<HashJoinIterator>(
          thd, mem_root, std::move(inner),
          GetUsedTables(param.inner, /*include_pruned_tables=*/true),
          estimated_build_rows, std::move(outer),
          GetUsedTables(param.outer, /*include_pruned_tables=*/true),
          param.store_rowids, param.tables_to_get_rowid_for,
          thd->variables.join_buff_size, std::move(conditions),
          param.allow_spill_to_disk, join_type,
          join_predicate->expr->join_conditions, probe_input_batch_mode,
          hash_table_generation);
      break;
    }
    case AccessPath::FILTER: {
      const auto &param = path->filter();
      unique_ptr_destroy_only<RowIterator> child = CreateIteratorFromAccessPath(
          thd, mem_root, param.child, join, eligible_for_batch_mode);
      if (child == nullptr) {
        return nullptr;
      }
      iterator = NewIterator<FilterIterator>(thd, mem_root, std::move(child),
                                             param.condition);
      break;
    }
    case AccessPath::SORT: {
      const auto &param = path->sort();
      unique_ptr_destroy_only<RowIterator> child = CreateIteratorFromAccessPath(
          thd, mem_root, param.child, join, /*eligible_for_batch_mode=*/true);
      if (child == nullptr) {
        return nullptr;
      }
      ha_rows num_rows_estimate = param.child->num_output_rows < 0.0
                                      ? HA_POS_ERROR
                                      : lrint(param.child->num_output_rows);
      Filesort *filesort = param.filesort;
      iterator = NewIterator<SortingIterator>(
          thd, mem_root, filesort, std::move(child), num_rows_estimate,
          param.tables_to_get_rowid_for, examined_rows);
      if (filesort->m_remove_duplicates) {
        filesort->tables[0]->duplicate_removal_iterator =
            down_cast<SortingIterator *>(iterator->real_iterator());
      } else {
        filesort->tables[0]->sorting_iterator =
            down_cast<SortingIterator *>(iterator->real_iterator());
      }
      break;
    }
    case AccessPath::AGGREGATE: {
      const auto &param = path->aggregate();
      unique_ptr_destroy_only<RowIterator> child = CreateIteratorFromAccessPath(
          thd, mem_root, param.child, join, eligible_for_batch_mode);
      if (child == nullptr) {
        return nullptr;
      }
      Prealloced_array<TABLE *, 4> tables =
          GetUsedTables(param.child, /*include_pruned_tables=*/true);
      iterator = NewIterator<AggregateIterator>(
          thd, mem_root, std::move(child), join,
          TableCollection(tables, /*store_rowids=*/false,
                          /*tables_to_get_rowid_for=*/0),
          param.rollup);
      break;
    }
    case AccessPath::TEMPTABLE_AGGREGATE: {
      const auto &param = path->temptable_aggregate();
      unique_ptr_destroy_only<RowIterator> subquery_iterator =
          CreateIteratorFromAccessPath(thd, mem_root, param.subquery_path, join,
                                       /*eligible_for_batch_mode=*/true);
      if (subquery_iterator == nullptr) {
        return nullptr;
      }
      unique_ptr_destroy_only<RowIterator> table_iterator =
          CreateIteratorFromAccessPath(thd, mem_root, param.table_path, join,
                                       eligible_for_batch_mode);
      if (table_iterator == nullptr) {
        return nullptr;
      }
      iterator = NewIterator<TemptableAggregateIterator>(
          thd, mem_root, std::move(subquery_iterator), param.temp_table_param,
          param.table, std::move(table_iterator), join, param.ref_slice);
      break;
    }
    case AccessPath::LIMIT_OFFSET: {
      const auto &param = path->limit_offset();
      unique_ptr_destroy_only<RowIterator> child = CreateIteratorFromAccessPath(
          thd, mem_root, param.child, join, eligible_for_batch_mode);
      if (child == nullptr) {
        return nullptr;
      }
      ha_rows *send_records = nullptr;
      if (param.send_records_override != nullptr) {
        send_records = param.send_records_override;
      } else if (join != nullptr) {
        send_records = &join->send_records;
      }
      iterator = NewIterator<LimitOffsetIterator>(
          thd, mem_root, std::move(child), param.limit, param.offset,
          param.count_all_rows, param.reject_multiple_rows, send_records);
      break;
    }
    case AccessPath::STREAM: {
      const auto &param = path->stream();
      unique_ptr_destroy_only<RowIterator> child = CreateIteratorFromAccessPath(
          thd, mem_root, param.child, param.join, eligible_for_batch_mode);
      if (child == nullptr) {
        return nullptr;
      }
      iterator = NewIterator<StreamingIterator>(
          thd, mem_root, std::move(child), param.temp_table_param, param.table,
          param.provide_rowid, param.join, param.ref_slice);
      break;
    }
    case AccessPath::MATERIALIZE: {
      // The table access path should be a single iterator, not a tree.
      // (ALTERNATIVE counts as a single iterator in this regard.)
      assert(path->materialize().table_path->type == AccessPath::TABLE_SCAN ||
             path->materialize().table_path->type == AccessPath::REF ||
             path->materialize().table_path->type == AccessPath::REF_OR_NULL ||
             path->materialize().table_path->type == AccessPath::EQ_REF ||
             path->materialize().table_path->type == AccessPath::ALTERNATIVE ||
             path->materialize().table_path->type == AccessPath::CONST_TABLE);

      unique_ptr_destroy_only<RowIterator> table_iterator =
          CreateIteratorFromAccessPath(thd, mem_root,
                                       path->materialize().table_path, join,
                                       eligible_for_batch_mode);
      if (table_iterator == nullptr) {
        return nullptr;
      }
      MaterializePathParameters *param = path->materialize().param;

      Mem_root_array<MaterializeIterator::QueryBlock> query_blocks(
          thd->mem_root, param->query_blocks.size());
      for (size_t i = 0; i < param->query_blocks.size(); ++i) {
        const MaterializePathParameters::QueryBlock &from =
            param->query_blocks[i];
        MaterializeIterator::QueryBlock &to = query_blocks[i];
        to.subquery_iterator = CreateIteratorFromAccessPath(
            thd, mem_root, from.subquery_path, from.join,
            /*eligible_for_batch_mode=*/true);
        if (to.subquery_iterator == nullptr) {
          return nullptr;
        }
        to.select_number = from.select_number;
        to.join = from.join;
        to.disable_deduplication_by_hash_field =
            from.disable_deduplication_by_hash_field;
        to.copy_items = from.copy_items;
        to.temp_table_param = from.temp_table_param;
        to.is_recursive_reference = from.is_recursive_reference;

        if (to.is_recursive_reference) {
          // Find the recursive reference to ourselves; there should be
          // exactly one, as per the standard.
          RowIterator *recursive_reader = FindSingleIteratorOfType(
              from.subquery_path, AccessPath::FOLLOW_TAIL);
          if (recursive_reader == nullptr) {
            // The recursive reference was optimized away, e.g. due to an
            // impossible WHERE condition, so we're not a recursive
            // reference after all.
            to.is_recursive_reference = false;
          } else {
            to.recursive_reader =
                down_cast<FollowTailIterator *>(recursive_reader);
          }
        }
      }
      JOIN *subjoin = param->ref_slice == -1 ? nullptr : query_blocks[0].join;
      iterator = NewIterator<MaterializeIterator>(
          thd, mem_root, std::move(query_blocks), param->table,
          std::move(table_iterator), param->cte, param->unit, subjoin,
          param->ref_slice, param->rematerialize, param->limit_rows,
          param->reject_multiple_rows);

      if (param->invalidators != nullptr) {
        MaterializeIterator *materialize =
            down_cast<MaterializeIterator *>(iterator->real_iterator());
        for (const AccessPath *invalidator_path : *param->invalidators) {
          // We create iterators left-to-right, so we should have created the
          // invalidators before this.
          assert(invalidator_path->iterator != nullptr);

          materialize->AddInvalidator(down_cast<CacheInvalidatorIterator *>(
              invalidator_path->iterator->real_iterator()));
        }
      }

      break;
    }
    case AccessPath::MATERIALIZE_INFORMATION_SCHEMA_TABLE: {
      const auto &param = path->materialize_information_schema_table();
      unique_ptr_destroy_only<RowIterator> table_iterator =
          CreateIteratorFromAccessPath(thd, mem_root, param.table_path, join,
                                       eligible_for_batch_mode);
      if (table_iterator == nullptr) {
        return nullptr;
      }
      iterator = NewIterator<MaterializeInformationSchemaTableIterator>(
          thd, mem_root, std::move(table_iterator), param.table_list,
          param.condition);
      break;
    }
    case AccessPath::APPEND: {
      const auto &param = path->append();
      vector<unique_ptr_destroy_only<RowIterator>> children;
      children.reserve(param.children->size());
      for (const AppendPathParameters &child : *param.children) {
        children.push_back(
            CreateIteratorFromAccessPath(thd, mem_root, child.path, child.join,
                                         /*eligible_for_batch_mode=*/true));
        if (children.back() == nullptr) {
          return nullptr;
        }
      }
      iterator =
          NewIterator<AppendIterator>(thd, mem_root, std::move(children));
      break;
    }
    case AccessPath::WINDOW: {
      const auto &param = path->window();
      unique_ptr_destroy_only<RowIterator> child = CreateIteratorFromAccessPath(
          thd, mem_root, param.child, join, eligible_for_batch_mode);
      if (child == nullptr) {
        return nullptr;
      }
      if (param.needs_buffering) {
        iterator = NewIterator<BufferingWindowIterator>(
            thd, mem_root, std::move(child), param.temp_table_param, join,
            param.ref_slice);
      } else {
        iterator = NewIterator<WindowIterator>(thd, mem_root, std::move(child),
                                               param.temp_table_param, join,
                                               param.ref_slice);
      }
      break;
    }
    case AccessPath::WEEDOUT: {
      const auto &param = path->weedout();
      unique_ptr_destroy_only<RowIterator> child = CreateIteratorFromAccessPath(
          thd, mem_root, param.child, join, eligible_for_batch_mode);
      if (child == nullptr) {
        return nullptr;
      }
      iterator = NewIterator<WeedoutIterator>(thd, mem_root, std::move(child),
                                              param.weedout_table,
                                              param.tables_to_get_rowid_for);
      break;
    }
    case AccessPath::REMOVE_DUPLICATES: {
      const auto &param = path->remove_duplicates();
      unique_ptr_destroy_only<RowIterator> child = CreateIteratorFromAccessPath(
          thd, mem_root, param.child, join, eligible_for_batch_mode);
      if (child == nullptr) {
        return nullptr;
      }
      iterator = NewIterator<RemoveDuplicatesIterator>(
          thd, mem_root, std::move(child), join, param.group_items,
          param.group_items_size);
      break;
    }
    case AccessPath::REMOVE_DUPLICATES_ON_INDEX: {
      const auto &param = path->remove_duplicates_on_index();
      unique_ptr_destroy_only<RowIterator> child = CreateIteratorFromAccessPath(
          thd, mem_root, param.child, join, eligible_for_batch_mode);
      if (child == nullptr) {
        return nullptr;
      }
      iterator = NewIterator<RemoveDuplicatesOnIndexIterator>(
          thd, mem_root, std::move(child), param.table, param.key,
          param.loosescan_key_len);
      break;
    }
    case AccessPath::ALTERNATIVE: {
      const auto &param = path->alternative();
      unique_ptr_destroy_only<RowIterator> child = CreateIteratorFromAccessPath(
          thd, mem_root, param.child, join, eligible_for_batch_mode);
      if (child == nullptr) {
        return nullptr;
      }
      unique_ptr_destroy_only<RowIterator> table_scan_iterator =
          CreateIteratorFromAccessPath(thd, mem_root, param.table_scan_path,
                                       join, eligible_for_batch_mode);
      if (table_scan_iterator == nullptr) {
        return nullptr;
      }
      iterator = NewIterator<AlternativeIterator>(
          thd, mem_root, param.table_scan_path->table_scan().table,
          std::move(child), std::move(table_scan_iterator), param.used_ref);
      break;
    }
    case AccessPath::CACHE_INVALIDATOR: {
      const auto &param = path->cache_invalidator();
      unique_ptr_destroy_only<RowIterator> child = CreateIteratorFromAccessPath(
          thd, mem_root, param.child, join, eligible_for_batch_mode);
      if (child == nullptr) {
        return nullptr;
      }
      iterator = NewIterator<CacheInvalidatorIterator>(
          thd, mem_root, std::move(child), param.name);
      break;
    }
    case AccessPath::DELETE_ROWS: {
      // TODO(khatlen): There is no iterator type for DELETE_ROWS yet. The logic
      // for deleting the rows lives in Query_result_delete, but much of it
      // could just as well have been in a delete iterator. For now, just create
      // the iterator for the child.
      return CreateIteratorFromAccessPath(thd, path->delete_rows().child, join,
                                          eligible_for_batch_mode);
    }
  }

  path->iterator = iterator.get();
  return iterator;
}

void FindTablesToGetRowidFor(AccessPath *path) {
  table_map handled_by_others = 0;

  auto add_tables_handled_by_others = [path, &handled_by_others](
                                          AccessPath *subpath, const JOIN *) {
    if (path == subpath) return false;  // Skip ourselves.
    switch (subpath->type) {
      case AccessPath::HASH_JOIN:
        handled_by_others |=
            GetUsedTableMap(subpath, /*include_pruned_tables=*/true);
        FindTablesToGetRowidFor(subpath);
        return true;  // Don't double-traverse.
      case AccessPath::BKA_JOIN:
        handled_by_others |= GetUsedTableMap(subpath->bka_join().outer,
                                             /*include_pruned_tables=*/true);
        FindTablesToGetRowidFor(subpath);
        return true;  // Don't double-traverse.
      case AccessPath::STREAM: {
        subpath->stream().provide_rowid = true;
        TABLE *table = subpath->stream().table;
        if (table->pos_in_table_list == nullptr) {
          // Don't need to set anything; see comment on the similar
          // test in NewSortAccessPath().
        } else {
          handled_by_others |= table->pos_in_table_list->map();
        }
        // Doesn't really matter, we don't cross query blocks anyway.
        return true;
      }
      default:
        return false;
    }
  };

  // We stop at MATERIALIZE and STREAM (they supply row IDs for us without
  // having to ask the tables below).
  switch (path->type) {
    case AccessPath::HASH_JOIN:
      WalkAccessPaths(path, /*join=*/nullptr,
                      WalkAccessPathPolicy::STOP_AT_MATERIALIZATION,
                      add_tables_handled_by_others);
      path->hash_join().store_rowids = true;
      path->hash_join().tables_to_get_rowid_for =
          GetUsedTableMap(path, /*include_pruned_tables=*/true) &
          ~handled_by_others;
      break;
    case AccessPath::BKA_JOIN:
      WalkAccessPaths(path->bka_join().outer, /*join=*/nullptr,
                      WalkAccessPathPolicy::STOP_AT_MATERIALIZATION,
                      add_tables_handled_by_others);
      path->bka_join().store_rowids = true;
      path->bka_join().tables_to_get_rowid_for =
          GetUsedTableMap(path->bka_join().outer,
                          /*include_pruned_tables=*/true) &
          ~handled_by_others;
      break;
    case AccessPath::WEEDOUT:
      WalkAccessPaths(path, /*join=*/nullptr,
                      WalkAccessPathPolicy::STOP_AT_MATERIALIZATION,
                      add_tables_handled_by_others);
      path->weedout().tables_to_get_rowid_for =
          GetUsedTableMap(path, /*include_pruned_tables=*/true) &
          ~handled_by_others;
      break;
    case AccessPath::SORT:
      WalkAccessPaths(path, /*join=*/nullptr,
                      WalkAccessPathPolicy::STOP_AT_MATERIALIZATION,
                      add_tables_handled_by_others);
      path->sort().tables_to_get_rowid_for =
          GetUsedTableMap(path, /*include_pruned_tables=*/true) &
          ~handled_by_others;
      break;
    default:
      my_abort();
  }
}

static Item *ConditionFromFilterPredicates(
    const Mem_root_array<Predicate> &predicates, OverflowBitset mask,
    int num_where_predicates) {
  List<Item> items;
  for (int pred_idx : BitsSetIn(mask)) {
    if (pred_idx >= num_where_predicates) break;
    items.push_back(predicates[pred_idx].condition);
  }
  return CreateConjunction(&items);
}

void ExpandSingleFilterAccessPath(THD *thd, AccessPath *path, const JOIN *join,
                                  const Mem_root_array<Predicate> &predicates,
                                  unsigned num_where_predicates) {
  // Expand join filters for nested loop joins.
  if (path->type == AccessPath::NESTED_LOOP_JOIN &&
      !path->nested_loop_join().already_expanded_predicates &&
      !(path->nested_loop_join().equijoin_predicates.empty() &&
        path->nested_loop_join()
            .join_predicate->expr->join_conditions.empty()) &&
      path->nested_loop_join().inner->type != AccessPath::ZERO_ROWS) {
    AccessPath *right_path = path->nested_loop_join().inner;
    const RelationalExpression *expr =
        path->nested_loop_join().join_predicate->expr;

    // While we're collecting the join conditions, calculate cost and output
    // rows (purely for display purposes). Note that this mirrors the
    // calculation we are doing in CostingReceiver::ProposeNestedLoopJoin();
    // we don't have space in the AccessPath to store it there.
    double filter_cost = right_path->cost;
    double filter_rows = right_path->num_output_rows;

    List<Item> items;
    for (size_t filter_idx :
         BitsSetIn(path->nested_loop_join().equijoin_predicates)) {
      Item *condition = expr->equijoin_conditions[filter_idx];
      items.push_back(condition);
      filter_cost +=
          EstimateFilterCost(thd, filter_rows, condition, join->query_block)
              .cost_if_not_materialized;
      filter_rows *= EstimateSelectivity(thd, condition, /*trace=*/nullptr);
    }
    for (Item *condition : expr->join_conditions) {
      items.push_back(condition);
      filter_cost +=
          EstimateFilterCost(thd, filter_rows, condition, join->query_block)
              .cost_if_not_materialized;
      filter_rows *= EstimateSelectivity(thd, condition, /*trace=*/nullptr);
    }
    assert(!items.is_empty());

    AccessPath *filter_path = new (thd->mem_root) AccessPath;
    filter_path->type = AccessPath::FILTER;
    filter_path->filter().child = right_path;

    // We don't bother trying to materialize subqueries in join conditions,
    // since they should be very rare.
    filter_path->filter().materialize_subqueries = false;

    CopyBasicProperties(*right_path, filter_path);
    filter_path->filter().condition = CreateConjunction(&items);
    filter_path->cost = filter_cost;
    filter_path->num_output_rows = filter_rows;

    path->nested_loop_join().inner = filter_path;

    // Since multiple root paths may have their filters expanded,
    // and the same nested loop may be a subpath in several
    // of them, we need to make sure we don't add the join predicates
    // more than once, so mark them as done here.
    path->nested_loop_join().already_expanded_predicates = true;
  }

  // Expand filters _after_ the access path (these are much more common).
  Item *condition = ConditionFromFilterPredicates(
      predicates, path->filter_predicates, num_where_predicates);
  if (condition == nullptr) {
    return;
  }
  AccessPath *new_path = new (thd->mem_root) AccessPath(*path);
  new_path->filter_predicates.Clear();
  new_path->num_output_rows = path->num_output_rows_before_filter;
  new_path->cost = path->cost_before_filter;

  // We don't really know how much of init_cost comes from the filter,
  // but we need to heed the invariant that cost >= init_cost
  // also for the new (non-filter) path we're creating, even if it's
  // just for display. Heuristically allocate as much as possible to
  // the filter.
  double filter_only_cost = path->cost - path->cost_before_filter;
  new_path->init_cost = std::max(new_path->init_cost - filter_only_cost, 0.0);
  new_path->init_once_cost =
      std::max(new_path->init_once_cost - filter_only_cost, 0.0);
  assert(new_path->cost >= new_path->init_cost);
  assert(new_path->init_cost >= new_path->init_once_cost);

  path->type = AccessPath::FILTER;
  path->filter().condition = condition;
  path->filter().child = new_path;
  path->filter().materialize_subqueries = false;

  // Clear filter_predicates, but keep applied_sargable_join_predicates.
  MutableOverflowBitset applied_sargable_join_predicates =
      path->applied_sargable_join_predicates().Clone(thd->mem_root);
  applied_sargable_join_predicates.ClearBits(0, num_where_predicates);
  path->filter_predicates = std::move(applied_sargable_join_predicates);
}

void ExpandFilterAccessPaths(THD *thd, AccessPath *path_arg, const JOIN *join,
                             const Mem_root_array<Predicate> &predicates,
                             unsigned num_where_predicates) {
  WalkAccessPaths(path_arg, join, WalkAccessPathPolicy::ENTIRE_QUERY_BLOCK,
                  [thd, &predicates, num_where_predicates](
                      AccessPath *path, const JOIN *sub_join) {
                    ExpandSingleFilterAccessPath(
                        thd, path, sub_join, predicates, num_where_predicates);
                    return false;
                  });
}
