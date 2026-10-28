"""去掉界面上的 FCL 痕迹，只保留一句小字致谢"""
import os, re, glob

DEC = os.path.join(os.path.dirname(os.path.abspath(__file__)), "dec")

CREDIT = ("琉璃启动器\\n\\n"
          "本启动器使用了部分 FCL（Fold Craft Launcher）的代码，遵循 GPL-3.0 开源协议。")

# 1) 全局替换 FCL 字样
def strip_brand():
    n = 0
    for p in glob.glob(os.path.join(DEC, "res", "values*", "strings.xml")):
        s = open(p, encoding="utf-8").read()
        o = s
        s = s.replace("Fold Craft Launcher", "琉璃启动器")
        s = re.sub(r"(?<![A-Za-z])FCL(?![A-Za-z])", "琉璃启动器", s)
        # 修掉替换后出现的重复
        s = s.replace("琉璃启动器（琉璃启动器）", "琉璃启动器")
        s = s.replace("琉璃启动器-Team", "琉璃启动器")
        if s != o:
            open(p, "w", encoding="utf-8").write(s)
            n += 1
    print(f"  清理了 {n} 个 strings.xml")


# 2) 关于页只留一句小字
def set_credit():
    n = 0
    for p in [os.path.join(DEC, "res", "values", "strings.xml"),
              os.path.join(DEC, "res", "values-zh", "strings.xml"),
              os.path.join(DEC, "res", "values-zh-rCN", "strings.xml")]:
        if not os.path.exists(p):
            continue
        s = open(p, encoding="utf-8").read()
        s2 = re.sub(r'<string name="about_desc">.*?</string>',
                    f'<string name="about_desc">{CREDIT}</string>', s, flags=re.S)
        s2 = re.sub(r'<string name="about_developer">.*?</string>',
                    '<string name="about_developer">琉璃启动器</string>', s2, flags=re.S)
        if s2 != s:
            open(p, "w", encoding="utf-8").write(s2)
            n += 1
    print(f"  致谢写入 {n} 个文件")


# 3) 公告走远程，把 URL 改成无效地址
def kill_announcement():
    n = 0
    for p in glob.glob(os.path.join(DEC, "smali*", "**", "*.smali"), recursive=True):
        try:
            s = open(p, encoding="utf-8").read()
        except Exception:
            continue
        if "announcement_v2.txt" not in s:
            continue
        s2 = s.replace(
            "https://gitee.com/fcl-team/FCL-Repo/raw/main/res/announcement_v2.txt",
            "https://127.0.0.1/announcement_v2.txt")
        s2 = s2.replace(
            "https://raw.githubusercontent.com/FCL-Team/FCL-Repo/refs/heads/main/res/announcement_v2.txt",
            "https://127.0.0.1/announcement_v2.txt")
        if s2 != s:
            open(p, "w", encoding="utf-8").write(s2)
            n += 1
    print(f"  停用公告请求：{n} 处")


if __name__ == "__main__":
    print("[1] 清理 FCL 字样")
    strip_brand()
    print("[2] 关于页致谢")
    set_credit()
    print("[3] 公告")
    kill_announcement()
    print("完成")
