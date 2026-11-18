#include "mod.h"

// mmap_region_node_t 仓库(单向链表) + 链表头节点(不可分配) + 保护仓库的自旋锁
static mmap_region_node_t node_list[N_MMAP];
static mmap_region_node_t list_head;
static spinlock_t list_lk;

// 初始化上述三个数据结构
void mmap_init()
{
    spinlock_init(&list_lk,"mmap_depot_lk");
    for(int i=0;i<N_MMAP-1;i++){
        node_list[i].next = &node_list[i+1];
    }
    node_list[N_MMAP-1].next=NULL;
    list_head.next=&node_list[0];
}

// 从仓库申请一个 mmap_region_t
// 若仓库空了则 panic
mmap_region_t *mmap_region_alloc()
{
    //获取自旋锁
    spinlock_acquire(&list_lk);
    if (list_head.next == NULL){

        spinlock_release(&list_lk);
        panic("mmap_region_alloc: depot is empty");

    }
    mmap_region_node_t *node = list_head.next;

    list_head.next = node->next;

    mmap_region_t *x = &node->mmap;
    //清空原节点
    node->mmap.begin = 0;
    node->mmap.npages = 0;
    node->mmap.next = NULL;
    //释放自旋锁
    spinlock_release(&list_lk);
    return x;

}
// 向仓库归还一个 mmap_region_t
void mmap_region_free(mmap_region_t *mmap)
{
    // 检查传入的指针是否为空
    if (mmap == NULL)
    {
        panic("mmap_region_free: NULL pointer");
    }

    // 获取仓库自旋锁
    spinlock_acquire(&list_lk);

    // 检查传入的 mmap 是否属于 node_list 仓库
    mmap_region_node_t *node = NULL;

    for (int i = 0; i < N_MMAP; i++)
    {
        if (&node_list[i].mmap == mmap)
        {
            node = &node_list[i];
            break;
        }
    }

    // 如果不属于仓库，拒绝释放
    if (node == NULL)
    {
        spinlock_release(&list_lk);
        panic("mmap_region_free: invalid mmap pointer");
    }

    // 遍历空闲链表，检查是否发生重复释放
    mmap_region_node_t *tmp = list_head.next;

    while (tmp != NULL)
    {
        if (tmp == node)
        {
            spinlock_release(&list_lk);
            panic("mmap_region_free: double free");
        }

        tmp = tmp->next;
    }

    // 将节点插入仓库空闲链表的头部
    node->next = list_head.next;
    list_head.next = node;

    // 释放仓库自旋锁
    spinlock_release(&list_lk);
}
// 输出可用的 mmap_region_node_t 链
// for debug
void mmap_show_nodelist()
{
    spinlock_acquire(&list_lk);

    mmap_region_node_t *tmp = list_head.next;
    int node = 0, index = 0;
    while (tmp)
    {
        index = tmp - &(node_list[0]);
        printf("node %d index = %d\n", node++, index);
        tmp = tmp->next;
    }

    spinlock_release(&list_lk);
}