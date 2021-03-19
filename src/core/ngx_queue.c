
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>


/*
 * find the middle queue element if the queue has odd number of elements
 * or the first element of the queue's second part otherwise
 */

ngx_queue_t *
ngx_queue_middle(ngx_queue_t *queue)
{
    ngx_queue_t  *middle, *next;

    middle = ngx_queue_head(queue);         //头结点的下一个结点

    if (middle == ngx_queue_last(queue)) {  // 如果middle等于头结点的上一个结点，则链表只有两个元素 head 和 mid
        return middle;
    }

    next = ngx_queue_head(queue);           // next保存链表的头结点之后的结点

    for ( ;; ) {
        middle = ngx_queue_next(middle);

        next = ngx_queue_next(next);
        // 走到这里 middle、next都指向第二个有效结点

        if (next == ngx_queue_last(queue)) {    // 当next走到末尾元素时，mid才走一半，所以mid是中间元素
            return middle;
        }

        next = ngx_queue_next(next);    // next走的速度是middle的两倍

        if (next == ngx_queue_last(queue)) {
            return middle;
        }
    }
}


/* the stable insertion sort */
// 链表排序
void
ngx_queue_sort(ngx_queue_t *queue,
    ngx_int_t (*cmp)(const ngx_queue_t *, const ngx_queue_t *))
{
    ngx_queue_t  *q, *prev, *next;

    q = ngx_queue_head(queue);

    if (q == ngx_queue_last(queue)) {
        return;
    }

    for (q = ngx_queue_next(q); q != ngx_queue_sentinel(queue); q = next) {

        prev = ngx_queue_prev(q);
        next = ngx_queue_next(q);

        ngx_queue_remove(q);

        do {
            if (cmp(prev, q) <= 0) {
                break;
            }

            prev = ngx_queue_prev(prev);

        } while (prev != ngx_queue_sentinel(queue));

        ngx_queue_insert_after(prev, q);
    }
}
