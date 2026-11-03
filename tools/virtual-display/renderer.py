"""
ePaper Display Renderer for Petra Virtual Screen
------------------------------------------------
Enhanced with Soft Book-Grade Presentation Mode:
- Warm paper canvas (#FAF9F5) and soft carbon charcoal ink (#1C1E21)
- Sub-pixel anti-aliased typography and rounded curves
- Lithographic photo smoothing for 1-bit camera bitmaps
- Dual-mode support: "soft" (modern PC presentation) and "hardware" (strict e-paper simulation)
- Interactive STEM education tools: show_diagram, show_card (quizzes), show_message (code), show_steps
"""

import math
import os
import re
import qrcode
from io import BytesIO
from PIL import Image, ImageDraw, ImageFont, ImageFilter, ImageOps

import logos_data

SCREEN_WIDTH = 800
SCREEN_HEIGHT = 480
PHOTO_WIDTH = 800
PHOTO_HEIGHT = 400
PHOTO_BYTES = 40000

# Available themes
THEMES = {
    "soft": {
        "name": "soft",
        "bg": (250, 249, 245),          # Natural warm book paper (#FAF9F5)
        "ink": (28, 30, 33),            # Soft carbon charcoal (#1C1E21)
        "white_ink": (250, 249, 245),
        "light_tint": (240, 238, 231),  # Subtle card fill
        "border": (45, 48, 52),
        "smooth_photos": True,
    },
    "hardware": {
        "name": "hardware",
        "bg": (246, 245, 240),          # Physical e-paper white
        "ink": (18, 18, 18),            # Deep black
        "white_ink": (246, 245, 240),
        "light_tint": (230, 228, 222),
        "border": (18, 18, 18),
        "smooth_photos": False,
    }
}

ACTIVE_THEME = "soft"
BG_COLOR = THEMES[ACTIVE_THEME]["bg"]
INK_COLOR = THEMES[ACTIVE_THEME]["ink"]
WHITE_INK = THEMES[ACTIVE_THEME]["white_ink"]
LIGHT_TINT = THEMES[ACTIVE_THEME]["light_tint"]
BORDER_COLOR = THEMES[ACTIVE_THEME]["border"]
SMOOTH_PHOTOS = THEMES[ACTIVE_THEME]["smooth_photos"]


def set_theme(theme_name):
    global ACTIVE_THEME, BG_COLOR, INK_COLOR, WHITE_INK, LIGHT_TINT, BORDER_COLOR, SMOOTH_PHOTOS
    if theme_name in THEMES:
        ACTIVE_THEME = theme_name
        t = THEMES[theme_name]
        BG_COLOR = t["bg"]
        INK_COLOR = t["ink"]
        WHITE_INK = t["white_ink"]
        LIGHT_TINT = t["light_tint"]
        BORDER_COLOR = t["border"]
        SMOOTH_PHOTOS = t["smooth_photos"]
        logos_data.reload_logos(INK_COLOR)


# Fonts directory on Windows
FONTS_DIR = r"C:\Windows\Fonts"

def _load_font(filename, size, fallback="arial.ttf"):
    path = os.path.join(FONTS_DIR, filename)
    if not os.path.exists(path):
        path = os.path.join(FONTS_DIR, fallback)
    try:
        return ImageFont.truetype(path, size)
    except Exception:
        return ImageFont.load_default()

# Anti-aliased TrueType fonts
FONT_TITLE_BOLD = _load_font("arialbd.ttf", 30)
FONT_SUBTITLE_BOLD = _load_font("arialbd.ttf", 23)
FONT_BODY = _load_font("arial.ttf", 21)
FONT_BODY_BOLD = _load_font("arialbd.ttf", 19)
FONT_SMALL = _load_font("arial.ttf", 16)
FONT_SMALL_BOLD = _load_font("arialbd.ttf", 16)
FONT_TINY_BOLD = _load_font("arialbd.ttf", 13)

FONT_SERIF_TITLE = _load_font("timesbd.ttf", 34)
FONT_SERIF_SUB = _load_font("timesbd.ttf", 23)
FONT_SERIF_ITALIC = _load_font("timesbi.ttf", 19)
FONT_SERIF_SMALL_ITALIC = _load_font("timesi.ttf", 16)

FONT_COMIC_TITLE = _load_font("comicbd.ttf", 28, fallback="arialbd.ttf")
FONT_COMIC_SUB = _load_font("comic.ttf", 20, fallback="arial.ttf")

FONT_MONO = _load_font("consola.ttf", 18, fallback="arial.ttf")
FONT_MONO_BOLD = _load_font("consolab.ttf", 18, fallback="arialbd.ttf")
FONT_MONO_SMALL = _load_font("consola.ttf", 15, fallback="arial.ttf")


def new_canvas():
    """Create a new 800x480 canvas filled with paper background."""
    return Image.new("RGB", (SCREEN_WIDTH, SCREEN_HEIGHT), BG_COLOR)


def wrap_text(draw, text, font, max_width):
    """Word wrap text to fit within max_width."""
    lines = []
    paragraphs = text.split("\n")
    for para in paragraphs:
        if not para:
            lines.append("")
            continue
        words = para.split(" ")
        current_line = ""
        for word in words:
            candidate = f"{current_line} {word}".strip()
            bbox = draw.textbbox((0, 0), candidate, font=font)
            w = bbox[2] - bbox[0]
            if w <= max_width or not current_line:
                current_line = candidate
            else:
                lines.append(current_line)
                current_line = word
        if current_line:
            lines.append(current_line)
    return lines


def fit_text(draw, text, font, max_width):
    """Truncate text with ... if it exceeds max_width."""
    if not text:
        return ""
    bbox = draw.textbbox((0, 0), text, font=font)
    if bbox[2] - bbox[0] <= max_width:
        return text
    t = text
    while len(t) > 3:
        t = t[:-4] + "..."
        b = draw.textbbox((0, 0), t, font=font)
        if b[2] - b[0] <= max_width:
            return t
    return text[:3] + "..."


def draw_header(draw, title):
    """Top black header banner matching firmware."""
    draw.rectangle([0, 0, SCREEN_WIDTH, 70], fill=INK_COLOR)
    fitted = fit_text(draw, title, FONT_TITLE_BOLD, SCREEN_WIDTH - 40)
    bbox = draw.textbbox((0, 0), fitted, font=FONT_TITLE_BOLD)
    w, h = bbox[2] - bbox[0], bbox[3] - bbox[1]
    draw.text(((SCREEN_WIDTH - w) // 2, (70 - h) // 2 - 2), fitted, fill=WHITE_INK, font=FONT_TITLE_BOLD)


def draw_footer(canvas, draw, status_text='Say "Hey Petra"'):
    """Footer bar with PlastiBytes logo and right status text."""
    margin = 30
    draw.line([margin, SCREEN_HEIGHT - 48, SCREEN_WIDTH - margin, SCREEN_HEIGHT - 48], fill=BORDER_COLOR, width=1)
    
    # PlastiBytes small logo
    canvas.paste(logos_data.LOGO_PB_SMALL_IMG, (margin, SCREEN_HEIGHT - 41), logos_data.LOGO_PB_SMALL_IMG)
    
    # Right-aligned status
    bbox = draw.textbbox((0, 0), status_text, font=FONT_SMALL)
    w, h = bbox[2] - bbox[0], bbox[3] - bbox[1]
    draw.text((SCREEN_WIDTH - margin - w, SCREEN_HEIGHT - 24 - h // 2), status_text, fill=INK_COLOR, font=FONT_SMALL)


def generate_qr(text, size):
    """Generate high-contrast QR code scaled cleanly to size x size."""
    qr = qrcode.QRCode(
        version=None,
        error_correction=qrcode.constants.ERROR_CORRECT_M,
        box_size=10,
        border=1,
    )
    qr.add_data(text)
    qr.make(fit=True)
    img = qr.make_image(fill_color=INK_COLOR, back_color=BG_COLOR).convert("RGB")
    return img.resize((size, size), Image.Resampling.NEAREST)


def draw_download_box(canvas, draw, qr_text):
    """QR box in bottom right corner for downloading postcard."""
    x, y, w, h = 662, 332, 132, 144
    draw.rounded_rectangle([x - 4, y - 4, x + w + 4, y + h + 4], radius=8, fill=BG_COLOR, outline=BORDER_COLOR, width=2)
    title = "Scan: your copy"
    bbox = draw.textbbox((0, 0), title, font=FONT_TINY_BOLD)
    tw = bbox[2] - bbox[0]
    draw.text((x + (w - tw) // 2, y + 2), title, fill=INK_COLOR, font=FONT_TINY_BOLD)
    qr_img = generate_qr(qr_text, 120)
    canvas.paste(qr_img, (x + 6, y + 20))


def draw_event_badge(canvas, draw, x, y, event_name):
    """CITS event badge pill."""
    w, h = 240, logos_data.LOGO_CITS_H + 10
    draw.rounded_rectangle([x, y, x + w, y + h], radius=10, fill=BG_COLOR, outline=BORDER_COLOR, width=2)
    canvas.paste(logos_data.LOGO_CITS_IMG, (x + 6, y + 5), logos_data.LOGO_CITS_IMG)
    text = fit_text(draw, event_name, FONT_SMALL_BOLD, w - logos_data.LOGO_CITS_W - 20)
    draw.text((x + logos_data.LOGO_CITS_W + 14, y + h // 2 - 8), text, fill=INK_COLOR, font=FONT_SMALL_BOLD)


# ---------------------------------------------------------------- Idle Screens

def render_welcome(status="Online", event_name="CITS26 - Yaounde"):
    canvas = new_canvas()
    draw = ImageDraw.Draw(canvas)
    
    # PlastiBytes large logo
    canvas.paste(logos_data.LOGO_PB_LARGE_IMG, ((SCREEN_WIDTH - logos_data.LOGO_PB_LARGE_W) // 2, 22), logos_data.LOGO_PB_LARGE_IMG)
    
    # Title
    t1 = 'Petra: STEM Teacher & Mentor'
    b1 = draw.textbbox((0, 0), t1, font=FONT_TITLE_BOLD)
    draw.text(((SCREEN_WIDTH - (b1[2] - b1[0])) // 2, 135), t1, fill=INK_COLOR, font=FONT_TITLE_BOLD)
    
    # Subtitle
    t2 = 'Say "Hey Petra" - Learn Electronics, Robotics & 3D Printing'
    b2 = draw.textbbox((0, 0), t2, font=FONT_SUBTITLE_BOLD)
    draw.text(((SCREEN_WIDTH - (b2[2] - b2[0])) // 2, 185), t2, fill=INK_COLOR, font=FONT_SUBTITLE_BOLD)
    
    # 4 step rounded boxes with arrows
    steps = ["Learn Circuit", "Write Code", "3D Print", "Build Project"]
    box_w, box_h, gap, y = 160, 60, 30, 245
    start_x = (SCREEN_WIDTH - (4 * box_w + 3 * gap)) // 2
    for i, s in enumerate(steps):
        bx = start_x + i * (box_w + gap)
        draw.rounded_rectangle([bx, y, bx + box_w, y + box_h], radius=10, fill=LIGHT_TINT, outline=BORDER_COLOR, width=2)
        sb = draw.textbbox((0, 0), s, font=FONT_BODY_BOLD)
        draw.text((bx + (box_w - (sb[2] - sb[0])) // 2, y + (box_h - (sb[3] - sb[1])) // 2 - 2), s, fill=INK_COLOR, font=FONT_BODY_BOLD)
        if i < 3:
            ax = bx + box_w + 4
            ay = y + box_h // 2
            draw.polygon([(ax, ay - 9), (ax, ay + 9), (ax + gap - 8, ay)], fill=BORDER_COLOR)
            
    # Suggestions text
    t3 = 'Try: "How does an LED work?" - "Take my picture" - "Test my knowledge on circuits"'
    b3 = draw.textbbox((0, 0), t3, font=FONT_SMALL)
    draw.text(((SCREEN_WIDTH - (b3[2] - b3[0])) // 2, 355), t3, fill=INK_COLOR, font=FONT_SMALL)
    
    draw_footer(canvas, draw, status)
    return canvas


def render_qr_screen(url, label, event_name="CITS26 - Yaounde"):
    canvas = new_canvas()
    draw = ImageDraw.Draw(canvas)
    
    # Large QR code on left
    qr_img = generate_qr(url, 340)
    canvas.paste(qr_img, (30, 40))
    
    # Right panel
    tx = 30 + 370
    tw = SCREEN_WIDTH - tx - 30
    canvas.paste(logos_data.LOGO_PB_SMALL_IMG, (tx, 50), logos_data.LOGO_PB_SMALL_IMG)
    draw.text((tx, 110), "Scan me!", fill=INK_COLOR, font=FONT_SUBTITLE_BOLD)
    
    # Wrapped label
    lines = wrap_text(draw, label, FONT_BODY, tw)
    curr_y = 165
    for line in lines:
        if curr_y > 330:
            break
        draw.text((tx, curr_y), line, fill=INK_COLOR, font=FONT_BODY)
        curr_y += 32
        
    clean_url = url.replace("https://", "").replace("http://", "")
    draw.text((tx, 360), fit_text(draw, clean_url, FONT_SMALL_BOLD, tw), fill=INK_COLOR, font=FONT_SMALL_BOLD)
    
    draw_footer(canvas, draw, event_name)
    return canvas


def render_event_screen(status="Online", event_name="CITS26 - Yaounde"):
    canvas = new_canvas()
    draw = ImageDraw.Draw(canvas)
    
    canvas.paste(logos_data.LOGO_CITS_IMG, (30, 30), logos_data.LOGO_CITS_IMG)
    draw.text((30 + logos_data.LOGO_CITS_W + 20, 45), "Welcome to CITS26!", fill=INK_COLOR, font=FONT_TITLE_BOLD)
    draw.text((30, 140), "Cameroon International Tech Summit", fill=INK_COLOR, font=FONT_SUBTITLE_BOLD)
    draw.text((30, 185), "15-17 Oct 2026  -  Palais des Congres, Yaounde", fill=INK_COLOR, font=FONT_BODY)
    draw.text((30, 245), "Be Bold. Limitless. Visionary. Resilient.", fill=INK_COLOR, font=FONT_SUBTITLE_BOLD)
    draw.text((30, 310), 'Say "Hey Petra, take my picture" for a', fill=INK_COLOR, font=FONT_BODY)
    draw.text((30, 345), "free CITS26 postcard!", fill=INK_COLOR, font=FONT_BODY)
    
    draw_footer(canvas, draw, status)
    return canvas


# ---------------------------------------------------------------- AI Tools

def render_message(text, status='Say "Hey Petra"'):
    """
    Renders messages. If text contains code or formulas, renders
    in a clean syntax code box with monospace font and line numbers.
    """
    canvas = new_canvas()
    draw = ImageDraw.Draw(canvas)
    
    # Detect code block or multi-line syntax
    is_code = False
    clean_code = text.strip()
    if clean_code.startswith("```") and clean_code.endswith("```"):
        is_code = True
        first_newline = clean_code.find("\n")
        if first_newline != -1:
            clean_code = clean_code[first_newline + 1:-3].strip()
        else:
            clean_code = clean_code[3:-3].strip()
    elif "\n" in text and any(kw in text for kw in ("void ", "def ", "int ", "digitalWrite", "pinMode", "import ", "const ")):
        is_code = True

    if is_code:
        draw_header(draw, "Code Snippet / Formula")
        bx, by, bw, bh = 30, 90, SCREEN_WIDTH - 60, SCREEN_HEIGHT - 160
        draw.rounded_rectangle([bx, by, bx + bw, by + bh], radius=10, fill=LIGHT_TINT, outline=BORDER_COLOR, width=2)
        # Tab bar
        draw.rounded_rectangle([bx, by, bx + bw, by + 32], radius=8, fill=INK_COLOR)
        draw.text((bx + 14, by + 6), "syntax // petra-code", fill=WHITE_INK, font=FONT_MONO_BOLD)
        
        # Render code lines with line numbers
        code_lines = clean_code.split("\n")[:8]
        cy = by + 45
        for idx, cl in enumerate(code_lines):
            num_str = f"{idx + 1:2d} |"
            draw.text((bx + 14, cy), num_str, fill="#777777", font=FONT_MONO_SMALL)
            draw.text((bx + 60, cy), cl[:58], fill=INK_COLOR, font=FONT_MONO)
            cy += 28
    else:
        draw_header(draw, "Petra says")
        box_w, box_h = SCREEN_WIDTH - 60, SCREEN_HEIGHT - 160
        lines = wrap_text(draw, text, FONT_SUBTITLE_BOLD, box_w)
        line_h = 36
        total_h = len(lines) * line_h
        start_y = 90 + max(0, (box_h - total_h) // 2)
        for i, line in enumerate(lines[:6]):
            bb = draw.textbbox((0, 0), line, font=FONT_SUBTITLE_BOLD)
            draw.text(((SCREEN_WIDTH - (bb[2] - bb[0])) // 2, start_y + i * line_h), line, fill=INK_COLOR, font=FONT_SUBTITLE_BOLD)
        
    draw_footer(canvas, draw, status)
    return canvas


def render_card(title, lines, verdict="none", badge_text="", status="Interactive STEM Guide"):
    """
    Renders concept cards and interactive quizzes.
    Supports:
    - Quizzes: verdict='yes' (Correct! 🎉) or 'no' (Try again! 💡)
    - Recycling: verdict='yes' (Recyclable) or 'no' (Not for filament)
    - Concepts / Tutorials: verdict='none'
    - Custom badge text via badge_text parameter
    """
    canvas = new_canvas()
    draw = ImageDraw.Draw(canvas)
    draw_header(draw, title)
    
    text_w = SCREEN_WIDTH - 60
    verdict_lower = (verdict or "").lower()
    
    has_badge = verdict_lower in ("yes", "no", "correct", "wrong")
    if has_badge:
        is_positive = verdict_lower in ("yes", "correct")
        combined_text = (title + " " + " ".join(lines)).lower()
        is_recycling = any(w in combined_text for w in ("bottle", "plastic", "filament", "pet ", "recyc", "flake"))
        
        if badge_text:
            b_main = badge_text.upper()
            b_sub1, b_sub2 = "", ""
        elif is_recycling:
            b_main = "YES" if is_positive else "NO"
            b_sub1 = "Recyclable" if is_positive else "Not for"
            b_sub2 = "into filament" if is_positive else "filament"
        else:
            b_main = "CORRECT" if is_positive else "TRY AGAIN"
            b_sub1 = "Well done! 🎉" if is_positive else "Check hint 💡"
            b_sub2 = ""
            
        bx, by, bw, bh = SCREEN_WIDTH - 30 - 220, 100, 220, 220
        draw.rounded_rectangle([bx, by, bx + bw, by + bh], radius=20, fill=INK_COLOR if is_positive else LIGHT_TINT, outline=BORDER_COLOR, width=3)
        
        v_col = WHITE_INK if is_positive else INK_COLOR
        vb = draw.textbbox((0, 0), b_main, font=FONT_SUBTITLE_BOLD if len(b_main) > 6 else FONT_SERIF_TITLE)
        draw.text((bx + (bw - (vb[2] - vb[0])) // 2, by + 45), b_main, fill=v_col, font=FONT_SUBTITLE_BOLD if len(b_main) > 6 else FONT_SERIF_TITLE)
        
        if b_sub1:
            sb1 = draw.textbbox((0, 0), b_sub1, font=FONT_BODY_BOLD)
            draw.text((bx + (bw - (sb1[2] - sb1[0])) // 2, by + 120), b_sub1, fill=v_col, font=FONT_BODY_BOLD)
        if b_sub2:
            sb2 = draw.textbbox((0, 0), b_sub2, font=FONT_BODY_BOLD)
            draw.text((bx + (bw - (sb2[2] - sb2[0])) // 2, by + 155), b_sub2, fill=v_col, font=FONT_BODY_BOLD)
            
        text_w -= (bw + 30)
        
    # Bullet points on the left
    curr_y = 115
    for item in lines:
        wrapped = wrap_text(draw, item, FONT_BODY, text_w - 40)
        for idx, subline in enumerate(wrapped):
            if curr_y > SCREEN_HEIGHT - 80:
                break
            if idx == 0:
                draw.ellipse([34, curr_y + 8, 46, curr_y + 20], fill=BORDER_COLOR)
            draw.text((60, curr_y), subline, fill=INK_COLOR, font=FONT_BODY)
            curr_y += 34
        curr_y += 8
        
    draw_footer(canvas, draw, status)
    return canvas


def render_steps(title, steps, status="Explained by Petra"):
    canvas = new_canvas()
    draw = ImageDraw.Draw(canvas)
    draw_header(draw, title)
    
    n = min(len(steps), 5)
    top = 90
    row_h = min(80, (SCREEN_HEIGHT - 60 - top) // n if n else 80)
    
    for i in range(n):
        cy = top + i * row_h + row_h // 2
        # Circle badge
        draw.ellipse([30, cy - 22, 74, cy + 22], fill=INK_COLOR)
        num_str = str(i + 1)
        nb = draw.textbbox((0, 0), num_str, font=FONT_SUBTITLE_BOLD)
        draw.text((52 - (nb[2] - nb[0]) // 2, cy - (nb[3] - nb[1]) // 2 - 2), num_str, fill=WHITE_INK, font=FONT_SUBTITLE_BOLD)
        
        # Connecting vertical bar
        if i < n - 1:
            draw.rectangle([50, cy + 22, 54, cy + row_h - 22], fill=BORDER_COLOR)
            
        step_text = fit_text(draw, steps[i], FONT_BODY_BOLD if row_h >= 70 else FONT_BODY, SCREEN_WIDTH - 120)
        draw.text((90, cy - 12), step_text, fill=INK_COLOR, font=FONT_BODY_BOLD if row_h >= 70 else FONT_BODY)
        
    draw_footer(canvas, draw, status)
    return canvas


def render_diagram(title, content, diagram_type="auto", status="Visual Diagram by Petra"):
    """
    Renders visual diagrams, flowcharts, circuits, and logic flows.
    """
    canvas = new_canvas()
    draw = ImageDraw.Draw(canvas)
    draw_header(draw, title)

    text = content.strip()
    if text.startswith("```"):
        text = re.sub(r"^```[a-z]*\n?", "", text)
        text = re.sub(r"\n?```$", "", text)

    raw_nodes = []
    if "->" in text or "-->" in text or "=>" in text:
        parts = re.split(r"-->|->|=>", text)
        raw_nodes = [p.strip() for p in parts if p.strip()]
    else:
        raw_nodes = [line.strip("- *").strip() for line in text.split("\n") if line.strip()]

    nodes = []
    for rn in raw_nodes:
        m = re.search(r"\[(.*?)\]|\((.*?)\)", rn)
        if m:
            clean = m.group(1) or m.group(2)
        else:
            clean = rn
        if clean:
            nodes.append(clean[:28])

    n = len(nodes)
    if n == 0:
        nodes = ["Step 1", "Step 2", "Step 3"]
        n = 3

    if n <= 4:
        gap = 25
        margin = 35
        avail_w = SCREEN_WIDTH - 2 * margin
        box_w = (avail_w - (n - 1) * gap) // n
        box_h = 130
        y = 175

        for i, node_text in enumerate(nodes):
            x = margin + i * (box_w + gap)
            draw.rounded_rectangle([x, y, x + box_w, y + box_h], radius=14, fill=LIGHT_TINT, outline=BORDER_COLOR, width=2)
            pill_h = 28
            draw.rounded_rectangle([x, y, x + box_w, y + pill_h], radius=10, fill=INK_COLOR)
            num_label = f"STEP {i + 1}"
            nb = draw.textbbox((0, 0), num_label, font=FONT_TINY_BOLD)
            draw.text((x + (box_w - (nb[2] - nb[0])) // 2, y + 6), num_label, fill=WHITE_INK, font=FONT_TINY_BOLD)

            wrapped = wrap_text(draw, node_text, FONT_BODY_BOLD, box_w - 20)
            text_total_h = len(wrapped) * 26
            ty = y + pill_h + max(10, (box_h - pill_h - text_total_h) // 2)
            for line in wrapped[:3]:
                lb = draw.textbbox((0, 0), line, font=FONT_BODY_BOLD)
                draw.text((x + (box_w - (lb[2] - lb[0])) // 2, ty), line, fill=INK_COLOR, font=FONT_BODY_BOLD)
                ty += 26

            if i < n - 1:
                ax_start = x + box_w + 3
                ax_end = ax_start + gap - 6
                ay = y + box_h // 2
                draw.line([ax_start, ay, ax_end, ay], fill=BORDER_COLOR, width=3)
                draw.polygon([(ax_end, ay - 7), (ax_end + 6, ay), (ax_end, ay + 7)], fill=BORDER_COLOR)
    else:
        nodes = nodes[:6]
        row1 = nodes[:3]
        row2 = nodes[3:]
        gap = 30
        margin = 50
        box_w = 200
        box_h = 95
        y1, y2 = 120, 270

        for i, node_text in enumerate(row1):
            x = margin + i * (box_w + gap)
            draw.rounded_rectangle([x, y1, x + box_w, y1 + box_h], radius=10, fill=LIGHT_TINT, outline=BORDER_COLOR, width=2)
            draw.text((x + 12, y1 + 8), f"#{i + 1}", fill="#777777", font=FONT_TINY_BOLD)
            wrapped = wrap_text(draw, node_text, FONT_BODY_BOLD, box_w - 24)
            ty = y1 + 30
            for line in wrapped[:2]:
                draw.text((x + 12, ty), line, fill=INK_COLOR, font=FONT_BODY_BOLD)
                ty += 24
            if i < len(row1) - 1:
                ax = x + box_w + 4
                ay = y1 + box_h // 2
                draw.line([ax, ay, ax + gap - 8, ay], fill=BORDER_COLOR, width=3)
                draw.polygon([(ax + gap - 8, ay - 6), (ax + gap - 2, ay), (ax + gap - 8, ay + 6)], fill=BORDER_COLOR)

        rx_turn = margin + 2 * (box_w + gap) + box_w // 2
        draw.line([rx_turn, y1 + box_h, rx_turn, y2], fill=BORDER_COLOR, width=3)
        draw.polygon([(rx_turn - 6, y2 - 6), (rx_turn, y2), (rx_turn + 6, y2 - 6)], fill=BORDER_COLOR)

        for j, node_text in enumerate(row2):
            idx = len(row2) - 1 - j
            x = margin + idx * (box_w + gap)
            draw.rounded_rectangle([x, y2, x + box_w, y2 + box_h], radius=10, fill=LIGHT_TINT, outline=BORDER_COLOR, width=2)
            draw.text((x + 12, y2 + 8), f"#{4 + j}", fill="#777777", font=FONT_TINY_BOLD)
            wrapped = wrap_text(draw, node_text, FONT_BODY_BOLD, box_w - 24)
            ty = y2 + 30
            for line in wrapped[:2]:
                draw.text((x + 12, ty), line, fill=INK_COLOR, font=FONT_BODY_BOLD)
                ty += 24
            if idx > 0:
                ax = x - 4
                ay = y2 + box_h // 2
                draw.line([ax, ay, ax - gap + 8, ay], fill=BORDER_COLOR, width=3)
                draw.polygon([(ax - gap + 8, ay - 6), (ax - gap + 2, ay), (ax - gap + 8, ay + 6)], fill=BORDER_COLOR)

    draw_footer(canvas, draw, status)
    return canvas


def render_setup(ap_name="PlastiBytes-Display-SETUP", status_detail="Configure MCP"):
    canvas = new_canvas()
    draw = ImageDraw.Draw(canvas)
    draw_header(draw, "Display setup")
    
    qr_img = generate_qr(f"http://192.168.4.1", 300)
    canvas.paste(qr_img, (30, 90))
    
    tx = 360
    draw.text((tx, 100), "1. Connect Virtual Screen", fill=INK_COLOR, font=FONT_SUBTITLE_BOLD)
    draw.text((tx, 140), "Enter your Xiaozhi MCP WebSocket endpoint.", fill=INK_COLOR, font=FONT_BODY)
    draw.text((tx, 175), "Available in xiaozhi.me -> Console -> Agents -> Petra.", fill=INK_COLOR, font=FONT_SMALL)
    
    draw.text((tx, 225), "2. LAN Photo Protocol Ready", fill=INK_COLOR, font=FONT_SUBTITLE_BOLD)
    draw.text((tx, 265), "Advertised over LAN via mDNS as epaper.local:80", fill=INK_COLOR, font=FONT_BODY)
    draw.text((tx, 295), "Petra sends photos via POST http://epaper.local/photo", fill=INK_COLOR, font=FONT_SMALL)
    draw.text((tx, 325), "Postcards downloadable at http://<ip>/postcard.png", fill=INK_COLOR, font=FONT_SMALL)
    
    draw_footer(canvas, draw, "Use the same Wi-Fi / LAN as Petra")
    return canvas


def render_error(title="Wi-Fi lost", detail="The display is offline."):
    canvas = new_canvas()
    draw = ImageDraw.Draw(canvas)
    draw_header(draw, title)
    
    lines = wrap_text(draw, detail, FONT_BODY, SCREEN_WIDTH - 60)
    y = 120
    for line in lines:
        draw.text((30, y), line, fill=INK_COLOR, font=FONT_BODY)
        y += 34
        
    draw_footer(canvas, draw, "Reconnecting...")
    return canvas


# ------------------------------------------------------------- Photo & Frames

def decode_photo_bitmap(raw_bytes, smooth=None):
    """
    Decodes 40,000 bytes (800x400 raw 1-bit bitmap, row-major, MSB first, 1=black).
    Applies subtle lithographic Gaussian smoothing on modern PC displays to eliminate
    harsh digital pixel dither.
    """
    if len(raw_bytes) != PHOTO_BYTES:
        raise ValueError(f"Expected {PHOTO_BYTES} bytes, got {len(raw_bytes)}")
    if smooth is None:
        smooth = SMOOTH_PHOTOS

    inv = bytes([~b & 0xFF for b in raw_bytes])
    img1 = Image.frombytes("1", (PHOTO_WIDTH, PHOTO_HEIGHT), inv)
    
    gray = img1.convert("L")
    if smooth:
        # Subtle Gaussian filter creates authentic book print / lithograph appearance
        gray = gray.filter(ImageFilter.GaussianBlur(radius=0.55))

    rgb = Image.new("RGB", (PHOTO_WIDTH, PHOTO_HEIGHT), BG_COLOR)
    alpha = ImageOps.invert(gray)
    ink_fill = Image.new("RGB", (PHOTO_WIDTH, PHOTO_HEIGHT), INK_COLOR)
    rgb.paste(ink_fill, (0, 0), alpha)
    return rgb


def draw_outlined_text(draw, text, x, y, font, text_color=None, outline_color=None):
    if text_color is None:
        text_color = INK_COLOR
    if outline_color is None:
        outline_color = WHITE_INK
    for dx in (-2, 0, 2):
        for dy in (-2, 0, 2):
            if dx or dy:
                draw.text((x + dx, y + dy), text, fill=outline_color, font=font)
    draw.text((x, y), text, fill=text_color, font=font)


def render_photo(raw_bitmap, caption="", frame="classic", event_name="CITS26 - Yaounde", download_qr=""):
    """
    Renders photo with selected frame and optional download QR box.
    Returns (display_image, digital_copy_image).
    """
    canvas = new_canvas()
    draw = ImageDraw.Draw(canvas)
    
    photo_img = decode_photo_bitmap(raw_bitmap)
    frame = (frame or "classic").lower()
    caption = caption or "Welcome to PlastiBytes!"
    
    # Photo placement offset
    px, py = 0, 0
    if frame == "hero":
        px, py = -180, 20
    elif frame == "film":
        py = -12
    canvas.paste(photo_img, (px, py))
    
    # Render frame overlay
    if frame == "polaroid":
        draw.rectangle([0, 0, SCREEN_WIDTH, 24], fill=BG_COLOR)
        draw.rectangle([0, 0, 40, SCREEN_HEIGHT], fill=BG_COLOR)
        draw.rectangle([SCREEN_WIDTH - 40, 0, SCREEN_WIDTH, SCREEN_HEIGHT], fill=BG_COLOR)
        draw.rectangle([0, 360, SCREEN_WIDTH, SCREEN_HEIGHT], fill=BG_COLOR)
        draw.rectangle([40, 24, SCREEN_WIDTH - 40, 360], outline=BORDER_COLOR, width=2)
        for tx in (60, SCREEN_WIDTH - 160):
            for ty in range(8, 34, 4):
                draw.line([tx, ty, tx + 100, ty], fill=BORDER_COLOR, width=1)
        draw.text((60, 385), fit_text(draw, caption, FONT_SERIF_TITLE, 560), fill=INK_COLOR, font=FONT_SERIF_TITLE)
        draw.text((60, 440), event_name, fill=INK_COLOR, font=FONT_SERIF_ITALIC)
        
    elif frame == "magazine":
        draw.rectangle([0, 0, logos_data.LOGO_PB_LARGE_W + 30, logos_data.LOGO_PB_LARGE_H + 24], fill=BG_COLOR)
        canvas.paste(logos_data.LOGO_PB_LARGE_IMG, (15, 12), logos_data.LOGO_PB_LARGE_IMG)
        draw_outlined_text(draw, "MONTHLY  -  OCT 2026", 18, logos_data.LOGO_PB_LARGE_H + 30, FONT_SMALL_BOLD)
        
        draw_outlined_text(draw, "COVER STAR", SCREEN_WIDTH - 200, 20, FONT_SUBTITLE_BOLD)
        draw_outlined_text(draw, "125K bottles recycled!", SCREEN_WIDTH - 240, 65, FONT_SMALL_BOLD)
        draw_outlined_text(draw, f"Live from {event_name}", SCREEN_WIDTH - 260, 95, FONT_SMALL_BOLD)
        
        draw.rectangle([0, 380, SCREEN_WIDTH, SCREEN_HEIGHT], fill=BG_COLOR)
        draw.rectangle([0, 380, SCREEN_WIDTH, 384], fill=INK_COLOR)
        draw.text((20, 410), fit_text(draw, caption, FONT_TITLE_BOLD, 620), fill=INK_COLOR, font=FONT_TITLE_BOLD)
        
    elif frame == "hero":
        draw.rectangle([380, 0, SCREEN_WIDTH, SCREEN_HEIGHT], fill=BG_COLOR)
        draw.rectangle([0, 0, 380, 40], fill=BG_COLOR)
        draw.rectangle([0, 0, 40, SCREEN_HEIGHT], fill=BG_COLOR)
        draw.rectangle([0, 400, 380, SCREEN_HEIGHT], fill=BG_COLOR)
        draw.rectangle([6, 6, SCREEN_WIDTH - 6, SCREEN_HEIGHT - 6], outline=BORDER_COLOR, width=4)
        draw.rectangle([16, 16, SCREEN_WIDTH - 16, SCREEN_HEIGHT - 16], outline=BORDER_COLOR, width=1)
        draw.rectangle([40, 40, 380, 400], outline=BORDER_COLOR, width=3)
        
        cx = 590
        t_cert = "CERTIFICATE"
        cb = draw.textbbox((0, 0), t_cert, font=FONT_SERIF_TITLE)
        draw.text((cx - (cb[2] - cb[0]) // 2, 40), t_cert, fill=INK_COLOR, font=FONT_SERIF_TITLE)
        draw.text((cx - 80, 92), "this proudly declares", fill=INK_COLOR, font=FONT_SERIF_ITALIC)
        
        draw.text((cx - 150, 128), fit_text(draw, caption, FONT_SUBTITLE_BOLD, 360), fill=INK_COLOR, font=FONT_SUBTITLE_BOLD)
        
        t_hero = "RECYCLING HERO"
        hb = draw.textbbox((0, 0), t_hero, font=FONT_SERIF_SUB)
        draw.text((cx - (hb[2] - hb[0]) // 2, 180), t_hero, fill=INK_COLOR, font=FONT_SERIF_SUB)
        draw.text((cx - 80, 230), f"at {event_name}", fill=INK_COLOR, font=FONT_SERIF_ITALIC)
        
        for sx, sy in [(470, 290), (530, 280), (590, 290)]:
            draw.text((sx, sy), "★", fill=INK_COLOR, font=FONT_TITLE_BOLD)
        draw.text((450, 330), "Signed: Petra, PlastiBytes AI", fill=INK_COLOR, font=FONT_SMALL_BOLD)
        
    elif frame == "wanted":
        draw.rectangle([0, 0, SCREEN_WIDTH, 110], fill=BG_COLOR)
        draw.rectangle([0, 0, 150, SCREEN_HEIGHT], fill=BG_COLOR)
        draw.rectangle([SCREEN_WIDTH - 150, 0, SCREEN_WIDTH, SCREEN_HEIGHT], fill=BG_COLOR)
        draw.rectangle([0, 370, SCREEN_WIDTH, SCREEN_HEIGHT], fill=BG_COLOR)
        draw.rectangle([150, 110, SCREEN_WIDTH - 150, 370], outline=BORDER_COLOR, width=4)
        draw.rectangle([4, 4, SCREEN_WIDTH - 4, SCREEN_HEIGHT - 4], outline=BORDER_COLOR, width=3)
        
        w_title = "WANTED"
        wb = draw.textbbox((0, 0), w_title, font=FONT_SERIF_TITLE)
        draw.text(((SCREEN_WIDTH - (wb[2] - wb[0])) // 2, 20), w_title, fill=INK_COLOR, font=FONT_SERIF_TITLE)
        
        draw.text((150, 380), fit_text(draw, caption, FONT_SERIF_SUB, 500), fill=INK_COLOR, font=FONT_SERIF_SUB)
        draw.text((150, 425), f"for saving plastic at {event_name}", fill=INK_COLOR, font=FONT_SERIF_SMALL_ITALIC)
        
        draw.text((20, 200), "REWARD:", fill=INK_COLOR, font=FONT_SERIF_SUB)
        draw.text((20, 235), "a cleaner", fill=INK_COLOR, font=FONT_SERIF_ITALIC)
        draw.text((20, 265), "planet", fill=INK_COLOR, font=FONT_SERIF_ITALIC)
        
    elif frame == "comic":
        draw.rectangle([0, 0, SCREEN_WIDTH, PHOTO_HEIGHT], outline=BORDER_COLOR, width=8)
        draw.rectangle([0, PHOTO_HEIGHT, SCREEN_WIDTH, SCREEN_HEIGHT], fill=BG_COLOR)
        
        bubble_text = fit_text(draw, caption, FONT_COMIC_SUB, 400)
        tb = draw.textbbox((0, 0), bubble_text, font=FONT_COMIC_SUB)
        bw, bh = tb[2] - tb[0] + 50, 70
        draw.rounded_rectangle([30, 25, 30 + bw, 25 + bh], radius=20, fill=BG_COLOR, outline=BORDER_COLOR, width=3)
        draw.polygon([(70, 95), (100, 95), (60, 130)], fill=BG_COLOR, outline=BORDER_COLOR)
        draw.text((50, 45), bubble_text, fill=INK_COLOR, font=FONT_COMIC_SUB)
        
        draw.ellipse([70, 360, 160, 440], fill=BG_COLOR, outline=BORDER_COLOR, width=3)
        draw.text((85, 385), "POW!", fill=INK_COLOR, font=FONT_COMIC_TITLE)
        draw.text((200, 435), f"Episode: {event_name}", fill=INK_COLOR, font=FONT_COMIC_SUB)
        
    elif frame == "film":
        draw.rectangle([0, 0, SCREEN_WIDTH, 44], fill=INK_COLOR)
        draw.rectangle([0, 344, SCREEN_WIDTH, 388], fill=INK_COLOR)
        for fx in range(14, SCREEN_WIDTH, 52):
            draw.rounded_rectangle([fx, 12, fx + 28, 32], radius=4, fill=BG_COLOR)
            draw.rounded_rectangle([fx, 356, fx + 28, 376], radius=4, fill=BG_COLOR)
        draw.rectangle([0, 388, SCREEN_WIDTH, SCREEN_HEIGHT], fill=BG_COLOR)
        draw.text((20, 410), fit_text(draw, caption, FONT_SUBTITLE_BOLD, 600), fill=INK_COLOR, font=FONT_SUBTITLE_BOLD)
        draw.text((20, 448), f"{event_name}  -  frame 26", fill=INK_COLOR, font=FONT_SMALL)
        
    else:
        # Classic (and "event")
        draw.rectangle([0, PHOTO_HEIGHT, SCREEN_WIDTH, SCREEN_HEIGHT], fill=BG_COLOR)
        draw.line([0, PHOTO_HEIGHT, SCREEN_WIDTH, PHOTO_HEIGHT], fill=BORDER_COLOR, width=2)
        draw.text((20, PHOTO_HEIGHT + 12), fit_text(draw, caption, FONT_SUBTITLE_BOLD, 460), fill=INK_COLOR, font=FONT_SUBTITLE_BOLD)
        canvas.paste(logos_data.LOGO_PB_SMALL_IMG, (20, PHOTO_HEIGHT + 44), logos_data.LOGO_PB_SMALL_IMG)
        draw.text((190, PHOTO_HEIGHT + 52), event_name, fill=INK_COLOR, font=FONT_SMALL)
        if frame == "event":
            draw_event_badge(canvas, draw, SCREEN_WIDTH - 255, 14, event_name)

    digital_copy = canvas.copy()
    if download_qr:
        draw_download_box(canvas, draw, download_qr)
        
    return canvas, digital_copy
