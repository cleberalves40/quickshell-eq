//! Estado persistente (~/.config/quickshell-eq/state.json) e presets.

use crate::bands::{self, N};
use anyhow::{Context, Result, bail};
use serde::{Deserialize, Serialize};
use std::path::PathBuf;

#[derive(Serialize, Deserialize, Clone, Debug)]
#[serde(default)]
pub struct State {
    /// Ganho de cada banda, em dB.
    pub bands: Vec<f32>,
    /// Ganho de entrada do limitador, em dB.
    pub gain_db: f32,
    /// -10 (só esquerdo) .. +10 (só direito).
    pub balance: i32,
    pub preset: String,
    /// As bandas foram mexidas depois de carregar o preset.
    pub modified: bool,
    /// O EQ de 31 bandas deve ficar no topo da cadeia.
    pub enabled: bool,
    /// Sink onde o EQ despeja o som (o topo da cadeia do Quickshell).
    pub upstream: String,
}

impl Default for State {
    fn default() -> Self {
        State {
            bands: vec![0.0; N],
            gain_db: 0.0,
            balance: 0,
            preset: "flat".into(),
            modified: false,
            enabled: false,
            upstream: String::new(),
        }
    }
}

fn home() -> PathBuf {
    std::env::var_os("HOME").map(PathBuf::from).unwrap_or_else(|| PathBuf::from("/tmp"))
}

pub fn config_dir() -> PathBuf {
    std::env::var_os("XDG_CONFIG_HOME")
        .map(PathBuf::from)
        .unwrap_or_else(|| home().join(".config"))
        .join("quickshell-eq")
}

pub fn runtime_dir() -> PathBuf {
    match std::env::var_os("XDG_RUNTIME_DIR") {
        Some(d) => PathBuf::from(d).join("quickshell-eq"),
        None => config_dir().join("run"),
    }
}

impl State {
    fn path() -> PathBuf {
        config_dir().join("state.json")
    }

    pub fn load() -> State {
        let mut st: State = std::fs::read_to_string(Self::path())
            .ok()
            .and_then(|s| serde_json::from_str(&s).ok())
            .unwrap_or_default();
        st.normalize();
        st
    }

    pub fn save(&self) -> Result<()> {
        let dir = config_dir();
        std::fs::create_dir_all(&dir)?;
        let tmp = dir.join(".state.json.tmp");
        std::fs::write(&tmp, serde_json::to_string_pretty(self)?)?;
        std::fs::rename(&tmp, Self::path())?;
        Ok(())
    }

    fn normalize(&mut self) {
        self.bands.resize(N, 0.0);
        for b in &mut self.bands {
            *b = bands::clamp_band(*b);
        }
        self.gain_db = self.gain_db.clamp(-bands::GAIN_MAX, bands::GAIN_MAX).round();
        self.balance = self.balance.clamp(-bands::BAL_MAX, bands::BAL_MAX);
    }
}

// ── Presets ──────────────────────────────────────────────────────────────
// Arquivo texto, uma banda por linha: "<freq Hz> <ganho dB>"; '#' comenta.

pub fn preset_dirs() -> Vec<PathBuf> {
    let share = std::env::var_os("QSEQ_SHARE")
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from("/usr/local/share/quickshell-eq"));
    vec![config_dir().join("presets"), share.join("presets")]
}

pub fn list_presets() -> Vec<String> {
    let mut names: Vec<String> = preset_dirs()
        .iter()
        .filter_map(|d| std::fs::read_dir(d).ok())
        .flatten()
        .flatten()
        .filter_map(|e| {
            let p = e.path();
            if p.extension()? != "txt" {
                return None;
            }
            Some(p.file_stem()?.to_string_lossy().into_owned())
        })
        .collect();
    names.sort_by_key(|n| n.to_lowercase());
    names.dedup();
    names
}

pub fn load_preset(name: &str) -> Result<Vec<f32>> {
    let path = preset_dirs()
        .into_iter()
        .map(|d| d.join(format!("{name}.txt")))
        .find(|p| p.is_file())
        .with_context(|| format!("preset '{name}' não encontrado"))?;
    let text = std::fs::read_to_string(&path)?;
    let mut out = vec![0.0; N];
    let mut seen = 0;
    for line in text.lines() {
        let line = line.split('#').next().unwrap_or("").trim();
        if line.is_empty() {
            continue;
        }
        let mut it = line.split_whitespace();
        let (Some(f), Some(g)) = (it.next(), it.next()) else { continue };
        let (Ok(f), Ok(g)) = (f.parse::<f32>(), g.parse::<f32>()) else { continue };
        if let Some(i) = bands::FREQS.iter().position(|&bf| (bf - f).abs() <= bf * 0.02) {
            out[i] = bands::clamp_band(g);
            seen += 1;
        }
    }
    if seen == 0 {
        bail!("{}: nenhuma banda reconhecida", path.display());
    }
    Ok(out)
}

pub fn valid_name(name: &str) -> bool {
    !name.is_empty()
        && name.len() <= 40
        && name.chars().all(|c| c.is_ascii_alphanumeric() || c == '_' || c == '-')
}

pub fn save_preset(name: &str, gains: &[f32]) -> Result<()> {
    if !valid_name(name) {
        bail!("nome inválido (use letras, números, _ e -)");
    }
    let dir = config_dir().join("presets");
    std::fs::create_dir_all(&dir)?;
    let mut s = String::from("# quickshell-eq: <freq Hz> <ganho dB>\n");
    for (f, g) in bands::FREQS.iter().zip(gains) {
        s.push_str(&format!("{f} {g:.1}\n"));
    }
    std::fs::write(dir.join(format!("{name}.txt")), s)?;
    Ok(())
}
