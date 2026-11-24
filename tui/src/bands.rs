//! Bandas ISO de 1/3 de oitava e os limites dos controles.

pub const N: usize = 31;

pub const FREQS: [f32; N] = [
    20.0, 25.0, 31.5, 40.0, 50.0, 63.0, 80.0, 100.0, 125.0, 160.0, 200.0, 250.0, 315.0, 400.0,
    500.0, 630.0, 800.0, 1000.0, 1250.0, 1600.0, 2000.0, 2500.0, 3150.0, 4000.0, 5000.0, 6300.0,
    8000.0, 10000.0, 12500.0, 16000.0, 20000.0,
];

/// Q de um filtro peaking com largura de 1/3 de oitava.
pub const Q: f32 = 4.32;

pub const BAND_MAX: f32 = 12.0;
pub const BAND_STEP: f32 = 0.5;
pub const GAIN_MAX: f32 = 12.0;
pub const BAL_MAX: i32 = 10;
/// Atenuação por passo do balanço, em dB (igual ao eq-ctl antigo).
pub const BAL_DB_STEP: f32 = 3.0;

/// Rótulo curto: 31.5, 630, 1k, 1k25, 12k5.
pub fn label(f: f32) -> String {
    if f >= 1000.0 {
        let s = format!("{}", f as u32);
        let (k, rest) = s.split_at(s.len() - 3);
        let rest = rest.trim_end_matches('0');
        format!("{k}k{rest}")
    } else if f.fract() != 0.0 {
        format!("{f:.1}")
    } else {
        format!("{}", f as u32)
    }
}

/// Multiplicadores (esquerdo, direito) para um balanço em -10..=10.
pub fn balance_mults(bal: i32) -> (f32, f32) {
    let att = |steps: i32| {
        if steps >= BAL_MAX {
            0.0
        } else {
            10f32.powf(-(steps as f32) * BAL_DB_STEP / 20.0)
        }
    };
    match bal {
        b if b < 0 => (1.0, att(-b)),
        b if b > 0 => (att(b), 1.0),
        _ => (1.0, 1.0),
    }
}

pub fn clamp_band(v: f32) -> f32 {
    (v.clamp(-BAND_MAX, BAND_MAX) * 10.0).round() / 10.0
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn rotulos() {
        assert_eq!(label(31.5), "31.5");
        assert_eq!(label(630.0), "630");
        assert_eq!(label(1000.0), "1k");
        assert_eq!(label(1250.0), "1k25");
        assert_eq!(label(3150.0), "3k15");
        assert_eq!(label(12500.0), "12k5");
    }

    #[test]
    fn balanco() {
        assert_eq!(balance_mults(0), (1.0, 1.0));
        assert_eq!(balance_mults(-10), (1.0, 0.0));
        assert_eq!(balance_mults(10), (0.0, 1.0));
        let (l, r) = balance_mults(2);
        assert!((l - 0.501).abs() < 0.01 && r == 1.0); // -6 dB
    }
}
