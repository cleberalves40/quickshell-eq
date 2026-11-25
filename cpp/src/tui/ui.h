// Tela (FTXUI). É só um cliente: o filtro vive no `eq-tui vigiar`.
#ifndef QSEQ_UI_H
#define QSEQ_UI_H

namespace qseq {

class Pw;
/** Devolve TUI_RECONNECT se a conexão com o PipeWire caiu (o chamador reconecta). */
constexpr int TUI_RECONNECT = 75;
int runTui(Pw &pw);
/** Desenha a tela uma vez em w×h e imprime (para testes). */
int snapshotTui(Pw &pw, int w, int h);

} // namespace qseq

#endif
