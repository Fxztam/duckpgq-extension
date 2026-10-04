#ifdef PGQ_TRANSFORMER_TEST
#include "duckpgq/third_party/duckdb_peg_parser/peg/transformer/peg_transformer.hpp"
#include "duckdb.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb/parser/expression/operator_expression.hpp"
#include <iostream>
#include <stdexcept>
using namespace duckdb;using namespace duckdb::duckpgq_peg;
static unique_ptr<ParsedExpression> P(const string&s){auto v=Parser::ParseExpressionList(s);return std::move(v[0]);}
static void Check(bool b,const char*m){if(!b)throw std::runtime_error(m);}
static Value Run(Connection &c,const string&s){auto r=c.Query("SELECT "+s);if(r->HasError())throw std::runtime_error(r->GetError());return r->GetValue(0,0);}
static bool Same(const Value&a,const Value&b){return a.IsNull()||b.IsNull()?a.IsNull()&&b.IsNull():a==b;}
void TestChildOperators(PEGTransformer&t){
    DuckDB db(nullptr);Connection c(db);
    for(const vector<string> args:{vector<string>{"NULL","73","99"},vector<string>{"NULL","NULL"},vector<string>{"0","73"},vector<string>{"'text'","'fallback'"}}){
        vector<unique_ptr<ParsedExpression>> children;vector<ParsedExpression*> ids;
        string ref="coalesce(";for(auto &s:args){if(!ids.empty())ref+=",";ref+=s;auto p=P(s);ids.push_back(p.get());children.push_back(std::move(p));}ref+=")";
        auto expr=PEGTransformerFactory::TransformCoalesceExpression(t,std::move(children));
        Check(expr->Equals(*P(ref)),"COALESCE AST mismatch");
        auto &actual=expr->Cast<OperatorExpression>().children;Check(actual.size()==ids.size(),"COALESCE arity");
        for(idx_t i=0;i<ids.size();i++)Check(actual[i].get()==ids[i],"COALESCE ownership/order");
        Check(Same(Run(c,expr->ToString()),Run(c,ref)),"COALESCE runtime mismatch");
    }
    for(const string input:{"CAST('bad' AS INTEGER)","CAST('73' AS INTEGER)","NULL","ln(-1)"}){
        auto child=P(input);auto *id=child.get();
        auto expr=PEGTransformerFactory::TransformTryExpression(t,std::move(child));
        Check(expr->Equals(*P("TRY("+input+")")),"TRY AST mismatch");
        Check(expr->Cast<OperatorExpression>().children.size()==1 && expr->Cast<OperatorExpression>().children[0].get()==id,"TRY ownership");
        Check(Same(Run(c,expr->ToString()),Run(c,"TRY("+input+")")),"TRY runtime mismatch");
    }
    auto star=P("COLUMNS(*)");auto *id=star.get();
    auto unpack=PEGTransformerFactory::TransformUnpackExpression(t,std::move(star));
    Check(unpack->Equals(*P("*COLUMNS(*)")),"UNPACK AST mismatch");
    Check(unpack->Cast<OperatorExpression>().children.size()==1&&unpack->Cast<OperatorExpression>().children[0].get()==id,"UNPACK ownership");
    Check(Run(c,"greatest("+unpack->ToString()+") FROM (VALUES (3,9)) t(x,y)")==Value::INTEGER(9),"UNPACK runtime");
    auto binding=PEGTransformerFactory::TransformTryExpression(t,P("missing_column"));
    Check(c.Query("SELECT "+binding->ToString())->HasError(),"TRY swallowed binder error");
    auto invalid=PEGTransformerFactory::TransformUnpackExpression(t,P("73"));
    Check(c.Query("SELECT "+invalid->ToString())->HasError(),"invalid UNPACK accepted");
    std::cout<<"PASS child operators: COALESCE/TRY AST and live NULL/error behavior, UNPACK, ownership/order, binder rejection\n";
}
#endif
