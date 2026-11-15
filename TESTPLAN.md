# lab-2 测试计划（TESTPLAN）

> 被测代码：`src/kernel/mem/pmem.c`、`src/kernel/mem/kvm.c`（组员实现）+ `src/kernel/main.c`
> 测试代码：`tests/`（39 个用例，`bash tests/run_all.sh` 一键运行 + 出图）
> 测试记录：`docs/testing/test-report.md`（结果 + 截图）；本文件是"设计 + 进度"

---

## 0. 测试边界：只测 lab-2 的功能代码

本文档以及 `docs/testing/` 下的交付文档，只针对组员在 lab-2 实现的**内存管理功能**：

- `mem/pmem.c`：物理内存初始化、分配、释放；
- `mem/kvm.c`：页表查找、映射、解映射、内核页表建立；
- `main.c` 中与这两个模块相关的初始化顺序（`pmem_init → kvm_init → kvm_inithart`）。

**不列入测试范围、也不写进测试报告**：工具链 / QEMU 版本差异、启动配置、终端环境等环境类问题。这些属于"跑测试前的环境准备"，排查方法放在 `tests/README.md` 的「启动零输出怎么办」，不计入缺陷清单。

> 这样报告里的每一条结论都能落到组员写的那几个文件上，评阅时可以对号入座。

---

## 1. 测试环境

| 项目 | 值 |
| --- | --- |
| 虚拟机 | VMware `xv6-lab`，Ubuntu 24.04.3 LTS x86_64 |
| 项目路径 | 虚拟机 `/mnt/hgfs/zhousx/my-toyos-lab-2` ↔ 主机 `D:\zhousx\my-toyos-lab-2` |
| 编译器 | `riscv64-linux-gnu-gcc 13.3.0`（`-Wall -Werror`） |
| QEMU | `qemu-system-riscv64`（以 `run_all.sh` 首行打印的版本为准） |
| 启动参数 | `-machine virt -bios none -m 128M -smp 2 -nographic` |
| 内存布局 | `ALLOC_BEGIN ≈ 0x8002_0000`，`ALLOC_END = 0x8800_0000`；内核区 1024 页，其余归用户区 |

---

## 2. 被测功能点（从组员代码里读出来的契约）

| 函数 | 代码位置 | 契约（代码里的注释 + 实现） | 对应用例 |
| --- | --- | --- | --- |
| `pmem_init` | pmem.c:8-40 | 内核区 `[ALLOC_BEGIN, +1024 页)`、用户区 `[kernel_end, ALLOC_END)`；两区各自建空闲链表、`allocable` 计数、初始化锁 | 03,04,05 |
| `pmem_alloc` | pmem.c:44-90 | 从对应链表摘头节点；空 → `assert` panic；返回前 `memset` 清零 | 03,04,05,06,07 |
| `pmem_free` | pmem.c:94-129 | 检查 `[ALLOC_BEGIN, ALLOC_END)`、4KB 对齐、区域标签；插回链表头；`allocable++` | 06,08-13 |
| `vm_getpte` | kvm.c:10-61 | 三级查找；`va >= VA_MAX` panic；`alloc=false` 时中间级无效返回 NULL；`alloc=true` 时补建页表；中间级不是页表 → panic | 14,30,35 |
| `vm_mappages` | kvm.c:67-101 | 五个前置检查（va/pa 对齐、len>0、`va+len<=VA_MAX`、叶子未映射）；按 `len` 向上取整逐页写 PTE | 15-27 |
| `vm_unmappages` | kvm.c:105-144 | 检查 va 对齐、len>0、区间；叶子必须有效且是叶子；`freeit` 决定是否 `pmem_free`；清 PTE | 28-34, 39 |
| `kvm_init` | kvm.c:148-179 | 新建根页表；UART/CLINT/PLIC 恒等映射；内核代码区 R\|X、数据区 R\|W、可分配区 R\|W | 02,38 |
| `kvm_inithart` | kvm.c:183-187 | `w_satp(MAKE_SATP(kernel_pgtbl))` + `sfence.vma` | 01,02 |
| `vm_print` | kvm.c:190-225 | 打印三级页表的所有有效项；中间级必须是页表、叶子必须不是页表 | 37 |

---

## 3. 测试策略：路径覆盖 + 等价类 + 边界值

组里的要求是"三种都要有"，这里明确它们各自落在哪些用例上。

### 3.1 路径覆盖（每条分支至少走一次）

| # | 分支 | 用例 |
| --- | --- | --- |
| P1 | `pmem_alloc(true)` 链表非空 | 03 |
| P2 | `pmem_alloc(true)` 链表空 → `spinlock_release` + `assert` | 03 |
| P3 | `pmem_alloc(false)` 链表非空 | 04 |
| P4 | `pmem_alloc(false)` 链表空 → panic | 04 |
| P5 | `pmem_free` 地址 < `ALLOC_BEGIN` | 09 |
| P6 | `pmem_free` 地址 ≥ `ALLOC_END` | 10 |
| P7 | `pmem_free` 未对齐 | 08 |
| P8 | `pmem_free` 区域标签与地址不符（两个方向） | 11,12 |
| P9 | `pmem_free` 正常路径（内核 / 用户） | 05,06 |
| P10 | `pmem_free` 重复释放（**代码没有这个分支**） | 13 |
| P11 | `vm_getpte` 顶级无效 + `alloc=false` → NULL | 14,30 |
| P12 | `vm_getpte` 顶级/次级无效 + `alloc=true` → 新建页表 | 14,18,19 |
| P13 | `vm_getpte` 叶子已存在（`alloc=true` 不再分配） | 26 |
| P14 | `vm_getpte` 中间级不是页表 → panic（要人为构造非法页表，正常 API 产生不了） | 未覆盖，见 §9 |
| P15 | `vm_mappages` va 未对齐 / pa 未对齐（同一个 `\|\|` 的两侧） | 22,23 |
| P16 | `vm_mappages` len == 0 | 24 |
| P17 | `vm_mappages` `va >= VA_MAX` 与 `len > VA_MAX-va` 两个条件 | 21,25 |
| P18 | `vm_mappages` 叶子已映射 → panic | 26 |
| P19 | `vm_unmappages` va 未对齐 / len==0 / 区间越界（`\|\|` 两侧） | 34,32,33,39 |
| P20 | `vm_unmappages` 中间级缺失 → NULL → panic | 30 |
| P21 | `vm_unmappages` 叶子 V=0 → panic | 31 |
| P22 | `vm_unmappages` `freeit=true` / `false` | 29 / 28 |
| P23 | 多页循环 `while(pages--)` 的 1 页 / 多页 / 跨界 | 15,16,18,19 |

### 3.2 等价类划分

| 维度 | 等价类 | 用例 |
| --- | --- | --- |
| 页所属区域 | 内核区 / 用户区 | 05（两区各 16 页，交叉不重叠） |
| 页状态 | 已分配 / 空闲 | 06,07（释放后再分配） |
| 地址对齐 | 对齐 / 未对齐 | 05 / 08,22,23 |
| 地址范围 | 区域内 / 左外 / 右外（半开区间） | 05 / 09 / 10 |
| 区域标签 | 标签正确 / 两个方向的标签错误 | 05 / 11,12 |
| `len` | 极小（1） / 一页（4096） / 多页 | 17,16 |
| `perm` | R / R\|W / R\|X | 15 / 15 / 27 |
| `alloc` 参数 | false / true | 14 |
| 映射状态 | 已映射 / 未映射（页表缺失、叶子 V=0） | 15 / 30,31 |
| 并发 | 单 hart / 双 hart | 其余 / 36 |

### 3.3 边界值

| 边界 | 取值 | 用例 |
| --- | --- | --- |
| `len` 下界 | 0（非法）、1、4095、4096、4097 | 24 / 17 / 17 / 17 / 17 |
| 虚地址下界 | 0 附近（`0x1ff000` 跨 2MB） | 18 |
| 2MB 页表边界 | `0x200000-4096` 起 2 页 | 18 |
| 1GB 页表边界 | `0x40000000-4096` 起 2 页 | 19 |
| 虚地址上界 | `VA_MAX-PGSIZE`（可映射）、`VA_MAX`（非法） | 20 / 21, 39 |
| 可分配区左界 | `ALLOC_BEGIN-PGSIZE`（外）、`ALLOC_BEGIN` 所在页 | 09 / 03 |
| 可分配区右界 | `ALLOC_END`（外）、`ALLOC_END-PGSIZE` | 10 / 38 |
| 页数边界 | 内核区正好 1024 页、用户区正好 N 页 | 03 / 04 |

---

## 4. 用例总表

完整 39 个用例的输入 / 预期 / 判定见 `docs/testing/test-cases.md`，机器可读的版本是 `tests/cases.tsv`。

| 段落 | 用例 | 数量 |
| --- | --- | --- |
| 基础 | 01–02 | 2 |
| 物理内存 | 03–13 | 11 |
| 页表 | 14–35, 37, 38, 39 | 25 |
| 并发 | 36 | 1 |

---

## 5. 代码审阅已发现的问题（用例负责确认）

| 编号 | 位置 | 问题 | 期望 | 用例 | 状态 |
| --- | --- | --- | --- | --- | --- |
| BUG-L2-1 | `pmem.c:94-129` | `pmem_free` 不检查"页是否已空闲"，重复释放会把 `list_head.next` 接成自环，之后 `pmem_alloc` 永远返回同一页、`allocable` 越界 | 应 panic | 13 | 待确认 |
| BUG-L2-2 | `kvm.c:105-144` | 解映射只清叶子、不回收空的三级页表；反复 map/unmap 不同 VA 会持续泄漏内核页 | 可接受（lab 未要求）但需登记 | — | 已知限制 |
| BUG-L2-3 | `kvm.c:67,105` | `vm_mappages/vm_unmappages` 不检查 `pgtbl` 非空、未清零；传 NULL 直接 load fault | 应显式 panic | — | 低危 |
| BUG-L2-4 | `pmem.c:13` | `kern_region.end = ALLOC_BEGIN + KERN_PAGES*PGSIZE` 没校验不超过 `ALLOC_END`；改 `KERN_PAGES` 会越界 | 应 `assert` | — | 低危 |
| BUG-L2-5 | `kvm.c:92-94` | `perm` 未校验：`perm=0` 会写出 V=1 但 R/W/X 全 0 的叶子，`vm_print`/`vm_getpte` 会把它误当页表 | 应拒绝 perm=0 | — | 低危 |
| BUG-L2-6 | `lib/print.c` | `%p` 只打印 32 位（lab-1 遗留），打印 64 位地址会截断 | 测试里只用 32 位以内的地址规避 | — | 已知 |
| BUG-L2-7 | `boot/start.c` | 每核栈 4KB（lab-1 BUG-7）；测试套件因此把数组都放 `static` | — | — | 设计约束 |

---

## 6. 执行步骤

```bash
# 0. 虚拟机里挂载共享目录（每次重启后）
sudo mkdir -p /mnt/hgfs && sudo vmhgfs-fuse .host:/ /mnt/hgfs -o allow_other

# 1. 进项目
cd /mnt/hgfs/zhousx/my-toyos-lab-2

# 2. 一条命令：跑 39 个用例 + 自动出图（约 2 分钟）
bash tests/run_all.sh

#   只想跑测试不出图：bash tests/run_tests.sh
#   只想要日志渲染图：bash tests/run_all.sh --render
#   想要真·屏幕截图：  bash tests/run_all.sh --gui（终端先最大化）

# 3. 看某个失败用例的完整串口输出
cat tests/results/13_pmem_double_free.log

# 4. 单跑 + 一屏展示（截图用）
bash tests/run_tests.sh 15_kvm_map_single
bash tests/show_case.sh 15_kvm_map_single
```

自动出图两种模式：

| 模式 | 依赖 | 产物 |
| --- | --- | --- |
| `--render`（默认在没有桌面时使用） | `python3-pil`、`fonts-noto-cjk` | `docs/testing/images/` 下 39 个用例 + `00-summary.png`，终端风格 PNG |
| `--gui` | `gnome-screenshot` + 桌面 | `tests/screenshot.list` 里的 30 个用例，逐屏截屏 |

---

## 7. 交付物

| 文件 | 内容 |
| --- | --- |
| `tests/` | 39 个用例 + 运行脚本 + 用例表（代码） |
| `tests/run_all.sh` | 一条命令：跑测试 + 自动出图 |
| `tests/CHECKLIST.md` | 一页版测试清单（功能点 → 用例 → 期望） |
| `tests/render_shot.py` | 把串口日志渲染成终端风格 PNG |
| `tests/README.md` | 套件用法、设计、排错 |
| `docs/testing/test-plan.md` | 测试计划（正式版） |
| `docs/testing/test-cases.md` | 用例设计（输入/预期/覆盖点） |
| `docs/testing/test-report.md` | 测试报告（结果 + 截图 + 缺陷） |
| `docs/testing/images/` | 每个用例一张截图（run_all 自动生成） |

---

## 8. 进度

| 阶段 | 内容 | 状态 |
| --- | --- | --- |
| T0 | 读组员代码、拆功能点与分支 | ✅ 2026-11-15 |
| T1 | 搭 `tests/` 框架（3 个 stage） | ✅ 2026-11-15 |
| T2 | 写 39 个用例 | ✅ 2026-11-15 |
| T3 | 虚拟机里跑通并修测试自身的问题 | ✅ 2026-11-15（含构建侧 2 处 include 修复） |
| T4 | 出结果 + 截图 + 报告 | ✅ 2026-11-15（PASS=38 / FAIL=1） |
| T5 | 把 BUG-L2-1 反馈给组员，修完回归 | ⬜ |

---

## 9. 风险与未覆盖

1. **防御性 assert 分支没做用例**：`vm_getpte` 里"中间级不是页表"（kvm.c:31、50）、`vm_unmappages` 里"叶子其实是页表"（kvm.c:129）、`vm_print` 的两个结构断言（kvm.c:202、211、220）。这些分支只有在**页表被手工改坏**时才走得到，正常调用 `vm_mappages` 产生不出来，所以没做用例；其余分支已全部覆盖。
2. **页表泄漏没做定量验证**：`pmem.c` 的 `allocable` 是 `static`，测试拿不到；要量化只能"分配到 panic 数页数"，但 panic 后同一次运行无法继续，需要两次运行对比，收益低。已登记为 BUG-L2-2。
3. **`vm_print` 只做结构断言**：输出格式逐字符比对意义不大，用例 37 只检查三层结构与叶子项的格式。
4. **时钟 / 中断没测**：lab-2 不涉及，CLINT/PLIC 只验证"页表里有映射"（用例 38），不验证设备可用（lab-3 的主题）。
5. **`-smp 4` 越界**：`NCPU=2` 的限制是 lab-1 的 BUG-2，仍适用，不在 lab-2 范围。
