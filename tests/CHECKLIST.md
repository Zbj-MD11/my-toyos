# lab-2 测试清单（一页版）

> 详细设计见 `../docs/testing/test-cases.md`；机器可读的判定规则见 `cases.tsv`。
> 每个用例 = 一次 QEMU 运行（工程副本 + 注入测试入口），`make build` 编译后进行。

## 怎么跑

```bash
cd /mnt/hgfs/zhousx/my-toyos-lab-2
bash tests/run_all.sh            # 跑全部 + 自动出图（一条命令搞定）
```

产物：`tests/results/<用例>.log`、`tests/results/summary.txt`、`docs/testing/images/*.png`。

## 一、启动链路（不改代码，验证"能跑"）— 2 项

| # | 被测功能 | 操作 | 期望 |
| --- | --- | --- | --- |
| 01 | 原始 `main.c` + `start.c` 的启动流程 | 原样编译，`-smp 2` | 两行 `cpu N is booting!`、`Welcome to ToyOS!` |
| 02 | `kvm_inithart`（satp）+ 恒等映射 | 读 `satp`；分配一页并读写 | MODE=8（Sv39）；写入能读回 |

## 二、物理内存 `pmem.c` — 11 项

| # | 被测功能 | 操作 | 期望 |
| --- | --- | --- | --- |
| 03 | `pmem_init` 内核区 + `pmem_alloc(true)` 空分支 | 连分 1024 页，再分第 1025 页 | 都在 `[ALLOC_BEGIN,+4MB)`；`panic! pmem_alloc:No available OS_Page` |
| 04 | 用户区页数 + `pmem_alloc(false)` 空分支 | 连分到耗尽 | `用户区页数 = 总页数-1024`；`panic! ...USER_Page` |
| 05 | 两区域隔离 / 对齐 / 不重复 | 两区各分 16 页并交叉比对 | 无重叠、都 4KB 对齐、无重复 |
| 06 | `pmem_free` 插回链表 | 分 8 页→全释放→再分 8 页 | 新 8 页都在原集合内（回收生效） |
| 07 | 分配清零契约 | 写 `0xAA`→释放→再分配 | 再次拿到同一页且 4096 字节全 0 |
| 08 | `pmem_free` 对齐检查 | 释放 `pa+2048` | `panic! pmem_free:Page address doesn't match size` |
| 09 | `pmem_free` 左边界外 | 释放 `ALLOC_BEGIN-4096` | `panic! pmem_free:Invalid page address` |
| 10 | `pmem_free` 右端点（半开区间） | 释放 `ALLOC_END` | `panic! pmem_free:Invalid page address` |
| 11 | 区域标签：用户页当内核页 | `pmem_free(user_page, true)` | `panic! pmem_free:Invalid OS_page address` |
| 12 | 区域标签：内核页当用户页 | `pmem_free(kernel_page, false)` | `panic! pmem_free:Invalid USER_page address` |
| 13 | **重复释放**（缺检测） | 同一页释放两次 | 第二次应 panic；当前实现接受 → **预期 FAIL（BUG-L2-1）** |

## 三、内核态虚拟内存 `kvm.c` — 25 项

| # | 被测功能 | 操作 | 期望 |
| --- | --- | --- | --- |
| 14 | `vm_getpte` 三级 × `alloc` 两值 | 空页表上 `false`/`true`/`false` | 第一次 NULL；第二次非空且 `V=0`；第三次地址不变 |
| 15 | `vm_mappages` 单页 | map 1 页 R\|W | PTE 有效、PA 相等、权限恰好 R\|W、邻居未映射 |
| 16 | 多页循环 | map 3 页 R，PA 连续 | 三页 PA 依次 +4096，第 4 页未映射 |
| 17 | `len` 向上取整 | `len`=1 / 4095 / 4097 | 映射 1 / 1 / 2 页 |
| 18 | 跨 2MB 页表边界 | `va=0x1ff000`，`len=8192` | 两页都有效（新建二级页表） |
| 19 | 跨 1GB 页表边界 | `va=0x3ffff000`，`len=8192` | 两页都有效（新建一级页表） |
| 20 | 虚地址上界内 | `va=VA_MAX-4096` | 映射成功 |
| 21 | 虚地址上界外 | `va=VA_MAX` | `panic! vm_mappages:Va out of range` |
| 22 | va 对齐检查 | `va=VA_A+1` | `panic! ...Va or Pa is not page-aligned` |
| 23 | pa 对齐检查 | `pa+1` | 同上（`\|\|` 的另一侧） |
| 24 | `len` 下界 | `len=0` | `panic! vm_mappages:Invalid len` |
| 25 | 区间溢出 | `va=VA_MAX-4096`，`len=4097` | `panic! ...Va out of range` |
| 26 | 重复映射 | 同一 VA map 两次 | `panic! ...Try to change existing mapping` |
| 27 | 权限位精确写入 | map 用 R\|X | 低 10 位恰好 V\|R\|X，不夹带 U/W/A/D |
| 28 | `vm_unmappages` 部分解映射 | 4 页解中间 2 页（`freeit=false`） | 首尾有效、中间无效；可再映射；全解后全无效 |
| 29 | `freeit=true` 归还物理页 | 解映射后立刻再分配 | 分配到的正是刚释放的那页 |
| 30 | 解映射未映射 VA | 空页表上 unmap | `panic! ...leaf pte dosen't exist` |
| 31 | 重复解映射 | 同一 VA unmap 两次 | `panic! ...leaf pte dosen't valid` |
| 32 | 解映射 `len=0` | `len=0` | `panic! vm_unmappages:Invalid len` |
| 33 | 解映射区间溢出 | `va=VA_MAX-4096`，`len=4097` | `panic! vm_unmappages:Va out of range` |
| 34 | 解映射 va 对齐 | `va=VA_A+1` | `panic! vm_unmappages:Va is not page-aligned` |
| 35 | `vm_getpte` 范围检查 | `va=VA_MAX` | `panic! vm_getpte:Va out of range` |
| 37 | `vm_print` 结构输出 | map 1 页后打印 | 三层结构 + `physical page 0: ... flags = 7` |
| 38 | `kvm_init` 六段映射 | 从 `satp` 取根页表逐段查 | UART/CLINT/PLIC 恒等 R\|W；内核代码区有 X；可分配区有 W |
| 39 | 解映射虚地址上界外 | `va=VA_MAX` | `panic! vm_unmappages:Va out of range` |

## 四、并发 — 1 项

| # | 被测功能 | 操作 | 期望 |
| --- | --- | --- | --- |
| 36 | 自旋锁保护空闲链表 | 两 hart 各分 64 页 | 128 页不重复、都在用户区；释放后还能再分 128 页 |

## 判定方式

- 正常用例：串口输出里出现 `RESULT: PASS <名字>`，且没有 `panic!`；
- 期望 panic 的用例：出现指定的 `panic! <函数原文>`，且没有 `LAB2:`（`LAB2:` = "错误输入被接受"的自曝标记）。

## 结果怎么用

| 情况 | 说明 |
| --- | --- |
| 37 PASS / 1 FAIL | 预期状态；FAIL 的是 13（BUG-L2-1） |
| 01 零输出 | 看 `README.md` 的「启动零输出怎么办」：属环境准备问题，不计入测试缺陷 |
| 别的 FAIL | 真 bug，看 `tests/results/<用例>.log` 的 panic 原文 |
