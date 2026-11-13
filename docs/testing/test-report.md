# lab-1 测试报告（round1）

| 项目 | 内容 |
| --- | --- |
| 被测分支 | `lab-1`（`origin/lab-1`，`d4a1536`） |
| 测试分支 | `test/lab-1-round1` |
| 测试日期 | 2026-11-11 21:30 – 2026-11-13 15:35（Asia/Shanghai） |
| 测试环境 | VMware Workstation 17.6.4 / Ubuntu 24.04.3 LTS x86_64 |
| 工具链 | `riscv64-linux-gnu-gcc 13.3.0`、`qemu-system-riscv64 8.2.2`、`gdb-multiarch 15.1` |
| 启动参数 | `-machine virt -bios none -m 128M -smp 2 -nographic` |

## 1. 结论摘要

1. **`lab-1` 原始代码在本测试环境下无法启动**：QEMU 启动后串口零输出，进程卡住。根因是 `start()` 未配置 PMP，QEMU ≥ 6.0 在 `mret` 进入 S-mode 时触发指令访问异常；而 `mtvec` 为 0，异常后跳转到地址 0 形成静默死循环（**BUG-1**）。
2. 在本地应用 PMP 修复后，**A1–A10 功能验收全部通过**：双核启动、`sum = 2000000`、单核对照、printf 五格式、4.1/4.2 锁实验、GDB 单步、10/10 稳定性。
3. round1 已覆盖 A1–A10、B1–B8；边界测试共登记 **6 个待修缺陷**（BUG-2 ~ BUG-7）。

## 2. 执行结果

| 编号 | 用例 | 结果 | 证据 |
| --- | --- | --- | --- |
| A1 | 干净编译 | ✅ 通过 | 7 个目标文件编译、链接成功；仅 RWX 段警告 |
| A2 | 双核启动 | ✅ 通过 | 两行 `cpu N is booting!` |
| A3 | 4.1 加锁版 | ✅ 通过 | 两次报告均为 `sum = 2000000` |
| A4 | 单核对照 | ✅ 通过 | 只有 `cpu 0`，`sum = 1000000` |
| A5 | 4.1 去锁对照 | ✅ 通过 | 5 次运行最大值 50 万–133 万，均远低于 2000000，成功复现丢更新 |
| A6 | 4.2 并行输出 | ✅ 通过 | 去掉 `print_lk` 后 A/B 字符大量交错，同一行内混排 |
| A7 | printf 五格式 | ✅ 通过 | 输出 `fmt: -123 0x123456789abcdef0 2a A ok` |
| A8 | 10 次稳定性 | ✅ 通过 | 10/10，`sum` 全部为 2000000 |
| A9 | GDB 单步 | ✅ 通过 | `_entry` 0x80000000 → `main` 0x80000020；`sp` 在 `CPU_stack` 内，`tp=1` |
| A10 | 最终回归 | ✅ 通过 | `test.sh` 双核通过；10/10 稳定，`sum` 最大值为 2000000 |
| B1–B8 | 边界与故障注入 | ✅ 已执行 | 发现 BUG-2（多核越界）、BUG-3（bios 约束）、BUG-4（Makefile 依赖）、BUG-5（printf `%%`）、BUG-6（panic 重入）、BUG-7（4KB 栈） |

## 3. 缺陷

### BUG-1：未配置 PMP，QEMU ≥ 6.0 下无法进入 S-mode（阻断）

| 项目 | 内容 |
| --- | --- |
| 严重级别 | 阻断（内核完全无法启动） |
| 影响范围 | 所有使用 QEMU ≥ 6.0（PMP 默认启用）的环境 |
| 复现步骤 | `make clean && make build`，然后 `qemu-system-riscv64 -machine virt -bios none -kernel target/kernel/kernel-qemu.elf -m 128M -smp 2 -nographic` |
| 实际结果 | 串口无任何输出，QEMU 持续运行但内核卡死 |
| 期望结果 | 输出两行 `cpu N is booting!` |

**根因**

QEMU 6.0 起，若 CPU 实现了 PMP 但一条规则都没有配置，执行 `mret` 从 M-mode 切换到 S-mode 时会抛出指令访问异常。`start.c` 只设置了 `satp/tp/mstatus/mepc`，没有配置 PMP；`mtvec` 仍为 0，异常后 PC 跳到地址 0，形成无限异常循环，因此没有任何串口输出。

**修复**

在 `mret` 之前配置一条 TOR 模式、覆盖全部物理内存的 PMP 规则：

- `src/kernel/arch/method.h`：新增 `w_pmpaddr0()`、`w_pmpcfg0()`；
- `src/kernel/boot/start.c`：`w_pmpaddr0(0x3fffffffffffffull); w_pmpcfg0(0xf);`

该配置在旧版本 QEMU 上同样合法，不改变原有行为。

**验证**

修复后 A1–A4、A8 全部通过；连续 10 次运行输出一致：

```
cpu 0 is booting!
cpu 1 is booting!
cpu 0 report: sum = 2000000
cpu 1 report: sum = 2000000
```

### BUG-2：`NCPU=2` 与 `-smp>2` 不匹配，多核启动踩内存（高）

| 项目 | 内容 |
| --- | --- |
| 严重级别 | 高（`-smp 4/8` 时内核不可用） |
| 复现步骤 | 以 `-smp 4` 或 `-smp 8` 启动 |
| 实际结果 | 只输出 `cpu 0 is booting!`；`-smp 8` 下输出被截断为 `cpu` |
| 期望结果 | 所有 hart 都启动并输出 |
| 根因 | `NCPU=2`，而 `entry.S` 用 `CPU_stack + (hartid+1)*4096` 设置栈，hartid≥2 时越过 8 KB 数组；`lib/cpu.c` 的 `cpus[hartid]` 同样越界，破坏 `.bss` 中的共享变量 |
| 建议 | `NCPU` 提升到实际最大核数并同步 `CPU_stack`/`cpus`，或在 `start()` 早期加 `assert(hartid < NCPU)` |

### BUG-3：内核依赖 `-bios none`（中，属运行约束）

| 项目 | 内容 |
| --- | --- |
| 严重级别 | 中（评测环境不匹配时完全无法启动） |
| 现象 | 不加 `-bios none` 时 QEMU 直接退出（rc=1） |
| 证据 | `Some ROM regions are overlapping`：OpenSBI `0x80000000-0x80041c80` 与内核 `0x80000000-0x80003508` 重叠 |
| 结论 | 当前内核假设 QEMU 从 M-mode 的 `0x80000000` 直接加载；若评测使用默认 OpenSBI，需要改启动协议（入口 `0x80200000`、S-mode 启动） |

**B4 结论**：`-cpu rv64` 与 `-cpu rv64,pmp=on` 均正常，只有 `-cpu rv64,pmp=off` 零输出。说明是"关闭 PMP"导致，不是 CPU 模型问题；推测 PMP CSR 访问在 `pmp=off` 时触发非法指令，`mtvec=0` 后静默循环。实际环境不会关闭 PMP，登记为运行约束（低）。

### BUG-4：Makefile 依赖缺失（中，影响开发/回归效率）

| 项目 | 内容 |
| --- | --- |
| 现象 1 | `make -n debug` 只输出 QEMU 命令、没有任何 gcc，说明 `debug` 不会先编译；`make clean` 后直接 `make debug` 会加载不存在的 kernel |
| 现象 2 | `touch src/kernel/arch/method.h` 后 `make build` 输出"无需做任何事"，说明改头文件不触发重编 |
| 根因 | `debug: $(KERN) .gdbinit` 中 `$(KERN)` 未定义；`-MD` 生成 `.d` 文件但 Makefile 从未 `include` |
| 建议 | `debug: build .gdbinit`；Makefile 末尾加 `-include $(KernelOBJ:.o=.d)` |

### BUG-5：printf 不支持 `%%`，未知格式符被静默丢弃（中）

| 项目 | 内容 |
| --- | --- |
| 严重级别 | 中（日志/百分比输出错误，调试信息丢失） |
| 现象 | `printf("e2:%%\n")` 只输出 `e2:`；`printf("e4:%q\n", 1)` 只输出 `e4:` |
| 期望 | `%%` 输出一个 `%`；未知格式符至少原样输出或报错 |
| 根因 | `printf` 的 switch 没有 `%` 分支，也没有 `default`，`i += 2` 直接跳过两个字符 |
| 建议 | 增加 `case '%': uart_putc_sync('%'); break;` 与 default 分支 |

### BUG-6：panic 在持有 `print_lk` 时重入，死锁/爆栈（高）

| 项目 | 内容 |
| --- | --- |
| 严重级别 | 高（错误处理路径本身不可用，且掩盖真实错误） |
| 复现 | 在 `print_init()` 中先 `spinlock_acquire(&print_lk)`，再 `panic("held-lock test")` |
| 实际结果 | 只输出 `b6: acquire print_lk then panic`，随后卡死到超时 |
| 期望结果 | panic 应打印原因并停机 |
| 根因 | `panic()` 调用 `printf()`，`printf()` 再次申请 `print_lk`；`spinlock_acquire` 内的 `assert(!spinlock_holding(lk))` 失败又调用 `panic()`，形成递归，最终爆栈或死锁 |
| 建议 | `panic` 改用不取锁的 raw 输出，或把 `printf` 拆成取锁/不取锁两层 |

### BUG-7：4KB 内核栈无保护，深调用会击穿（中）

| 项目 | 内容 |
| --- | --- |
| 严重级别 | 中（lab-1 调用链浅，暂不触发；后续 lab 复杂度上升后风险增大） |
| 复现 | 在 `main()` 中加入每层 256 字节的递归 `deep(30)`（约 8KB） |
| 实际结果 | 只打印 `b7: stack test start`，随后卡死，`survived` 未打印 |
| 期望结果 | 至少应检测到栈溢出并 panic，而不是静默踩内存 |
| 根因 | `CPU_stack` 每个 hart 只有 4096 字节，无 guard page/魔数检测；溢出会写进相邻 `.bss`（另一个 hart 的栈、锁、全局变量） |
| 建议 | 栈扩大到 8–16KB；栈底放魔数并在进入 `main`/上下文切换时检查；有条件时加 guard page |

## 4. 测试环境注意事项

1. **自动化脚本中启动 QEMU 必须重定向 stdin**：`< /dev/null`。否则 `timeout` 将 QEMU 放入新的后台进程组，QEMU 在 `-nographic` 下初始化终端时会被挂起，表现为"编译成功但运行零输出"的假故障（本轮排查中曾误判为内核问题）。
2. 修改头文件后必须 `make clean`：Makefile 使用 `-MD` 生成 `.d` 依赖文件但未 `include`，头文件变化不会触发重编。
3. **`make` 不带目标等价于 `make run`**（会构建并启动 QEMU，在前台等待），自动化脚本中不要裸跑 `make`；只编译请用 `make build`。

## 5. 后续计划（round2）

1. 提交测试报告 PR（`docs/testing/`）与 PMP 修复 PR（`src/kernel/boot/start.c` + `arch/method.h`）。
2. 按测试负责人安排，逐项修复 BUG-2 ~ BUG-7。
3. 修复后重跑 A2、A3、A8 与 B1–B4 做回归。
4. 评估是否需要补充异常/中断处理（当前 `mtvec = 0`，任何异常都会静默死循环）。
