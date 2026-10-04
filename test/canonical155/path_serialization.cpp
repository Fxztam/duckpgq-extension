#include "duckpgq/parser/path_element.hpp"
#include "duckpgq/parser/subpath_element.hpp"
#include "duckdb/common/serializer/binary_serializer.hpp"
#include "duckdb/common/serializer/binary_deserializer.hpp"
#include "duckdb/common/serializer/memory_stream.hpp"
#include "duckdb/parser/expression/constant_expression.hpp"
#include <iostream>
#include <stdexcept>
#include <cstring>
using namespace duckdb;
static void RequirePath(bool ok, const char *why) {
    if (!ok) throw std::runtime_error(why);
}
static unique_ptr<PathReference> Restore(MemoryStream &bytes, idx_t length) {
    MemoryStream input(bytes.GetData(), length);
    return BinaryDeserializer::Deserialize<PathReference>(input);
}
static unique_ptr<PathElement> Element(PGQMatchType type) {
    auto element = make_uniq<PathElement>(PGQPathReferenceType::PATH_ELEMENT);
    element->match_type = type;
    element->label = "Label.WithSpace";
    element->variable_binding = "MixedCase";
    return element;
}
static void Roundtrip(PathReference &path) {
    MemoryStream bytes;
    BinarySerializer::Serialize(path, bytes);
    auto restored = Restore(bytes, bytes.GetPosition());
    RequirePath(path.Equals(restored.get()), "polymorphic path roundtrip changed fields");
    for (idx_t i = 0; i < bytes.GetPosition(); i++) {
        bool rejected = false;
        try { auto ignored = Restore(bytes, i); }
        catch (const Exception &) { rejected = true; }
        RequirePath(rejected, "truncated path accepted");
    }
}
void TestPathSerialization() {
    // Independent wire oracle, not built with production Serialize methods.
    auto fixture = Element(PGQMatchType::MATCH_EDGE_RIGHT);
    MemoryStream actual, expected;
    BinarySerializer::Serialize(*fixture, actual);
    BinarySerializer oracle(expected);
    oracle.Begin();
    oracle.WriteProperty(100, "path_reference_type", uint8_t(0));
    oracle.WriteProperty(200, "match_type", uint8_t(3));
    oracle.WriteProperty(201, "label", string("Label.WithSpace"));
    oracle.WriteProperty(202, "variable_binding", string("MixedCase"));
    oracle.End();
    RequirePath(actual.GetPosition() == expected.GetPosition() &&
        std::memcmp(actual.GetData(), expected.GetData(), actual.GetPosition()) == 0, "path wire oracle mismatch");
    auto oracle_read = Restore(expected, expected.GetPosition());
    RequirePath(fixture->Equals(oracle_read.get()), "path oracle cannot be read");
    for (unsigned i = 0; i <= 4; i++) {
        auto element = Element(static_cast<PGQMatchType>(i));
        Roundtrip(*element);
    }
    for (unsigned mode = 0; mode <= 4; mode++) {
        SubPath outer(PGQPathReferenceType::SUBPATH);
        outer.path_mode = static_cast<PGQPathMode>(mode);
        outer.lower = 2; outer.upper = 7; outer.single_bind = false;
        outer.path_variable = "Outer";
        outer.path_list.push_back(Element(PGQMatchType::MATCH_VERTEX));
        auto inner = make_uniq<SubPath>(PGQPathReferenceType::SUBPATH);
        inner->path_variable = "Inner";
        inner->path_list.push_back(Element(PGQMatchType::MATCH_EDGE_RIGHT));
        outer.path_list.push_back(std::move(inner));
        Roundtrip(outer);
    }
    SubPath empty(PGQPathReferenceType::SUBPATH);
    Roundtrip(empty);
    empty.where_clause = make_uniq<ConstantExpression>(Value::BOOLEAN(true));
    Roundtrip(empty);
    for (unsigned i = 5; i < 256; i++) {
        MemoryStream invalid;
        BinarySerializer writer(invalid);
        writer.Begin();
        writer.WriteProperty(100, "path_reference_type", uint8_t(0));
        writer.WriteProperty(200, "match_type", uint8_t(i));
        writer.WriteProperty(201, "label", string("L"));
        writer.WriteProperty(202, "variable_binding", string("v"));
        writer.End();
        bool rejected = false;
        try { auto ignored = Restore(invalid, invalid.GetPosition()); }
        catch (const SerializationException &) { rejected = true; }
        RequirePath(rejected, "invalid match enum accepted");
    }
    for (unsigned i = 5; i < 256; i++) {
        MemoryStream invalid;
        BinarySerializer writer(invalid);
        writer.Begin();
        writer.WriteProperty(100, "path_reference_type", uint8_t(1));
        writer.WriteProperty(200, "path_mode", uint8_t(i));
        writer.WriteProperty(201, "path_list", vector<unique_ptr<PathReference>>{});
        writer.WriteProperty(202, "single_bind", true);
        writer.WriteProperty(203, "lower", int64_t(1));
        writer.WriteProperty(204, "upper", int64_t(1));
        writer.WriteProperty(205, "where_clause", unique_ptr<ParsedExpression>{});
        writer.WriteProperty(206, "path_variable", string(""));
        writer.End();
        bool rejected = false;
        try { auto ignored = Restore(invalid, invalid.GetPosition()); }
        catch (const SerializationException &) { rejected = true; }
        RequirePath(rejected, "invalid path mode accepted");
    }
    for (unsigned i = 2; i < 256; i++) {
        MemoryStream invalid;
        BinarySerializer writer(invalid);
        writer.Begin();
        writer.WriteProperty(100, "path_reference_type", uint8_t(i));
        writer.End();
        bool rejected = false;
        try { auto ignored = Restore(invalid, invalid.GetPosition()); }
        catch (const SerializationException &) { rejected = true; }
        RequirePath(rejected, "invalid discriminator must give SerializationException");
    }
    // Old writer omitted the base tag and reused 101. Reject, do not reinterpret.
    MemoryStream legacy;
    BinarySerializer old_writer(legacy);
    old_writer.Begin();
    old_writer.WriteProperty(100, "match_type", uint8_t(0));
    old_writer.WriteProperty(101, "label", string("L"));
    old_writer.WriteProperty(101, "variable_binding", string("v"));
    old_writer.End();
    bool legacy_rejected = false;
    try { auto ignored = Restore(legacy, legacy.GetPosition()); }
    catch (const SerializationException &) { legacy_rejected = true; }
    RequirePath(legacy_rejected, "broken legacy path format accepted");
    std::cout << "PASS PGQ paths: wire oracle, five match types, five modes, nested/empty/WHERE, "
                 "truncation, 756 invalid enum bytes, legacy rejection\n";
}
