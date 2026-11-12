#!/bin/zsh

# Ae 2027 汉化插件「无法转换 unicode 字符」修复插件 —— 一键安装
# 使用方法：把本文件与 !00_UTF8_Fixer_2027.plugin 放在同一个文件夹，双击运行。

set -euo pipefail

APP_TITLE="Ae 2027 UTF-8 修复插件"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PLUGIN_NAME="!00_UTF8_Fixer_2027.plugin"
PLUGIN_SRC="${SCRIPT_DIR}/${PLUGIN_NAME}"
AE_DIR="/Applications/Adobe After Effects 2027"
DEST="${AE_DIR}/Plug-ins/${PLUGIN_NAME}"

msg() {
  osascript -e "display dialog \"$1\" buttons {\"好\"} default button \"好\" with icon ${2:-note} with title \"${APP_TITLE}\"" >/dev/null 2>&1 || true
}

if [ ! -d "${PLUGIN_SRC}" ]; then
  msg "未找到 ${PLUGIN_NAME}。\\n请确保 install.command 与插件文件夹放在同一目录。" stop
  exit 1
fi

if [ ! -d "${AE_DIR}" ]; then
  msg "未找到 Adobe After Effects 2027。\\n请确认已安装 Ae 2027（27.0）后再运行。" stop
  exit 1
fi

if pgrep -x "After Effects" >/dev/null; then
  msg "After Effects 正在运行，请先完全退出 Ae 再安装。" caution
  exit 1
fi

INSTALL_SCRIPT="$(mktemp /tmp/ae2027_utf8fix_install.XXXXXX)"

cat > "${INSTALL_SCRIPT}" <<EOS
#!/bin/sh
set -e
SRC="${PLUGIN_SRC}"
DEST="${DEST}"
if [ -e "\$DEST" ]; then
  /bin/rm -rf "\$DEST"
fi
/usr/bin/ditto "\$SRC" "\$DEST"
/usr/bin/xattr -dr com.apple.quarantine "\$DEST" || true
/usr/bin/codesign --force --deep --sign - "\$DEST"
/usr/bin/codesign --verify --deep --strict "\$DEST"
EOS

if osascript -e "do shell script \"sh '${INSTALL_SCRIPT}'\" with administrator privileges" >/dev/null 2>&1; then
  /bin/rm -f "${INSTALL_SCRIPT}"
  msg "安装成功。\\n\\n安装位置：\\n${DEST}\\n\\n现在启动 After Effects 2027，应用汉化的第三方插件（如 Saber / Element）不会再弹「无法转换 unicode 字符」。"
else
  /bin/rm -f "${INSTALL_SCRIPT}"
  msg "安装失败或已取消（需要管理员权限）。\\n\\n也可以手动把 ${PLUGIN_NAME} 拖到：\\n${AE_DIR}/Plug-ins/" stop
  exit 1
fi
