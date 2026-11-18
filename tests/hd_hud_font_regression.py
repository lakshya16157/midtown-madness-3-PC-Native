"""HD HUD fonts keep one-bit alpha (tools/hud/hd_hud.py `upscale`, #42).

The disc's fonts are DXT1 with on/off alpha. A soft alpha ramp on the upscaled copy showed in the menus
as speckled, ragged letter outlines; the upscale must stay on/off, with the glyphs about as large as
before. Made-up letters, no game art. Needs Pillow; runs on any platform."""
from pathlib import Path
import importlib.util

ROOT = Path(__file__).resolve().parents[1]


def main():
    from PIL import Image, ImageDraw
    spec = importlib.util.spec_from_file_location("hd_hud_font_test", ROOT / "tools/hud/hd_hud.py")
    hd = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(hd)
    # Blue-filled shapes with a one-pixel black outline on transparent, as the disc's fonts are.
    glyphs = Image.new("RGBA", (64, 32), (0, 0, 0, 0))
    draw = ImageDraw.Draw(glyphs)
    draw.ellipse((3, 3, 28, 28), fill=(0, 0, 255, 255), outline=(0, 0, 0, 255))
    draw.polygon([(36, 28), (46, 3), (56, 3), (60, 28)], fill=(0, 0, 255, 255), outline=(0, 0, 0, 255))
    for factor in (2, 4):
        big = hd.upscale(glyphs, factor)
        assert big.size == (64 * factor, 32 * factor), big.size
        alpha = big.getchannel("A").histogram()
        assert sum(alpha[1:255]) == 0, f"{factor}x: {sum(alpha[1:255])} partly transparent pixels"
        before = glyphs.getchannel("A").histogram()[255] * factor * factor
        after = alpha[255]
        assert abs(after - before) < 0.08 * before, (factor, before, after)
    print("PASS HD HUD fonts: one-bit alpha stays one-bit at 2x and 4x, glyph area kept")


if __name__ == "__main__":
    main()
