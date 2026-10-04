#ifdef PGQ_TRANSFORMER_TEST
#include "duckpgq/third_party/duckdb_peg_parser/peg/transformer/peg_transformer.hpp"
#include "duckdb.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb/parser/expression/between_expression.hpp"
#include "duckdb/parser/expression/operator_expression.hpp"
#include <iostream>
#include <stdexcept>
using namespace duckdb;
using namespace duckdb::duckpgq_peg;
static unique_ptr<ParsedExpression> P(const string &s) { auto v=Parser::ParseExpressionList(s);return std::move(v[0]); }
static void Check(bool b,const char *m) { if(!b)throw std::runtime_error(m); }
static Value Run(Connection &c,const string &s) { auto r=c.Query("SELECT "+s);if(r->HasError())throw std::runtime_error(r->GetError());return r->GetValue(0,0); }
static bool Same(const Value &a,const Value &b) { return a.IsNull() || b.IsNull() ? a.IsNull() && b.IsNull() : a==b; }
void TestRegexBetween(PEGTransformer &t) {
    DuckDB db(nullptr);Connection c(db);
    Check(PEGTransformerFactory::TransformRegexMatchToken(t)=="regexp_full_match","canonical regex must use full matching");
    for(bool neg:{false,true}) for(const string input:{"0","1","2","3","NULL"}) {
        auto in=P(input);auto *ip=in.get();auto lo=P("1");auto *lp=lo.get();auto hi=P("2");auto *hp=hi.get();
        auto range=PEGTransformerFactory::TransformBetweenClause(t,std::move(lo),std::move(hi));
        auto op=PEGTransformerFactory::TransformBetweenInLikeOp(t,neg,std::move(range));
        auto expr=PEGTransformerFactory::TransformBetweenInLikeExpression(t,std::move(in),std::move(op));
        auto &b=(neg ? *expr->Cast<OperatorExpression>().children[0] : *expr).Cast<BetweenExpression>();
        Check(b.input.get()==ip && b.lower.get()==lp && b.upper.get()==hp,"BETWEEN move ownership lost");
        string ref=input+(neg?" NOT BETWEEN 1 AND 2":" BETWEEN 1 AND 2");
        Check(expr->Equals(*P(ref)),"BETWEEN AST mismatch");
        Check(Same(Run(c,expr->ToString()),Run(c,ref)),"BETWEEN result mismatch");
    }
    for(bool insensitive:{false,true}) for(bool neg:{false,true}) for(bool outer_not:{false,true}) {
        string token=insensitive ? (neg ? PEGTransformerFactory::TransformNotRegexInsensitiveMatchOp(t)
            : PEGTransformerFactory::TransformRegexInsensitiveMatchToken(t))
            : (neg ? PEGTransformerFactory::TransformNotSimilarToOp(t) : PEGTransformerFactory::TransformRegexMatchToken(t));
        auto clause=PEGTransformerFactory::TransformLikeClause(t,token,P("'abc'"),{});
        auto expr=PEGTransformerFactory::TransformBetweenInLikeExpression(t,P("'ABC'"),
            PEGTransformerFactory::TransformBetweenInLikeOp(t,outer_not,std::move(clause)));
        string ref="regexp_full_match('ABC','abc'"+string(insensitive?",'i'":"")+")";
        if(neg!=outer_not)ref="NOT ("+ref+")";
        Check(Run(c,expr->ToString())==Run(c,ref),"regex negation/case mismatch");
    }
    auto partial=PEGTransformerFactory::TransformLikeClause(t,PEGTransformerFactory::TransformRegexMatchToken(t),P("'b'"),{});
    auto full=PEGTransformerFactory::TransformBetweenInLikeExpression(t,P("'abc'"),
        PEGTransformerFactory::TransformBetweenInLikeOp(t,false,std::move(partial)));
    Check(Run(c,full->ToString())==Value::BOOLEAN(false),"regex silently changed to partial matching");
    // Independent SQL three-valued reduction, not the generated CASE/list-filter algorithm.
    for(const string op:{"~","!~","~*","!~*"}) for(bool any:{false,true})
        for(const string list:{"[]::VARCHAR[]","['ABC']","['x']","[NULL]::VARCHAR[]","['ABC',NULL]","['x',NULL]"}) {
            ParsedOperator po;po.name=op;po.is_any_all=true;po.is_any=any;
            vector<OtherOperatorTail> tails;
            tails.push_back(PEGTransformerFactory::TransformOtherOperatorTail(t,po,P(list)));
            auto expr=PEGTransformerFactory::TransformOtherOperatorExpression(t,P("'ABC'"),std::move(tails));
            bool neg=op[0]=='!';bool ci=op.back()=='*';
            string match="regexp_full_match('ABC',p"+string(ci?",'i'":"")+")";
            if(neg)match="NOT ("+match+")";
            string ref="TRUE "+string(any?"= ANY":"= ALL")+"(SELECT "+match+" FROM unnest("+list+") AS u(p))";
            Check(Same(Run(c,expr->ToString()),Run(c,ref)),"regex ANY/ALL three-valued mismatch");
        }
    bool rejected=false;
    try { PEGTransformerFactory::TransformLikeClause(t,PEGTransformerFactory::TransformRegexInsensitiveMatchToken(t),P("'x'"),P("'!'")); }
    catch(const ParserException &){rejected=true;}
    Check(rejected,"case-insensitive regex ESCAPE accepted");
    std::cout<<"PASS regex/BETWEEN: 10 range AST/move/runtime cases, full-match sentinel, eight negations, 48 ANY/ALL CASE results, ESCAPE rejection\n";
}
#endif
