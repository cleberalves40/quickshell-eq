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
