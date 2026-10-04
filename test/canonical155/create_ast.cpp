#ifdef PGQ_TRANSFORMER_TEST
#include "duckpgq/third_party/duckdb_peg_parser/peg/transformer/peg_transformer.hpp"
#include "duckpgq/third_party/duckdb_peg_parser/peg/ast/sequence_option.hpp"
#include "duckpgq/compat/name_metadata.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb/parser/parsed_data/create_schema_info.hpp"
#include "duckpgq/third_party/duckdb_peg_parser/peg/ast/analyze_target.hpp"
#include "duckpgq/third_party/duckdb_peg_parser/peg/ast/generic_copy_option.hpp"
#include "duckdb/parser/statement/attach_statement.hpp"
#include <stdexcept>
#include <iostream>
using namespace duckdb;
using namespace duckdb::duckpgq_peg;
static void CheckCreate(bool ok) { if (!ok) throw std::runtime_error("CREATE AST differs from canonical parser"); }
void TestCreateAST(PEGTransformer &t) {
    for (bool ignore : {false, true}) {
        optional<bool> flag;
        if (ignore) flag = true;
        const string prefix = ignore ? "IF NOT EXISTS " : "";
        auto q = duckpgq_compat::MakeQualifiedName({Identifier("Cat Space")}, Identifier("NewSchema"));
        auto actual = PEGTransformerFactory::TransformCreateSchemaStmt(t, flag, q);
        Parser p; p.ParseQuery("CREATE SCHEMA " + prefix + "\"Cat Space\".\"NewSchema\"");
        CheckCreate(actual->ToString() == p.statements[0]->ToString());
        auto seq_name = duckpgq_compat::MakeQualifiedName({Identifier("Cat"), Identifier("Sch")}, Identifier("Seq"));
        auto seq = PEGTransformerFactory::TransformCreateSequenceStmt(t, flag, seq_name, {});
        Parser s; s.ParseQuery("CREATE SEQUENCE " + prefix + "\"Cat\".\"Sch\".\"Seq\"");
        CheckCreate(seq->ToString() == s.statements[0]->ToString());
    }
    auto type_name = duckpgq_compat::MakeQualifiedName({Identifier("Sch")}, Identifier("Mood"));
    auto type = PEGTransformerFactory::TransformEnumStringLiteralList(t,
        vector<string>{"short", "this value is longer than an inline string"});
    auto actual_type = PEGTransformerFactory::TransformCreateTypeStmt(t, {}, type_name, std::move(type));
    Parser expected;
    expected.ParseQuery("CREATE TYPE \"Sch\".\"Mood\" AS ENUM ('short', 'this value is longer than an inline string')");
    CheckCreate(actual_type->ToString() == expected.statements[0]->ToString());
    bool rejected = false;
    try {
        auto ignored = PEGTransformerFactory::TransformCreateSchemaStmt(t, {},
            duckpgq_compat::MakeQualifiedName({Identifier("a"), Identifier("b")}, Identifier("c")));
    } catch (const ParserException &) { rejected = true; }
    CheckCreate(rejected);
    vector<unique_ptr<ParsedExpression>> args;
    args.push_back(make_uniq<ConstantExpression>(Value::BIGINT(7)));
    auto negative = make_uniq<FunctionExpression>("-", std::move(args));
    auto increment = PEGTransformerFactory::TransformSeqSetIncrement(t, true, std::move(negative));
    CheckCreate(static_cast<ValueSequenceOption &>(*increment.second).value.GetValue<int64_t>() == -7);
    std::cout << "PASS CREATE AST: schema/sequence parser parity, enum string ownership, negative increment\n";
    for (bool force : {false, true}) {
        for (bool named : {false, true}) {
            optional<bool> force_flag;
            optional<Identifier> catalog;
            if (force) force_flag = true;
            if (named) catalog = Identifier("Cat Space");
            auto checkpoint = PEGTransformerFactory::TransformCheckpointStatement(t, force_flag, catalog);
            Parser parser;
            parser.ParseQuery(string(force ? "FORCE " : "") + "CHECKPOINT" + (named ? " \"Cat Space\"" : ""));
            CheckCreate(checkpoint->ToString() == parser.statements[0]->ToString());
        }
    }
    auto ref = make_uniq<BaseTableRef>();
    ref->schema_name = "Sch"; ref->table_name = "Table";
    auto target = PEGTransformerFactory::TransformAnalyzeTarget(t, std::move(ref), vector<string>{"Col"});
    auto analyze = PEGTransformerFactory::TransformAnalyzeStatement(t, {}, std::move(target));
    Parser ap; ap.ParseQuery("ANALYZE \"Sch\".\"Table\" (\"Col\")");
    CheckCreate(analyze->ToString() == ap.statements[0]->ToString());
    for (int conflict = 0; conflict < 3; conflict++) {
        optional<bool> replace, ignore;
        if (conflict == 1) replace = true;
        if (conflict == 2) ignore = true;
        auto attach = PEGTransformerFactory::TransformAttachStatement(t, replace, ignore, true,
            make_uniq<ConstantExpression>(Value("folder/file.db")), Identifier("Alias"), {});
        Parser parser;
        parser.ParseQuery(string("ATTACH ") + (conflict == 1 ? "OR REPLACE " : conflict == 2 ? "IF NOT EXISTS " : "") +
            "'folder/file.db' AS \"Alias\"");
        CheckCreate(attach->ToString() == parser.statements[0]->ToString());
    }
    for (Value value : {Value(), Value::INTEGER(3)}) {
        bool rejected_path = false;
        try {
            auto ignored = PEGTransformerFactory::TransformAttachStatement(t, {}, {}, true,
                make_uniq<ConstantExpression>(value), {}, {});
        } catch (const ParserException &) { rejected_path = true; }
        CheckCreate(rejected_path);
    }
    bool dynamic_rejected = false;
    try {
        auto ignored = PEGTransformerFactory::TransformAttachStatement(t, {}, {}, true,
            make_uniq<ColumnRefExpression>("path_variable"), {}, {});
    } catch (const ParserException &) { dynamic_rejected = true; }
    CheckCreate(dynamic_rejected);
    vector<GenericCopyOption> options;
    options.emplace_back("read_only", Value(true));
    auto attach = PEGTransformerFactory::TransformAttachStatement(t, {}, {}, true,
        make_uniq<ConstantExpression>(Value(":memory:")), Identifier("Mem"), options);
    CheckCreate(attach->Cast<AttachStatement>().info->options.at("read_only").GetValue<bool>());
    std::cout << "PASS utility AST: ANALYZE, CHECKPOINT, ATTACH parity and path/option checks\n";
}
#endif
