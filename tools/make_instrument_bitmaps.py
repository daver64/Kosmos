#!/usr/bin/env python3
"""Draws the placeholder bitmaps for addons/instruments (the six-pack: attitude indicator, altimeter, airspeed, vertical speed,
heading and turn coordinator).

They are ordinary PNGs that the MFD scripts draw with g:image(), so artists can replace any of them with proper
artwork of the same size without touching the Lua. Run from the repository root: python3 tools/make_instrument_bitmaps.py
"""
import math
import os

from PIL import Image, ImageDraw, ImageFont

OUT = os.path.join(os.path.dirname(__file__), "..", "addons", "instruments")
SS = 4  # drawn at 4x and reduced for smooth edges
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
MFD_BACKGROUND = (18, 20, 26, 255)


def font(size):
    return ImageFont.truetype(FONT, size * SS)


def canvas(w, h, fill=(0, 0, 0, 0)):
    return Image.new("RGBA", (w * SS, h * SS), fill)


def save(img, name):
    w, h = img.size[0] // SS, img.size[1] // SS
    img.resize((w, h), Image.LANCZOS).save(os.path.join(OUT, name))


def polar(cx, cy, r, deg):
    a = math.radians(deg)
    return cx + r * math.sin(a), cy - r * math.cos(a)


def centred_text(d, x, y, text, size, fill):
    d.text((x * SS, y * SS), text, font=font(size), fill=fill, anchor="mm")


# ---- Attitude indicator ----------------------------------------------------------------------------------------------

BALL = 280  # the window the MFD cuts out of the sphere strip; also the diameter of the instrument
PX_PER_DEG = 3
STRIP_H = 180 * PX_PER_DEG + BALL


def ball_strip():
    """Sky above, ground below, a pitch ladder every 5 degrees. Pitch 0 is the middle row; +90 is the top."""
    img = canvas(BALL, STRIP_H)
    d = ImageDraw.Draw(img)
    mid = STRIP_H / 2
    for y in range(STRIP_H):
        t = y / STRIP_H
        if y < mid:
            k = y / mid  # zenith (dark) to horizon (light)
            c = (int(20 + 70 * k), int(80 + 100 * k), int(190 + 45 * k))
        else:
            k = (y - mid) / mid  # horizon (light) to nadir (dark)
            c = (int(150 - 70 * k), int(95 - 50 * k), int(45 - 25 * k))
        d.rectangle([0, y * SS, BALL * SS, (y + 1) * SS], fill=c + (255,))
    d.rectangle([0, (mid - 1.5) * SS, BALL * SS, (mid + 1.5) * SS], fill=(255, 255, 255, 255))
    cx = BALL / 2
    for deg in range(-90, 91, 5):
        if deg == 0:
            continue
        y = mid - deg * PX_PER_DEG
        half = 40 if deg % 10 == 0 else 20
        if abs(deg) == 90:
            half = 10
        d.line([(cx - half) * SS, y * SS, (cx + half) * SS, y * SS], fill=(255, 255, 255, 255), width=2 * SS)
        if deg % 10 == 0 and abs(deg) < 90:
            label = str(abs(deg))
            centred_text(d, cx - half - 14, y, label, 13, (255, 255, 255, 255))
            centred_text(d, cx + half + 14, y, label, 13, (255, 255, 255, 255))
    return img


def ball_bezel():
    """A 400x400 mask: opaque MFD background with a round hole, a ring, roll ticks, the sphere shading and the fixed aircraft symbol."""
    n = 400
    r = BALL / 2
    img = canvas(n, n, MFD_BACKGROUND)
    d = ImageDraw.Draw(img)
    c = n / 2
    # Punch the window.
    hole = Image.new("L", img.size, 255)
    ImageDraw.Draw(hole).ellipse([(c - r) * SS, (c - r) * SS, (c + r) * SS, (c + r) * SS], fill=0)
    img.putalpha(hole)
    d = ImageDraw.Draw(img)
    # Shading that makes the flat strip read as a ball: dark limb, soft highlight high and to the left.
    steps = 60
    ring = canvas(n, n)
    rd = ImageDraw.Draw(ring)
    for i in range(steps):
        k = i / (steps - 1)  # 0 centre, 1 limb
        rad = r * k
        rd.ellipse([(c - rad) * SS, (c - rad) * SS, (c + rad) * SS, (c + rad) * SS], outline=(0, 0, 0, int(170 * (k ** 4))), width=max(1, int(r / steps * SS * 1.6)))
    hl = canvas(n, n)
    ImageDraw.Draw(hl).ellipse([(c - 70) * SS, (c - 105) * SS, (c - 5) * SS, (c - 55) * SS], fill=(255, 255, 255, 38))
    sphere = Image.alpha_composite(ring, hl)
    clip = Image.new("L", img.size, 0)
    ImageDraw.Draw(clip).ellipse([(c - r) * SS, (c - r) * SS, (c + r) * SS, (c + r) * SS], fill=255)
    sphere.putalpha(Image.composite(sphere.getchannel("A"), Image.new("L", img.size, 0), clip))
    img = Image.alpha_composite(img, sphere)
    d = ImageDraw.Draw(img)
    # Bezel ring.
    d.ellipse([(c - r - 3) * SS, (c - r - 3) * SS, (c + r + 3) * SS, (c + r + 3) * SS], outline=(150, 156, 170, 255), width=4 * SS)
    # Roll scale just outside the ring: 10-degree ticks, long at 30/60, a triangle at the top.
    for deg in range(-60, 61, 10):
        long = deg % 30 == 0
        x1, y1 = polar(c, c, r + 8, deg)
        x2, y2 = polar(c, c, r + (22 if long else 15), deg)
        d.line([x1 * SS, y1 * SS, x2 * SS, y2 * SS], fill=(235, 238, 245, 255), width=3 * SS if long else 2 * SS)
    tx, ty = polar(c, c, r + 8, 0)
    d.polygon([(tx - 7) * SS, (ty - 14) * SS, (tx + 7) * SS, (ty - 14) * SS, tx * SS, ty * SS], fill=(235, 238, 245, 255))
    # Fixed aircraft symbol.
    orange = (255, 150, 30, 255)
    d.line([(c - 95) * SS, c * SS, (c - 32) * SS, c * SS], fill=orange, width=6 * SS)
    d.line([(c + 32) * SS, c * SS, (c + 95) * SS, c * SS], fill=orange, width=6 * SS)
    d.line([(c - 32) * SS, c * SS, (c - 32) * SS, (c + 14) * SS], fill=orange, width=6 * SS)
    d.line([(c + 32) * SS, c * SS, (c + 32) * SS, (c + 14) * SS], fill=orange, width=6 * SS)
    d.ellipse([(c - 5) * SS, (c - 5) * SS, (c + 5) * SS, (c + 5) * SS], fill=orange)
    return img


def ball_pointer():
    """The roll pointer: a small triangle that sits on the ring at zero roll and turns about the centre."""
    n = 400
    img = canvas(n, n)
    d = ImageDraw.Draw(img)
    c = n / 2
    r = BALL / 2
    y = c - r - 2
    d.polygon([(c - 8) * SS, (y - 16) * SS, (c + 8) * SS, (y - 16) * SS, c * SS, (y + 2) * SS], fill=(255, 150, 30, 255))
    return img


# ---- Altimeter -------------------------------------------------------------------------------------------------------

DIAL = 300


def altimeter_face():
    img = canvas(DIAL, DIAL)
    d = ImageDraw.Draw(img)
    c = DIAL / 2
    d.ellipse([2 * SS, 2 * SS, (DIAL - 2) * SS, (DIAL - 2) * SS], fill=(10, 11, 14, 255), outline=(150, 156, 170, 255), width=5 * SS)
    d.ellipse([12 * SS, 12 * SS, (DIAL - 12) * SS, (DIAL - 12) * SS], outline=(60, 64, 74, 255), width=2 * SS)
    for i in range(50):
        deg = i * 7.2
        major = i % 5 == 0
        x1, y1 = polar(c, c, c - 16, deg)
        x2, y2 = polar(c, c, c - (36 if major else 28), deg)
        d.line([x1 * SS, y1 * SS, x2 * SS, y2 * SS], fill=(240, 240, 240, 255), width=(4 if major else 2) * SS)
    for n in range(10):
        x, y = polar(c, c, c - 56, n * 36)
        centred_text(d, x, y, str(n), 30, (245, 245, 245, 255))
    centred_text(d, c, c - 48, "ALT", 14, (255, 200, 80, 255))
    centred_text(d, c, c + 46, "x100 m", 13, (170, 176, 190, 255))
    # Scale of the 100 km pointer on the rim.
    for i in range(10):
        x1, y1 = polar(c, c, c - 3, i * 36)
        x2, y2 = polar(c, c, c - 11, i * 36)
        d.line([x1 * SS, y1 * SS, x2 * SS, y2 * SS], fill=(255, 150, 30, 255), width=2 * SS)
    return img


def needle(length, width, tail, colour, hub=0):
    img = canvas(DIAL, DIAL)
    d = ImageDraw.Draw(img)
    c = DIAL / 2
    d.polygon([(c - width) * SS, c * SS, c * SS, (c - length) * SS, (c + width) * SS, c * SS, (c + width * 0.6) * SS, (c + tail) * SS, (c - width * 0.6) * SS, (c + tail) * SS], fill=colour)
    if hub:
        d.ellipse([(c - hub) * SS, (c - hub) * SS, (c + hub) * SS, (c + hub) * SS], fill=(40, 42, 50, 255), outline=colour, width=2 * SS)
    return img


def rim_marker():
    img = canvas(DIAL, DIAL)
    d = ImageDraw.Draw(img)
    c = DIAL / 2
    d.polygon([(c - 7) * SS, 8 * SS, (c + 7) * SS, 8 * SS, c * SS, 24 * SS], fill=(255, 150, 30, 255))
    return img


# ---- Airspeed, vertical speed, heading and turn coordinator ---------------------------------------------------------------

ASI_MAX_KT = 500  # the dial sweeps 300 degrees, starting at -150 (7:30 on the clock)
VSI_MAX_FPM = 3000  # zero at 9 o'clock, 170 degrees either way


def bezel(d, fill=(10, 11, 14, 255)):
    d.ellipse([2 * SS, 2 * SS, (DIAL - 2) * SS, (DIAL - 2) * SS], fill=fill, outline=(150, 156, 170, 255), width=5 * SS)
    d.ellipse([12 * SS, 12 * SS, (DIAL - 12) * SS, (DIAL - 12) * SS], outline=(60, 64, 74, 255), width=2 * SS)


def tick(d, c, deg, r_out, r_in, width, colour=(240, 240, 240, 255)):
    x1, y1 = polar(c, c, r_out, deg)
    x2, y2 = polar(c, c, r_in, deg)
    d.line([x1 * SS, y1 * SS, x2 * SS, y2 * SS], fill=colour, width=int(width * SS))


def asi_face():
    img = canvas(DIAL, DIAL)
    d = ImageDraw.Draw(img)
    c = DIAL / 2
    bezel(d)
    for kt in range(0, ASI_MAX_KT + 1, 10):
        deg = -150 + kt / ASI_MAX_KT * 300
        major = kt % 50 == 0
        tick(d, c, deg, c - 16, c - (36 if major else 28), 4 if major else 2)
        if major:
            x, y = polar(c, c, c - 56, deg)
            centred_text(d, x, y, str(kt), 17, (245, 245, 245, 255))
    centred_text(d, c, c - 44, "AIRSPEED", 13, (255, 200, 80, 255))
    centred_text(d, c, c + 52, "KNOTS", 13, (170, 176, 190, 255))
    return img


def vsi_angle(fpm):
    return -90 + fpm / VSI_MAX_FPM * 170


def vsi_face():
    img = canvas(DIAL, DIAL)
    d = ImageDraw.Draw(img)
    c = DIAL / 2
    bezel(d)
    for fpm in range(-VSI_MAX_FPM, VSI_MAX_FPM + 1, 250):
        major = fpm % 500 == 0
        tick(d, c, vsi_angle(fpm), c - 16, c - (34 if major else 26), 4 if major else 2)
    for thousand in range(-3, 4):
        x, y = polar(c, c, c - 56, vsi_angle(thousand * 1000))
        centred_text(d, x, y, str(abs(thousand)), 26, (245, 245, 245, 255))
    centred_text(d, c, c - 62, "UP", 14, (255, 200, 80, 255))
    centred_text(d, c, c + 62, "DN", 14, (255, 200, 80, 255))
    centred_text(d, c + 8, c - 14, "VERTICAL", 11, (170, 176, 190, 255))
    centred_text(d, c + 8, c + 2, "SPEED", 11, (170, 176, 190, 255))
    centred_text(d, c + 8, c + 18, "x1000 fpm", 11, (170, 176, 190, 255))
    return img


def dg_card():
    """The rotating compass card: ticks every 5 degrees, numerals every 30 (N, E, S, W at the quarters)."""
    img = canvas(DIAL, DIAL)
    d = ImageDraw.Draw(img)
    c = DIAL / 2
    d.ellipse([6 * SS, 6 * SS, (DIAL - 6) * SS, (DIAL - 6) * SS], fill=(10, 11, 14, 255))
    for deg in range(0, 360, 5):
        major = deg % 10 == 0
        tick(d, c, deg, c - 12, c - (30 if deg % 30 == 0 else 24 if major else 20), 4 if deg % 30 == 0 else 2)
    for deg in range(0, 360, 30):
        label = {0: "N", 90: "E", 180: "S", 270: "W"}.get(deg, str(deg // 10))
        x, y = polar(c, c, c - 48, deg)
        glyph = canvas(40, 40)
        gd = ImageDraw.Draw(glyph)
        centred_text(gd, 20, 20, label, 26 if deg % 90 == 0 else 22, (255, 200, 80, 255) if deg % 90 == 0 else (245, 245, 245, 255))
        glyph = glyph.rotate(-deg, resample=Image.BICUBIC)
        img.alpha_composite(glyph, (int((x - 20) * SS), int((y - 20) * SS)))
    return img


def dg_face():
    """The fixed overlay on the heading card: bezel ring, lubber line and the aircraft symbol."""
    img = canvas(DIAL, DIAL)
    d = ImageDraw.Draw(img)
    c = DIAL / 2
    d.ellipse([2 * SS, 2 * SS, (DIAL - 2) * SS, (DIAL - 2) * SS], outline=(150, 156, 170, 255), width=5 * SS)
    d.polygon([c * SS, 20 * SS, (c - 8) * SS, 4 * SS, (c + 8) * SS, 4 * SS], fill=(255, 150, 30, 255))
    d.line([c * SS, (c - 44) * SS, c * SS, (c + 40) * SS], fill=(255, 255, 255, 255), width=5 * SS)
    d.line([(c - 38) * SS, (c - 4) * SS, (c + 38) * SS, (c - 4) * SS], fill=(255, 255, 255, 255), width=5 * SS)
    d.line([(c - 14) * SS, (c + 36) * SS, (c + 14) * SS, (c + 36) * SS], fill=(255, 255, 255, 255), width=4 * SS)
    return img


def tc_face():
    img = canvas(DIAL, DIAL)
    d = ImageDraw.Draw(img)
    c = DIAL / 2
    bezel(d)
    # Standard-rate marks: the aircraft symbol lines up with these in a 3 degree per second turn.
    for deg in (70, 110, 250, 290):
        tick(d, c, deg, c - 18, c - 44, 5, (255, 255, 255, 255))
    tick(d, c, 0, c - 18, c - 44, 4, (255, 150, 30, 255))
    centred_text(d, 34, c - 4, "L", 24, (245, 245, 245, 255))
    centred_text(d, DIAL - 34, c - 4, "R", 24, (245, 245, 245, 255))
    centred_text(d, c, c - 62, "TURN", 14, (255, 200, 80, 255))
    centred_text(d, c, c + 18, "2 MIN", 13, (170, 176, 190, 255))
    centred_text(d, c, c + 34, "NO PITCH INFORMATION", 8, (170, 176, 190, 255))
    # The inclinometer tube; the slip ball rides in it (see turn.lua).
    pts = [((c + dx) * SS, (TUBE_Y - dx * dx / TUBE_CURVE) * SS) for dx in range(-62, 63, 2)]
    d.line(pts, fill=(70, 74, 84, 255), width=24 * SS, joint="curve")
    d.line(pts, fill=(18, 20, 26, 255), width=20 * SS, joint="curve")
    for dx in (-13, 13):
        d.line([(c + dx) * SS, (TUBE_Y - dx * dx / TUBE_CURVE - 12) * SS, (c + dx) * SS, (TUBE_Y - dx * dx / TUBE_CURVE + 12) * SS], fill=(240, 240, 240, 255), width=2 * SS)
    return img


TUBE_Y, TUBE_CURVE = 236, 100


def tc_aircraft():
    """The miniature aircraft seen from behind; it banks with the turn rate."""
    img = canvas(DIAL, DIAL)
    d = ImageDraw.Draw(img)
    c = DIAL / 2
    white = (250, 250, 250, 255)
    d.line([(c - 96) * SS, c * SS, (c + 96) * SS, c * SS], fill=white, width=7 * SS)
    d.ellipse([(c - 14) * SS, (c - 14) * SS, (c + 14) * SS, (c + 14) * SS], fill=white)
    d.line([c * SS, (c - 4) * SS, c * SS, (c - 44) * SS], fill=white, width=7 * SS)
    d.line([(c - 22) * SS, (c - 40) * SS, (c + 22) * SS, (c - 40) * SS], fill=white, width=5 * SS)
    return img


def main():
    os.makedirs(OUT, exist_ok=True)
    save(ball_strip(), "adi_ball.png")
    save(ball_bezel(), "adi_bezel.png")
    save(ball_pointer(), "adi_pointer.png")
    save(altimeter_face(), "alt_face.png")
    save(needle(112, 5, 28, (245, 245, 245, 255)), "alt_needle_100m.png")
    save(needle(72, 8, 14, (255, 255, 255, 255), hub=9), "alt_needle_1km.png")
    save(rim_marker(), "alt_marker_10km.png")
    save(asi_face(), "asi_face.png")
    save(needle(112, 6, 28, (245, 245, 245, 255)), "needle_long.png")
    save(vsi_face(), "vsi_face.png")
    save(dg_card(), "dg_card.png")
    save(dg_face(), "dg_face.png")
    save(tc_face(), "tc_face.png")
    save(tc_aircraft(), "tc_aircraft.png")


if __name__ == "__main__":
    main()
