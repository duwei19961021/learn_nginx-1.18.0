
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>


// 创建临时缓冲区
ngx_buf_t *
ngx_create_temp_buf(ngx_pool_t *pool, size_t size)
{
    ngx_buf_t *b;       // typedef struct ngx_buf_s  ngx_buf_t

    // #define ngx_calloc_buf(pool) ngx_pcalloc(pool, sizeof(ngx_buf_t))
    // 在内存池上开辟内存，这里只是给ngx_buf_t的描述结构体开辟空间，这个函数没找到原型，应该是在缓冲链表上
    b = ngx_calloc_buf(pool);
    if (b == NULL) {
        return NULL;
    }

    // b->start描述的是缓冲区的起始地址
    // 这个函数会根据size决定缓冲区在哪里分配内存？大数据块链表,小数据块链表
    b->start = ngx_palloc(pool, size);
    if (b->start == NULL) {
        return NULL;
    }

    /*
     * set by ngx_calloc_buf():
     *
     *     b->file_pos = 0;
     *     b->file_last = 0;
     *     b->file = NULL;
     *     b->shadow = NULL;
     *     b->tag = 0;
     *     and flags
     */

    // pos描述 待处理数据的开始标记，缓冲区创建之后pos和start是同一位置
    b->pos = b->start;

    // last描述 待处理数据的结尾标记
    b->last = b->start;

    // end描述 缓冲区结尾的指针地址
    b->end = b->last + size;

    // 标志位，为1时，表示内存可修改
    b->temporary = 1;

    return b;
}


// 创建一个缓冲区的链表结构 结点
ngx_chain_t *
ngx_alloc_chain_link(ngx_pool_t *pool)
{
    ngx_chain_t  *cl;

    cl = pool->chain;   // 拿到内存池的缓冲区链表头结点

    /*
     * 缓冲区链表上保存的是 被清空了的 ngx_chain_t 结构，如果要申请缓冲区时先去缓冲区链表上取结点过来使用，
     * 避免想系统申请内存，达到重复使用内存的效果，
     * 如果取到了缓冲区链表上的节点则将这个节点从缓冲区链表上移除并将这个结点作为此函数的返回值
     */
    if (cl) {
        pool->chain = cl->next;
        return cl;
    }

    /*
    struct ngx_chain_s {
        ngx_buf_t    *buf;      // typedef struct ngx_buf_s  ngx_buf_t, 数据区域
        ngx_chain_t  *next;     // typedef struct ngx_chain_s ngx_chain_t, 指向下一个结点的指针
    };
     */

    /*
     * 走到这里也就是意味着没取到结点(暂时没有空闲结点)，于是便调用ngx_palloc申请内存，
     * ngx_palloc，再次强调这个函数，分配时有两种情况：size大于max则分配至大数据块内存链表上，否则分配至小数据块内存链表上。
     * 这里只是给ngx_chain_t结构分配内存，所以必然分配至小块数据链表上
     */
    cl = ngx_palloc(pool, sizeof(ngx_chain_t));
    if (cl == NULL) {
        return NULL;
    }

    // 分配完成之后将次结点返回给调用者使用
    return cl;
}


// 当需要一个较大的缓冲区时，此函数会创建多个buf(ngx_chain_t)结构并将它们以链表的形式串联起来
ngx_chain_t *
ngx_create_chain_of_bufs(ngx_pool_t *pool, ngx_bufs_t *bufs)
{
    /*
     * typedef struct {
     *      ngx_int_t    num;   // typedef intptr_t ngx_int_t;
     *      size_t       size;
     * } ngx_bufs_t;
     */
    u_char       *p;
    ngx_int_t     i;    // int型指针
    ngx_buf_t    *b;    // typedef struct ngx_buf_s  ngx_buf_t;
    ngx_chain_t  *chain, *cl, **ll;

    p = ngx_palloc(pool, bufs->num * bufs->size);
    /* 此时调用ngx_palloc分配的内存是存在于小数据块链表还是大数据块链表不太确定，取决于num*size的具体结果，
     * 但是我认为分配至小数据块链表上的概率偏高
     */
    if (p == NULL) {    // 检查是否分配成功
        return NULL;
    }

    ll = &chain;

    for (i = 0; i < bufs->num; i++) {

        b = ngx_calloc_buf(pool);   // 为buf的管理结构 ngx_int_t 分配内存
        if (b == NULL) {
            return NULL;
        }

        /*
         * set by ngx_calloc_buf():
         *
         *     b->file_pos = 0;
         *     b->file_last = 0;
         *     b->file = NULL;
         *     b->shadow = NULL;
         *     b->tag = 0;
         *     and flags
         *
         */

        b->pos = p;         // 待处理数据的始(pos)终(last)
        b->last = p;
        b->temporary = 1;   /* 标志位，为1时，内存可修改 */

        b->start = p;       /* 缓冲区开始的指针地址 */
        p += bufs->size;    // size是buf的大小，起点确定后，起点+size就是缓冲区结束的位置即end
        b->end = p;         /* 缓冲区结束的指针地址 */

        cl = ngx_alloc_chain_link(pool);    // 此函数功能上面已经写过不再赘述
        if (cl == NULL) {
            return NULL;
        }

        cl->buf = b;        // 申请到一个buf后将上面设置好了的buf管理结构的地址赋值给 cl的buf指针
        *ll = cl;           // ll = &chain; ll是缓冲区链表的头，cl是ngx_chain_t的指针，保存自然要用二级指针去保存
        ll = &cl->next;     // 链表的尾插，细品
    }

    *ll = NULL;

    return chain;           // 循环结束后返回整个链表的头结点
}


// 此函数见名知意：拷贝缓冲区链表
ngx_int_t
ngx_chain_add_copy(ngx_pool_t *pool, ngx_chain_t **chain, ngx_chain_t *in)
/*
 * chain 二级指针保存一个缓冲区链表的头结点的地址
 */
{
    ngx_chain_t  *cl, **ll;

    ll = chain; // ll此时保存的是链表头

    for (cl = *chain; cl; cl = cl->next) {  // cl解引用拿到头结点，开始遍历
        ll = &cl->next; // ->优先级大于&，所以这里的操作意思是：拿到chain链表的尾结点
    }

    /*
     * 遍历in链表
     */
    while (in) {
        cl = ngx_alloc_chain_link(pool);    // 创建一个缓冲区的链表结构 结点
        if (cl == NULL) {   // 永远都要检查是否分配成功
            *ll = NULL;
            return NGX_ERROR;
        }

        cl->buf = in->buf;  // 将cl的buf指向in的buf(浅拷贝)
        *ll = cl;           // ll是 指向链表尾巴结点的指针，将cl赋值给尾巴，也就是添加到chain链表的尾部
        ll = &cl->next;     // 链表继续往下走
        in = in->next;      // 被拷贝的链表也往下走，继续拷贝下一个结点
    }

    *ll = NULL;
    // 看到这里就能明白这个函数的功能了，将一个链表添加到另一个链表的末尾(但是不是直接连接两个链表哦)
    return NGX_OK;
}


// 通过函数名猜测，这个函数的功能应该是从缓冲区链表上获取空闲结点
ngx_chain_t *
ngx_chain_get_free_buf(ngx_pool_t *p, ngx_chain_t **free)
{
    ngx_chain_t  *cl;

    // 如果*free为空则必获取不到的呀
    // 不为空的话利用cl保存头结点，再将free指向链表的下一个结点，把cl(头结点)返回给调用者
    if (*free) {
        cl = *free;
        *free = cl->next;
        cl->next = NULL;
        return cl;
    }

    // 走到这里说明获取不到空闲结点，那就申请一个
    cl = ngx_alloc_chain_link(p);
    if (cl == NULL) {
        return NULL;
    }

    cl->buf = ngx_calloc_buf(p);    // 分配空间给buf
    // #define ngx_calloc_buf(pool) ngx_pcalloc(pool, sizeof(ngx_buf_t))
    if (cl->buf == NULL) {          // 给buf的管理结构分配内存ngx_buf_t
        return NULL;
    }

    cl->next = NULL;

    return cl;
}

void
ngx_chain_update_chains(ngx_pool_t *p, ngx_chain_t **free, ngx_chain_t **busy,
    ngx_chain_t **out, ngx_buf_tag_t tag)
{
    ngx_chain_t  *cl;

    if (*out) {                 // 如果out链不为空且busy链为空
        if (*busy == NULL) {    // 则将busy指向out
            *busy = *out;

        } else {                // 否则就是busy链不为空，则遍历busy链表，并将结点头插至out链表
            for (cl = *busy; cl->next; cl = cl->next) { /* void */ }

            cl->next = *out;
        }

        *out = NULL;
    }

    while (*busy) {             // 遍历busy链表
        cl = *busy;

    /*
    #define ngx_buf_in_memory(b) (b->temporary || b->memory || b->mmap) ，buf类型是其中一种则为真
    #define ngx_buf_size(b)  (ngx_buf_in_memory(b) ? (off_t) (b->last - b->pos):(b->file_last - b->file_pos))，计算buf的大小
    */
        if (ngx_buf_size(cl->buf) != 0) {   // 如果缓冲区不为空则不能释放
            break;
        }

        // 如果tag和函数传入的不相同
        if (cl->buf->tag != tag) {
            *busy = cl->next;
            ngx_free_chain(p, cl);  // 则将buf添加到内存池的缓冲区空闲链上
            continue;   // 继续遍历，不走下面的逻辑
        }
        // tag相同则调整缓冲区的始终两个指针为同一个位置
        cl->buf->pos = cl->buf->start;
        cl->buf->last = cl->buf->start;

        *busy = cl->next;
        cl->next = *free;   // 将reset之后的借点添加到空闲链表
        *free = cl;
    }
}

/*
#define ngx_free_chain(pool, cl)                                             \
    cl->next = pool->chain;                                                  \
    pool->chain = cl
*/

off_t
ngx_chain_coalesce_file(ngx_chain_t **in, off_t limit)  // coalesce 合并
{
    off_t         total, size, aligned, fprev;
    ngx_fd_t      fd;
    ngx_chain_t  *cl;

    total = 0;

    cl = *in;
    fd = cl->buf->file->fd;

    do {
        size = cl->buf->file_last - cl->buf->file_pos;

        if (size > limit - total) {
            size = limit - total;

            aligned = (cl->buf->file_pos + size + ngx_pagesize - 1)
                       & ~((off_t) ngx_pagesize - 1);

            if (aligned <= cl->buf->file_last) {
                size = aligned - cl->buf->file_pos;
            }

            total += size;
            break;
        }

        total += size;
        fprev = cl->buf->file_pos + size;
        cl = cl->next;

    } while (cl
             && cl->buf->in_file
             && total < limit
             && fd == cl->buf->file->fd
             && fprev == cl->buf->file_pos);

    *in = cl;

    return total;
}


ngx_chain_t *
ngx_chain_update_sent(ngx_chain_t *in, off_t sent)
{
    off_t  size;

    for ( /* void */ ; in; in = in->next) {

        if (ngx_buf_special(in->buf)) {
            continue;
        }

        if (sent == 0) {
            break;
        }

        size = ngx_buf_size(in->buf);

        if (sent >= size) {
            sent -= size;

            if (ngx_buf_in_memory(in->buf)) {
                in->buf->pos = in->buf->last;
            }

            if (in->buf->in_file) {
                in->buf->file_pos = in->buf->file_last;
            }

            continue;
        }

        if (ngx_buf_in_memory(in->buf)) {
            in->buf->pos += (size_t) sent;
        }

        if (in->buf->in_file) {
            in->buf->file_pos += sent;
        }

        break;
    }

    return in;
}
