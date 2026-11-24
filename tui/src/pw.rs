//! Acesso ao PipeWire: pactl (via pipewire-pulse), pw-dump, pw-cli e o
//! processo `pipewire -c` que hospeda o filter-chain de 31 bandas.
//!
//! Todas as ferramentas rodam com stdin fechado e sob timeout(1): o pw-cli
//! sem tty pode ficar preso.

use crate::bands::{self, N};
use crate::state::{State, runtime_dir};
use anyhow::{Context, Result, bail};
use serde_json::Value;
use std::path::PathBuf;
use std::process::{Command, Stdio};
use std::time::{Duration, Instant};

/// Sink de entrada do EQ (o que os apps veem).
pub const SINK: &str = "qseq31";
/// Stream de saída do EQ, ligado ao topo da cadeia do Quickshell.
pub const SAIDA: &str = "qseq31.saida";

const LIMITER: &str = "/usr/local/lib/ladspa/fast_lookahead_limiter_1913.so";
const LIMIT_DB: f32 = -1.0;

fn run(prog: &str, args: &[&str]) -> Result<String> {
    let out = Command::new("timeout")
        .arg("5")
        .arg(prog)
        .args(args)
        .stdin(Stdio::null())
        .stderr(Stdio::piped())
        .output()
        .with_context(|| format!("não consegui executar {prog}"))?;
    if !out.status.success() {
        let err = String::from_utf8_lossy(&out.stderr);
        bail!("{prog} {}: {}", args.first().unwrap_or(&""), err.trim());
    }
    Ok(String::from_utf8_lossy(&out.stdout).into_owned())
}

fn pactl(args: &[&str]) -> Result<String> {
    run("pactl", args)
}

// ── Sinks e streams (pactl) ─────────────────────────────────────────────

#[derive(Debug, Clone)]
pub struct Sink {
    pub index: u64,
    pub name: String,
}

#[derive(Debug, Clone)]
pub struct Input {
    pub index: u64,
    pub sink: u64,
    pub node: String,
}

pub fn sinks() -> Result<Vec<Sink>> {
    let v: Value = serde_json::from_str(&pactl(&["-f", "json", "list", "short", "sinks"])?)?;
    Ok(v.as_array()
        .into_iter()
        .flatten()
        .filter_map(|s| {
            Some(Sink { index: s["index"].as_u64()?, name: s["name"].as_str()?.to_string() })
        })
        .collect())
}

pub fn sink_inputs() -> Result<Vec<Input>> {
    let v: Value = serde_json::from_str(&pactl(&["-f", "json", "list", "sink-inputs"])?)?;
    Ok(v.as_array()
        .into_iter()
        .flatten()
        .filter_map(|s| {
            Some(Input {
                index: s["index"].as_u64()?,
                sink: s["sink"].as_u64()?,
                node: s["properties"]["node.name"].as_str().unwrap_or("").to_string(),
            })
        })
        .collect())
}

pub fn sink_index(name: &str) -> Result<Option<u64>> {
    Ok(sinks()?.into_iter().find(|s| s.name == name).map(|s| s.index))
}

pub fn sink_name(index: u64) -> Result<Option<String>> {
    Ok(sinks()?.into_iter().find(|s| s.index == index).map(|s| s.name))
}

pub fn default_sink() -> Result<String> {
    Ok(pactl(&["get-default-sink"])?.trim().to_string())
}

pub fn set_default_sink(name: &str) -> Result<()> {
    pactl(&["set-default-sink", name]).map(drop)
}

/// Volume médio dos canais, 0.0..=1.5.
pub fn volume(sink: &str) -> Result<f32> {
    let out = pactl(&["get-sink-volume", sink])?;
    let pcts: Vec<f32> = out
        .split('/')
        .filter_map(|p| p.trim().strip_suffix('%')?.trim().parse().ok())
        .collect();
    if pcts.is_empty() {
        bail!("volume ilegível: {}", out.trim());
    }
    Ok(pcts.iter().sum::<f32>() / pcts.len() as f32 / 100.0)
}

pub fn set_volume(sink: &str, v: f32) -> Result<()> {
    let pct = format!("{}%", (v.clamp(0.0, 1.5) * 100.0).round() as u32);
    pactl(&["set-sink-volume", sink, &pct]).map(drop)
}

pub fn muted(sink: &str) -> Result<bool> {
    Ok(pactl(&["get-sink-mute", sink])?.contains("yes"))
}

pub fn set_mute(sink: &str, on: bool) -> Result<()> {
    pactl(&["set-sink-mute", sink, if on { "1" } else { "0" }]).map(drop)
}

pub fn move_input(index: u64, sink: &str) -> Result<()> {
    pactl(&["move-sink-input", &index.to_string(), sink]).map(drop)
}

/// O stream de saída do EQ, se existir.
pub fn our_output() -> Result<Option<Input>> {
    Ok(sink_inputs()?.into_iter().find(|i| i.node == SAIDA))
}

// ── Nó do filtro (pw-dump / pw-cli) ─────────────────────────────────────

pub fn node_id(name: &str) -> Result<Option<u64>> {
    let v: Value = serde_json::from_str(&run("pw-dump", &["-N"])?)?;
    Ok(v.as_array().into_iter().flatten().find_map(|o| {
        (o["info"]["props"]["node.name"].as_str() == Some(name)).then(|| o["id"].as_u64())?
    }))
}

/// Parâmetros do grafo para um estado.
pub fn params_for(st: &State) -> Vec<(String, f32)> {
    let mut p = Vec::with_capacity(2 * N + 3);
    for (i, g) in st.bands.iter().enumerate() {
        p.push((format!("eqL{}:Gain", i + 1), *g));
        p.push((format!("eqR{}:Gain", i + 1), *g));
    }
    let (l, r) = bands::balance_mults(st.balance);
    p.push(("balL:Mult".into(), l));
    p.push(("balR:Mult".into(), r));
    p.push(("lim:Input gain (dB)".into(), st.gain_db));
    p
}

pub fn set_params(node: u64, params: &[(String, f32)]) -> Result<()> {
    let mut s = String::from("{ params = [ ");
    for (k, v) in params {
        s.push_str(&format!("\"{k}\" {v} "));
    }
    s.push_str("] }");
    run("pw-cli", &["set-param", &node.to_string(), "Props", &s]).map(drop)
}

// ── Processo do filter-chain ────────────────────────────────────────────

fn conf_path() -> PathBuf {
    runtime_dir().join("eq31.conf")
}
fn pid_path() -> PathBuf {
    runtime_dir().join("eq31.pid")
}
fn sup_pid_path() -> PathBuf {
    runtime_dir().join("eq31.sup.pid")
}
pub fn log_path() -> PathBuf {
    runtime_dir().join("eq31.log")
}

pub fn gen_conf(st: &State, target: &str) -> String {
    let mut nodes = String::new();
    let mut links = String::new();
    for c in ["L", "R"] {
        for (i, f) in bands::FREQS.iter().enumerate() {
            let n = i + 1;
            nodes.push_str(&format!(
                "                    {{ type = builtin name = eq{c}{n} label = bq_peaking \
                 control = {{ \"Freq\" = {f} \"Q\" = {q} \"Gain\" = {g} }} }}\n",
                q = bands::Q,
                g = st.bands[i]
            ));
            let next = if n < N { format!("eq{c}{}:In", n + 1) } else { format!("bal{c}:In") };
            links.push_str(&format!(
                "                    {{ output = \"eq{c}{n}:Out\" input = \"{next}\" }}\n"
            ));
        }
    }
    let (bl, br) = bands::balance_mults(st.balance);
    format!(
        r#"# gerado pelo eq-tui (quickshell-eq); não edite: é refeito a cada subida
context.properties = {{ log.level = 2 }}
context.spa-libs = {{
    audio.convert.* = audioconvert/libspa-audioconvert
    support.*       = support/libspa-support
}}
context.modules = [
    {{ name = libpipewire-module-protocol-native }}
    {{ name = libpipewire-module-client-node }}
    {{ name = libpipewire-module-adapter }}
    {{ name = libpipewire-module-filter-chain
        args = {{
            node.description = "EQ 31 bandas"
            media.name       = "EQ 31 bandas"
            filter.graph = {{
                nodes = [
{nodes}                    {{ type = builtin name = balL label = linear control = {{ "Mult" = {bl} }} }}
                    {{ type = builtin name = balR label = linear control = {{ "Mult" = {br} }} }}
                    {{ type = ladspa name = lim plugin = {LIMITER} label = fastLookaheadLimiter
                      control = {{ "Input gain (dB)" = {gain} "Limit (dB)" = {LIMIT_DB} "Release time (s)" = 0.05 }} }}
                ]
                links = [
{links}                    {{ output = "balL:Out" input = "lim:Input 1" }}
                    {{ output = "balR:Out" input = "lim:Input 2" }}
                ]
                inputs  = [ "eqL1:In" "eqR1:In" ]
                outputs = [ "lim:Output 1" "lim:Output 2" ]
            }}
            audio.channels = 2
            audio.position = [ FL FR ]
            capture.props = {{
                node.name   = "{SINK}"
                media.class = "Audio/Sink"
            }}
            playback.props = {{
                node.name     = "{SAIDA}"
                node.passive  = true
                target.object = "{target}"
            }}
        }}
    }}
]
"#,
        gain = st.gain_db,
    )
}

/// Regrava a configuração (usada se o daemon(8) reiniciar o filtro).
pub fn write_conf(st: &State, target: &str) -> Result<()> {
    std::fs::create_dir_all(runtime_dir())?;
    std::fs::write(conf_path(), gen_conf(st, target))?;
    Ok(())
}

fn pid_alive(path: &PathBuf) -> bool {
    let Ok(pid) = std::fs::read_to_string(path) else { return false };
    let pid = pid.trim();
    !pid.is_empty()
        && Command::new("kill")
            .args(["-0", pid])
            .stderr(Stdio::null())
            .status()
            .is_ok_and(|s| s.success())
}

pub fn running() -> bool {
    pid_alive(&pid_path())
}

/// rtprio(1) disponível? (mac_priority libera ao grupo realtime)
fn rt_ok() -> bool {
    Command::new("rtprio")
        .args(["10", "true"])
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .status()
        .is_ok_and(|s| s.success())
}

/// Espera até `cond` valer ou o prazo acabar.
pub fn wait_for(timeout: Duration, mut cond: impl FnMut() -> bool) -> bool {
    let t0 = Instant::now();
    while t0.elapsed() < timeout {
        if cond() {
            return true;
        }
        std::thread::sleep(Duration::from_millis(100));
    }
    false
}

pub fn start(st: &State, target: &str) -> Result<()> {
    if running() {
        return Ok(());
    }
    write_conf(st, target)?;
    let mut cmd = Command::new("daemon");
    cmd.arg("-r")
        .arg("-P")
        .arg(sup_pid_path())
        .arg("-p")
        .arg(pid_path())
        .arg("-o")
        .arg(log_path());
    if rt_ok() {
        cmd.args(["rtprio", "10"]);
    }
    cmd.arg("pipewire").arg("-c").arg(conf_path());
    let ok = cmd
        .stdin(Stdio::null())
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .status()
        .context("daemon(8) não executou")?
        .success();
    if !ok {
        bail!("daemon(8) falhou ao iniciar o filtro");
    }
    if !wait_for(Duration::from_secs(5), || sink_index(SINK).ok().flatten().is_some()) {
        bail!("o sink {SINK} não apareceu (veja {})", log_path().display());
    }
    Ok(())
}

pub fn stop() -> Result<()> {
    for p in [sup_pid_path(), pid_path()] {
        if let Ok(pid) = std::fs::read_to_string(&p) {
            let _ = Command::new("kill").arg(pid.trim()).stderr(Stdio::null()).status();
        }
    }
    wait_for(Duration::from_secs(3), || !running());
    let _ = std::fs::remove_file(pid_path());
    let _ = std::fs::remove_file(sup_pid_path());
    Ok(())
}
