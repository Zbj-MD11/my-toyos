# 测试结果截图

生成方式（一条命令）：

```bash
bash tests/run_all.sh              # 跑测试 + 自动出图
bash tests/run_all.sh --render     # 只用真实日志渲染 PNG（20 张 + 汇总）
bash tests/run_all.sh --gui        # 逐屏 gnome-screenshot（需要桌面环境）
```

- `--render`：`tests/render_shot.py` 把 `tests/results/*.log` 画成终端窗口样式，标题栏是用例 id + 说明，右上角是 PASS/FAIL，底部是启动参数；不依赖桌面。
- `--gui`：`tests/screenshot_cases.sh` 逐屏截图（需 `gnome-screenshot`，终端最大化）。

文件名 = 用例 id，`test-report.md` 里用 `![用例01](images/01_boot.png)` 引用。
日志里的退格字节（`0x08`）在图上显示成 `\b`，便于阅读。

| 文件 | 内容 |
| --- | --- |
| `00-summary.png` | `bash tests/run_tests.sh` 的 PASS/FAIL 汇总 + 按类别统计 |
| `01_boot.png` | 原样内核双核启动 + 欢迎横幅 + ticks 递增 |
| `02_path_trap_init.png` | `stvec` / `sie` / `sstatus.SIE` 三个开关 |
| `03_bva_tick_zero.png` | 启动刚结束 `ticks0 = 0` |
| `04_path_tick_monotonic.png` | 5 个 tick 递增 + `scause` 是 SSI + `SPIE=1` |
| `05_path_ctx_regs.png` | s0–s11 哨兵跨 3 次中断不变 |
| `06_eq_uart_echo.png` | `abc` 回显 |
| `07_bva_uart_fifo16.png` | 16 字符一次写入全部回显 |
| `08_eq_uart_backspace.png` | 退格后屏幕是 `ac` |
| `09_eq_uart_crlf.png` | `\r` / `\n` / CRLF 三行只出 3 个换行 |
| `10_bva_uart_bs_start.png` | 行首退格不输出，屏幕是 `b` |
| `11_eq_uart_multiline.png` | 连续三行都回显（验证 plic_complete） |
| `12_bva_uart_empty_line.png` | 只送 `\r`：无非空输出、1 个换行 |
| `13_fi_ecall.png` | ecall → 打印原因 + panic |
| `14_fi_illegal.png` | 非法指令 → 打印原因 + panic |
| `15_fi_load_pf.png` | 读未映射地址 → Load page fault |
| `16_fi_store_pf.png` | 写未映射地址 → Store/AMO page fault |
| `17_con_smp2_tick.png` | 双核 4 秒 40 个 tick（未翻倍） |
| `18_con_smp2_uart.png` | hart1 收到 SSI 且 SSIP 被清；串口回显 |
| `19_st_soak.png` | 25 tick 内 tick 与输入交错，无死锁 |

截图后按 `test-report.md` 第 3 节的方式引用：`![用例01](images/01_boot.png)`。
