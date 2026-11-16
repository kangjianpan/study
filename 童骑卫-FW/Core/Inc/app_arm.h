#ifndef APP_ARM_H
#define APP_ARM_H
#include <stdint.h>
typedef struct
{
    uint32_t off_since, neutral_since;
    uint8_t initialized, was_enabled, feedback, clear_seen;
    uint8_t neutral_done, idle_seen, fault;
} arm_sequence_t;
void Arm_Update(arm_sequence_t *s, uint8_t enabled, uint8_t feedback,
                uint16_t pedal, uint32_t now_ms);
uint8_t Arm_CanRun(const arm_sequence_t *s);
#endif
