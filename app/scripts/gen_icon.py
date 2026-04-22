"""Generate labs.ico (+ a PNG) from the LabsLogoWidget design.

The SVG-style geometry is the same "L glyph + signal pulse + accent bar"
pattern we paint at runtime in LabsLogoWidget. We render several sizes
(16/32/48/64/128/256) and bake them into a single .ico so Windows Explorer
picks the right size per zoom level.
"""
from PIL import Image, ImageDraw
from pathlib import Path
import sys

OUT_DIR = Path(__file__).resolve().parent.parent / "engine" / "resources"
OUT_DIR.mkdir(parents=True, exist_ok=True)

ACCENT  = (59, 130, 246, 255)    # #3B82F6
HI      = (96, 165, 250, 255)    # #60A5FA
SOFT    = (191, 219, 254, 200)   # #BFDBFE @ ~78%
BG      = (11, 16, 32, 255)      # #0B1020 — gives the icon a framed card look


def render(size: int) -> Image.Image:
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    # Rounded-square background card (radius ~18% of size).
    r = max(2, int(size * 0.18))
    d.rounded_rectangle((0, 0, size - 1, size - 1), radius=r, fill=BG)

    # Scale the 30x30 logo geometry up to `size`.
    s = size / 30.0
    def px(x, y, w, h):
        return (int(round(x * s)), int(round(y * s)),
                int(round((x + w) * s)), int(round((y + h) * s)))

    # "L" glyph polygon (same points as LabsLogoWidget).
    pts = [
        (4 * s,  4 * s),  (13 * s, 4 * s),
        (13 * s, 19 * s), (26 * s, 19 * s),
        (26 * s, 26 * s), (4 * s, 26 * s),
    ]
    d.polygon(pts, fill=ACCENT)

    # Signal pulse
    d.ellipse(px(18, 6, 5, 5), fill=HI)
    # Accent bar
    d.rectangle(px(24, 22, 4, 2), fill=SOFT)
    return img


def main():
    sizes = [16, 24, 32, 48, 64, 128, 256]
    ico_path = OUT_DIR / "labs.ico"
    png_path = OUT_DIR / "labs.png"

    big = render(256)
    big.save(png_path, format="PNG")

    frames = [render(s) for s in sizes]
    frames[0].save(ico_path, format="ICO",
                   sizes=[(s, s) for s in sizes],
                   append_images=frames[1:])

    print(f"wrote {png_path}")
    print(f"wrote {ico_path}")


if __name__ == "__main__":
    main()
