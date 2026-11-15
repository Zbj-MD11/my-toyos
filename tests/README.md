# tests/ —— lab-2 内存管理测试套件

围绕 **物理内存（`pmem_init/alloc/free`）** 和 **内核态虚拟内存（`vm_getpte/mappages/unmappages`、`kvm_init/inithart`）** 做黑盒 + 白盒测试。
不修改 `src/`：每个用例都把工程复制到 `tests/.work/<用例>`，再注入测试入口和测试代码。

## 一条命令跑完

```bash
cd /mnt/hgfs/zhousx/my-toyos-lab-2     # 虚拟机里的共享目录
bash tests/run_all.sh                  # 跑 39 个用例 + 自动出图（推荐）
```

`run_all.sh` 会做三件事：跑测试 → 生成截图 → 打印产物位置。截图有两种模式：

| 命令 | 截图方式 | 适用 |
| --- | --- | --- |
| `bash tests/run_all.sh` | 自动：有桌面且装了 gnome-screenshot 就真截图，否则渲染 PNG | 默认 |
| `bash tests/run_all.sh --render` | 用 `tests/render_shot.py` 把真实串口日志渲染成终端风格 PNG | 无桌面 / ssh / 想要稳定一致 |
| `bash tests/run_all.sh --gui` | 逐屏 `gnome-screenshot`（终端要最大化） | 想要"真·屏幕截图" |
| `bash tests/run_all.sh --no-shot` | 不出图 | 只回归 |

渲染模式需要一次安装（VM 里）：

```bash
sudo apt install -y python3-pil fonts-noto-cjk
```

单跑测试（不出图）：

```bash
bash tests/run_tests.sh
```

输出：

```
============================================================
 lab-2 内存管理测试    2026-11-xx xx:xx:xx
============================================================
用例                           结果   说明
01_boot                        PASS   原始内核启动（未注入测试代码）
...
PASS=37  FAIL=1          # 只有 13_pmem_double_free 预期失败
```

日志在 `tests/results/<用例>.log`，失败先看这里。

单跑一个用例、单看一个用例：

```bash
bash tests/run_tests.sh 15_kvm_map_single
bash tests/show_case.sh 15_kvm_map_single     # 一屏：说明 + 串口输出 + 判定
```

## 目录

```
tests/
├── run_all.sh          一条命令：跑测试 + 出图
├── run_tests.sh        只跑测试（读 cases.tsv）
├── cases.tsv           用例表：id|stage|测试函数|smp|超时|必须出现|禁止出现|说明
├── CHECKLIST.md        一页版测试清单（功能点 → 用例 → 期望）
├── suite/lab2_tests.c  全部测试函数（注入到 src/kernel/lab2_tests.c）
├── templates/          main.c 模板
│   ├── main_pmem.c     stage=pmem：只 print_init + pmem_init
│   └── main_vm.c       stage=vm  ：再加 kvm_init + kvm_inithart
├── show_case.sh        展示单个用例（截图用）
├── screenshot.list     需要真截图（--gui）的用例清单
├── screenshot_cases.sh gnome-screenshot 逐屏截图
├── render_shot.py      把日志渲染成终端风格 PNG（--render）
├── .work/              每个用例的工程副本（自动生成）
└── results/            每个用例的串口日志 + summary.txt（自动生成）
```

三个阶段（`cases.tsv` 的 stage 列）：

| stage | main 里做了什么 | 用途 |
| --- | --- | --- |
| `orig` | 原始 `main.c`，什么都不注入 | 证明"组员的代码原样能启动" |
| `pmem` | `print_init` + `pmem_init` + 用例 | 物理内存用例，页数干净（内核区恰好 1024 页） |
| `vm` | 再加 `kvm_init` + `kvm_inithart` | 页表用例，在 Sv39 打开后运行 |

## 用例一览

| 类别 | 用例 | 覆盖点 |
| --- | --- | --- |
| 基础 | 01_boot | 原样启动、双核、欢迎横幅 |
| 基础 | 02_sv39_identity | `satp` MODE=8、恒等映射可读写 |
| 物理内存 | 03_pmem_kernel_exhaust | 内核区 1024 页、链表空分支 panic |
| 物理内存 | 04_pmem_user_exhaust | 用户区页数 = 总页数-1024、panic |
| 物理内存 | 05_pmem_alloc_unique | 两区域不重叠 / 对齐 / 不重复 |
| 物理内存 | 06_pmem_lifo_reuse | 释放的页回到链表并能再分配 |
| 物理内存 | 07_pmem_zero_after_free | 再分配的页已清零 |
| 物理内存 | 08_pmem_free_unaligned | 未对齐 -> panic |
| 物理内存 | 09/10_pmem_free_*range | 左边界外 / 右端点 -> panic |
| 物理内存 | 11/12_pmem_free_*_as_* | 区域标签错误 -> panic |
| 物理内存 | 13_pmem_double_free | 重复释放应被拒绝（**当前 FAIL**） |
| 页表 | 14_kvm_getpte_paths | `alloc=false` / `alloc=true` 两条分支 |
| 页表 | 15/16_kvm_map_* | 单页 / 多页映射，PA 与权限位 |
| 页表 | 17_kvm_map_len_boundary | len = 1 / 4095 / 4097（向上取整） |
| 页表 | 18/19_kvm_map_cross_* | 跨 2MB、跨 1GB 页表边界 |
| 页表 | 20_kvm_map_top_va | 最高可用 VA |
| 页表 | 21–26_kvm_map_* | VA_MAX / 未对齐 va、pa / len=0 / 溢出 / 重复映射 -> panic |
| 页表 | 27_kvm_perm_flags | 权限位精确写入 |
| 页表 | 28_kvm_unmap_partial | 部分解映射，邻居不受影响 |
| 页表 | 29_kvm_unmap_freeit | `freeit=true` 把页还给空闲链表 |
| 页表 | 30–35_kvm_unmap_* | 未映射 / 重复解映射 / len=0 / 溢出 / 未对齐 -> panic |
| 页表 | 37_kvm_vm_print | `vm_print` 输出三级页表结构 |
| 页表 | 38_kvm_init_mappings | 从 satp 取根页表，检查 UART/CLINT/PLIC/内核/可分配区映射 |
| 页表 | 39_kvm_unmap_va_max | 解映射 `va = VA_MAX` -> panic（`\|\|` 的另一侧） |
| 并发 | 36_pmem_concurrent | 双 hart 各 64 页，不重复；释放后可再分配 |

## 三类测试设计如何落地

组里要求"路径覆盖 + 等价类 + 边界类"，对应关系：

**路径覆盖**（每条分支至少走一次）

| 分支 | 用例 |
| --- | --- |
| `pmem_alloc` `in_kernel=true` 链表非空 / 空 | 03 / 03 |
| `pmem_alloc` `in_kernel=false` 链表非空 / 空 | 04 / 04 |
| `pmem_free` 范围检查 / 对齐检查 / 区域检查 / 正常路径 | 09,10 / 08 / 11,12 / 06 |
| `vm_getpte` 三级页表 `alloc=false` 返回 NULL | 14, 30 |
| `vm_getpte` 三级页表 `alloc=true` 新建页表 | 14, 18, 19 |
| `vm_mappages` 五个前置检查（对齐/len/越界/已映射） | 22,23 / 24 / 21,25 / 26 |
| `vm_unmappages` 五个前置检查 + `freeit` 两个取值 | 34 / 32 / 33 / 30,31 / 28 / 29 |

**等价类**：{内核页, 用户页} × {已分配, 空闲} × {对齐, 未对齐} × {区域内, 区域外}；
`len` ∈ {极小, 一页, 多页}；`perm` ∈ {R, R|W, R|X}（15, 27）。

**边界值**：`len` = 1, 4095, 4096, 4097；`va` = 0 附近 / 2MB / 1GB / `VA_MAX-PGSIZE` / `VA_MAX`；
`pmem_free` 的地址取 `ALLOC_BEGIN-PGSIZE`、`ALLOC_END`、区域内首/末页。

## 预期失败 / 已知缺陷

| 编号 | 描述 | 用例 | 状态 |
| --- | --- | --- | --- |
| BUG-L2-1 | `pmem_free` 不检测重复释放，第二次释放会把链表接成环 | 13 | 待组员修 |

其余用例应当全绿。若出现别的 FAIL，多半是真 bug，按 `tests/results/<用例>.log` 定位。

## 启动零输出怎么办（环境排查，不计入测试缺陷）

按这个顺序排，别一上来就改代码：

1. **先排除脚本/终端**：`-nographic` 的 QEMU 在 stdin 还连着终端时可能被挂起，表现为零输出（lab-1 踩过这个坑）。本套件已经在所有 QEMU 调用里加了 `< /dev/null`；手工跑也要加。
2. **`-bios none` 必须在**：走 OpenSBI 时入口变成 `0x80200000`，内核起不来。
3. **还不行就用环境诊断工具**：不同 QEMU 版本对"未配置 PMP"的处理不同，用 A/B 判定（只在 `/tmp` 副本上改，不动 `src/`）：

   ```bash
   bash tools/diagnose_pmp.sh
   ```

   结论三种：都 >0 字节 → 环境放行，直接跑；A=0、B>0 → 这台机器要求 PMP（属环境约束）；都 0 → 与 PMP 无关，继续查别处。
4. **改了头文件要 `make clean`**：Makefile 生成了 `.d` 但没 `include`，头文件变化不触发重编。

## 截图

```bash
bash tests/run_all.sh              # 跑测试 + 出图（推荐）
bash tests/run_all.sh --render     # 只用日志渲染（39 张 + 00-summary）
bash tests/run_all.sh --gui        # 真·屏幕截图（终端最大化，30 张）
```

- `--render`：`tests/render_shot.py` 读 `tests/results/*.log`，画成终端窗口样式的 PNG，标题栏写用例 id + 说明、右上角写 PASS/FAIL、底部写启动参数。不依赖桌面，内容与真实串口日志一致。
- `--gui`：`tests/screenshot_cases.sh` 按 `tests/screenshot.list` 逐屏显示 + `gnome-screenshot`，依赖桌面会话和 `gnome-screenshot`。

两种模式的产物都在 `docs/testing/images/`，文件名 = 用例 id，`test-report.md` 里直接引用。

## 设计约束

- **不用大栈变量**：内核每核只有 4KB 栈（lab-1 BUG-7），本套件的数组全部是 `static`（放 `.bss`）。
- **每个 panic 用例单独一次 QEMU 运行**：panic 之后 `while(1)`，同一次运行里无法继续。
- **判定用输出字符串**：`assert/panic` 的原文就是判定依据，所以改内核里 panic 文案要同步改 `cases.tsv`。
