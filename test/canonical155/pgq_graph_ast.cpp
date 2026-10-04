#ifdef PGQ_TRANSFORMER_TEST
#include "duckpgq/third_party/duckdb_peg_parser/peg/transformer/peg_transformer.hpp"
#include "duckpgq/compat/expression_access.hpp"
#include "duckpgq/compat/function_access.hpp"
#include "duckpgq/compat/name_metadata.hpp"
#include "duckpgq/parser/path_element.hpp"
#include "duckpgq/parser/path_pattern.hpp"
#include "duckpgq/parser/subpath_element.hpp"
#include "duckpgq/parser/tableref/matchref.hpp"
#include "duckpgq/parser/parsed_data/create_property_graph_info.hpp"
#include "duckpgq/parser/parsed_data/drop_property_graph_info.hpp"
#include "duckdb/parser/expression/constant_expression.hpp"
#include "duckdb/parser/expression/function_expression.hpp"
#include "duckdb/parser/expression/star_expression.hpp"
#include "duckdb/parser/statement/create_statement.hpp"
#include "duckdb/parser/statement/drop_statement.hpp"
#include "duckdb/parser/tableref/table_function_ref.hpp"
#include <iostream>
#include <stdexcept>
using namespace duckdb; using namespace duckdb::duckpgq_peg; using F=PEGTransformerFactory;
static void Check(bool b,const char*m){if(!b)throw std::runtime_error(m);}
static unique_ptr<BaseTableRef> Table(vector<string> names){
 vector<Identifier> ids;for(auto&n:names)ids.push_back(Identifier(n));
 return duckpgq_compat::TableFromNames(std::move(ids));
}
static string S(const Identifier&i){return i.GetIdentifierName();}
static vector<string> Strs(const vector<Identifier>&v){vector<string> r;for(auto&i:v)r.push_back(S(i));return r;}
static unique_ptr<ParsedExpression> Num(int64_t v){return make_uniq<ConstantExpression>(Value::BIGINT(v));}
static QualifiedName QN(const string&n){return duckpgq_compat::MakeQualifiedName(Identifier(n));}
static unique_ptr<PathReference> Vertex(PEGTransformer&t,const string&var,optional<string> label={}){
 optional<Identifier> l;if(label)l=Identifier(*label);
 return F::TransformGraphVertexReference(t,Identifier(var),l,{});
}
static unique_ptr<PathReference> Edge(PEGTransformer&t,const string&l,const string&var,optional<string> label,const string&r){
 optional<Identifier> lab;if(label)lab=Identifier(*label);
 return F::TransformGraphEdgePattern(t,l,F::TransformGraphEdgeBody(t,Identifier(var),lab,{}),r);
}
static PathElement &Elem(PathReference&r){return dynamic_cast<PathElement&>(r);}
static unique_ptr<PathPattern> Pattern(PEGTransformer&t,optional<Identifier> var,optional<string> search,optional<string> mode){
 vector<vector<unique_ptr<PathReference>>> tail;
 return F::TransformGraphPathPattern(t,var,search,mode,F::TransformGraphPathSequence(t,Vertex(t,"a"),std::move(tail)));
}
void TestPgqGraph(PEGTransformer&t){
 // ---- property keyword/flag helpers
 {
  auto all=F::TransformPropertyGraphAllProperties(t);Check(all.all_columns&&!all.no_columns&&all.columns.empty(),"all properties");
  auto cols=F::TransformPropertyGraphAllColumns(t,true);Check(cols.all_columns&&cols.except_columns.empty(),"all columns");
  auto exc=F::TransformPropertyGraphAllColumnsExcept(t,true,vector<Identifier>{Identifier("secret")});Check(exc.all_columns&&Strs(exc.except_columns)==vector<string>{"secret"},"all except");
  auto none=F::TransformPropertyGraphNoProperties(t);Check(none.no_columns&&!none.all_columns,"no properties");
  auto list=F::TransformPropertyGraphPropertyList(t,vector<Identifier>{Identifier("id"),Identifier("name")});
  Check(Strs(list.columns)==vector<string>({"id","name"})&&!list.all_columns,"property list");
  Check(S(F::TransformPropertyGraphProperty(t,Identifier("c"),Identifier("alias")))=="c","property keeps column name");
 }
 // ---- vertex tables: explicit label, 3-part name with alias, default label, implicit sub-labels
 auto person=F::TransformPropertyGraphVertexTable(t,Table({"Person"}),{},
   F::TransformPropertyGraphPropertyList(t,vector<Identifier>{Identifier("id"),Identifier("name")}),
   F::TransformPropertyGraphExplicitLabel(t,Identifier("Human"),{}));
 Check(person->is_vertex_table&&S(person->table_name)=="Person"&&S(person->main_label)=="Human"&&
   Strs(person->column_names)==vector<string>({"id","name"})&&!person->all_columns,"vertex explicit label");
 auto city=F::TransformPropertyGraphVertexTable(t,Table({"db","s","City"}),TableAlias{Identifier("c"),{}},{},{});
 Check(S(city->catalog_name)=="db"&&S(city->schema_name)=="s"&&S(city->table_name)=="City"&&S(city->table_name_alias)=="c"&&
   city->all_columns&&S(city->main_label)=="c","vertex 3-part name, alias becomes default label, no property clause means all columns");
 auto plain=F::TransformPropertyGraphVertexTable(t,Table({"Org"}),{},{},{});
 Check(S(plain->main_label)=="Org"&&S(plain->catalog_name).empty(),"vertex default label is the table name");
 auto sub=F::TransformPropertyGraphVertexTable(t,Table({"Thing"}),{},{},
   F::TransformPropertyGraphImplicitLabel(t,F::TransformPropertyGraphSubLabels(t,Identifier("kind"),vector<Identifier>{Identifier("A"),Identifier("B")})));
 Check(S(sub->discriminator)=="kind"&&Strs(sub->sub_labels)==vector<string>({"A","B"})&&S(sub->main_label)=="Thing","implicit sub labels");
 // ---- edge table with source/destination keys
 auto knows=F::TransformPropertyGraphEdgeTable(t,Table({"Knows"}),{},
   F::TransformPropertyGraphKeyReference(t,vector<Identifier>{Identifier("a")},Table({"Person"}),vector<Identifier>{Identifier("id")}),
   F::TransformPropertyGraphKeyReference(t,vector<Identifier>{Identifier("b")},Table({"Person"}),vector<Identifier>{Identifier("id")}),
   F::TransformPropertyGraphNoProperties(t),F::TransformPropertyGraphExplicitLabel(t,Identifier("KNOWS"),{}));
 Check(!knows->is_vertex_table&&S(knows->table_name)=="Knows"&&S(knows->main_label)=="KNOWS"&&knows->no_columns&&
   Strs(knows->source_fk)==vector<string>{"a"}&&Strs(knows->source_pk)==vector<string>{"id"}&&S(knows->source_reference)=="Person"&&
   Strs(knows->destination_fk)==vector<string>{"b"}&&S(knows->destination_reference)=="Person","edge table keys and label");
 Check(F::TransformSourceTableReference(t,Table({"Person"})).table&&F::TransformDestinationTableReference(t,Table({"Person"})).table,"table reference markers");
 // ---- CREATE PROPERTY GRAPH: linking, label map, conflict mode, errors
 {
  vector<shared_ptr<PropertyGraphTable>> vertices={person,city};
  vector<shared_ptr<PropertyGraphTable>> edges={knows};
  auto stmt=F::TransformCreatePropertyGraphStmt(t,true,QN("g"),std::move(vertices),std::move(edges));
  auto &info=dynamic_cast<CreatePropertyGraphInfo&>(*stmt->info);
  Check(info.property_graph_name=="g"&&info.on_conflict==OnCreateConflict::IGNORE_ON_CONFLICT&&info.vertex_tables.size()==2&&info.edge_tables.size()==1,"create graph shape");
  Check(info.label_map.size()==3&&info.label_map.count("Human")==1&&info.label_map.count("c")==1&&info.label_map.count("KNOWS")==1,"label map");
  Check(info.edge_tables[0]->source_pg_table==person&&info.edge_tables[0]->destination_pg_table==person,"edge endpoints linked to the matching vertex table");
  auto without=F::TransformCreatePropertyGraphStmt(t,{},QN("g2"),vector<shared_ptr<PropertyGraphTable>>{person},{});
  auto &winfo=dynamic_cast<CreatePropertyGraphInfo&>(*without->info);
  Check(winfo.on_conflict==OnCreateConflict::ERROR_ON_CONFLICT&&winfo.edge_tables.empty(),"create graph defaults");
  int bad=0;
  try{F::TransformCreatePropertyGraphStmt(t,{},QN(""),vector<shared_ptr<PropertyGraphTable>>{person},{});}catch(const ParserException&){bad++;}
  try{F::TransformCreatePropertyGraphStmt(t,{},QN("dup"),vector<shared_ptr<PropertyGraphTable>>{person,person},{});}catch(const ConstraintException&){bad++;}
  Check(bad==2,"create graph rejections (empty name, duplicate label)");
 }
 // ---- DROP PROPERTY GRAPH
 {
  auto d=F::TransformDropPropertyGraph(t,true,QN("g"));
  auto &info=dynamic_cast<DropPropertyGraphInfo&>(*d->info);
  Check(info.property_graph_name=="g"&&info.missing_ok,"drop if exists");
  auto d2=F::TransformDropPropertyGraph(t,{},QN("g"));
  Check(!dynamic_cast<DropPropertyGraphInfo&>(*d2->info).missing_ok,"drop without if exists");
 }
 // ---- path elements: directions, labels, quantifiers
 {
  auto left=Edge(t,"<-","e",string("E"),"-");Check(Elem(*left).match_type==PGQMatchType::MATCH_EDGE_LEFT&&Elem(*left).label=="E"&&Elem(*left).variable_binding=="e","edge left");
  auto right=Edge(t,"-","e",{},"->");Check(Elem(*right).match_type==PGQMatchType::MATCH_EDGE_RIGHT&&Elem(*right).label=="e","edge right, label defaults to the variable");
  auto any=Edge(t,"-","e",{},"-");Check(Elem(*any).match_type==PGQMatchType::MATCH_EDGE_ANY,"edge any");
  auto both=Edge(t,"<-","e",{},"->");Check(Elem(*both).match_type==PGQMatchType::MATCH_EDGE_LEFT_RIGHT,"edge left-right");
  int bad=0;try{Edge(t,"->","e",{},"-");}catch(const ParserException&){bad++;}
  Check(bad==1,"unsupported edge direction");
  Check(F::TransformGraphEdgeLeftArrow(t)=="<-"&&F::TransformGraphEdgeRightArrow(t)=="->"&&F::TransformGraphEdgeSpacedRightArrow(t)=="->"&&F::TransformGraphEdgeDash(t)=="-","arrow markers");
  auto v=Vertex(t,"a",string("Person"));
  Check(Elem(*v).match_type==PGQMatchType::MATCH_VERTEX&&Elem(*v).label=="Person"&&Elem(*v).variable_binding=="a","vertex reference");
  struct Q{string quantifier;int64_t lower,upper;};
  const int64_t max=NumericLimits<int64_t>::Maximum();
  vector<Q> quantifiers;
  quantifiers.push_back({F::TransformGraphStarQuantifier(t),0,max});
  quantifiers.push_back({F::TransformGraphPlusQuantifier(t),1,max});
  quantifiers.push_back({F::TransformGraphQuestionQuantifier(t),0,1});
  quantifiers.push_back({F::TransformGraphFixedQuantifier(t,Num(3)),3,3});
  quantifiers.push_back({F::TransformGraphRangeQuantifier(t,Num(2),Num(5)),2,5});
  quantifiers.push_back({F::TransformGraphRangeQuantifier(t,{},Num(4)),0,4});
  quantifiers.push_back({F::TransformGraphRangeQuantifier(t,Num(2),{}),2,max});
  for(auto&q:quantifiers){
   auto quantified=F::TransformGraphQuantifiedEdgePattern(t,Edge(t,"-","e",{},"->"),q.quantifier);
   auto &sp=dynamic_cast<SubPath&>(*quantified);
   Check(sp.lower==q.lower&&sp.upper==q.upper&&sp.path_list.size()==1&&Elem(*sp.path_list[0]).match_type==PGQMatchType::MATCH_EDGE_RIGHT,"quantifier bounds");
  }
  bad=0;
  try{F::TransformGraphQuantifiedEdgePattern(t,Edge(t,"-","e",{},"->"),F::TransformGraphRangeQuantifier(t,Num(5),Num(2)));}catch(const ConstraintException&){bad++;}
  Check(bad==1,"quantifier lower>upper");
  Check(F::TransformGraphQuantifiedEdgePattern(t,Edge(t,"-","e",{},"->"),{})->path_reference_type==PGQPathReferenceType::PATH_ELEMENT,"no quantifier keeps the element");
 }
 // ---- full pattern -> duckpgq_match table function
 {
  vector<vector<unique_ptr<PathReference>>> tail;
  tail.push_back(F::TransformGraphEdgeVertexPattern(t,Edge(t,"-","k",string("Knows"),"->"),Vertex(t,"b",string("Person"))));
  auto seq=F::TransformGraphPathSequence(t,Vertex(t,"a",string("Person")),std::move(tail));
  Check(seq->path_elements.size()==3,"path sequence length");
  auto pattern=F::TransformGraphPathPattern(t,{},F::TransformGraphAllShortestPrefix(t),{},std::move(seq));
  Check(pattern->shortest&&pattern->all&&pattern->topk==0,"all shortest prefix");
  vector<unique_ptr<PathPattern>> patterns;patterns.push_back(std::move(pattern));
  auto ref=F::TransformGraphTableRef(t,F::TransformGraphTableUnderscoreKeyword(t,Identifier("GRAPH_TABLE")),QN("g"),
     std::move(patterns),{},{},TableAlias{Identifier("gt"),{}});
  auto &fn=dynamic_cast<TableFunctionRef&>(*ref);
  Check(fn.alias=="gt","graph table alias");
  auto &call=fn.function->Cast<FunctionExpression>();
  Check(duckpgq_compat::FunctionName(call)=="duckpgq_match"&&call.children.size()==1,"duckpgq_match call");
  auto &match=call.children[0]->Cast<MatchExpression>();
  Check(match.pg_name=="g"&&match.alias=="gt"&&match.path_patterns.size()==1,"match expression");
  Check(match.ToString().rfind("GRAPH_TABLE (g MATCH",0)==0,"match expression renders GRAPH_TABLE");
  // no COLUMNS clause: one star per vertex variable (a and b), none for the edge
  Check(match.column_list.size()==2&&match.column_list[0]->Cast<StarExpression>().relation_name=="a"&&
        match.column_list[1]->Cast<StarExpression>().relation_name=="b","default star columns per vertex");
 }
 // ---- search prefixes, path modes, GRAPH_TABLE keywords
 {
  auto any=Pattern(t,{},F::TransformGraphAnyShortestPrefix(t),{});Check(any->shortest&&!any->all,"any shortest");
  auto top=Pattern(t,{},F::TransformGraphTopKShortestPrefix(t,Num(3)),{});Check(top->shortest&&top->topk==1,"top-k shortest prefix");
  vector<pair<string,PGQPathMode>> modes;
  modes.push_back({F::TransformGraphWalkPathMode(t),PGQPathMode::WALK});
  modes.push_back({F::TransformGraphTrailPathMode(t),PGQPathMode::TRAIL});
  modes.push_back({F::TransformGraphSimplePathMode(t),PGQPathMode::SIMPLE});
  modes.push_back({F::TransformGraphAcyclicPathMode(t),PGQPathMode::ACYCLIC});
  for(auto&m:modes){
   auto p=Pattern(t,Identifier("p"),{},m.first);
   auto &sp=dynamic_cast<SubPath&>(*p->path_elements[0]);
   Check(sp.path_mode==m.second&&sp.path_variable=="p"&&sp.path_list.size()==1,"path mode and variable");
  }
  int bad=0;
  try{Pattern(t,{},{},string("bogus"));}catch(const ParserException&){bad++;}
  try{F::TransformGraphTableUnderscoreKeyword(t,Identifier("graph_tablex"));}catch(const ParserException&){bad++;}
  vector<unique_ptr<PathPattern>> none;
  try{F::TransformGraphTableRef(t,"match",QN("g"),std::move(none),{},{},{});}catch(const ParserException&){bad++;}
  Check(bad==3,"path mode, keyword and GRAPH_TABLE rejections");
  Check(F::TransformGraphTableSpacedKeyword(t)=="graph table"&&F::TransformGraphTableUnderscoreKeyword(t,Identifier("Graph_Table"))=="Graph_Table","graph table keywords");
 }
 std::cout<<"PASS PGQ graph syntax: vertex/edge tables, labels and keys, CREATE/DROP PROPERTY GRAPH, label map, edge linking, four directions, seven quantifiers, search prefixes, path modes, duckpgq_match construction, MATCH rendering, seven rejections"<<std::endl;
}
#endif
