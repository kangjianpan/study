#include "ringbuf.h"

void RingBuf_Init(ringbuf_t *rb, uint8_t *mem, uint16_t size)
{
    rb->buf  = mem;
    rb->size = size;
    rb->head = 0u;
    rb->tail = 0u;
}

int RingBuf_Put(ringbuf_t *rb, uint8_t byte)
{
    uint16_t next;

    /* size 是 2 的幂，所以 &(size-1) 等价于对 size 取模，但不用做除法 */
    next = (uint16_t)((rb->head + 1u) & (uint16_t)(rb->size - 1u));
    if (next == rb->tail)
    {
        return 0;               /* 满了：丢不丢由调用者决定 */
    }

    rb->buf[rb->head] = byte;
    rb->head = next;

    return 1;
}

int RingBuf_Get(ringbuf_t *rb, uint8_t *byte)
{
    if (rb->tail == rb->head)
    {
        return 0;               /* 空 */
    }

    *byte = rb->buf[rb->tail];
    rb->tail = (uint16_t)((rb->tail + 1u) & (uint16_t)(rb->size - 1u));

    return 1;
}

uint16_t RingBuf_Count(const ringbuf_t *rb)
{
    return (uint16_t)((rb->head - rb->tail) & (uint16_t)(rb->size - 1u));
}
