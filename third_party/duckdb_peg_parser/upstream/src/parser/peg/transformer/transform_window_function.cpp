#include "duckpgq/compat/window_function.hpp"
#include "duckdb/common/string_util.hpp"
#if !__has_include("duckdb/common/identifier.hpp")
namespace duckdb {
namespace duckpgq_peg {
unique_ptr<WindowExpression> BuildWindowFunction(PEGTransformer &t, const QualifiedName &name,
    MethodArguments args, optional<vector<OrderByNode>> within_group,
    unique_ptr<ParsedExpression> filter, bool export_state, unique_ptr<WindowExpression> frame) {
    if(t.in_window_definition) throw ParserException("window functions are not allowed in window definitions");
    if(!frame) throw ParserException("Window frame is missing");
    if(export_state) throw ParserException("EXPORT_STATE is not supported for window functions!");
    if(within_group) throw ParserException("WITHIN GROUP is not supported for canonical DuckDB 1.5.5 window functions");
    auto function=StringUtil::Lower(name.name);
    if(function=="first" || function=="last") function+="_value";
    const auto type=WindowExpression::WindowToExpressionType(function);
    if(type==ExpressionType::INVALID) throw ParserException("Unsupported window function");
    const bool aggregate=type==ExpressionType::WINDOW_AGGREGATE;
    if(!aggregate && args.distinct) throw ParserException("DISTINCT is not implemented for non-aggregate window functions!");
    if(!aggregate && filter) throw ParserException("FILTER is not implemented for non-aggregate window functions!");
    if(aggregate && args.has_ignore_nulls) throw ParserException("RESPECT/IGNORE NULLS is not supported for windowed aggregates");
    if(type==ExpressionType::WINDOW_RANK_DENSE && !args.order_bys.empty()) throw ParserException("ORDER BY is not supported for dense_rank");
    const bool excludable=aggregate || type==ExpressionType::WINDOW_FIRST_VALUE || type==ExpressionType::WINDOW_LAST_VALUE || type==ExpressionType::WINDOW_NTH_VALUE;
    if(!excludable && frame->exclude_clause!=WindowExcludeMode::NO_OTHER && !args.order_bys.empty()) throw ParserException("EXCLUDE is not supported for this window function with argument ORDER BY");
    auto children=LowerFunctionArguments(std::move(args.arguments));
    if(children.size()==1 && children[0]->GetExpressionClass()==ExpressionClass::STAR && !args.distinct && args.order_bys.empty()) {
        auto &star=children[0]->Cast<StarExpression>();
        if(!star.columns && star.exclude_list.empty() && star.replace_list.empty()) children.clear();
    }
    const bool offset=type==ExpressionType::WINDOW_LEAD || type==ExpressionType::WINDOW_LAG;
    const idx_t max_args=offset?3:(type==ExpressionType::WINDOW_NTH_VALUE?2:1);
    if(!aggregate && children.size()>max_args) throw ParserException("Incorrect number of parameters for function %s",function);
    auto result=make_uniq<WindowExpression>(type,name.catalog,name.schema,function);
    result->ignore_nulls=args.ignore_nulls; result->distinct=args.distinct;
    result->filter_expr=std::move(filter); result->arg_orders=std::move(args.order_bys);
    result->partitions=std::move(frame->partitions); result->orders=std::move(frame->orders);
    result->start=frame->start; result->end=frame->end; result->exclude_clause=frame->exclude_clause;
    result->start_expr=std::move(frame->start_expr); result->end_expr=std::move(frame->end_expr);
    if(aggregate) result->children=std::move(children);
    else {
        if(!children.empty()) result->children.push_back(std::move(children[0]));
        if(offset) {
            if(children.size()>1) result->offset_expr=std::move(children[1]);
            if(children.size()>2) result->default_expr=std::move(children[2]);
        } else if(type==ExpressionType::WINDOW_NTH_VALUE && children.size()>1) result->children.push_back(std::move(children[1]));
    }
    return result;
}
}
}
#endif
