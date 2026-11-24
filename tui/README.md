# eq-tui

Equalizador gráfico de 31 bandas (ISO, 1/3 de oitava) no terminal, sobre o PipeWire.

## Como funciona

O `eq-tui` sobe um `filter-chain` próprio num processo `pipewire -c` (sob `daemon(8) -r` e,
quando possível, `rtprio 10`):

```
apps → qseq31 (31 × bq_peaking por canal → balanço → limitador -1 dB) → topo da cadeia atual
```

A cadeia do Quickshell (`fx_*` → `eq_p2` → `vol_out` → `jbl`/`local_p2`) continua como está.
Quando o EQ é ligado, o `qseq31` vira a saída padrão por cima dela, fica com o volume do
usuário, e o antigo topo vai a 100%. Se o Quickshell remontar a cadeia, a tela aberta ou o
`eq-tui vigiar` recolocam o `qseq31` no topo (cerca de 2 s depois).

| Controle | Implementação |
|---|---|
| 31 bandas, ±12 dB | `eqL/eqR<n>:Gain`, alterado ao vivo com `pw-cli set-param` |
| Ganho, ±12 dB | `lim:Input gain (dB)` (o limitador evita o clipping) |
| Balanço L/R, ±10 | `balL/balR:Mult`, 3 dB por passo; ±10 silencia o outro lado |
| Volume e mudo | `pactl` no `@DEFAULT_SINK@`, o mesmo que as teclas e o Quickshell usam |

## Uso

```sh
eq-tui              # tela
eq-tui ligar | desligar | status
eq-tui vigiar       # sem tela, para o autostart (ex.: exec-once no Hyprland)
```

Teclas: `←→` banda, `↑↓` ±0,5 dB (`⇧` ±3), `Tab` alterna Volume/Ganho/Balanço/Bandas,
`0` zera o controle em foco, `r` deixa tudo plano, `+`/`-` volume, `m` mudo, `e` liga ou
desliga o EQ, `p` perfis, `s` salva o perfil, `q` sai.

## Arquivos

- Estado: `~/.config/quickshell-eq/state.json`
- Perfis do usuário: `~/.config/quickshell-eq/presets/<nome>.txt` (`<freq Hz> <ganho dB>`)
- Perfis do sistema: `/usr/local/share/quickshell-eq/presets/` (ou `$QSEQ_SHARE/presets`)
- Execução: `$XDG_RUNTIME_DIR/quickshell-eq/` (`eq31.conf`, pids, `eq31.log`)

## Compilar

```sh
cd tui && cargo build --release      # target/release/eq-tui
```
