# lab-3 测试用例设计

- 机器可读版本：`tests/cases.tsv`（判定规则以它为准）
- 每条用例一次 QEMU 运行；panic 用例由 `timeout` 结束，退出码非 0 是预期现象
- 用例 ID 与 `cases.tsv` 一致；"模板编号"对应 `ToyOS_测试模板_v2` 的 BVA/EQ/PATH/CON/FI/ST

## 判定方式

| 用例类型 | PASS 的条件 |
| --- | --- |
| 正常用例 | 串口出现 `RESULT: PASS <名字>`，且没有 `panic!` |
| 异常用例（13–16） | 出现 `unexpected exception: <样因>` **且** `panic! trap_kernel_handler`，且没有 `LAB3:` |
| 需要数数字的（08/09/10/12/17） | 日志末尾出现 `[check] PASS ...`，数字证据写在同一行 |

`LAB3:` 是测试代码的自曝标记：该 trap 没 trap、该 panic 没 panic 就打出来，被 `mustnot` 抓住。

---

## A. 总览（stage=orig）

| 用例 | 方法 | 操作 | 预期 | 覆盖点 |
| --- | --- | --- | --- | --- |
| 01_boot | 冒烟 | 员员样样 `main.c`，`-smp 2` 跑 8 秒 | 两行 `cpu N is booting!` + `Welcome to ToyOS!` + `ticks = N` 持续递增 | 启动链路、双核、串口输出、时钟滴答 |

## B. 边界值 BVA

| 用例 | 模板编号 | 操作 | 预期 | 覆盖点 |
| --- | --- | --- | --- | --- |
| 03_bva_tick_zero | BVA-01 | 初始化刚结束读 `timer_get_ticks()` | `ticks0 ≤ 3`（建 128MB 页表会耗时 1–2 个 tick） | `ticks` 下界；计数器是否清零、会不会一开机就狂刷 |
| 07_bva_uart_fifo16 | BVA-02 | 一次写 16 个字符（`0123456789abcdef`），再单独送 `\r` | 16 个字符全部回显 | 16550 FIFO 容量边界（17 个会被硬件丢，见 §5 未覆盖） |
| 10_bva_uart_bs_start | BVA-03 | 输入 `a` + DEL + DEL + `b` + `\r` | 屏幕只剩 `b`；`\b \b` 恰好 1 次（第二个 DEL 在行首不输出） | `line_len == 0` 边界 |
| 12_bva_uart_empty_line | BVA-04 | 只送一个 `\r` | 无任何非空输出、恰好 1 个换行、不卡 | 空行边界 |

## C. 等价类 EQ

| 用例 | 模板编号 | 操作 | 预期 | 覆盖点 |
| --- | --- | --- | --- | --- |
| 06_eq_uart_echo | EQ-01 | 输入 `abc\r` | 回显 `abc` 并换行 | 可打印字符（有效类） |
| 08_eq_uart_backspace | EQ-02 | 输入 `ab` + BS(0x08) + `c\r` | 屏幕最后一行是 `ac`，只 1 个换行 | `\b` 退格（删字符） |
| 09_eq_uart_crlf | EQ-03 | 依次送 `x\r`、`y\n`、`z\r\n` | 三行 `x`/`y`/`z`，总共只 3 个换行 | `\r`、孤立 `\n`、CRLF 三种换行（CRLF 不能变两个换行） |
| 11_eq_uart_multiline | EQ-04 | 连续三行 `one\r` `two\r` `three\r` | 三行都回显 | 多次进入中断：验证 `plic_claim` 之后必须 `plic_complete` |

## D. 路径覆盖 PATH

| 用例 | 模板编号 | 操作 | 预期 | 覆盖点 |
| --- | --- | --- | --- | --- |
| 02_path_trap_init | PATH-01 | 初始化完成后读 `stvec` / `sie` / `sstatus` | `stvec == kernel_vector` 且 4 字节对齐；`sie.SEIE/SSIE` 置位；`sstatus.SIE = 1` | `trap_kernel_inithart` 的三步 |
| 04_path_tick_monotonic | PATH-02 | 等 5 个 tick，读 `scause`/`sstatus` | ticks 单调递增；`scause = 0x8000…001`（以 SSI 投递）；`sstatus.SPIE = 1` | MTIMECMP → timer_vector → `csrw sip` → timer_interrupt_handler → timer_update 全链路 |
| 05_path_ctx_regs | PATH-03 | 汇编在 s0–s11 放 12 个哨兵，经历 3 次时钟中断后逐个比较 | 12 个寄存器全部不变（返回 0） | `trap.S` 的上下文保存/恢复 |

## E. 并发与竞争 CON

| 用例 | 模板编号 | 操作 | 预期 | 覆盖点 |
| --- | --- | --- | --- | --- |
| 17_con_smp2_tick | CON-01 | `-smp 2` 跑 4 秒，每个 tick 打一行 `TICK n`，数行数 | 10–55 行（≈1 倍速率） | `timer_interrupt_handler` 里 `mycpuid()==0` 的守卫：ticks 不能翻倍（翻倍会到 ~70 行） |
| 18_con_smp2_uart | CON-02 | `-smp 2`，hart1 等自己的 SSI 并检查；hart0 开 25 tick 的输入窗口收 `smp\r` | hart1 的 `scause` 是 `0x8000…001`、`sip.SSIP` 被清；`smp` 正常回显 | per-hart 的 `mscratch`/`MTIMECMP`、hart1 的清 SSIP、串口中断被唯一 claim |

## F. 异常与故障注入 FI

| 用例 | 模板编号 | 操作 | 预期 | 覆盖点 |
| --- | --- | --- | --- | --- |
| 13_fi_ecall | FI-01 | S-mode 执行 `ecall` | `unexpected exception: Environment call from S-mode` + `panic! trap_kernel_handler` | 异常分支、`exception_info[9]` |
| 14_fi_illegal | FI-02 | 执行 `.word 0`（非法指令） | `unexpected exception: Illegal instruction` + panic | `exception_info[2]` |
| 15_fi_load_pf | FI-03 | 读未映射地址 `va 0` | `unexpected exception: Load page fault` + panic | `exception_info[13]` |
| 16_fi_store_pf | FI-04 | 写未映射地址 `va 0` | `unexpected exception: Store/AMO page fault` + panic | `exception_info[15]` |

> 这四条都是**自然故障**，没有注入代码；panic 是规格要求（指导书："遇到意料之外的异常，输出报错信息并终止"）。

## G. 压力与性能 ST / PERF

| 用例 | 模板编号 | 操作 | 预期 | 覆盖点 |
| --- | --- | --- | --- | --- |
| 19_st_soak | ST-01 | `-smp 2`，25 个 tick（≈2.5 秒）内分三段送 `soak-ok\r`，tick 不停 | `soak-ok` 连续回显、ticks 照常推进、无死锁、无 panic | 中断与输入长期交错下的稳定性 |
| — | PERF | 不适用 | — | 见 `../../TESTPLAN.md` §3.6：无可量化的被测性能指标 |

---

## 用例与方法对应

| 方法 | 用例 |
| --- | --- |
| 路径覆盖 | 02, 04, 05 |
| 等价类 | 06, 08, 09, 11 |
| 边界值 | 03, 07, 10, 12 |
| 并发 | 17, 18 |
| 异常注入 | 13, 14, 15, 16 |
| 压力 | 19 |
| 冒烟（不计类） | 01 |

> 一条用例常同时覆盖多类：例如 09 既是"CRLF 等价类"，也用到了 `last_was_cr` 的状态转换路径；04 既是"时钟全链路路径"，也验证了速率边界。

## 单条用例详细记录（示例：08）

| 项目 | 内容 |
| --- | --- |
| 用例名称 | 退格 0x08：`ab` → 删到 `a` → 再打 `c` |
| 主类别 / 关联 | 等价类 EQ-02 / 路径（`\b && line_len>0`） |
| 优先级 | P0 |
| 关联函数 | `uart_intr()`（`src/kernel/lib/uart.c:68`） |
| 前置条件 | 系统启动完成，串口中断已打开 |
| 测试数据 | 串口依次收到 `0x61 0x62 0x08 0x63 0x0d` |
| 执行命令 | `bash tests/run_tests.sh 08_eq_uart_backspace` |
| 预期结果 | 屏幕最后一行 `ac`；输入窗口内 `\r\n` 出现 1 次；`\b \b` 出现 1 次 |
| 超时 / 重复 | 8 秒，跑 2 次一致 |
| 实际结果 | PASS，`[check] PASS rendered=['ac'] crlf=1 bs=1` |
| 证据 | `tests/results/08_eq_uart_backspace.log`、`images/08_eq_uart_backspace.png` |
