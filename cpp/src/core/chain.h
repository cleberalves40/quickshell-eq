// Filtro de 31 bandas e convivência com a cadeia do Quickshell.
//
// O `eq-tui vigiar` hospeda o filter-chain dentro do próprio processo
// (pw_context_load_module) enquanto o estado diz "ligado":
//
//     apps → qseq31 → <topo da cadeia do Quickshell> → … → saída
//
// O qseq31 fica com o volume do usuário; o topo anterior vai a 100%. Quando
// outro programa muda a saída padrão (o Quickshell remontando a cadeia), o
// evento do metadado dispara a recolocação depois de 1 s de estabilidade.
//
// A tela e os subcomandos só gravam o state.json e mandam SIGHUP ao vigiar.
#ifndef QSEQ_CHAIN_H
#define QSEQ_CHAIN_H

#include "pw.h"
#include "state.h"

#include <csignal>
#include <string>
#include <sys/types.h>

namespace qseq {

Params paramsFor(const State &st);
/** Margem automática (dB a descer na entrada do limitador): o pico inteiro da curva do EQ,
 *  ou metade dele com o limitador ligado, que segura o que passar de 0 dB. */
float autoMarginDb(const State &st);
/** Argumentos (SPA-JSON) do libpipewire-module-filter-chain. */
std::string filterArgs(const State &st, const std::string &target);

std::string vigiarPidFile();
std::string vigiarLogFile();
pid_t vigiarPid();

/** Grava enabled no estado e avisa o vigiar (sobe um, se preciso). */
bool setEnabled(bool on, std::string &msg);

/** Laço do vigiar; `set` são os sinais já bloqueados pelo chamador. */
int runVigiar(Pw &pw, const sigset_t &set);

} // namespace qseq

#endif
