// Paleta Nord (https://www.nordtheme.com), com os mesmos papéis do Theme.qml
// do Quickshell, para a tela combinar com a barra e o popup.
#ifndef QSEQ_THEME_H
#define QSEQ_THEME_H

#include <array>
#include <ftxui/screen/color.hpp>

namespace qseq::nord {

using ftxui::Color;

// Polar Night
inline const Color n0 = Color::RGB(0x2E, 0x34, 0x40);
inline const Color n1 = Color::RGB(0x3B, 0x42, 0x52);
inline const Color n2 = Color::RGB(0x43, 0x4C, 0x5E);
inline const Color n3 = Color::RGB(0x4C, 0x56, 0x6A);
// Snow Storm
inline const Color n4 = Color::RGB(0xD8, 0xDE, 0xE9);
inline const Color n5 = Color::RGB(0xE5, 0xE9, 0xF0);
inline const Color n6 = Color::RGB(0xEC, 0xEF, 0xF4);
// Frost
inline const Color n7 = Color::RGB(0x8F, 0xBC, 0xBB);
inline const Color n8 = Color::RGB(0x88, 0xC0, 0xD0);
inline const Color n9 = Color::RGB(0x81, 0xA1, 0xC1);
inline const Color n10 = Color::RGB(0x5E, 0x81, 0xAC);
// Aurora
inline const Color n11 = Color::RGB(0xBF, 0x61, 0x6A); // vermelho
inline const Color n12 = Color::RGB(0xD0, 0x87, 0x70); // laranja
inline const Color n13 = Color::RGB(0xEB, 0xCB, 0x8B); // amarelo
inline const Color n14 = Color::RGB(0xA3, 0xBE, 0x8C); // verde
inline const Color n15 = Color::RGB(0xB4, 0x8E, 0xAD); // roxo

// Papéis (Theme.qml)
inline const Color BG = n0;
inline const Color SURFACE = n1;
inline const Color SURFACE_HI = n2;
inline const Color FG = n6;
inline const Color TEXT = n4;
inline const Color FG_DIM = n9;
inline const Color MUTED = n3; // trilhos, grade, bordas
inline const Color ACCENT = n8;
inline const Color ACCENT_FG = n0;
inline const Color OK = n14;
inline const Color WARN = n13;
inline const Color CRIT = n11;

/** Degradê do analisador, de baixo para cima: Frost → Aurora. */
inline const std::array<Color, 8> SPECTRUM = {n10, n9, n8, n7, n14, n13, n12, n11};

} // namespace qseq::nord

#endif
