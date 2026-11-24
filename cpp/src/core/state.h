// Estado persistente (~/.config/quickshell-eq/state.json) e presets.
// Mesmo formato da versão em Rust: as duas leem os mesmos arquivos.
#ifndef QSEQ_STATE_H
#define QSEQ_STATE_H

#include "bands.h"

#include <string>
#include <vector>

namespace qseq {

struct State {
    std::vector<float> bands = std::vector<float>(N, 0.0f); // dB
    float gain_db = 0;      // ganho de entrada do limitador
    int balance = 0;        // -10 (só esquerdo) .. +10 (só direito)
    std::string preset = "flat";
    bool modified = false;  // bandas mexidas depois de carregar o preset
    bool enabled = false;   // o EQ deve ficar no topo da cadeia
    std::string upstream;   // onde o EQ despeja o som
    // efeitos (ver fx.h)
    bool fx_comp = false;
    bool fx_limiter = true; // o limitador sempre protegeu o ganho; ligado por padrão
    bool fx_spatial = false;
    bool fx_echo = false;
    std::string reverb = "Off";

    static State load();
    bool save(std::string *err = nullptr) const;
    void normalize();
    std::string toJson() const;
    static bool fromJson(const std::string &text, State &out);
};

std::string configDir();
std::string runtimeDir();

std::vector<std::string> presetDirs();
std::vector<std::string> listPresets();
bool loadPreset(const std::string &name, std::vector<float> &out, std::string &err);
bool parsePreset(const std::string &text, std::vector<float> &out);
bool validName(const std::string &name);
bool savePreset(const std::string &name, const std::vector<float> &bands, std::string &err);

} // namespace qseq

#endif
