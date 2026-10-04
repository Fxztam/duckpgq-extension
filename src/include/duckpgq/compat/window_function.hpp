#pragma once
#include "duckpgq/third_party/duckdb_peg_parser/peg/transformer/peg_transformer.hpp"
namespace duckdb {
namespace duckpgq_peg {
#if !__has_include("duckdb/common/identifier.hpp")
unique_ptr<WindowExpression> BuildWindowFunction(PEGTransformer &transformer, const QualifiedName &name,
    MethodArguments arguments, optional<vector<OrderByNode>> within_group,
    unique_ptr<ParsedExpression> filter, bool export_state, unique_ptr<WindowExpression> frame);
#endif
}
}
