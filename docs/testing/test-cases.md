# lab-1 测试用例

> 测试代码在 `tests/`，用 `bash tests/run_tests.sh` 一键运行。
> 每个用例在工程副本上修改代码（`prepare.sh` / `files/`），不会改动 `src/`。

## 1. 用例总表

| 编号 | 用例 | 功能点 | 等价类 / 边界 | 操作 | 预期 |
| --- | --- | --- | --- | --- | --- |
| 01 | `01_boot_dual` | 双核启动、4.1 加锁求和 | 正常路径 | `-smp 2` 原样运行 | 两行 booting；sum 最大值 2000000 |
| 02 | `02_boot_single` | 单核分支 | 边界：只有 1 个 hart | `-smp 1` | 只有 cpu 0；sum=1000000 |
| 03 | `03_sum_unlocked` | 锁的必要性 | 对照：去掉 `sum_lk` | 删除 acquire/release | sum 最大值 < 2000000 |
| 04 | `04_print_nolock` | 输出互斥 | 对照：去掉 `print_lk` | 两核各打印 300 行 64 字符 | 同一行出现 A/B 交错 |
| 05 | `05_printf_formats` | printf 五个格式符 | 等价类：`%d/%x/%p/%c/%s` | 一行输出全部格式 | `fmt: -123 0x123456789abcdef0 2a A ok` |
| 06 | `06_printf_boundary` | printf 数值边界 | 边界：MIN/MAX/0/-1/UINT64_MAX | 五行输出 | `b1:-2147483648` 等 |
| 07 | `07_printf_percent` | printf 特殊格式 | `%%`、NULL、`%q`、空串 | 五行输出 | `%%` 应输出 `%`（当前失败，BUG-5） |
| 08 | `08_lock_double_acquire` | 锁错误用法 | 重复加锁 | 连续 acquire 两次 | panic，提示已持有锁 |
| 09 | `09_lock_release_unheld` | 锁错误用法 | 未持有释放 | 直接 release | panic，提示未持有锁 |
| 10 | `10_lock_stress` | 锁竞争 | 2×20 万次小粒度加锁 | 每轮 acquire/increment/release | 最大值 400000 |

## 2. printf 用例设计

### 2.1 等价类（05）

| 输入 | 类别 | 预期 |
| --- | --- | --- |
| `%d` + `-123` | 有符号十进制 | `-123` |
| `%x` + `0x123456789abcdef0` | 64 位十六进制（带 0x） | `0x123456789abcdef0` |
| `%p` + `42` | 32 位十六进制（不带 0x） | `2a` |
| `%c` + `'A'` | 字符 | `A` |
| `%s` + `"ok"` | 字符串 | `ok` |

### 2.2 边界值（06）

| 编号 | 输入 | 预期 |
| --- | --- | --- |
| b1 | `INT_MIN`（`-2147483647 - 1`） | `-2147483648` |
| b2 | `INT_MAX` | `2147483647` |
| b3 | `0` | `0` |
| b4 | `-1` | `-1` |
| b5 | `UINT64_MAX` + `%x` | `0xffffffffffffffff` |

### 2.3 特殊格式（07）

| 编号 | 输入 | 期望 | 当前实际 |
| --- | --- | --- | --- |
| p1 | `%%` | `%` | 空（BUG-5） |
| p2 | `%s` + NULL | 空 | 空，未崩溃 |
| p3 | `%q` + 1 | 不崩溃 | 空（被静默丢弃） |
| p4 | `100%%` | `100%` | `100`（BUG-5） |
| p5 | `%s` + `""` | 空 | 空 |

> 07 用例的 FAIL 是**预期结果**，用于暴露 `printf` 不支持 `%%` 的缺陷。

## 3. 自旋锁用例设计

### 3.1 互斥与竞争

| 用例 | 场景 | 判定 |
| --- | --- | --- |
| 01 | 两核各 100 万次，整段持有 `sum_lk` | 两次报告的最大值 = 2000000 |
| 10 | 两核各 20 万次，每次 acquire/release | 最大值 = 400000 |

### 3.2 去锁对照

| 用例 | 改动 | 判定 |
| --- | --- | --- |
| 03 | 删除 `main.c` 中 `sum_lk` 的 acquire/release | 最大值 < 2000000（复现丢更新） |
| 04 | 删除 `print.c` 中 `print_lk` 的 acquire/release，并用并发打印的 `main.c` | 同一行出现 A/B 交错 |

### 3.3 错误用法

| 用例 | 注入代码 | 预期 |
| --- | --- | --- |
| 08 | `spinlock_acquire(&sum_lk); spinlock_acquire(&sum_lk);` | panic：`current CPU has hold the lock` |
| 09 | `spinlock_release(&sum_lk);` | panic：`current CPU does not hold the lock` |

## 4. 运行方式

```bash
# 全部用例（约 1–2 分钟）
bash tests/run_tests.sh

# 只跑一个
bash tests/run_tests.sh 05_printf_formats

# 单独展示（截图用）
bash tests/show_case.sh 05_printf_formats
```

运行日志在 `tests/results/<用例>.log`。

## 5. 判定

- `check.sh` 返回 0 → PASS；
- 返回非 0 → FAIL，先看 `tests/results/<用例>.log`；
- 07 的 FAIL 属已知缺陷，其余用例应全部 PASS。
