# quickshell-eq

Equalizador gráfico de 31 bandas para integração com Quickshell no FreeBSD.
Inclui o controlador `eq-ctl`, suporte opcional à saída Bluetooth por meio de
`jbl`, componentes QML, presets e exemplos de configuração.

## Requisitos

- FreeBSD com `virtual_oss` e `virtual_oss_cmd` (incluídos no sistema-base
  do FreeBSD 15+). O `virtual_equalizer` deve ser instalado separadamente: use
  `audio/virtual_oss_equalizer` no FreeBSD 15+ (requer fontes do sistema) ou a
  opção EQUALIZER do port `audio/virtual_oss` em versões anteriores.
- Quickshell e PulseAudio (`pactl`).
- `doas` para iniciar/parar a saída Bluetooth como root.

## Instalação manual

Copie `bin/eq-ctl` e, se desejar Bluetooth, `bin/jbl` para um diretório do
`PATH`. Instale `share/quickshell-eq/` sob `/usr/local/share/`. Os arquivos em
`etc-examples/` são apenas exemplos: revise-os e integre somente as opções
necessárias ao seu sistema.

Copie `EqualizerPopup.qml` para sua configuração Quickshell e integre o
componente à sua barra. `Bar.qml` é uma referência, não deve substituir
a configuração existente sem revisão. Copie os presets para
`~/.config/eq-profiles/`; o controle `eq-ctl` usa esse diretório.

## Bluetooth

Configure `JBL_BDADDR` com o endereço Bluetooth do seu próprio dispositivo.
O endereço não é embutido no projeto. O comando `jbl start` requer privilégios
de root; configure `doas` de forma restrita para permitir apenas o comando
necessário. Consulte os exemplos e a documentação do sistema antes de alterar
`/etc/bluetooth/hosts`, `hcsecd.conf` ou permissões de dispositivos.

## Port do FreeBSD

Os arquivos do port estão sendo preparados em `~/ports/audio/quickshell-eq`.
A origem do port será o repositório
[cleberalves40/quickshell-eq](https://github.com/cleberalves40/quickshell-eq).

## Licença

BSD 2-Clause License; consulte [LICENSE](LICENSE).
