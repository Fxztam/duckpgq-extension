#pragma once
#include "duckpgq/parser/identifier.hpp"
#include "duckdb/common/serializer/serializer.hpp"
#include "duckdb/common/serializer/deserializer.hpp"

namespace duckdb {
namespace duckpgq_compat {
// Match the pinned fork's plain-string wire format, including exact spelling.
// Do not add methods to, specialize, or shadow host Serializer/Deserializer.
inline void WriteIdentifierProperty(Serializer &out, field_id_t id, const char *tag, const Identifier &value) {
    out.WriteProperty(id, tag, value.GetIdentifierName());
}
inline void WriteIdentifierProperty(Serializer &out, field_id_t id, const char *tag,
                                    const vector<Identifier> &values) {
    out.WriteProperty(id, tag, IdentifiersToStrings(values));
}
inline void ReadIdentifierProperty(Deserializer &in, field_id_t id, const char *tag, Identifier &value) {
    string decoded;
    in.ReadProperty(id, tag, decoded);
    value = Identifier(std::move(decoded));
}
inline void ReadIdentifierProperty(Deserializer &in, field_id_t id, const char *tag, vector<Identifier> &values) {
    vector<string> decoded;
    in.ReadProperty(id, tag, decoded);
    values = StringsToIdentifiers(decoded);
}
} // namespace duckpgq_compat
} // namespace duckdb
