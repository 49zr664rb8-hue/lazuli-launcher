# 琉璃启动器 · Lazuli Launcher

一个 Minecraft Java 版启动器，两个平台：

| 平台 | 说明 |
| --- | --- |
| **Windows** | 纯 C11 + Win32 + GDI+ 手写，无第三方依赖，单个 exe |
| **Android** | 基于 FCL（Fold Craft Launcher）改造，保留其 JVM / GL4ES / LWJGL 核心 |

> 非官方产品，与 Mojang Studios、Microsoft 无隶属关系。

---

## 目录

```
desktop-src/     Windows 版源码（C11，约 8800 行）
tools/           开发期用的截图/调试脚本
mobile/          Android 版改造脚本（基于 FCL 发行包反编译）
site/            官网页面
build.bat        Windows 版构建脚本
LICENSE          GPL-3.0
```

---

## Windows 版

**构建**（需要 MinGW-w64 的 gcc 与 windres）：

```bat
build.bat
```

产物在 `build/LazuliLauncher.exe`。

**功能**

- 微软正版登录（设备代码流）
- 版本清单 / 一键安装（jar + 库 + 资源）
- 加载器：Fabric / Forge / NeoForge
- 模组与整合包管理
- 皮肤预览与上传
- 陶瓦联机

---

## Android 版

Android 版**不是**从零写的，而是在 [FCL](https://github.com/FCL-Team/FoldCraftLauncher) 的发行包上做资源层与 smali 层改造：

- 换应用名 / 图标 / 主题色
- 重排主界面（账户+版本+启动移到左侧浮动卡，功能图标改底部横向导航）
- 自绘 7 个 vector 图标
- 深色渐变壁纸 + 中性玻璃面板（白 7% 填充 + 白 10% 描边）
- 强制深色模式（`ThemeEngine.isNightMode` 恒 true）
- 停用主题染色（`ThemeEngine.applyColor` 空实现）
- 重写用户协议与隐私政策（13 种语言）

改造脚本在 `mobile/`，配合 [apktool](https://apktool.org/) 使用：

```bash
apktool d FCL-release-x.x.x-all.apk -o dec
python mobile/apply_rebrand.py       # 换名字/图标/关于页
python mobile/apply_glass_ui.py      # 深色玻璃主题
python mobile/apply_accent_legal.py  # 主题色 + 协议
python mobile/apply_debrand.py       # 清除 FCL 字样
apktool b dec -o unsigned.apk
zipalign -f -p 4 unsigned.apk aligned.apk
apksigner sign --ks <你的密钥> --out Lazuli-Launcher-Mobile.apk aligned.apk
```

**安装包**见 [Releases](../../releases)。

---

## 开源说明

本项目遵循 **GNU General Public License v3.0**。

Android 版使用了 FCL 的部分代码，因此同样以 GPL-3.0 发布。同时感谢：

- [FCL / Fold Craft Launcher](https://github.com/FCL-Team/FoldCraftLauncher) — GPL-3.0
- [PojavLauncher](https://github.com/PojavLauncherTeam/PojavLauncher) — JVM 启动与图形转译思路
- [GL4ES](https://github.com/ptitSeb/gl4es) — 桌面 OpenGL 到 OpenGL ES 转译
- [LWJGL](https://www.lwjgl.org/) — 图形 / 音频 / 窗口绑定
- [HMCL](https://github.com/HMCL-dev/HMCL) — FCL 的部分核心功能源自 HMCLCore
