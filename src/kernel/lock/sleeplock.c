#include "mod.h"
#include "../proc/mod.h"
// 睡眠锁初始化
void sleeplock_init(sleeplock_t *lk, char *name)
{
    spinlock_init(&(lk->lock),"sleeplk_spinlock");
    lk->name=name;
    lk->locked=0;
    lk->pid=-1;
}

// 检查当前进程是否持有睡眠锁
bool sleeplock_holding(sleeplock_t *lk)
{
    spinlock_acquire(&lk->lock);
    if(lk->locked && myproc()->pid==lk->pid){
        spinlock_release(&lk->lock);
        return true;
    }else{
        spinlock_release(&lk->lock);
        return false;
    }
    
}

// 当前进程尝试获取睡眠锁, 失败进入睡眠状态
void sleeplock_acquire(sleeplock_t *lk)
{
    // 获取内部自旋锁
    spinlock_acquire(&lk->lock);

    while (lk->locked)
    {
        // 当前进程进入睡眠
        proc_sleep(lk, &lk->lock);
    }
    // 成功获得睡眠锁
    lk->locked=1;

    lk->pid=myproc()->pid;
    // 释放内部自旋锁
    spinlock_release(&lk->lock);
}

// 释放睡眠锁, 唤醒其他等待睡眠锁的进程
void sleeplock_release(sleeplock_t *lk)
{
    // 获取内部自旋锁
    spinlock_acquire(&lk->lock);
    lk->locked=0;
    lk->pid=-1;
    proc_wakeup(lk);
    spinlock_release(&lk->lock);
}