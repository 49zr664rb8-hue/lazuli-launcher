"""把反编译后的 FCL 改成「琉璃启动器」：应用名 / 关于页致谢 / 图标 / 主题色"""
import os, re, glob, io
from PIL import Image, ImageDraw, ImageFont, ImageFilter

DEC = os.path.join(os.path.dirname(os.path.abspath(__file__)), "dec")
ICON_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "icons")

APP_NAME = "琉璃启动器"

NEW_DESC = """琉璃启动器是一个基于 FCL（Fold Craft Launcher）修改的 Android 版 Minecraft Java 启动器。

特别鸣谢
本应用基于 FCL（Fold Craft Launcher）修改，使用了其部分代码。
FCL 遵循 GNU General Public License v3.0 开源，原项目地址：
https://github.com/FCL-Team/FoldCraftLauncher

同时感谢以下开源项目：
· PojavLauncher / Boardwalk —— JVM 启动与图形转译思路
· GL4ES —— 桌面 OpenGL 到 OpenGL ES 的转译层
· LWJGL —— 图形 / 音频 / 窗口绑定
· HMCL —— FCL 的部分核心功能源自 HMCLCore

按照 GPL-3.0 的要求，本修改版同样以 GPL-3.0 发布，源码可向作者索取。"""


def patch_strings():
    n = 0
    for p in glob.glob(os.path.join(DEC, "res", "values*", "strings.xml")):
        s = open(p, encoding="utf-8").read()
        o = s
        # 应用名（所有语言都统一）
        s = re.sub(r'<string name="app_name">.*?</string>',
                   f'<string name="app_name">{APP_NAME}</string>', s, flags=re.S)
        # 关于页显示的名字
        s = re.sub(r'<string name="about_launcher">.*?</string>',
                   f'<string name="about_launcher">{APP_NAME}</string>', s, flags=re.S)
        if s != o:
            open(p, "w", encoding="utf-8").write(s)
            n += 1
    print(f"  改了 {n} 个 strings.xml 的应用名")

    # 关于页正文只改默认语言（中文设备会用 values-zh，一起改）
    for p in [os.path.join(DEC, "res", "values", "strings.xml")]:
        if not os.path.exists(p):
            continue
        s = open(p, encoding="utf-8").read()
        esc = NEW_DESC.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
        s2 = re.sub(r'<string name="about_desc">.*?</string>',
                    f'<string name="about_desc">{esc}</string>', s, flags=re.S)
        open(p, "w", encoding="utf-8").write(s2)
        print("  关于页正文已替换" if s2 != s else "  ! about_desc 未找到")

    zh = os.path.join(DEC, "res", "values-zh", "strings.xml")
    if os.path.exists(zh):
        s = open(zh, encoding="utf-8").read()
        if 'name="about_desc"' in s:
            esc = NEW_DESC.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
            s = re.sub(r'<string name="about_desc">.*?</string>',
                       f'<string name="about_desc">{esc}</string>', s, flags=re.S)
            open(zh, "w", encoding="utf-8").write(s)
            print("  values-zh 关于页已替换")


def patch_colors():
    p = os.path.join(DEC, "res", "values", "colors.xml")
    if not os.path.exists(p):
        return
    s = open(p, encoding="utf-8").read()
    s2 = re.sub(r'(<color name="icon_background_color">).*?(</color>)',
                r'\g<1>#FF10161F\g<2>', s)
    if s2 != s:
        open(p, "w", encoding="utf-8").write(s2)
        print("  图标背景色 -> #10161F")


def mark(size, bg=False, radius=0.0):
    """画一个尺寸为 size 的图标；bg=True 画深色底，False 画透明前景"""
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    if bg:
        d = ImageDraw.Draw(img)
        d.rounded_rectangle([0, 0, size - 1, size - 1], radius=int(size * radius),
                            fill=(16, 22, 31, 255))
        g = Image.new("RGBA", (size, size), (0, 0, 0, 0))
        ImageDraw.Draw(g).ellipse([size * 0.14, size * 0.14, size * 0.86, size * 0.86],
                                  fill=(40, 200, 120, 70))
        g = g.filter(ImageFilter.GaussianBlur(size * 0.09))
        img = Image.alpha_composite(img, g)
    d = ImageDraw.Draw(img)
    f = ImageFont.truetype("C:/Windows/Fonts/msyhbd.ttc", int(size * 0.46))
    tw = d.textlength("琉", font=f)
    d.text(((size - tw) / 2, size * 0.5 - size * 0.33), "琉", font=f, fill=(70, 240, 160, 255))
    return img


def patch_icons():
    dens = {"mdpi": 108, "hdpi": 162, "xhdpi": 216, "xxhdpi": 324, "xxxhdpi": 432}
    n = 0
    for p in glob.glob(os.path.join(DEC, "res", "mipmap*", "*")):
        base = os.path.basename(p)
        if not base.startswith("ic_launcher"):
            continue
        if not (base.endswith(".webp") or base.endswith(".png")):
            continue
        folder = os.path.basename(os.path.dirname(p))
        d = "xxhdpi"
        for k in dens:
            if k in folder:
                d = k
        size = dens.get(d, 216)
        out_png = os.path.splitext(p)[0] + ".png"

        if "foreground" in base:
            im = mark(size)                      # 透明前景
        elif "round" in base:
            im = mark(size, bg=True, radius=0.5)  # 圆形底
        else:
            im = mark(size, bg=True, radius=0.22)  # 方形圆角底

        im.save(out_png)
        if p != out_png and os.path.exists(p):
            os.remove(p)                          # 同名不同扩展会冲突
        n += 1
    print(f"  替换 {n} 个图标文件")


if __name__ == "__main__":
    print("[1] 应用名与致谢")
    patch_strings()
    print("[2] 主题色")
    patch_colors()
    print("[3] 图标")
    patch_icons()
    print("完成")
