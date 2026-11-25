# lab-3 测试说明（tests/）

> 被测代码：lab-3 的 `src/kernel/trap/*`（trap.S / trap_kernel.c / timer.c / plic.c）
> 与 `src/kernel/lib/uart.c` 的 `uart_intr()`，以及 `main.c` 的初始化顺序。
>
> 本文档只描述**怎么跑、测什么、判定标准、哪些没测**。
> 正式的测试计划 / 用例 / 报告放在 `docs/testing/`。

---

## 1. 一条命令

虚拟机里（每次开机先挂载共享目录）：

```bash
sudo mkdir -p /mnt/hgfs
sudo vmhgfs-fuse .host:/ /mnt/hgfs -o allow_other

bash /mnt/hgfs/zhousx/my-toyos-lab-3/tools/lab3_test.sh
```

> `tools/lab3_test.sh` 是我本机的私有脚本（**不提交**）：负责把共享目录同步到 `~/lab3`、做本机环境适配、构建、冒烟，再调用 `tests/run_all.sh`。
> 仓库里拿到这份代码后，直接 `make build && bash tests/run_all.sh` 即可。

它按顺序做 6 件事，任何一步失败都会停下并打印原因：

| 步骤 | 做什么 | 失败时 |
| --- | --- | --- |
| 1 | `rsync` 共享目录 → `~/lab3`（不在 hgfs 上编译） | 检查挂载 |
| 2 | 给 `start.c` 打**本地 PMP 补丁**（只在本机、不提交） | 打印原因退出 |
| 3 | `make clean && make build` | 打印后 40 行编译错误 |
| 4 | 原样跑 5 秒**冒烟**（有 banner + ticks 才继续） | 打印排查清单 |
| 5 | `tests/run_all.sh --render`：全部用例 + 出图 | 继续回传结果 |
| 6 | `tests/results/`、`docs/testing/images/` 回传共享目录 | — |

只想冒烟 / 只跑一条：

```bash
bash tools/lab3_test.sh --smoke
bash tools/lab3_test.sh --case 04_path_tick_monotonic
bash tools/lab3_test.sh --tests          # 跳过冒烟
bash tests/run_all.sh --no-shot          # 只跑测试不出图
bash tests/show_case.sh 08_eq_uart_backspace   # 一屏回看某一条（截图用）
```

产物：

```
tests/results/<id>.log     每条用例的真实串口输出
tests/results/summary.txt  汇总（含按类别统计）
tests/results/status.tsv   id|类别|PASS/FAIL（给文档做统计用）
docs/testing/images/*.png  每条用例一张图 + 00-summary.png
```

---

## 2. 判定约定

| 用例类型 | 通过条件 |
| --- | --- |
| 正常用例 | 串口出现 `RESULT: PASS <名字>`，且没有 `panic!` |
| 异常用例（13–16） | 出现 `unexpected exception: <原因>` **且** `panic! trap_kernel_handler`；出现 `LAB3:` 视为自曝失败 |
| 需要数数/还原屏幕的（08/09/10/12/17） | CHECK 脚本追加的 `[check] PASS ...` 一行，数字证据也在这一行里 |

`LAB3:` 是测试代码自己的自曝标记：只要被测代码该 trap 没 trap、该 panic 没 panic，就会打出来，被 `mustnot` 抓住。

---

## 3. 19 条用例

| ID | 类别 | 输入 / 触发 | 主要判定点 |
| --- | --- | --- | --- |
| 01_boot | 总览 | 原样 `main.c`（不注入），`-smp 2`，8 秒 | 双核 banner + `ticks = N` 在走 |
| 02_path_trap_init | 路径 | 读完初始化后读 CSR | `stvec == kernel_vector` 且 4 字节对齐；`sie.SEIE/SSIE`；`sstatus.SIE=1` |
| 03_bva_tick_zero | 边界 | 启动刚结束 | `ticks0 ≤ 3`：计数器从 0 起，且不会一开机就狂刷 |
| 04_path_tick_monotonic | 路径 | 等 5 个 tick | 单调递增、能到 5（MTIMECMP→timer_vector→SSIP→timer_update 全链路）；`scause` 必须是 `0x8000…001`（计时器以 SSI 投递）、`sstatus.SPIE=1` |
| 05_path_ctx_regs | 路径 | 汇编在 s0–s11 放哨兵，跨 3 次 trap | 12 个 callee-saved 寄存器一个都没被 trap.S 改坏 |
| 06_eq_uart_echo | 等价类 | `abc\r` | 回显 `abc` |
| 07_bva_uart_fifo16 | 边界 | 一次写 16 字符 | 16 个全回显（=16550 FIFO 容量） |
| 08_eq_uart_backspace | 等价类 | `ab\bc\r` | 屏幕最后一行是 `ac`，只 1 个换行 |
| 09_eq_uart_crlf | 等价类 | `x\r` `y\n` `z\r\n` | 三行 x/y/z，总共只 3 个换行（CRLF 不重复换行） |
| 10_bva_uart_bs_start | 边界 | `a` DEL DEL `b` | 行首退格不输出；`\b \b` 恰好 1 次；最后一行 `b` |
| 11_eq_uart_multiline | 等价类 | 连续 3 行 | one/two/three 都在（`plic_claim` 后必须 `plic_complete`） |
| 12_bva_uart_empty_line | 边界 | 只送 `\r` | 无任何非空输出、恰好 1 个换行、不卡 |
| 13_fi_ecall | 异常 | `ecall`（S-mode） | `Environment call from S-mode` + panic |
| 14_fi_illegal | 异常 | `.word 0` | `Illegal instruction` + panic |
| 15_fi_load_pf | 异常 | 读 va 0 | `Load page fault` + panic |
| 16_fi_store_pf | 异常 | 写 va 0 | `Store/AMO page fault` + panic |
| 17_con_smp2_tick | 并发 | `-smp 2` 跑 4 秒数 `TICK` 行 | 10–55 行（≈1 倍；若 ~70 行说明重复计数） |
| 18_con_smp2_uart | 并发 | `-smp 2` + 串口输入 | hart1 的 `scause` 是 `0x8000…001`（确实收到 SSI）、SSIP 被清、回显正常 |
| 19_st_soak | 压力 | 25 个 tick 内分三段输入 | `soak-ok` 连续回显、ticks 照常推进、无死锁 |

### 关于双核 tick 的一个纠正

`timer_interrupt_handler()` 里是 `if (mycpuid() == 0) timer_update();`，
所以 `-smp 2` 下 ticks 的增速**仍然是 1 倍**（约 10/s），不是 2 倍。
用例 17 的阈值 10–55 就是照这个写的：2 倍会到 ~70，直接被抓住。

---

## 4. 六类测试矩阵

| 类别 | 计划 | 用例 | 说明 |
| --- | ---: | --- | --- |
| 边界值 BVA | 4 | 03, 07, 10, 12 | ticks 下界 0；FIFO 16 字符；`line_len==0`；空行 |
| 等价类 EQ | 4 | 06, 08, 09, 11 | 可打印 / BS / CR·LF·CRLF / 多行 |
| 路径覆盖 PATH | 3 | 02, 04, 05 | 两个 switch 的可达分支 + `uart_intr` 全部分支 + trap.S 存取 |
| 并发与竞争 CON | 2 | 17, 18 | 双核 tick 不翻倍；双核串口 + hart1 SSIP |
| 异常与故障 FI | 4 | 13–16 | ecall / 非法指令 / load PF / store PF（自然故障，无注入钩子） |
| 压力 ST | 1 | 19 | 3 秒 soak：tick 与输入交错 |
| 性能 PERF | 0 | — | **不适用**，理由见下 |

**为什么 PERF 是 N/A**：lab-3 没有调度、没有动态资源，唯一可量化的是 tick 速率，
它由 `INTERVAL=1000000` 与 QEMU 的 10MHz timebase 决定，属于平台属性而不是被测代码的性能；
而且 QEMU 5.0 与 8.2.2 的中断投递时序不同，跨环境数值不可比。
模板 §7.1 也写了"跑完 ≠ 性能达标"。所以 ST 只保留 1 条稳定性 soak，PERF 整节省略。

### 4.1 对照两张流程图（时钟 / UART）

两张图一共 30 多个框。**S-mode 能读到的东西都落成用例了；落不了的是 M-mode 私有 CSR 和 PLIC 硬件自己的判断逻辑**——不是漏测，是权限上读不到（在 S-mode 读 `mtvec`/`mscratch`/`mie`/`mideleg` 会直接非法指令 panic）。

| 流程图里的框 | 对应用例 | 怎么验的 |
| --- | --- | --- |
| `start.c` 里每个 hart 执行 `timer_init()` | 18 | hart1 能收到自己的 SSI，说明它的 `mtvec`/`mscratch`/`MTIMECMP` 都配好了 |
| `w_mideleg(SSIE\|SEIE)`：编号 7 不委托 | 04 | `scause` 必须是 `0x8000…001`；若误委托了 STIP/MTIP 会看到 5/7，且 handler 走 default panic |
| `MTIMECMP = MTIME + INTERVAL` | 04（行为） | CLINT 是 M-mode 设备，S-mode 侧只能看"tick 在按 0.1s 走" |
| `mscratch[hart][3]/[4]` + `csrrw a0, mscratch, a0` | 05 + 18 | 05 验寄存器存取不脏；18 验两核的临时空间没串 |
| `w_mtvec(timer_vector)` / `w_mstatus(\|MIE)` / `w_mie(\|MTIE)` | 04（行为） | 全是 M-mode CSR，读一次就 panic；能持续来 tick 就说明三个都设对了 |
| `csrw sip, 2` → S 软件中断 | 04 | `scause` 最高位 = 1、编号 = 1 |
| 硬件置 `sstatus.SPIE=1 / SIE=0`，`sret` 恢复 | 04 | tick 之后 `sstatus.SPIE` 必须是 1 |
| `kernel_vector` 存/取 32 个寄存器 | 05 | s0–s11 哨兵跨 3 次中断不变 |
| `mycpuid() == 0 ? update : nothing` | 17 + 18 | 双核 4 秒的 tick 数不能翻倍；hart1 的 SSIP 必须被清 |
| `plic_init()`：`priority[UART_IRQ] = 1` | 06 / 11（间接） | 不单独读寄存器；串口能回显，就说明优先级/使能/阈值这一串都生效了 |
| `plic_inithart()`：`SENABLE` 置位 + 阈值 0 | 06 / 11（间接） | 同上；per-hart 的 `SENABLE` 由"哪个 hart 收到都能回显"间接证明 |
| `stvec = kernel_vector` / `intr_on` / `w_sie` | 02 | 读 `stvec`、`sie`、`sstatus.SIE` |
| PLIC "优先级 > 阈值才响应" | 不能测 | 这是 PLIC 硬件（QEMU 模型）的行为，实现只负责把值配成 1 和 0 |
| `plic_claim` / `plic_complete` | 11 | 连打三行：漏 complete 的话只有第一行能回显 |
| `uart_intr`（图里省略了） | 06–12 | 回显、退格、CR/LF/CRLF、多行、FIFO 边界 |
| `sret` 回到被打断的流 | 04 + 05 | tick 后 ticks 继续涨、寄存器没乱 |

图里没画、但代码里有、也测了的：异常分支（`scause` 最高位 = 0，13–16 四条）、`scause & 0xf` 截断（13–16 的 `trap_id` 打印）。
图里画了但测不了的：`assert(sstatus & SSTATUS_SPP)`、`assert(intr_get() == 0)`（要破坏不变量才触发），见 §5。

---

## 5. 没测到的（诚实清单）

| 项 | 为什么没测 |
| --- | --- |
| `trap_kernel_handler` 的 `interrupt default` 分支（中断号不是 1/9） | S-mode 只能收到已委托的 SSIE/SEIE，构造不出未委托中断；要测只能改内核里的 `mideleg` |
| `external_interrupt_handler` 的 `default`（非法 IRQ 号） | `plic_claim` 只会返回已使能的 IRQ，无法让它返回 1..9 |
| `trap_kernel_handler` 的两个 `assert`（SPP=1、SIE=0） | 需要先破坏内核不变量，属于"改代码才能测" |
| 一次写 17 个字符 | 16550 FIFO 只有 16 字节，第 17 个会被 QEMU 模型丢掉，是硬件模型行为，不是内核代码的问题 |
| `INTERVAL` 改大/改小 | 不增加任何分支覆盖，还要改内核代码重新编译；tick 速率抖动也说明不了问题 |
| 中断嵌套 | 当前设计进 handler 时 SIE 已被硬件清 0，不存在嵌套 |
| PLIC 的配置值（priority / SENABLE / 阈值）没有单独读 | 本轮范围内靠"能不能回显"（06/11）间接覆盖；PLIC 是恒等映射的 MMIO，想直接读也就十来行代码，属于可加但不加 |

路径覆盖的实际口径：**`trap_kernel_handler` / `external_interrupt_handler` / `uart_intr` 的可达分支全部覆盖；不可达的 3 条在文档里注明原因**。用例通过不等于覆盖，这一条按模板 §4.4 写。

---

## 6. 环境问题不算缺陷（也不写进报告结论）

| 现象 | 原因 | 处理 |
| --- | --- | --- |
| 串口零输出，日志只有 `terminating on signal 15` | QEMU 8.x 不放行"没配 PMP 的 S-mode 访问" | `tools/lab3_test.sh` 自动打本地补丁；`bash tools/diagnose_pmp.sh` 可做 A/B 确认 |
| **只有注入用例零输出、`01_boot` 正常** | 注入的 `.S` 排在 `boot/entry.S` 前面，把 `_entry` 挤离 0x80000000，上电直接跳进探针死等 | `run_tests.sh` 现在把探针放到 `src/kernel/trap/`，并在每个用例后自检"0x80000000 处是不是 `_entry`"（不是会打 `[warn]`） |
| 手工跑 QEMU 时挂住不动 | `-nographic` 且 stdin 连着终端 | 加 `< /dev/null`（输入用例用管道喂，见 `tests/stdin/`） |
| 编译报 `Werror` 里一堆 unused | 测试代码有没用到的变量 | 改测试代码，别动工程的 `CFLAGS` |
| 测试里读 `time`/`cycle` 直接 panic | `start.c` 没写 `mcounteren`，S-mode 读这些 CSR 是非法指令 | 别在 S-mode 读时间；tick 速率靠数 `TICK` 行 |
| `panic!` 之后再也没有输出 | `panic()` 把 `panicked` 置 1，`uart_putc_sync` 会一直自旋 | 一个 panic 用例一次 QEMU（`cases.tsv` 已按这个拆） |
| 图片里中文是方框 | 缺中文字体 | `sudo apt install -y fonts-noto-cjk` |
| 脚本报 `$'\r'` | 从 Windows 侧改过文件 | `run_tests.sh` / `lab3_test.sh` 都会先 `sed 's/\r$//'` |

---

## 7. 排错顺序

1. **零输出** → `bash tools/lab3_test.sh --smoke`；不行就 `bash tools/diagnose_pmp.sh`（A=0/B>0 即 PMP）。
2. **某条 FAIL** → `bash tests/show_case.sh <id>` 看原文；`tests/results/<id>.build.log` 是编译日志。
3. **CHECK 失败**（08/09/10/12/17）→ 看日志里 `[check] FAIL ...` 那行的数字，和 `tests/checks/<id>.sh` 的期望对比。
4. **想改判定** → 只改 `tests/cases.tsv` 的 must/mustnot，或 `tests/checks/<id>.sh`；别改 `run_tests.sh`。

---

## 8. 目录

```
tests/
├── run_all.sh          跑全部 + 出图
├── run_tests.sh        读 cases.tsv：复制工程 → 注入 → 编译 → QEMU → 判定
├── cases.tsv           用例表（8 列，stage 列就是六类标签）
├── show_case.sh        一屏回看单条
├── render_shot.py      串口日志 → 终端风格 PNG
├── suite/
│   ├── lab3_tests.c        测试函数（注入到 src/kernel/lab3_tests.c）
│   └── lab3_tests_asm.S    s0-s11 哨兵探针（注入到 src/kernel/lab3_tests_asm.S）
├── templates/main_test.c   复刻 main.c 的初始化顺序，@CASE@ 占位
├── stdin/<id>.sh       给 QEMU 喂串口输入（带 sleep，避免 FIFO 溢出）
└── checks/<id>.sh      需要数行数 / 还原屏幕的额外判定
```
