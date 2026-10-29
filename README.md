# 琉璃启动器 · Lazuli Launcher

一个 Minecraft Java 版启动器，两个平台：

| 平台 | 说明 |
| --- | --- |
| **Windows** | 纯 C11 + Win32 + GDI+ 手写，无第三方依赖，单个 exe |
| **Android** | 基于 FCL（Fold Craft Launcher）改造，保留其 JVM / GL4ES / LWJGL 核心 |

> 非官方产品，与 Mojang Studios、Microsoft 无隶属关系。

---

## 下载

Windows 版直接下载：[`dist/LazuliLauncher.exe`](dist/LazuliLauncher.exe)（约 370 KB，免安装）

---

## 更新日志

### v1.1.0

**新功能**
- 版本隔离：每个版本独立的 `mods` / `saves` / `config`，设置里可开关
- 版本列表可直接删除版本（两段确认；被 Forge/Fabric 版本依赖时拒绝删除）
- 皮肤支持本地预览，未登录也能导入衣架

**修复**
- 分块下载：核对磁盘实际大小，不符自动回退单线程重下
- 下载失败不再留下 0 字节的残骸文件；启动前自动清理空 jar
- 彻底移除对 `tar.exe` 的依赖，改用 `Expand-Archive`（避免 libarchive DLL 冲突）
- 模组下载落到全局 `mods` 的问题（`content_kind` 判断错误）
- 版本行「删除」「启动」按钮点不动（命中区注册顺序）
- Forge/NeoForge 安装失败：`--installClient` 不接受路径参数
- Forge 安装器 `Injecting profile` 失败：`launcher_profiles.json` 写入多了一个 `\0`
- Java 版本选错：原来只判断「太旧」，Java 25 跑 1.19.2 会静默崩溃
- 版本 json 未沿 `inheritsFrom` 读取 `javaVersion`
- Forge 模块冲突（`minecraft` 与 `_1._19._2` 重复导出）：修正 `-DignoreList`
- 默认游戏目录改为标准 `%APPDATA%\.minecraft`

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
