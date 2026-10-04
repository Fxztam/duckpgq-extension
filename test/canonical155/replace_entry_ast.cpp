#ifdef PGQ_TRANSFORMER_TEST
#include "duckpgq/third_party/duckdb_peg_parser/peg/transformer/peg_transformer.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb.hpp"
#include <stdexcept>
#include <iostream>
using namespace duckdb;using namespace duckdb::duckpgq_peg;
static unique_ptr<ParsedExpression> P(const string&s){auto v=Parser::ParseExpressionList(s);return std::move(v[0]);}
static void Check(bool b,const char*m){if(!b)throw std::runtime_error(m);}
void TestReplaceEntry(PEGTransformer&t){
    for(const string name:{"MixedCase","a.b","unicode_ö"}) {
        auto column=PEGTransformerFactory::TransformTableReservedColumnName(t,Identifier("Table Name"),Identifier(name));
        Check(column->column_names==vector<string>({"Table Name",name}),"qualified column constructor spelling/order");
        auto parsed=Parser::ParseExpressionList(column->ToString());
        Check(column->Equals(*parsed[0]),"qualified column AST roundtrip");
    }
    DuckDB db(nullptr);Connection connection(db);
    for(const string direction:{"trim","ltrim","rtrim"}) {
        TrimArguments args;if(direction!="trim")args.trim_direction=direction;
        auto value=P("'  hello  '");auto *identity=value.get();args.expressions.push_back(std::move(value));
        auto expr=PEGTransformerFactory::TransformTrimExpression(t,std::move(args));
        // Quote the function name to avoid canonical TRIM grammar's main qualification.
        auto expected_ast=P("\""+direction+"\"('  hello  ')");
        if(!expr->Equals(*expected_ast))throw std::runtime_error("trim AST actual="+expr->ToString()+" expected="+expected_ast->ToString());
        Check(expr->Cast<FunctionExpression>().children[0].get()==identity,"trim ownership");
        auto actual=connection.Query("SELECT "+expr->ToString());
        Check(!actual->HasError(),"trim execution");
        string expected=direction=="trim"?"hello":direction=="ltrim"?"hello  ":"  hello";
        Check(actual->GetValue(0,0)==Value(expected),"trim result");
    }
    for(const string name:{"MixedCase","a.b","quote\"name","unicode_ö"}){
        auto value=P("73");auto *id=value.get();
        auto entry=PEGTransformerFactory::TransformReplaceEntry(t,std::move(value),make_uniq<ColumnRefExpression>(name));
        Check(entry.first==name&&entry.second.get()==id,"REPLACE name or ownership lost");
        auto map=PEGTransformerFactory::TransformReplaceEntrySingle(t,std::move(entry));
        Check(map.size()==1&&map[name].get()==id,"REPLACE single move");
    }
    auto entry=PEGTransformerFactory::TransformReplaceEntry(t,P("1"),P("t.x"));
    Check(entry.first=="x","qualified replacement leaf name");
    bool rejected=false;
    try{PEGTransformerFactory::TransformReplaceEntry(t,P("1"),P("2"));}catch(const InternalException&){rejected=true;}
    Check(rejected,"non-column replacement target accepted");
    vector<pair<string,unique_ptr<ParsedExpression>>> duplicates;
    duplicates.emplace_back("MixedCase",P("1"));duplicates.emplace_back("mixedcase",P("2"));
    rejected=false;try{PEGTransformerFactory::TransformReplaceEntryList(t,std::move(duplicates));}catch(const ParserException&){rejected=true;}
    Check(rejected,"case-insensitive duplicate accepted");
    vector<pair<string,unique_ptr<ParsedExpression>>> entries;
    auto x=P("1");auto *id=x.get();entries.emplace_back("x",std::move(x));entries.emplace_back("y",P("2"));
    auto map=PEGTransformerFactory::TransformReplaceEntryList(t,std::move(entries));
    Check(map.size()==2&&map["x"].get()==id,"REPLACE list ownership");
    std::cout<<"PASS REPLACE entries: quoted/Unicode/qualified names, move ownership, invalid target and case-insensitive duplicates\n";
}
#endif
