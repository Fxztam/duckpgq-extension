#ifdef PGQ_TRANSFORMER_TEST
#include "duckpgq/third_party/duckdb_peg_parser/peg/transformer/peg_transformer.hpp"
#include "duckdb.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb/parser/expression/comparison_expression.hpp"
#include "duckdb/parser/expression/operator_expression.hpp"
#include "duckdb/parser/expression/cast_expression.hpp"
#include <iostream>
#include <stdexcept>
using namespace duckdb;
using namespace duckdb::duckpgq_peg;
static unique_ptr<ParsedExpression> ParseOne(const string &sql) {
    auto expressions = Parser::ParseExpressionList(sql);
    return std::move(expressions[0]);
}
static void Check(bool ok, const char *message) {
    if (!ok) throw std::runtime_error(message);
}
void TestStructComparison(PEGTransformer &t) {
    DuckDB db(nullptr); Connection con(db);
    vector<FunctionArgument> fields;
    vector<ParsedExpression *> identities;
    for (const string name : {"MixedCase", "a.b", "quote\"name", "unicode_ö"}) {
        auto value = ParseOne("73");
        identities.push_back(value.get());
        fields.push_back(PEGTransformerFactory::TransformStructField(t, Identifier(name), std::move(value)));
    }
    auto record = PEGTransformerFactory::TransformStructExpression(t, std::move(fields));
    // Canonical brace syntax qualifies main.struct_pack; this PGQ entry has
    // always emitted an unqualified call. Check its exact AST against that
    // call, and separately compare execution with the canonical brace literal.
    auto oracle = ParseOne("struct_pack(\"MixedCase\" := 73,\"a.b\" := 73,\"quote\"\"name\" := 73,\"unicode_ö\" := 73)");
    if (!record->Equals(*oracle)) throw std::runtime_error("struct AST actual="+record->ToString()+" expected="+oracle->ToString());
    auto &args = record->Cast<FunctionExpression>().children;
    for (idx_t i=0;i<args.size();i++) Check(args[i].get()==identities[i], "struct value ownership lost");
    auto actual = con.Query("SELECT "+record->ToString());
    auto expected = con.Query("SELECT {'MixedCase':73,'a.b':73,'quote\"name':73,'unicode_ö':73}");
    Check(!actual->HasError() && !expected->HasError(), "struct execution failed");
    Check(actual->GetValue(0,0)==expected->GetValue(0,0), "struct result differs");
    auto empty = PEGTransformerFactory::TransformStructExpression(t, {});
    Check(empty->Cast<FunctionExpression>().children.empty(), "empty struct has arguments");
    for (const string input : {"NULL", "0", "1"}) {
        for (int literal=0;literal<3;literal++) for (bool negate : {false,true}) {
            Value value = literal==0 ? Value() : Value::BOOLEAN(literal==1);
            string suffix = literal==0 ? "NULL" : (literal==1 ? "TRUE" : "FALSE");
            auto child=ParseOne(input); auto *identity=child.get();
            vector<unique_ptr<ParsedExpression>> tails;
            tails.push_back(PEGTransformerFactory::TransformIsLiteral(t,negate,value));
            auto expr=PEGTransformerFactory::TransformIsExpression(t,std::move(child),std::move(tails));
            auto ref=ParseOne(input+" IS "+(negate ? "NOT " : "")+suffix);
            Check(expr->Equals(*ref),"IS test AST differs");
            ParsedExpression *moved=literal==0 ? expr->Cast<OperatorExpression>().children[0].get()
                : expr->Cast<ComparisonExpression>().left->Cast<CastExpression>().child.get();
            Check(moved==identity,"IS child ownership lost");
            auto a=con.Query("SELECT "+expr->ToString()); auto b=con.Query("SELECT "+ref->ToString());
            Check(!a->HasError() && !b->HasError(),"IS execution failed");
            Check(a->GetValue(0,0)==b->GetValue(0,0),"IS result differs");
        }
    }
    auto unchanged=ParseOne("73"); auto *identity=unchanged.get();
    auto pass=PEGTransformerFactory::TransformIsExpression(t,std::move(unchanged),{});
    Check(pass.get()==identity,"absent IS tail must preserve expression");
    bool rejected=false;
    vector<unique_ptr<ParsedExpression>> invalid;
    invalid.push_back(ParseOne("73"));
    try { PEGTransformerFactory::TransformIsExpression(t,ParseOne("1"),std::move(invalid)); }
    catch (const InternalException &) { rejected=true; }
    Check(rejected,"unexpected IS tail class accepted");
    const vector<string> ops={"=","<>","<",">","<=",">="};
    const vector<ExpressionType> types={ExpressionType::COMPARE_EQUAL,ExpressionType::COMPARE_NOTEQUAL,
        ExpressionType::COMPARE_LESSTHAN,ExpressionType::COMPARE_GREATERTHAN,
        ExpressionType::COMPARE_LESSTHANOREQUALTO,ExpressionType::COMPARE_GREATERTHANOREQUALTO};
    for(idx_t i=0;i<ops.size();i++) {
        vector<ComparisonExpressionTail> tails;
        tails.push_back(PEGTransformerFactory::TransformComparisonExpressionTail(t,types[i],{},ParseOne("2")));
        auto expr=PEGTransformerFactory::TransformComparisonExpression(t,ParseOne("1"),std::move(tails));
        Check(expr->Equals(*ParseOne("1 "+ops[i]+" 2")),"comparison AST differs");
    }
    for (bool negate : {false,true}) {
        vector<IsDistinctFromTail> tails;
        tails.push_back(PEGTransformerFactory::TransformIsDistinctFromTail(t,
            negate ? ExpressionType::COMPARE_NOT_DISTINCT_FROM : ExpressionType::COMPARE_DISTINCT_FROM,
            ParseOne("NULL")));
        auto expr=PEGTransformerFactory::TransformIsDistinctFromExpression(t,ParseOne("1"),std::move(tails));
        Check(expr->Equals(*ParseOne(negate ? "1 IS NOT DISTINCT FROM NULL" : "1 IS DISTINCT FROM NULL")),
              "distinct comparison AST differs");
    }
    vector<unique_ptr<ParsedExpression>> chain;
    chain.push_back(PEGTransformerFactory::TransformIsNullOperator(t));
    chain.push_back(PEGTransformerFactory::TransformIsLiteral(t,false,Value::BOOLEAN(true)));
    auto nested=PEGTransformerFactory::TransformIsExpression(t,ParseOne("NULL"),std::move(chain));
    Check(nested->Equals(*ParseOne("(NULL IS NULL) IS TRUE")),"nested IS tail ordering differs");
    auto keyword=PEGTransformerFactory::TransformNotNullKeyword(t);
    auto op=PEGTransformerFactory::TransformNotNullOperator(t);
    Check(keyword->GetExpressionType()==ExpressionType::OPERATOR_IS_NOT_NULL &&
          op->GetExpressionType()==ExpressionType::OPERATOR_IS_NOT_NULL,"NOT NULL token mappings differ");
    std::cout<<"PASS struct/comparison: quoted aliases, ownership, 18 live IS cases, six comparisons, empty/passthrough/rejection\n";
}
#endif
