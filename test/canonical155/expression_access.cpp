#include "duckpgq/compat/expression_access.hpp"
#include "duckpgq/compat/function_access.hpp"
#include "duckpgq/compat/alter_access.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb/parser/statement/select_statement.hpp"
#include "duckdb/parser/query_node/select_node.hpp"
#include <iostream>
#include <stdexcept>
using namespace duckdb;
void TestExpressionAccess() {
    auto child = make_uniq<ConstantExpression>(Value(73));
    auto *identity = child.get();
    CastExpression cast(LogicalType::BIGINT, nullptr);
    duckpgq_compat::CastChildMutable(cast) = std::move(child);
    if (child || &duckpgq_compat::CastChild(cast) != identity) throw std::runtime_error("cast ownership lost");
    OperatorExpression op(ExpressionType::ARRAY_EXTRACT);
    duckpgq_compat::OperatorChildren(op).push_back(std::move(duckpgq_compat::CastChildMutable(cast)));
    if (cast.child || op.children[0].get() != identity) throw std::runtime_error("operator ownership lost");
    const OperatorExpression &constant_op = op;
    if (duckpgq_compat::OperatorChildren(constant_op)[0].get() != identity) throw std::runtime_error("const operator access differs");
    for (const auto &names : vector<vector<string>>{{"Mixed Name"}, {"Sch", "Tbl"}, {"Cat", "Sch", "Tbl"}, {"s\"x", "ö.table"}}) {
        string sql = "SELECT * FROM ";
        for (idx_t i = 0; i < names.size(); ++i) {
            if (i) sql += ".";
            sql += KeywordHelper::WriteQuoted(names[i], '"');
        }
        Parser parser;
        parser.ParseQuery(sql);
        auto &expected = parser.statements[0]->Cast<SelectStatement>().node->Cast<SelectNode>();
        auto actual = duckpgq_compat::TableFromNames(StringsToIdentifiers(names));
        if (actual->ToString() != expected.from_table->ToString()) throw std::runtime_error("table qualification differs from canonical parser");
    }
    auto table = duckpgq_compat::TableFromNames({Identifier("MixedTable")});
    table->alias = duckpgq_compat::HostName(Identifier("Alias Case"));
    table->column_name_alias = duckpgq_compat::HostNames({Identifier("X"), Identifier("Y")});
    if (table->alias != "Alias Case" || table->column_name_alias != vector<string>{"X", "Y"}) throw std::runtime_error("alias spelling or order lost");
    for (const auto &names : vector<vector<string>>{{}, {"a", "b", "c", "d"}}) {
        bool rejected = false;
        try { duckpgq_compat::TableFromNames(StringsToIdentifiers(names)); }
        catch (const ParserException &) { rejected = true; }
        if (!rejected) throw std::runtime_error("invalid table qualification accepted");
    }
    vector<Identifier> columns{Identifier("Cat"), Identifier("Sch"), Identifier("Tbl"), Identifier("X")};
    ColumnRefExpression column(duckpgq_compat::HostNames(columns));
    if (column.column_names != vector<string>{"Cat", "Sch", "Tbl", "X"}) throw std::runtime_error("four-part column path lost");
    std::cout << "PASS expression adapters: canonical table AST, invalid arities, aliases, column paths, cast/operator ownership\n";
}
