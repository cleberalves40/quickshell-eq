// eq-tui: equalizador de 31 bandas no terminal, cliente nativo do PipeWire.

#include "chain.h"
#include "pw.h"
#include "state.h"
#include "ui.h"

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>

#include <pipewire/pipewire.h>

using namespace qseq;

namespace {

const char *USAGE =
    "uso: eq-tui              abre a tela\n"
    "     eq-tui ligar        põe o EQ de 31 bandas no topo da cadeia de áudio\n"
    "     eq-tui desligar     tira o EQ e devolve a saída ao topo anterior\n"
    "     eq-tui status       mostra o estado\n"
    "     eq-tui vigiar       hospeda o filtro e o mantém no topo (autostart)\n"
    "     eq-tui --version\n";

/** Espera até `cond` valer (com a trava) ou o prazo acabar. */
template <class F> bool waitFor(Pw &pw, int ms, F cond)
{
    for (int t = 0; t < ms; t += 100) {
        pw.roundtrip();
        {
            Pw::Lock l(pw);
            if (cond())
                return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return false;
}

int status(Pw &pw)
{
    State st = State::load();
    Pw::Lock l(pw);
    bool run = pw.hasNode(SINK);
    std::printf("estado salvo: %s\n", st.enabled ? "ligado" : "desligado");
    std::printf("filtro: %s\n", run ? "rodando" : "parado");
    std::printf("saída padrão: %s\n", pw.defaultSink().c_str());
    if (run)
        std::printf("despeja em: %s\n", pw.linkedSink(SAIDA).c_str());
    if (auto v = pw.volume(pw.defaultSink()))
        std::printf("volume: %.0f%%%s\n", *v * 100, pw.muted(pw.defaultSink()) ? " (mudo)" : "");
    std::printf("vigiar: %s\n", vigiarPid() ? "ativo" : "parado");
    std::printf("perfil: %s%s  ganho: %+.0f dB  balanço: %+d\n", st.preset.c_str(),
                st.modified ? "*" : "", st.gain_db, st.balance);
    return 0;
}

int onOff(Pw &pw, bool on)
{
    std::string msg;
    if (!setEnabled(on, msg)) {
        std::fprintf(stderr, "eq-tui: %s\n", msg.c_str());
        return 1;
    }
    bool ok = on ? waitFor(pw, 6000, [&] { return pw.defaultSink() == SINK; })
                 : waitFor(pw, 6000, [&] { return !pw.hasNode(SINK); });
    Pw::Lock l(pw);
    if (!ok) {
        std::fprintf(stderr, "eq-tui: %s (veja %s)\n",
                     on ? "o EQ não chegou ao topo" : "o filtro não saiu", vigiarLogFile().c_str());
        return 1;
    }
    if (on)
        std::printf("EQ 31 ligado sobre %s\n", pw.linkedSink(SAIDA).c_str());
    else
        std::printf("EQ 31 desligado; saída padrão: %s\n", pw.defaultSink().c_str());
    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    std::string cmd = argc > 1 ? argv[1] : "";
    if (cmd == "-h" || cmd == "--help" || cmd == "ajuda") {
        std::fputs(USAGE, stdout);
        return 0;
    }
    if (cmd == "--print-args") { // depuração: argumentos do filter-chain para o estado salvo
        std::fputs(filterArgs(State::load(), argc > 2 ? argv[2] : "local_p2").c_str(), stdout);
        return 0;
    }
    if (cmd == "--version") {
        std::printf("eq-tui %s (C++, libpipewire %s)\n", EQ_TUI_VERSION, pw_get_library_version());
        return 0;
    }
    if (!cmd.empty() && cmd != "--snapshot" && cmd != "ligar" && cmd != "desligar" && cmd != "status" && cmd != "vigiar") {
        std::fprintf(stderr, "eq-tui: comando desconhecido: %s\n%s", cmd.c_str(), USAGE);
        return 2;
    }

    // O vigiar trata os sinais com sigwait: bloqueia antes de criar as threads.
    sigset_t set;
    sigemptyset(&set);
    if (cmd == "vigiar") {
        for (int s : {SIGHUP, SIGTERM, SIGINT, SIGUSR1})
            sigaddset(&set, s);
        pthread_sigmask(SIG_BLOCK, &set, nullptr);
    }

    pw_init(&argc, &argv);
    int rc;
    if (cmd.empty()) {
        // Tela: se o PipeWire reiniciar, espera ele voltar e reabre (até ~30 s).
        for (;;) {
            std::unique_ptr<Pw> pw;
            std::string err;
            for (int t = 0; t < 60; t++) {
                pw = std::make_unique<Pw>();
                if (pw->connect(err))
                    break;
                pw.reset();
                if (t == 0)
                    std::fprintf(stderr, "eq-tui: aguardando o PipeWire (%s)...\n", err.c_str());
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
            if (!pw) {
                std::fprintf(stderr, "eq-tui: %s\n", err.c_str());
                rc = 1;
                break;
            }
            rc = runTui(*pw);
            if (rc != TUI_RECONNECT)
                break;
            std::fprintf(stderr, "eq-tui: o PipeWire reiniciou; reconectando...\n");
        }
        pw_deinit();
        return rc;
    }
    {
        Pw pw;
        std::string err;
        if (!pw.connect(err)) {
            std::fprintf(stderr, "eq-tui: %s\n", err.c_str());
            rc = 1;
        } else if (cmd.empty()) {
            rc = runTui(pw);
        } else if (cmd == "--snapshot") {
            int w = argc > 2 ? std::atoi(argv[2]) : 80, h = argc > 3 ? std::atoi(argv[3]) : 24;
            rc = snapshotTui(pw, w, h);
        } else if (cmd == "status") {
            rc = status(pw);
        } else if (cmd == "vigiar") {
            rc = runVigiar(pw, set);
        } else {
            rc = onOff(pw, cmd == "ligar");
        }
    }
    pw_deinit();
    return rc;
}
