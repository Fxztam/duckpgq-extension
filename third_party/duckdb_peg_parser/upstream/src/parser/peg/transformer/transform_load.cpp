#include "duckdb/parser/statement/load_statement.hpp"
#include "duckdb/parser/statement/update_extensions_statement.hpp"
#include "duckdb/parser/parsed_data/update_extensions_info.hpp"
#include "duckpgq/third_party/duckdb_peg_parser/peg/transformer/peg_transformer.hpp"
#include "duckpgq/third_party/duckdb_peg_parser/peg/ast/extension_repository_info.hpp"

namespace duckdb {
namespace duckpgq_peg {

unique_ptr<SQLStatement> PEGTransformerFactory::TransformLoadStatement(PEGTransformer &transformer,
                                                                       const Identifier &col_id_or_string,
                                                                       const optional<Identifier> &extension_alias) {
	auto result = make_uniq<LoadStatement>();
	auto info = make_uniq<LoadInfo>();
	info->repo_is_alias = false;
	info->filename = col_id_or_string.GetIdentifierName();
#if __has_include("duckdb/common/identifier.hpp")
	if (extension_alias) {
		info->alias = *extension_alias;
		info->load_type = LoadType::LOAD_AS;
	} else {
		info->load_type = LoadType::LOAD;
	}
#else
	// Canonical DuckDB 1.5.5 has neither LoadInfo::alias nor LoadType::LOAD_AS: reject instead of dropping the alias.
	if (extension_alias) {
		throw ParserException("LOAD ... AS is not supported by DuckDB 1.5.5");
	}
	info->load_type = LoadType::LOAD;
#endif
	result->info = std::move(info);
	return std::move(result);
}

Identifier PEGTransformerFactory::TransformExtensionAlias(PEGTransformer &transformer, const Identifier &identifier) {
	return identifier;
}

unique_ptr<SQLStatement> PEGTransformerFactory::TransformInstallStatement(
    PEGTransformer &transformer, const bool &has_result, const QualifiedName &identifier_or_string_literal,
    const optional<ExtensionRepositoryInfo> &from_source, const optional<string> &version_number) {
	auto result = make_uniq<LoadStatement>();
	auto info = make_uniq<LoadInfo>();
	info->load_type = has_result ? LoadType::FORCE_INSTALL : LoadType::INSTALL;
#if __has_include("duckdb/common/identifier.hpp")
	info->filename = identifier_or_string_literal.Name().GetIdentifierName();
#else
	info->filename = identifier_or_string_literal.name;
#endif
	info->repo_is_alias = false;
	if (from_source) {
		info->repository = from_source->name.GetIdentifierName();
		info->repo_is_alias = from_source->repository_is_alias;
	}
	if (version_number) {
		info->version = *version_number;
	}
	result->info = std::move(info);
	return std::move(result);
}

ExtensionRepositoryInfo PEGTransformerFactory::TransformFromSourceIdentifier(PEGTransformer &transformer,
                                                                             const Identifier &identifier) {
	ExtensionRepositoryInfo result;
	result.name = identifier;
	result.repository_is_alias = true;
	return result;
}

ExtensionRepositoryInfo PEGTransformerFactory::TransformFromSourceString(PEGTransformer &transformer,
                                                                         const string &string_literal) {
	ExtensionRepositoryInfo result;
	result.name = Identifier(string_literal);
	result.repository_is_alias = false;
	return result;
}

unique_ptr<SQLStatement>
PEGTransformerFactory::TransformUpdateExtensionsStatement(PEGTransformer &transformer,
                                                          const optional<vector<Identifier>> &identifier) {
	auto result = make_uniq<UpdateExtensionsStatement>();
	auto info = make_uniq<UpdateExtensionsInfo>();
	if (identifier) {
#if __has_include("duckdb/common/identifier.hpp")
		info->extensions_to_update = *identifier;
#else
		for (auto &name : *identifier) {
			info->extensions_to_update.push_back(name.GetIdentifierName());
		}
#endif
	}
	result->info = std::move(info);
	return std::move(result);
}

string PEGTransformerFactory::TransformVersionNumber(PEGTransformer &transformer,
                                                     const QualifiedName &identifier_or_string_literal) {
#if __has_include("duckdb/common/identifier.hpp")
	return identifier_or_string_literal.Name().GetIdentifierName();
#else
	return identifier_or_string_literal.name;
#endif
}

} // namespace duckpgq_peg
} // namespace duckdb
