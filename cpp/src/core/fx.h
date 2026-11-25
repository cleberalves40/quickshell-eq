// Efeitos, com os mesmos parâmetros do EqualizerPopup do Quickshell.
//
// Todos ficam sempre no grafo e são ligados ou desligados ao vivo pelos
// parâmetros (mistura seco/processado), sem recarregar o filtro:
//
//   EQ → compressor (mixer seco/comprimido) → 3D (largura 0 = neutro)
//      → eco (Dry/Wet 0 = seco) → reverb L/R (Dry/wet 0 = seco)
//      → balanço → limitador (-1 dB ligado, 0 dB desligado) + ganho
#ifndef QSEQ_FX_H
#define QSEQ_FX_H

#include <array>

namespace qseq::fx {

constexpr const char *LADSPA = "/usr/local/lib/ladspa/";

// dysonCompress: Peak limit (dB), Release time (s), Fast compression ratio, Compression ratio
constexpr float COMP_PEAK = -12, COMP_RELEASE = 0.4f, COMP_FAST = 0.4f, COMP_RATIO = 0.6f;

// fastLookaheadLimiter: Limit (dB) e Release time (s). Soltura de 0,25 s: com
// 0,05 s o limitador deformava a onda dos graves (soava como saturação).
constexpr float LIMIT_ON = -1.0f, LIMIT_OFF = 0.0f;
constexpr float LIMIT_RELEASE = 0.25f;

// matrixSpatialiser: Width
constexpr int SPATIAL_WIDTH = 200;

// lcrDelay: eco estéreo L 250 ms, R 375 ms. Um pouco mais presente que o do
// popup (feedback 30→35%, níveis 20→24, mistura 30→40%).
struct EchoParams {
    float lDelay = 250, lLevel = 24, cDelay = 0, cLevel = 0, rDelay = 375, rLevel = 24;
    float feedback = 35, highDamp = 50, lowDamp = 20, spread = 25, wet = 0.4f;
};
constexpr EchoParams ECHO{};

// Efeitos de modulação (plugins de Steve Harris). Os dois já devolvem o som original
// misturado (chorus: 100%; flanger: 71%), então o mixer só alterna seco/processado,
// como no compressor, com o volume do lado processado igualado ao do seco. O LFO do canal direito é um pouco mais rápido: os lados ondulam
// fora de fase e o efeito abre no estéreo.
// multivoiceChorus: Number of voices, Delay base (ms), Voice separation (ms), Detune (%),
// LFO frequency (Hz) [2-30], Output attenuation (dB)
struct ChorusParams {
    float voices = 3, base = 18, separation = 1, detune = 1.5f, lfoL = 2.0f, lfoR = 2.3f, atten = -4;
};
constexpr ChorusParams CHORUS{};
// flanger: Delay base (ms), Max slowdown (ms), LFO frequency (Hz), Feedback [-1, 1]
// Ajuste "jato" de synth-pop do fim dos anos 80 (referência: Round & Round, New Order):
// varredura lenta (~5 s), funda e ressonante.
struct FlangerParams {
    float base = 2, depth = 4.5f, lfoL = 0.20f, lfoR = 0.23f, feedback = 0.7f;
    // Medido com ruído rosa: com estes ajustes a saída fica 2,1 dB acima do seco (a
    // ressonância soma energia); 0,8 iguala, e ligar o efeito não muda o volume.
    float makeup = 0.8f;
};
constexpr FlangerParams FLANGER{};

// plate: Reverb time, Damping, Dry/wet mix. Cada modo também abre o estéreo
// (largura do matrixSpatialiser): quanto maior o espaço, mais envolvente. Com o
// 3D ligado, as larguras se somam (limite do plugin: 512).
struct ReverbMode {
    const char *name;
    float time, damping, mix;
    int width;
};
constexpr std::array<ReverbMode, 5> REVERBS = {{
    {"Off", 1.2f, 0.5f, 0.0f, 0},
    {"Sala", 1.2f, 0.5f, 0.20f, 60},
    {"Opera", 2.6f, 0.35f, 0.30f, 120},
    {"Stadium", 4.5f, 0.25f, 0.35f, 180},
    {"Caverna", 6.5f, 0.15f, 0.45f, 300},
}};
constexpr int WIDTH_MAX = 512;

/** Índice do modo pelo nome; 0 (Off) se não existir. */
inline int reverbIndex(const char *name)
{
    for (size_t i = 0; i < REVERBS.size(); i++) {
        const char *a = REVERBS[i].name;
        const char *b = name;
        while (*a && *a == *b) {
            a++;
            b++;
        }
        if (*a == 0 && *b == 0)
            return static_cast<int>(i);
    }
    return 0;
}

} // namespace qseq::fx

#endif
