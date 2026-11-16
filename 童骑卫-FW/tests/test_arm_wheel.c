#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "app_arm.h"
#include "app_config.h"
#include "drv_wheel.h"
#include "drv_imu.h"
uint32_t mock_time;
int main(void)
{
    arm_sequence_t s = {0};
    uint16_t speed, pulses;
    Arm_Update(&s, 0, 0, 0, 0);
    Arm_Update(&s, 1, 0, 0, 10); assert(!Arm_CanRun(&s));
    Arm_Update(&s, 1, 1, 600, 20); assert(!Arm_CanRun(&s));
    Arm_Update(&s, 1, 1, 0, 100); assert(!Arm_CanRun(&s));
    Arm_Update(&s, 1, 1, 600, 3019); assert(!Arm_CanRun(&s));
    Arm_Update(&s, 1, 1, 600, 3020); assert(!Arm_CanRun(&s));
    Arm_Update(&s, 1, 1, 0, 3030); assert(Arm_CanRun(&s));
    Arm_Update(&s, 1, 1, 600, 3040); assert(Arm_CanRun(&s));
    Arm_Update(&s, 1, 0, 600, 3050); assert(!Arm_CanRun(&s));
    Arm_Update(&s, 0, 1, 0, 3060); assert(!s.fault);
    Arm_Update(&s, 0, 1, 0, 3090); assert(s.fault && !Arm_CanRun(&s));
    memset(&s, 0, sizeof(s));
    Arm_Update(&s, 0, 1, 0, 0);
    Arm_Update(&s, 1, 1, 0, 10); assert(s.fault);
    memset(&s, 0, sizeof(s));
    Arm_Update(&s, 0, 0, 0, 0xfffffff0u);
    Arm_Update(&s, 1, 1, 0, 0xfffffffau);
    Arm_Update(&s, 1, 1, 0, 2994u); assert(Arm_CanRun(&s));
    Arm_Update(&s, 0, 0, 0, 3010); assert(!Arm_CanRun(&s));
    Arm_Update(&s, 1, 1, 0, 3020); assert(!Arm_CanRun(&s));
    DrvWheel_Init();
    mock_time = 0;
    assert(DrvWheel_Read(&speed, &pulses) == DRV_OK && speed == 0);
    DrvWheel_Edge(0, 100); DrvWheel_Edge(1, 100);
    DrvWheel_Edge(0, 200); DrvWheel_Edge(1, 200);
    DrvWheel_Edge(0, 201); /* reject contact glitch */
    mock_time = 200;
    assert(DrvWheel_Read(&speed, &pulses) == DRV_OK && speed == 3600 && pulses == 4);
    mock_time = 400;
    assert(DrvWheel_Read(&speed, &pulses) == DRV_OK && speed == 1800);
    mock_time = 2200;
    assert(DrvWheel_Read(&speed, &pulses) == DRV_OK && speed == 0);
    DrvWheel_Edge(0, 3000); DrvWheel_Edge(0, 3100);
    mock_time = 3100; assert(DrvWheel_Read(&speed, &pulses) == DRV_ERR);
    puts("ARM sequence and wheel capture: passed");
    return 0;
}
