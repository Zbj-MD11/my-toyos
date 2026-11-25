# lab-3 测试计划

| 项目 | 内容 |
| --- | --- |
| 被测版本 | 仓库 `Zbj-MD11/my-toyos` 的 `lab-3` 分支（本地副本 `~/lab3`） |
| 被测对象 | 中断与异常的识别与处理：`trap/` 下 5 个函数 + `timer.c` 4 个函数 + `uart_intr` + `start` / `main` |
| 测试类型 | 黑盒（串口输出 + CSR/MMIO 读数）+ 白盒（按代码分支设计用例） |
| 测试代码 | `tests/`，19 条用例，`bash tests/run_all.sh` 一键运行 |
| 用例明细 | `test-cases.md` |
| 结果与截图 | `test-report.md`、`images/` |

## 1. 测试目标

1. 验证 lab-3 新增的中断链路满足指导书与代码注释写明的契约：
   - **时钟**：M-mode 配 `MTIMECMP`/`mscratch`/`mtvec`，每个 tick 回 S-mode 以软件中断投递，`ticks` 单调递增；
   - **串口**：PLIC 把 UART 中断送到 S-mode，`uart_intr` 回显字符并正确处理退格、CR/LF；
   - **异常**：未预期的异常/中断被识别、打印原因并 panic，而不是静默卡死。
2. 用**路径覆盖 + 等价类 + 边界值**保证每个可达分支至少一条用例，再补**并发**（两个 hart）和**异常**两类——这两类是本实验最容易出问题、也最容易被单核测试漏掉的部分。
3. 产出可复现的证据：一条命令跑完全部用例，每条一份串口日志 + 一张截图。

## 2. 范围

**在范围内**（lab-3 实现涉及的 12 个函数）

| 函数 | 位置 |
| --- | --- |
| `start()` | `src/kernel/boot/start.c:8` |
| `timer_init()` / `timer_create()` / `timer_update()` / `timer_get_ticks()` | `src/kernel/trap/timer.c:12/45/52/61` |
| `trap_kernel_init()` / `trap_kernel_inithart()` / `trap_kernel_handler()` | `src/kernel/trap/trap_kernel.c:48/58/72` |
| `external_interrupt_handler()` / `timer_interrupt_handler()` | `src/kernel/trap/trap_kernel.c:120/144` |
| `uart_intr()` | `src/kernel/lib/uart.c:68` |
| `main()`（初始化顺序） | `src/kernel/main.c:14` |

**不在范围内**

- `trap.S` 与 `plic.c`：实验给出的代码（NEW）。它们被上表函数调用，测试会**间接**覆盖（05 验 `trap.S` 的寄存器存取，11 验 claim/complete），但这两块不单独列为被测功能，只算连带覆盖；
- 进程、用户态、页表生命周期（lab-4 起）；
- QEMU 之外的真实硬件、以及 QEMU 5/8 的版本差异（属环境，不进结论）。

## 3. 环境与通过标准

- 环境：VMware `xv6-lab`（4 vCPU / 8 GB）、Ubuntu 24.04.3 LTS；
- 工具链：`riscv64-linux-gnu-gcc 13.3.0`、`qemu-system-riscv64 8.2.2`；
- 参数：`-machine virt -bios none -m 128M -smp N -nographic`，无输入用例 stdin 重定向 `/dev/null`，输入用例用管道定时喂；
- 通过标准：
  1. 全部用例按 `tests/cases.tsv` 的 `must / mustnot` 规则判定为 PASS；
  2. 异常类用例判 PASS 的标准是"打印了正确原因 + `panic! trap_kernel_handler`"，**panic 本身就是规格**；
  3. 同一版本重跑结果一致；本报告结论取 2026-11-25 14:22 那次全量运行。

## 4. 测试设计方法

### 4.1 路径覆盖（分支表见 `../../TESTPLAN.md` §3.1）

对 `trap_kernel_handler` 的两个 `switch`、`external_interrupt_handler` 的 `switch`、`uart_intr` 的 7 条分支逐个设计用例。
重点：中断/异常两条大分支、`trap_id` 1 与 9、`uart_intr` 的 `-1 / \r / \n / \b / 普通字符`。

### 4.2 等价类

| 维度 | 有效等价类 | 无效/特殊等价类 |
| --- | --- | --- |
| 输入字符 | 可打印 ASCII | `\r`、`\n`、`\b`、`0x7f` |
| 行状态 | 行内有字符（`line_len>0`） | 行首（`line_len==0`） |
| 中断源 | 仅时钟 | 仅串口、两者交替 |
| hart 数 | 1 | 2 |

### 4.3 边界值

- `ticks` 下界 0（启动刚结束时 ≤3，容许建页表的耗时）；
- 16550 FIFO 容量 **16** 字符（一次写满正好不溢出）；
- `line_len` 的 0/1 边界（行首退格、删到空再退格）；
- 空行（只送 `\r`）。

### 4.4 并发与竞争

`-smp 2`：验证 `timer_interrupt_handler` 里 `mycpuid()==0` 的守卫（ticks 不能翻倍）、hart1 的 SSIP 必须被清（否则它永远重入 `kernel_vector`）、串口中断被唯一 claim。

### 4.5 异常与故障注入

lab-3 没有注入钩子，异常路径由**自然故障**触发：`ecall`、非法指令、读写未映射地址。这四条覆盖了 `trap_kernel_handler` 的异常分支和 `exception_info[]` 的 4 个文案。

### 4.6 压力与性能

压力保留 1 条 3 秒 soak（tick 与输入交错）；性能 N/A，理由见 `../../TESTPLAN.md` §3.6。

## 5. 用例组织

`tests/cases.tsv` 是唯一事实来源：每个用例一行（id、类别、测试函数、smp、超时、必现字符串、禁止字符串、说明）。
`tests/run_tests.sh` 逐行执行：复制工程到 `tests/.work/<id>` → 注入测试代码并重写 `main.c` → `make clean && make build` → QEMU 限时运行 → 判定。

需要数行数或还原屏幕的用例（08/09/10/12/17）额外走 `tests/checks/<id>.sh`，把 `[check] PASS ...` 一行写回日志，数字证据留在截图里。

## 6. 前置条件

被测代码不做任何修改：所有用例都跑在工程的临时副本上，只**新增** `src/kernel/lab3_tests.c`、`src/kernel/trap/lab3_tests_asm.S`（探针）并替换 `main.c`（测试入口），`src/` 其余部分保持原有实现不变。

## 7. 风险

1. 判定依赖 panic 文案与 `RESULT: PASS` 字符串，被测代码改文案需同步改 `cases.tsv`；
2. 时钟类用例的阈值放宽（4 秒 10–55 个 tick），避免 VM 抖动误判，代价是抓不到 <10% 的速率偏差；
3. `trap.S` 的寄存器探针是汇编写法，若被测代码调整栈帧布局需要同步调整；
4. M-mode 私有 CSR（`mtvec`/`mscratch`/`mie`/`mideleg`）在 S-mode 读不到，只能行为验证（见 `../../TESTPLAN.md` §9）。
