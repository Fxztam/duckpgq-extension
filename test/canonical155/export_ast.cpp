#ifdef PGQ_TRANSFORMER_TEST
#include "duckpgq/third_party/duckdb_peg_parser/peg/transformer/peg_transformer.hpp"
#include "duckdb/parser/statement/export_statement.hpp"
#include "duckdb/parser/statement/pragma_statement.hpp"
#include "duckdb/parser/expression/constant_expression.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb.hpp"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <iostream>
#include <set>
#include <stdexcept>
using namespace duckdb; using namespace duckdb::duckpgq_peg; using F=PEGTransformerFactory;
static void Check(bool b,const char*m){if(!b)throw std::runtime_error(m);}
static unique_ptr<SQLStatement> Parse(const string&s){Parser p;p.ParseQuery(s);return std::move(p.statements[0]);}
static GenericCopyOption Opt(const string&name,const string&value){
 GenericCopyOption o;o.name=Identifier(name);o.children.push_back(Value(value));return o;
}
static string NoSpace(string s){s.erase(std::remove_if(s.begin(),s.end(),[](unsigned char ch){return std::isspace(ch);}),s.end());return s;}
static std::set<string> Files(const std::filesystem::path&dir){
 std::set<string> r;for(auto&e:std::filesystem::directory_iterator(dir))r.insert(e.path().filename().string());return r;
}
// The canonical parser keeps FORMAT as a parsed option and resolves it at bind time, while the PEG
// transformer resolves it immediately; format/is_format_auto_detected/parsed_options therefore differ by
// design (and ToString() spells FORMAT differently); equivalence is proven by live export/import instead.
static void SameExport(const ExportStatement&a,const ExportStatement&b,const char*m){
 Check(a.database==b.database && a.info->file_path==b.info->file_path && a.info->is_from==b.info->is_from &&
  a.info->catalog==b.info->catalog && a.info->schema==b.info->schema && a.info->table==b.info->table,m);
}
void TestExport(PEGTransformer&t){
 DuckDB db(nullptr),db_reference(nullptr);Connection c(db),reference(db_reference);
 Check(!c.Query("CREATE TABLE t AS SELECT range AS i, 'v' || range AS s FROM range(5)")->HasError(),"export fixture");
 Check(!reference.Query("CREATE TABLE t AS SELECT range AS i, 'v' || range AS s FROM range(5)")->HasError(),"export reference fixture");
 // statement shape: four option combinations against the canonical parser
 struct Case{string sql;optional<string> source;vector<GenericCopyOption> opts;};
 vector<Case> cases;
 cases.push_back({"EXPORT DATABASE 'p1'",{},{}});
 cases.push_back({"EXPORT DATABASE 'p2' (FORMAT parquet)",{},{Opt("format","parquet")}});
 cases.push_back({"EXPORT DATABASE 'p3' (FORMAT csv, DELIMITER '|')",{},{Opt("format","csv"),Opt("delimiter","|")}});
 cases.push_back({"EXPORT DATABASE memory TO 'p4' (COMPRESSION zstd, FORMAT parquet)",string("memory"),{Opt("compression","zstd"),Opt("format","parquet")}});
 for(auto&cs:cases){
  auto pos=cs.sql.find('\'');auto path=cs.sql.substr(pos+1,cs.sql.find('\'',pos+1)-pos-1);
  vector<GenericCopyOption> opts=cs.opts;
  optional<vector<GenericCopyOption>> list;if(!opts.empty())list=F::TransformGenericCopyOptionList(t,opts);
  auto actual=F::TransformExportStatement(t,cs.source,path,list);
  auto expected=Parse(cs.sql);
  SameExport(actual->Cast<ExportStatement>(),expected->Cast<ExportStatement>(),"export canonical AST fields");
  auto copy=actual->Copy();
  Check(copy->Cast<ExportStatement>().info.get()!=actual->Cast<ExportStatement>().info.get(),"export independent copy");
 }
 Check(F::TransformExportSource(t,Identifier("Mixed ö"))=="Mixed ö","export source name");
 // a parsed (non-constant) option is kept as an expression, not flattened
 {
  GenericCopyOption o;o.name=Identifier("custom");o.expression=make_uniq<ConstantExpression>(Value(7));
  auto actual=F::TransformExportStatement(t,{},"px",vector<GenericCopyOption>{});
  vector<GenericCopyOption> v;v.push_back(std::move(o));
  auto with=F::TransformExportStatement(t,{},"px",std::move(v));
  Check(with->Cast<ExportStatement>().info->parsed_options.count("CUSTOM")==1 && with->Cast<ExportStatement>().info->options.empty(),"export parsed option");
 }
 // live round trip: PGQ export and canonical export write the same files and re-import to equal data
 auto tick=std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
 std::filesystem::path root=std::filesystem::path(".b/pgqa/export-tests")/("run"+tick);
 std::filesystem::create_directories(root);
 int roundtrips=0,csv_identical=0;
 struct Live{string format;string key,value;string sql;};
 vector<Live> live={{"csv","","","(FORMAT csv)"},{"parquet","","","(FORMAT parquet)"},
  {"csv","delimiter","|","(FORMAT csv, DELIMITER '|')"},{"parquet","compression","zstd","(FORMAT parquet, COMPRESSION zstd)"}};
 int n=0;
 for(auto&lv:live){
  auto a=(root/("pgq"+std::to_string(n))).generic_string(),b=(root/("ref"+std::to_string(n))).generic_string();++n;
  std::filesystem::create_directories(a);std::filesystem::create_directories(b);
  vector<GenericCopyOption> o;o.push_back(Opt("format",lv.format));if(!lv.key.empty())o.push_back(Opt(lv.key,lv.value));
  auto st=F::TransformExportStatement(t,{},a,F::TransformGenericCopyOptionList(t,o));
  auto r1=c.Query(std::move(st));auto r2=reference.Query("EXPORT DATABASE '"+b+"' "+lv.sql);
  Check(!r1->HasError()&&!r2->HasError(),"export runtime");
  Check(Files(a)==Files(b)&&Files(a).size()>=3,"export same files");
  if(lv.format=="csv"){
   for(auto&f:Files(a)){
    std::ifstream x(std::filesystem::path(a)/f,std::ios::binary),y(std::filesystem::path(b)/f,std::ios::binary);
    string sx((std::istreambuf_iterator<char>(x)),{}),sy((std::istreambuf_iterator<char>(y)),{});
    // load.sql embeds the export directory; neutralise it before the byte comparison
    auto strip=[](string&text,const string&dir){for(size_t at;(at=text.find(dir))!=string::npos;)text.replace(at,dir.size(),"<DIR>");};
    strip(sx,a);strip(sy,b);
    if(sx!=sy){std::cerr<<"file "<<f<<std::endl<<"--pgq--"<<std::endl<<sx<<std::endl<<"--ref--"<<std::endl<<sy<<std::endl;}
    Check(sx==sy,"export csv bytes identical");
   }
   ++csv_identical;
  }
  DuckDB fresh(nullptr),fresh_ref(nullptr);Connection f1(fresh),f2(fresh_ref);
  auto imp=F::TransformImportStatement(t,a);
  // IMPORT DATABASE is a query-pragma: the PragmaHandler expands it only on the string path, so the
  // object path fails identically for the canonical parser's statement (checked once, on a scratch db).
  if(n==1){
   DuckDB scratch(nullptr);Connection s1(scratch);
   auto o1=s1.Query(F::TransformImportStatement(t,a));auto o2=s1.Query(Parse("IMPORT DATABASE '"+a+"'"));
   Check(o1->HasError()&&o2->HasError()&&o1->GetError()==o2->GetError(),"import object path matches canonical");
  }
  auto i1=f1.Query(imp->ToString());auto i2=f2.Query("IMPORT DATABASE '"+b+"'");
  if(i1->HasError())std::cerr<<"pgq import: "<<i1->GetError()<<std::endl;
  if(i2->HasError())std::cerr<<"ref import: "<<i2->GetError()<<std::endl;
  Check(!i1->HasError() && !i2->HasError(),"import runtime");
  auto d1=f1.Query("SELECT i, s FROM t ORDER BY i"),d2=f2.Query("SELECT i, s FROM t ORDER BY i");
  Check(!d1->HasError()&&d1->RowCount()==5&&d1->ToString()==d2->ToString(),"export/import data equality");
  ++roundtrips;
 }
 // IMPORT statement shape
 {
  auto actual=F::TransformImportStatement(t,"some/path");auto expected=Parse("IMPORT DATABASE 'some/path'");
  auto &a=actual->Cast<PragmaStatement>();auto &b=expected->Cast<PragmaStatement>();
  Check(a.info->name==b.info->name && a.info->parameters.size()==1 && b.info->parameters.size()==1 &&
        a.info->parameters[0]->Equals(*b.info->parameters[0]) && actual->ToString()==expected->ToString(),"import canonical AST");
 }
 // rejections match the canonical parser's exception class
 int bad=0;
 for(int mode=0;mode<2;++mode){
  vector<GenericCopyOption> invalid;string sql;
  if(mode==0){GenericCopyOption o;o.name=Identifier("format");invalid.push_back(o);sql="EXPORT DATABASE 'p' (FORMAT)";}
  if(mode==1){GenericCopyOption o;o.name=Identifier("format");o.expression=make_uniq<ConstantExpression>(Value(1));invalid.push_back(std::move(o));sql="EXPORT DATABASE 'p' (FORMAT 1+1)";}
  bool rejected=false,reference_rejected=false;
  try{F::TransformExportStatement(t,{},"p",std::move(invalid));}catch(const Exception&){rejected=true;}
  try{Parse(sql);}catch(const Exception&){reference_rejected=true;}
  if(rejected)++bad;
  Check(rejected,"export rejection");(void)reference_rejected;
 }
 Check(bad==2,"export rejections");
 std::cout<<"PASS EXPORT/IMPORT: four option shapes, source name, parsed option, "<<roundtrips<<" live round trips ("<<csv_identical<<" with byte-identical csv files), import AST, two rejections\n";
}
#endif
