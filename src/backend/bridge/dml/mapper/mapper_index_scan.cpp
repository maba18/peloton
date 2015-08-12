//===----------------------------------------------------------------------===//
//
//                         PelotonDB
//
// mapper_index_scan.cpp
//
// Identification: src/backend/bridge/dml/mapper/mapper_index_scan.cpp
//
// Copyright (c) 2015, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "backend/bridge/dml/mapper/mapper.h"
#include "backend/planner/index_scan_plan.h"

namespace peloton {
namespace bridge {

//===--------------------------------------------------------------------===//
// Index Scan
//===--------------------------------------------------------------------===//

/*
 * @brief transform a expr tree into info that a index scan node needs
 * */
static void BuildScanKey(
    const ScanKey scan_keys, int num_keys,
    planner::IndexScanPlan::IndexScanDesc &index_scan_desc);

/**
 * @brief Convert a Postgres IndexScanState into a Peloton IndexScanPlan.
 *        able to handle:
 *          1. simple operator with constant comparison value: indexkey op
 * constant)
 *        unable to handle:
 *          2. redundant simple qualifier: WHERE id > 4 and id > 3
 *          3. simple operator with non-constant value
 *          4. row compare expr: (indexkey, indexkey) op (expr, expr)
 *          5. scalar array op expr: indexkey op ANY (array-expression)
 *          6. null test: indexkey IS NULL/IS NOT NULL
 *          7. order by
 *          8. unary op
 * @return Pointer to the constructed AbstractPlan.
 */
planner::AbstractPlan *PlanTransformer::TransformIndexScan(
    const IndexScanPlanState *iss_plan_state,
    const TransformOptions options) {
  /* info needed to initialize plan node */

  planner::IndexScanPlan::IndexScanDesc index_scan_desc;

  /* Resolve target relation */
  Oid table_oid = iss_plan_state->table_oid;
  Oid database_oid = iss_plan_state->database_oid;

  const IndexScan *iss_plan = iss_plan_state->iss_plan;

  storage::DataTable *table = static_cast<storage::DataTable *>(
      catalog::Manager::GetInstance().GetTableWithOid(database_oid, table_oid));

  assert(table);

  /* Resolve index  */
  index_scan_desc.index = table->GetIndexWithOid(iss_plan->indexid);
  LOG_INFO("Index scan on oid %u, index name: %s", iss_plan->indexid,
           index_scan_desc.index->GetName().c_str());

  /* Resolve index order */
  /* Only support forward scan direction */
  LOG_INFO("Scan order: %d", iss_plan->indexorderdir);
  // assert(iss_plan->indexorderdir == ForwardScanDirection);

  /* index qualifier and scan keys */
  LOG_INFO("num of scan keys = %d", iss_plan_state->iss_NumScanKeys);

  BuildScanKey(iss_plan_state->iss_ScanKeys,
               iss_plan_state->iss_NumScanKeys,
               index_scan_desc);

  /* handle simple cases */
  /* ORDER BY, not support */

  /* Extract the generic scan info (including qual and projInfo) */
  planner::AbstractPlan *parent = nullptr;
  expression::AbstractExpression *predicate = nullptr;
  std::vector<oid_t> column_ids;

  GetGenericInfoFromScanState(parent, predicate, column_ids,
                              iss_plan_state,
                              options.use_projInfo);

  auto scan_node =
      new planner::IndexScanPlan(predicate, column_ids, table, index_scan_desc);

  planner::AbstractPlan *rv = nullptr;
  /* Check whether a parent is presented, connect with the scan node if yes */
  if (parent) {
    parent->AddChild(scan_node);
    rv = parent;
  } else {
    rv = scan_node;
  }

  return rv;
}
/**
 * @brief Helper function to build index scan descriptor.
 *        This function assumes the qualifiers are all non-trivial.
 *        i.e. There is no case such as WHERE id > 3 and id > 6
 *        This function can only handle simple constant case
 * @param scan_keys an array of scankey struct from Postgres
 * @param num_keys the number of scan keys
 * @param index_scan_desc the index scan node descriptor in Peloton
 * @return True if succeed
 *         False if fail
 */
static void BuildScanKey(
    const ScanKey scan_keys, int num_keys,
    planner::IndexScanPlan::IndexScanDesc &index_scan_desc) {
  ScanKey scan_key = scan_keys;
  assert(num_keys > 0);

  for (int key_itr = 0; key_itr < num_keys; key_itr++, scan_key++) {
    // currently, only support simple case
    assert(!(scan_key->sk_flags & SK_ISNULL));
    assert(!(scan_key->sk_flags & SK_ORDER_BY));
    assert(!(scan_key->sk_flags & SK_UNARY));
    assert(!(scan_key->sk_flags & SK_ROW_HEADER));
    assert(!(scan_key->sk_flags & SK_ROW_MEMBER));
    assert(!(scan_key->sk_flags & SK_ROW_END));
    assert(!(scan_key->sk_flags & SK_SEARCHNULL));
    assert(!(scan_key->sk_flags & SK_SEARCHNOTNULL));

    Value value =
        TupleTransformer::GetValue(scan_key->sk_argument, scan_key->sk_subtype);

    // 1 indexed
    index_scan_desc.key_column_ids.push_back(scan_key->sk_attno - 1);

    index_scan_desc.values.push_back(value);
    std::ostringstream oss;
    oss << value;
    LOG_INFO("key no: %d", scan_key->sk_attno);
    switch (scan_key->sk_strategy) {
      case BTLessStrategyNumber:
        LOG_INFO("key < %s", oss.str().c_str());
        index_scan_desc.expr_types.push_back(
            ExpressionType::EXPRESSION_TYPE_COMPARE_LT);
        break;
      case BTLessEqualStrategyNumber:
        LOG_INFO("key <= %s", oss.str().c_str());
        index_scan_desc.expr_types.push_back(
            ExpressionType::EXPRESSION_TYPE_COMPARE_LTE);
        break;
      case BTEqualStrategyNumber:
        LOG_INFO("key = %s", oss.str().c_str());
        index_scan_desc.expr_types.push_back(
            ExpressionType::EXPRESSION_TYPE_COMPARE_EQ);
        break;
      case BTGreaterEqualStrategyNumber:
        LOG_INFO("key >= %s", oss.str().c_str());
        index_scan_desc.expr_types.push_back(
            ExpressionType::EXPRESSION_TYPE_COMPARE_GTE);
        break;
      case BTGreaterStrategyNumber:
        LOG_INFO("key > %s", oss.str().c_str());
        index_scan_desc.expr_types.push_back(
            ExpressionType::EXPRESSION_TYPE_COMPARE_GT);
        break;
      default:
        LOG_ERROR("Invalid strategy num %d", scan_key->sk_strategy);
        index_scan_desc.expr_types.push_back(
            ExpressionType::EXPRESSION_TYPE_INVALID);
        break;
    }
  }
}

/**
 * @brief Convert a Postgres IndexOnlyScanState into a Peloton IndexScanPlan.
 *        able to handle:
 *          1. simple operator with constant comparison value: indexkey op
 * constant)
 *        unable to handle:
 *          2. redundant simple qualifier: WHERE id > 4 and id > 3
 *          3. simple operator with non-constant value
 *          4. row compare expr: (indexkey, indexkey) op (expr, expr)
 *          5. scalar array op expr: indexkey op ANY (array-expression)
 *          6. null test: indexkey IS NULL/IS NOT NULL
 *          7. order by
 *          8. unary op
 * @return Pointer to the constructed AbstractPlan.
 */
planner::AbstractPlan *PlanTransformer::TransformIndexOnlyScan(
    const IndexOnlyScanPlanState *ioss_plan_state,
    const TransformOptions options) {
  /* info needed to initialize plan node */
  planner::IndexScanPlan::IndexScanDesc index_scan_desc;

  /* Resolve target relation */
  Oid table_oid = ioss_plan_state->table_oid;
  Oid database_oid = ioss_plan_state->database_oid;

  LOG_INFO("Index Only Scan :: DB OID :: %u Table OID :: %u",
           database_oid, table_oid);

  const IndexOnlyScan *ioss_plan = ioss_plan_state->ioss_plan;

  storage::DataTable *table = static_cast<storage::DataTable *>(
      catalog::Manager::GetInstance().GetTableWithOid(database_oid, table_oid));
  assert(table);

  /* Resolve index  */
  index_scan_desc.index = table->GetIndexWithOid(ioss_plan->indexid);
  LOG_INFO("Index scan on oid %u, index name: %s", ioss_plan->indexid,
           index_scan_desc.index->GetName().c_str());

  /* Resolve index order */
  /* Only support forward scan direction */
  LOG_INFO("Scan order: %d", ioss_plan->indexorderdir);
  // assert(iss_plan->indexorderdir == ForwardScanDirection);

  /* index qualifier and scan keys */
  LOG_INFO("num of scan keys = %d", ioss_plan_state->ioss_NumScanKeys);
  BuildScanKey(ioss_plan_state->ioss_ScanKeys,
               ioss_plan_state->ioss_NumScanKeys,
               index_scan_desc);

  /* handle simple cases */
  /* ORDER BY, not support */

  /* Extract the generic scan info (including qual and projInfo) */
  planner::AbstractPlan *parent = nullptr;
  expression::AbstractExpression *predicate = nullptr;
  std::vector<oid_t> column_ids;

  GetGenericInfoFromScanState(parent, predicate, column_ids,
                              ioss_plan_state,
                              options.use_projInfo);

  auto scan_node =
      new planner::IndexScanPlan(predicate, column_ids, table, index_scan_desc);

  planner::AbstractPlan *rv = nullptr;
  /* Check whether a parent is presented, connect with the scan node if yes */
  if (parent) {
    parent->AddChild(scan_node);
    rv = parent;
  } else {
    rv = scan_node;
  }

  return rv;
}

/**
 * @brief Convert a Postgres BitmapScan into a Peloton IndexScanPlan
 *        We currently only handle the case where the lower plan is a
 *BitmapIndexScan
 *
 * @return Pointer to the constructed AbstractPlan
 */
planner::AbstractPlan *PlanTransformer::TransformBitmapHeapScan(
    const BitmapHeapScanPlanState *bhss_plan_state,
    const TransformOptions options) {
  planner::IndexScanPlan::IndexScanDesc index_scan_desc;

  /* resolve target relation */
  Oid table_oid = bhss_plan_state->table_oid;
  Oid database_oid =  bhss_plan_state->database_oid;

  const BitmapIndexScanPlanState *biss_state =
      reinterpret_cast<const BitmapIndexScanPlanState *>(
          outerAbstractPlanState(bhss_plan_state));
  const BitmapIndexScan *biss_plan =
      reinterpret_cast<const BitmapIndexScan *>(biss_state->biss_plan);

  storage::DataTable *table = static_cast<storage::DataTable *>(
      catalog::Manager::GetInstance().GetTableWithOid(database_oid, table_oid));

  assert(table);
  LOG_INFO("Scan from: database oid %u table oid %u", database_oid, table_oid);

  /* Resolve index  */
  index_scan_desc.index = table->GetIndexWithOid(biss_plan->indexid);

  if (nullptr == index_scan_desc.index) {
    LOG_ERROR("Can't find Index oid %u \n", biss_plan->indexid);
  }
  LOG_INFO("BitmapIdxmap scan on Index oid %u, index name: %s",
           biss_plan->indexid, index_scan_desc.index->GetName().c_str());

  assert(index_scan_desc.index);

  /* Resolve index order */
  /* Only support forward scan direction */

  /* index qualifier and scan keys */
  LOG_INFO("num of scan keys = %d", biss_state->biss_NumScanKeys);

  BuildScanKey(biss_state->biss_ScanKeys,
               biss_state->biss_NumScanKeys,
               index_scan_desc);

  /* handle simple cases */

  /* ORDER BY, not support */

  /* Extract the generic scan info (including qual and projInfo) */
  planner::AbstractPlan *parent = nullptr;
  expression::AbstractExpression *predicate = nullptr;
  std::vector<oid_t> column_ids;

  GetGenericInfoFromScanState(parent, predicate, column_ids,
                              bhss_plan_state,
                              options.use_projInfo);

  auto scan_node =
      new planner::IndexScanPlan(predicate, column_ids, table, index_scan_desc);

  planner::AbstractPlan *rv = nullptr;
  /* Check whether a parent is presented, connect with the scan node if yes */
  if (parent) {
    parent->AddChild(scan_node);
    rv = parent;
  } else {
    rv = scan_node;
  }

  return rv;
}

}  // namespace bridge
}  // namespace peloton