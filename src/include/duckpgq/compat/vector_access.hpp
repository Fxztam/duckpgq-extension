#pragma once
#include "duckdb/common/types/data_chunk.hpp"
#include "duckdb/common/types/vector.hpp"
#include "duckdb/common/vector_operations/ternary_executor.hpp"
#include "duckdb/planner/expression/bound_function_expression.hpp"
#include "duckpgq/parser/identifier.hpp"

namespace duckdb {
namespace duckpgq_compat {

// Execution-time accessors. The pinned DuckDB-PGQ fork tracks a newer DuckDB than canonical 1.5.5 and
// renamed several of them; each helper has the fork branch (unchanged behavior) and the canonical branch.
#if __has_include("duckdb/common/identifier.hpp")
template <class T>
inline T *MutableData(Vector &vector) {
	return FlatVector::GetDataMutable<T>(vector);
}
inline ValidityMask &MutableValidity(Vector &vector) {
	return FlatVector::ValidityMutable(vector);
}
// The fork's Vector knows its own size; `count` is only needed on the canonical host.
inline void ToUnified(Vector &vector, idx_t count, UnifiedVectorFormat &format) {
	(void)count;
	vector.ToUnifiedFormat(format);
}
inline void SetOutputCardinality(DataChunk &chunk, idx_t count) {
	chunk.SetChildCardinality(count);
}
inline FunctionData *BindInfo(const BoundFunctionExpression &expression) {
	return expression.BindInfo();
}
inline Vector &ListChild(Vector &list) {
	return ListVector::GetChild(list);
}
inline void CheckOutputCardinality(DataChunk &chunk, idx_t count) {
	chunk.CheckCardinality(count);
}
template <class A, class B, class C, class R, class FUN>
inline void ExecuteTernary(Vector &a, Vector &b, Vector &c, Vector &result, idx_t count, FUN fun) {
	(void)count;
	TernaryExecutor::Execute<A, B, C, R>(a, b, c, result, fun);
}
inline Value IdentifierValue(const Identifier &identifier) {
	return Value(identifier);
}
#else
template <class T>
inline T *MutableData(Vector &vector) {
	return FlatVector::GetData<T>(vector);
}
inline ValidityMask &MutableValidity(Vector &vector) {
	return FlatVector::Validity(vector);
}
inline void ToUnified(Vector &vector, idx_t count, UnifiedVectorFormat &format) {
	vector.ToUnifiedFormat(count, format);
}
inline void SetOutputCardinality(DataChunk &chunk, idx_t count) {
	chunk.SetCardinality(count);
}
inline FunctionData *BindInfo(const BoundFunctionExpression &expression) {
	return expression.bind_info.get();
}
inline Vector &ListChild(Vector &list) {
	return ListVector::GetEntry(list);
}
inline void CheckOutputCardinality(DataChunk &chunk, idx_t count) {
	D_ASSERT(chunk.size() == count);
	(void)chunk;
	(void)count;
}
template <class A, class B, class C, class R, class FUN>
inline void ExecuteTernary(Vector &a, Vector &b, Vector &c, Vector &result, idx_t count, FUN fun) {
	TernaryExecutor::Execute<A, B, C, R>(a, b, c, result, count, fun);
}
inline Value IdentifierValue(const Identifier &identifier) {
	return Value(identifier.GetIdentifierName());
}
#endif

} // namespace duckpgq_compat
} // namespace duckdb
