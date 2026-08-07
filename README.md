www# Quantum Atom Simulator (C / raylib)

A real-time 3D atomic visualizer built with C and [raylib](https://www.raylib.com/). It renders all 36 elements from Hydrogen to Krypton in three different visualization modes — classic Bohr orbits, quantum-mechanical probability clouds, and orbital shape (s/p/d/f) lobes — complete with an animated plasma nucleus, electron excitation/photon emission, and a live emission spectrum readout.

Part of the [Black-Mozart Interactive Lab](https://black-mozart.github.io/) 

## Project Structure

```
quantum-atom-sim-c/
├── quantum_atom.c   ← full engine (~1100 lines)
├── Makefile         ← build system
└── README.md
```

---

## Table of Contents

- [Features](#features)
- [Requirements](#requirements)
- [Building](#building)
  - [Linux](#linux)
  - [macOS](#macos)
  - [Windows](#windows)
- [Running](#running)
- [Controls](#controls)
- [Visualization Modes](#visualization-modes)
- [How It Works](#how-it-works)
  - [Elements & Electron Configuration](#elements--electron-configuration)
  - [Wavefunction Math](#wavefunction-math)
  - [Nucleus](#nucleus)
  - [Electron Excitation & Spectral Lines](#electron-excitation--spectral-lines)
  - [Rendering Pipeline](#rendering-pipeline)
- [Code Structure](#code-structure)
- [Performance Notes](#performance-notes)
- [Known Fixes in This Version](#known-fixes-in-this-version)
- [Troubleshooting](#troubleshooting)
- [Possible Extensions](#possible-extensions)
- [License](#license)

---

## Features

- **36 elements** (H → Kr) with accurate ground-state electron configurations, atomic mass, and period.
- **Three visualization modes**: Bohr shell model, quantum probability cloud, and orbital lobe shapes.
- **Interactive electron excitation** — click an electron to bump it up an energy level and emit a photon, which is logged as a colored line on a live emission spectrum bar.
- **Animated nucleus** with a pulsing plasma corona, glowing proton/neutron nucleons, and rotating arc rings.
- **Camera-facing billboard particles** for the electron trails, probability cloud, orbital lobes, and background starfield — all rendered as lightweight `RL_TRIANGLES` quads instead of full 3D spheres for performance.
- **Post-processing shader**: additive bloom (luminance-gated, wide Gaussian-style kernel), chromatic aberration, vignette, and Reinhard tonemapping.
- **Fully responsive UI panel** — resizes and repositions itself based on the actual window resolution, not a hardcoded 1920×1080.
- **Element transition flash** and other subtle UI polish (progress bar, period indicator dots, hover states on buttons).
- Toggleable shell visibility (K/L/M/N), auto-spin camera, and bloom on/off.

---

## Requirements

- **C compiler** — GCC, Clang, or MSVC (C99 or later).
- **[raylib](https://github.com/raysoft/raylib)** — version 5.x or later. The code specifically works around a change in raylib 6's `rlgl` module (see [Known Fixes](#known-fixes-in-this-version)), so raylib 5+ is recommended.
- **GPU with OpenGL 3.3 support** — the post-processing shader is written in GLSL `#version 330`.
- Standard C library headers only (`stdio.h`, `math.h`, `stdlib.h`, `string.h`, `time.h`, `float.h`) — no other third-party dependencies beyond raylib.

---

## Building

The included `Makefile` auto-detects your platform (Linux / macOS / Windows) and links the right raylib flags for you:

```bash
make          # builds ./quantum_atom (or quantum_atom.exe on Windows)
make run      # build and run
make debug    # build with -g -O0 -DDEBUG
make clean    # remove build artifacts
```

If raylib is built from source rather than installed system-wide, point the Makefile at it:

```bash
make RAYLIB_DIR=/path/to/raylib/src
```

### Manual compilation

If you'd rather invoke the compiler directly instead of using `make`:

### Linux

Install raylib via your package manager or build it from source, then:

```bash
gcc quantum_atom.c -o quantum_atom \
    -lraylib -lGL -lm -lpthread -ldl -lrt -lX11
```

If you built raylib from source and it isn't in your default library path:

```bash
gcc quantum_atom.c -o quantum_atom \
    -I/path/to/raylib/src -L/path/to/raylib/src \
    -lraylib -lGL -lm -lpthread -ldl -lrt -lX11
```

### macOS

```bash
gcc quantum_atom.c -o quantum_atom \
    -lraylib -framework OpenGL -framework Cocoa \
    -framework IOKit -framework CoreVideo
```

(Using Homebrew's raylib: `brew install raylib` first.)

### Windows (MinGW)

```bash
gcc quantum_atom.c -o quantum_atom.exe ^
    -lraylib -lopengl32 -lgdi32 -lwinmm
```

Or use the raylib project template / `w64devkit` that ships with prebuilt raylib binaries and Makefiles, and just drop this file in as `main.c`.

---

## Running

```bash
./quantum_atom        # Linux / macOS
quantum_atom.exe       # Windows
```

The window opens maximized by default (`FLAG_WINDOW_MAXIMIZED`) and is resizable; the render target and shader resolution uniform update automatically on resize.

---

## Controls

| Input | Action |
|---|---|
| Mouse drag (left button) | Rotate the atom / camera |
| Mouse scroll | Zoom in / out |
| Click an electron | Excite it → emits a photon |
| `←` / `→` | Previous / next element |
| `↑` / `↓` | Jump 10 elements forward / back |
| `1` | Switch to Bohr orbit mode |
| `2` | Switch to quantum probability cloud mode |
| `3` | Switch to orbital shape (s/p/d/f) mode |
| `K` `L` `M` `N` | Toggle visibility of the corresponding electron shell |
| `A` | Toggle camera auto-spin |
| `R` | Reset camera to default position and re-enable auto-spin |
| `B` | Toggle bloom / post-processing shader |

All of these are also available as clickable UI elements in the right-hand panel (element arrows, mode buttons, shell toggle buttons).

---

## Visualization Modes

1. **Bohr Model (`1`)** — Electrons orbit the nucleus on fixed circular shells (K, L, M, N), each with a colored glow trail, additive halo, and a small excitation arc animation when clicked. This is the classic (if physically simplified) "planetary" atom picture.

2. **Quantum Probability Cloud (`2`)** — For each occupied orbital, ~3000 points are stochastically sampled from the product of the radial probability density `|R(r)|²·r²` and an angular probability term, using rejection sampling. The result is a diffuse, breathing point cloud that approximates the electron's true probability distribution.

3. **Orbital Shapes (`3`)** — Renders idealized s/p/d/f lobe geometry per orbital, colored by the sign of the wavefunction (warm colors for the positive lobe, cool colors for the negative lobe), slowly counter-rotating for visual clarity.

---

## How It Works

### Elements & Electron Configuration

The `ELEMS[]` table hardcodes atomic number, symbol, name, atomic mass, ground-state electron configuration across 8 orbital slots (`1s 2s 2p 3s 3p 3d 4s 4p`), and period for all 36 elements from Hydrogen to Krypton — including the two 4th-period exceptions (Chromium and Copper) that fill `4s¹ 3d⁵`/`4s¹ 3d¹⁰` instead of the naive Aufbau order.

### Wavefunction Math

- `radialProb(n, l, rho)` — a simplified, unnormalized hydrogen-like radial probability function `|R_nl(ρ)|²·ρ²`, piecewise by `n·10+l` for the orbitals used in this simulator (1s through 4p).
- `angularProb(l, variant, theta, phi)` — approximates the angular parts of real spherical harmonics (s, p, d, f shapes) for constructing both the probability cloud and the lobe geometry.
- `buildCloud(oi)` uses rejection sampling: it first scans the radial function on a 600-step grid to estimate its maximum, then draws random `(ρ, θ, φ)` triples and accepts/rejects them proportionally to the combined radial × angular probability, until `N_CLOUD` (3000) accepted points are collected per orbital (or an attempt cap is hit).
- `buildShape(oi)` generates idealized lobe geometry per angular-momentum quantum number `l` (sphere for s, dumbbells for p, cloverleaf/torus shapes for d, more complex nodal patterns for f), tagging each point with a `+1`/`-1` sign for phase coloring.

### Nucleus

`buildNucleus(Z)` estimates neutron count as `round(Z × 1.2)`, places protons and neutrons at random positions inside a sphere (rejection-sampled to stay within the unit ball, then scaled by `2.4 + cbrt(nucleon_count) × 1.9`), and gives each nucleon a random oscillation phase/amplitude for a "wobbling plasma" animation. Protons render in warm orange-red, neutrons in cool blue.

### Electron Excitation & Spectral Lines

Clicking an electron (via ray-sphere intersection against each electron's current 3D position) triggers `exciteElectron()`:

1. Sets the electron's excitation level and starts a decay timer.
2. Computes an approximate transition energy using the hydrogen-like formula `ΔE = 13.6 eV × (1/n₁² − 1/n₂²)`, where `n₂ = min(n₁+1, 4)`.
3. Converts that energy to a wavelength via `λ = 1240 / ΔE` (nm·eV).
4. Spawns a photon particle (an expanding wireframe/ring sphere) colored by that wavelength (`wl2col`, a rough visible-spectrum-to-RGB approximation covering 380–700 nm).
5. Appends a corresponding line to `specLines[]`, which is drawn on the emission-spectrum bar at the bottom of the side panel and fades out over time.

### Rendering Pipeline

1. **3D pass** — stars, shell rings, the active visualization mode (Bohr/cloud/shapes), nucleus, and photons are drawn inside `BeginMode3D`/`EndMode3D`, optionally into an off-screen `RenderTexture2D` if bloom is enabled.
2. **Post-process pass** — if bloom is on, the render texture is drawn full-screen through `FS_BLOOM`, a single fragment shader that applies chromatic aberration, an 11×11 luminance-gated Gaussian-ish bloom kernel, a radial vignette, and Reinhard tonemapping with a gamma lift.
3. **2D overlay pass** — the side panel (`drawPanel`) and HUD (`drawHUD`, FPS counter + excitation/element-change screen flashes) are drawn last, directly to the screen, unaffected by the post-process shader.

Camera-facing billboard quads (`rlBillboard`) are used throughout for electron trails, cloud points, orbital-shape points, and stars — computed from per-frame `gCamRight`/`gCamUp` vectors derived from the camera's forward vector, and batched inside single `rlBegin(RL_TRIANGLES)/rlEnd()` blocks for performance.

---

## Code Structure

| Section | Purpose |
|---|---|
| Constants & tables | `MAX_ELECTRONS`, `SHELL_R`, `ORB_COL`/`ORB_N`/`ORB_L`/`ORB_SH`, orbital metadata |
| `FS_BLOOM` | GLSL fragment shader source for post-processing |
| `ELEMS[]` | Static periodic table data (Z 1–36) |
| Types | `Electron`, `Photon`, `Nucleon`, `SpecLine` structs |
| Globals | Simulation state: current element, camera, view mode, UI state, particle buffers |
| Math helpers | `randf`, `clampf`, `radialProb`, `angularProb` |
| `buildCloud` / `buildShape` / `buildNucleus` / `buildAtom` | Procedural geometry generation, called once per orbital (cloud/shape at startup) and once per element change (nucleus/atom) |
| `getCamera`, `electronPos3D`, `rlBillboard` | Camera and per-frame geometry helpers |
| `wl2col`, `drawSpectrum` | Wavelength-to-color mapping and spectrum bar rendering |
| `drawNucleus`, `drawShellRings`, `drawElectronsBohr`, `drawCloud`, `drawOrbitalShapes`, `drawPhotons`, `drawStars` | Per-mode 3D draw routines |
| `exciteElectron` | Click-to-excite logic and photon/spectral-line spawning |
| `drawPanel`, `drawHUD` | 2D UI overlay |
| `initStars` | One-time starfield generation |
| `main` | Window/shader setup, input handling, per-frame update, and the render pipeline described above |

---

## Performance Notes

- Probability cloud and orbital shape point sets (`N_CLOUD = 3000`, `N_SHAPE = 2000` per orbital, across up to 8 orbitals) are precomputed once at startup — they are **not** regenerated per frame or per element change, only re-positioned/recolored.
- Particles are rendered as billboarded triangle pairs instead of `DrawSphereEx` calls or raylib's `RL_POINTS` mode (which is not supported by `rlgl` in raylib 6 and previously caused vertex-buffer corruption / crashes — see below).
- The background starfield (`BG_STARS = 600`) uses the same billboard technique instead of 600 individual `DrawSphereEx` calls, which was roughly an order of magnitude slower in the original version of this program.
- `fpsSmooth` provides a lightly damped FPS readout in the top-left HUD, color-coded green (≥55 fps) / orange (≥30 fps) / red (below).

---

## Known Fixes in This Version

The header comment documents six fixes applied over an earlier version of this simulator:

1. `SW`/`SH` are read from the actual window size (`GetScreenWidth/Height`) **before** `LoadRenderTexture` is called, instead of using a hardcoded 1920×1080 that mismatched real screen size.
2. `RL_POINTS` (mode `0x0000`) is not implemented in raylib 6's `rlgl`, which corrupted the vertex buffer and could crash the program; all point-like particles now use small `RL_TRIANGLES` billboard quads instead.
3. Electron click detection previously set the `dragging` flag before checking for an electron hit, which could swallow clicks; the check order was fixed so click detection happens first.
4. The element-configuration string buffer (`cfgstr`) was widened to 128 bytes; the original 64-byte buffer could overflow given certain UTF‑8 superscript characters (now using a plain-ASCII format like `1s2 2s2 2p2` instead).
5. The side panel layout was made compact and responsive so it fits any screen/window height rather than assuming a fixed resolution.
6. The starfield was switched from 2000 individual `DrawSphereEx` calls to lightweight billboard triangles, roughly a 10× speedup.

---

## Troubleshooting

- **Blank/black window or shader errors**: confirm your GPU/driver supports OpenGL 3.3+. If `LoadShaderFromMemory` fails, the program automatically falls back to `shOK = false` / `useBloom = false` and renders without post-processing.
- **Linker errors on Linux** (`undefined reference to ...`): make sure all the required system libraries are linked (`-lGL -lm -lpthread -ldl -lrt -lX11`), and that raylib itself is installed/visible to the linker.
- **Choppy performance**: try `B` to disable bloom (the shader pass is the most expensive part of the frame), or reduce `N_CLOUD` / `N_SHAPE` / `BG_STARS` constants and rebuild.
- **UI panel looks cut off**: the panel is fixed at 220px wide and anchored to the right edge of whatever the current window size is; this is expected on very small windows.

---

## Possible Extensions

- Extend `ELEMS[]` beyond Krypton (Z=36) into period 5+.
- Add proper normalization/units to the radial and angular probability functions for closer physical accuracy.
- Support multiple simultaneously excited electrons with distinct decay timers already implemented per-electron (`excLvl`/`excTimer`) — the current click logic just excites one at a time, but this could be exposed further in the UI.
- Add a settings panel for particle counts (`N_CLOUD`, `N_SHAPE`, `BG_STARS`) without recompiling.
- Export the current emission spectrum or electron configuration as an image/text file.

---

## License

No license is specified in the source. Add one (e.g. MIT) if you intend to distribute or open-source this project.

---

## Credits

Part of the **[Black-Mozart Interactive Lab](https://black-mozart.github.io/)**.

Built with [raylib](https://www.raylib.com/).


