#ifndef EDITOR_JSON_H
#define EDITOR_JSON_H

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace editor {

// Enough JSON for a project file, and no more. A hand-written reader of about
// two hundred lines rather than a library, because the whole point of the
// project file is that a person can open it and understand it - and a format
// that simple does not need a parser anyone has to go and fetch.
//
// Objects keep the order they were written in, which matters here: a file
// rewritten by the editor should come back looking like the one that went in.
class Json {
public:
    enum Type { Null, Bool, Number, String, Array, Object };

    Json() : type_(Null), bool_(false), number_(0) {}

    static Json fromBool(bool value);
    static Json fromNumber(double value);
    static Json fromText(const std::string& value);
    static Json array();
    static Json object();

    Type type() const { return type_; }
    bool is(Type t) const { return type_ == t; }

    bool boolean(bool fallback = false) const;
    long integer(long fallback = 0) const;
    std::string text(const std::string& fallback = std::string()) const;

    size_t size() const;
    const Json& at(size_t index) const;

    bool has(const std::string& key) const;
    const Json& get(const std::string& key) const;

    // An object's members in the order they were written. What lets "groups"
    // be an object of arrays and still come back in the order it went in.
    const std::string& keyAt(size_t index) const;
    const Json& valueAt(size_t index) const;

    void push(const Json& value);
    void set(const std::string& key, const Json& value);

    // Reads text into a value. On failure the reason says where it gave up, in
    // words meant for whoever has to fix the file.
    static Json parse(const std::string& text, std::string& error);

    // Written back out with two-space indentation, which is what a file meant
    // to be read and edited by hand wants.
    std::string write(int depth = 0) const;

private:
    Type type_;
    bool bool_;
    double number_;
    std::string text_;
    std::vector<Json> items_;
    std::vector<std::pair<std::string, Json> > members_;
};

}  // namespace editor

#endif
