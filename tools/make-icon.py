"""kotemado.ico を作る。

窓(青い見出しの付いた白い四角)と、その四隅を押さえる橙のかぎ括弧。
「この位置に固定する」を表す。小さい寸法では線を太めにして潰れないようにする。

    python tools/make-icon.py      -> src/kotemado.ico
"""
from pathlib import Path
from PIL import Image, ImageDraw

SIZES = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]
SS = 8  # 縦横 8 倍で描いて縮める(なめらかにするため)

BLUE = (0, 103, 192, 255)
BODY = (250, 251, 253, 255)
EDGE = (40, 78, 130, 255)
ORANGE = (255, 106, 43, 255)


def draw(size: int) -> Image.Image:
    s = size * SS
    im = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    u = s / 32.0  # 32 マス方眼で設計する

    small = size <= 24
    # 窓
    x0, y0, x1, y1 = 5 * u, 7 * u, 27 * u, 25 * u
    r = 2.2 * u
    d.rounded_rectangle([x0, y0, x1, y1], r, fill=BODY, outline=EDGE, width=max(int(1.0 * u), SS))
    bar = 11.5 * u if small else 11 * u
    d.rounded_rectangle([x0, y0, x1, bar], r, fill=BLUE)
    d.rectangle([x0, bar - r, x1, bar], fill=BLUE)
    if not small:
        # 本文の行
        for i, w in enumerate((14, 10, 12)):
            yy = (14.5 + i * 3.2) * u
            d.rounded_rectangle([8 * u, yy, (8 + w) * u, yy + 1.4 * u], 0.7 * u,
                                fill=(170, 186, 206, 255))

    # 四隅のかぎ括弧
    t = (2.6 if small else 2.0) * u
    L = (7.0 if small else 6.5) * u
    m = 1.0 * u
    X0, Y0, X1, Y1 = m, m + 1 * u, s - m, s - m - 1 * u
    for cx, cy, sx, sy in ((X0, Y0, 1, 1), (X1, Y0, -1, 1), (X0, Y1, 1, -1), (X1, Y1, -1, -1)):
        d.rectangle(sorted_box(cx, cy, cx + sx * L, cy + sy * t), fill=ORANGE)
        d.rectangle(sorted_box(cx, cy, cx + sx * t, cy + sy * L), fill=ORANGE)

    return im.resize((size, size), Image.LANCZOS)


def sorted_box(ax, ay, bx, by):
    return [min(ax, bx), min(ay, by), max(ax, bx), max(ay, by)]


def main():
    out = Path(__file__).resolve().parent.parent / "src" / "kotemado.ico"
    images = [draw(n) for n in SIZES]
    images[-1].save(out, format="ICO", sizes=[(n, n) for n in SIZES], append_images=images[:-1])
    images[-1].save(out.with_suffix(".png"))
    print("wrote", out)


if __name__ == "__main__":
    main()
