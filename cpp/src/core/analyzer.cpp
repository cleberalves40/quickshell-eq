#include "analyzer.h"

#include "pw.h"

#include <algorithm>
#include <cmath>

#include <pipewire/pipewire.h>
#include <spa/param/audio/raw-utils.h>
#include <spa/pod/builder.h>

namespace qseq {

namespace {

constexpr float RATE = 48000.0f; // taxa do grafo (quantum 2048 a 48 kHz)

int64_t nowNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

void onProcessCb(void *data) { static_cast<Analyzer *>(data)->onProcess(); }

const pw_stream_events streamEvents = [] {
    pw_stream_events e{};
    e.version = PW_VERSION_STREAM_EVENTS;
    e.process = onProcessCb;
    return e;
}();

} // namespace

struct Analyzer::Hook {
    spa_hook listener{};
};

void fft(std::vector<float> &re, std::vector<float> &im)
{
    const size_t n = re.size();
    for (size_t i = 1, j = 0; i < n; i++) { // permutação bit-reversa
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j) {
            std::swap(re[i], re[j]);
            std::swap(im[i], im[j]);
        }
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        double ang = -2 * 3.14159265358979323846 / static_cast<double>(len);
        float wr = static_cast<float>(std::cos(ang)), wi = static_cast<float>(std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            float cr = 1, ci = 0;
            for (size_t k = 0; k < len / 2; k++) {
                size_t a = i + k, b = a + len / 2;
                float tr = re[b] * cr - im[b] * ci;
                float ti = re[b] * ci + im[b] * cr;
                re[b] = re[a] - tr;
                im[b] = im[a] - ti;
                re[a] += tr;
                im[a] += ti;
                float nr = cr * wr - ci * wi;
                ci = cr * wi + ci * wr;
                cr = nr;
            }
        }
    }
}

Analyzer::Levels bandLevels(const std::vector<float> &power, float rate, double s1, double enbw)
{
    Analyzer::Levels out{};
    const size_t bins = power.size();
    const double binHz = rate / (2.0 * static_cast<double>(bins - 1));
    const double edge = std::pow(2.0, 1.0 / 6.0); // meia banda de 1/3 de oitava
    for (int i = 0; i < N; i++) {
        double lo = FREQS[i] / edge, hi = FREQS[i] * edge;
        size_t k0 = static_cast<size_t>(std::ceil(lo / binHz));
        size_t k1 = std::min(bins - 1, static_cast<size_t>(std::floor(hi / binHz)));
        double sum = 0;
        if (k0 > k1) { // banda mais estreita que um bin (graves): o bin mais próximo
            size_t k = std::min(bins - 1, static_cast<size_t>(std::lround(FREQS[i] / binHz)));
            sum = power[k];
        } else {
            for (size_t k = k0; k <= k1; k++)
                sum += power[k];
        }
        // Senoide de amplitude A: soma do lóbulo ≈ (A·S1/2)²·ENBW → 20·log10(A)
        double a2 = sum * 4.0 / (s1 * s1 * enbw);
        out[i] = std::max(Analyzer::FLOOR_DB, static_cast<float>(10 * std::log10(a2 + 1e-12)));
    }
    return out;
}

Analyzer::Analyzer(Pw &pw) : pw_(pw), hook_(std::make_unique<Hook>()), ring_(FFT_N, 0.0f), window_(FFT_N)
{
    double s2 = 0;
    for (int i = 0; i < FFT_N; i++) { // Hann
        window_[i] = static_cast<float>(0.5 - 0.5 * std::cos(2 * 3.14159265358979323846 * i / (FFT_N - 1)));
        s1_ += window_[i];
        s2 += window_[i] * window_[i];
    }
    enbw_ = FFT_N * s2 / (s1_ * s1_);
}

Analyzer::~Analyzer()
{
    Pw::Lock l(pw_);
    setTarget("");
}

void Analyzer::setTarget(const std::string &sink)
{
    uint32_t id = sink.empty() ? 0 : pw_.nodeId(sink).value_or(0);
    if (sink == target_ && id == targetId_ && (stream_ || id == 0))
        return;
    target_ = sink;
    targetId_ = id;
    if (stream_) {
        spa_hook_remove(&hook_->listener);
        pw_stream_destroy(stream_);
        stream_ = nullptr;
    }
    if (id == 0 || !pw_.core())
        return;

    pw_properties *props = pw_properties_new(
        PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Monitor", PW_KEY_MEDIA_ROLE, "DSP",
        PW_KEY_NODE_NAME, "qseq31.analisador", PW_KEY_NODE_DESCRIPTION, "Analisador do EQ 31",
        PW_KEY_STREAM_CAPTURE_SINK, "true", PW_KEY_TARGET_OBJECT, sink.c_str(),
        // passivo: não mantém o grafo acordado; sem religar sozinho (a tela escolhe o alvo)
        "node.passive", "true", "node.dont-reconnect", "true", nullptr);
    stream_ = pw_stream_new(pw_.core(), "qseq31-analisador", props);
    if (!stream_)
        return;
    pw_stream_add_listener(stream_, &hook_->listener, &streamEvents, this);

    uint8_t buf[1024];
    spa_pod_builder b;
    spa_pod_builder_init(&b, buf, sizeof buf);
    spa_audio_info_raw info{};
    info.format = SPA_AUDIO_FORMAT_F32;
    info.rate = static_cast<uint32_t>(RATE);
    info.channels = 2;
    info.position[0] = SPA_AUDIO_CHANNEL_FL;
    info.position[1] = SPA_AUDIO_CHANNEL_FR;
    const spa_pod *params[1] = {spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &info)};
    pw_stream_connect(stream_, PW_DIRECTION_INPUT, PW_ID_ANY,
                      static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS),
                      params, 1);
}

void Analyzer::onProcess()
{
    pw_buffer *b = pw_stream_dequeue_buffer(stream_);
    if (!b)
        return;
    spa_data &d = b->buffer->datas[0];
    if (d.data && d.chunk && d.chunk->size > 0) {
        auto *s = reinterpret_cast<const float *>(static_cast<const uint8_t *>(d.data) + d.chunk->offset);
        uint32_t frames = d.chunk->size / (2 * sizeof(float));
        std::lock_guard lk(m_);
        for (uint32_t i = 0; i < frames; i++) {
            ring_[pos_] = 0.5f * (s[2 * i] + s[2 * i + 1]);
            pos_ = (pos_ + 1) % ring_.size();
        }
        lastNs_ = nowNs();
    }
    pw_stream_queue_buffer(stream_, b);
}

bool Analyzer::receiving() const { return nowNs() - lastNs_.load() < 300'000'000; }

Analyzer::Levels Analyzer::levels()
{
    Levels out;
    out.fill(FLOOR_DB);
    if (!receiving())
        return out;
    std::vector<float> re(FFT_N), im(FFT_N, 0.0f);
    {
        std::lock_guard lk(m_);
        for (int i = 0; i < FFT_N; i++)
            re[i] = ring_[(pos_ + i) % ring_.size()] * window_[i];
    }
    fft(re, im);
    std::vector<float> power(FFT_N / 2 + 1);
    for (int k = 0; k <= FFT_N / 2; k++)
        power[k] = re[k] * re[k] + im[k] * im[k];
    return bandLevels(power, RATE, s1_, enbw_);
}

} // namespace qseq
