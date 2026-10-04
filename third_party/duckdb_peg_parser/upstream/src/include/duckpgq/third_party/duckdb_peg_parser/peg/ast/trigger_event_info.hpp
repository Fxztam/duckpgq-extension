#pragma once
#include "duckpgq/third_party/duckdb_peg_parser/peg/ast/trigger_type_compat.hpp"
#include "duckdb/common/vector.hpp"

#include "duckpgq/parser/identifier.hpp"
namespace duckdb {
namespace duckpgq_peg {
struct TriggerEventInfo {
	TriggerEventType event_type;
	vector<Identifier> columns;
};
} // namespace duckpgq_peg
} // namespace duckdb
