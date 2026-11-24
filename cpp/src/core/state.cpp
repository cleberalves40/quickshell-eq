#include "state.h"

#include "fx.h"
#include "json.h"
#include "sys.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <sstream>

namespace qseq {

namespace {

std::string home()
{
    const char *h = std::getenv("HOME");
    return h && *h ? h : "/tmp";
}

std::string fmt(float v)
{
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.1f", v);
    return buf;
}

} // namespace

std::string configDir()
{
    const char *x = std::getenv("XDG_CONFIG_HOME");
    return std::string(x && *x ? x : (home() + "/.config").c_str()) + "/quickshell-eq";
}

std::string runtimeDir()
{
    const char *x = std::getenv("XDG_RUNTIME_DIR");
    if (x && *x)
        return std::string(x) + "/quickshell-eq";
    return configDir() + "/run";
}

void State::normalize()
{
    bands.resize(N, 0.0f);
    for (float &b : bands)
        b = clampBand(b);
    gain_db = std::round(std::clamp(gain_db, -GAIN_MAX, GAIN_MAX));
    balance = std::clamp(balance, -BAL_MAX, BAL_MAX);
    reverb = fx::REVERBS[fx::reverbIndex(reverb.c_str())].name;
}

std::string State::toJson() const
{
    std::ostringstream o;
    o << "{\n  \"bands\": [";
    for (size_t i = 0; i < bands.size(); i++)
        o << (i ? ", " : "") << fmt(bands[i]);
    o << "],\n  \"gain_db\": " << fmt(gain_db) << ",\n  \"balance\": " << balance
      << ",\n  \"preset\": " << json::quote(preset)
      << ",\n  \"modified\": " << (modified ? "true" : "false")
      << ",\n  \"enabled\": " << (enabled ? "true" : "false")
      << ",\n  \"upstream\": " << json::quote(upstream)
      << ",\n  \"fx_comp\": " << (fx_comp ? "true" : "false")
      << ",\n  \"fx_limiter\": " << (fx_limiter ? "true" : "false")
      << ",\n  \"fx_spatial\": " << (fx_spatial ? "true" : "false")
      << ",\n  \"fx_echo\": " << (fx_echo ? "true" : "false")
      << ",\n  \"reverb\": " << json::quote(reverb) << "\n}\n";
    return o.str();
}

bool State::fromJson(const std::string &text, State &st)
{
    json::Value v;
    if (!json::parse(text, v) || v.type != json::Value::Object)
        return false;
    st = State{};
    if (auto b = v.get("bands"); b && b->type == json::Value::Array) {
        st.bands.clear();
        for (const auto &x : b->arr)
            st.bands.push_back(static_cast<float>(x.num));
    }
    if (auto g = v.get("gain_db"))
        st.gain_db = static_cast<float>(g->num);
    if (auto b = v.get("balance"))
        st.balance = static_cast<int>(std::lround(b->num));
    if (auto p = v.get("preset"); p && p->type == json::Value::String)
        st.preset = p->str;
    if (auto m = v.get("modified"))
        st.modified = m->b;
    if (auto e = v.get("enabled"))
        st.enabled = e->b;
    if (auto u = v.get("upstream"); u && u->type == json::Value::String)
        st.upstream = u->str;
    auto flag = [&](const char *k, bool &dst) {
        if (auto x = v.get(k); x && x->type == json::Value::Bool)
            dst = x->b;
    };
    flag("fx_comp", st.fx_comp);
    flag("fx_limiter", st.fx_limiter);
    flag("fx_spatial", st.fx_spatial);
    flag("fx_echo", st.fx_echo);
    if (auto r = v.get("reverb"); r && r->type == json::Value::String)
        st.reverb = r->str;
    st.normalize();
    return true;
}

State State::load()
{
    State st;
    std::string text;
    if (sys::readFile(configDir() + "/state.json", text))
        fromJson(text, st);
    st.normalize();
    return st;
}

bool State::save(std::string *err) const
{
    std::string dir = configDir();
    std::string e;
    if (!sys::mkdirs(dir, e) || !sys::writeFileAtomic(dir + "/state.json", toJson(), e)) {
        if (err)
            *err = e;
        return false;
    }
    return true;
}

// ── Presets: "<freq Hz> <ganho dB>" por linha; '#' comenta ──────────────

std::vector<std::string> presetDirs()
{
    const char *share = std::getenv("QSEQ_SHARE");
    std::string s = share && *share ? share : "/usr/local/share/quickshell-eq";
    return {configDir() + "/presets", s + "/presets"};
}

std::vector<std::string> listPresets()
{
    std::vector<std::string> names;
    for (const auto &d : presetDirs()) {
        DIR *dir = opendir(d.c_str());
        if (!dir)
            continue;
        while (dirent *e = readdir(dir)) {
            std::string n = e->d_name;
            if (n.size() > 4 && n.compare(n.size() - 4, 4, ".txt") == 0)
                names.push_back(n.substr(0, n.size() - 4));
        }
        closedir(dir);
    }
    auto lower = [](std::string s) {
        for (char &c : s)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    std::sort(names.begin(), names.end(),
              [&](const std::string &a, const std::string &b) { return lower(a) < lower(b); });
    names.erase(std::unique(names.begin(), names.end()), names.end());
    return names;
}

bool parsePreset(const std::string &text, std::vector<float> &out)
{
    out.assign(N, 0.0f);
    int seen = 0;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        line = line.substr(0, line.find('#'));
        std::istringstream ls(line);
        float f, g;
        if (!(ls >> f >> g))
            continue;
        for (int i = 0; i < N; i++) {
            if (std::fabs(FREQS[i] - f) <= FREQS[i] * 0.02f) {
                out[i] = clampBand(g);
                seen++;
                break;
            }
        }
    }
    return seen > 0;
}

bool loadPreset(const std::string &name, std::vector<float> &out, std::string &err)
{
    for (const auto &d : presetDirs()) {
        std::string text;
        std::string path = d + "/" + name + ".txt";
        if (!sys::readFile(path, text))
            continue;
        if (parsePreset(text, out))
            return true;
        err = path + ": nenhuma banda reconhecida";
        return false;
    }
    err = "preset '" + name + "' não encontrado";
    return false;
}

bool validName(const std::string &name)
{
    if (name.empty() || name.size() > 40)
        return false;
    return std::all_of(name.begin(), name.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-';
    });
}

bool savePreset(const std::string &name, const std::vector<float> &bands, std::string &err)
{
    if (!validName(name)) {
        err = "nome inválido (use letras, números, _ e -)";
        return false;
    }
    std::string dir = configDir() + "/presets";
    if (!sys::mkdirs(dir, err))
        return false;
    std::ostringstream o;
    o << "# quickshell-eq: <freq Hz> <ganho dB>\n";
    for (int i = 0; i < N; i++)
        o << FREQS[i] << ' ' << fmt(bands[i]) << '\n';
    return sys::writeFileAtomic(dir + "/" + name + ".txt", o.str(), err);
}

} // namespace qseq
