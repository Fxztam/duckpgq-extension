#ifdef PGQ_TRANSFORMER_TEST
#include "duckpgq/third_party/duckdb_peg_parser/peg/transformer/peg_transformer.hpp"
#include "duckpgq/third_party/duckdb_peg_parser/peg/ast/add_column_entry.hpp"
#include "duckpgq/compat/alter_access.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb/parser/statement/alter_statement.hpp"
#include "duckdb/parser/statement/multi_statement.hpp"
#include "duckdb/parser/statement/update_statement.hpp"
#include <iostream>
#include <stdexcept>
using namespace duckdb;
using namespace duckdb::duckpgq_peg;
static void CheckAlter(bool ok) { if (!ok) throw std::runtime_error("ALTER AST mismatch"); }
static unique_ptr<BaseTableRef> Target() {
    auto target = make_uniq<BaseTableRef>();
    target->schema_name = "Sch"; target->table_name = "Tbl";
    return target;
}
static void Compare(PEGTransformer &t, unique_ptr<AlterTableInfo> operation, const string &sql) {
    vector<unique_ptr<AlterTableInfo>> operations;
    operations.push_back(std::move(operation));
    auto info = PEGTransformerFactory::TransformAlterTableStmt(t, {}, Target(), std::move(operations));
    auto actual = PEGTransformerFactory::TransformAlterStatement(t, std::move(info));
    Parser parser; parser.ParseQuery(sql);
    CheckAlter(actual->ToString() == parser.statements[0]->ToString());
}
void TestAlterAST(PEGTransformer &t) {
    Compare(t, PEGTransformerFactory::TransformRenameAlter(t, Identifier("NewTable")),
        "ALTER TABLE \"Sch\".\"Tbl\" RENAME TO \"NewTable\"");
    for (vector<string> path : {vector<string>{"Col"}, vector<string>{"Rec", "Field"}}) {
        auto name = make_uniq<ColumnRefExpression>(path);
        auto qualified = path.size() == 1 ? "\"Col\"" : "\"Rec\".\"Field\"";
        Compare(t, PEGTransformerFactory::TransformRenameColumn(t, true, std::move(name), Identifier("NewName")),
            string("ALTER TABLE \"Sch\".\"Tbl\" RENAME COLUMN ") + qualified + " TO \"NewName\"");
        Compare(t, PEGTransformerFactory::TransformDropColumn(t, true, {},
            make_uniq<ColumnRefExpression>(path), {}), string("ALTER TABLE \"Sch\".\"Tbl\" DROP COLUMN ") + qualified);
    }
    for (string action : {"drop", "set"}) {
        auto info = PEGTransformerFactory::TransformChangeNullability(t, action);
        Compare(t, PEGTransformerFactory::TransformAlterColumn(t, true,
            make_uniq<ColumnRefExpression>("Col"), std::move(info)),
            "ALTER TABLE \"Sch\".\"Tbl\" ALTER COLUMN \"Col\" " + action + " NOT NULL");
    }
    AddColumnEntry column;
    column.column_path = {Identifier("NewColumn")};
    column.type = LogicalType::DOUBLE;
    column.default_value = make_uniq<FunctionExpression>("random", vector<unique_ptr<ParsedExpression>>{});
    vector<unique_ptr<AlterTableInfo>> operations;
    operations.push_back(PEGTransformerFactory::TransformAddColumn(t, true, {}, std::move(column)));
    auto info = PEGTransformerFactory::TransformAlterTableStmt(t, {}, Target(), std::move(operations));
    auto result = PEGTransformerFactory::TransformAlterStatement(t, std::move(info));
    auto &multi = result->Cast<MultiStatement>();
    CheckAlter(multi.statements.size() == 3);
    auto &add = multi.statements[0]->Cast<AlterStatement>().info->Cast<AddColumnInfo>();
    CheckAlter(add.new_column.DefaultValue().Cast<ConstantExpression>().value.IsNull());
    auto &update = multi.statements[1]->Cast<UpdateStatement>();
    CheckAlter(update.prioritize_table_when_binding && update.set_info->columns == vector<string>{"NewColumn"});
    CheckAlter(update.table->Cast<BaseTableRef>().schema_name == "Sch");
    CheckAlter(update.set_info->expressions[0]->ToString() == "random()");
    auto &restore = multi.statements[2]->Cast<AlterStatement>().info->Cast<SetDefaultInfo>();
    CheckAlter(restore.column_name == "NewColumn" && restore.expression->ToString() == "random()");
    CheckAlter(restore.expression.get() != update.set_info->expressions[0].get());
    case_insensitive_map_t<unique_ptr<ParsedExpression>> options;
    options["x"] = make_uniq<ConstantExpression>(Value::INTEGER(1));
    bool rejected = false;
    try { auto ignored = PEGTransformerFactory::TransformResetOptions(t, std::move(options)); }
    catch (const ParserException &) { rejected = true; }
    CheckAlter(rejected);
    std::cout << "PASS ALTER AST: rename/drop/nested/nullability, three-stage default materialization, RESET rejection\n";
}
#endif
