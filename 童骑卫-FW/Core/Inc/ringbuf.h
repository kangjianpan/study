#ifndef __RINGBUF_H
#define __RINGBUF_H

#include <stdint.h>

/* 单生产者 / 单消费者字节环形缓冲。
 * 生产者是中断服务程序，消费者是任务，两边各只动一个下标，所以不用加锁。
 * size 必须是 2 的幂，这样下标回绕可以直接按位与，省掉一次取模。 */
typedef struct
{
    uint8_t         *buf;
    uint16_t         size;
    volatile uint16_t head;     /* 只由生产者写 */
    volatile uint16_t tail;     /* 只由消费者写 */
} ringbuf_t;

void     RingBuf_Init(ringbuf_t *rb, uint8_t *mem, uint16_t size);
int      RingBuf_Put(ringbuf_t *rb, uint8_t byte);      /* 返回 0 表示已满 */
int      RingBuf_Get(ringbuf_t *rb, uint8_t *byte);     /* 返回 0 表示已空 */
uint16_t RingBuf_Count(const ringbuf_t *rb);

#endif /* __RINGBUF_H */
