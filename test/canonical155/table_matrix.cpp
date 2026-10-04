#ifdef PGQ_TRANSFORMER_TEST
#include "duckpgq/third_party/duckdb_peg_parser/peg/transformer/peg_transformer.hpp"
#include "duckpgq/compat/name_metadata.hpp"
#include "duckdb/parser/parsed_data/create_table_info.hpp"
#include "duckdb/parser/constraints/not_null_constraint.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb.hpp"
#include <iostream>
#include <stdexcept>
using namespace duckdb;using namespace duckdb::duckpgq_peg;using F=PEGTransformerFactory;
static void Check(bool b,const string&m){if(!b)throw std::runtime_error(m);}
static unique_ptr<ParsedExpression> Expr(const string &s) {
 auto list=Parser::ParseExpressionList(s);return std::move(list[0]);
}
static CreateTableColumnElement Col(PEGTransformer&t,const string&name,LogicalType type,
                                    vector<ColumnConstraintEntry> constraints={}) {
 return F::TransformCreateTableColumnDefinition(t,F::TransformColumnDefinition(t,{name},type,{},false,std::move(constraints)));
}
static unique_ptr<CreateStatement> Table(PEGTransformer&t,const string&name,vector<CreateTableColumnElement> elems) {
 return F::TransformCreateTableStmt(t,{},duckpgq_compat::MakeQualifiedName(Identifier(name)),
   F::TransformCreateColumnList(t,F::TransformCreateTableColumnList(t,std::move(elems)),{},{}),{});
}
static void Run(Connection&c,unique_ptr<SQLStatement> s){auto r=c.Query(std::move(s));Check(!r->HasError(),r->HasError()?r->GetError():"");}
static void SQL(Connection&c,const string&s){auto r=c.Query(s);Check(!r->HasError(),r->HasError()?r->GetError():"");}
void TestTableMatrix(PEGTransformer&t) {
 DuckDB db(nullptr);Connection c(db);
 for(bool primary:{false,true}) {
   vector<CreateTableColumnElement> elems;elems.push_back(Col(t,"a",LogicalType::INTEGER));
   elems.push_back(F::TransformCreateTableConstraint(t,F::TransformCheckConstraint(t,Expr("a > 0")).constraint));
   vector<ColumnConstraintEntry> cc;cc.push_back(F::TransformNotNullConstraint(t,true));
   cc.push_back(primary?F::TransformPrimaryKeyConstraint(t):F::TransformUniqueConstraint(t));
   elems.push_back(Col(t,"b",LogicalType::INTEGER,std::move(cc)));
   string name=primary?"inter_pk":"inter_unique";
   auto actual=Table(t,name,std::move(elems));
   Parser p;p.ParseQuery("CREATE TABLE "+name+"(a INTEGER, CHECK(a>0), b INTEGER NOT NULL "+string(primary?"PRIMARY KEY":"UNIQUE")+")");
   auto &a=actual->info->Cast<CreateTableInfo>();auto &b=p.statements[0]->Cast<CreateStatement>().info->Cast<CreateTableInfo>();
   for(idx_t i=0;i<a.constraints.size();i++)if(a.constraints[i]->type==ConstraintType::NOT_NULL)
     Check(a.constraints[i]->Cast<NotNullConstraint>().index.index==1,"interleaved constraint has wrong column index");
   Check(a.ToString()==b.ToString(),"interleaved canonical AST");
   Run(c,std::move(actual));SQL(c,"INSERT INTO "+name+" VALUES(1,7)");
   Check(c.Query("INSERT INTO "+name+" VALUES(2,7)")->HasError(),"interleaved uniqueness");
   Check(c.Query("INSERT INTO "+name+" VALUES(2,NULL)")->HasError(),"interleaved not null");
   Check(c.Query("INSERT INTO "+name+" VALUES(-1,8)")->HasError(),"interleaved check");
 }
 SQL(c,"CREATE TABLE parent(id INTEGER PRIMARY KEY); INSERT INTO parent VALUES(7)");
 for(bool mismatch:{false,true}) {
   auto base=make_uniq<BaseTableRef>();base->table_name="parent";
   auto fk=F::TransformForeignKeyConstraint(t,std::move(base),vector<string>{"id"},F::TransformKeyActions(t,{},{}));
   vector<ColumnConstraintEntry> cc;cc.push_back(std::move(fk));
   vector<CreateTableColumnElement> elems;elems.push_back(Col(t,"parent_id",mismatch?LogicalType::VARCHAR:LogicalType::INTEGER,std::move(cc)));
   auto actual=Table(t,mismatch?"bad_fk":"child",std::move(elems));
   auto r=c.Query(std::move(actual));
   if(mismatch){Check(r->HasError() && r->GetErrorType()==ExceptionType::BINDER,"FK type mismatch diagnosis");continue;}
   Check(!r->HasError(),"FK creation");SQL(c,"INSERT INTO child VALUES(7)");
   Check(c.Query("INSERT INTO child VALUES(8)")->HasError(),"FK missing parent");
   Check(c.Query("DELETE FROM parent WHERE id=7")->HasError(),"FK referenced parent deletion");
 }
 vector<CreateTableColumnElement> elems;elems.push_back(Col(t,"x",LogicalType::INTEGER));
 auto generated=F::TransformGeneratedColumn(t,true,Expr("x + 1"),true);
 elems.push_back(F::TransformCreateTableColumnDefinition(t,F::TransformColumnDefinition(t,{"y"},{},std::move(generated),false,{})));
 Run(c,Table(t,"generated_t",std::move(elems)));SQL(c,"INSERT INTO generated_t(x) VALUES(7); UPDATE generated_t SET x=9");
 auto r=c.Query("SELECT y FROM generated_t");Check(!r->HasError() && r->GetValue(0,0)==Value(10),"generated recalculation");
 int rejected=0;
 try{F::TransformGeneratedColumn(t,true,Expr("tbl.x"),true);}catch(const ParserException&){rejected++;}
 try{vector<ColumnConstraintEntry> cc;cc.push_back(F::TransformDefaultValue(t,Expr("1")));
 F::TransformColumnDefinition(t,{"y"},{},F::TransformGeneratedColumn(t,true,Expr("1"),true),false,std::move(cc));}
 catch(const ParserException&){rejected++;}
 Check(rejected==2,"generated forbidden qualifier/default");
 vector<ColumnConstraintEntry> cc;cc.push_back(F::TransformColumnCollation(t,{"nocase"}));
 auto varchar=F::TransformType(t,F::TransformCharacterSimpleType(t,{}),{});
 elems.clear();elems.push_back(Col(t,"txt",varchar,std::move(cc)));Run(c,Table(t,"collated_t",std::move(elems)));
 SQL(c,"INSERT INTO collated_t VALUES('Hello')");
 r=c.Query("SELECT txt='hello' FROM collated_t");Check(!r->HasError() && r->GetValue(0,0)==Value(true),"collation runtime");
 int collation_rejected=0;
 for(bool generated_case:{false,true}) {
   vector<ColumnConstraintEntry> invalid;invalid.push_back(F::TransformColumnCollation(t,{"nocase"}));
   try {
     if(generated_case)
       F::TransformColumnDefinition(t,{"bad"},{},F::TransformGeneratedColumn(t,true,Expr("'x'"),true),false,std::move(invalid));
     else
       F::TransformColumnDefinition(t,{"bad"},F::TransformType(t,F::TransformSimpleNumericType(t,"INTEGER"),{}),{},false,std::move(invalid));
   }catch(const ParserException&){collation_rejected++;}
 }
 Check(collation_rejected==2,"collation rejects generated and non-VARCHAR columns");
 for(bool sorted:{false,true}) {
   vector<unique_ptr<ParsedExpression>> keys;keys.push_back(Expr("x"));auto *identity=keys[0].get();
   auto options=sorted?F::TransformSortedOptPartitionOptions(t,std::move(keys),{}):F::TransformPartitionOptSortedOptions(t,std::move(keys),{});
   Parser p;p.ParseQuery("SELECT 1 AS x");
   auto def=F::TransformCreateTableAs(t,{},std::move(options),{},std::move(p.statements[0]),{});
   auto statement=F::TransformCreateTableStmt(t,{},duckpgq_compat::MakeQualifiedName(Identifier(sorted?"sorted_t":"partition_t")),std::move(def),{});
   auto &info=statement->info->Cast<CreateTableInfo>();
   Check((sorted?info.sort_keys:info.partition_keys)[0].get()==identity,"partition/sort move");
   r=c.Query(std::move(statement));
   auto reference=c.Query(string("CREATE TABLE reference_t ")+(sorted?"SORTED BY":"PARTITIONED BY")+" (x) AS SELECT 1 AS x");
   string diagnostic=string(sorted?"SORTED BY":"PARTITIONED BY")+" is not supported for tables in a duckdb catalog";
   Check(r->HasError() && reference->HasError() &&
         r->GetErrorType()==ExceptionType::CATALOG && reference->GetErrorType()==ExceptionType::CATALOG &&
         r->GetError().find(diagnostic)!=string::npos && reference->GetError().find(diagnostic)!=string::npos,
         "partition/sort canonical catalog rejection");
 }
 std::cout<<"PASS TABLE matrix: interleaved constraints, FK enforcement/types, generated/collated columns, partition/sort ownership and rejection\n";
}
#endif
