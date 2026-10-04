#ifdef PGQ_TRANSFORMER_TEST
#include "duckpgq/third_party/duckdb_peg_parser/peg/transformer/peg_transformer.hpp"
#include "duckpgq/third_party/duckdb_peg_parser/peg/matcher.hpp"
#include "duckdb/storage/arena_allocator.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb/parser/expression/constant_expression.hpp"
#include "duckdb.hpp"
#include "duckdb/common/exception/conversion_exception.hpp"
#include <stdexcept>
#include <iostream>
using namespace duckdb;using namespace duckdb::duckpgq_peg;
static void Check(bool b,const char*m){if(!b)throw std::runtime_error(m);}
void TestUnaryNumbers(PEGTransformer &) {
    for(const string s:{"0","2147483647","2147483648","-2147483648","9223372036854775807",
        "-9223372036854775808","170141183460469231731687303715884105727",
        "-170141183460469231731687303715884105728","340282366920938463463374607431768211455",
        "1.25","-1.25","1e2","1_000"}) {
        auto actual=PEGTransformerFactory::ConvertNumberToValue(s);
        // PGQ intentionally parses a single negative number atomically. The
        // canonical SQL parser first types the positive magnitude and may widen.
        string expected=s;
        if(s=="-2147483648")expected="CAST('-2147483648' AS INTEGER)";
        if(s=="-9223372036854775808")expected="CAST('-9223372036854775808' AS BIGINT)";
        if(s=="-170141183460469231731687303715884105728")expected="CAST('"+s+"' AS HUGEINT)";
        DuckDB db(nullptr);Connection c(db);auto ref=c.Query("SELECT "+expected);
        Check(!ref->HasError(),"numeric oracle error");
        auto &v=actual->Cast<ConstantExpression>().value;
        if(v.type()!=ref->GetValue(0,0).type() || v!=ref->GetValue(0,0))throw std::runtime_error("numeric mismatch "+s+" actual="+v.ToString()+":"+v.type().ToString()+" expected="+ref->GetValue(0,0).ToString()+":"+ref->GetValue(0,0).type().ToString());
    }
    for(auto v:{Value::INTEGER(NumericLimits<int32_t>::Minimum()),Value::BIGINT(NumericLimits<int64_t>::Minimum())}){
        ConstantExpression expr(v);auto neg=PEGTransformerFactory::TryNegateValue(expr);
        Check(bool(neg),"minimum negation failed");
        Check(neg->Cast<ConstantExpression>().value.ToString()==(v.type().id()==LogicalTypeId::INTEGER?"2147483648":"9223372036854775808"),"minimum negation precision");
    }
    ConstantExpression huge(Value::HUGEINT(NumericLimits<hugeint_t>::Minimum()));
    Check(!PEGTransformerFactory::TryNegateValue(huge),"HUGEINT minimum must fall back");
    // Leaf dispatch is test-local; prefix construction and numeric folding are production code.
    ArenaAllocator arena(Allocator::DefaultAllocator());vector<MatcherToken> tokens;PEGTransformerState state(tokens);
    case_insensitive_map_t<PEGTransformer::AnyTransformFunction> funcs;case_insensitive_map_t<PEGRule> rules;ParserOptions options;
    ParsedExpression *identity=nullptr;
    funcs["op"]=[](PEGTransformer &,ParseResult &p)->unique_ptr<TransformResultValue>{
        return make_uniq<TypedTransformResult<string>>(p.Cast<KeywordParseResult>().keyword);
    };
    funcs["leaf"]=[&](PEGTransformer &,ParseResult &)->unique_ptr<TransformResultValue>{
        auto v=Parser::ParseExpressionList("x");identity=v[0].get();
        return make_uniq<TypedTransformResult<unique_ptr<ParsedExpression>>>(std::move(v[0]));
    };
    PEGTransformer t(arena,state,funcs,rules,options);
    for(const vector<string> ops:{vector<string>{},vector<string>{"-"},vector<string>{"+","-"},vector<string>{"-","-"}}){
        vector<unique_ptr<KeywordParseResult>> owned;vector<reference<ParseResult>> refs;
        for(auto &op:ops){auto v=make_uniq<KeywordParseResult>(op,optional_idx());v->name="op";refs.push_back(*v);owned.push_back(std::move(v));}
        RepeatParseResult repeat(refs,optional_idx());OptionalParseResult opt;
        OptionalParseResult present(&repeat,optional_idx());
        ListParseResult base({},"leaf",optional_idx());
        ListParseResult root({ops.empty()?static_cast<ParseResult&>(opt):static_cast<ParseResult&>(present),base},"PrefixExpression",optional_idx());
        auto actual=PEGTransformerFactory::TransformPrefixExpression(t,root);
        auto *leaf=actual.get();
        for(auto &op:ops){auto &fn=leaf->Cast<FunctionExpression>();Check(fn.is_operator && fn.function_name==op,"prefix order/flag");leaf=fn.children[0].get();}
        Check(leaf==identity,"prefix ownership");
        string sql="x";for(auto i=ops.rbegin();i!=ops.rend();++i)sql=*i+"("+sql+")";
        auto expected=Parser::ParseExpressionList(sql);Check(actual->Equals(*expected[0]),"prefix canonical AST");
    }
    for(const string magnitude:{"2147483648","9223372036854775808","170141183460469231731687303715884105728"}) {
        KeywordParseResult minus("-",optional_idx());minus.name="op";
        RepeatParseResult repeat({minus},optional_idx());OptionalParseResult present(&repeat,optional_idx());
        NumberParseResult number(magnitude,optional_idx());number.name="NumberLiteral";
        OptionalParseResult no_suffix;
        ListParseResult base({number,no_suffix},"BaseExpression",optional_idx());
        ListParseResult root({present,base},"PrefixExpression",optional_idx());
        auto actual=PEGTransformerFactory::TransformPrefixExpression(t,root);
        auto expected=PEGTransformerFactory::ConvertNumberToValue("-"+magnitude);
        Check(actual->Equals(*expected),"atomic negative literal path");
    }
    const string huge_text="340282366920938463463374607431768211456";
    auto big=PEGTransformerFactory::ConvertNumberToValue(huge_text);
    Check(big->Cast<ConstantExpression>().value.type().id()==LogicalTypeId::BIGNUM &&
          big->Cast<ConstantExpression>().value.ToString()==huge_text,"BIGNUM precision lost");
    bool rejected=false;try{PEGTransformerFactory::ConvertNumberToValue("not_a_number");}
    catch(const InvalidInputException &){rejected=true;}
    Check(rejected,"invalid numeric text accepted");
    std::cout<<"PASS unary/numbers: 13 numeric boundaries, widening/fallback, four prefix ASTs, three atomic minima, BIGNUM and invalid text\n";
}
#endif
