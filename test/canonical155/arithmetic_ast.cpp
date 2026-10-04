#ifdef PGQ_TRANSFORMER_TEST
#include "duckpgq/third_party/duckdb_peg_parser/peg/transformer/peg_transformer.hpp"
#include "duckdb.hpp"
#include "duckdb/parser/parser.hpp"
#include <stdexcept>
#include <iostream>
using namespace duckdb;using namespace duckdb::duckpgq_peg;
static unique_ptr<ParsedExpression> P(const string &s){auto v=Parser::ParseExpressionList(s);return std::move(v[0]);}
void TestArithmetic(PEGTransformer &t) {
    using Fn=unique_ptr<ParsedExpression>(*)(PEGTransformer &,unique_ptr<ParsedExpression>,optional<vector<BinaryExpressionTail>>);
    vector<Fn> fns={PEGTransformerFactory::TransformBitwiseExpression,PEGTransformerFactory::TransformAdditiveExpression,
        PEGTransformerFactory::TransformMultiplicativeExpression,PEGTransformerFactory::TransformExponentiationExpression};
    vector<string> ops={"&","-","/","^"};
    DuckDB db(nullptr);Connection c(db);
    for(idx_t i=0;i<fns.size();i++) {
        auto first=P("8");auto *id=first.get();vector<BinaryExpressionTail> tails;
        tails.push_back({ops[i],P("2"),optional_idx()});
        tails.push_back({ops[i],P("2"),optional_idx()});
        auto expr=fns[i](t,std::move(first),std::move(tails));
        auto &outer=expr->Cast<FunctionExpression>();auto &inner=outer.children[0]->Cast<FunctionExpression>();
        if(!outer.is_operator || !inner.is_operator || inner.children[0].get()!=id)throw std::runtime_error("arithmetic flags/ownership");
        auto ref=P("8 "+ops[i]+" 2 "+ops[i]+" 2");
        if(!expr->Equals(*ref))throw std::runtime_error("arithmetic association AST differs: "+ops[i]);
        auto a=c.Query("SELECT "+expr->ToString());auto b=c.Query("SELECT "+ref->ToString());
        if(a->HasError()||b->HasError()||a->GetValue(0,0)!=b->GetValue(0,0))throw std::runtime_error("arithmetic execution");
        auto single=P("73");auto *same=single.get();
        if(fns[i](t,std::move(single),{}).get()!=same)throw std::runtime_error("arithmetic passthrough ownership");
    }
    const bool saved=t.options.integer_division;
    t.options.integer_division=true;
    vector<BinaryExpressionTail> tail;tail.push_back({"/",P("2"),optional_idx()});
    auto integer=PEGTransformerFactory::TransformMultiplicativeExpression(t,P("7"),std::move(tail));
    t.options.integer_division=saved;
    if(integer->Cast<FunctionExpression>().function_name!="//")throw std::runtime_error("integer division option lost");
    auto r=c.Query("SELECT "+integer->ToString());
    if(r->HasError()||r->GetValue(0,0)!=Value::INTEGER(3))throw std::runtime_error("integer division result");
    std::cout<<"PASS arithmetic: four operator chains, association, flags, ownership, passthrough and integer division\n";
}
#endif
