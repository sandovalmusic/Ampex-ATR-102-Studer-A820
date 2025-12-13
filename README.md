# Ampex ATR-102 / Studer A820 Tape Emulation

[![Build](https://github.com/sandovalmusic/Ampex-ATR-102-Studer-A820/actions/workflows/build.yml/badge.svg)](https://github.com/sandovalmusic/Ampex-ATR-102-Studer-A820/actions/workflows/build.yml)

Physics-based tape saturation plugin emulating the **Ampex ATR-102** and **Studer A820** tape machines with GP9 tape formula at 30 IPS.

## Download

**[Download Latest Builds](https://github.com/sandovalmusic/Ampex-ATR-102-Studer-A820/actions/workflows/build.yml)** — Click the latest successful run, scroll to "Artifacts":

| Platform | Format | File |
|----------|--------|------|
| **Windows** | VST3 | `Ampex-ATR102-Studer-A820-Windows-VST3.zip` |
| **macOS** | VST3/AU | `Ampex-ATR102-Studer-A820-macOS-VST3.zip` / `...AU.zip` |
| **Linux** | VST3 | `Ampex-ATR102-Studer-A820-Linux-VST3.zip` |

---

## Design Philosophy

Professional tape machines (ATR-102, A820) were precision instruments—their electronics measure linearly. The characteristic tape response originates from magnetic recording physics:

- Ferromagnetic hysteresis (Jiles-Atherton model)
- AC bias linearization effects
- Head gap geometry (frequency-dependent phase)
- Nonlinear compression near saturation

This emulation models the **tape and heads**, not the electronics that were designed for transparency.

---

## Controls

| Control | Range | Default | Function |
|---------|-------|---------|----------|
| **Mode** | Master / Tracks | Master | Ampex ATR-102 or Studer A820 |
| **Drive** | -12 dB to +18 dB | -6 dB | Input level to saturation stage |
| **Volume** | -20 dB to +9.5 dB | 0 dB | Output level |

---

## Machine Specifications

### Measured THD vs Input Level (1 kHz)

| Mode | Machine | THD @ 0 dB | THD @ +6 dB | E/O Ratio |
|------|---------|------------|-------------|-----------|
| **Master** | Ampex ATR-102 | 0.079% | 0.394% | 0.50 (odd-dominant) |
| **Tracks** | Studer A820 | 0.238% | 1.091% | 1.11 (even-dominant) |

### Frequency Response Tolerances (Per-Instance Randomized)

Based on manufacturer specifications:

| Machine | Low Shelf | High Shelf | Source |
|---------|-----------|------------|--------|
| **Ampex ATR-102** | ±0.05 dB @ 50 Hz | ±0.02 dB @ 15 kHz | "Most accurate analogue tape recorder ever produced" |
| **Studer A820** | ±0.15 dB @ 70 Hz | ±0.05 dB @ 12 kHz | Spec: ±1 dB 60-20 kHz @ 30 IPS |

Each plugin instance generates unique randomized tolerance values, simulating individual machine calibration.

---

## Technical Implementation

### Signal Flow

```
INPUT → Drive → 2x Upsample → Envelope Follower
                                    │
                    ┌───────────────┴───────────────┐
                    ↓                               ↓
                 HFCut                        Clean HF Path
                    │                         (Input - HFCut)
                    ↓                               │
              + DC Bias                             │
                    │                               │
                    ↓                               │
          Jiles-Atherton Core                       │
             (hysteresis)                           │
                    │                               │
                    ↓                               │
          Level-Scaled Cubic                        │
           (THD generation)                         │
                    │                               │
                    └───────────────┬───────────────┘
                                    ↓
                        Sum (saturated + cleanHF)
                                    ↓
                        Machine EQ (head bump)
                                    ↓
                        Dispersive Allpass (phase smear)
                                    ↓
                        DC Block → Azimuth Delay (R)
                                    ↓
                        2x Downsample
                                    ↓
                        Crosstalk (Studer) → Wow
                                    ↓
                        Tolerance EQ → Print-through (Studer)
                                    ↓
                               Volume → OUTPUT
```

**Latency:** ~7 samples @ 44.1 kHz (~0.16 ms)

---

### Saturation Architecture

Two complementary saturation layers with DC bias for even/odd harmonic control:

#### 1. Jiles-Atherton Hysteresis

Physics-based magnetic domain model implementing the differential equation:

```
dM/dH = (M_an - M) / (δk - α(M_an - M)) + c·dM_an/dH
```

Where:
- `M_an` = anhysteretic magnetization (Langevin function)
- `δ` = sign of dH/dt (direction of field change)
- `k` = coercivity parameter (27,500)
- `α` = mean field parameter (1.6×10⁻³)
- `c` = reversibility coefficient (0.98)

**Machine-specific blend:**
- Ampex: 6% J-A contribution
- Studer: 12% J-A contribution

#### 2. Level-Scaled Cubic Saturation

Pure cubic saturation produces THD ∝ amplitude² (slope 2.0 on log-log plot). Measured tape curves show steeper slopes. Level-scaled cubic achieves this:

```
effectiveA3 = a3_base × level^power
THD_slope = 2 + power
```

**Parameters:**

| Machine | a3 | power | input_bias | THD slope |
|---------|-----|-------|------------|-----------|
| Ampex ATR-102 | 0.0030 | 0.35 | 0.075 | 2.35 |
| Studer A820 | 0.0058 | 0.48 | 0.180 | 2.48 |

#### DC Bias for Even/Odd Control

The `input_bias` parameter shifts the signal on the saturation S-curve, creating asymmetric clipping that generates even harmonics:
- **0.075 bias** → E/O = 0.50 (odd-dominant, Ampex)
- **0.180 bias** → E/O = 1.11 (even-dominant, Studer)

---

### AC Bias Shielding (Parallel HF Path)

Real tape uses AC bias (~150-432 kHz oscillator) to linearize magnetic recording. High frequencies experience less distortion because bias keeps magnetic particles in their linear region.

**Implementation:** Parallel architecture where HF content bypasses saturation:

```
Input ──┬── HFCut ─────────→ Saturation ──┬── Sum → Output
        │                                  │
        └── (Input - HFCut) ───────────────┘
             Clean HF (no saturation)
```

**HF Saturation Reduction (dB relative to LF):**

| Machine | Bias Frequency | @ 10 kHz | @ 20 kHz |
|---------|---------------|----------|----------|
| Ampex ATR-102 | 432 kHz | -7 dB | -11 dB |
| Studer A820 | 153.6 kHz | -5 dB | -9 dB |

Curves derived from Quantegy GP9 specifications (370 Oe coercivity, +17.5 dB MOL).

---

### Machine EQ (Head Bump)

Modeled from Jack Endino measurements and published specifications at 30 IPS:

| Machine | Highpass | Head Bump | Notes |
|---------|----------|-----------|-------|
| **Ampex** | 16 Hz | +1.1 dB @ 40 Hz | -1.2 dB @ 20 Hz |
| **Studer** | 18 dB/oct @ 27-30 Hz | +1.1 dB @ 46 Hz, +1.2 dB @ 110 Hz | Dual bump, -9 dB @ 20 Hz |

---

### HF Phase Smear (Dispersive Allpass)

Head gap geometry creates frequency-dependent group delay via cascaded first-order allpass filters:

| Machine | Head Gap | Corner Frequency | Phase @ 20 kHz |
|---------|----------|------------------|----------------|
| Ampex ATR-102 | 0.25 µm (Flux Magnetics ceramic) | 10 kHz | ~15° |
| Studer A820 | 3 µm (1.317 playback head) | 2.8 kHz | ~45° |

---

### Stereo Processing

| Feature | Ampex ATR-102 | Studer A820 |
|---------|---------------|-------------|
| **Azimuth Delay** | 8 µs (R channel) | 12 µs (R channel) |
| **Crosstalk** | None | -55 dB mono bleed |
| **Wow Modulation** | Disabled (servo transport) | 0.02% @ 0.5-2 Hz |
| **Print-through** | None | -58 dB @ 65 ms pre-echo |

**Azimuth Implementation:** Thiran allpass interpolation for fractional delay (preserves flat magnitude response, no HF roll-off from linear interpolation artifacts).

---

### Oversampling

2× minimum-phase IIR oversampling. Low THD allows 2× (vs 4×/8× typical for physics-based tape emulations), reducing group delay artifacts.

Sessions at 96 kHz+ automatically bypass oversampling filters.

---

## Building

```bash
cd Plugin && mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release && make -j8
```

**Requirements:** CMake 3.22+, C++17, macOS 10.13+ (JUCE 8.0.4 fetched automatically)

---

## Project Structure

```
Ampex-ATR-102-Studer-A820/
├── Source/DSP/
│   ├── HybridTapeProcessor.cpp/h   # Main saturation engine
│   ├── JilesAthertonCore.h         # Physics-based hysteresis
│   ├── BiasShielding.cpp/h         # AC bias shielding curves
│   └── MachineEQ.cpp/h             # Head bump EQ
└── Plugin/Source/
    ├── PluginProcessor.cpp/h       # JUCE wrapper
    └── PluginEditor.cpp/h          # UI
```

---

## Changelog

### v1.2.0 (December 2025)
- Renamed from "Low THD Tape Sim" to "Ampex ATR-102 | Studer A820"
- Azimuth delay changed to Thiran allpass interpolation (eliminates ~1.3 dB HF roll-off from linear interpolation)
- Per-instance tolerance EQ based on manufacturer specifications

### v1.1.0 (December 2025)
- Fixed Jiles-Atherton slew limiting bug causing intermittent crackling
- High-frequency tolerance reduced to ±0.02-0.03 dB for tight L/R stereo matching
- Wow modulation depth corrected to 0.02% (was 0.04%)

---

## Credits

Developed by AudioBengineer

**DSP References:**
- Jiles-Atherton: "Theory of ferromagnetic hysteresis" (1986)
- Tape saturation: DAFx 2019 "Real-Time Physical Modelling for Analog Tape Machines"
- Machine EQ: Jack Endino frequency response measurements
