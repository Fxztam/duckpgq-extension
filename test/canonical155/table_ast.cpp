#ifdef PGQ_TRANSFORMER_TEST
#include "duckpgq/third_party/duckdb_peg_parser/peg/transformer/peg_transformer.hpp"
#include "duckpgq/compat/name_metadata.hpp"
#include "duckdb/parser/parsed_data/create_table_info.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb.hpp"
#include <iostream>
#include <stdexcept>
using namespace duckdb;using namespace duckdb::duckpgq_peg;using F=PEGTransformerFactory;
static void Check(bool b,const char*m){if(!b)throw std::runtime_error(m);}
void TestTableMatrix(PEGTransformer &t);
void TestTable(PEGTransformer&t){
 TestTableMatrix(t);
 DuckDB db(nullptr);Connection c(db);
 vector<ColumnConstraintEntry> constraints;
 constraints.push_back(F::TransformDefaultValue(t,make_uniq<ConstantExpression>(Value(73))));
 constraints.push_back(F::TransformNotNullConstraint(t,true));
 auto column=F::TransformColumnDefinition(t,{"x"},LogicalType::INTEGER,{},false,std::move(constraints));
 vector<CreateTableColumnElement> elements;
 elements.push_back(F::TransformCreateTableColumnDefinition(t,std::move(column)));
 auto columns=F::TransformCreateTableColumnList(t,std::move(elements));
 auto def=F::TransformCreateColumnList(t,std::move(columns),{},{});
 auto actual=F::TransformCreateTableStmt(t,{},duckpgq_compat::MakeQualifiedName(Identifier("Mixed_ö")),std::move(def),{});
 auto &info=actual->info->Cast<CreateTableInfo>();
 Check(info.table=="Mixed_ö" && info.constraints.size()==1,"table name/constraints");
 auto r=c.Query(std::move(actual));if(r->HasError())throw std::runtime_error(r->GetError());
 Check(!c.Query("INSERT INTO \"Mixed_ö\" DEFAULT VALUES")->HasError(),"table default insertion");
 r=c.Query("SELECT x FROM \"Mixed_ö\"");Check(!r->HasError() && r->GetValue(0,0)==Value(73),"table default value");
 Check(c.Query("INSERT INTO \"Mixed_ö\" VALUES(NULL)")->HasError(),"NOT NULL enforcement");
 for(bool empty:{false,true}){
   Parser parser;parser.ParseQuery("SELECT 19 AS x");auto *query=parser.statements[0].get();
   auto as=F::TransformCreateTableAs(t,{},{},{},std::move(parser.statements[0]),empty);
   Check(as.select_statement.get()==query,"CTAS statement move");
   auto stmt=F::TransformCreateTableStmt(t,{},duckpgq_compat::MakeQualifiedName(Identifier(empty?"no_rows":"with_rows")),std::move(as),{});
   Check(!c.Query(std::move(stmt))->HasError(),"CTAS execution");
   r=c.Query(string("SELECT count(*) FROM ")+(empty?"no_rows":"with_rows"));
   Check(!r->HasError() && r->GetValue(0,0).GetValue<int64_t>()==(empty?0:1),"CTAS WITH DATA modes");
 }
 int bad=0;
 try{F::TransformCreateColumnList(t,{},{},{});}catch(const ParserException&){bad++;}
 try{F::TransformColumnDefinition(t,{"x"},{},{},false,{});}catch(const ParserException&){bad++;}
 try{CreateTableDefinition blank;F::TransformCreateTableStmt(t,{},duckpgq_compat::MakeQualifiedName(Identifier("")),std::move(blank),{});}catch(const ParserException&){bad++;}
 try{F::TransformStoredGeneratedColumn(t);}catch(const InvalidInputException&){bad++;}
 try{F::TransformCascadeKeyAction(t);}catch(const ParserException&){bad++;}
 Check(bad==5,"table negative cases");
 std::cout<<"PASS CREATE TABLE: column/default/NOT NULL live execution, CTAS move/data modes, five rejections\n";
}
#endif
