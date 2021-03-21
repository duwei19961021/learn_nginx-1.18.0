
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>


ngx_list_t *
ngx_list_create(ngx_pool_t *pool, ngx_uint_t n, size_t size)
{
    ngx_list_t  *list;

    list = ngx_palloc(pool, sizeof(ngx_list_t));    // 在small链表上给链表的管理结构(ngx_list_t)分配内存
    if (list == NULL) {
        return NULL;
    }

    if (ngx_list_init(list, pool, n, size) != NGX_OK) { // 初始化数据区域
        return NULL;
    }

    return list;
}


void *
ngx_list_push(ngx_list_t *l)
{
    void             *elt;
    ngx_list_part_t  *last;

    last = l->last;     // 拿到最后一个元素的指针，尾插效率高

    if (last->nelts == l->nalloc) { // 说明空间用完了，需要扩容

        /* the last part is full, allocate a new list part */

        last = ngx_palloc(l->pool, sizeof(ngx_list_part_t));    // 从内存池上给结点分配内存
        if (last == NULL) {
            return NULL;
        }

        last->elts = ngx_palloc(l->pool, l->nalloc * l->size);  // 从内存池上给新结点分配数据区的内存，每个结点的数据区域都是一样大
        if (last->elts == NULL) {
            return NULL;
        }

        last->nelts = 0;
        last->next = NULL;

        l->last->next = last;
        l->last = last;
    }

    elt = (char *) last->elts + l->size * last->nelts;  // 首地址加上已经使用的内存等于未使用内存的起始位置
    last->nelts++;

    return elt; // 返回分配的内存的首地址
}
