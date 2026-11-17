#include "mod.h"

/*
    测试: 从用户空间传入一个int类型的数组
    uint64 addr 数组起始地址
    uint32 len  元素数量
    成功返回0
*/
uint64 sys_copyin()
{
    //获取用户传入的参数
    uint32 len=0;
    uint64 addr=0;
    arg_uint64(0,&addr);
    arg_uint32(1,&len);

    //检查数组长度
    assert(len > 0 && len <= 5,
           "sys_copyin: invalid array length");

    //在当前进程的内核栈上创建缓冲区
    int buf[5];

    //获取当前进程
    proc_t *p = myproc();
    //将用户数组复制到内核缓冲区
    uvm_copyin(
        p->pgtbl,
        (uint64)buf,
        addr,
        len * sizeof(int)
    );

    //输出接收到的数据
    printf("sys_copyin: ");

    for (uint32 i = 0; i < len; i++)
    {
        printf("%d ", buf[i]);
    }

    printf("\n");

    return 0;
}

/*
    测试: 向用户空间传出一个int类型的数组
    uint64 addr 数组起始地址
    成功返回拷贝的元素数量
*/
uint64 sys_copyout()
{
    //获取用户传入的目标地址（a0）
    uint64 addr = 0;
    arg_uint64(0, &addr);

    //在内核栈上创建测试数组
    int buf[5] = {1, 2, 3, 4, 5};

    //获取当前进程
    proc_t *p = myproc();

    //将内核数组复制到用户地址空间
    uvm_copyout(
        p->pgtbl,
        addr,
        (uint64)buf,
        sizeof(buf)
    );

    //返回复制的元素数量
    return 5;
}

/*
    测试: 从用户空间传入一个字符串
    uint64 addr 字符串起始地址
    成功返回0
*/
uint64 sys_copyinstr()
{
    //在内核栈上创建字符串缓冲区
    char buf[STR_MAXLEN];

    //获取用户传入的字符串地址（a0）
    //并将用户字符串复制到内核缓冲区
    arg_str(0, buf, STR_MAXLEN);

    //输出复制得到的字符串
    printf("sys_copyinstr: %s\n", buf);

    return 0;
}

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
    return 0;
}

/*
    解除一段内存映射
    uint64 start 起始地址
    uint32 len   范围 (字节, 需检查是否是page-aligned)
    成功返回0 失败返回-1
*/
uint64 sys_munmap()
{
    return 0;
}