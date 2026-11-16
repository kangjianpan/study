#include "app_arm.h"
#include "app_config.h"

void Arm_Update(arm_sequence_t *s, uint8_t enabled, uint8_t feedback,
                uint16_t pedal, uint32_t now)
{
    if (!s->initialized) { s->off_since = now; s->initialized = 1; }
    if (!enabled)
    {
        if (s->was_enabled) { s->off_since = now; s->clear_seen = 0; }
        s->was_enabled = 0; s->feedback = 0;
        s->neutral_done = 0; s->idle_seen = 0;
        if (!feedback) s->clear_seen = 1;
        else if ((uint32_t)(now - s->off_since) >= ARM_CLEAR_SETTLE_MS) s->fault = 1;
        return;
    }
    /* Feedback cannot legitimately precede release of the hardware clear. */
    if (!s->was_enabled && !s->clear_seen) s->fault = 1;
    s->was_enabled = 1;
    if (!feedback)
    {
        s->feedback = 0; s->neutral_done = 0; s->idle_seen = 0;
        return;
    }
    if (!s->feedback) { s->neutral_since = now; s->idle_seen = 0; }
    s->feedback = 1;
    s->neutral_done = (uint32_t)(now - s->neutral_since) >= ESC_NEUTRAL_DWELL_MS;
    if (s->neutral_done && pedal < PEDAL_IDLE_THRESHOLD) s->idle_seen = 1;
}
uint8_t Arm_CanRun(const arm_sequence_t *s)
{ return s->was_enabled && s->feedback && s->neutral_done && s->idle_seen && !s->fault; }
