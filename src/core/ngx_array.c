
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>


ngx_array_t *
ngx_array_create(ngx_pool_t *p, ngx_uint_t n, size_t size)
{
    ngx_array_t *a;

    a = ngx_palloc(p, sizeof(ngx_array_t)); // 在内存池上为ngx_array_t结构体申请内存
    if (a == NULL) {
        return NULL;
    }

    if (ngx_array_init(a, p, n, size) != NGX_OK) {  // 在紧跟着ngx_array_t结构体所在内存之后的位置初始化数组
        return NULL;
    }

    return a;
}


void
ngx_array_destroy(ngx_array_t *a)
{
    /*
     * 在 ngx_array_create() 函数中 ngx_array_t结构的内存和数组的内存是分开申请的，不一定连续，
     * 所以在将内存归还给内存池的时候也得分开归还
     */
    ngx_pool_t  *p;

    p = a->pool;    // 内存池的地址是被保存在ngx数组数据结构中的

    /*
     * 如果 数组首元素地址 + 元素大小*元素个数 == last的指向位置，
     * 则说明这个数组的内存段是在used区域的尾部的，last直接减去 元素大小*元素个数 就代表销毁了这个数组的内存
     */
    if ((u_char *) a->elts + a->size * a->nalloc == p->d.last) {
        p->d.last -= a->size * a->nalloc;
    }

    /*
     *  道理和上面类似
     */
    if ((u_char *) a + sizeof(ngx_array_t) == p->d.last) {
        p->d.last = (u_char *) a;
    }

    // 否则就不能销毁
}

void *
ngx_array_push(ngx_array_t *a)  // 添加1个元素
{
    void        *elt, *new;
    size_t       size;
    ngx_pool_t  *p;

    if (a->nelts == a->nalloc) {

        /* the array is full */

        size = a->size * a->nalloc; // 计算当前数组的大小

        p = a->pool;

        if ((u_char *) a->elts + size == p->d.last
            && p->d.last + a->size <= p->d.end)
        /*
         * 如果elts指向的位置加上当前数组的大小等于last 且 last到end之间 有足够的空间去容纳一个元素则直接在这个内存池上添加元素
         * 这个判定是要保证添加的元素所在的内存与之前数组的内存是连续的
         */
        {
            /*
             * the array allocation is the last in the pool
             * and there is space for new allocation
             */

            p->d.last += a->size;
            a->nalloc++;

        } else {
            // 否则就是不能保证内存连续或者空间不足
            /* allocate a new array */

            // 申请一个两倍的空间，new是首地址
            new = ngx_palloc(p, 2 * size);
            if (new == NULL) {
                return NULL;
            }

            // #define ngx_memcpy(dst, src, n)   (void) memcpy(dst, src, n) 看到这个👴🏻想笑
            ngx_memcpy(new, a->elts, size); // 将原来的内存地址上的数组部分的内容拷贝到新内存池上
            a->elts = new;  // 指向新内存的位置
            a->nalloc *= 2; // 元素个数翻倍
        }
    }

    // 未使用的索引
    elt = (u_char *) a->elts + a->size * a->nelts;
    a->nelts++;

    return elt;
}


void *
ngx_array_push_n(ngx_array_t *a, ngx_uint_t n)
{
    void        *elt, *new;
    size_t       size;
    ngx_uint_t   nalloc;
    ngx_pool_t  *p;

    size = n * a->size;

    if (a->nelts + n > a->nalloc) {

        /* the array is full */

        p = a->pool;

        /*
         * 如果elts指向的位置加上当前数组的大小等于last 且 last到end之间 有足够的空间去容纳n个元素则直接在这个内存池上添加元素
         * 这个判定是要保证添加的元素所在的内存与之前数组的内存是连续的
         */
        if ((u_char *) a->elts + a->size * a->nalloc == p->d.last
            && p->d.last + size <= p->d.end)
        {
            /*
             * the array allocation is the last in the pool
             * and there is space for new allocation
             */

            p->d.last += size;  // size = n * a->size; 调整last的指向
            a->nalloc += n;     // 更新元素个数

        } else {    // 否则扩容
            /* allocate a new array */

            nalloc = 2 * ((n >= a->nalloc) ? n : a->nalloc);

            new = ngx_palloc(p, nalloc * a->size);
            if (new == NULL) {
                return NULL;
            }

            ngx_memcpy(new, a->elts, a->nelts * a->size);
            a->elts = new;
            a->nalloc = nalloc;
        }
    }

    elt = (u_char *) a->elts + a->size * a->nelts;
    a->nelts += n;

    return elt;
}
