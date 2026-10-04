#pragma once

#include "duckdb/common/constants.hpp"
#if __has_include("duckdb/common/enums/trigger_type.hpp")
#include "duckdb/common/enums/trigger_type.hpp"
#else
namespace duckdb {
namespace duckpgq_peg {

// DuckDB introduced these parser-only trigger enums after v1.5.5. Keep the
// PEG parser representation extension-owned when DuckPGQ is compiled against
// the pinned v1.5.5 ABI.
enum class TriggerTiming : uint8_t { BEFORE = 0, AFTER = 1, INSTEAD_OF = 2 };

enum class TriggerEventType : uint8_t { INSERT_EVENT = 0, DELETE_EVENT = 1, UPDATE_EVENT = 2 };

enum class TriggerForEach : uint8_t { STATEMENT = 0, ROW = 1 };

} // namespace duckpgq_peg
} // namespace duckdb
#endif
