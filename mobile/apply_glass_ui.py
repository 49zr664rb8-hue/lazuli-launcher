"""把 FCL 的界面改成深色玻璃 + 翡翠绿（对齐桌面版视觉）
   只动资源层：shape drawable / 主题属性 / 主题色，不碰 Kotlin 逻辑。
"""
import os, re, glob

DEC = os.path.join(os.path.dirname(os.path.abspath(__file__)), "dec")
DRAWABLE = os.path.join(DEC, "res", "drawable")

# 桌面版配色
BG_TOP   = "#FF0B1017"
BG_BOT   = "#FF151C28"
CARD     = "#CC141B26"
CARD_HI  = "#CC1B2330"
STROKE   = "#33FFFFFF"
STROKE_HI= "#4D3DDC84"
ACCENT   = "#FF3DDC84"
TEXT     = "#FFE8EEF6"
TEXT2    = "#FF9AA7B8"
TEXT3    = "#FF687485"

GLASS = """<?xml version="1.0" encoding="utf-8"?>
<shape xmlns:android="http://schemas.android.com/apk/res/android">
    <corners android:radius="{r}"/>
    <solid android:color="{fill}"/>
    <stroke android:width="1dp" android:color="{stroke}"/>
</shape>
"""

RIPPLE = """<?xml version="1.0" encoding="utf-8"?>
<ripple xmlns:android="http://schemas.android.com/apk/res/android"
    android:color="#33FFFFFF">
    <item>
        <shape>
            <corners android:radius="{r}"/>
            <solid android:color="{fill}"/>
            <stroke android:width="1dp" android:color="{stroke}"/>
        </shape>
    </item>
</ripple>
"""

# 名字 -> (圆角, 是否 ripple)
TARGETS = {
    "bg_container_white":            ("18dp", False),
    "bg_container_white_clickable":  ("18dp", True),
    "bg_item":                       ("14dp", False),
    "bg_item_clickable":             ("14dp", True),
    "bg_item_rounded":               ("14dp", False),
    "bg_tab_top_rounded":            ("18dp", False),
    "dialog_background":             ("22dp", False),
}

# 只有部分圆角的
PARTIAL = {
    "bg_item_rounded_top":    ("topLeftRadius", "topRightRadius"),
    "bg_item_rounded_bottom": ("bottomLeftRadius", "bottomRightRadius"),
}


def write_glass():
    n = 0
    for name, (r, ripple) in TARGETS.items():
        p = os.path.join(DRAWABLE, name + ".xml")
        if not os.path.exists(p):
            print(f"  ! 缺 {name}.xml"); continue
        tpl = RIPPLE if ripple else GLASS
        open(p, "w", encoding="utf-8").write(
            tpl.format(r=r, fill=CARD, stroke=STROKE))
        n += 1
    for name in PARTIAL:
        p = os.path.join(DRAWABLE, name + ".xml")
        if not os.path.exists(p):
            continue
        xml = ('<?xml version="1.0" encoding="utf-8"?>\n'
               '<shape xmlns:android="http://schemas.android.com/apk/res/android">\n'
               f'    <corners android:{PARTIAL[name][0]}="18dp" android:{PARTIAL[name][1]}="18dp"/>\n'
               f'    <solid android:color="{CARD}"/>\n'
               f'    <stroke android:width="1dp" android:color="{STROKE}"/>\n'
               '</shape>\n')
        open(p, "w", encoding="utf-8").write(xml)
        n += 1
    print(f"  重写 {n} 个容器 drawable")


def add_bg():
    p = os.path.join(DRAWABLE, "lazuli_bg.xml")
    open(p, "w", encoding="utf-8").write(
        '<?xml version="1.0" encoding="utf-8"?>\n'
        '<shape xmlns:android="http://schemas.android.com/apk/res/android">\n'
        '    <gradient android:angle="270" android:startColor="%s" android:endColor="%s"/>\n'
        '</shape>\n' % (BG_TOP, BG_BOT))
    print("  新增 lazuli_bg 渐变背景")


DARK_ITEMS = f"""
        <item name="android:windowBackground">@drawable/lazuli_bg</item>
        <item name="android:colorBackground">{BG_TOP}</item>
        <item name="colorSurface">{CARD}</item>
        <item name="colorOnSurface">{TEXT}</item>
        <item name="colorOnBackground">{TEXT}</item>
        <item name="colorOnPrimary">#FF06231A</item>
        <item name="colorSurfaceVariant">{CARD_HI}</item>
        <item name="colorOnSurfaceVariant">{TEXT2}</item>
        <item name="colorOutline">{STROKE}</item>
        <item name="android:colorForeground">{TEXT}</item>
        <item name="android:textColorPrimary">{TEXT}</item>
        <item name="android:textColorSecondary">{TEXT2}</item>
        <item name="android:textColorTertiary">{TEXT3}</item>
        <item name="android:textColorHint">{TEXT3}</item>
        <item name="android:statusBarColor">{BG_TOP}</item>
        <item name="android:navigationBarColor">{BG_BOT}</item>
        <item name="android:windowLightStatusBar">false</item>
"""


def patch_theme():
    n = 0
    for p in glob.glob(os.path.join(DEC, "res", "values*", "styles.xml")):
        s = open(p, encoding="utf-8").read()
        if 'name="Theme.FoldCraftLauncher"' not in s:
            continue
        if "lazuli_bg" in s:
            continue
        # 在该 style 的结束标签前插入深色属性
        m = re.search(r'(<style name="Theme\.FoldCraftLauncher"[^>]*>)(.*?)(</style>)', s, re.S)
        if not m:
            continue
        body = m.group(2).rstrip()
        new = m.group(1) + body + "\n" + DARK_ITEMS + "    " + m.group(3)
        s = s[:m.start()] + new + s[m.end():]
        open(p, "w", encoding="utf-8").write(s)
        n += 1
    print(f"  深色主题应用到 {n} 个 styles.xml")


def patch_accent():
    p = os.path.join(DEC, "res", "values", "colors.xml")
    s = open(p, encoding="utf-8").read()
    s2 = re.sub(r'(<color name="default_theme_color">).*?(</color>)',
                r'\g<1>' + ACCENT + r'\g<2>', s)
    if s2 != s:
        open(p, "w", encoding="utf-8").write(s2)
        print("  主题色 -> 翡翠绿 #3DDC84")
    # 顺带把几个明显的白底色改掉
    for k, v in [("card_background", CARD), ("background", BG_TOP)]:
        s = open(p, encoding="utf-8").read()
        s2 = re.sub(r'(<color name="%s">).*?(</color>)' % k, r'\g<1>' + v + r'\g<2>', s)
        if s2 != s:
            open(p, "w", encoding="utf-8").write(s2)
            print(f"  {k} -> {v}")


if __name__ == "__main__":
    print("[1] 容器 drawable")
    write_glass()
    print("[2] 背景渐变")
    add_bg()
    print("[3] 主题深色化")
    patch_theme()
    print("[4] 主题色")
    patch_accent()
    print("完成")
