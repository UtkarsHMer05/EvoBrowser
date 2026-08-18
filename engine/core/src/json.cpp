#include "evo/json.hpp"

#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace evo::json {

const Value* Value::find(const std::string& key) const {
  if (kind_ != Kind::Object) return nullptr;
  auto it = object_.find(key);
  return it == object_.end() ? nullptr : &it->second;
}

namespace {

class Parser {
 public:
  explicit Parser(const std::string& text) : text_(text) {}

  std::optional<Value> run() {
    skip_ws();
    auto value = parse_value();
    if (!value) return std::nullopt;
    skip_ws();
    if (pos_ != text_.size()) return std::nullopt;  // trailing garbage
    return value;
  }

 private:
  const std::string& text_;
  std::size_t pos_ = 0;

  bool eof() const { return pos_ >= text_.size(); }
  char peek() const { return eof() ? '\0' : text_[pos_]; }
  char take() { return eof() ? '\0' : text_[pos_++]; }

  void skip_ws() {
    while (!eof() &&
           (peek() == ' ' || peek() == '\t' || peek() == '\n' ||
            peek() == '\r')) {
      ++pos_;
    }
  }

  bool consume(char c) {
    if (peek() == c) {
      ++pos_;
      return true;
    }
    return false;
  }

  bool consume_lit(const char* lit) {
    const std::size_t len = std::char_traits<char>::length(lit);
    if (text_.compare(pos_, len, lit) == 0) {
      pos_ += len;
      return true;
    }
    return false;
  }

  std::optional<Value> parse_value() {
    skip_ws();
    if (eof()) return std::nullopt;
    switch (peek()) {
      case '{':
        return parse_object();
      case '[':
        return parse_array();
      case '"':
        return parse_string_value();
      case 't':
        return consume_lit("true") ? std::optional(Value(true)) : std::nullopt;
      case 'f':
        return consume_lit("false") ? std::optional(Value(false))
                                    : std::nullopt;
      case 'n':
        return consume_lit("null") ? std::optional(Value()) : std::nullopt;
      default:
        return parse_number();
    }
  }

  std::optional<Value> parse_object() {
    take();  // '{'
    Object obj;
    skip_ws();
    if (consume('}')) return Value(std::move(obj));
    while (true) {
      skip_ws();
      auto key = parse_string();
      if (!key) return std::nullopt;
      skip_ws();
      if (!consume(':')) return std::nullopt;
      auto value = parse_value();
      if (!value) return std::nullopt;
      obj.emplace(std::move(*key), std::move(*value));
      skip_ws();
      if (consume(',')) continue;
      if (consume('}')) break;
      return std::nullopt;
    }
    return Value(std::move(obj));
  }

  std::optional<Value> parse_array() {
    take();  // '['
    Array arr;
    skip_ws();
    if (consume(']')) return Value(std::move(arr));
    while (true) {
      auto value = parse_value();
      if (!value) return std::nullopt;
      arr.push_back(std::move(*value));
      skip_ws();
      if (consume(',')) continue;
      if (consume(']')) break;
      return std::nullopt;
    }
    return Value(std::move(arr));
  }

  std::optional<std::string> parse_string() {
    if (!consume('"')) return std::nullopt;
    std::string out;
    while (true) {
      if (eof()) return std::nullopt;
      const char c = take();
      if (c == '"') break;
      if (static_cast<unsigned char>(c) < 0x20) return std::nullopt;
      if (c != '\\') {
        out.push_back(c);
        continue;
      }
      if (eof()) return std::nullopt;
      const char esc = take();
      switch (esc) {
        case '"':
          out.push_back('"');
          break;
        case '\\':
          out.push_back('\\');
          break;
        case '/':
          out.push_back('/');
          break;
        case 'b':
          out.push_back('\b');
          break;
        case 'f':
          out.push_back('\f');
          break;
        case 'n':
          out.push_back('\n');
          break;
        case 'r':
          out.push_back('\r');
          break;
        case 't':
          out.push_back('\t');
          break;
        case 'u': {
          // Decode a \uXXXX escape (BMP only; surrogate pairs are emitted as
          // two replacement-free code units — sufficient for ASCII node ids).
          if (pos_ + 4 > text_.size()) return std::nullopt;
          unsigned int code = 0;
          for (int i = 0; i < 4; ++i) {
            const char h = text_[pos_++];
            code <<= 4;
            if (h >= '0' && h <= '9') code |= static_cast<unsigned>(h - '0');
            else if (h >= 'a' && h <= 'f') code |= static_cast<unsigned>(h - 'a' + 10);
            else if (h >= 'A' && h <= 'F') code |= static_cast<unsigned>(h - 'A' + 10);
            else return std::nullopt;
          }
          if (code < 0x80) {
            out.push_back(static_cast<char>(code));
          } else if (code < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (code >> 6)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
          } else {
            out.push_back(static_cast<char>(0xE0 | (code >> 12)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
          }
          break;
        }
        default:
          return std::nullopt;
      }
    }
    return out;
  }

  std::optional<Value> parse_string_value() {
    auto s = parse_string();
    if (!s) return std::nullopt;
    return Value(std::move(*s));
  }

  std::optional<Value> parse_number() {
    const std::size_t start = pos_;
    if (peek() == '-') ++pos_;
    while (!eof() && std::isdigit(static_cast<unsigned char>(peek()))) ++pos_;
    if (pos_ == start) return std::nullopt;
    if (peek() == '.') {
      ++pos_;
      if (eof() || !std::isdigit(static_cast<unsigned char>(peek())))
        return std::nullopt;
      while (!eof() && std::isdigit(static_cast<unsigned char>(peek()))) ++pos_;
    }
    if (peek() == 'e' || peek() == 'E') {
      ++pos_;
      if (peek() == '+' || peek() == '-') ++pos_;
      if (eof() || !std::isdigit(static_cast<unsigned char>(peek())))
        return std::nullopt;
      while (!eof() && std::isdigit(static_cast<unsigned char>(peek()))) ++pos_;
    }
    const std::string slice = text_.substr(start, pos_ - start);
    double value = 0.0;
    const auto [ptr, ec] =
        std::from_chars(slice.data(), slice.data() + slice.size(), value);
    if (ec != std::errc{} || ptr != slice.data() + slice.size())
      return std::nullopt;
    return Value(value);
  }
};

void serialize_string(const std::string& s, std::ostringstream& out) {
  out << '"';
  for (const char c : s) {
    switch (c) {
      case '"':
        out << "\\\"";
        break;
      case '\\':
        out << "\\\\";
        break;
      case '\b':
        out << "\\b";
        break;
      case '\f':
        out << "\\f";
        break;
      case '\n':
        out << "\\n";
        break;
      case '\r':
        out << "\\r";
        break;
      case '\t':
        out << "\\t";
        break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", c);
          out << buf;
        } else {
          out << c;
        }
    }
  }
  out << '"';
}

void serialize_impl(const Value& v, std::ostringstream& out) {
  switch (v.kind()) {
    case Value::Kind::Null:
      out << "null";
      break;
    case Value::Kind::Bool:
      out << (v.as_bool() ? "true" : "false");
      break;
    case Value::Kind::Number: {
      const double n = v.as_number();
      if (std::isfinite(n) && n == std::floor(n) && std::fabs(n) < 1e15) {
        out << static_cast<long long>(n);
      } else {
        out << n;
      }
      break;
    }
    case Value::Kind::String:
      serialize_string(v.as_string(), out);
      break;
    case Value::Kind::Array: {
      out << '[';
      bool first = true;
      for (const auto& item : v.as_array()) {
        if (!first) out << ',';
        first = false;
        serialize_impl(item, out);
      }
      out << ']';
      break;
    }
    case Value::Kind::Object: {
      out << '{';
      bool first = true;
      for (const auto& [key, value] : v.as_object()) {
        if (!first) out << ',';
        first = false;
        serialize_string(key, out);
        out << ':';
        serialize_impl(value, out);
      }
      out << '}';
      break;
    }
  }
}

}  // namespace

std::optional<Value> parse(const std::string& text) {
  Parser parser(text);
  return parser.run();
}

std::string serialize(const Value& value) {
  std::ostringstream out;
  serialize_impl(value, out);
  return out.str();
}

}  // namespace evo::json
