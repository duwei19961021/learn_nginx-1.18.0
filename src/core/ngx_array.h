
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_ARRAY_H_INCLUDED_
#define _NGX_ARRAY_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


typedef struct {
    void        *elts;      // 指向第一个元素的指针
    ngx_uint_t   nelts;     // 未使用的元素的索引
    size_t       size;      // 每个元素大小
    ngx_uint_t   nalloc;    //  元素个数
    ngx_pool_t  *pool;      //  内存池
} ngx_array_t;


ngx_array_t *ngx_array_create(ngx_pool_t *p, ngx_uint_t n, size_t size);    // 在内存池上申请内存，n个size大小的元素的数组
void ngx_array_destroy(ngx_array_t *a); // 销毁数组
void *ngx_array_push(ngx_array_t *a);   //
void *ngx_array_push_n(ngx_array_t *a, ngx_uint_t n);


static ngx_inline ngx_int_t // ngx_array_create会调用此函数，申请之后进行初始化操作
ngx_array_init(ngx_array_t *array, ngx_pool_t *pool, ngx_uint_t n, size_t size)
{
    /*
     * set "array->nelts" before "array->elts", otherwise MSVC thinks
     * that "array->nelts" may be used without having been initialized
     */

    array->nelts = 0;
    array->size = size;
    array->nalloc = n;
    array->pool = pool;

    array->elts = ngx_palloc(pool, n * size);   // 去内存池上分配内存
    if (array->elts == NULL) {
        return NGX_ERROR;
    }

    return NGX_OK;
}


#endif /* _NGX_ARRAY_H_INCLUDED_ */
