#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// A small JSON reader for the answers 3dsDL gets from web services, such as
// GitHub's release API. It builds a tree of values; nothing is written back.
namespace json {

enum class Type { Null, Bool, Number, String, Array, Object };

struct Value {
    Type type = Type::Null;
    bool boolean = false;
    double number = 0;
    std::string string;
    std::vector<Value> items;                          // arrays
    std::vector<std::pair<std::string, Value>> fields; // objects, in order

    // The field called key, or a shared null value when this is not an
    // object or has no such field.
    const Value &operator[](const std::string &key) const;
    // The text of a string, or fallback for any other type.
    std::string text(const std::string &fallback = "") const;
    // A number as an integer, or fallback for any other type.
    int64_t integer(int64_t fallback = 0) const;
    bool isNull() const { return type == Type::Null; }
};

// Parses text into out. Returns false for anything that is not one complete
// JSON value, or nests deeper than maxDepth, with the reason in error.
bool parse(const std::string &text, Value &out, std::string &error,
           int maxDepth = 64);

} // namespace json
