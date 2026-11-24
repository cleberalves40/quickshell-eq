// JSON mínimo, só o necessário para o state.json (compatível com a versão em Rust).
#ifndef QSEQ_JSON_H
#define QSEQ_JSON_H

#include <map>
#include <string>
#include <vector>

namespace qseq::json {

struct Value {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    bool b = false;
    double num = 0;
    std::string str;
    std::vector<Value> arr;
    std::map<std::string, Value> obj;

    const Value *get(const std::string &key) const;
};

/** Devolve false se o texto não for JSON válido. */
bool parse(const std::string &text, Value &out);
/** Texto entre aspas, com os escapes necessários. */
std::string quote(const std::string &s);

} // namespace qseq::json

#endif
