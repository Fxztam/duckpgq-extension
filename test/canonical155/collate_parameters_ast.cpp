#ifdef PGQ_TRANSFORMER_TEST
#include "duckpgq/third_party/duckdb_peg_parser/peg/transformer/peg_transformer.hpp"
#include "duckdb.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb/parser/expression/collate_expression.hpp"
#include "duckdb/parser/expression/parameter_expression.hpp"
#include <iostream>
#include <stdexcept>
using namespace duckdb;using namespace duckdb::duckpgq_peg;
static unique_ptr<ParsedExpression> P(const string &s){auto v=Parser::ParseExpressionList(s);return std::move(v[0]);}
static void Check(bool ok,const char *m){if(!ok)throw std::runtime_error(m);}
void TestCollateParameters(PEGTransformer &t){
    DuckDB db(nullptr);Connection c(db);
    for(const string spec:{"'nocase'","nocase","nocase.noaccent"}){
        auto child=P("'AbC'");auto *id=child.get();vector<unique_ptr<ParsedExpression>> tails;tails.push_back(P(spec));
        auto expr=PEGTransformerFactory::TransformCollateExpression(t,std::move(child),std::move(tails));
        Check(expr->Cast<CollateExpression>().child.get()==id,"collation child ownership");
        string name=spec=="'nocase'"?"nocase":spec;
        Check(expr->Equals(*P("'AbC' COLLATE "+name)),"collation AST mismatch");
        auto r=c.Query("SELECT ("+expr->ToString()+") = 'abc'");
        Check(!r->HasError() && r->GetValue(0,0)==Value::BOOLEAN(true),"collation runtime mismatch");
    }
    bool rejected=false;vector<unique_ptr<ParsedExpression>> bad;bad.push_back(P("abs(1)"));
    try{PEGTransformerFactory::TransformCollateExpression(t,P("'x'"),std::move(bad));}catch(const NotImplementedException &){rejected=true;}
    Check(rejected,"invalid collation class accepted");
    t.ClearParameters();
    auto a=PEGTransformerFactory::TransformAnonymousParameter(t);
    auto b=PEGTransformerFactory::TransformAnonymousParameter(t);
    Check(a->Cast<ParameterExpression>().identifier=="1" && b->Cast<ParameterExpression>().identifier=="2" && t.ParamCount()==2,"anonymous numbering");
    t.ClearParameters();
    for(bool question:{false,true}){
        auto x=question?PEGTransformerFactory::TransformQuestionMarkNumberedParameter(t,P("7")):PEGTransformerFactory::TransformNumberedParameter(t,P("7"));
        Check(x->Cast<ParameterExpression>().identifier=="7" && t.ParamCount()==7,"explicit parameter numbering");
        Check(x->Equals(*P("$7")),"parameter AST mismatch");
    }
    for(const string invalid:{"0","-1"})for(bool question:{false,true}){
        rejected=false;
        try{if(question)PEGTransformerFactory::TransformQuestionMarkNumberedParameter(t,P(invalid));
            else PEGTransformerFactory::TransformNumberedParameter(t,P(invalid));}
        catch(const ParserException &){rejected=true;}
        Check(rejected,"nonpositive parameter accepted");
    }
    t.ClearParameters();
    auto n=PEGTransformerFactory::TransformColLabelParameter(t,"MixedName");
    auto n2=PEGTransformerFactory::TransformColLabelParameter(t,"mixedname");
    Check(t.ParamCount()==1 && n->Cast<ParameterExpression>().identifier=="MixedName","named parameter spelling/reuse");
    rejected=false;try{PEGTransformerFactory::TransformAnonymousParameter(t);}catch(const NotImplementedException &){rejected=true;}
    Check(rejected,"named/anonymous mixing accepted");
    t.ClearParameters();PEGTransformerFactory::TransformAnonymousParameter(t);
    rejected=false;try{PEGTransformerFactory::TransformColLabelParameter(t,"name");}catch(const NotImplementedException &){rejected=true;}
    Check(rejected,"anonymous/named mixing accepted");
    t.ClearParameters();
    auto pos=PEGTransformerFactory::TransformPositionalExpression(t,P("2"));
    Check(pos->Equals(*P("#2")),"positional AST mismatch");
    for(const string invalid:{"0","-1"}){
        rejected=false;try{PEGTransformerFactory::TransformPositionalExpression(t,P(invalid));}catch(const ParserException &){rejected=true;}
        Check(rejected,"invalid positional reference accepted");
    }
    std::cout<<"PASS collate/parameters: three live collations, AST/ownership, anonymous/explicit/named reuse, mixing and positional negatives\n";
}
#endif
