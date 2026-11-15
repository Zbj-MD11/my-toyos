# lab-2 测试计划

| 项目 | 内容 |
| --- | --- |
| 被测版本 | `lab-2` 本地副本（`src/kernel/mem/`） |
| 被测对象 | 物理内存管理（`pmem_init/alloc/free`）、内核态虚拟内存（`vm_getpte/mappages/unmappages`、`kvm_init/inithart`、`vm_print`） |
| 测试类型 | 黑盒（接口行为）+ 白盒（按代码分支设计用例） |
| 测试代码 | `tests/`，39 个用例，`bash tests/run_tests.sh` 一键运行 |
| 用例明细 | `test-cases.md` |
| 结果与截图 | `test-report.md`、`images/` |

## 1. 测试目标

1. 验证 lab-2 的两个模块是否满足指导书与代码注释里写明的契约：
   - 物理页 4KB 对齐、按区域（内核/用户）隔离、分配清零、释放可回收、页数正确；
   - Sv39 三级页表的查找、映射、解映射、权限位与边界检查。
2. 用**路径覆盖 + 等价类 + 边界值**三类方法保证用例不是"只跑通一条路"，让缺陷在交付前暴露。
3. 产出可复现的证据：一条命令跑完全部用例，每个用例一份串口日志、一张截图。

## 2. 范围

**在范围内**

- `pmem_init` / `pmem_alloc` / `pmem_free` 的正常路径与全部错误路径；
- `vm_getpte` 三级查找的两条 `alloc` 分支、`VA_MAX` 检查；
- `vm_mappages` / `vm_unmappages` 的五个前置检查、多页循环、2MB/1GB 页表边界、`freeit` 两种取值；
- `kvm_init` 的六段映射、`kvm_inithart` 的 satp 切换；
- 双 hart 并发分配/释放；
- `vm_print` 的结构输出。

**不在范围内**

- 中断/异常（lab-3）、进程与用户态页表（lab-4 起）；
- `-smp > NCPU` 的越界问题（lab-1 BUG-2，沿用）；
- 性能与 QEMU 之外的硬件平台。

## 3. 环境与通过标准

- 环境：Ubuntu 24.04.3、`riscv64-linux-gnu-gcc 13.3.0`、`qemu-system-riscv64 8.2.2`；
- 参数：`-machine virt -bios none -m 128M -smp 2 -nographic`，stdin 重定向到 `/dev/null`；
- 通过标准：
  1. 全部用例按 `tests/cases.tsv` 的 `must / mustnot` 规则判定为 PASS；
  2. 已知缺陷（BUG-L2-1）对应的用例可以是 FAIL，但必须在报告里登记原因与修复建议；
  3. 同一版本连跑 3 次结果一致（无偶发）。

## 4. 测试设计方法

### 4.1 路径覆盖

对每个被测函数画出分支，逐个分支至少设计一条用例（矩阵见 `../../TESTPLAN.md` §3.1）。
重点：

- `pmem_alloc` 的两个 `in_kernel` 取值 × 链表空/非空 = 4 条路；
- `pmem_free` 的五道检查（范围、对齐、区域、标签、正常）= 5 条路；
- `vm_getpte` 的三级 × `alloc` 两值 × 中间级"是/不是页表"；
- `vm_mappages` / `vm_unmappages` 的前置检查组合。

### 4.2 等价类

| 维度 | 有效等价类 | 无效等价类 |
| --- | --- | --- |
| 页地址 | 区域内 4KB 对齐 | 区域外、未对齐 |
| 区域标签 | 与地址匹配 | 内核页标用户、用户页标内核 |
| 页状态 | 已分配、空闲 | 已释放（重复释放） |
| `len` | ≥1 | 0 |
| `va` | `< VA_MAX` | `≥ VA_MAX` |
| 映射状态 | 已映射 | 未映射（页表缺失 / 叶子 V=0） |
| `perm` | R、R\|W、R\|X | — |

### 4.3 边界值

`len` ∈ {0, 1, 4095, 4096, 4097}；`va` ∈ {`0x1ff000`, `0x200000`, `0x3ffff000`, `0x40000000`, `VA_MAX-PGSIZE`, `VA_MAX`}；
`pa` ∈ {`ALLOC_BEGIN`, `ALLOC_BEGIN-4096`, `ALLOC_END-4096`, `ALLOC_END`}；
页数 ∈ {1, 1024, 用户区总页数}。

## 5. 用例组织

`tests/cases.tsv` 是唯一事实来源：每个用例一行，包含 id、注入阶段、测试函数、QEMU 参数、必现字符串、禁止字符串、说明。
`tests/run_tests.sh` 逐行执行，`tests/show_case.sh` 逐行展示，`tests/screenshot_cases.sh` 逐行截图。

三个阶段：

| stage | 初始化到哪一步 | 为什么 |
| --- | --- | --- |
| `orig` | 原始 `main.c` | 证明"交付的代码原样能启动" |
| `pmem` | `print_init` + `pmem_init` | 物理内存页数干净（内核区正好 1024 页） |
| `vm` | 再加 `kvm_init` + `kvm_inithart` | 在 Sv39 打开后测页表 |

## 6. 前置条件

被测代码不做任何修改：所有用例都跑在工程的临时副本上，只替换 `main.c`（测试入口）并追加 `tests/suite/lab2_tests.c`，`src/` 保持原样，保证结论对应组员提交的那份代码。

## 7. 风险

1. 用例判定依赖 panic 文案，内核改文案需同步改 `tests/cases.tsv`；
2. panic 用例每次都要等超时，全量约 1–2 分钟；
3. 页表泄漏无法定量（`allocable` 是 `static`），只登记不量化。
