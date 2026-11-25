#include "chain.h"

#include "fx.h"
#include "sys.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <map>
#include <sstream>
#include <unistd.h>

#include <pipewire/pipewire.h>

namespace qseq {

namespace {

constexpr const char *FALLBACK = "local_p2";

void logf(const char *fmt, const std::string &a = {})
{
    char ts[32];
    std::time_t t = std::time(nullptr);
    std::strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", std::localtime(&t));
    std::fprintf(stderr, "%s eq-tui: ", ts);
    std::fprintf(stderr, fmt, a.c_str());
    std::fputc('\n', stderr);
    std::fflush(stderr);
}

std::string num(float v)
{
    char buf[32];
    std::snprintf(buf, sizeof buf, "%g", v);
    return buf;
}

} // namespace

float autoMarginDb(const State &st)
{
    float peak = curvePeakDb(st.bands);
    return st.fx_limiter ? peak * 0.5f : peak;
}

Params paramsFor(const State &st)
{
    Params p;
    p.reserve(2 * N + 32);
    for (int i = 0; i < N; i++) {
        p.emplace_back("eqL" + std::to_string(i + 1) + ":Gain", st.bands[i]);
        p.emplace_back("eqR" + std::to_string(i + 1) + ":Gain", st.bands[i]);
    }
    // compressor: o mixer escolhe entre o sinal seco (Gain 1) e o comprimido (Gain 2)
    for (const char *c : {"L", "R"}) {
        p.emplace_back(std::string("mixC") + c + ":Gain 1", st.fx_comp ? 0.0f : 1.0f);
        p.emplace_back(std::string("mixC") + c + ":Gain 2", st.fx_comp ? 1.0f : 0.0f);
    }
    // chorus e flanger: mesmo esquema seco/processado do compressor
    const auto &ch = fx::CHORUS;
    const auto &fl = fx::FLANGER;
    for (const char *c : {"L", "R"}) {
        std::string C = c;
        bool left = C == "L";
        p.emplace_back("mixH" + C + ":Gain 1", st.fx_chorus ? 0.0f : 1.0f);
        p.emplace_back("mixH" + C + ":Gain 2", st.fx_chorus ? 1.0f : 0.0f);
        p.emplace_back("chor" + C + ":Number of voices", ch.voices);
        p.emplace_back("chor" + C + ":Delay base (ms)", ch.base);
        p.emplace_back("chor" + C + ":Voice separation (ms)", ch.separation);
        p.emplace_back("chor" + C + ":Detune (%)", ch.detune);
        p.emplace_back("chor" + C + ":LFO frequency (Hz)", left ? ch.lfoL : ch.lfoR);
        p.emplace_back("chor" + C + ":Output attenuation (dB)", ch.atten);
        p.emplace_back("mixF" + C + ":Gain 1", st.fx_flanger ? 0.0f : 1.0f);
        p.emplace_back("mixF" + C + ":Gain 2", st.fx_flanger ? fl.makeup : 0.0f);
        p.emplace_back("flan" + C + ":Delay base (ms)", fl.base);
        p.emplace_back("flan" + C + ":Max slowdown (ms)", fl.depth);
        p.emplace_back("flan" + C + ":LFO frequency (Hz)", left ? fl.lfoL : fl.lfoR);
        p.emplace_back("flan" + C + ":Feedback", fl.feedback);
    }
    const auto &rv = fx::REVERBS[fx::reverbIndex(st.reverb.c_str())];
    int width = (st.fx_spatial ? fx::SPATIAL_WIDTH : 0) + rv.width;
    p.emplace_back("spat:Width", static_cast<float>(std::min(width, fx::WIDTH_MAX)));
    // eco: todos os parâmetros vão ao vivo, para ajustes valerem sem recarregar
    const auto &e = fx::ECHO;
    for (auto [k, x] : {std::pair{"L delay (ms)", e.lDelay}, {"L level", e.lLevel}, {"C delay (ms)", e.cDelay},
                        {"C level", e.cLevel}, {"R delay (ms)", e.rDelay}, {"R level", e.rLevel},
                        {"Feedback", e.feedback}, {"High damp (%)", e.highDamp}, {"Low damp (%)", e.lowDamp},
                        {"Spread", e.spread}})
        p.emplace_back(std::string("echo:") + k, x);
    p.emplace_back("echo:Dry/Wet level", st.fx_echo ? e.wet : 0.0f);
    for (const char *c : {"L", "R"}) {
        std::string n = std::string("rev") + c;
        p.emplace_back(n + ":Reverb time", rv.time);
        p.emplace_back(n + ":Damping", rv.damping);
        p.emplace_back(n + ":Dry/wet mix", rv.mix);
    }
    auto [l, r] = balanceMults(st.balance);
    p.emplace_back("balL:Mult", l);
    p.emplace_back("balR:Mult", r);
    // Margem automática: a entrada desce pelo pico da curva do EQ (metade, com o
    // limitador ligado), para os reforços nos graves não saturarem.
    p.emplace_back("lim:Input gain (dB)", std::clamp(st.gain_db - autoMarginDb(st), -20.0f, 20.0f));
    p.emplace_back("lim:Release time (s)", fx::LIMIT_RELEASE);
    p.emplace_back("lim:Limit (dB)", st.fx_limiter ? fx::LIMIT_ON : fx::LIMIT_OFF);
    return p;
}

std::string filterArgs(const State &st, const std::string &target)
{
    // Os valores iniciais saem de paramsFor: o grafo já nasce no estado salvo.
    std::map<std::string, float> v;
    for (const auto &[k, x] : paramsFor(st))
        v[k] = x;
    auto ctl = [&](const std::string &node, const std::string &port) {
        return "\"" + port + "\" = " + num(v[node + ":" + port]);
    };
    const std::string L = fx::LADSPA;
    std::ostringstream nodes, links;
    for (const char *c : {"L", "R"}) {
        std::string C = c;
        for (int i = 0; i < N; i++) {
            int n = i + 1;
            std::string name = "eq" + C + std::to_string(n);
            nodes << "      { type = builtin name = " << name << " label = bq_peaking control = { "
                  << "\"Freq\" = " << num(FREQS[i]) << " \"Q\" = " << num(Q) << " " << ctl(name, "Gain")
                  << " } }\n";
            if (n < N)
                links << "      { output = \"" << name << ":Out\" input = \"eq" << C << n + 1 << ":In\" }\n";
        }
        // compressor com caminho seco em paralelo (uma saída pode ir a várias entradas)
        nodes << "      { type = ladspa name = comp" << C << " plugin = \"" << L
              << "dyson_compress_1403.so\" label = dysonCompress control = { \"Peak limit (dB)\" = "
              << num(fx::COMP_PEAK) << " \"Release time (s)\" = " << num(fx::COMP_RELEASE)
              << " \"Fast compression ratio\" = " << num(fx::COMP_FAST) << " \"Compression ratio\" = "
              << num(fx::COMP_RATIO) << " } }\n"
              << "      { type = builtin name = mixC" << C << " label = mixer control = { "
              << ctl("mixC" + C, "Gain 1") << " " << ctl("mixC" + C, "Gain 2") << " } }\n"
              << "      { type = ladspa name = rev" << C << " plugin = \"" << L
              << "plate_1423.so\" label = plate control = { " << ctl("rev" + C, "Reverb time") << " "
              << ctl("rev" + C, "Damping") << " " << ctl("rev" + C, "Dry/wet mix") << " } }\n"
              << "      { type = ladspa name = chor" << C << " plugin = \"" << L
              << "multivoice_chorus_1201.so\" label = multivoiceChorus control = { "
              << ctl("chor" + C, "Number of voices") << " " << ctl("chor" + C, "Delay base (ms)") << " "
              << ctl("chor" + C, "Voice separation (ms)") << " " << ctl("chor" + C, "Detune (%)") << " "
              << ctl("chor" + C, "LFO frequency (Hz)") << " " << ctl("chor" + C, "Output attenuation (dB)")
              << " } }\n"
              << "      { type = builtin name = mixH" << C << " label = mixer control = { "
              << ctl("mixH" + C, "Gain 1") << " " << ctl("mixH" + C, "Gain 2") << " } }\n"
              << "      { type = ladspa name = flan" << C << " plugin = \"" << L
              << "flanger_1191.so\" label = flanger control = { " << ctl("flan" + C, "Delay base (ms)") << " "
              << ctl("flan" + C, "Max slowdown (ms)") << " " << ctl("flan" + C, "LFO frequency (Hz)") << " "
              << ctl("flan" + C, "Feedback") << " } }\n"
              << "      { type = builtin name = mixF" << C << " label = mixer control = { "
              << ctl("mixF" + C, "Gain 1") << " " << ctl("mixF" + C, "Gain 2") << " } }\n"
              << "      { type = builtin name = bal" << C << " label = linear control = { "
              << ctl("bal" + C, "Mult") << " } }\n";
        links << "      { output = \"eq" << C << N << ":Out\" input = \"comp" << C << ":Input\" }\n"
              << "      { output = \"eq" << C << N << ":Out\" input = \"mixC" << C << ":In 1\" }\n"
              << "      { output = \"comp" << C << ":Output\" input = \"mixC" << C << ":In 2\" }\n"
              // compressor → chorus (seco em paralelo) → flanger (seco em paralelo)
              << "      { output = \"mixC" << C << ":Out\" input = \"chor" << C << ":Input\" }\n"
              << "      { output = \"mixC" << C << ":Out\" input = \"mixH" << C << ":In 1\" }\n"
              << "      { output = \"chor" << C << ":Output\" input = \"mixH" << C << ":In 2\" }\n"
              << "      { output = \"mixH" << C << ":Out\" input = \"flan" << C << ":Input\" }\n"
              << "      { output = \"mixH" << C << ":Out\" input = \"mixF" << C << ":In 1\" }\n"
              << "      { output = \"flan" << C << ":Output\" input = \"mixF" << C << ":In 2\" }\n";
    }
    nodes << "      { type = ladspa name = spat plugin = \"" << L
          << "matrix_spatialiser_1422.so\" label = matrixSpatialiser control = { " << ctl("spat", "Width")
          << " } }\n"
          << "      { type = ladspa name = echo plugin = \"" << L
          << "lcr_delay_1436.so\" label = lcrDelay control = { " << ctl("echo", "L delay (ms)") << " "
          << ctl("echo", "L level") << " " << ctl("echo", "C delay (ms)") << " " << ctl("echo", "C level") << " "
          << ctl("echo", "R delay (ms)") << " " << ctl("echo", "R level") << " " << ctl("echo", "Feedback") << " "
          << ctl("echo", "High damp (%)") << " " << ctl("echo", "Low damp (%)") << " " << ctl("echo", "Spread")
          << " " << ctl("echo", "Dry/Wet level") << " } }\n"
          << "      { type = ladspa name = lim plugin = \"" << L
          << "fast_lookahead_limiter_1913.so\" label = fastLookaheadLimiter control = { "
          << ctl("lim", "Input gain (dB)") << " " << ctl("lim", "Limit (dB)") << " "
          << ctl("lim", "Release time (s)") << " } }\n";
    links << "      { output = \"mixFL:Out\" input = \"spat:Input L\" }\n"
          << "      { output = \"mixFR:Out\" input = \"spat:Input R\" }\n"
          << "      { output = \"spat:Output L\" input = \"echo:L input\" }\n"
          << "      { output = \"spat:Output R\" input = \"echo:R input\" }\n"
          << "      { output = \"echo:L output\" input = \"revL:Input\" }\n"
          << "      { output = \"echo:R output\" input = \"revR:Input\" }\n"
          << "      { output = \"revL:Left output\" input = \"balL:In\" }\n"
          << "      { output = \"revR:Right output\" input = \"balR:In\" }\n"
          << "      { output = \"balL:Out\" input = \"lim:Input 1\" }\n"
          << "      { output = \"balR:Out\" input = \"lim:Input 2\" }\n";

    std::ostringstream o;
    o << "{\n"
      << "  node.description = \"EQ 31 bandas\"\n"
      << "  media.name = \"EQ 31 bandas\"\n"
      << "  filter.graph = {\n"
      << "    nodes = [\n" << nodes.str() << "    ]\n"
      << "    links = [\n" << links.str() << "    ]\n"
      << "    inputs = [ \"eqL1:In\" \"eqR1:In\" ]\n"
      << "    outputs = [ \"lim:Output 1\" \"lim:Output 2\" ]\n"
      << "  }\n"
      << "  audio.channels = 2\n"
      << "  audio.position = [ FL FR ]\n"
      << "  capture.props = { node.name = \"" << SINK << "\" media.class = \"Audio/Sink\" }\n"
      << "  playback.props = { node.name = \"" << SAIDA << "\" node.passive = true target.object = \""
      << target << "\" }\n"
      << "}\n";
    return o.str();
}

std::string vigiarPidFile() { return runtimeDir() + "/vigiar.pid"; }
std::string vigiarLogFile() { return runtimeDir() + "/vigiar.log"; }
pid_t vigiarPid() { return sys::alivePid(vigiarPidFile()); }

bool setEnabled(bool on, std::string &msg)
{
    State st = State::load();
    st.enabled = on;
    if (!st.save(&msg))
        return false;
    if (pid_t pid = vigiarPid()) {
        kill(pid, SIGHUP);
        msg = on ? "ligando o EQ 31…" : "desligando o EQ 31…";
        return true;
    }
    if (!on) {
        msg = "EQ 31 desligado";
        return true;
    }
    std::string err;
    if (!sys::mkdirs(runtimeDir(), err)) {
        msg = err;
        return false;
    }
    std::vector<std::string> argv = {"daemon", "-r", "-P", runtimeDir() + "/vigiar.sup.pid",
                                     "-o", vigiarLogFile()};
    if (sys::run({"rtprio", "10", "true"}) == 0) {
        argv.push_back("rtprio");
        argv.push_back("10");
    }
    argv.push_back(sys::selfPath());
    argv.push_back("vigiar");
    if (sys::run(argv) != 0) {
        msg = "daemon(8) falhou ao iniciar o vigiar";
        return false;
    }
    msg = "vigiar iniciado; ligando o EQ 31…";
    return true;
}

// ── vigiar ──────────────────────────────────────────────────────────────

namespace {

class Daemon {
public:
    explicit Daemon(Pw &pw) : pw_(pw) {}

    // Tudo abaixo roda com a trava do Pw presa.
    void init()
    {
        timer_ = pw_loop_add_timer(pw_.loop(), &Daemon::onTimerCb, this);
        unloadTimer_ = pw_loop_add_timer(pw_.loop(), &Daemon::onUnloadCb, this);
        pw_.setOnChange([this] { onChange(); });
    }

    void reload()
    {
        st_ = State::load();
        if (stopping_)
            return; // onUnload decide depois de descarregar
        if (st_.enabled && !mod_)
            start();
        else if (!st_.enabled && mod_)
            stop();
        else if (mod_)
            pw_.setParams(SINK, paramsFor(st_));
    }

    /** Desfaz a ligação (saída padrão e streams) sem mexer no estado salvo. */
    void detach()
    {
        if (!mod_)
            return;
        std::string up = pw_.linkedSink(SAIDA);
        if (up.empty() || up == SINK || !pw_.hasNode(up))
            up = (!st_.upstream.empty() && pw_.hasNode(st_.upstream)) ? st_.upstream : FALLBACK;
        if (!pw_.hasNode(up))
            return;
        if (auto v = pw_.volume(SINK))
            pw_.setVolume(up, *v);
        pw_.setDefaultSink(up);
        for (uint32_t id : pw_.streamsInto(SINK, ""))
            pw_.moveStream(id, up);
        logf("EQ 31 desligado; saída volta para %s", up);
    }

    void unload()
    {
        pw_.unloadModule(mod_);
        mod_ = nullptr;
        attachPending_ = false;
    }

    void finish()
    {
        pw_.setOnChange(nullptr);
        if (timer_)
            pw_loop_destroy_source(pw_.loop(), timer_);
        if (unloadTimer_)
            pw_loop_destroy_source(pw_.loop(), unloadTimer_);
        timer_ = unloadTimer_ = nullptr;
    }

private:
    void start()
    {
        std::string top = pw_.defaultSink();
        if (top.empty() || top == SINK || !pw_.hasNode(top))
            top = (!st_.upstream.empty() && pw_.hasNode(st_.upstream)) ? st_.upstream : FALLBACK;
        if (pw_.hasNode(SINK)) {
            logf("já existe um nó %s (outro filtro rodando?); não vou subir outro", SINK);
            return;
        }
        std::string err;
        mod_ = pw_.loadModule("libpipewire-module-filter-chain", filterArgs(st_, top), err);
        if (!mod_) {
            logf("não consegui carregar o filtro: %s", err);
            return;
        }
        attachPending_ = true;
        attachTarget_ = top;
        logf("filtro carregado; vai despejar em %s", top);
    }

    void stop()
    {
        detach();
        // Dá tempo aos streams de mudarem de sink antes de remover o filtro.
        stopping_ = true;
        arm(unloadTimer_, 300);
    }

    void attach(const std::string &top)
    {
        if (top == SINK || !pw_.hasNode(top))
            return;
        auto v = pw_.volume(top);
        if (v)
            pw_.setVolume(SINK, *v);
        if (pw_.linkedSink(SAIDA) != top)
            if (auto id = pw_.nodeId(SAIDA))
                pw_.moveStream(*id, top);
        pw_.setDefaultSink(SINK);
        for (uint32_t id : pw_.streamsInto(top, SAIDA))
            pw_.moveStream(id, SINK);
        if (v)
            pw_.setVolume(top, 1.0f);
        st_.upstream = top;
        State disk = State::load();
        disk.upstream = top;
        disk.save();
        logf("EQ 31 no topo, sobre %s", top);
    }

    void onChange()
    {
        if (!pw_.connected()) {
            kill(getpid(), SIGUSR1); // o laço principal encerra; o daemon(8) reinicia
            return;
        }
        if (!mod_ || stopping_)
            return;
        if (attachPending_) {
            if (pw_.hasNode(SINK) && pw_.hasNode(SAIDA)) {
                attachPending_ = false;
                attach(attachTarget_);
            }
            return;
        }
        std::string def = pw_.defaultSink();
        if (def.empty() || def == SINK) {
            pending_.clear();
            // A saída do EQ tem de estar no topo da cadeia. Na remontagem do
            // Quickshell, o pedido de mudança pode chegar quando o sink novo
            // ainda não aceita links, e o WirePlumber cai num sink cru.
            if (linkWrong() && !fixArmed_) {
                fixArmed_ = true;
                arm(timer_, 1000);
            }
            return;
        }
        if (def != pending_) {
            pending_ = def;
            arm(timer_, 1000); // espera a rampa do Quickshell terminar
        }
    }

    bool linkWrong() const
    {
        const std::string &up = st_.upstream;
        if (up.empty() || up == SINK || !pw_.hasNode(up) || !pw_.hasNode(SAIDA))
            return false;
        std::string cur = pw_.linkedSink(SAIDA);
        return !cur.empty() && cur != up;
    }

    void onTimer()
    {
        bool fix = fixArmed_;
        fixArmed_ = false;
        if (!mod_ || stopping_ || attachPending_)
            return;
        std::string def = pw_.defaultSink();
        if (!def.empty() && def != SINK && def == pending_) {
            attach(def);
        } else if (fix && def == SINK && linkWrong()) {
            std::string cur = pw_.linkedSink(SAIDA);
            if (auto id = pw_.nodeId(SAIDA))
                pw_.moveStream(*id, st_.upstream);
            logf("%s", "saída do EQ estava em " + cur + "; religada em " + st_.upstream);
        }
        pending_.clear();
    }

    void onUnload()
    {
        unload();
        stopping_ = false;
        if (st_.enabled) // religado enquanto descarregava
            start();
    }

    void arm(spa_source *t, int ms)
    {
        timespec v{ms / 1000, (ms % 1000) * 1000000L};
        timespec i{0, 0};
        pw_loop_update_timer(pw_.loop(), t, &v, &i, false);
    }

    static void onTimerCb(void *d, uint64_t) { static_cast<Daemon *>(d)->onTimer(); }
    static void onUnloadCb(void *d, uint64_t) { static_cast<Daemon *>(d)->onUnload(); }

    Pw &pw_;
    State st_;
    pw_impl_module *mod_ = nullptr;
    bool attachPending_ = false;
    bool stopping_ = false;
    bool fixArmed_ = false;
    std::string attachTarget_;
    std::string pending_;
    spa_source *timer_ = nullptr;
    spa_source *unloadTimer_ = nullptr;
};

} // namespace

int runVigiar(Pw &pw, const sigset_t &set)
{
    // Outro vigiar ativo: fica de reserva (sair faria o daemon -r reiniciar em laço).
    if (vigiarPid()) {
        logf("já existe um vigiar rodando; aguardando ele sair%s", "");
        while (vigiarPid()) {
            timespec ts{0, 0};
            int sig = 0;
            ts.tv_sec = 2;
            if (sigtimedwait(&set, nullptr, &ts) > 0) {
                (void)sig;
                return 0;
            }
        }
    }
    std::string err;
    if (!sys::mkdirs(runtimeDir(), err) ||
        !sys::writeFileAtomic(vigiarPidFile(), std::to_string(getpid()) + "\n", err)) {
        std::fprintf(stderr, "eq-tui: %s\n", err.c_str());
        return 1;
    }
    logf("vigiar iniciado%s", "");
    Daemon d(pw);
    {
        Pw::Lock l(pw);
        d.init();
        d.reload();
    }
    int rc = 0;
    for (;;) {
        int sig = 0;
        if (sigwait(&set, &sig) != 0)
            continue;
        if (sig == SIGHUP) {
            Pw::Lock l(pw);
            d.reload();
            continue;
        }
        if (sig == SIGUSR1) {
            logf("conexão com o PipeWire perdida%s", "");
            rc = 1;
        }
        break;
    }
    if (pw.connected()) {
        {
            Pw::Lock l(pw);
            d.detach();
        }
        pw.roundtrip();
        usleep(200 * 1000);
        Pw::Lock l(pw);
        d.unload();
    }
    {
        Pw::Lock l(pw);
        d.finish();
    }
    unlink(vigiarPidFile().c_str());
    logf("vigiar encerrado%s", "");
    return rc;
}

} // namespace qseq
