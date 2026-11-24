//! Convivência com a cadeia do Quickshell.
//!
//! A cadeia atual (fx_* → eq_p2 → vol_out → jbl/local_p2) continua do jeito que
//! é. Ligado, o EQ de 31 bandas entra por cima dela:
//!
//!     apps → qseq31 → <topo da cadeia, ex.: fx_3d> → … → saída
//!
//! O volume do usuário fica no sink padrão (qseq31) e o antigo topo vai a
//! 100%, como o EqualizerPopup já faz com os seus filtros. Quando o Quickshell
//! remonta a cadeia, ele torna padrão o seu novo topo; o `Watch` percebe isso e
//! recoloca o qseq31 por cima.

use crate::pw::{self, SAIDA, SINK};
use crate::state::{State, runtime_dir};
use anyhow::{Context, Result, bail};
use std::time::Duration;

const FALLBACK: &str = "local_p2";

/// Move para `to` os streams de apps que estão no sink `from`.
fn move_inputs(from: &str, to: &str) -> Result<()> {
    let Some(from_idx) = pw::sink_index(from)? else { return Ok(()) };
    for i in pw::sink_inputs()? {
        if i.sink == from_idx && i.node != SAIDA {
            let _ = pw::move_input(i.index, to);
        }
    }
    Ok(())
}

/// Põe o qseq31 por cima de `top`, que passa a 100%.
fn attach(st: &mut State, top: &str) -> Result<()> {
    if top == SINK {
        bail!("o EQ não pode tocar nele mesmo");
    }
    let vol = pw::volume(top).unwrap_or(0.2);
    pw::set_volume(SINK, vol)?;
    if let Some(out) = pw::our_output()? {
        if pw::sink_index(top)? != Some(out.sink) {
            pw::move_input(out.index, top)?;
        }
    }
    pw::set_default_sink(SINK)?;
    move_inputs(top, SINK)?;
    pw::set_volume(top, 1.0)?;
    st.upstream = top.to_string();
    pw::write_conf(st, top)?;
    Ok(())
}

/// Onde o EQ está despejando o som agora.
fn current_upstream(st: &State) -> String {
    pw::our_output()
        .ok()
        .flatten()
        .and_then(|o| pw::sink_name(o.sink).ok().flatten())
        .filter(|n| n != SINK)
        .unwrap_or_else(|| {
            if st.upstream.is_empty() { FALLBACK.into() } else { st.upstream.clone() }
        })
}

pub fn enable(st: &mut State) -> Result<String> {
    let top = match pw::default_sink()? {
        d if d == SINK || d.is_empty() => current_upstream(st),
        d => d,
    };
    if !pw::running() {
        pw::start(st, &top)?;
    }
    attach(st, &top)?;
    st.enabled = true;
    Ok(format!("EQ 31 ligado sobre {top}"))
}

pub fn disable(st: &mut State) -> Result<String> {
    st.enabled = false;
    if !pw::running() {
        return Ok("EQ 31 já estava desligado".into());
    }
    let up = current_upstream(st);
    let vol = pw::volume(SINK).unwrap_or(0.2);
    pw::set_volume(&up, vol)?;
    pw::set_default_sink(&up)?;
    move_inputs(SINK, &up)?;
    pw::stop()?;
    Ok(format!("EQ 31 desligado; saída volta para {up}"))
}

/// Recoloca o EQ no topo quando outro programa muda a saída padrão.
#[derive(Default)]
pub struct Watch {
    pending: Option<String>,
}

impl Watch {
    /// Chamar ~1 vez por segundo. `autostart`: sobe o filtro se estiver parado.
    pub fn tick(&mut self, st: &mut State, autostart: bool) -> Result<Option<String>> {
        if !st.enabled {
            self.pending = None;
            return Ok(None);
        }
        if !pw::running() {
            return if autostart { enable(st).map(Some) } else { Ok(None) };
        }
        let def = pw::default_sink()?;
        if def == SINK || def.is_empty() {
            self.pending = None;
            return Ok(None);
        }
        // Espera a saída ficar estável por um ciclo: o Quickshell faz uma rampa
        // de volume ao remontar a cadeia.
        if self.pending.as_deref() == Some(def.as_str()) {
            self.pending = None;
            attach(st, &def)?;
            return Ok(Some(format!("EQ 31 recolocado sobre {def}")));
        }
        self.pending = Some(def);
        Ok(None)
    }
}

fn vigiar_pid() -> std::path::PathBuf {
    runtime_dir().join("vigiar.pid")
}

pub fn vigiar_ativo() -> bool {
    std::fs::read_to_string(vigiar_pid())
        .ok()
        .map(|p| p.trim().to_string())
        .filter(|p| !p.is_empty() && *p != std::process::id().to_string())
        .is_some_and(|p| {
            std::process::Command::new("kill")
                .args(["-0", &p])
                .stderr(std::process::Stdio::null())
                .status()
                .is_ok_and(|s| s.success())
        })
}

/// Modo sem tela: mantém o EQ no topo (para o autostart do Hyprland).
pub fn vigiar() -> Result<()> {
    if vigiar_ativo() {
        bail!("já existe um eq-tui vigiar rodando");
    }
    std::fs::create_dir_all(runtime_dir())?;
    std::fs::write(vigiar_pid(), std::process::id().to_string())
        .context("não consegui gravar o pid")?;
    let mut w = Watch::default();
    loop {
        // Relê o estado: a tela pode ter ligado ou desligado o EQ.
        let mut st = State::load();
        match w.tick(&mut st, true) {
            Ok(Some(msg)) => eprintln!("eq-tui: {msg}"),
            Ok(None) => {}
            Err(e) => eprintln!("eq-tui: {e:#}"),
        }
        std::thread::sleep(Duration::from_secs(1));
    }
}

pub fn status() -> Result<()> {
    let st = State::load();
    let run = pw::running();
    println!("estado salvo: {}", if st.enabled { "ligado" } else { "desligado" });
    println!("filtro: {}", if run { "rodando" } else { "parado" });
    if run {
        println!("saída padrão: {}", pw::default_sink()?);
        println!("despeja em: {}", current_upstream(&st));
    }
    println!("vigiar: {}", if vigiar_ativo() { "ativo" } else { "parado" });
    println!(
        "perfil: {}{}  ganho: {:+} dB  balanço: {:+}",
        st.preset,
        if st.modified { "*" } else { "" },
        st.gain_db,
        st.balance
    );
    Ok(())
}

pub fn cli(on: bool) -> Result<()> {
    let mut st = State::load();
    let msg = if on { enable(&mut st)? } else { disable(&mut st)? };
    st.save()?;
    if on {
        if let Some(id) = pw::node_id(SINK)? {
            pw::set_params(id, &pw::params_for(&st))?;
        }
    }
    println!("{msg}");
    Ok(())
}
