# Ae 2027 Unicode Fixer

> 修复 Adobe After Effects 2027（macOS / Apple 芯片）里第三方汉化插件弹窗「无法转换 Unicode 字符」的问题。

[![Release](https://img.shields.io/badge/release-v1.0-2ea44f)](https://github.com/wibi8bo/Ae2027_Unicode_Fixer/releases/latest)
[![Platform](https://img.shields.io/badge/platform-macOS%20arm64-lightgrey)](#适用环境)
[![License](https://img.shields.io/badge/license-MIT-blue)](LICENSE)

---

## 问题是什么

升级到 **Ae 2027（27.0.x）** 之后，Ae 更换了文本处理模块，汉化过的第三方插件（VideoCopilot Saber / Element 等）应用这类汉化插件时会弹窗

```
无法转换 Unicode 字符。
增效工具效果“xxx”中的参数计数不匹配
由于无法初始化，因此无法应用此效果。
```

**本插件提供 Ae 2027 上述报错结果方案。**

## 特性

| 项目   | 说明                                       |
| ---- | ---------------------------------------- |
| 生效方式 | Ae 启动时静默加载，**不新增任何效果菜单项**                |
| 转换范围 | 仅当输入不是合法 UTF-8、且转换结果确实含中日韩字符时才替换         |
| 兼容性  | 不修改任何第三方插件文件，Saber、Element 等可原样使用        |
| 安全性  | 合法 UTF-8 / ASCII 一律原样透传，不影响 Ae 自身功能与英文插件 |
| 架构   | macOS Apple 芯片（arm64）；Intel Mac 暂不支持     |

## 适用环境

- macOS + Apple 芯片（M1 / M2 / M3 / M4）
- Adobe After Effects 2027（27.0.x）中文版
- 症状：应用汉化第三方插件时弹窗「无法转换 Unicode 字符」，参数显示乱码

## 下载与安装

前往 [Releases](https://github.com/wibi8bo/Ae2027_Unicode_Fixer/releases/latest) 下载
`Ae2027_Unicode_Fixer-macOS-v1.0.zip`，解压后得到：

```
!00_UTF8_Fixer_2027.plugin
install.command
安装说明.txt
```

### 方法一：一键安装（推荐）

1. 请确认 Ae 版本为 2027，完全退出 After Effects（⌘Q）。
2. 双击 `install.command`。
3. 按提示输入一次开机密码（需要写入 `/Applications`）。
4. 看到「安装成功」后重新打开 Ae 2027 即可。

> 若双击提示「无法打开，因为无法验证开发者」，右键 → 打开，或在终端执行
> `xattr -dr com.apple.quarantine <解压目录>` 后再双击。

### 方法二：手动安装

把 `!00_UTF8_Fixer_2027.plugin` 整个文件夹拖入：

```
/Applications/Adobe After Effects 2027/Plug-ins/
```

然后在终端执行一次：

```bash
xattr -dr com.apple.quarantine "/Applications/Adobe After Effects 2027/Plug-ins/!00_UTF8_Fixer_2027.plugin"
codesign --force --deep --sign - "/Applications/Adobe After Effects 2027/Plug-ins/!00_UTF8_Fixer_2027.plugin"
```

`Plug-ins` 目录属主是 root，拖拽时可能需要输入管理员密码。

### 卸载

```bash
sudo rm -rf "/Applications/Adobe After Effects 2027/Plug-ins/!00_UTF8_Fixer_2027.plugin"
```

## 工作原理

旧版插件只能 hook 那些入口函数叫 `PluginMain` 的插件；Saber / Element 走的是
`EntryPointFunc`，所以旧方案覆盖不到。本插件改为在 **Ae 宿主进程内**接管文本转换入口
`dvacore::string::UTF8to16`：

```
汉化插件传入 GBK 字符串
        │
        ▼
  dvacore::string::UTF8to16   ← 本插件在此挂钩
        │
        ├── 合法 UTF-8 ────────────► 原样透传
        │
        └── 非法 UTF-8 ──► GB18030 解码 ──► 含中日韩字符?
                                              ├── 是 ──► 转成 UTF-8 再交给 Ae
                                              └── 否 ──► 原样透传（避免误伤 MacRoman 等）
```

具体做法：

1. `dlsym` 定位 `dvacore::string::UTF8to16`（Ae 2027 上符号未 strip，可直接取到）。
2. 在函数入口写入 16 字节跳转（`ldr x16, #8; br x16; <hook>`），并用 `mmap(MAP_JIT)`
   分配 trampoline 重放被覆盖的 4 条指令后跳回原函数。
3. Hook 里做 UTF-8 合法性校验，非法时才用 libiconv 的 `GB18030 → UTF-8` 转码。
4. 转码结果只在前 11 个字符以内时内联在返回对象里（libc++ SSO），长度写回出参。

因为改的是宿主而不是插件，所以不需要重新打包任何一个汉化插件。

## 注意事项

- 效果（Effects）菜单里**不会**多出任何条目，插件在 Ae 启动时静默加载，属于正常现象。
- Ae 27.x 小版本升级后通常仍然有效；如果某次 Adobe 大改后失效，请开 issue，会跟进更新。
- 同一份插件**只能安装一次**。不要同时在 `Plug-ins/` 根目录和子目录里各放一份，否则会重复 hook 导致 Ae 崩溃。
- 插件为 ad-hoc 签名（无开发者证书），首次安装需要放行。

## 排查日志

插件会写少量日志到 `/tmp/ae2027_fixer.log`。正常启动只有三行：

```
[UTF8Fixer2027] dvacore::string::UTF8to16 at 0x...
[UTF8Fixer2027] patched 0x...
[UTF8Fixer2027] hooked dvacore::string::UTF8to16
```

应用汉化插件时若出现

```
[UTF8Fixer2027] converted GBK -> UTF-8: "..."
```

说明转换已生效。若日志里是 `keep non-utf8 as-is`，说明该字符串被判定为其它 8-bit
编码而跳过，请把日志和复现步骤一并提 issue。

临时关闭修复（用于对比验证）：

```bash
launchctl setenv AE27_PASSTHROUGH 1    # 关闭，重启 Ae 生效
launchctl unsetenv AE27_PASSTHROUGH    # 恢复
```

## 从源码构建

需要 Xcode Command Line Tools。

```bash
./build.sh                     # 输出到 build/!00_UTF8_Fixer_2027.plugin
```

`build.sh` 会调用 `src/make_rsrc.py` 生成 Ae 需要的 PiPL 资源
（`Contents/Resources/!00_UTF8_Fixer_2027.rsrc`），然后编译 `src/fixer.cpp` 并做
ad-hoc 签名。

手动构建等价命令：

```bash
clang++ -arch arm64 -std=c++17 -O2 -bundle -undefined dynamic_lookup \
  -o build/!00_UTF8_Fixer_2027.plugin/Contents/MacOS/!00_UTF8_Fixer_2027 \
  src/fixer.cpp -liconv
codesign --force --deep -s - build/!00_UTF8_Fixer_2027.plugin
```

## 目录结构

```
.
├── README.md
├── LICENSE
├── 安装说明.txt              # 简版说明（基础介绍 + 安装教程），随发布包分发
├── install.command           # 一键安装脚本
├── build.sh
├── src
│   ├── fixer.cpp             # hook + GB18030 转码逻辑
│   ├── make_rsrc.py          # 生成 PiPL 资源
│   └── Info.plist
├── !00_UTF8_Fixer_2027.plugin                 # 构建产物，不纳入版本管理
└── Ae2027_Unicode_Fixer-macOS-v1.0.zip        # 发布包，不纳入版本管理
```

## 版本记录

| 版本 | 说明 |
| --- | --- |
| v1.0 | 首个公开版本。改为 hook 宿主 `dvacore::string::UTF8to16`，覆盖 Saber / Element 等 `EntryPointFunc` 插件 |

## 感谢
感谢 B 站 up 主 @AdoreLowen 早起贡献，原仓库地址[AE-UTF8-Fixer](https://github.com/AdoreLowen/AE-UTF8-Fixer)

## 许可

[MIT](LICENSE)
