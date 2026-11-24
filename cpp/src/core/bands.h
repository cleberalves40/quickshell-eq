// Bandas ISO de 1/3 de oitava e os limites dos controles.
#ifndef QSEQ_BANDS_H
#define QSEQ_BANDS_H

#include <array>
#include <string>
#include <utility>
#include <vector>

namespace qseq {

constexpr int N = 31;

constexpr std::array<float, N> FREQS = {
    20, 25, 31.5f, 40, 50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630,
    800, 1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500, 16000, 20000,
};

constexpr float Q = 4.32f;          // peaking com largura de 1/3 de oitava
constexpr float BAND_MAX = 12.0f;
constexpr float BAND_STEP = 0.5f;
constexpr float GAIN_MAX = 12.0f;
constexpr int BAL_MAX = 10;
constexpr float BAL_DB_STEP = 3.0f; // atenuação por passo do balanço

/** Rótulo curto: 31.5, 630, 1k, 1k25, 12k5. */
std::string label(float f);
/** Multiplicadores (esquerdo, direito) para um balanço em -10..10. */
std::pair<float, float> balanceMults(int bal);
/** Limita a ±BAND_MAX e arredonda para 0,1 dB. */
float clampBand(float v);

/** Resposta (dB) dos 31 filtros peaking em cascata na frequência f (RBJ, como o bq_peaking). */
float curveDb(const std::vector<float> &bands, float f, float rate = 48000.0f);
/** Maior ganho da curva entre 20 Hz e 20 kHz (0 se a curva só atenua). */
float curvePeakDb(const std::vector<float> &bands, float rate = 48000.0f);

} // namespace qseq

#endif
