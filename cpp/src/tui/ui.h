// Tela (FTXUI). É só um cliente: o filtro vive no `eq-tui vigiar`.
#ifndef QSEQ_UI_H
#define QSEQ_UI_H

namespace qseq {

class Pw;
int runTui(Pw &pw);
/** Desenha a tela uma vez em w×h e imprime (para testes). */
int snapshotTui(Pw &pw, int w, int h);

} // namespace qseq

#endif
