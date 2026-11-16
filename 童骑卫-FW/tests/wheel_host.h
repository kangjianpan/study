#include <stdint.h>
extern uint32_t mock_time;
#define taskENTER_CRITICAL() ((void)0)
#define taskEXIT_CRITICAL() ((void)0)
#define HAL_GetTick() mock_time
