#!/usr/bin/env python3
"""Build isolated case copies; never modify repository src. Linux/Ubuntu runner."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import threading
import time

ROOT = Path(__file__).resolve().parent.parent
HI = (1 << 38)
USTACK = HI - 3*4096
BASE = '#include "sys.h"\nint main(void) { BODY while(1) asm volatile("nop"); }\n'
def program(body):
    return BASE.replace('BODY', body)
def fault(insn):
    return program('syscall(0); asm volatile(' + json.dumps(insn) + ');')

def cases():
    rows = [
        ('01_boot', 'basic', None, 1, 2, None),
        ('02_process_mapping', 'path', program('syscall(0); syscall(0);'), 1, 2, None),
        ('03_syscall_once', 'basic', program('if (syscall(0)!=0) asm volatile("unimp");'), 1, 1, None),
        ('04_syscall_repeat', 'boundary', program('for(int i=0;i<32;i++) if(syscall(0)!=0) asm volatile("unimp");'), 1, 32, None),
        ('05_user_timer', 'path', program('syscall(0);'), 1, 1, None),
        ('06_user_uart', 'path', program('syscall(0);'), 1, 1, None),
        ('07_smp2', 'concurrent', program('syscall(0);syscall(0);'), 2, 2, None),
        ('08_unknown_one', 'invalid', program('syscall(1);'), 1, 0, 'unknown syscall: 1'),
        ('09_unknown_large', 'invalid', program('syscall(2147483647);'), 1, 0, 'unknown syscall: 2147483647'),
        ('10_unknown_negative', 'invalid', program('syscall(-1);'), 1, 0, 'unknown syscall: -1'),
    ]
    faults = [
        ('11_illegal', '.word 0', 2, None),
        ('12_privileged', 'csrr t0, sstatus', 2, None),
        ('13_zero_read', 'li t0,0; ld t1,0(t0)', 13, 0),
        ('14_zero_write', 'li t0,0; sd zero,0(t0)', 15, 0),
        ('15_code_write', 'li t0,4096; sb zero,0(t0)', 15, 4096),
        ('16_trapframe_read', f'li t0,{HI-8192}; ld t1,0(t0)', 13, HI-8192),
        ('17_trampoline_read', f'li t0,{HI-4096}; ld t1,0(t0)', 13, HI-4096),
        ('18_stack_execute', f'li t0,{USTACK}; jr t0', 12, USTACK),
        ('19_stack_below', f'li t0,{USTACK-1}; lbu t1,0(t0)', 13, USTACK-1),
        ('20_code_after', 'li t0,8192; lbu t1,0(t0)', 13, 8192),
    ]
    rows += [(name,'protection',fault(ins),1,1,(cause,addr)) for name,ins,cause,addr in faults]
    body = f'volatile unsigned char *p=(void*){USTACK}UL; p[0]=0x5a; p[4095]=0xa5; if(p[0]!=0x5a || p[4095]!=0xa5) asm volatile("unimp"); syscall(0);'
    rows.append(('21_stack_endpoints','boundary',program(body),1,1,None))
    rows.append(('22_code_endpoints','boundary',program('volatile unsigned char *p=(void*)4096UL; volatile unsigned char x=p[0]; x=p[4095]; (void)x; syscall(0);'),1,1,None))
    # Assembly main avoids compiler-generated register changes around ecall.
    regs = ['ra','gp','tp'] + [f't{i}' for i in range(7)] + [f's{i}' for i in range(12)] + [f'a{i}' for i in range(1,7)]
    asm = ['addi sp,sp,-16','sd sp,0(sp)']
    for i,r in enumerate(regs): asm += [f'li {r},{100+i}']
    asm += ['li a7,0','li a0,77','ecall','bnez a0,9f','ld a0,0(sp)','bne a0,sp,9f','bnez a7,9f']
    for i,r in enumerate(regs): asm += [f'li a0,{100+i}',f'bne {r},a0,9f']
    asm += ['li a7,0','ecall','8: j 8b','9: unimp','j 9b']
    user = '__attribute__((naked)) int main(void) { __asm__ volatile('+json.dumps('\n'.join(asm))+'); }\n'
    rows.append(('23_trap_registers','path',user,1,2,None))
    rows.append(('24_context_switch','path',program('syscall(0);'),1,1,None))
    return rows

def replace_once(path, old, new):
    s = path.read_text(encoding='utf-8-sig')
    if s.count(old) != 1:
        raise RuntimeError(f'injection anchor changed: {path.name}: {old!r}')
    path.write_text(s.replace(old,new),encoding='utf-8',newline='\n')

def prepare(work, user, count):
    work.mkdir(parents=True)
    for name in ['Makefile','common.mk','kernel.ld']:
        shutil.copy2(ROOT/name,work/name)
    shutil.copytree(ROOT/'src',work/'src')
    if user is None:
        return
    (work/'src/user/initcode.c').write_text(user,encoding='utf-8')
    probe = (ROOT/'tests/probe.c').read_text(encoding='utf-8')
    (work/'src/kernel/trap/lab4_probe.c').write_text(f'#define EXPECT_CALLS {count}\n'+probe,encoding='utf-8')
    # In a subdirectory so entry.S remains the first assembly object.
    asm=['.text','.globl lab4_context_roundtrip','lab4_context_roundtrip:',
         'addi sp,sp,-112','sd ra,0(sp)']
    for i in range(12): asm += [f'sd s{i},{8+8*i}(sp)',f'li s{i},{800+i}']
    asm += ['sd sp,104(sp)','call swtch','ld t0,104(sp)','bne t0,sp,9f']
    for i in range(12): asm += [f'li t0,{800+i}',f'bne s{i},t0,9f']
    asm += ['li a0,0','j 8f','9: li a0,1','8:']
    for i in range(12): asm += [f'ld s{i},{8+8*i}(sp)']
    asm += ['ld ra,0(sp)','addi sp,sp,112','ret']
    (work/'src/kernel/trap/lab4_context.S').write_text('\n'.join(asm)+'\n',encoding='utf-8')
    p = work/'src/kernel/proc/proc.c'
    replace_once(p,'swtch(&cpu->ctx, &proczero.ctx);','extern void lab4_created(void); lab4_created();\n    swtch(&cpu->ctx, &proczero.ctx);')
    if work.name=='24_context_switch':
        replace_once(p,'extern void lab4_created(void); lab4_created();','extern void lab4_context_test(void); lab4_context_test();\n    extern void lab4_created(void); lab4_created();')
    p = work/'src/kernel/trap/trap_user.c'
    replace_once(p,'uint64 scause = r_scause();','extern void lab4_trap(void); lab4_trap();\n    uint64 scause = r_scause();')
    replace_once(p,'    trap_user_return();','    extern void lab4_return(void); lab4_return();\n    trap_user_return();')

def run_case(row, run):
    name,category,user,smp,count,expected = row
    work = run/name
    logpath = run/(name+'.log')
    reason = []
    try:
        prepare(work,user,count)
        with (run/(name+'.build.log')).open('wb') as out:
            subprocess.run(['make','clean'],cwd=work,stdout=out,stderr=subprocess.STDOUT,check=True,timeout=30)
            subprocess.run(['make','build'],cwd=work,stdout=out,stderr=subprocess.STDOUT,check=True,timeout=120)
        symbols = subprocess.check_output(['riscv64-linux-gnu-nm','-n',str(work/'target/kernel/kernel-qemu.elf')],text=True)
        if not any(line.split()==['0000000080000000','T','_entry'] for line in symbols.splitlines()):
            raise RuntimeError('_entry is not at 0x80000000')
        if (work/'target/user/initcode').stat().st_size > 4096:
            raise RuntimeError('user binary exceeds one code page')
        cmd = ['qemu-system-riscv64','-machine','virt','-bios','none','-m','128M','-smp',str(smp),'-nographic','-monitor','none','-kernel',str(work/'target/kernel/kernel-qemu.elf')]
        with logpath.open('wb') as out:
            out.write(('Command: '+' '.join(cmd)+'\n').encode()); out.flush()
            p = subprocess.Popen(cmd,stdin=subprocess.PIPE,stdout=out,stderr=subprocess.STDOUT)
            def feed():
                try:
                    for payload in [b'lab4-uart-A\n',b'lab4-uart-B\n']:
                        time.sleep(1)
                        p.stdin.write(payload); p.stdin.flush()
                except (BrokenPipeError,OSError):
                    pass
            t = None
            if name == '06_user_uart':
                t=threading.Thread(target=feed,daemon=True); t.start()
            try:
                p.wait(timeout=5)
                reason.append(f'QEMU exited early: {p.returncode}')
            except subprocess.TimeoutExpired:
                p.terminate()
                try: p.wait(timeout=3)
                except subprocess.TimeoutExpired: p.kill(); p.wait()
            if t: t.join(timeout=3)
            p.stdin.close()
        text = logpath.read_text(errors='replace')
        if user is not None:
            for token in ['[probe] process, initial context and mappings OK']:
                if token not in text: reason.append('missing '+token)
        if expected is None:
            if name=='24_context_switch' and '[probe] swtch ra, sp, s0-s11 roundtrip OK' not in text: reason.append('context roundtrip evidence missing')
            for token in ([] if user is None else ['[probe] expected syscall count reached','[probe] syscall return a0=0, PC+4 OK']):
                if token not in text: reason.append('missing '+token)
            if text.count('proczero: hello world!') != count: reason.append('wrong syscall output count')
            if 'panic!' in text or 'unexpected' in text: reason.append('unexpected trap/panic')
            if name in ['05_user_timer','06_user_uart','07_smp2'] and '[probe] user timer, ticks and unchanged PC OK' not in text: reason.append('user timer evidence missing')
            if name=='06_user_uart':
                for token in ['lab4-uart-A','lab4-uart-B','[probe] user external interrupt and unchanged PC OK']:
                    if token not in text: reason.append('missing '+token)
            if smp==2 and (text.count('cpu 0 is booting!')!=1 or text.count('cpu 1 is booting!')!=1): reason.append('SMP startup count')
        else:
            if isinstance(expected,tuple):
                cause,addr=expected
                token = {2:'Illegal instruction',12:'Instruction page fault',13:'Load page fault',15:'Store/AMO page fault'}[cause]
                if 'unexpected user exception: '+token not in text: reason.append('wrong user exception')
                # printf %x uses the kernel's own hexadecimal format; parse numerically.
                import re
                m=re.search(r'scause = (?:0x)?([0-9a-fA-F]+), sepc = (?:0x)?([0-9a-fA-F]+), stval = (?:0x)?([0-9a-fA-F]+)',text)
                if not m or int(m[1],16)!=cause or (addr is not None and int(m[3],16)!=addr): reason.append('wrong scause/stval')
            elif expected not in text: reason.append('wrong unknown syscall diagnosis')
            panic = 'panic! trap_user_handler' if isinstance(expected,tuple) else 'panic! trap_user_handler: unknown syscall'
            if panic not in text: reason.append('expected panic missing')
            if 'BUG' in text or 'TEST:' in text: reason.append('test assertion failed')
    except (OSError,RuntimeError,subprocess.SubprocessError) as e:
        reason.append(str(e))
    status = 'FAIL' if reason else 'PASS'
    with logpath.open('a',encoding='utf-8') as f:
        f.write('\n[check] '+status+' '+('; '.join(reason) if reason else 'all assertions satisfied')+'\n')
    print(f'{name:26} {status}',flush=True)
    return dict(id=name,category=category,status=status,reasons=reason,smp=smp)

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--case',action='append',default=[])
    ap.add_argument('--list',action='store_true')
    args=ap.parse_args()
    rows=cases()
    if args.list:
        for r in rows: print(r[0],r[1])
        return 0
    unknown=set(args.case)-{r[0] for r in rows}
    if unknown: ap.error('unknown case: '+','.join(sorted(unknown)))
    if args.case: rows=[r for r in rows if r[0] in args.case]
    run=ROOT/'tests/results'/time.strftime('%Y%m%d-%H%M%S')
    run.mkdir(parents=True,exist_ok=False)
    (ROOT/'tests/results/latest.txt').write_text(run.name,encoding='utf-8')
    results=[run_case(r,run) for r in rows]
    (run/'status.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8')
    summary='lab4 '+run.name+'\n'+'\n'.join(f"{r['id']:26} {r['status']}" for r in results)
    summary+=f"\nPASS={sum(r['status']=='PASS' for r in results)} FAIL={sum(r['status']=='FAIL' for r in results)}\n"
    (run/'summary.txt').write_text(summary,encoding='utf-8')
    print(summary)
    return int(any(r['status']=='FAIL' for r in results))
if __name__=='__main__': raise SystemExit(main())
