# 测试结果截图

每个用例一张截图，文件名直接用用例名：

| 文件 | 内容 |
| --- | --- |
| `00-summary.png` | `bash tests/run_tests.sh` 的 PASS/FAIL 汇总 |
| `01_boot_dual.png` | 用例 01：双核启动 + sum=2000000 |
| `02_boot_single.png` | 用例 02：单核，只有 cpu 0 |
| `03_sum_unlocked.png` | 用例 03：去锁后 sum < 2000000 |
| `04_print_nolock.png` | 用例 04：A/B 交错 |
| `05_printf_formats.png` | 用例 05：五个格式符 |
| `06_printf_boundary.png` | 用例 06：数值边界 |
| `07_printf_percent.png` | 用例 07：`%%` 失败（BUG-5） |
| `08_lock_double_acquire.png` | 用例 08：重复加锁 panic |
| `09_lock_release_unheld.png` | 用例 09：未持有释放 panic |
| `10_lock_stress.png` | 用例 10：锁竞争 sum=400000 |

生成方式：

```bash
bash tests/run_tests.sh          # 先跑一遍
bash tests/show_case.sh 01_boot_dual   # 一屏一个用例，PrtSc 截图
```

截图后在 `test-report.md` 里用 `![用例01](images/01_boot_dual.png)` 引用。
