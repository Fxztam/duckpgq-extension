#include "duckpgq/compat/name_metadata.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb/parser/statement/select_statement.hpp"
#include "duckdb/parser/statement/create_statement.hpp"
#include "duckdb/parser/statement/set_statement.hpp"
#include "duckdb/parser/expression/constant_expression.hpp"
#include <stdexcept>
using namespace duckdb;
static void RequireName(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }

void TestNameMetadata() {
    struct NameCase { vector<string> path; string name; string sql; };
    const vector<NameCase> cases = {
        {{}, "plain", "CREATE TYPE plain AS ENUM ('a')"},
        {{"S"}, "Mixed Name", "CREATE TYPE \"S\".\"Mixed Name\" AS ENUM ('a')"},
        {{"C", "S"}, "N", "CREATE TYPE \"C\".\"S\".\"N\" AS ENUM ('a')"},
        {{"a.b"}, "a\"b", "CREATE TYPE \"a.b\".\"a\"\"b\" AS ENUM ('a')"}
    };
    for (const auto &item : cases) {
        Parser parser; parser.ParseQuery(item.sql);
        auto &expected = parser.statements[0]->Cast<CreateStatement>().info->Cast<CreateTypeInfo>();
        auto q = duckpgq_compat::MakeQualifiedName(StringsToIdentifiers(item.path), Identifier(item.name));
        CreateTypeInfo actual;
        duckpgq_compat::SetTypeQualifiedName(actual, q);
        RequireName(actual.catalog == expected.catalog && actual.schema == expected.schema && actual.name == expected.name,
                    "qualified type name differs from canonical parser");
    }
    bool rejected = false;
    try { duckpgq_compat::MakeQualifiedName(StringsToIdentifiers({"a","b","c"}), Identifier("d")); }
    catch (const ParserException &) { rejected = true; }
    RequireName(rejected, "excess name qualification must be rejected");
    rejected = false;
    try {
        duckpgq_compat::UseTargetText(
            duckpgq_compat::MakeQualifiedName(StringsToIdentifiers({"a","b"}), Identifier("c")));
    } catch (const ParserException &) { rejected = true; }
    RequireName(rejected, "USE must reject three-part targets");
    for (const string sql : {"SELECT $Name, $name, $Other", "SELECT ?, ?", "SELECT $2, $1, $2"}) {
        Parser parser; parser.ParseQuery(sql);
        auto &expected = *parser.statements[0];
        identifier_map_t<idx_t> source;
        for (const auto &entry : expected.named_param_map) source.emplace(Identifier(entry.first), entry.second);
        SelectStatement actual;
        actual.query = "preserve"; actual.stmt_location = 7; actual.stmt_length = 8;
        duckpgq_compat::TransferStatementParameters(actual, source, sql.find('?') != string::npos);
        RequireName(actual.named_param_map == expected.named_param_map, "parameter map differs from canonical parser");
        duckpgq_compat::TransferStatementParameters(actual, {}, false);
        RequireName(actual.named_param_map == expected.named_param_map, "empty outer transfer erased inner parameters");
        RequireName(actual.query == "preserve" && actual.stmt_location == 7 && actual.stmt_length == 8,
                    "parameter transfer modified source metadata");
    }
    struct UseCase { vector<string> path; string name; string sql; };
    for (const auto &item : vector<UseCase>{{{}, "plain", "USE plain"}, {{"db space"}, "s\"x", "USE \"db space\".\"s\"\"x\""}}) {
        Parser parser; parser.ParseQuery(item.sql);
        auto &expected = static_cast<SetVariableStatement &>(*parser.statements[0]);
        auto q = duckpgq_compat::MakeQualifiedName(StringsToIdentifiers(item.path), Identifier(item.name));
        RequireName(duckpgq_compat::UseTargetText(q) == expected.value->Cast<ConstantExpression>().value.GetValue<string>(),
                    "USE quoting differs from canonical parser");
    }
}
