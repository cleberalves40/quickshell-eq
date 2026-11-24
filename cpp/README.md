# eq-tui (C++)

Equalizador gráfico de 31 bandas no terminal, como cliente nativo do PipeWire
(`libpipewire`) e com a interface em FTXUI, na mesma estrutura do octopkg-tui.
Usa os mesmos arquivos da versão em Rust (`state.json` e presets em dB).

## Diferenças em relação à versão em Rust (descontinuada)

| | Rust (`tui/`) | C++ (`cpp/`) |
|---|---|---|
| PipeWire | `pactl`, `pw-cli` e `pw-dump` como processos | `libpipewire` direto: registry, metadado `default`, Props dos sinks |
| Filtro | processo `pipewire -c eq31.conf` à parte | `libpipewire-module-filter-chain` carregado dentro do `eq-tui vigiar` |
| Saída padrão | consulta a cada 1 s (~2 s para reagir) | evento do metadado (1 s de espera pela rampa do Quickshell) |
| Ajustes | `pw-cli set-param` (um processo por ajuste) | `pw_node_set_param` |
| Volume e mudo | `pactl` | `channelVolumes`/`mute` nos Props do sink (cúbico, como o pactl) |
| Mover streams | `pactl move-sink-input` | metadado `target.node`/`target.object`, como o pipewire-pulse |

Use o popup do Quickshell em `flat` e sem efeitos: o EQ 31 faz tudo. A tela avisa
quando há efeitos do popup (sinks `fx_*`) ativos em paralelo, que se somariam aos daqui.

O `vigiar` hospeda o filtro enquanto o estado diz "ligado". A tela e os
subcomandos só gravam o `state.json` e mandam SIGHUP ao vigiar (ou o sobem
com `daemon -r`, sob `rtprio 10` quando possível). Ele também confere se a
saída do EQ (`qseq31.saida`) está no topo da cadeia e a religa, se o
WirePlumber a tiver deixado num sink cru durante uma remontagem do Quickshell.

## Visual e analisador

A tela usa a paleta [Nord](https://www.nordtheme.com) com os mesmos papéis do
`Theme.qml` do Quickshell: fundo nord0, texto nord4, destaque nord8 (Frost), ok
nord14, aviso nord13, erro nord11, trilhos e bordas nord3.

Acima do gráfico do EQ fica um **analisador de espectro** (`a` liga e desliga), nas
mesmas 31 colunas das bandas: ele mostra o som *depois* do EQ e dos efeitos. É uma
captura nativa (`pw_stream` passivo no monitor do sink abaixo do `qseq31`) com FFT de
8192 pontos (janela de Hann) somada por banda de 1/3 de oitava, em dBFS de -72 a -12.
As barras sobem em degradê Frost → Aurora; a marca roxa (nord15) é o pico, que segura
0,8 s. Com o analisador na tela, ela redesenha a ~25 quadros/s (cerca de 10% de um
núcleo); sem ele, uma vez por segundo. Em terminais com menos de 26 linhas, ele some.

## Efeitos

Os mesmos do EqualizerPopup do Quickshell, com os mesmos parâmetros, ficam sempre
no grafo e são ligados ao vivo (mistura seco/processado), sem recarregar o filtro
nem cortar o som:

| Efeito | Plugin | Ligado | Desligado |
|---|---|---|---|
| Compressor (`c`) | `dysonCompress` (-12 dB, 0,4 s, 0,4, 0,6) | mixer 100% comprimido | mixer 100% seco |
| Limitador (`i`) | `fastLookaheadLimiter` | limite -1 dB | limite 0 dB (só evita clipping) |
| 3D (`d`) | `matrixSpatialiser` | largura 200 | largura 0 (neutro) |
| Eco (`x`) | `lcrDelay` (L 250 ms, R 375 ms, feedback 35%, níveis 24) | 40% processado | seco |
| Reverb (`v`) | `plate`, um por canal (estéreo) + abertura 3D do modo | Sala, Opera, Stadium, Caverna | seco |

Cada modo de reverb também abre o estéreo pelo 3D, mais quanto maior o espaço
(largura Sala 60, Opera 120, Stadium 180, Caverna 300); com o 3D ligado as larguras se
somam, até 512. O eco é um pouco mais presente que o do popup (feedback 35%, 40%
processado).

Ordem: EQ → compressor → 3D → eco → reverb → balanço → limitador (com o Ganho).
O limitador vem ligado por padrão, com soltura de 0,25 s. A entrada dele desce
automaticamente pelo pico da curva do EQ (a resposta real dos 31 filtros somados):
pela metade do pico com o limitador ligado, que segura o que passar de 0 dB, ou pelo
pico inteiro com ele desligado. O Ganho atua por cima dessa margem. Com o limitador
ligado, o volume vai até 150% (ele entra antes do limitador); sem ele, até 100%. Com tudo no grafo, o filtro usa cerca de 5% de um
núcleo (2 ms por ciclo de 2048 amostras).

## Presets

`share/quickshell-eq/presets/` tem 23: os 8 de 31 bandas do quickshell-eq 1.0 e os
14 do popup do Quickshell (15 bandas do mbeq, interpoladas em log-frequência). Os do
popup que tinham o mesmo nome de um de 31 bandas levam o sufixo `_qs` (`rock_qs`,
`pop_qs`...). O `fones` é próprio para fones de ouvido: graves profundos, menos
embolado no médio-grave, alívio em 4-6,3 kHz e ar no topo.

## Compilar

```sh
pkg install ftxui cmake ninja pipewire
cmake -S cpp -B cpp/build -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C cpp/build && ctest --test-dir cpp/build
```

## Uso

```sh
eq-tui                      # tela
eq-tui ligar | desligar | status
eq-tui vigiar               # hospeda o filtro (autostart)
eq-tui --snapshot 100 30    # desenha a tela uma vez (testes)
eq-tui --print-args         # argumentos do filter-chain para o estado salvo
```

Teclas: `←→` banda, `↑↓` ±0,5 dB, `PgUp`/`PgDn` (ou `K`/`J`) ±3 dB, `Tab`
alterna Volume/Ganho/Balanço/Bandas, `0` zera, `r` plano, `+`/`-` volume,
`m` mudo, `e` liga/desliga, `p` perfis, `s` salva, `c`/`i`/`d`/`x`/`v` efeitos, `a` analisador, `q` sai.
Na linha Efeitos (via `Tab`): `←→` escolhe, `Enter`/espaço liga (no reverb, troca o modo),
`0` desliga todos (menos o limitador).

Log do vigiar: `$XDG_RUNTIME_DIR/quickshell-eq/vigiar.log`.
