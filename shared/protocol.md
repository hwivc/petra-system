# Local protocol: Petra → ePaper panel

Both devices sit on the same Wi-Fi. The panel advertises itself with mDNS as `epaper.local` (port 80).
Nothing here goes to the cloud.

## POST /photo
- Body: raw 1-bit bitmap, **800 × 400**, row-major, MSB first, **1 = black**, 100 bytes per row → **40,000 bytes**.
  Petra has already applied the art style (photo, sketch, comic, woodcut, engraving, stipple).
- Header `Content-Length: 40000` (required).
- Header `X-Caption`: URL-encoded UTF-8 caption, max 80 chars (optional).
- Header `X-Frame`: `classic`, `polaroid`, `magazine`, `hero`, `wanted`, `comic`, `film` or `event`
  (optional, default `classic`). The panel draws the frame, caption and logos.
- Responses: `200 ok` · `400 bad size` · `503 busy` (panel is refreshing; retry after ~3 s).

## GET /status
- `200` with JSON `{"ok":true,"busy":false}`.

## Digital copy (download hotspot)
- After showing a photo, the panel opens an open hotspot `PlastiBytes-Photo-XXXX` (it stays on the
  venue Wi-Fi at the same time) and shows its Wi-Fi QR code in the bottom-right corner.
- Phones that join get a captive page at `http://192.168.4.1/` with the postcard (`/postcard.png`,
  800×480 1-bit PNG) and a Download button.
- The hotspot closes 1 minute after a download, when the phone leaves after downloading, after
  3 minutes, or when something else is shown. The photo is then deleted from memory.

## Cloud tools on the panel (xiaozhi.me MCP endpoint)
`show_message(text)`, `show_card(title, lines, verdict)`, `show_steps(title, steps)`,
`show_qr(url, label)`, `clear_display()`.
