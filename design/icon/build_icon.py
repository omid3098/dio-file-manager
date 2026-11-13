"""Rebuild src/dio-file-manager.ico from design/icon/icon.svg through headless Edge (no build dependency:
the build only embeds the committed .ico). Run from anywhere:  python design/icon/build_icon.py

16 and 20 px keep only the tile and the folder; the edge and the spark are cut from the SVG there.
Pass --preview DIR to also write every size on dark and light backgrounds into DIR."""
import io, os, pathlib, re, struct, subprocess, sys, tempfile
from PIL import Image

here = pathlib.Path(__file__).resolve().parent
out_ico = here.parent.parent / "src" / "dio-file-manager.ico"
SIZES = (16, 20, 24, 32, 40, 48, 64, 256)
BIG = 1024
edge = pathlib.Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "Microsoft/Edge/Application/msedge.exe"


def variant(svg, small):
    if small:
        svg = re.sub(r"<!--edge-->.*?<!--/edge-->", "", svg, flags=re.S)
        svg = re.sub(r"<!--spark-->.*?<!--/spark-->", "", svg, flags=re.S)
    return svg


def raster(svg, px, work):
    """The SVG at px x px on a transparent background."""
    svg = svg.replace('width="256" height="256"', f'width="{px}" height="{px}"', 1)
    page = work / "page.html"
    page.write_text('<!doctype html><meta charset="utf-8"><style>html,body{margin:0;background:transparent;overflow:hidden}'
                    f'svg{{display:block}}</style>{svg}', encoding="utf-8")
    png = work / "shot.png"
    if png.exists():
        png.unlink()
    subprocess.run([str(edge), "--headless=new", "--disable-gpu", f"--user-data-dir={work / 'profile'}", "--hide-scrollbars",
                    "--force-device-scale-factor=1", "--default-background-color=00000000", f"--window-size={px},{px}",
                    f"--screenshot={png}", page.as_uri()], check=True, capture_output=True, timeout=120,
                   creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    img = Image.open(png).convert("RGBA")
    if img.size != (px, px):
        img = img.crop((0, 0, px, px))
    return img


def main():
    src = (here / "icon.svg").read_text(encoding="utf-8")
    frames = {}
    with tempfile.TemporaryDirectory() as t:
        work = pathlib.Path(t)
        small = raster(variant(src, True).replace("EDGE_W", "0"), BIG, work)
        full_by_edge = {}
        for size in SIZES:
            if size <= 20:
                big = small
            else:
                edge_w = f"{256 / size:.3f}"   # one device pixel at the final size
                full_by_edge[size] = raster(variant(src, False).replace("EDGE_W", edge_w), BIG, work)
                big = full_by_edge[size]
            frames[size] = big.resize((size, size), Image.LANCZOS)
    entries, images = b"", b""
    for size in SIZES:
        buf = io.BytesIO()
        frames[size].save(buf, "PNG", optimize=True)
        data = buf.getvalue()
        entries += struct.pack("<BBBBHHII", size % 256, size % 256, 0, 0, 1, 32, len(data), 6 + 16 * len(SIZES) + len(images))
        images += data
    out_ico.write_bytes(struct.pack("<HHH", 0, 1, len(SIZES)) + entries + images)
    print(f"{out_ico}: {', '.join(map(str, SIZES))} px, {out_ico.stat().st_size} bytes")
    if "--preview" in sys.argv:
        d = pathlib.Path(sys.argv[sys.argv.index("--preview") + 1])
        d.mkdir(parents=True, exist_ok=True)
        for name, bg in (("dark", (28, 30, 36, 255)), ("light", (238, 240, 244, 255))):
            sheet = Image.new("RGBA", (sum(SIZES) + 24 * (len(SIZES) + 1), 256 + 48), bg)
            x = 24
            for size in SIZES:
                sheet.alpha_composite(frames[size], (x, 24 + (256 - size) // 2))
                x += size + 24
            sheet.save(d / f"icon-sizes-{name}.png")
        zoom = Image.new("RGBA", (6 * 128, 2 * 128 + 8), (0, 0, 0, 0))
        for i, size in enumerate(SIZES):
            z = frames[size].resize((128, 128), Image.NEAREST) if size < 128 else frames[size].resize((128, 128), Image.LANCZOS)
            zoom.alpha_composite(z, ((i % 6) * 128, (i // 6) * 132))
        zoom.save(d / "icon-sizes-zoomed.png")


main()
