# tests/ —— lab-1 功能测试

围绕 `src/kernel/main.c` 的功能点做测试：**双核启动、4.1 并行加法（自旋锁）、printf、panic 路径**。
每个用例通过 `prepare.sh` 修改工程副本（不改主代码），编译后用 QEMU 运行，`check.sh` 判定。

## 运行

```bash
cd <项目根目录>
bash tests/run_tests.sh                    # 全部
bash tests/run_tests.sh 05_printf_formats  # 单个
```

输出示例：

```
用例                         结果   说明
01_boot_dual                 PASS   双核启动 + 加锁求和
...
PASS=8  FAIL=2
```

原始输出在 `tests/results/<用例>.log`，失败时先看这里。

## 目录

```
tests/
├── run_tests.sh          一键运行
├── cases/
│   ├── 01_boot_dual/
│   │   ├── case.conf     运行参数（-smp、内存、超时）与说明
│   │   ├── check.sh      判定规则
│   │   ├── prepare.sh    可选：修改工程副本（插入测试代码）
│   │   └── files/        可选：直接覆盖某些源文件
└── results/              运行日志（自动生成，不入库）
```

## 用例与功能点对应

| 用例 | 功能点 | 测试方法 |
| --- | --- | --- |
| 01_boot_dual | 双核启动 + 加锁求和 | `-smp 2`，两行 booting，sum 最大值 2000000 |
| 02_boot_single | 单核分支 | `-smp 1`，只有 cpu 0，sum=1000000 |
| 03_sum_unlocked | 锁的必要性 | 去掉 `sum_lk`，sum 明显小于 2000000 |
| 04_print_nolock | 输出锁的必要性 | 去掉 `print_lk`，A/B 字符交错 |
| 05_printf_formats | printf 五个格式符 | 等价类：`%d/%x/%p/%c/%s` |
| 06_printf_boundary | printf 数值边界 | 边界值：INT_MIN/INT_MAX/0/-1/UINT64_MAX |
| 07_printf_percent | 特殊格式符 | `%%`、`%q`、NULL、空串 |
| 08_lock_double_acquire | 锁的错误用法 | 重复加锁应 panic |
| 09_lock_release_unheld | 锁的错误用法 | 未持有就释放应 panic |
| 10_lock_stress | 锁竞争 | 两核各 20 万次小粒度加锁，sum=400000 |

## 说明

- 用例 03/04 是**对照实验**：故意去掉锁，证明锁是必要的。
- 用例 07 的 `%%` 在当前实现下会失败，这是已经登记的 BUG-5。
- 运行环境需要 `qemu-system-riscv64`、`riscv64-linux-gnu-gcc`、`python3`。
