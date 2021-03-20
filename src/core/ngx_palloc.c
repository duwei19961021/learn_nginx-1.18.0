
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>


static ngx_inline void *ngx_palloc_small(ngx_pool_t *pool, size_t size,
    ngx_uint_t align);
static void *ngx_palloc_block(ngx_pool_t *pool, size_t size);
static void *ngx_palloc_large(ngx_pool_t *pool, size_t size);


ngx_pool_t *
ngx_create_pool(size_t size, ngx_log_t *log)
{
    ngx_pool_t  *p;

    p = ngx_memalign(NGX_POOL_ALIGNMENT, size, log);
    /*
        #define ngx_memalign(alignment, size, log)  ngx_alloc(size, log)，
        而ngx_alloc实际上是去调用malloc
    */

    if (p == NULL) {
        return NULL;
    }

    p->d.last = (u_char *) p + sizeof(ngx_pool_t);
    /*
        p是申请得到的内存的首地址，前sizeof(ngx_pool_t)字节的内存要留给内存池的管理结构(我也叫他控制中心)，
        控制中心之后的内存是给申请者使用的。last是申请者能够使用内存的起始位置，end是结束位置(由数据结构得知)
    */

    p->d.end = (u_char *) p + size;
    /*
        end指向申请得到的内存的末尾位置，
        p是首地址，加上size就是结束地址即end的指向位置，
        内存的申请者总共申请了size个字节的内存，能够使用的内存为：size - sizeof(ngx_pool_t)
    */

    p->d.next = NULL;
    /*
        内存池通过指针相连，组成一个链表，下一个结点在创建时应当指向NULL
    */

    p->d.failed = 0;

    size = size - sizeof(ngx_pool_t);
    p->max = (size < NGX_MAX_ALLOC_FROM_POOL) ? size : NGX_MAX_ALLOC_FROM_POOL;
    /*
   NGX_MAX_ALLOC_FROM_POOL should be (ngx_pagesize - 1), i.e. 4095 on x86.
   On Windows NT it decreases a number of locked pages in a kernel.

   max取size和操作系统弄内存页二者中较小的值
   */

    p->current = p;
    p->chain = NULL;
    p->large = NULL;
    p->cleanup = NULL;
    p->log = log;
    /*
        只有缓存池的父节点，才会用到这几个成员  ，子节点只挂载在p->d.next,并且只负责p->d的数据内容
    */

    return p;
}

void
ngx_destroy_pool(ngx_pool_t *pool)
{
    ngx_pool_t          *p, *n;
    ngx_pool_large_t    *l;
    ngx_pool_cleanup_t  *c;

    for (c = pool->cleanup; c; c = c->next) {
        if (c->handler) {
            ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, pool->log, 0,
                           "run cleanup: %p", c);
            c->handler(c->data);
        }
    }
    /*
        只有父结点才有cleanup链表，
        cleanup链表的结点结构有三个成员：清理函数handler、指向存储数据的data、指向下一个结点的next
        这里遍历cleanup链表，调用handler清理data
    */

#if (NGX_DEBUG)	// 编译debug级别，如果为true，会打印日志

    /*
     * we could allocate the pool->log from this pool
     * so we cannot use this log while free()ing the pool
     */

    for (l = pool->large; l; l = l->next) {
        ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, pool->log, 0, "free: %p", l->alloc);
    }

    for (p = pool, n = pool->d.next; /* void */; p = n, n = n->d.next) {
        ngx_log_debug2(NGX_LOG_DEBUG_ALLOC, pool->log, 0,
                       "free: %p, unused: %uz", p, p->d.end - p->d.last);

        if (n == NULL) {
            break;
        }
    }

#endif

    for (l = pool->large; l; l = l->next) {
        if (l->alloc) {
            ngx_free(l->alloc);
        }
    }
    /*
        只有父结点才有large链表，
        遍历large(大块数据链表)链表，释放alloc指针指向的存储数据的内存
        #define ngx_free          free
        由此可知ngx_free是个宏，底层调用的还是free，这里直接将大块数据内存还给了操作系统
    */

    for (p = pool, n = pool->d.next; /* void */; p = n, n = n->d.next) {
        ngx_free(p);
        /*
            至此，父结点上的large链表以及cleanup链表都被清理了，然后循环清理内存池结点，
            小块数据内存是分配在内存池结点上的，和其控制中心(ngx_pool_t)是连续的，
            释放时直接释放当前结点就行了
        */

        /*
            至此清理工作结束。
            但是有个小疑问：chain(缓冲区)链表为啥没有被清理？
        */

        if (n == NULL) {
            break;
        }
    }
}

void
ngx_reset_pool(ngx_pool_t *pool)
{
    ngx_pool_t        *p;
    ngx_pool_large_t  *l;

    for (l = pool->large; l; l = l->next) {
        if (l->alloc) {
            ngx_free(l->alloc);
        }
    }
    /*
        清理large链表
    */

    for (p = pool; p; p = p->d.next) {
        p->d.last = (u_char *) p + sizeof(ngx_pool_t);
        p->d.failed = 0;
    }
    /*
        重置小数据块区域的内存，注意这里是修改last的指向，将last重新指向起始位置，
        内存并没有被擦除，因为是小数据块内存，这里没有将其归还给操作系统，
        目的是避免频繁的malloc和free产生内存碎片问题，频繁调用这两个函数也会给
        操作系统带来额外的消耗。
    */

    pool->current = pool;
    pool->chain = NULL;
    pool->large = NULL;
}

void *
ngx_palloc(ngx_pool_t *pool, size_t size)
{
#if !(NGX_DEBUG_PALLOC)
    /*
        这里是个条件编译，如果没开启ngx palloc debug就会根据size决定调用ngx_palloc_small还是   					   	ngx_palloc_large，如果开启了debug则一律调用ngx_palloc_large
    */

    if (size <= pool->max) {
        return ngx_palloc_small(pool, size, 1);
    }
#endif

    return ngx_palloc_large(pool, size);
}

void *
ngx_pnalloc(ngx_pool_t *pool, size_t size)
{
#if !(NGX_DEBUG_PALLOC)
    if (size <= pool->max) {
        return ngx_palloc_small(pool, size, 0);
    }
#endif

    return ngx_palloc_large(pool, size);
}


static ngx_inline void *
ngx_palloc_small(ngx_pool_t *pool, size_t size, ngx_uint_t align)
{
    u_char      *m;
    ngx_pool_t  *p;

    p = pool->current;

    do {
        m = p->d.last;
        /*
            m保存了last的指向，last <-> end 之间是未被使用的内存
        */

        if (align) {
            m = ngx_align_ptr(m, NGX_ALIGNMENT);
        }
        /*
            align，暂时不懂是啥意思
        */

        if ((size_t) (p->d.end - m) >= size) {
            p->d.last = m + size;

            return m;
        }
        /*
            如果end到m(即last)之间的字节数大于size，那么这个内存池结点上的足够申请者使用，
            则将last后移size个字节并返回之前last的指向的内存的地址供调用者使用
        */

        p = p->d.next;

    } while (p);

    return ngx_palloc_block(pool, size);
    /*
        走到这里说明内存池的所有结点上都没有足够的空间分配出去，
        此时则调用ngx_palloc_block新开一个内存池的节点(扩容)
    */
}


static void *
ngx_palloc_block(ngx_pool_t *pool, size_t size)
{
    u_char      *m;
    size_t       psize;
    ngx_pool_t  *p, *new;

    psize = (size_t) (pool->d.end - (u_char *) pool);
    /*
        要新创建的内存池的结点的size是根据头结点的size创建的
    */

    m = ngx_memalign(NGX_POOL_ALIGNMENT, psize, pool->log);
    /*
        申请psize大小的内存块
    */

    if (m == NULL) {
        return NULL;
    }

    new = (ngx_pool_t *) m;
    /*
        类型转换
    */

    new->d.end = m + psize;
    /*
        end指向末尾
    */

    new->d.next = NULL;
    /*
        下一个结点指针 指向NULL避免野指针
    */

    new->d.failed = 0;

    m += sizeof(ngx_pool_data_t);
    /*
        前sizeof(ngx_pool_data_t)个字节留给ngx_pool_data_t结构使用，
        这里用的比较巧妙，细品细品，此时有点惊叹设计者的做法。
        因为创建的是子节点，所以max、current指针、缓冲区链表指针、large链表指针、
        cleanup链表指针，这几个成员所占的内存都可以分配出去给调用者使用(子结点用不到这几个成员)，
        避免了空间浪费。
    */

    m = ngx_align_ptr(m, NGX_ALIGNMENT);
    new->d.last = m + size;
    /*
        移动last size个字节，标识这段内存已经被分配出去了
    */

    for (p = pool->current; p->d.next; p = p->d.next) {
        if (p->d.failed++ > 4) {
            pool->current = p->d.next;
        }
    }
    /*
        每遍历一个结点failed++，当failed超过了4时，current会指向新子结点，
        这么做可以避免遍历整个链表(如果链表足够长，遍历一次效率比较低)
    */

    p->d.next = new;

    return m;
}

static void *
ngx_palloc_large(ngx_pool_t *pool, size_t size)
{
    void              *p;
    ngx_uint_t         n;
    ngx_pool_large_t  *large;

    p = ngx_alloc(size, pool->log);
    /*
        底层调用malloc分配内存
    */

    if (p == NULL) {
        return NULL;
    }

    n = 0;

    for (large = pool->large; large; large = large->next) {
        if (large->alloc == NULL) {
            large->alloc = p;
            return p;
        }
        /*
            遍历large链表，寻找空闲结点，找到了则将large链表上的空闲结点的alloc指向刚刚申请的内存p
        */

        if (n++ > 3) {
            break;
        }
        /*
            如果找了三次还没找到那就不找了，避免链表过长时查找效率低
        */

    }

    large = ngx_palloc_small(pool, sizeof(ngx_pool_large_t), 1);
    /*
        在内存池结点的小块数据内存上给large的控制中心结构ngx_pool_large_t分配一块内存
    */

    if (large == NULL) {
        ngx_free(p);
        /*
            如果large分配失败p是要释放的，避免内存泄露
        */
        return NULL;
    }

    large->alloc = p;
    large->next = pool->large;
    pool->large = large;
    /*
        新开的large结点变为large链表的头结点。
    */

    return p;
}

void *
ngx_pmemalign(ngx_pool_t *pool, size_t size, size_t alignment)
{
    void              *p;
    ngx_pool_large_t  *large;

    p = ngx_memalign(alignment, size, pool->log);
    if (p == NULL) {
        return NULL;
    }

    large = ngx_palloc_small(pool, sizeof(ngx_pool_large_t), 1);
    if (large == NULL) {
        ngx_free(p);
        return NULL;
    }

    large->alloc = p;
    large->next = pool->large;
    pool->large = large;

    return p;
}


ngx_int_t
ngx_pfree(ngx_pool_t *pool, void *p)
{
    ngx_pool_large_t  *l;

    for (l = pool->large; l; l = l->next) {
        if (p == l->alloc) {
            ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, pool->log, 0,
                           "free: %p", l->alloc);
            ngx_free(l->alloc);
            l->alloc = NULL;

            return NGX_OK;
        }
    }

    return NGX_DECLINED;
}


void *
ngx_pcalloc(ngx_pool_t *pool, size_t size)
{
    void *p;

    p = ngx_palloc(pool, size);
    if (p) {
        ngx_memzero(p, size);
    }

    return p;
}


ngx_pool_cleanup_t *
ngx_pool_cleanup_add(ngx_pool_t *p, size_t size)
{
    ngx_pool_cleanup_t  *c;

    c = ngx_palloc(p, sizeof(ngx_pool_cleanup_t));
    /*
        给cleanup的控制中心结构分配内存，分配在内存池小块数据内存上的。
    */

    if (c == NULL) {
        return NULL;
    }

    if (size) {
        c->data = ngx_palloc(p, size);
        if (c->data == NULL) {
            return NULL;
        }

    } else {
        c->data = NULL;
    }
    /*
        根据size决定是否分配数据区，数据区分配的内存在哪据size的大小而定，
        可能在小数据块内存上也可能在large链表上
    */

    c->handler = NULL;
    c->next = p->cleanup;

    p->cleanup = c;
    /*
        头插进cleanup链表
    */

    ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, p->log, 0, "add cleanup: %p", c);

    return c;
}


void
ngx_pool_run_cleanup_file(ngx_pool_t *p, ngx_fd_t fd)
{
    ngx_pool_cleanup_t       *c;
    ngx_pool_cleanup_file_t  *cf;

    for (c = p->cleanup; c; c = c->next) {
        if (c->handler == ngx_pool_cleanup_file) {

            cf = c->data;

            if (cf->fd == fd) {
                c->handler(cf);
                c->handler = NULL;
                return;
            }
        }
    }
}


void
ngx_pool_cleanup_file(void *data)
{
    ngx_pool_cleanup_file_t  *c = data;

    ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, c->log, 0, "file cleanup: fd:%d",
                   c->fd);

    if (ngx_close_file(c->fd) == NGX_FILE_ERROR) {
        ngx_log_error(NGX_LOG_ALERT, c->log, ngx_errno,
                      ngx_close_file_n " \"%s\" failed", c->name);
    }
}


void
ngx_pool_delete_file(void *data)
{
    ngx_pool_cleanup_file_t  *c = data;

    ngx_err_t  err;

    ngx_log_debug2(NGX_LOG_DEBUG_ALLOC, c->log, 0, "file cleanup: fd:%d %s",
                   c->fd, c->name);

    if (ngx_delete_file(c->name) == NGX_FILE_ERROR) {
        err = ngx_errno;

        if (err != NGX_ENOENT) {
            ngx_log_error(NGX_LOG_CRIT, c->log, err,
                          ngx_delete_file_n " \"%s\" failed", c->name);
        }
    }

    if (ngx_close_file(c->fd) == NGX_FILE_ERROR) {
        ngx_log_error(NGX_LOG_ALERT, c->log, ngx_errno,
                      ngx_close_file_n " \"%s\" failed", c->name);
    }
}


#if 0

static void *
ngx_get_cached_block(size_t size)
{
    void                     *p;
    ngx_cached_block_slot_t  *slot;

    if (ngx_cycle->cache == NULL) {
        return NULL;
    }

    slot = &ngx_cycle->cache[(size + ngx_pagesize - 1) / ngx_pagesize];

    slot->tries++;

    if (slot->number) {
        p = slot->block;
        slot->block = slot->block->next;
        slot->number--;
        return p;
    }

    return NULL;
}

#endif
