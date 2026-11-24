#include "ui.h"

#include "bands.h"
#include "chain.h"
#include "fx.h"
#include "pw.h"
#include "state.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>
#include <ftxui/screen/terminal.hpp>

namespace qseq {

using namespace ftxui;
using Clock = std::chrono::steady_clock;

namespace {

const Color POS = Color::Cyan;
const Color NEG = Color::RedLight;
const Color OK = Color::GreenLight;
const Color DIM = Color::GrayDark;

constexpr float VOL_STEP = 0.05f;

enum class Focus { Volume, Gain, Balance, Effects, Bands };
constexpr int NFOCUS = 5;

const char *FX_NAMES[] = {"Compressor", "Limitador", "3D", "Eco", "Reverb"};
constexpr int NFX = 5;
enum class Mode { Normal, Presets, SaveName };

std::string repeat(const std::string &s, int n)
{
    std::string o;
    for (int i = 0; i < n; i++)
        o += s;
    return o;
}

std::string fmt(const char *f, double v)
{
    char buf[48];
    std::snprintf(buf, sizeof buf, f, v);
    return buf;
}

class App {
public:
    explicit App(Pw &pw) : pw_(pw), st_(State::load()) { refreshSlow(); }

    Element render(int w, int h);
    bool onEvent(const Event &e);
    void refreshSlow()
    {
        vigiar_ = vigiarPid() != 0;
        st_.enabled = State::load().enabled;
        if (!msg_.empty() && Clock::now() - msgAt_ > std::chrono::seconds(6))
            msg_.clear();
    }
    bool quit = false;

private:
    struct Snap {
        std::string def, upstream;
        float vol = 0;
        bool muted = false;
        bool running = false;
        std::string popupFx; // efeitos do popup do Quickshell ativos em paralelo
    };

    void snapshot();
    void say(const std::string &m, bool err = false)
    {
        msg_ = m;
        msgErr_ = err;
        msgAt_ = Clock::now();
    }
    void touch();
    void band(float d);
    void gain(float d);
    void balance(int d);
    void volume(float d);
    void toggleMute();
    void toggleEq();
    void toggleFx(int i);
    bool fxOn(int i) const;
    bool keyEffects(const Event &e);
    Element effects();
    void zero();
    void flat();
    void loadPreset(const std::string &name);
    void savePreset(const std::string &name);
    bool keyNormal(const Event &e);
    bool keyBands(const Event &e);
    bool keySlider(const Event &e);

    Element controls(int width);
    Element chart(int width, int height);
    Element info();
    Element help(int width);
    Element modal();

    Pw &pw_;
    State st_;
    Snap s_;
    Focus focus_ = Focus::Bands;
    int sel_ = 17; // 1 kHz
    int fxSel_ = 0;
    Mode mode_ = Mode::Normal;
    std::vector<std::string> presets_;
    int presetSel_ = 0;
    std::string name_;
    std::string msg_;
    bool msgErr_ = false;
    Clock::time_point msgAt_{};
    bool vigiar_ = false;
};

void App::snapshot()
{
    Pw::Lock l(pw_);
    s_.def = pw_.defaultSink();
    s_.running = pw_.hasNode(SINK);
    s_.upstream = s_.running ? pw_.linkedSink(SAIDA) : "";
    s_.vol = pw_.volume(s_.def).value_or(0.0f);
    s_.muted = pw_.muted(s_.def);
    // Os efeitos do popup aparecem como sinks fx_*; com o EQ 31 eles se somam aos daqui.
    static const std::pair<const char *, const char *> POPUP[] = {
        {"fx_comp", "compressor"}, {"fx_lim", "limitador"}, {"fx_3d", "3D"},
        {"fx_echo", "eco"}, {"fx_rev", "reverb"}};
    s_.popupFx.clear();
    for (const auto &[node, name] : POPUP)
        if (pw_.hasNode(node))
            s_.popupFx += (s_.popupFx.empty() ? "" : ", ") + std::string(name);
}

// ── ações ───────────────────────────────────────────────────────────────

void App::touch()
{
    {
        Pw::Lock l(pw_);
        if (pw_.hasNode(SINK))
            pw_.setParams(SINK, paramsFor(st_));
    }
    // upstream e enabled pertencem ao vigiar e aos subcomandos
    State disk = State::load();
    st_.upstream = disk.upstream;
    st_.enabled = disk.enabled;
    std::string err;
    if (!st_.save(&err))
        say(err, true);
}

void App::band(float d)
{
    st_.bands[sel_] = clampBand(st_.bands[sel_] + d);
    st_.modified = true;
    touch();
}

void App::gain(float d)
{
    st_.gain_db = std::clamp(st_.gain_db + d, -GAIN_MAX, GAIN_MAX);
    touch();
}

void App::balance(int d)
{
    st_.balance = std::clamp(st_.balance + d, -BAL_MAX, BAL_MAX);
    touch();
}

void App::volume(float d)
{
    float v = std::round(std::clamp(s_.vol + d, 0.0f, 1.0f) * 100.0f) / 100.0f;
    Pw::Lock l(pw_);
    if (!pw_.setVolume(s_.def, v))
        say("não consegui mudar o volume de " + s_.def, true);
}

void App::toggleMute()
{
    Pw::Lock l(pw_);
    pw_.setMute(s_.def, !s_.muted);
}

void App::toggleEq()
{
    std::string m;
    bool on = !st_.enabled;
    bool ok = setEnabled(on, m);
    if (ok)
        st_.enabled = on;
    say(m, !ok);
}

bool App::fxOn(int i) const
{
    switch (i) {
    case 0: return st_.fx_comp;
    case 1: return st_.fx_limiter;
    case 2: return st_.fx_spatial;
    case 3: return st_.fx_echo;
    default: return st_.reverb != "Off";
    }
}

void App::toggleFx(int i)
{
    switch (i) {
    case 0: st_.fx_comp = !st_.fx_comp; break;
    case 1: st_.fx_limiter = !st_.fx_limiter; break;
    case 2: st_.fx_spatial = !st_.fx_spatial; break;
    case 3: st_.fx_echo = !st_.fx_echo; break;
    default: {
        int r = (fx::reverbIndex(st_.reverb.c_str()) + 1) % static_cast<int>(fx::REVERBS.size());
        st_.reverb = fx::REVERBS[r].name;
        touch();
        say(std::string("reverb: ") + st_.reverb);
        return;
    }
    }
    touch();
    say(std::string(FX_NAMES[i]) + (fxOn(i) ? " ligado" : " desligado"));
}

void App::zero()
{
    switch (focus_) {
    case Focus::Bands: band(-st_.bands[sel_]); break;
    case Focus::Gain: gain(-st_.gain_db); break;
    case Focus::Balance: balance(-st_.balance); break;
    case Focus::Effects:
        st_.fx_comp = st_.fx_spatial = st_.fx_echo = false;
        st_.fx_limiter = true;
        st_.reverb = "Off";
        touch();
        say("efeitos desligados (limitador mantido)");
        break;
    case Focus::Volume: break;
    }
}

void App::flat()
{
    st_.bands.assign(N, 0.0f);
    st_.preset = "flat";
    st_.modified = false;
    touch();
    say("todas as bandas em 0 dB");
}

void App::loadPreset(const std::string &name)
{
    std::vector<float> b;
    std::string err;
    if (!qseq::loadPreset(name, b, err))
        return say(err, true);
    st_.bands = b;
    st_.preset = name;
    st_.modified = false;
    touch();
    say("perfil " + name + " carregado");
}

void App::savePreset(const std::string &name)
{
    std::string err;
    if (!qseq::savePreset(name, st_.bands, err))
        return say(err, true);
    st_.preset = name;
    st_.modified = false;
    touch();
    say("perfil " + name + " salvo em " + configDir() + "/presets");
}

// ── teclado ─────────────────────────────────────────────────────────────

bool isChar(const Event &e, const char *c) { return e.is_character() && e.character() == c; }

bool App::onEvent(const Event &e)
{
    if (e == Event::Custom) {
        refreshSlow();
        return true;
    }
    if (mode_ == Mode::Presets) {
        if (e == Event::ArrowUp || isChar(e, "k"))
            presetSel_ = std::max(0, presetSel_ - 1);
        else if (e == Event::ArrowDown || isChar(e, "j"))
            presetSel_ = std::min(static_cast<int>(presets_.size()) - 1, presetSel_ + 1);
        else if (e == Event::Return) {
            mode_ = Mode::Normal;
            if (presetSel_ >= 0 && presetSel_ < static_cast<int>(presets_.size()))
                loadPreset(presets_[presetSel_]);
        } else if (e == Event::Escape || isChar(e, "q") || isChar(e, "p"))
            mode_ = Mode::Normal;
        return true;
    }
    if (mode_ == Mode::SaveName) {
        if (e == Event::Return) {
            mode_ = Mode::Normal;
            savePreset(name_);
        } else if (e == Event::Escape) {
            mode_ = Mode::Normal;
        } else if (e == Event::Backspace) {
            if (!name_.empty())
                name_.pop_back();
        } else if (e.is_character() && e.character().size() == 1 && name_.size() < 40) {
            name_ += e.character();
        }
        return true;
    }
    return keyNormal(e);
}

bool App::keyNormal(const Event &e)
{
    if (isChar(e, "q") || e == Event::Escape) {
        quit = true;
        return true;
    }
    if (e == Event::Tab) {
        focus_ = static_cast<Focus>((static_cast<int>(focus_) + 1) % NFOCUS);
        return true;
    }
    if (e == Event::TabReverse) {
        focus_ = static_cast<Focus>((static_cast<int>(focus_) + NFOCUS - 1) % NFOCUS);
        return true;
    }
    if (isChar(e, "+") || isChar(e, "=")) {
        volume(VOL_STEP);
        return true;
    }
    if (isChar(e, "-")) {
        volume(-VOL_STEP);
        return true;
    }
    if (isChar(e, "m")) {
        toggleMute();
        return true;
    }
    if (isChar(e, "e")) {
        toggleEq();
        return true;
    }
    if (isChar(e, "0")) {
        zero();
        return true;
    }
    static const char *FX_KEYS[] = {"c", "i", "d", "x", "v"};
    for (int i = 0; i < NFX; i++) {
        if (isChar(e, FX_KEYS[i])) {
            toggleFx(i);
            return true;
        }
    }
    if (isChar(e, "r")) {
        flat();
        return true;
    }
    if (isChar(e, "p")) {
        presets_ = listPresets();
        if (presets_.empty()) {
            say("nenhum perfil encontrado", true);
        } else {
            auto it = std::find(presets_.begin(), presets_.end(), st_.preset);
            presetSel_ = it == presets_.end() ? 0 : static_cast<int>(it - presets_.begin());
            mode_ = Mode::Presets;
        }
        return true;
    }
    if (isChar(e, "s")) {
        name_ = st_.preset == "flat" ? "" : st_.preset;
        mode_ = Mode::SaveName;
        return true;
    }
    if (focus_ == Focus::Effects)
        return keyEffects(e);
    return focus_ == Focus::Bands ? keyBands(e) : keySlider(e);
}

bool App::keyBands(const Event &e)
{
    if (e == Event::ArrowLeft || isChar(e, "h"))
        sel_ = std::max(0, sel_ - 1);
    else if (e == Event::ArrowRight || isChar(e, "l"))
        sel_ = std::min(N - 1, sel_ + 1);
    else if (e == Event::Home)
        sel_ = 0;
    else if (e == Event::End)
        sel_ = N - 1;
    else if (e == Event::ArrowUp || isChar(e, "k"))
        band(BAND_STEP);
    else if (e == Event::ArrowDown || isChar(e, "j"))
        band(-BAND_STEP);
    else if (e == Event::PageUp || isChar(e, "K"))
        band(3.0f);
    else if (e == Event::PageDown || isChar(e, "J"))
        band(-3.0f);
    else
        return false;
    return true;
}

bool App::keyEffects(const Event &e)
{
    if (e == Event::ArrowLeft || isChar(e, "h"))
        fxSel_ = std::max(0, fxSel_ - 1);
    else if (e == Event::ArrowRight || isChar(e, "l"))
        fxSel_ = std::min(NFX - 1, fxSel_ + 1);
    else if (e == Event::Return || isChar(e, " "))
        toggleFx(fxSel_);
    else if (e == Event::ArrowUp || isChar(e, "k"))
        focus_ = Focus::Balance;
    else if (e == Event::ArrowDown || isChar(e, "j"))
        focus_ = Focus::Bands;
    else
        return false;
    return true;
}

bool App::keySlider(const Event &e)
{
    int dir = 0;
    if (e == Event::ArrowLeft || isChar(e, "h"))
        dir = -1;
    else if (e == Event::ArrowRight || isChar(e, "l"))
        dir = 1;
    else if (e == Event::ArrowUp || isChar(e, "k")) {
        focus_ = static_cast<Focus>((static_cast<int>(focus_) + NFOCUS - 1) % NFOCUS);
        return true;
    } else if (e == Event::ArrowDown || isChar(e, "j")) {
        focus_ = static_cast<Focus>((static_cast<int>(focus_) + 1) % NFOCUS);
        return true;
    } else {
        return false;
    }
    switch (focus_) {
    case Focus::Volume: volume(dir * VOL_STEP); break;
    case Focus::Gain: gain(static_cast<float>(dir)); break;
    case Focus::Balance: balance(dir); break;
    case Focus::Effects:
    case Focus::Bands: break;
    }
    return true;
}

// ── desenho ─────────────────────────────────────────────────────────────

Element labelEl(const std::string &s, bool focused)
{
    auto t = text(s) | bold;
    return focused ? t | color(Color::Black) | bgcolor(POS) : t;
}

Element bipolar(const std::string &name, bool focused, float pos, int w, const std::string &value,
                bool changed)
{
    int mid = w / 2;
    int knob = static_cast<int>(std::lround((std::clamp(pos, -1.0f, 1.0f) + 1.0f) / 2.0f * (w - 1)));
    std::string s;
    for (int i = 0; i < w; i++)
        s += i == knob ? "●" : i == mid ? "┼" : "·";
    return hbox({labelEl(name, focused), text(" "), text(s) | color(changed ? POS : DIM), text(value)});
}

Element App::controls(int width)
{
    int w = std::max(10, width - 10 - 20); // à direita: valor + margem automática
    int filled = static_cast<int>(std::lround(std::clamp(s_.vol, 0.0f, 1.0f) * w));
    Element vol = hbox({
        labelEl(" Volume  ", focus_ == Focus::Volume),
        text(" "),
        text(repeat("█", filled)) | color(s_.muted ? DIM : OK),
        text(repeat("░", w - filled)) | color(DIM),
        text(fmt(" %3.0f%%", s_.vol * 100.0)),
        s_.muted ? text(" mudo") | color(Color::Red) | bold : text(""),
    });
    std::string balTxt = st_.balance == 0  ? " centro"
                         : st_.balance < 0 ? " L " + std::to_string(-st_.balance)
                                           : " R " + std::to_string(st_.balance);
    return vbox({
        vol,
        bipolar(" Ganho   ", focus_ == Focus::Gain, st_.gain_db / GAIN_MAX, w,
                fmt(" %+.0f dB", st_.gain_db) + fmt(" auto -%.1f", curvePeakDb(st_.bands)),
                st_.gain_db != 0),
        bipolar(" Balanço ", focus_ == Focus::Balance, static_cast<float>(st_.balance) / BAL_MAX, w,
                balTxt, st_.balance != 0),
        effects(),
    });
}

Element App::effects()
{
    bool focused = focus_ == Focus::Effects;
    Elements row{labelEl(" Efeitos ", focused), text(" ")};
    for (int i = 0; i < NFX; i++) {
        bool on = fxOn(i);
        std::string name = FX_NAMES[i];
        if (i == 4 && on)
            name += ": " + st_.reverb + " +3D";
        Element item = text(std::string(on ? " ● " : " ○ ") + name + " ");
        item = on ? item | color(OK) | bold : item | color(DIM);
        if (focused && i == fxSel_)
            item = item | inverted;
        row.push_back(item);
    }
    return hbox(std::move(row));
}

Element App::chart(int width, int height)
{
    constexpr int AXIS = 5;
    int h = height - 1; // última linha: rótulos
    if (h < 3 || width < AXIS + N)
        return text(" terminal pequeno demais") | color(DIM);
    if (h % 2 == 0)
        h--; // ímpar: a linha do meio é o 0 dB
    int cw = std::clamp((width - AXIS) / N, 1, 4);
    int barW = cw >= 3 ? cw - 1 : 1;
    int mid = h / 2;
    int half = h;
    bool focused = focus_ == Focus::Bands;

    Elements rows;
    for (int row = 0; row < h; row++) {
        std::string axis = row == 0       ? fmt("%+4.0f ", BAND_MAX)
                           : row == mid   ? "   0 "
                           : row == h - 1 ? fmt("%+4.0f ", -BAND_MAX)
                                          : "     ";
        Elements cells{text(axis) | color(DIM)};
        int topU = (h - 1 - row) * 2 + 1;
        int botU = (h - 1 - row) * 2;
        for (int i = 0; i < N; i++) {
            float g = st_.bands[i];
            int n = static_cast<int>(std::lround(std::fabs(g) / BAND_MAX * half));
            auto filled = [&](int u) {
                if (g > 0)
                    return u >= half && u < half + n;
                if (g < 0)
                    return u < half && u + n >= half;
                return false;
            };
            bool t = filled(topU), b = filled(botU);
            const char *ch = t && b ? "█" : t ? "▀" : b ? "▄" : row == mid ? "─" : " ";
            bool sel = i == sel_;
            Color c = (t || b) ? (sel ? Color::White : (g > 0 ? POS : NEG)) : DIM;
            Element cell = text(repeat(ch, barW)) | color(c);
            if (sel) {
                cell = cell | bold;
                if (focused && !(t || b) && row != mid)
                    cell = cell | bgcolor(Color::Palette256(236));
            }
            cells.push_back(cell);
            if (cw > barW)
                cells.push_back(text(repeat(row == mid ? "─" : " ", cw - barW)) | color(DIM));
        }
        rows.push_back(hbox(std::move(cells)));
    }

    // rótulos de frequência sem sobreposição; o da banda escolhida sempre aparece
    std::string selTxt = label(FREQS[sel_]);
    int selX = AXIS + sel_ * cw;
    Elements lab{text(std::string(AXIS, ' '))};
    int cur = AXIS;      // fim do que já foi escrito
    int nextFree = AXIS; // um espaço depois do último rótulo
    for (int i = 0; i < N; i++) {
        int bx = AXIS + i * cw;
        std::string t = label(FREQS[i]);
        int len = static_cast<int>(t.size());
        bool collides = i != sel_ && bx < selX + static_cast<int>(selTxt.size()) + 1 && selX < bx + len + 1;
        if ((bx >= nextFree && !collides) || (i == sel_ && bx >= cur)) {
            if (bx > cur)
                lab.push_back(text(std::string(bx - cur, ' ')));
            lab.push_back(i == sel_ ? text(t) | color(POS) | bold : text(t) | color(DIM));
            cur = bx + len;
            nextFree = cur + 1;
        }
    }
    rows.push_back(hbox(std::move(lab)));
    return vbox(std::move(rows));
}

Element App::info()
{
    if (!msg_.empty())
        return text(" " + msg_) | color(msgErr_ ? Color::Red : Color::Yellow);
    if (s_.running && !s_.popupFx.empty())
        return text(" popup do Quickshell também com " + s_.popupFx + ": desligue lá para não dobrar") |
               color(Color::Yellow);
    float f = FREQS[sel_];
    std::string unit = f >= 1000 ? fmt("%g kHz", f / 1000.0) : fmt("%g Hz", f);
    return hbox({
        text(" banda "),
        text(unit) | color(POS) | bold,
        text(fmt(": %+.1f dB", st_.bands[sel_])),
        text("   saída padrão: " + s_.def) | color(DIM),
        text(vigiar_ ? "   vigiar ativo" : "   vigiar parado") | color(DIM),
    });
}

Element App::help(int width)
{
    bool wide = width >= 112;
    const char *t;
    if (focus_ == Focus::Bands)
        t = wide ? " ←→ banda  ↑↓ ±0.5 dB  PgUp/PgDn ±3  0 zerar  r plano  Tab controles  +/- vol  m mudo  e liga  p perfis  s salvar  c/i/d/x/v efeitos  q sair"
                 : " ←→ banda ↑↓ dB 0 zera Tab ctrl +- vol m mudo e EQ p perfis s salva c/i/d/x/v fx q sai";
    else if (focus_ == Focus::Effects)
        t = wide ? " ←→ efeito  Enter/espaço liga/desliga (reverb: próximo modo)  0 desliga todos  c compressor  i limitador  d 3D  x eco  v reverb  q sair"
                 : " ←→ efeito  Enter liga  0 desliga todos  c comp i lim d 3D x eco v reverb  q sai";
    else
        t = wide ? " ←→ ajustar  ↑↓/Tab controle  0 zerar  +/- vol  m mudo  e liga/desliga EQ  p perfis  s salvar  c/i/d/x/v efeitos  q sair"
                 : " ←→ ajusta ↑↓/Tab ctrl 0 zera +- vol m mudo e EQ p perfis s salva q sai";
    return text(t) | color(DIM);
}

Element App::modal()
{
    if (mode_ == Mode::Presets) {
        Elements items;
        for (int i = 0; i < static_cast<int>(presets_.size()); i++) {
            auto t = text(" " + presets_[i] + " ");
            items.push_back(i == presetSel_ ? t | color(Color::Black) | bgcolor(POS) | focus : t);
        }
        return window(text(" perfis (Enter carrega, Esc fecha) "), vbox(std::move(items)) | vscroll_indicator | frame) |
               size(WIDTH, EQUAL, 38) | size(HEIGHT, LESS_THAN, 20) | clear_under | center;
    }
    if (mode_ == Mode::SaveName)
        return window(text(" salvar perfil como (Enter / Esc) "), text(" " + name_ + "▏")) |
               size(WIDTH, EQUAL, 46) | clear_under | center;
    return emptyElement();
}

Element App::render(int w, int h)
{
    snapshot();
    int innerW = w - 2;
    int chartH = h - 2 - 4 - 1 - 1 - 1; // bordas, controles+efeitos, separador, info, ajuda

    std::string preset = st_.preset + (st_.modified ? "*" : "");
    Element eq;
    if (s_.running && s_.def == SINK)
        eq = text("ligado → " + s_.upstream) | color(OK);
    else if (s_.running)
        eq = text("rodando, fora da saída padrão") | color(Color::Yellow);
    else if (st_.enabled && !vigiar_)
        eq = text("ligado no estado, mas o vigiar está parado") | color(Color::Red);
    else if (st_.enabled)
        eq = text("ligando…") | color(Color::Yellow);
    else
        eq = text("desligado") | color(DIM);
    Element title = hbox({text(" quickshell-eq ") | bold, text("· perfil: "), text(preset) | color(POS),
                          text(" · EQ 31: "), eq, text(" ")});

    Element main = window(title, vbox({
                                     controls(innerW),
                                     separator() | color(DIM),
                                     chart(innerW, chartH) | flex,
                                     info(),
                                     help(innerW),
                                 }));
    if (mode_ == Mode::Normal)
        return main;
    return dbox({main, modal()});
}

} // namespace

int runTui(Pw &pw)
{
    App app(pw);
    auto screen = ScreenInteractive::Fullscreen();
    pw.setOnChange([&screen] { screen.PostEvent(Event::Custom); });

    // Atualização lenta (vigiar, mensagens) mesmo sem eventos do PipeWire.
    std::atomic<bool> stop{false};
    std::mutex m;
    std::condition_variable cv;
    std::thread ticker([&] {
        std::unique_lock lk(m);
        while (!cv.wait_for(lk, std::chrono::seconds(1), [&] { return stop.load(); }))
            screen.PostEvent(Event::Custom);
    });

    auto renderer = Renderer([&] {
        auto dim = Terminal::Size();
        return app.render(dim.dimx, dim.dimy);
    });
    auto comp = CatchEvent(renderer, [&](Event e) {
        bool handled = app.onEvent(e);
        if (app.quit)
            screen.Exit();
        return handled;
    });
    screen.Loop(comp);

    pw.setOnChange(nullptr);
    {
        std::lock_guard lk(m);
        stop = true;
    }
    cv.notify_all();
    ticker.join();
    return 0;
}

int snapshotTui(Pw &pw, int w, int h)
{
    App app(pw);
    auto screen = Screen::Create(Dimension::Fixed(w), Dimension::Fixed(h));
    Render(screen, app.render(w, h));
    std::fputs(screen.ToString().c_str(), stdout);
    std::fputc('\n', stdout);
    return 0;
}

} // namespace qseq
