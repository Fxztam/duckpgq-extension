#pragma once
#include "duckdb/common/exception.hpp"
#if __has_include("duckdb/parser/parsed_data/connect_info.hpp")
#include "duckdb/parser/parsed_data/connect_info.hpp"
#endif
namespace duckdb {
namespace duckpgq_peg {
#if !__has_include("duckdb/parser/parsed_data/connect_info.hpp")
// Complete only for parser-template instantiation; never constructed or sent
// to the host. All corresponding transforms reject unsupported syntax.
struct ConnectInfo { ConnectInfo() = delete; };
#endif
[[noreturn]] inline void RejectUnsupportedHostStatement(const char *statement) {
    throw ParserException("DuckPGQ: %s is not supported by canonical DuckDB 1.5.5", statement);
}
} // namespace duckpgq_peg
} // namespace duckdb
