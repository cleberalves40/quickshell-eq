#include "bands.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace qseq {

std::string label(float f)
{
    char buf[16];
    if (f >= 1000.0f) {
        std::string s = std::to_string(static_cast<unsigned>(f));
        std::string k = s.substr(0, s.size() - 3);
        std::string rest = s.substr(s.size() - 3);
        while (!rest.empty() && rest.back() == '0')
            rest.pop_back();
        return k + "k" + rest;
    }
    if (f != std::floor(f)) {
        std::snprintf(buf, sizeof buf, "%.1f", f);
        return buf;
    }
    return std::to_string(static_cast<unsigned>(f));
}

std::pair<float, float> balanceMults(int bal)
{
    auto att = [](int steps) {
        return steps >= BAL_MAX ? 0.0f : std::pow(10.0f, -steps * BAL_DB_STEP / 20.0f);
    };
    if (bal < 0)
        return {1.0f, att(-bal)};
    if (bal > 0)
        return {att(bal), 1.0f};
    return {1.0f, 1.0f};
}

float curveDb(const std::vector<float> &bands, float f, float rate)
{
    const double pi = 3.14159265358979323846;
    double w = 2 * pi * f / rate;
    double total = 0;
    for (size_t i = 0; i < bands.size() && i < FREQS.size(); i++) {
        if (bands[i] == 0)
            continue;
        double A = std::pow(10.0, bands[i] / 40.0);
        double w0 = 2 * pi * FREQS[i] / rate;
        double alpha = std::sin(w0) / (2 * Q);
        double b0 = 1 + alpha * A, b1 = -2 * std::cos(w0), b2 = 1 - alpha * A;
        double a0 = 1 + alpha / A, a1 = b1, a2 = 1 - alpha / A;
        // |H(e^jw)| = |b0 + b1 z^-1 + b2 z^-2| / |a0 + a1 z^-1 + a2 z^-2|
        auto mag2 = [&](double c0, double c1, double c2) {
            double re = c0 + c1 * std::cos(w) + c2 * std::cos(2 * w);
            double im = -c1 * std::sin(w) - c2 * std::sin(2 * w);
            return re * re + im * im;
        };
        total += 10 * std::log10(mag2(b0, b1, b2) / mag2(a0, a1, a2));
    }
    return static_cast<float>(total);
}

float curvePeakDb(const std::vector<float> &bands, float rate)
{
    float peak = 0;
    for (int k = 0; k <= 400; k++) { // 20 Hz .. 20 kHz em passos de ~1/40 de oitava
        float f = 20.0f * std::pow(1000.0f, k / 400.0f);
        peak = std::max(peak, curveDb(bands, f, rate));
    }
    for (float f : FREQS)
        peak = std::max(peak, curveDb(bands, f, rate));
    return peak;
}

float clampBand(float v)
{
    return std::round(std::clamp(v, -BAND_MAX, BAND_MAX) * 10.0f) / 10.0f;
}

} // namespace qseq
