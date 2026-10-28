"""1) 把硬编码的主题色从蓝色换成翡翠绿
   2) 用我们自己的用户协议 / 隐私政策替换 assets/legal 下的全部文档
"""
import os, re, glob

DEC = os.path.join(os.path.dirname(os.path.abspath(__file__)), "dec")

# ---------- 1. 主题色 ----------
def fix_accent():
    n = 0
    for p in glob.glob(os.path.join(DEC, "smali*", "**", "*.smali"), recursive=True):
        try:
            s = open(p, encoding="utf-8").read()
        except Exception:
            continue
        o = s
        s = s.replace("-0x886831", "0xff3ddc84")   # #FF7797CF -> 翡翠绿
        s = s.replace("-0xb1883f", "0xff2fb86f")   # #FF4E77C1 -> 深翡翠
        if s != o:
            open(p, "w", encoding="utf-8").write(s)
            n += 1
    print(f"  主题色硬编码清理：{n} 个 smali")


# ---------- 2. 协议文档 ----------
EULA = """琉璃启动器 用户协议

一、关于本应用
琉璃启动器是一个用于在 Android 设备上安装与启动 Minecraft Java 版的第三方启动器。
本应用使用了部分 FCL（Fold Craft Launcher）的代码，遵循 GNU General Public License v3.0。
本应用非官方产品，与 Mojang Studios、Microsoft 无隶属关系。

二、使用条件
1. 你需要自行准备游戏本体所需的账号与文件。
2. 你应遵守 Minecraft 最终用户许可协议（EULA）及所在地区的法律法规。
3. 不得将本应用用于任何商业用途或违法用途。

三、免责声明
1. 本应用按“现状”提供，不提供任何明示或暗示的担保。
2. 因使用本应用造成的存档损坏、数据丢失、账号异常等后果，由使用者自行承担。
3. 请自行备份重要存档。

四、开源与致谢
本应用基于以下开源项目：
· FCL / Fold Craft Launcher（GPL-3.0）
· PojavLauncher / Boardwalk
· GL4ES
· LWJGL
· HMCL（FCL 的部分核心功能源自 HMCLCore）
按照 GPL-3.0 的要求，本修改版同样以 GPL-3.0 发布。

五、协议变更
本协议可能随版本更新而调整，更新后继续使用即视为接受新协议。
"""

PRIVACY = """琉璃启动器 隐私政策

一、我们收集什么
本应用不收集、不上传任何个人身份信息。

二、数据存在哪里
以下数据仅保存在你的设备本地：
· 账号登录凭证（用于启动游戏）
· 游戏文件、存档、配置
· 应用设置与主题偏好
卸载本应用会一并删除上述数据。

三、网络访问
本应用会在以下情况访问网络：
· 下载游戏本体、依赖库、资源文件
· 获取版本清单与模组信息
· 你主动使用的联机功能
· 检查应用更新
这些请求仅用于完成对应功能，不会附带你的个人信息。

四、第三方服务
游戏文件与模组数据来自 Mojang、Modrinth 等公开服务，
其数据处理遵循各自的隐私政策。

五、权限说明
· 存储权限：读写游戏文件与存档，这是运行游戏所必需的。
· 网络权限：下载游戏与联机。
· 通知权限：显示下载与安装进度。

六、联系方式
如有疑问，可通过项目主页反馈。
"""


def fix_legal():
    root = os.path.join(DEC, "assets", "legal")
    if not os.path.isdir(root):
        print("  ! 没找到 assets/legal")
        return
    n = 0
    for lang in os.listdir(root):
        d = os.path.join(root, lang)
        if not os.path.isdir(d):
            continue
        for name, text in (("eula.txt", EULA), ("privacy.txt", PRIVACY)):
            p = os.path.join(d, name)
            if os.path.exists(p):
                open(p, "w", encoding="utf-8").write(text)
                n += 1
    print(f"  替换协议文档：{n} 个文件（{len(os.listdir(root))} 种语言）")


if __name__ == "__main__":
    print("[1] 主题色")
    fix_accent()
    print("[2] 协议与隐私政策")
    fix_legal()
    print("完成")
