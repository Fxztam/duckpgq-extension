#ifdef PGQ_TRANSFORMER_TEST
#include "duckpgq/third_party/duckdb_peg_parser/peg/transformer/peg_transformer.hpp"
#include "duckpgq/third_party/duckdb_peg_parser/peg/ast/insert_values.hpp"
#include "duckpgq/third_party/duckdb_peg_parser/peg/ast/join_qualifier.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb/parser/statement/delete_statement.hpp"
#include "duckdb/parser/statement/update_statement.hpp"
#include "duckdb/parser/statement/insert_statement.hpp"
#include "duckdb/parser/statement/merge_into_statement.hpp"
#include <iostream>
#include <stdexcept>
using namespace duckdb;
using namespace duckdb::duckpgq_peg;
static void CheckDML(bool ok) { if (!ok) throw std::runtime_error("DML AST parity or ownership mismatch"); }
void TestDMLAST(PEGTransformer &t) {
    for (string sql : {"DELETE FROM items",
        "WITH src AS (SELECT 1 AS id) DELETE FROM items AS dst USING src WHERE dst.id=src.id RETURNING dst.id"}) {
        Parser p; p.ParseQuery(sql);
        auto expected = p.statements[0]->ToString();
        auto &s = p.statements[0]->Cast<DeleteStatement>();
        auto table = s.table.get();
        auto actual = PEGTransformerFactory::TransformDeleteStatement(t, std::move(s.cte_map),
            unique_ptr_cast<TableRef, BaseTableRef>(std::move(s.table)), std::move(s.using_clauses),
            std::move(s.condition), std::move(s.returning_list));
        CheckDML(actual->ToString() == expected && actual->Cast<DeleteStatement>().table.get() == table);
    }
    for (string sql : {"UPDATE items SET n=2",
        "WITH src AS (SELECT 3 AS n) UPDATE items AS dst SET n=src.n FROM src WHERE dst.n=1 RETURNING dst.n"}) {
        Parser p; p.ParseQuery(sql);
        auto expected = p.statements[0]->ToString();
        auto &s = p.statements[0]->Cast<UpdateStatement>();
        auto table = s.table.get();
        auto condition = std::move(s.set_info->condition);
        auto actual = PEGTransformerFactory::TransformUpdateStatement(t, std::move(s.cte_map),
            std::move(s.table), std::move(s.set_info), std::move(s.from_table),
            std::move(condition), std::move(s.returning_list));
        CheckDML(actual->ToString() == expected && actual->Cast<UpdateStatement>().table.get() == table);
    }
    for (string sql : {"INSERT INTO items DEFAULT VALUES",
        "WITH src AS (SELECT 2 AS n) INSERT INTO items(n) SELECT n FROM src RETURNING n",
        "INSERT INTO items(n) VALUES (2) ON CONFLICT(n) DO NOTHING RETURNING n"}) {
        Parser p; p.ParseQuery(sql);
        auto expected = p.statements[0]->ToString();
        auto &s = p.statements[0]->Cast<InsertStatement>();
        auto target = make_uniq<BaseTableRef>();
        target->catalog_name = s.catalog; target->schema_name = s.schema; target->table_name = s.table;
        InsertValues values; values.default_values = s.default_values; values.select_statement = std::move(s.select_statement);
        optional<vector<string>> columns;
        if (!s.columns.empty()) columns = s.columns;
        optional<unique_ptr<OnConflictInfo>> conflict;
        if (s.on_conflict_info) conflict = std::move(s.on_conflict_info);
        auto actual = PEGTransformerFactory::TransformInsertStatement(t, std::move(s.cte_map), {},
            std::move(target), s.column_order, columns, std::move(values), std::move(conflict),
            std::move(s.returning_list));
        CheckDML(actual->ToString() == expected);
    }
    Parser m; m.ParseQuery("MERGE INTO items dst USING source src ON dst.id=src.id WHEN MATCHED THEN DELETE");
    auto expected = m.statements[0]->ToString();
    auto &s = m.statements[0]->Cast<MergeIntoStatement>();
    JoinQualifier join; join.on_clause = std::move(s.join_condition);
    join.using_columns = StringsToIdentifiers(s.using_columns);
    vector<pair<MergeActionCondition, unique_ptr<MergeIntoAction>>> actions;
    for (auto &group : s.actions) for (auto &action : group.second) actions.emplace_back(group.first, std::move(action));
    auto actual = PEGTransformerFactory::TransformMergeIntoStatement(t, std::move(s.cte_map),
        unique_ptr_cast<TableRef, BaseTableRef>(std::move(s.target)), std::move(s.source),
        std::move(join), std::move(actions), std::move(s.returning_list));
    CheckDML(actual->ToString() == expected);
    vector<unique_ptr<ParsedExpression>> args;
    auto value = make_uniq<ConstantExpression>(Value::INTEGER(7));
    auto identity = value.get(); args.push_back(std::move(value));
    auto tuple = PEGTransformerFactory::TransformUpdateSetTuple(t, {Identifier("n")},
        make_uniq<FunctionExpression>("row", std::move(args)));
    CheckDML(tuple->columns == vector<string>{"n"} && tuple->expressions[0].get() == identity);
    bool rejected = false;
    try {
        vector<unique_ptr<ParsedExpression>> empty;
        auto ignored = PEGTransformerFactory::TransformUpdateSetTuple(t, {Identifier("n")},
            make_uniq<FunctionExpression>("row", std::move(empty)));
    } catch (const ParserException &) { rejected = true; }
    CheckDML(rejected);
    auto trunc_target = make_uniq<BaseTableRef>();
    trunc_target->table_name = "items";
    auto trunc = PEGTransformerFactory::TransformTruncateStatement(t, true, std::move(trunc_target));
    Parser trunc_parser; trunc_parser.ParseQuery("TRUNCATE items");
    CheckDML(trunc->ToString() == trunc_parser.statements[0]->ToString());
    rejected = false;
    try {
        auto target = make_uniq<BaseTableRef>(); target->table_name = "items";
        InsertValues defaults; defaults.default_values = true;
        auto ignored = PEGTransformerFactory::TransformInsertStatement(t, {}, {}, std::move(target),
            {}, vector<string>{"n"}, std::move(defaults), {}, {});
    } catch (const ParserException &) { rejected = true; }
    CheckDML(rejected);
    rejected = false;
    try {
        auto target = make_uniq<BaseTableRef>(); target->table_name = "items";
        auto source = make_uniq<BaseTableRef>(); source->table_name = "src";
        JoinQualifier condition; condition.on_clause = make_uniq<ConstantExpression>(Value::BOOLEAN(true));
        vector<pair<MergeActionCondition, unique_ptr<MergeIntoAction>>> duplicate;
        for (int i = 0; i < 2; i++) {
            auto action = make_uniq<MergeIntoAction>();
            action->action_type = MergeActionType::MERGE_DELETE;
            duplicate.emplace_back(MergeActionCondition::WHEN_MATCHED, std::move(action));
        }
        auto ignored = PEGTransformerFactory::TransformMergeIntoStatement(t, {}, std::move(target),
            std::move(source), std::move(condition), std::move(duplicate), {});
    } catch (const ParserException &) { rejected = true; }
    CheckDML(rejected);
    std::cout << "PASS DML AST: DELETE/UPDATE/INSERT/MERGE, CTE/WHERE/USING/FROM/RETURNING, tuple move and arity\n";
}
#endif
