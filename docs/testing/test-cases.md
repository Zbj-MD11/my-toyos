# lab-1 测试用例

> 所有命令在项目根目录执行。
> QEMU 统一用 `timeout --foreground <秒> qemu-system-riscv64 ... < /dev/null`，避免终端 stdin 导致 QEMU 挂起。
> 命令中的 `/mnt/hgfs/zhousx/my-toyos-lab-1` 是测试者的本地项目路径，请替换为你自己的路径。

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
| A10 | 最终回归 | `bash tools/test.sh` + `bash tools/stability.sh 10` | 全部通过 |

## B. 边界与故障注入

| 编号 | 用例 | 步骤 | 观察点 |
| --- | --- | --- | --- |
| B1 | 多核越界 | `-smp 4`、`-smp 8` 启动 | `NCPU=2`，hartid≥2 时 `CPU_stack`、`cpus[]` 是否越界 |
| B2 | 内存边界 | `-m 32M`、`-m 1G` | 是否正常启动；PMP 范围是否覆盖 |
| B3 | 默认 bios | 去掉 `-bios none` 启动 | 是否进入 OpenSBI、内核是否还能被正确加载 |
| B4 | 关闭 PMP | `-cpu rv64,pmp=off` 启动 | 应正常；用于反向验证 PMP 补丁 |
| B5 | printf 边界 | 测试 `%%`、`INT_MIN`、`NULL`、`%q`、200 字符字符串 | 是否丢失字符、崩溃或输出错误值 |
| B6 | panic/assert | ① 重复加锁 ② 未持有就释放 ③ 持 `print_lk` 时 panic | 是否打印 panic；③ 是否死锁/爆栈 |
| B7 | 长输出/栈压力 | 副本中加入每层 256 字节的递归 `deep(30)`（约 8KB） | 是否触发 4 KB 栈溢出 |
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

### A9 GDB 单步

终端 1：`make build && make debug`（QEMU 冻结在复位向量，端口 = `25000 + uid%5000`，uid 1000 时为 26000）。

终端 2：

```bash
gdb-multiarch -batch \
  -ex 'set architecture riscv:rv64' \
  -ex 'target remote 127.0.0.1:26000' \
  -ex 'tbreak _entry' \
  -ex 'c' \
  -ex 'printf "hit _entry pc=%p\n", $pc' \
  -ex 'tbreak main' \
  -ex 'c' \
  -ex 'printf "hit main pc=%p\n", $pc' \
  -ex 'info registers pc sp tp' \
  -ex 'detach' \
  target/kernel/kernel-qemu.elf
```

用 `tbreak`（临时断点）而不是 `break`，否则第二个 hart 会再次停在 `_entry`。

### B7 栈压力

```bash
rm -rf ~/lab1-exp-stack
cp -r /mnt/hgfs/zhousx/my-toyos-lab-1 ~/lab1-exp-stack
cd ~/lab1-exp-stack

python3 - <<'PY'
p = 'src/kernel/main.c'
s = open(p).read()
func = '''static int deep(int n)
{
    volatile char buf[256];
    buf[0] = (char)n;
    if (n <= 0) return buf[0];
    return deep(n - 1) + buf[0];
}

void main()'''
s = s.replace('void main()', func, 1)
s = s.replace('        print_init();',
              '        print_init();\n        printf("b7: stack test start\\n");\n        deep(30);\n        printf("b7: stack test survived\\n");', 1)
open(p, 'w').write(s)
PY

make clean && make build
timeout --foreground 5 qemu-system-riscv64 -machine virt -bios none \
  -kernel target/kernel/kernel-qemu.elf -m 128M -smp 2 -nographic < /dev/null | head -20
```
