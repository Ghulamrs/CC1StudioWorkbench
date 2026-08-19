#include "json.h"

#include <cstdio>
#include <cstdlib>

namespace editor {

namespace {

// Made once and never destroyed, deliberately. A function-local static with a
// destructor registers an atexit handler the first time it is reached, and
// doing that from a native file inside a mixed native/managed binary corrupts
// the heap - found from a stack trace on Windows reading
//
//     Json::get -> atexit -> register_onexit_function -> RtlSizeHeap
//
// One object of a few bytes outlives the program instead. Nothing here owns
// anything the operating system will not take back.
const Json& nothing() {
    static const Json* empty = new Json();
    return *empty;
}

bool space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

struct Reader {
    const std::string& in;
    size_t at;
    std::string error;

    Reader(const std::string& text) : in(text), at(0) {}

    void skip() {
        for (;;) {
            while (at < in.size() && space(in[at])) ++at;
            // Not JSON, and deliberate: a project file people edit by hand is
            // a file people want to leave notes in.
            if (at + 1 < in.size() && in[at] == '/' && in[at + 1] == '/') {
                while (at < in.size() && in[at] != '\n') ++at;
                continue;
            }
            return;
        }
    }

    bool fail(const std::string& what) {
        if (error.empty()) {
            char where[32];
            std::snprintf(where, sizeof where, " at character %lu",
                          static_cast<unsigned long>(at + 1));
            error = what + where;
        }
        return false;
    }

    bool literal(const char* word) {
        size_t n = 0;
        while (word[n]) ++n;
        if (in.compare(at, n, word) != 0) return false;
        at += n;
        return true;
    }

    bool string(std::string& out) {
        if (at >= in.size() || in[at] != '"') return fail("expected a string");
        ++at;
        out.clear();
        while (at < in.size()) {
            char c = in[at++];
            if (c == '"') return true;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (at >= in.size()) break;
            char esc = in[at++];
            switch (esc) {
                case '"':  out += '"';  break;
                case '\\': out += '\\'; break;
                case '/':  out += '/';  break;
                case 'n':  out += '\n'; break;
                case 't':  out += '\t'; break;
                case 'r':  out += '\r'; break;
                case 'b':  out += '\b'; break;
                case 'f':  out += '\f'; break;
                default:   return fail("unknown escape in a string");
            }
        }
        return fail("a string was never closed");
    }

    bool value(Json& out) {
        skip();
        if (at >= in.size()) return fail("the file ended too early");

        char c = in[at];
        if (c == '"') {
            std::string s;
            if (!string(s)) return false;
            out = Json::fromText(s);
            return true;
        }
        if (c == '{') return object(out);
        if (c == '[') return array(out);
        if (literal("true"))  { out = Json::fromBool(true);  return true; }
        if (literal("false")) { out = Json::fromBool(false); return true; }
        if (literal("null"))  { out = Json();                return true; }

        if (c == '-' || (c >= '0' && c <= '9')) {
            size_t start = at;
            if (in[at] == '-') ++at;
            while (at < in.size() && ((in[at] >= '0' && in[at] <= '9') || in[at] == '.' ||
                                      in[at] == 'e' || in[at] == 'E' || in[at] == '+' ||
                                      in[at] == '-'))
                ++at;
            out = Json::fromNumber(std::atof(in.substr(start, at - start).c_str()));
            return true;
        }

        return fail("expected a value");
    }

    bool array(Json& out) {
        out = Json::array();
        ++at;   // [
        skip();
        if (at < in.size() && in[at] == ']') { ++at; return true; }

        for (;;) {
            Json item;
            if (!value(item)) return false;
            out.push(item);

            skip();
            if (at < in.size() && in[at] == ',') { ++at; continue; }
            if (at < in.size() && in[at] == ']') { ++at; return true; }
            return fail("expected a comma or a closing bracket");
        }
    }

    bool object(Json& out) {
        out = Json::object();
        ++at;   // {
        skip();
        if (at < in.size() && in[at] == '}') { ++at; return true; }

        for (;;) {
            skip();
            std::string key;
            if (!string(key)) return false;

            skip();
            if (at >= in.size() || in[at] != ':') return fail("expected a colon");
            ++at;

            Json item;
            if (!value(item)) return false;
            out.set(key, item);

            skip();
            if (at < in.size() && in[at] == ',') { ++at; continue; }
            if (at < in.size() && in[at] == '}') { ++at; return true; }
            return fail("expected a comma or a closing brace");
        }
    }
};

std::string quoted(const std::string& s) {
    std::string out = "\"";
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\t': out += "\\t";  break;
            case '\r': out += "\\r";  break;
            default:   out += c;      break;
        }
    }
    return out + "\"";
}

}  // namespace

Json Json::fromBool(bool value) {
    Json j;
    j.type_ = Bool;
    j.bool_ = value;
    return j;
}

Json Json::fromNumber(double value) {
    Json j;
    j.type_ = Number;
    j.number_ = value;
    return j;
}

Json Json::fromText(const std::string& value) {
    Json j;
    j.type_ = String;
    j.text_ = value;
    return j;
}

Json Json::array() {
    Json j;
    j.type_ = Array;
    return j;
}

Json Json::object() {
    Json j;
    j.type_ = Object;
    return j;
}

bool Json::boolean(bool fallback) const { return type_ == Bool ? bool_ : fallback; }

long Json::integer(long fallback) const {
    return type_ == Number ? static_cast<long>(number_) : fallback;
}

std::string Json::text(const std::string& fallback) const {
    return type_ == String ? text_ : fallback;
}

size_t Json::size() const {
    if (type_ == Array) return items_.size();
    if (type_ == Object) return members_.size();
    return 0;
}

const Json& Json::at(size_t index) const {
    if (type_ == Array && index < items_.size()) return items_[index];
    return nothing();
}

bool Json::has(const std::string& key) const {
    for (size_t i = 0; i < members_.size(); ++i)
        if (members_[i].first == key) return true;
    return false;
}

const Json& Json::get(const std::string& key) const {
    for (size_t i = 0; i < members_.size(); ++i)
        if (members_[i].first == key) return members_[i].second;
    return nothing();
}

const std::string& Json::keyAt(size_t index) const {
    // Never destroyed, for the reason given above nothing().
    static const std::string* none = new std::string();
    if (type_ == Object && index < members_.size()) return members_[index].first;
    return *none;
}

const Json& Json::valueAt(size_t index) const {
    if (type_ == Object && index < members_.size()) return members_[index].second;
    return nothing();
}

void Json::push(const Json& value) {
    if (type_ != Array) { type_ = Array; items_.clear(); }
    items_.push_back(value);
}

void Json::set(const std::string& key, const Json& value) {
    if (type_ != Object) { type_ = Object; members_.clear(); }
    for (size_t i = 0; i < members_.size(); ++i) {
        if (members_[i].first == key) {
            members_[i].second = value;
            return;
        }
    }
    members_.push_back(std::make_pair(key, value));
}

Json Json::parse(const std::string& text, std::string& error) {
    Reader reader(text);
    Json out;

    if (!reader.value(out)) {
        error = reader.error;
        return Json();
    }

    reader.skip();
    if (reader.at != text.size()) {
        error = "there is more text after the end of the value";
        return Json();
    }

    error.clear();
    return out;
}

std::string Json::write(int depth) const {
    std::string pad(static_cast<size_t>(depth) * 2, ' ');
    std::string inner(static_cast<size_t>(depth + 1) * 2, ' ');

    switch (type_) {
        case Null:   return "null";
        case Bool:   return bool_ ? "true" : "false";
        case String: return quoted(text_);
        case Number: {
            char buffer[32];
            // Written as a whole number when it is one: an indent width of 4
            // should not come back as 4.0.
            if (number_ == static_cast<double>(static_cast<long>(number_)))
                std::snprintf(buffer, sizeof buffer, "%ld", static_cast<long>(number_));
            else
                std::snprintf(buffer, sizeof buffer, "%g", number_);
            return buffer;
        }
        case Array: {
            if (items_.empty()) return "[]";
            std::string out = "[\n";
            for (size_t i = 0; i < items_.size(); ++i) {
                out += inner + items_[i].write(depth + 1);
                if (i + 1 < items_.size()) out += ",";
                out += "\n";
            }
            return out + pad + "]";
        }
        case Object: {
            if (members_.empty()) return "{}";
            std::string out = "{\n";
            for (size_t i = 0; i < members_.size(); ++i) {
                out += inner + quoted(members_[i].first) + ": " +
                       members_[i].second.write(depth + 1);
                if (i + 1 < members_.size()) out += ",";
                out += "\n";
            }
            return out + pad + "}";
        }
    }
    return "null";
}

}  // namespace editor
