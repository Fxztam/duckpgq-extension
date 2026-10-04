#ifdef PGQ_TRANSFORMER_TEST
#include "duckpgq/third_party/duckdb_peg_parser/peg/transformer/peg_transformer.hpp"
#include "duckdb/parser/statement/copy_statement.hpp"
#include "duckdb/parser/statement/copy_database_statement.hpp"
#include "duckdb/parser/statement/pragma_statement.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb.hpp"
#include <iostream>
#include <stdexcept>
using namespace duckdb;
using namespace duckdb::duckpgq_peg;
using F=PEGTransformerFactory;
static void Check(bool b,const char*m){if(!b)throw std::runtime_error(m);}
static unique_ptr<BaseTableRef> Table() {
 auto r=make_uniq<BaseTableRef>();r->catalog_name="db";r->schema_name="s.dot";r->table_name="Mixed";return r;
}
void TestCopy(PEGTransformer&t){
 for(bool from:{false,true}) {
   auto s=F::TransformCopyTable(t,Table(),vector<string>{"unicode_ö","x"},from,
      F::TransformCopyFileNameStringLiteral(t,"sample.CSV.gz"),{});
   auto &i=*s->Cast<CopyStatement>().info;
   Check(i.catalog=="db" && i.schema=="s.dot" && i.table=="Mixed","COPY qualified target");
   Check(i.select_list==vector<string>({"unicode_ö","x"}) && i.is_from==from,"COPY columns/direction");
   Check(i.file_path=="sample.CSV.gz" && i.format=="csv","COPY path/format");
 }
 for(auto pair:{std::make_pair("x.PARQUET.zst","parquet"),std::make_pair("no_extension",""),std::make_pair("x.","")})
   Check(F::ExtractFormat(pair.first)==pair.second,"COPY inferred format");
 vector<GenericCopyOption> opts;
 opts.emplace_back("format",Value("csv"));opts.emplace_back("header",Value(true));
 opts.push_back(F::TransformPartitionByOption(t,{"Mixed"}));
 auto s=F::TransformCopyTable(t,Table(),{},false,F::TransformCopyFileNameStringLiteral(t,"x.data"),opts);
 auto &i=*s->Cast<CopyStatement>().info;
 Check(i.format=="csv" && !i.is_format_auto_detected && i.options.count("format")==0,"explicit COPY format");
 Check(i.parsed_options.at("header")->Cast<ConstantExpression>().value.GetValue<bool>(),"COPY header");
 Check(i.parsed_options.at("partition_by")->ToString().find("Mixed")!=string::npos,"COPY partition column");
 GenericCopyOption dynamic;dynamic.name=Identifier("custom");dynamic.expression=make_uniq<ConstantExpression>(Value(7));
 opts={};opts.push_back(dynamic);
 auto dyn=make_uniq<ColumnRefExpression>("path");auto *identity=dyn.get();
 s=F::TransformCopyTable(t,Table(),{},false,std::move(dyn),opts);
 Check(s->Cast<CopyStatement>().info->file_path_expression.get()==identity,"COPY path move");
 Check(s->Cast<CopyStatement>().info->parsed_options.at("custom").get()!=opts[0].expression.get(),"COPY option deep copy");
 opts={};opts.emplace_back("HEADER",Value(true));opts.emplace_back("header",Value(false));
 bool bad=false;try{F::TransformCopyTable(t,Table(),{},true,F::TransformCopyFileNameStringLiteral(t,"x"),opts);}
 catch(const ParserException&){bad=true;}Check(bad,"COPY duplicate options");
 for(const char *name:{"format","partition_by"}) {
   GenericCopyOption empty;empty.name=Identifier(name);opts={};opts.push_back(empty);
   bool rejected=false;
   try {F::TransformCopyTable(t,Table(),{},false,F::TransformCopyFileNameStringLiteral(t,"x"),opts);}
   catch(const ParserException&) {Check(string(name)=="format","wrong COPY parser rejection");rejected=true;}
   catch(const BinderException&) {Check(string(name)=="partition_by","wrong COPY binder rejection");rejected=true;}
   Check(rejected,"COPY empty option accepted");
 }
 auto stdout_name=F::TransformCopyFileNameIdentifier(t,Identifier("stdout"));
 Check(stdout_name->Cast<ConstantExpression>().value==Value("/dev/stdout"),"COPY stdout mapping");
 auto dotted=F::TransformCopyFileNameIdentifierColId(t,Identifier("part.csv"));
 Check(dotted->Cast<ConstantExpression>().value==Value("part.csv"),"COPY dotted filename");
 Parser parser;parser.ParseQuery("SELECT 73 AS value");
 auto select=unique_ptr_cast<SQLStatement,SelectStatement>(std::move(parser.statements[0]));
 auto *node=select->node.get();
 s=F::TransformCopySelect(t,std::move(select),F::TransformCopyFileNameStringLiteral(t,"x.csv"),{});
 Check(s->Cast<CopyStatement>().info->select_statement.get()==node,"COPY SELECT move");
 auto pragma=F::TransformCopyFromDatabaseWithoutFlag(t,Identifier("src"),Identifier("dst"));
 Check(pragma->Cast<PragmaStatement>().info->parameters.size()==2,"COPY database pragma arguments");
 Check(pragma->Cast<PragmaStatement>().info->parameters[0]->Cast<ConstantExpression>().value==Value("src") &&
       pragma->Cast<PragmaStatement>().info->parameters[1]->Cast<ConstantExpression>().value==Value("dst"),
       "COPY database pragma source/target");
 DuckDB db(nullptr);Connection c(db);
 for(const char *sql:{"ATTACH ':memory:' AS src","ATTACH ':memory:' AS dst","CREATE TABLE src.items AS SELECT 73 AS id"})
   Check(!c.Query(sql)->HasError(),"COPY database fixture");
 for(auto flag:{CopyDatabaseType::COPY_SCHEMA,CopyDatabaseType::COPY_DATA}) {
   auto statement=F::TransformCopyFromDatabaseWithFlag(t,Identifier("src"),Identifier("dst"),flag);
   auto result=c.Query(std::move(statement));
   if(result->HasError())throw std::runtime_error(result->GetError());
 }
 auto result=c.Query("SELECT id FROM dst.items");
 Check(!result->HasError() && result->GetValue(0,0)==Value(73),"COPY database live result");
 std::cout<<"PASS COPY: target/path/options/ownership, duplicate rejection, SELECT and live database schema/data copy\n";
}
#endif
