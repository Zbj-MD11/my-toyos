# lab-1 测试用例

> 所有命令在项目根目录执行。
> QEMU 统一用 `timeout --foreground <秒> qemu-system-riscv64 ... < /dev/null`，避免终端 stdin 导致 QEMU 挂起。

## A. 功能验收

| 编号 | 用例 | 步骤 | 预期结果 |
| --- | --- | --- | --- |
| A1 | 干净编译 | `make clean && make build` | 无 error；仅允许 `LOAD segment with RWX permissions` 警告 |
| A2 | 双核启动 | `make build` 后以 `-smp 2` 启动 | 输出 `cpu 0 is booting!` 与 `cpu 1 is booting!` |
| A3 | 4.1 加锁版 | 同 A2，观察 `sum` 报告 | 两次报告中最大值为 `2000000` |
| A4 | 单核对照 | 以 `-smp 1` 启动 | 只有 `cpu 0`，`sum = 1000000` |
| A5 | 4.1 去锁对照 | 副本中删除 `sum_lk` 的 acquire/release，重复运行 | 至少一次 `sum < 2000000`（观察到丢更新） |
| A6 | 4.2 并行输出 | 副本中删除 `print_lk`，两核并发打印长串 | 出现 A/B 字符交错的行 |
| A7 | printf 五格式 | 副本中插入一行格式测试 | `fmt: -123 0x123456789abcdef0 2a A ok` |
| A8 | 10 次稳定性 | 连续启动 10 次，每次检查两行启动信息 | 10/10 通过 |
| A9 | GDB 单步 | `make debug` + `gdb-multiarch`，断点 `_entry`、`main` | 断点命中，`mret` 后 PC 进入 `main` |
| A10 | 最终回归 | 修复后重跑 A2、A3、A8、B1–B4 | 全部通过 |

## B. 边界与故障注入

| 编号 | 用例 | 步骤 | 观察点 |
| --- | --- | --- | --- |
| B1 | 多核越界 | `-smp 4`、`-smp 8` 启动 | `NCPU=2`，hartid≥2 时 `CPU_stack`、`cpus[]` 是否越界 |
| B2 | 内存边界 | `-m 32M`、`-m 1G` | 是否正常启动；PMP 范围是否覆盖 |
| B3 | 默认 bios | 去掉 `-bios none` 启动 | 是否进入 OpenSBI、内核是否还能被正确加载 |
| B4 | 关闭 PMP | `-cpu rv64,pmp=off` 启动 | 应正常；用于反向验证 PMP 补丁 |
| B5 | printf 边界 | 测试 `%%`、`INT_MIN`、`NULL`、`%q`、200 字符字符串 | 是否丢失字符、崩溃或输出错误值 |
| B6 | panic/assert | ① 重复加锁 ② 未持有就释放 ③ 持 `print_lk` 时 panic | 是否打印 panic；③ 是否死锁/爆栈 |
| B7 | 长输出/栈压力 | 循环打印长字符串 | 是否触发 4 KB 栈溢出 |
| B8 | Makefile 行为 | `make`、`make debug`（未 build）、改头文件后 build | 默认目标、debug 依赖、头文件依赖是否符合预期 |

## 附录：需要改代码的用例

### A5 去锁对照

```bash
rm -rf ~/lab1-exp-nosum
cp -r /mnt/hgfs/zhousx/my-toyos-lab-1 ~/lab1-exp-nosum
cd ~/lab1-exp-nosum
python3 - <<'PY'
p = 'src/kernel/main.c'
lines = open(p).read().splitlines(True)
lines = [l for l in lines
         if 'spinlock_acquire(&sum_lk)' not in l
         and 'spinlock_release(&sum_lk)' not in l]
open(p, 'w').writelines(lines)
PY
make clean && make build
for i in $(seq 1 5); do
  timeout --foreground 5 qemu-system-riscv64 -machine virt -bios none \
    -kernel target/kernel/kernel-qemu.elf -m 128M -smp 2 -nographic \
    < /dev/null | grep "sum ="
done
```

### A6 并行输出

```bash
rm -rf ~/lab1-exp-noprintlock
cp -r /mnt/hgfs/zhousx/my-toyos-lab-1 ~/lab1-exp-noprintlock
cd ~/lab1-exp-noprintlock

cat > src/kernel/main.c <<'EOF'
#include "arch/mod.h"
#include "lib/mod.h"

volatile static int started = 0;

void main()
{
    int cpuid = r_tp();

    if (cpuid == 0) {
        print_init();
        __sync_synchronize();
        started = 1;
    } else {
        while (started == 0);
        __sync_synchronize();
    }

    for (int i = 0; i < 300; i++) {
        if (cpuid == 0)
            printf("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n");
        else
            printf("BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB\n");
    }
    while (1);
}
EOF

python3 - <<'PY'
p = 'src/kernel/lib/print.c'
lines = open(p).read().splitlines(True)
lines = [l for l in lines
         if 'spinlock_acquire(&print_lk)' not in l
         and 'spinlock_release(&print_lk)' not in l]
open(p, 'w').writelines(lines)
PY

make clean && make build
timeout --foreground 5 qemu-system-riscv64 -machine virt -bios none \
  -kernel target/kernel/kernel-qemu.elf -m 128M -smp 2 -nographic \
  < /dev/null | head -40
```

### A7 printf 格式

```bash
rm -rf ~/lab1-exp-printf
cp -r /mnt/hgfs/zhousx/my-toyos-lab-1 ~/lab1-exp-printf
cd ~/lab1-exp-printf
python3 - <<'PY'
p = 'src/kernel/lib/print.c'
s = open(p).read()
anchor = '    spinlock_init(&print_lk, "printf");'
add = '\n    printf("fmt: %d %x %p %c %s\\n", -123, 0x123456789abcdef0ULL, 42, \'A\', "ok");'
open(p, 'w').write(s.replace(anchor, anchor + add, 1))
PY
make clean && make build
timeout --foreground 5 qemu-system-riscv64 -machine virt -bios none \
  -kernel target/kernel/kernel-qemu.elf -m 128M -smp 2 -nographic < /dev/null
```

### B5 printf 边界

在 A7 副本中，把插入行替换为：

```c
printf("e1:%d\n", -2147483648);
printf("e2:%%\n");
printf("e3:%s\n", (char*)0);
printf("e4:%q\n", 1);
printf("e6:%d\n", 2147483647);
char buf[201];
for (int i = 0; i < 200; i++) buf[i] = 'x';
buf[200] = 0;
printf("e5:%s\n", buf);
```

### B6 panic/assert

在副本的 `print_init()` 末尾插入 `spinlock_acquire(&print_lk); panic("held-lock test");`，观察是否递归 panic。重复加锁、未持有释放分别在 `main()` 的 CPU0 分支中构造。
