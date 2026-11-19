#include "mod.h"

/*
    用户堆空间伸缩
    uint64 new_heap_top (如果是0, 代表查询当前堆顶位置)
    成功返回new_heap_top, 失败返回-1
*/
uint64 sys_brk()
{
    //获取用户通过 a0 传入的新堆顶
    uint64 new_heap_top = 0;
    arg_uint64(0, &new_heap_top);

    //获取当前进程
    proc_t *p = myproc();

    //记录当前堆顶
    uint64 old_heap_top = p->heap_top;

    //如果传入 0，直接返回当前堆顶
    if (new_heap_top == 0)
    {
        printf("sys_brk: query heap_top = %x\n", old_heap_top);
        return old_heap_top;
    }

    //检查新堆顶是否位于合法范围
    if (new_heap_top < USER_BASE + PGSIZE ||
        new_heap_top > MMAP_BEGIN)
    {
        return (uint64)-1;
    }

    //如果新旧堆顶相同，不需要操作
    if (new_heap_top == old_heap_top)
    {
        return old_heap_top;
    }

    //根据新旧堆顶的大小关系处理
    if (new_heap_top > old_heap_top)
    {
        // 堆增长
        uint64 len = new_heap_top - old_heap_top;

        // 底层函数使用 uint32 表示长度
        if (len > 0xFFFFFFFFULL)
        {
            return (uint64)-1;
        }

        p->heap_top = uvm_heap_grow(
            p->pgtbl,
            old_heap_top,
            (uint32)len
        );
    }
    else
    {
        // 堆收缩
        uint64 len = old_heap_top - new_heap_top;

        if (len > 0xFFFFFFFFULL)
        {
            return (uint64)-1;
        }

        p->heap_top = uvm_heap_ungrow(
            p->pgtbl,
            old_heap_top,
            (uint32)len
        );
    }

    //输出调试信息
    printf(
        "sys_brk: heap_top %x -> %x\n",
        old_heap_top,
        p->heap_top
    );

    //返回新的堆顶
    return p->heap_top;
}

/*
    增加一段内存映射
    uint64 start 起始地址
    uint32 len   范围 (字节,需检查是否是page-aligned)
    成功返回映射空间的起始地址, 失败返回-1
*/
uint64 sys_mmap()
{
    // 获取用户传入的参数
    uint64 begin = 0;
    uint32 len = 0;

    arg_uint64(0, &begin);
    arg_uint32(1, &len);

    // 检查申请长度是否合法
    if (len == 0 || len % PGSIZE != 0)
    {
        return (uint64)-1;
    }

    // 如果用户指定了起始地址，需要检查地址是否合法
    if (begin != 0)
    {
        if (begin % PGSIZE != 0 ||
            begin < MMAP_BEGIN ||
            begin >= MMAP_END ||
            (uint64)len > MMAP_END - begin)
        {
            return (uint64)-1;
        }
    }

    // 将字节数转换为页面数量
    uint32 npages = len / PGSIZE;

    // 调用用户虚拟内存管理函数
    uint64 actual_begin = uvm_mmap(
        begin,
        npages,
        PTE_R | PTE_W
    );

    // 输出本次 mmap 的结果
    printf(
        "sys_mmap: begin = %x, len = %d\n",
        actual_begin,
        (int)len
    );

    // 输出当前进程的 mmap 链表和页表
    proc_t *p = myproc();

    uvm_show_mmaplist(p->mmap);
    vm_print(p->pgtbl);
    printf("\n");

    // 返回实际分配的起始地址
    return actual_begin;
}
/*
    解除一段内存映射
    uint64 start 起始地址
    uint32 len   范围 (字节, 需检查是否是page-aligned)
    成功返回0 失败返回-1
*/
/*
    解除一段内存映射
    uint64 start 起始地址
    uint32 len   范围（字节，需检查是否是page-aligned）
    成功返回0，失败返回-1
*/
uint64 sys_munmap()
{
    // 获取用户传入的参数
    uint64 begin = 0;
    uint32 len = 0;

    arg_uint64(0, &begin);
    arg_uint32(1, &len);

    // 检查释放长度是否合法
    if (len == 0 || len % PGSIZE != 0)
    {
        return (uint64)-1;
    }

    // 检查释放地址及范围是否合法
    if (begin % PGSIZE != 0 ||
        begin < MMAP_BEGIN ||
        begin >= MMAP_END ||
        (uint64)len > MMAP_END - begin)
    {
        return (uint64)-1;
    }

    // 将字节数转换为页面数量
    uint32 npages = len / PGSIZE;

    // 解除映射并释放对应物理页
    uvm_munmap(begin, npages);

    // 输出本次 munmap 的结果
    printf(
        "sys_munmap: begin = %x, len = %d\n",
        begin,
        (int)len
    );

    // 输出当前进程的 mmap 链表和页表
    proc_t *p = myproc();

    uvm_show_mmaplist(p->mmap);
    vm_print(p->pgtbl);
    printf("\n");

    // 释放成功
    return 0;
}


/*
    打印一个字符串
    char *str
    成功返回0
*/
uint64 sys_print_str()
{

}

/*
    打印一个32位整数
    int num
    成功返回0
*/
uint64 sys_print_int()
{

}

/*
    进程复制
    返回子进程的pid
*/
uint64 sys_fork()
{

}

/*
    等待子进程退出
    uint64 addr_exit_state
*/
uint64 sys_wait()
{

}

/*
    进程退出
    int exit_code
    不返回
*/
uint64 sys_exit()
{

}

/*
    让进程睡眠一段时间
    uint32 ntick (1个tick大约0.1秒)
    成功返回0
*/
uint64 sys_sleep()
{

}

/*
    返回当前进程的pid
*/
uint64 sys_getpid()
{

}