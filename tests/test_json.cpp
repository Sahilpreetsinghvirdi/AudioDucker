#include "TestHarness.h"

#include "common/JsonMini.h"

#include <string>

using ducker::JsonValue;
using ducker::ParseJson;
using ducker::WriteJson;
using ducker::EscapeJsonString;

TEST(JsonParseNativeMessagePayload) {
    JsonValue v;
    std::string err;
    EXPECT_TRUE(ParseJson(R"({"type":"youtube-count","count":3})", v, &err));
    EXPECT_EQ(v.AsString("type"), "youtube-count");
    EXPECT_EQ(v.AsInt("count"), 3);
}

TEST(JsonParseTypes) {
    JsonValue v;
    EXPECT_TRUE(ParseJson(R"({"s":"x","i":42,"f":-1.5,"b":true,"n":null})", v));
    EXPECT_EQ(v.AsString("s"), "x");
    EXPECT_EQ(v.AsInt("i"), 42);
    EXPECT_EQ(v.AsInt("f"), -1); // AsInt truncates numbers
    EXPECT_TRUE(v.AsBool("b"));
    EXPECT_FALSE(v.AsBool("n")); // null is not a bool
}

TEST(JsonParseNestedAndArrays) {
    JsonValue v;
    EXPECT_TRUE(ParseJson(R"({"a":[1,2,3],"o":{"k":"v"}})", v));
    const JsonValue* a = v.Find("a");
    EXPECT_TRUE(a != nullptr);
    EXPECT_EQ(a->type, JsonValue::Type::Array);
    EXPECT_EQ(a->arr.size(), static_cast<size_t>(3));
    EXPECT_EQ(a->arr[2].num, 3.0);
    const JsonValue* o = v.Find("o");
    EXPECT_TRUE(o != nullptr);
    EXPECT_EQ(o->AsString("k"), "v");
}

TEST(JsonParseEscapes) {
    JsonValue v;
    EXPECT_TRUE(ParseJson(R"({"q":"\"","bs":"\\","nl":"a\nb","tab":"\t"})", v));
    EXPECT_EQ(v.AsString("q"), "\"");
    EXPECT_EQ(v.AsString("bs"), "\\");
    EXPECT_EQ(v.AsString("nl"), "a\nb");
    EXPECT_EQ(v.AsString("tab"), "\t");
}

TEST(JsonParseUnicodeEscape) {
    JsonValue v;
    EXPECT_TRUE(ParseJson(R"({"s":"\u00e9"})", v));
    std::string s = v.AsString("s");
    EXPECT_EQ(s.size(), static_cast<size_t>(2));
    EXPECT_EQ(static_cast<unsigned char>(s[0]), 0xC3);
    EXPECT_EQ(static_cast<unsigned char>(s[1]), 0xA9);
    // ASCII escapes stay single-byte.
    JsonValue a;
    EXPECT_TRUE(ParseJson(R"({"s":"\u0041"})", a));
    EXPECT_EQ(a.AsString("s"), "A");
}

TEST(JsonParseNumbers) {
    JsonValue v;
    EXPECT_TRUE(ParseJson(R"([0, 42, -7, 1.5, 1e3, 2.5e-2])", v));
    EXPECT_EQ(v.type, JsonValue::Type::Array);
    EXPECT_NEAR(v.arr[0].num, 0.0, 1e-9);
    EXPECT_NEAR(v.arr[1].num, 42.0, 1e-9);
    EXPECT_NEAR(v.arr[2].num, -7.0, 1e-9);
    EXPECT_NEAR(v.arr[3].num, 1.5, 1e-9);
    EXPECT_NEAR(v.arr[4].num, 1000.0, 1e-9);
    EXPECT_NEAR(v.arr[5].num, 0.025, 1e-9);
}

TEST(JsonParseRejectsInvalid) {
    JsonValue v;
    std::string err;
    EXPECT_FALSE(ParseJson("", v, &err));
    EXPECT_FALSE(ParseJson("tru", v));
    EXPECT_FALSE(ParseJson("{}x", v, &err));
    EXPECT_FALSE(ParseJson("{]", v));
    EXPECT_FALSE(ParseJson("[1,]", v));
    EXPECT_FALSE(ParseJson("{\"a\"}", v));
    EXPECT_FALSE(ParseJson("{\"a\":}", v));
    EXPECT_FALSE(ParseJson("\"unterminated", v));
}

TEST(JsonParseWhitespaceTolerant) {
    JsonValue v;
    EXPECT_TRUE(ParseJson("  { \"type\" : \"x\" , \"count\" : 5 }  ", v));
    EXPECT_EQ(v.AsString("type"), "x");
    EXPECT_EQ(v.AsInt("count"), 5);
}

TEST(JsonWriteNativeMessagePayload) {
    JsonValue v;
    v.type = JsonValue::Type::Object;
    v.obj.emplace_back("type", JsonValue{});
    v.obj.back().second.type = JsonValue::Type::String;
    v.obj.back().second.str = "youtube-count";
    v.obj.emplace_back("count", JsonValue{});
    v.obj.back().second.type = JsonValue::Type::Number;
    v.obj.back().second.num = 3;

    std::string out;
    EXPECT_TRUE(WriteJson(v, out));
    EXPECT_EQ(out, R"({"type":"youtube-count","count":3})");
}

TEST(JsonRoundTrip) {
    JsonValue v;
    EXPECT_TRUE(ParseJson(R"({ "a" : [1, 2.5, true], "b" : "x\ny", "c" : null })", v));
    std::string out;
    EXPECT_TRUE(WriteJson(v, out));
    JsonValue back;
    EXPECT_TRUE(ParseJson(out, back));
    EXPECT_EQ(back.AsString("b"), "x\ny");
    EXPECT_EQ(back.AsInt("a"), 0); // arrays have no scalar value
    EXPECT_EQ(back.Find("a")->arr.size(), static_cast<size_t>(3));
    EXPECT_TRUE(back.Find("a")->arr[2].b);
}

TEST(JsonEscapeString) {
    EXPECT_EQ(EscapeJsonString("plain"), "\"plain\"");
    EXPECT_EQ(EscapeJsonString("a\"b"), "\"a\\\"b\"");
    EXPECT_EQ(EscapeJsonString("a\\b"), "\"a\\\\b\"");
    EXPECT_EQ(EscapeJsonString("a\nb"), "\"a\\nb\"");
}

TEST(JsonWriteNumberFormats) {
    JsonValue v;
    v.type = JsonValue::Type::Number;
    v.num = 1000;
    std::string out;
    EXPECT_TRUE(WriteJson(v, out));
    EXPECT_EQ(out, "1000");

    JsonValue f;
    f.type = JsonValue::Type::Number;
    f.num = 0.5;
    out.clear();
    EXPECT_TRUE(WriteJson(f, out));
    EXPECT_EQ(out, "0.5");
}
