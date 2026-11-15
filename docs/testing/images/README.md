# 测试结果截图

生成方式（一条命令）：

```bash
bash tests/run_all.sh              # 跑测试 + 自动出图
bash tests/run_all.sh --render     # 只用真实日志渲染 PNG（39 张 + 汇总）
bash tests/run_all.sh --gui        # 逐屏 gnome-screenshot（30 张）
```

- `--render`：`tests/render_shot.py` 把 `tests/results/*.log` 画成终端窗口样式，标题栏是用例 id + 说明，右上角是 PASS/FAIL，底部是启动参数；不依赖桌面。
- `--gui`：`tests/screenshot_cases.sh` 按 `tests/screenshot.list` 逐屏截图（需 `gnome-screenshot`，终端最大化）。

文件名 = 用例 id，`test-report.md` 里用 `![用例01](images/01_boot.png)` 引用。

| 文件 | 内容 |
| --- | --- |
| `00-summary.png` | `bash tests/run_tests.sh` 的 PASS/FAIL 汇总（手工截） |
| `01_boot.png` | 原始内核双核启动 + 欢迎横幅 |
| `02_sv39_identity.png` | satp MODE=8、恒等映射读写 |
| `03_pmem_kernel_exhaust.png` | 内核区 1024 页、耗尽 panic |
| `04_pmem_user_exhaust.png` | 用户区页数、耗尽 panic |
| `05_pmem_alloc_unique.png` | 区域隔离 / 对齐 / 不重复 |
| `06_pmem_lifo_reuse.png` | 释放后可再分配 |
| `07_pmem_zero_after_free.png` | 再分配页已清零 |
| `08_pmem_free_unaligned.png` | 未对齐释放 panic |
| `09_pmem_free_below_range.png` | 左边界外 panic |
| `11_pmem_free_user_as_kernel.png` | 区域标签错误 panic |
| `13_pmem_double_free.png` | 重复释放（BUG-L2-1，预期 FAIL） |
| `14_kvm_getpte_paths.png` | getpte 两条 alloc 分支 |
| `15_kvm_map_single.png` | 单页映射 PA / 权限 |
| `16_kvm_map_multi.png` | 多页映射 |
| `17_kvm_map_len_boundary.png` | len = 1 / 4095 / 4097 |
| `18_kvm_map_cross_2mb.png` | 跨 2MB 页表边界 |
| `19_kvm_map_cross_1gb.png` | 跨 1GB 页表边界 |
| `20_kvm_map_top_va.png` | 最高可用 VA |
| `22_kvm_map_unaligned_va.png` | va 未对齐 panic |
| `24_kvm_map_len_zero.png` | len=0 panic |
| `25_kvm_map_overflow.png` | 区间溢出 panic |
| `26_kvm_map_duplicate.png` | 重复映射 panic |
| `27_kvm_perm_flags.png` | 权限位精确 |
| `28_kvm_unmap_partial.png` | 部分解映射 |
| `29_kvm_unmap_freeit.png` | freeit=true 归还物理页 |
| `30_kvm_unmap_unmapped.png` | 未映射 panic |
| `31_kvm_unmap_invalid_leaf.png` | 重复解映射 panic |
| `36_pmem_concurrent.png` | 双 hart 并发分配 / 释放 |
| `37_kvm_vm_print.png` | 三级页表结构输出 |
| `38_kvm_init_mappings.png` | UART/CLINT/PLIC/内核映射 |

截图后按 `test-report.md` 第 3 节的方式引用：`![用例01](images/01_boot.png)`。
