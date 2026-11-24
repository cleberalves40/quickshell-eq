//! Estado da tela e tratamento das teclas.

use crate::bands::{self, N};
use crate::chain::{self, Watch};
use crate::pw::{self, SINK};
use crate::state::{self, State};
use anyhow::Result;
use ratatui::crossterm::event::{self, Event, KeyCode, KeyEvent, KeyEventKind, KeyModifiers};
use std::time::{Duration, Instant};

#[derive(Clone, Copy, PartialEq, Eq)]
pub enum Focus {
    Volume,
    Gain,
    Balance,
    Bands,
}

impl Focus {
    fn next(self) -> Self {
        match self {
            Focus::Volume => Focus::Gain,
            Focus::Gain => Focus::Balance,
            Focus::Balance => Focus::Bands,
            Focus::Bands => Focus::Volume,
        }
    }
    fn prev(self) -> Self {
        match self {
            Focus::Volume => Focus::Bands,
            Focus::Gain => Focus::Volume,
            Focus::Balance => Focus::Gain,
            Focus::Bands => Focus::Balance,
        }
    }
}

pub enum Mode {
    Normal,
    Presets { list: Vec<String>, sel: usize },
    SaveName(String),
}

pub struct App {
    pub st: State,
    pub focus: Focus,
    pub sel: usize,
    pub mode: Mode,
    pub msg: String,
    pub msg_err: bool,
    msg_at: Instant,
    // ao vivo
    pub vol: f32,
    pub muted: bool,
    pub def_sink: String,
    pub running: bool,
    pub upstream: String,
    pub vigiar: bool,
    // aplicação
    node: Option<u64>,
    params_dirty: bool,
    state_dirty: bool,
    changed_at: Instant,
    refreshed_at: Instant,
    watch: Watch,
    pub quit: bool,
}

const VOL_STEP: f32 = 0.05;
const DEBOUNCE: Duration = Duration::from_millis(60);

impl App {
    pub fn new() -> App {
        let mut app = App {
            st: State::load(),
            focus: Focus::Bands,
            sel: 17, // 1 kHz
            mode: Mode::Normal,
            msg: String::new(),
            msg_err: false,
            msg_at: Instant::now(),
            vol: 0.0,
            muted: false,
            def_sink: String::new(),
            running: false,
            upstream: String::new(),
            vigiar: false,
            node: None,
            params_dirty: false,
            state_dirty: false,
            changed_at: Instant::now(),
            refreshed_at: Instant::now() - Duration::from_secs(10),
            watch: Watch::default(),
            quit: false,
        };
        app.refresh();
        // Filtro já rodando: garante que ele reflita o estado salvo.
        if app.running {
            app.params_dirty = true;
        }
        app
    }

    pub fn active(&self) -> bool {
        self.running && self.def_sink == SINK
    }

    fn say(&mut self, msg: impl Into<String>) {
        self.msg = msg.into();
        self.msg_err = false;
        self.msg_at = Instant::now();
    }

    fn fail(&mut self, e: anyhow::Error) {
        self.msg = format!("{e:#}");
        self.msg_err = true;
        self.msg_at = Instant::now();
    }

    fn refresh(&mut self) {
        self.refreshed_at = Instant::now();
        self.running = pw::running();
        self.vigiar = chain::vigiar_ativo();
        match pw::default_sink() {
            Ok(d) => self.def_sink = d,
            Err(e) => return self.fail(e),
        }
        self.vol = pw::volume("@DEFAULT_SINK@").unwrap_or(0.0);
        self.muted = pw::muted("@DEFAULT_SINK@").unwrap_or(false);
        self.upstream = if self.running {
            pw::our_output()
                .ok()
                .flatten()
                .and_then(|o| pw::sink_name(o.sink).ok().flatten())
                .unwrap_or_default()
        } else {
            String::new()
        };
        if !self.running {
            self.node = None;
        }
    }

    fn touch(&mut self) {
        self.params_dirty = true;
        self.state_dirty = true;
        self.changed_at = Instant::now();
    }

    fn apply(&mut self) {
        if self.running {
            if self.node.is_none() {
                self.node = pw::node_id(SINK).ok().flatten();
            }
            if let Some(id) = self.node {
                if let Err(e) = pw::set_params(id, &pw::params_for(&self.st)) {
                    self.node = None;
                    return self.fail(e);
                }
            }
        }
        self.params_dirty = false;
        if self.state_dirty {
            self.state_dirty = false;
            if let Err(e) = self.st.save() {
                return self.fail(e);
            }
            if self.running && !self.upstream.is_empty() {
                let _ = pw::write_conf(&self.st, &self.upstream);
            }
        }
    }

    pub fn on_tick(&mut self) {
        if (self.params_dirty || self.state_dirty) && self.changed_at.elapsed() >= DEBOUNCE {
            self.apply();
        }
        if self.refreshed_at.elapsed() >= Duration::from_secs(1) {
            if !self.vigiar {
                match self.watch.tick(&mut self.st, false) {
                    Ok(Some(m)) => {
                        self.say(m);
                        self.state_dirty = true;
                    }
                    Ok(None) => {}
                    Err(e) => self.fail(e),
                }
            }
            self.refresh();
        }
        if !self.msg.is_empty() && self.msg_at.elapsed() > Duration::from_secs(6) {
            self.msg.clear();
        }
    }

    pub fn poll(&mut self, timeout: Duration) -> Result<()> {
        if event::poll(timeout)? {
            if let Event::Key(k) = event::read()? {
                if k.kind == KeyEventKind::Press {
                    self.on_key(k);
                }
            }
        }
        Ok(())
    }

    // ── Ações ───────────────────────────────────────────────────────────

    fn band(&mut self, delta: f32) {
        let v = &mut self.st.bands[self.sel];
        *v = bands::clamp_band(*v + delta);
        self.st.modified = true;
        self.touch();
    }

    fn gain(&mut self, delta: f32) {
        self.st.gain_db = (self.st.gain_db + delta).clamp(-bands::GAIN_MAX, bands::GAIN_MAX);
        self.touch();
    }

    fn balance(&mut self, delta: i32) {
        self.st.balance = (self.st.balance + delta).clamp(-bands::BAL_MAX, bands::BAL_MAX);
        self.touch();
    }

    fn volume(&mut self, delta: f32) {
        let v = ((self.vol + delta).clamp(0.0, 1.0) * 100.0).round() / 100.0;
        match pw::set_volume("@DEFAULT_SINK@", v) {
            Ok(()) => self.vol = v,
            Err(e) => self.fail(e),
        }
    }

    fn toggle_mute(&mut self) {
        match pw::set_mute("@DEFAULT_SINK@", !self.muted) {
            Ok(()) => self.muted = !self.muted,
            Err(e) => self.fail(e),
        }
    }

    fn toggle_eq(&mut self) {
        self.say(if self.running { "desligando…" } else { "ligando…" });
        let r = if self.active() || self.running {
            chain::disable(&mut self.st)
        } else {
            chain::enable(&mut self.st)
        };
        match r {
            Ok(m) => self.say(m),
            Err(e) => self.fail(e),
        }
        self.node = None;
        self.refresh();
        self.touch();
    }

    fn zero(&mut self) {
        match self.focus {
            Focus::Bands => self.band(-self.st.bands[self.sel]),
            Focus::Gain => self.gain(-self.st.gain_db),
            Focus::Balance => self.balance(-self.st.balance),
            Focus::Volume => {}
        }
    }

    fn flat(&mut self) {
        self.st.bands = vec![0.0; N];
        self.st.preset = "flat".into();
        self.st.modified = false;
        self.touch();
        self.say("todas as bandas em 0 dB");
    }

    fn load_preset(&mut self, name: &str) {
        match state::load_preset(name) {
            Ok(b) => {
                self.st.bands = b;
                self.st.preset = name.to_string();
                self.st.modified = false;
                self.touch();
                self.say(format!("perfil {name} carregado"));
            }
            Err(e) => self.fail(e),
        }
    }

    fn save_preset(&mut self, name: &str) {
        match state::save_preset(name, &self.st.bands) {
            Ok(()) => {
                self.st.preset = name.to_string();
                self.st.modified = false;
                self.state_dirty = true;
                self.say(format!("perfil {name} salvo em {}", state::config_dir().join("presets").display()));
            }
            Err(e) => self.fail(e),
        }
    }

    // ── Teclado ─────────────────────────────────────────────────────────

    fn on_key(&mut self, k: KeyEvent) {
        if k.modifiers.contains(KeyModifiers::CONTROL) && k.code == KeyCode::Char('c') {
            self.quit = true;
            return;
        }
        match std::mem::replace(&mut self.mode, Mode::Normal) {
            Mode::Normal => self.key_normal(k),
            Mode::Presets { list, mut sel } => match k.code {
                KeyCode::Up | KeyCode::Char('k') => {
                    sel = sel.saturating_sub(1);
                    self.mode = Mode::Presets { list, sel };
                }
                KeyCode::Down | KeyCode::Char('j') => {
                    sel = (sel + 1).min(list.len().saturating_sub(1));
                    self.mode = Mode::Presets { list, sel };
                }
                KeyCode::Enter => {
                    if let Some(n) = list.get(sel).cloned() {
                        self.load_preset(&n);
                    }
                }
                KeyCode::Esc | KeyCode::Char('q') | KeyCode::Char('p') => {}
                _ => self.mode = Mode::Presets { list, sel },
            },
            Mode::SaveName(mut name) => match k.code {
                KeyCode::Enter => self.save_preset(&name.clone()),
                KeyCode::Esc => {}
                KeyCode::Backspace => {
                    name.pop();
                    self.mode = Mode::SaveName(name);
                }
                KeyCode::Char(c) if name.len() < 40 => {
                    name.push(c);
                    self.mode = Mode::SaveName(name);
                }
                _ => self.mode = Mode::SaveName(name),
            },
        }
    }

    fn key_normal(&mut self, k: KeyEvent) {
        let big = k.modifiers.contains(KeyModifiers::SHIFT);
        match k.code {
            KeyCode::Char('q') | KeyCode::Esc => self.quit = true,
            KeyCode::Tab => self.focus = self.focus.next(),
            KeyCode::BackTab => self.focus = self.focus.prev(),
            KeyCode::Char('+') | KeyCode::Char('=') => self.volume(VOL_STEP),
            KeyCode::Char('-') => self.volume(-VOL_STEP),
            KeyCode::Char('m') => self.toggle_mute(),
            KeyCode::Char('e') => self.toggle_eq(),
            KeyCode::Char('0') => self.zero(),
            KeyCode::Char('r') => self.flat(),
            KeyCode::Char('p') => {
                let list = state::list_presets();
                if list.is_empty() {
                    self.fail(anyhow::anyhow!("nenhum perfil encontrado"));
                } else {
                    let sel = list.iter().position(|n| *n == self.st.preset).unwrap_or(0);
                    self.mode = Mode::Presets { list, sel };
                }
            }
            KeyCode::Char('s') => {
                let n = if self.st.preset == "flat" { String::new() } else { self.st.preset.clone() };
                self.mode = Mode::SaveName(n);
            }
            _ => match self.focus {
                Focus::Bands => self.key_bands(k.code, big),
                f => self.key_slider(f, k.code),
            },
        }
    }

    fn key_bands(&mut self, code: KeyCode, big: bool) {
        let step = if big { 3.0 } else { bands::BAND_STEP };
        match code {
            KeyCode::Left | KeyCode::Char('h') => self.sel = self.sel.saturating_sub(1),
            KeyCode::Right | KeyCode::Char('l') => self.sel = (self.sel + 1).min(N - 1),
            KeyCode::Home => self.sel = 0,
            KeyCode::End => self.sel = N - 1,
            KeyCode::Up | KeyCode::Char('k') => self.band(step),
            KeyCode::Down | KeyCode::Char('j') => self.band(-step),
            KeyCode::Char('K') | KeyCode::PageUp => self.band(3.0),
            KeyCode::Char('J') | KeyCode::PageDown => self.band(-3.0),
            _ => {}
        }
    }

    fn key_slider(&mut self, f: Focus, code: KeyCode) {
        let dir = match code {
            KeyCode::Left | KeyCode::Char('h') => -1,
            KeyCode::Right | KeyCode::Char('l') => 1,
            KeyCode::Up | KeyCode::Char('k') => return self.focus = self.focus.prev(),
            KeyCode::Down | KeyCode::Char('j') => return self.focus = self.focus.next(),
            _ => return,
        };
        match f {
            Focus::Volume => self.volume(dir as f32 * VOL_STEP),
            Focus::Gain => self.gain(dir as f32),
            Focus::Balance => self.balance(dir),
            Focus::Bands => {}
        }
    }

    /// Grava o que estiver pendente antes de sair.
    pub fn flush(&mut self) {
        if self.params_dirty || self.state_dirty {
            self.apply();
        }
    }
}
