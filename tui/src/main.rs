//! eq-tui: equalizador de 31 bandas no terminal, sobre o PipeWire.

mod app;
mod bands;
mod chain;
mod pw;
mod state;
mod ui;

use anyhow::{Result, bail};
use std::time::Duration;

const USAGE: &str = "\
uso: eq-tui              abre a tela
     eq-tui ligar        põe o EQ de 31 bandas no topo da cadeia de áudio
     eq-tui desligar     tira o EQ e devolve a saída ao topo anterior
     eq-tui status       mostra o estado
     eq-tui vigiar       sem tela: sobe o EQ se ele estiver marcado como ligado e o
                         recoloca no topo quando o Quickshell remonta a cadeia";

fn tui() -> Result<()> {
    let mut app = app::App::new();
    let mut term = ratatui::init();
    let res = (|| -> Result<()> {
        while !app.quit {
            term.draw(|f| ui::draw(f, &app))?;
            app.poll(Duration::from_millis(100))?;
            app.on_tick();
        }
        Ok(())
    })();
    ratatui::restore();
    app.flush();
    res
}

fn main() -> Result<()> {
    let arg = std::env::args().nth(1);
    match arg.as_deref() {
        None => tui(),
        Some("ligar") => chain::cli(true),
        Some("desligar") => chain::cli(false),
        Some("status") => chain::status(),
        Some("vigiar") => chain::vigiar(),
        Some("-h" | "--help" | "ajuda") => {
            println!("{USAGE}");
            Ok(())
        }
        Some(x) => bail!("comando desconhecido: {x}\n{USAGE}"),
    }
}
