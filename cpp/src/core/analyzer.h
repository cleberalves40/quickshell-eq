// Analisador de espectro: captura nativa (pw_stream) do monitor de um sink e
// FFT por bandas ISO de 1/3 de oitava, alinhadas com as 31 bandas do EQ.
#ifndef QSEQ_ANALYZER_H
#define QSEQ_ANALYZER_H

#include "bands.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

struct pw_stream;

namespace qseq {

class Pw;

class Analyzer {
public:
    static constexpr int FFT_N = 8192;     // ~5,9 Hz por bin a 48 kHz
    static constexpr float FLOOR_DB = -90;
    using Levels = std::array<float, N>;

    explicit Analyzer(Pw &pw);
    ~Analyzer();
    Analyzer(const Analyzer &) = delete;
    Analyzer &operator=(const Analyzer &) = delete;

    /** Escuta o monitor do sink; refaz o stream se o nó mudou. Com a trava do Pw. */
    void setTarget(const std::string &sink);
    /** dBFS por banda (onda senoidal em escala cheia = 0 dB). Sem a trava. */
    Levels levels();
    /** Chegou áudio nos últimos 300 ms. */
    bool receiving() const;

    void onProcess(); // callback do stream

private:
    struct Hook;
    Pw &pw_;
    pw_stream *stream_ = nullptr;
    std::unique_ptr<Hook> hook_;
    std::string target_;
    uint32_t targetId_ = 0;

    std::mutex m_;
    std::vector<float> ring_;
    size_t pos_ = 0;
    std::atomic<int64_t> lastNs_{0};

    std::vector<float> window_;
    double s1_ = 0, enbw_ = 1;
};

/** FFT complexa in-place (raiz 2); n deve ser potência de 2. Exposta para os testes. */
void fft(std::vector<float> &re, std::vector<float> &im);
/** Potência por banda ISO a partir de |X|² (bins 0..n/2), em dBFS. */
Analyzer::Levels bandLevels(const std::vector<float> &power, float rate, double s1, double enbw);

} // namespace qseq

#endif
