
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>


ngx_buf_t *
ngx_create_temp_buf(ngx_pool_t *pool, size_t size)
{
    ngx_buf_t *b;
    // 分配一个ngx_buf_t结构体，用于存储buf元数据
    b = ngx_calloc_buf(pool);
    if (b == NULL) {
        return NULL;
    }
    // 分配一块内存用于存储数据
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
    // 初始化位置指向
    b->pos = b->start;
    b->last = b->start;
    b->end = b->last + size;
    b->temporary = 1;

    return b;
}

// 分配一个ngx_chain_t结构体
ngx_chain_t *
ngx_alloc_chain_link(ngx_pool_t *pool)
{
    ngx_chain_t  *cl;

    cl = pool->chain;
    // 如果还有可用的，直接返回，并更新链表指针
    if (cl) {
        pool->chain = cl->next;
        return cl;
    }
    // 否则分配一个新的
    cl = ngx_palloc(pool, sizeof(ngx_chain_t));
    if (cl == NULL) {
        return NULL;
    }

    return cl;
}

// https://www.processon.com/view/link/5edde50b1e085375de8258fe
ngx_chain_t *
ngx_create_chain_of_bufs(ngx_pool_t *pool, ngx_bufs_t *bufs)
{
    u_char       *p;
    ngx_int_t     i;
    ngx_buf_t    *b;
    ngx_chain_t  *chain, *cl, **ll;
    // 分配num个buf，每个大小为size的内存，然后用ngx_buf_t结构体和ngx_chain_t管理
    p = ngx_palloc(pool, bufs->num * bufs->size);
    if (p == NULL) {
        return NULL;
    }
    // 二级指针指向一级指针的地址，*ll的时候可以修改一级指针变量的内容
    ll = &chain;
    // 每次循环分配一个ngx_bufs_t
    for (i = 0; i < bufs->num; i++) {
        // 分配一个ngx_buf_t
        b = ngx_calloc_buf(pool);
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
        // 设置位置
        b->pos = p;
        b->last = p;
        b->temporary = 1;

        b->start = p;
        // 更新p的位置，指向下一个ngx_bufs_t首地址
        p += bufs->size;
        b->end = p;
        // 分配一个ngx_chain_t
        cl = ngx_alloc_chain_link(pool);
        if (cl == NULL) {
            return NULL;
        }
        // ngx_chain_t指向ngx_bufs_t
        cl->buf = b;
        // 连成一条ngx_chain_t链表
        *ll = cl;
        // 指向next指针的地址，准备修改他的内容
        ll = &cl->next;
    }

    *ll = NULL;

    return chain;
}

// 把in链表的数据复制（追加）到chain链表（共享ngx_bufs_t）
ngx_int_t
ngx_chain_add_copy(ngx_pool_t *pool, ngx_chain_t **chain, ngx_chain_t *in)
{
    ngx_chain_t  *cl, **ll;

    ll = chain;
    // 当cl->next为NULL的时候，ll指向cl的next域的地址
    for (cl = *chain; cl; cl = cl->next) {
        ll = &cl->next;
    }

    while (in) {
        // 分配一个ngx_chain_t
        cl = ngx_alloc_chain_link(pool);
        if (cl == NULL) {
            *ll = NULL;
            return NGX_ERROR;
        }
        // 共享一个ngx_bufs_t
        cl->buf = in->buf;
        *ll = cl;
        // 指向chain链表最后一个节点的next域的地址
        ll = &cl->next;
        // 准备复制下一个
        in = in->next;
    }

    *ll = NULL;

    return NGX_OK;
}

// 从free链表中摘下一个空闲的ngx_chain_t节点，如果没有则申请一个
ngx_chain_t *
ngx_chain_get_free_buf(ngx_pool_t *p, ngx_chain_t **free)
{
    ngx_chain_t  *cl;
    // 非空则说明有可用的ngx_chain_t节点，更新头指针指向，返回空闲节点
    if (*free) {
        cl = *free;
        *free = cl->next;
        cl->next = NULL;
        return cl;
    }
    // 没有空闲节点，申请一个
    cl = ngx_alloc_chain_link(p);
    if (cl == NULL) {
        return NULL;
    }

    cl->buf = ngx_calloc_buf(p);
    if (cl->buf == NULL) {
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

    if (*out) {
        // busy链表为空则直接指向out链表
        if (*busy == NULL) {
            *busy = *out;

        } else {
            // 否则找到busy的最后一个节点，把out链表追加到busy链表后面
            for (cl = *busy; cl->next; cl = cl->next) { /* void */ }

            cl->next = *out;
        }

        *out = NULL;
    }
    // busy链表非空
    while (*busy) {
        cl = *busy;

        if (ngx_buf_size(cl->buf) != 0) {
            break;
        }
        // tag不一样则直接free掉，更新busy链表的头指针
        if (cl->buf->tag != tag) {
            *busy = cl->next;
            ngx_free_chain(p, cl);
            continue;
        }
        // 重置位置
        cl->buf->pos = cl->buf->start;
        cl->buf->last = cl->buf->start;

        *busy = cl->next;
        // 插入free链表
        cl->next = *free;
        *free = cl;
    }
}


off_t
ngx_chain_coalesce_file(ngx_chain_t **in, off_t limit)
{
    off_t         total, size, aligned, fprev;
    ngx_fd_t      fd;
    ngx_chain_t  *cl;

    total = 0;

    cl = *in;
    fd = cl->buf->file->fd;

    do {
        // 算出大小
        size = cl->buf->file_last - cl->buf->file_pos;
        // limit是总大小，total是当前已处理的大小，size是当前待处理的大小
        if (size > limit - total) {
            size = limit - total;
            // 按ngx_pagesize对齐，不够则补齐
            aligned = (cl->buf->file_pos + size + ngx_pagesize - 1)
                       & ~((off_t) ngx_pagesize - 1);
            // 对齐后还没超过last，则更新size的值，如果对齐后超过了last，则不更新size
            if (aligned <= cl->buf->file_last) {
                size = aligned - cl->buf->file_pos;
            }
            // 重新算total
            total += size;
            // 达到limit了
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
    // 返回处理的大小
    return total;
}

// 根据sent的大小，更新in链表的buf节点
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
        // sent比当前buf大小还大
        if (sent >= size) {
            // 更新sent
            sent -= size;
            // 更pos位置，说明pos到last之间的数据已被处理
            // 内存数据
            if (ngx_buf_in_memory(in->buf)) {
                in->buf->pos = in->buf->last;
            }
            // 文件数据
            if (in->buf->in_file) {
                in->buf->file_pos = in->buf->file_last;
            }

            continue;
        }
        // sent < size，即buf中的数据可能只被处理了一部分（大小是sent），更新pos。
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
