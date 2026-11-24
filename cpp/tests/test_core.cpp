// Testes do núcleo sem PipeWire: bandas, JSON, estado e presets.
#include "analyzer.h"
#include "bands.h"
#include "chain.h"
#include "fx.h"
#include "json.h"
#include "state.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace qseq;

static int failures = 0;
#define CHECK(c)                                                              \
    do {                                                                      \
        if (!(c)) {                                                           \
            std::fprintf(stderr, "%s:%d: falhou: %s\n", __FILE__, __LINE__, #c); \
            failures++;                                                       \
        }                                                                     \
    } while (0)

static void bandas()
{
    CHECK(label(31.5f) == "31.5");
    CHECK(label(630) == "630");
    CHECK(label(1000) == "1k");
    CHECK(label(1250) == "1k25");
    CHECK(label(3150) == "3k15");
    CHECK(label(12500) == "12k5");
    CHECK(balanceMults(0) == std::make_pair(1.0f, 1.0f));
    CHECK(balanceMults(-10) == std::make_pair(1.0f, 0.0f));
    CHECK(balanceMults(10) == std::make_pair(0.0f, 1.0f));
    auto [l, r] = balanceMults(2);
    CHECK(std::fabs(l - 0.501f) < 0.01f && r == 1.0f);
    CHECK(clampBand(20) == BAND_MAX);
    CHECK(clampBand(1.04f) == 1.0f);

    // curva: plana = 0; uma banda isolada atinge o próprio ganho no centro
    std::vector<float> b(N, 0.0f);
    CHECK(curvePeakDb(b) == 0);
    b[17] = 6; // 1 kHz
    CHECK(std::fabs(curveDb(b, 1000) - 6) < 0.05f);
    CHECK(std::fabs(curveDb(b, 100)) < 0.2f);
    // bandas vizinhas se somam: o pico passa do maior ganho individual
    for (int i = 0; i < 6; i++)
        b[i] = 8;
    CHECK(curvePeakDb(b) > 8);
}

static void json_()
{
    json::Value v;
    CHECK(json::parse(R"({ "name": "fx_3d", "a": [1, -2.5, 3e1], "b": true, "n": null })", v));
    CHECK(v.get("name") && v.get("name")->str == "fx_3d");
    CHECK(v.get("a") && v.get("a")->arr.size() == 3 && v.get("a")->arr[2].num == 30);
    CHECK(v.get("b") && v.get("b")->b);
    CHECK(!json::parse("{ \"x\": ", v));
    CHECK(json::quote("a\"b\\c") == "\"a\\\"b\\\\c\"");
}

static void estado()
{
    State st;
    st.bands[3] = 4.5f;
    st.gain_db = -3;
    st.balance = 2;
    st.preset = "rock";
    st.modified = true;
    st.enabled = true;
    st.upstream = "fx_3d";
    State back;
    CHECK(State::fromJson(st.toJson(), back));
    CHECK(back.bands[3] == 4.5f && back.gain_db == -3 && back.balance == 2);
    CHECK(back.preset == "rock" && back.modified && back.enabled && back.upstream == "fx_3d");

    // gerado pela versão em Rust (serde_json::to_string_pretty)
    const char *rust = "{\n  \"bands\": [\n    1.0,\n    -2.5\n  ],\n  \"gain_db\": 3.0,\n"
                       "  \"balance\": -1,\n  \"preset\": \"pop\",\n  \"modified\": false,\n"
                       "  \"enabled\": false,\n  \"upstream\": \"\"\n}";
    CHECK(State::fromJson(rust, back));
    CHECK(back.bands.size() == N && back.bands[1] == -2.5f && back.bands[2] == 0);
    CHECK(back.gain_db == 3 && back.balance == -1 && back.preset == "pop");
}

static void presets()
{
    std::vector<float> b;
    CHECK(parsePreset("# x\n20 1.5\n1000 -3\n31.5 2 # comentário\n", b));
    CHECK(b[0] == 1.5f && b[2] == 2.0f && b[17] == -3.0f);
    CHECK(!parsePreset("lixo\n", b));
    CHECK(validName("meu_perfil-2") && !validName("a/b") && !validName(""));

    std::string err;
    CHECK(loadPreset("rock", b, err)); // QSEQ_SHARE aponta para share/quickshell-eq
    CHECK(b.size() == N && b[0] == 1.0f);
    auto names = listPresets();
    CHECK(names.size() >= 22);
}

static float param(const Params &p, const std::string &k)
{
    for (const auto &[name, v] : p)
        if (name == k)
            return v;
    return -999;
}

static void efeitos()
{
    State st;
    Params p = paramsFor(st);
    // padrão: tudo seco, limitador em -1 dB
    CHECK(param(p, "mixCL:Gain 1") == 1 && param(p, "mixCL:Gain 2") == 0);
    CHECK(param(p, "spat:Width") == 0 && param(p, "echo:Dry/Wet level") == 0);
    CHECK(param(p, "revL:Dry/wet mix") == 0 && param(p, "revR:Dry/wet mix") == 0);
    CHECK(param(p, "lim:Limit (dB)") == fx::LIMIT_ON);
    CHECK(param(p, "lim:Input gain (dB)") == 0 && param(p, "lim:Release time (s)") == fx::LIMIT_RELEASE);

    st.fx_comp = st.fx_spatial = st.fx_echo = true;
    st.fx_limiter = false;
    st.reverb = "Opera";
    p = paramsFor(st);
    CHECK(param(p, "mixCR:Gain 1") == 0 && param(p, "mixCR:Gain 2") == 1);
    CHECK(param(p, "spat:Width") == fx::SPATIAL_WIDTH + 120); // 3D + abertura do modo Opera
    CHECK(param(p, "echo:Dry/Wet level") == fx::ECHO.wet);
    CHECK(param(p, "revL:Reverb time") == 2.6f && param(p, "revR:Dry/wet mix") == 0.30f);
    CHECK(param(p, "lim:Limit (dB)") == fx::LIMIT_OFF);

    std::string a = filterArgs(st, "fx_3d");
    CHECK(a.find("dysonCompress") != std::string::npos && a.find("label = plate") != std::string::npos);
    CHECK(a.find("\"Dry/wet mix\" = 0.3") != std::string::npos); // o grafo nasce no estado salvo
    CHECK(a.find("target.object = \"fx_3d\"") != std::string::npos);

    CHECK(fx::reverbIndex("Caverna") == 4 && fx::reverbIndex("nada") == 0);
    CHECK(param(p, "echo:Feedback") == fx::ECHO.feedback && param(p, "echo:R delay (ms)") == 375);

    // reverb sozinho já abre o estéreo; somado ao 3D, respeita o limite do plugin
    State cav;
    cav.reverb = "Caverna";
    CHECK(param(paramsFor(cav), "spat:Width") == 300);
    cav.fx_spatial = true;
    CHECK(param(paramsFor(cav), "spat:Width") == fx::SPATIAL_WIDTH + 300);
    st.reverb = "nada";
    st.normalize();
    CHECK(st.reverb == "Off");

    State back;
    st.reverb = "Sala";
    CHECK(State::fromJson(st.toJson(), back));
    CHECK(back.fx_comp && !back.fx_limiter && back.fx_spatial && back.fx_echo && back.reverb == "Sala");
}

static void analisador()
{
    // senoide de 1 kHz, amplitude 0,5 (-6 dBFS), janela de Hann como no Analyzer
    const int n = Analyzer::FFT_N;
    std::vector<float> re(n), im(n, 0.0f);
    double s1 = 0, s2 = 0;
    for (int i = 0; i < n; i++) {
        double w = 0.5 - 0.5 * std::cos(2 * M_PI * i / (n - 1));
        s1 += w;
        s2 += w * w;
        re[i] = static_cast<float>(0.5 * std::sin(2 * M_PI * 1000.0 * i / 48000.0) * w);
    }
    fft(re, im);
    std::vector<float> p(n / 2 + 1);
    for (int k = 0; k <= n / 2; k++)
        p[k] = re[k] * re[k] + im[k] * im[k];
    auto lv = bandLevels(p, 48000, s1, n * s2 / (s1 * s1));
    CHECK(std::fabs(lv[17] + 6.02f) < 0.5f); // banda de 1 kHz
    CHECK(lv[5] < -60 && lv[28] < -60);       // 63 Hz e 12,5 kHz quase em silêncio
    CHECK(lv[0] >= Analyzer::FLOOR_DB);
}

int main()
{
    bandas();
    json_();
    estado();
    presets();
    efeitos();
    analisador();
    if (failures)
        std::fprintf(stderr, "%d falha(s)\n", failures);
    else
        std::puts("ok");
    return failures ? 1 : 0;
}
