#include "app_main.h"
#include "app_config.h"
#include "app_state.h"

#include "drv_motor.h"

/* ControlTask，周期 5 ms
 *
 * 油门量 = 踏板读数，然后依次被三层逻辑修正：
 *   - 三级限速权限（柔性限幅，不做硬切）
 *   - 两个 ToF 传感器算出来的碰撞时间
 *   - 斜率限制，保证电调永远看不到阶跃
 * 最后把结果送给电调。 */

void ControlTask(void *arg)
{
    TickType_t    last_wake;    /* vTaskDelayUntil 的基准，锁住 5 ms 周期 */
    sensor_data_t s;            /* 本拍传感器快照（Peek 最新值） */
    limit_cfg_t   lim;          /* 本拍的限速快照 */
    int16_t       target;       /* 本拍"想给"的油门，千分比，还没过斜率限制 */
    int16_t       out;          /* 本拍"实际给"的油门，跨拍保留，充当积分器的当前值 */
    uint16_t      limit_x100;   /* 有效限速，km/h × 100，与 speed 同量纲便于比较 */
    uint16_t      speed;        /* 本拍车速，km/h × 100 */
    uint16_t      d_min;        /* 左右两颗 ToF 里更近的那一个，mm */
    uint32_t      ttc_ms;       /* 碰撞时间，ms；到障碍物还剩多久 */
    uint16_t      over;         /* 超出限速多少，km/h × 100 */

    (void)arg;

    last_wake = xTaskGetTickCount();
    out = 0;

    for (;;)
    {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(PERIOD_CONTROL_MS));

        (void)xQueuePeek(g_q_sensor, &s, 0);
        App_GetLimitSnapshot(&lim);

        g_control_tick = xTaskGetTickCount();
        if ((g_drive_allowed == 0u) || (lim.effective == 0u))
        {
            out = 0;
            Motor_SetOutput(0);
            continue;
        }

        /* 不在 RUN 状态时油门指令一律给 0。DECEL 就靠这一条：
         * 电调保持使能，下面的斜率限制让目标值按 THROTTLE_RAMP_DOWN
         * 慢慢收到 0，而不是在高速行驶中直接切断动力。 */
        if (g_app_state != APP_STATE_RUN)
        {
            target = 0;
            goto slew;
        }

        /* 踏板读数 */
        if (g_dbg_throttle >= 0)
        {
            target = g_dbg_throttle;
        }
        else
        {
            target = (int16_t)s.throttle_permille;
        }
        if (target < 0)
        {
            target = 0;
        }
        if (target > 1000)
        {
            target = 1000;
        }

        speed = s.speed_kmh_x100;

        /* 柔性限速
         * 一过限速就开始按比例收油门；超出 SOFT_LIMIT_DEADBAND 才完全切断。 */
        limit_x100 = (uint16_t)((uint16_t)lim.effective * 100u);
        if ((limit_x100 > 0u) && (speed > limit_x100))
        {
            over = (uint16_t)(speed - limit_x100);
            if (over >= SOFT_LIMIT_DEADBAND_X100)
            {
                target = 0;
            }
            else
            {
                target = (int16_t)(((int32_t)target *
                                    (int32_t)(SOFT_LIMIT_DEADBAND_X100 - over)) /
                                   (int32_t)SOFT_LIMIT_DEADBAND_X100);
            }
        }

        /* 障碍避让 */
        if (s.tof_present != 0u)
        {
            d_min = (s.tof_mm[0] < s.tof_mm[1]) ? s.tof_mm[0] : s.tof_mm[1];

            if ((speed > 0u) && (d_min > 0u))
            {
                /* 碰撞时间(ms) = 360 * 距离(mm) / 车速(km/h x100)
                 * 系数 360 的来历：1 km/h = 277.8 mm/s，
                 * 车距 d mm、速度 v km/h，则 t = d / (v * 277.8) 秒
                 * = 3.6 * d * 100 / v_x100 ms = 360 * d / v_x100。 */
                ttc_ms = (360u * (uint32_t)d_min) / (uint32_t)speed;

                if (ttc_ms < TTC_BRAKE_MS)
                {
                    target = 0;
                }
                else if (ttc_ms < TTC_WARN_MS)
                {
                    target = (int16_t)(target / 2);
                }
            }

            if (d_min < TOF_STOP_MM)
            {
                target = 0;
            }
        }

        /* 斜率限制：每次走一小步，逼近 target */
slew:
        if (target > out)
        {
            out = (int16_t)(out + THROTTLE_RAMP_UP);
            if (out > target)
            {
                out = target;
            }
        }
        else if (target < out)
        {
            out = (int16_t)(out - THROTTLE_RAMP_DOWN);
            if (out < target)
            {
                out = target;
            }
        }

        if (out < 0)
        {
            out = 0;
        }

        Motor_SetOutput(out);
    }
}
