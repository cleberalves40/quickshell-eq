//! Desenho da tela.

use crate::app::{App, Focus, Mode};
use crate::bands::{self, N};
use ratatui::Frame;
use ratatui::layout::{Constraint, Layout, Rect};
use ratatui::style::{Color, Modifier, Style, Stylize};
use ratatui::text::{Line, Span};
use ratatui::widgets::{Block, Clear, List, ListItem, ListState, Paragraph};

const POS: Color = Color::Cyan;
const NEG: Color = Color::LightRed;
const OK: Color = Color::Green;
const DIM: Color = Color::DarkGray;

pub fn draw(f: &mut Frame, app: &App) {
    let area = f.area();
    let st = &app.st;
    let preset = format!("{}{}", st.preset, if st.modified { "*" } else { "" });
    let eq = if app.active() {
        Span::styled(format!("ligado → {}", app.upstream), Style::new().fg(OK))
    } else if app.running {
        Span::styled("rodando, fora da saída padrão", Style::new().fg(Color::Yellow))
    } else {
        Span::styled("desligado", Style::new().fg(DIM))
    };
    let title = Line::from(vec![
        " quickshell-eq ".bold(),
        "· perfil: ".into(),
        Span::styled(preset, Style::new().fg(POS)),
        " · EQ 31: ".into(),
        eq,
        " ".into(),
    ]);
    let block = Block::bordered().title(title);
    let inner = block.inner(area);
    f.render_widget(block, area);

    let [ctrl, sep, chart, info, help] = Layout::vertical([
        Constraint::Length(3),
        Constraint::Length(1),
        Constraint::Min(5),
        Constraint::Length(1),
        Constraint::Length(1),
    ])
    .areas(inner);

    draw_controls(f, app, ctrl);
    f.render_widget(
        Paragraph::new("─".repeat(sep.width as usize)).style(Style::new().fg(DIM)),
        sep,
    );
    draw_bands(f, app, chart);
    draw_info(f, app, info);
    draw_help(f, app, help);

    match &app.mode {
        Mode::Presets { list, sel } => draw_presets(f, list, *sel, area),
        Mode::SaveName(name) => draw_save(f, name, area),
        Mode::Normal => {}
    }
}

fn label_style(focused: bool) -> Style {
    if focused { Style::new().fg(Color::Black).bg(POS).bold() } else { Style::new().bold() }
}

fn draw_controls(f: &mut Frame, app: &App, area: Rect) {
    let w = area.width.saturating_sub(10 + 12).max(10) as usize;
    let st = &app.st;

    // Volume: barra cheia
    let filled = ((app.vol.clamp(0.0, 1.0)) * w as f32).round() as usize;
    let vol_color = if app.muted { DIM } else { OK };
    let vol = Line::from(vec![
        Span::styled(" Volume  ", label_style(app.focus == Focus::Volume)),
        " ".into(),
        Span::styled("█".repeat(filled), Style::new().fg(vol_color)),
        Span::styled("░".repeat(w - filled), Style::new().fg(DIM)),
        format!(" {:>3}%", (app.vol * 100.0).round() as i32).into(),
        if app.muted { " mudo".red().bold() } else { "".into() },
    ]);

    let gain = bipolar(
        " Ganho   ",
        app.focus == Focus::Gain,
        st.gain_db / bands::GAIN_MAX,
        w,
        format!(" {:+} dB", st.gain_db),
        st.gain_db != 0.0,
    );
    let bal_txt = match st.balance {
        0 => " centro".to_string(),
        b if b < 0 => format!(" L {}", -b),
        b => format!(" R {b}"),
    };
    let bal = bipolar(
        " Balanço ",
        app.focus == Focus::Balance,
        st.balance as f32 / bands::BAL_MAX as f32,
        w,
        bal_txt,
        st.balance != 0,
    );
    f.render_widget(Paragraph::new(vec![vol, gain, bal]), area);
}

/// Trilho com cursor: -1.0 ..= 1.0.
fn bipolar(name: &str, focused: bool, pos: f32, w: usize, value: String, changed: bool) -> Line<'static> {
    let mid = w / 2;
    let knob = ((pos.clamp(-1.0, 1.0) + 1.0) / 2.0 * (w - 1) as f32).round() as usize;
    let mut s = String::with_capacity(w * 3);
    for i in 0..w {
        s.push(if i == knob { '●' } else if i == mid { '┼' } else { '·' });
    }
    let c = if changed { POS } else { DIM };
    Line::from(vec![
        Span::styled(name.to_string(), label_style(focused)),
        " ".into(),
        Span::styled(s, Style::new().fg(c)),
        Span::raw(value),
    ])
}

fn draw_bands(f: &mut Frame, app: &App, area: Rect) {
    const AXIS: u16 = 5;
    let mut h = area.height.saturating_sub(1) as usize; // última linha: rótulos
    if h < 3 || area.width < AXIS + N as u16 {
        return; // terminal pequeno demais
    }
    if h % 2 == 0 {
        h -= 1; // ímpar: a linha do meio é o 0 dB
    }
    let cw = ((area.width.saturating_sub(AXIS)) as usize / N).clamp(1, 4);
    let bar_w = if cw >= 3 { cw - 1 } else { cw.min(2).max(1) - usize::from(cw == 2) };
    let bar_w = bar_w.max(1);
    let mid = h / 2; // índice da linha do meio (de cima)
    let half = h; // unidades de meia linha de cada lado
    let focused = app.focus == Focus::Bands;

    let mut lines: Vec<Line> = Vec::with_capacity(h + 1);
    for row in 0..h {
        let axis = if row == 0 {
            format!("{:>+4} ", bands::BAND_MAX as i32)
        } else if row == mid {
            "   0 ".to_string()
        } else if row == h - 1 {
            format!("{:>+4} ", -(bands::BAND_MAX as i32))
        } else {
            "     ".to_string()
        };
        let mut spans = vec![Span::styled(axis, Style::new().fg(DIM))];
        // unidades (meias linhas) contadas de baixo; o 0 dB fica em `half`
        let top_u = (h - 1 - row) * 2 + 1;
        let bot_u = (h - 1 - row) * 2;
        for (i, &g) in app.st.bands.iter().enumerate() {
            let n = ((g.abs() / bands::BAND_MAX) * half as f32).round() as usize;
            let filled = |u: usize| {
                if g > 0.0 { u >= half && u < half + n } else if g < 0.0 { u < half && u + n >= half } else { false }
            };
            let (t, b) = (filled(top_u), filled(bot_u));
            let ch = match (t, b) {
                (true, true) => '█',
                (true, false) => '▀',
                (false, true) => '▄',
                _ if row == mid => '─',
                _ => ' ',
            };
            let sel = i == app.sel;
            let mut style = Style::new().fg(if t || b {
                if g > 0.0 { POS } else { NEG }
            } else {
                DIM
            });
            if sel {
                style = style.add_modifier(Modifier::BOLD);
                if focused && !(t || b) && ch == ' ' {
                    style = style.bg(Color::Indexed(236));
                }
                if t || b {
                    style = style.fg(Color::White);
                }
            }
            spans.push(Span::styled(ch.to_string().repeat(bar_w), style));
            if cw > bar_w {
                let gap = if row == mid { "─" } else { " " };
                spans.push(Span::styled(gap.repeat(cw - bar_w), Style::new().fg(DIM)));
            }
        }
        lines.push(Line::from(spans));
    }

    // rótulos de frequência sem sobreposição
    let mut lab = vec![' '; AXIS as usize + cw * N + 6];
    let mut lab_style: Vec<bool> = vec![false; lab.len()];
    let mut next_free = 0usize;
    let sel_x = AXIS as usize + app.sel * cw;
    let sel_txt = bands::label(bands::FREQS[app.sel]);
    for (i, fr) in bands::FREQS.iter().enumerate() {
        let x = AXIS as usize + i * cw;
        let t = bands::label(*fr);
        let collides_sel = i != app.sel && x < sel_x + sel_txt.len() + 1 && sel_x < x + t.len() + 1;
        if (x >= next_free && !collides_sel) || i == app.sel {
            for (k, c) in t.chars().enumerate() {
                if x + k < lab.len() {
                    lab[x + k] = c;
                    lab_style[x + k] = i == app.sel;
                }
            }
            next_free = x + t.len() + 1;
        }
    }
    let mut spans = Vec::new();
    let mut buf = String::new();
    let mut cur = false;
    for (c, s) in lab.into_iter().zip(lab_style) {
        if s != cur && !buf.is_empty() {
            spans.push(lab_span(std::mem::take(&mut buf), cur));
        }
        cur = s;
        buf.push(c);
    }
    spans.push(lab_span(buf, cur));
    lines.push(Line::from(spans));

    f.render_widget(Paragraph::new(lines), area);
}

fn lab_span(s: String, sel: bool) -> Span<'static> {
    if sel { Span::styled(s, Style::new().fg(POS).bold()) } else { Span::styled(s, Style::new().fg(DIM)) }
}

fn draw_info(f: &mut Frame, app: &App, area: Rect) {
    let line = if !app.msg.is_empty() {
        let c = if app.msg_err { Color::Red } else { Color::Yellow };
        Line::from(Span::styled(format!(" {}", app.msg), Style::new().fg(c)))
    } else {
        let fr = bands::FREQS[app.sel];
        let unit = if fr >= 1000.0 { format!("{} kHz", fr / 1000.0) } else { format!("{fr} Hz") };
        Line::from(vec![
            " banda ".into(),
            Span::styled(unit, Style::new().fg(POS).bold()),
            format!(": {:+.1} dB", app.st.bands[app.sel]).into(),
            Span::styled(format!("   saída padrão: {}", app.def_sink), Style::new().fg(DIM)),
            if app.vigiar { Span::styled("   vigiar ativo", Style::new().fg(DIM)) } else { "".into() },
        ])
    };
    f.render_widget(Paragraph::new(line), area);
}

fn draw_help(f: &mut Frame, app: &App, area: Rect) {
    let wide = area.width >= 112;
    let txt = match (app.focus, wide) {
        (Focus::Bands, true) => " ←→ banda  ↑↓ ±0.5 dB  ⇧↑↓ ±3  0 zerar  r plano  Tab controles  +/- vol  m mudo  e liga  p perfis  s salvar  q sair",
        (Focus::Bands, false) => " ←→ banda ↑↓ dB 0 zera r plano Tab ctrl +- vol m mudo e EQ p perfis s salva q sai",
        (_, true) => " ←→ ajustar  ↑↓/Tab controle  0 zerar  +/- vol  m mudo  e liga/desliga EQ  p perfis  s salvar  q sair",
        (_, false) => " ←→ ajusta ↑↓/Tab ctrl 0 zera +- vol m mudo e EQ p perfis s salva q sai",
    };
    f.render_widget(Paragraph::new(txt).style(Style::new().fg(DIM)), area);
}

fn centered(area: Rect, w: u16, h: u16) -> Rect {
    let w = w.min(area.width);
    let h = h.min(area.height);
    Rect { x: area.x + (area.width - w) / 2, y: area.y + (area.height - h) / 2, width: w, height: h }
}

fn draw_presets(f: &mut Frame, list: &[String], sel: usize, area: Rect) {
    let r = centered(area, 32, (list.len() as u16 + 2).min(18));
    f.render_widget(Clear, r);
    let items: Vec<ListItem> = list.iter().map(|n| ListItem::new(format!(" {n}"))).collect();
    let w = List::new(items)
        .block(Block::bordered().title(" perfis (Enter carrega, Esc fecha) "))
        .highlight_style(Style::new().fg(Color::Black).bg(POS));
    let mut ls = ListState::default().with_selected(Some(sel));
    f.render_stateful_widget(w, r, &mut ls);
}

fn draw_save(f: &mut Frame, name: &str, area: Rect) {
    let r = centered(area, 44, 3);
    f.render_widget(Clear, r);
    let p = Paragraph::new(format!(" {name}▏"))
        .block(Block::bordered().title(" salvar perfil como (Enter / Esc) "));
    f.render_widget(p, r);
}

#[cfg(test)]
mod tests {
    use super::*;
    use ratatui::{Terminal, backend::TestBackend};

    #[test]
    #[ignore]
    fn snapshot() {
        for (w, h) in [(80u16, 24u16), (120, 30)] {
            let mut app = App::new();
            app.st.bands = crate::state::load_preset("rock").unwrap();
            app.st.preset = "rock".into();
            app.st.gain_db = 3.0;
            app.st.balance = -2;
            let mut t = Terminal::new(TestBackend::new(w, h)).unwrap();
            t.draw(|f| draw(f, &app)).unwrap();
            let buf = t.backend().buffer();
            for y in 0..h {
                let s: String = (0..w).map(|x| buf[(x, y)].symbol().to_string()).collect();
                println!("{s}");
            }
        }
    }
}
