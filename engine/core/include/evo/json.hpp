#pragma once

// Minimal JSON support for the engine core. Milestone 05 scope: parse and
// serialize the canonical DAG shape only. No external JSON dependency — the
// core engine (M04–M15) builds against the C++ standard library alone.

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace evo::json {

class Value;
using Array = std::vector<Value>;
using Object = std::map<std::string, Value>;  // ordered => deterministic output

class Value {
 public:
  enum class Kind { Null, Bool, Number, String, Array, Object };

  Value() : kind_(Kind::Null) {}
  explicit Value(bool b) : kind_(Kind::Bool), bool_(b) {}
  explicit Value(double n) : kind_(Kind::Number), number_(n) {}
  explicit Value(std::string s) : kind_(Kind::String), string_(std::move(s)) {}
  explicit Value(Array a) : kind_(Kind::Array), array_(std::move(a)) {}
  explicit Value(Object o) : kind_(Kind::Object), object_(std::move(o)) {}

  Kind kind() const { return kind_; }
  bool is_null() const { return kind_ == Kind::Null; }

  bool as_bool() const { return bool_; }
  double as_number() const { return number_; }
  const std::string& as_string() const { return string_; }
  const Array& as_array() const { return array_; }
  const Object& as_object() const { return object_; }

  // Object member lookup; nullptr when absent or not an object.
  const Value* find(const std::string& key) const;

 private:
  Kind kind_;
  bool bool_ = false;
  double number_ = 0.0;
  std::string string_;
  Array array_;
  Object object_;
};

// Parses a JSON document. Returns std::nullopt on any syntax error; the
// engine treats malformed payloads as a hard input error, never a crash.
std::optional<Value> parse(const std::string& text);

// Serializes a value deterministically (object keys in map order, strings
// escaped). Suitable for canonical round-trip comparisons.
std::string serialize(const Value& value);

}  // namespace evo::json
