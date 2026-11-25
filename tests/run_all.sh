#!/usr/bin/env bash
# ============================================================
# tests/run_all.sh —— 一条命令：跑完全部用例 + 自动出图
#
# 用法:
#   bash tests/run_all.sh              # 跑测试 + 自动出图（有桌面用真截图，没有就渲染 PNG）
#   bash tests/run_all.sh --render     # 强制用日志渲染 PNG（不动鼠标，ssh 也能用）
#   bash tests/run_all.sh --gui        # 强制用 gnome-screenshot 截终端（终端要先最大化）
#   bash tests/run_all.sh --no-shot    # 只跑测试
#
# 产物:
#   tests/results/<用例>.log      每个用例的串口输出
#   tests/results/summary.txt     汇总表（含按类别统计）
#   docs/testing/images/*.png     截图（每条用例一张 + 00-summary）
# ============================================================

set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1

MODE="auto"
for a in "$@"; do
    case "$a" in
        --gui) MODE="gui" ;;
        --render) MODE="render" ;;
        --no-shot) MODE="none" ;;
        -h|--help) sed -n '2,18p' "$0"; exit 0 ;;
        *) echo "未知参数: $a（可用 --gui / --render / --no-shot）"; exit 2 ;;
    esac
done

mkdir -p "$ROOT/tests/results" "$ROOT/docs/testing/images"

echo
echo "############ 1/3 跑测试 ############"
echo
# 注意：summary.txt 由 run_tests.sh 自己写（它开头会 rm -rf results，用 tee 会写丢）
bash "$ROOT/tests/run_tests.sh"
rc=$?

if [ "$MODE" = "auto" ]; then
    if [ -n "${DISPLAY:-}" ] && command -v gnome-screenshot >/dev/null 2>&1; then
        MODE="gui"
    else
        MODE="render"
    fi
fi

echo
echo "############ 2/3 生成截图（模式: $MODE） ############"
echo
case "$MODE" in
    gui)
        echo "提示: 请保持终端窗口最大化并位于前台，脚本会逐屏截图（每张约 2 秒）。"
        sleep 2
        bash "$ROOT/tests/screenshot_cases.sh"
        ;;
    render)
        if python3 -c "import PIL" >/dev/null 2>&1; then
            python3 "$ROOT/tests/render_shot.py"
        else
            echo "缺少 pillow，无法渲染截图。安装后重试："
            echo "  sudo apt install -y python3-pil fonts-noto-cjk"
        fi
        ;;
    none)
        echo "按 --no-shot 跳过"
        ;;
esac

echo
echo "############ 3/3 产物 ############"
echo " 汇总    : tests/results/summary.txt"
echo " 日志    : tests/results/"
echo " 截图    : docs/testing/images/"
ls -1 "$ROOT/docs/testing/images" 2>/dev/null | head -n 8
echo " ..."
echo
echo " 单用例回看: bash tests/show_case.sh <用例名>"
echo

exit "$rc"
