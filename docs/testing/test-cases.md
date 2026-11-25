# lab-2 测试用例设计

- 机器可读版本：`tests/cases.tsv`（判定规则以它为准）
- 每个用例一次 QEMU 运行；panic 用例由 `timeout` 结束，退出码非 0 是预期现象
- "预期"列里的 `panic!` 后面是被测代码 `assert/panic` 的原文

## 判定方式

| 用例类型 | PASS 的条件 |
| --- | --- |
| 正常用例 | 串口输出出现 `RESULT: PASS <名字>`，且没有 `panic!` |
| 期望 panic 的用例 | 串口输出出现指定的 `panic! <原文>`，且没有 `LAB2:`（`LAB2:` 是"错误输入被接受"的自曝标记） |

---

## A. 基础（stage=orig / vm）

| 编号 | 方法 | 操作 | 预期 | 覆盖点 |
| --- | --- | --- | --- | --- |
| 01_boot | 冒烟 | 原始 `main.c` 原样编译运行，`-smp 2` | 两行 `cpu N is booting!` + `Welcome to ToyOS!` | 启动链路、双核 |
| 02_sv39_identity | 路径+边界 | `satp` 的 MODE 字段；分配一个用户页，用 `pa` 当地址读写 | MODE=8；写入值能读回 | `kvm_inithart`、恒等映射 |

## B. 物理内存（stage=pmem）

| 编号 | 方法 | 操作 | 预期 | 覆盖点 |
| --- | --- | --- | --- | --- |
| 03_pmem_kernel_exhaust | 路径+边界 | 连续 `pmem_alloc(true)` 1024 次，再分配第 1025 次 | 每页在 `[ALLOC_BEGIN, ALLOC_BEGIN+4MB)` 且对齐；`kernel pages = 1024`；第 1025 次 `panic! pmem_alloc:No available OS_Page` | 内核区页数、链表空分支 |
| 04_pmem_user_exhaust | 路径+边界 | 连续 `pmem_alloc(false)` 到耗尽 | `user pages = 总页数-1024`；页都在 `[kernel_end, ALLOC_END)`；再分配 `panic! pmem_alloc:No available USER_Page` | 用户区页数、右边界 |
| 05_pmem_alloc_unique | 等价类 | 两区各分配 16 页 | 全部 4KB 对齐、落在本区、两两不重复、两区不交叉；释放后 PASS | {内核,用户}×{页} |
| 06_pmem_lifo_reuse | 路径 | 分配 8 页 → 全部释放 → 再分配 8 页 | 新分配的 8 页都在原集合里 | 释放插回链表、回收路径 |
| 07_pmem_zero_after_free | 等价类 | 分配 → 写 `0xAA` → 释放 → 再分配同一页 | 再次分配到的就是该页，且 4096 字节全 0 | 分配清零契约 |
| 08_pmem_free_unaligned | 边界 | 释放 `pa + 2048` | `panic! pmem_free:Page address doesn't match size` | 对齐检查 |
| 09_pmem_free_below_range | 边界 | 释放 `ALLOC_BEGIN - 4096` | `panic! pmem_free:Invalid page address` | 左边界外 |
| 10_pmem_free_at_end | 边界 | 释放 `ALLOC_END`（半开区间右端点） | `panic! pmem_free:Invalid page address` | 右端点 |
| 11_pmem_free_user_as_kernel | 等价类 | 用户页用 `in_kernel=true` 释放 | `panic! pmem_free:Invalid OS_page address` | 区域标签 |
| 12_pmem_free_kernel_as_user | 等价类 | 内核页用 `in_kernel=false` 释放 | `panic! pmem_free:Invalid USER_page address` | 区域标签 |
| 13_pmem_double_free | 路径 | 同一页连续释放两次 | 第二次必须被拒绝；当前实现接受 → 打印 `LAB2: double free accepted`，用例判 FAIL（BUG-L2-1） | 重复释放（代码缺分支） |

## C. 内核态虚拟内存（stage=vm）

| 编号 | 方法 | 操作 | 预期 | 覆盖点 |
| --- | --- | --- | --- | --- |
| 14_kvm_getpte_paths | 路径 | 空页表上 `vm_getpte(va,false)` / `(va,true)` / `(va,false)` | 第一次 NULL；第二次返回非空叶子且 `V=0`；第三次与第二次同一地址 | 三级 `alloc` 分支 |
| 15_kvm_map_single | 路径 | 映射 1 页 R\|W | PTE 有效、PA 相等、权限恰好 R\|W、相邻页未映射 | 单页映射 |
| 16_kvm_map_multi | 路径 | 映射 3 页 R，PA 连续 | 三页 PTE 的 PA 依次 +4096，第 4 页未映射 | `while(pages--)` |
| 17_kvm_map_len_boundary | 边界 | `len` = 1 / 4095 / 4097 | 分别映射 1 / 1 / 2 页 | 向上取整边界 |
| 18_kvm_map_cross_2mb | 边界 | `va = 0x200000-4096`，`len = 8192` | 两页都有效（跨 2MB 边界要新建二级页表） | 页表边界 |
| 19_kvm_map_cross_1gb | 边界 | `va = 0x40000000-4096`，`len = 8192` | 两页都有效（跨 1GB 要新建一级页表） | 页表边界 |
| 20_kvm_map_top_va | 边界 | `va = VA_MAX-4096`，`len = 4096` | 映射成功 | 虚地址上界内 |
| 21_kvm_map_va_max | 边界 | `va = VA_MAX` | `panic! vm_mappages:Va out of range` | 虚地址上界外 |
| 22_kvm_map_unaligned_va | 边界 | `va = VA_A+1` | `panic! vm_mappages:Va or Pa is not page-aligned` | `\|\|` 左侧 |
| 23_kvm_map_unaligned_pa | 边界 | `pa+1` | 同上 | `\|\|` 右侧 |
| 24_kvm_map_len_zero | 边界 | `len = 0` | `panic! vm_mappages:Invalid len` | `len` 下界 |
| 25_kvm_map_overflow | 边界 | `va = VA_MAX-4096`，`len = 4097` | `panic! vm_mappages:Va out of range` | 加法溢出防护 |
| 26_kvm_map_duplicate | 路径 | 同一 VA 映射两次 | `panic! vm_mappages:Try to change existing mapping` | 已映射分支 |
| 27_kvm_perm_flags | 等价类 | 用 `PTE_R\|PTE_X` 映射 | 低 10 位恰好 `V\|R\|X`，不夹带 U/W/A/D | 权限位 |
| 28_kvm_unmap_partial | 路径 | 映射 4 页，解映射中间 2 页（`freeit=false`） | 首尾仍有效、中间无效；再映射成功；整体解映射后全部无效 | 部分解映射 |
| 29_kvm_unmap_freeit | 等价类 | `freeit=true` 解映射后立刻再分配 | 分配到的正是刚才那一页 | 物理页归还 |
| 30_kvm_unmap_unmapped | 路径 | 空页表上解映射 | `panic! vm_unmappages:leaf pte dosen't exist` | 中间级缺失 |
| 31_kvm_unmap_invalid_leaf | 路径 | 连续解映射同一 VA 两次 | `panic! vm_unmappages:leaf pte dosen't valid` | 叶子 V=0 |
| 32_kvm_unmap_len_zero | 边界 | `len = 0` | `panic! vm_unmappages:Invalid len` | `len` 下界 |
| 33_kvm_unmap_overflow | 边界 | `va = VA_MAX-4096`，`len = 4097` | `panic! vm_unmappages:Va out of range` | 溢出防护 |
| 34_kvm_unmap_unaligned | 边界 | `va = VA_A+1` | `panic! vm_unmappages:Va is not page-aligned` | 对齐检查 |
| 35_kvm_getpte_va_max | 边界 | `vm_getpte(VA_MAX, false)` | `panic! vm_getpte:Va out of range` | getpte 的范围检查 |
| 37_kvm_vm_print | 结构 | 映射 1 页后 `vm_print` | 输出三层结构，叶子项为 `physical page 0: pa = ... flags = 7` | debug 接口 |
| 38_kvm_init_mappings | 路径 | 从 `satp` 取根页表，检查六段映射 | UART/CLINT/PLIC 恒等映射且 R\|W；内核代码区有 X；可分配区有 W | `kvm_init` |
| 39_kvm_unmap_va_max | 边界 | `vm_unmappages(VA_MAX, PGSIZE)` | `panic! vm_unmappages:Va out of range` | `\|\|` 的左侧条件 |

## D. 并发（stage=vm）

| 编号 | 方法 | 操作 | 预期 | 覆盖点 |
| --- | --- | --- | --- | --- |
| 36_pmem_concurrent | 并发 | 两 hart 各分配 64 页（原子变量同步） | 128 页两两不重复、都在用户区；释放后 hart0 还能再分配 128 页 | 自旋锁保护链表 |

---

## 用例与三类方法的对应

| 方法 | 用例编号 |
| --- | --- |
| 路径覆盖 | 01,02,03,04,06,11,12,13,14,15,16,19,26,28,29,30,31,37,38,39 |
| 等价类 | 05,07,11,12,27,29 |
| 边界值 | 03,04,08,09,10,17,18,19,20,21,22,23,24,25,32,33,34,35,39 |
| 并发/稳定性 | 36 |

> 一个用例通常同时覆盖多类（例如 03 既是路径覆盖的"链表空分支"，也是"1024 页"的边界）。

## 已知预期失败

| 用例 | 原因 | 修复建议 |
| --- | --- | --- |
| 13_pmem_double_free | `pmem_free` 没有"页已在空闲链表"的检测，第二次释放把 `node->next` 指向自己，链表成环 | 增加页状态位（如 PTE 之外的 bitmap）或在 `pmem_free` 里遍历链表查重；简单做法：分配时把页从链表摘下后写 magic，释放时校验 magic 再清空 |
