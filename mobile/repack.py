#!/usr/bin/env python3
"""把 FCL 的发行包改成「琉璃启动器」：
   换应用名 / 换图标 / 加致谢 / 换主题色，保留 GPL-3.0 与原作者署名。

用法：python repack.py <FCL.apk>
依赖：apktool.jar、JDK、build-tools（zipalign + apksigner）
"""
import os, re, sys, shutil, subprocess, zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
TC   = r"C:\Users\m1597\.workbuddy-ai\toolchain"
JDK  = os.path.join(TC, "jdk-17.0.20.1+1")
BT   = os.path.join(TC, "bt", "android-14")
APKTOOL = os.path.join(HERE, "apktool.jar")
KS   = r"C:\Users\m1597\WorkBuddy AI\2026-10-27-21-39-52\LazuliMobile\keystore\lazuli.keystore"

APP_NAME = "琉璃启动器"
CREDIT = ("特别鸣谢\n"
          "本应用基于 FCL（Fold Craft Launcher）修改，使用了其部分代码。\n"
          "FCL 遵循 GNU General Public License v3.0 开源。\n"
          "原项目：https://github.com/FCL-Team/FoldCraftLauncher\n"
          "同时感谢 PojavLauncher、Boardwalk、GL4ES、LWJGL 等开源项目。\n"
          "本修改版同样以 GPL-3.0 发布。")


def run(cmd, **kw):
    r = subprocess.run(cmd, shell=True, capture_output=True, text=True, encoding="utf-8", errors="ignore", **kw)
    if r.returncode != 0:
        print(r.stdout[-3000:])
        print(r.stderr[-3000:])
    return r.returncode, r.stdout or ""


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "FCL.apk")
    if not os.path.exists(src):
        print("找不到 APK:", src); return 1

    work = os.path.join(HERE, "work")
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    out = os.path.join(work, "dec")

    java = os.path.join(JDK, "bin", "java.exe")
    print("[1/5] apktool 反编译")
    rc, _ = run(f'"{java}" -jar "{APKTOOL}" d -f -o "{out}" "{src}"')
    if rc != 0: return 1

    print("[2/5] 改应用名")
    strs = os.path.join(out, "res", "values", "strings.xml")
    s = open(strs, encoding="utf-8").read()
    s2 = re.sub(r'(<string name="app_name">)(.*?)(</string>)',
                lambda m: m.group(1) + APP_NAME + m.group(3), s, count=1)
    if s2 == s:
        print("  ! 没找到 app_name，尝试其它键")
    open(strs, "w", encoding="utf-8").write(s2)

    print("[3/5] 加致谢")
    # 追加到 strings.xml 末尾，并尝试挂到已有的「关于」类字符串上
    add = '\n    <string name="lazuli_credit">' + CREDIT.replace("&", "&amp;").replace("<", "&lt;") + '</string>\n'
    s = open(strs, encoding="utf-8").read()
    if "lazuli_credit" not in s:
        s = s.replace("</resources>", add + "</resources>")
    open(strs, "w", encoding="utf-8").write(s)

    # 找所有 values*/strings.xml 里像「关于」的条目，把致谢拼进去
    hit = 0
    for root, _, files in os.walk(os.path.join(out, "res")):
        for fn in files:
            if fn != "strings.xml": continue
            p = os.path.join(root, fn)
            t = open(p, encoding="utf-8").read()
            def repl(m):
                nonlocal hit
                if "FCL" in m.group(2) or "致谢" in m.group(2): return m.group(0)
                hit += 1
                return m.group(1) + m.group(2) + "\\n\\n" + CREDIT + m.group(3)
            t2 = re.sub(r'(<string name="[^"]*(?:about|About|关于)[^"]*">)(.*?)(</string>)', repl, t, flags=re.S)
            if t2 != t: open(p, "w", encoding="utf-8").write(t2)
    print(f"  挂到 {hit} 处「关于」文本")

    print("[4/5] 换图标")
    n = 0
    for root, _, files in os.walk(os.path.join(out, "res")):
        base = os.path.basename(root)
        for fn in files:
            if not fn.startswith("ic_launcher") or not fn.endswith(".png"): continue
            dens = "xxhdpi"
            for d in ("mdpi", "hdpi", "xhdpi", "xxhdpi", "xxxhdpi"):
                if d in base: dens = d
            srcpng = os.path.join(HERE, "icons", f"ic_launcher_{dens}.png")
            if os.path.exists(srcpng):
                shutil.copy(srcpng, os.path.join(root, fn))
                n += 1
    print(f"  替换 {n} 个图标")

    print("[5/5] 重新打包 + 签名")
    unsigned = os.path.join(work, "unsigned.apk")
    rc, _ = run(f'"{java}" -jar "{APKTOOL}" b -o "{unsigned}" "{out}"')
    if rc != 0: return 1

    aligned = os.path.join(work, "aligned.apk")
    run(f'"{os.path.join(BT, "zipalign.exe")}" -f -p 4 "{unsigned}" "{aligned}"')

    final = os.path.join(HERE, "Lazuli-FCL.apk")
    env = os.environ.copy()
    env["JAVA_HOME"] = JDK
    r = subprocess.run(
        f'"{os.path.join(BT, "apksigner.bat")}" sign --ks "{KS}" '
        f'--ks-pass pass:lazuli123 --key-pass pass:lazuli123 '
        f'--out "{final}" "{aligned}"',
        shell=True, capture_output=True, text=True, env=env)
    if r.returncode != 0:
        print(r.stdout[-2000:], r.stderr[-2000:]); return 1

    print("完成 ->", final, round(os.path.getsize(final) / 1048576, 1), "MB")
    return 0


if __name__ == "__main__":
    sys.exit(main())
