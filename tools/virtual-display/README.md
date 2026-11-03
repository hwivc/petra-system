# Petra Virtual Display

Desktop virtual display window mimicking the 800×480 ePaper panel (`firmware/epaper-panel`) **without needing any firmware changes**.

Petra automatically finds it over the LAN as **`epaper.local` (port 80)** and sends photos, while Xiaozhi AI tools connect through the **MCP WebSocket endpoint**.

---

## What's New: Soft Modern Paper Presentation

Optimized for modern PC monitors, laptops, and projectors with high refresh rates:

- **Soft Natural Book Paper**: Warm natural paper canvas (`#FAF9F5`) and soft carbon charcoal ink (`#1C1E21`), eliminating harsh LCD glare while preserving the calm e-paper feel.
- **60fps Smooth Ink Settle Transition**: Replaces the jarring physical black-blink with a silky 60fps ink-dissolve animation (~140ms).
- **Lithographic Photo Smoothing**: Sub-pixel micro-filtering blends Petra's 1-bit dithered camera frames into smooth, authentic fine-print illustrations.
- **Dual-Mode Theme Switcher**: Toggle anytime with one click between **🌿 Soft Mode** (modern book presentation) and **⚡ E-Ink Mode** (strict hardware simulation).
- **Window Maximization & Fullscreen (F11)**: Proportional auto-scaling preserving the 5:3 e-paper aspect ratio.

---

## Interactive STEM Education Tools

- `show_diagram(title, content)`: Renders flowcharts, circuits (`Power 3.3V -> Resistor -> LED -> GND`), and system diagrams.
- `show_card(title, lines, verdict, badge_text)`: Interactive quiz cards (**CORRECT! 🎉** / **TRY AGAIN 💡**), recycling badges, and concept summaries.
- `show_message(text)`: Displays text or automatically formats code/formula blocks in Consolas monospace syntax boxes.
- `show_steps(title, steps)`: Sequential procedure workflow steps.
- `show_qr(url, label)`: Displays large QR codes for mentorship, website, WhatsApp community, etc.
- `clear_display()`: Resets display to welcome rotation.
- `POST /photo`: Receives 800×400 raw 1-bit bitmaps directly from Petra with art styles and frames (`classic`, `polaroid`, `magazine`, `hero`, `wanted`, `comic`, `film`, `event`).
- `GET /postcard.png` & `GET /`: Digital copy download portal with a mobile-friendly download page and direct PNG export.

---

## Quick Start

### 1. Launch the Virtual Screen

Double-click `start_virtual_display.bat` or run in PowerShell:
```powershell
cd C:\petra-system\tools\virtual-display
python run_virtual_display.py
```

### 2. Connect to Xiaozhi MCP (First-time setup)

1. Open **[xiaozhi.me](https://xiaozhi.me)** → **Console** → **Agents** → **Petra**.
2. Copy your **MCP endpoint** (e.g. `wss://api.xiaozhi.me/mcp/...`).
3. In the Virtual Display window, click **⚙ Configure MCP** and paste the URL.
4. The status indicator will switch to **Xiaozhi MCP: Online**.
5. Petra can now control the screen through voice commands!

---

## Shortcuts & Controls

| Control | Action |
|---|---|
| **F11** or **⛶ Fullscreen** | Toggle Fullscreen / Presentation mode |
| **Escape** | Exit Fullscreen mode |
| **🌿 Soft Mode / ⚡ E-Ink Mode** | Toggle between soft book paper and classic hardware simulation |
| **FX Dropdown** | Choose transition: `fade` (60fps smooth ink fade), `flash` (e-ink blink), or `none` |
| **📷 Photo** | Test camera capture with postcard frame |
| **📊 Diagram** | Test circuit or code flowchart diagram |
| **📋 Card / Quiz** | Test educational concept card or quiz |
| **🔢 Steps** | Test numbered workflow steps |
| **🔗 QR** | Test QR code display |
| **🏠 Welcome** | Return to STEM Lab welcome screen |
