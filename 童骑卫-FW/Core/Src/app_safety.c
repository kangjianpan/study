#include "app_main.h"
#include "app_config.h"
#include "app_pins.h"
#include "app_state.h"
#include "bsp.h"
#include "drv_motor.h"

/* SafetyTask：最高优先级，周期 10 ms
 *
 * 职责
 *   1. 每个节拍翻转一次监控心跳（PB13）
 *   2. 逐条评估所有故障条件
 *   3. 跑状态机（文档 7.5）并控制电调使能线
 *   4. 喂看门狗——而且只在完整跑完一圈之后才喂
 *
 * 故障分三类，区别很关键：
 *   HARD  直接进 FAULT_LOCK，立刻切断驱动
 *   SOFT  先进 DECEL：把油门斜坡降下来，一直不消失才切
 *   INFO  只限速，不改状态（丢链路就属于这一类）
 *
 * 高速行驶中直接断驱动本身就是危险动作，
 * 这正是 DECEL 这个状态存在的全部理由。 */

#define FAULT_MASK_HARD (FAULT_ESTOP      | FAULT_ROLLOVER   | FAULT_CRASH     | \
                         FAULT_OVERCURRENT | FAULT_UNDERVOLT  | FAULT_REMOTE_STOP | \
                         FAULT_INJECT | FAULT_SENSOR | FAULT_PEDAL | FAULT_COMMISSION)

#define FAULT_MASK_SOFT (FAULT_INTERLOCK  | FAULT_OVERSPEED)

/* SafetyTask 的文件级状态
 *
 * 这些变量跨节拍保留，而且只有 SafetyTask 自己读写，所以不加锁、不 volatile。
 * 每个"条件持续一段时间才判定"的故障，都需要一对 (计时中标志, 起始时刻)；
 * 每个"判定后不允许自动恢复"的故障，都需要一个闩锁位。
 *
 * s_hb_level        心跳输出电平，每拍取反，PB13 上看到 50 Hz 方波说明本任务在跑
 * s_roll_timing     倾覆计时器是否正在计时（1 = 上一拍已经超阈值，正在累计）
 * s_roll_start      倾覆计时起点，只在 s_roll_timing 由 0 变 1 时写入
 * s_roll_latched    倾覆已判定，只能复位清除
 * s_crash_latched   碰撞已判定，只能复位清除
 * s_estop_latched   急停已按下，只能复位清除（松开按钮不能自动恢复）
 * s_decel_latched   DECEL 超时已判定，见下方详细说明
 * s_oc_latched      过流已判定，只能复位清除（仅 APP_CHECK_OVERCURRENT=1 时存在）
 * s_published_faults 上一拍推给黑匣子的故障字，用来检测跳变、避免每拍都记一条
 * s_link_lost       上一拍的失联判断结果，只在变化时去改限速，避免反复写 */
static uint8_t    s_hb_level = 0u;
static uint8_t    s_roll_timing = 0u;
static TickType_t s_roll_start = 0u;
static uint8_t    s_roll_latched = 0u;
static uint8_t    s_crash_latched = 0u;
static uint8_t    s_estop_latched = 0u;
static uint8_t    s_uv_latched = 0u;
/* DECEL 超时之后置位。没有这一位的话，升级就只是去 FAULT_LOCK 逛一个节拍：
 * 下一拍已经看不到硬故障，又直接掉回 STANDBY，
 * 那条"我们已经不再信任它了"的规则就完全看不出来。
 * 和别的闩锁一样，只能靠复位清除。 */
static uint8_t    s_decel_latched = 0u;
#if APP_CHECK_OVERCURRENT
static uint8_t    s_oc_latched = 0u;
#endif
static uint32_t   s_published_faults = 0u;
static uint8_t    s_link_lost = 0u;

/* 故障评估 */
static uint32_t evaluate_local_faults(const sensor_data_t *s, const limit_cfg_t *lim)
{
    uint32_t faults = 0u;           /* 本拍累积出来的故障位 */
    uint32_t limit_x100;            /* 当前有效限速，换算成 km/h × 100 */
    int32_t  ax;                    /* 三轴加速度，单位 mg，先提出来避免重复取 */
    int32_t  ay;
    int32_t  az;
    uint32_t mag2;                  /* 加速度模的平方，单位 (mg)^2，比平方省一次开方 */

    /* 急停，低电平有效。做成了闩锁：文档规定物理急停不能靠软件恢复，
     * 所以松开按钮本身不应该把车重新武装起来。 */
    if (HAL_GPIO_ReadPin(ESTOP_GPIO_Port, ESTOP_Pin) == GPIO_PIN_RESET)
    {
        s_estop_latched = 1u;
    }
    if (s_estop_latched != 0u)
    {
        faults |= FAULT_ESTOP;
    }

    if ((s->sample_valid == 0u) ||
        ((xTaskGetTickCount() - s->sample_tick) > pdMS_TO_TICKS(SENSOR_STALE_MS)) ||
        ((xTaskGetTickCount() - g_control_tick) > pdMS_TO_TICKS(CONTROL_STALE_MS)) ||
        !s->imu_present || !s->tof_present || !s->nfc_present || !s->speed_valid)
        faults |= FAULT_SENSOR;
    if (s->pedal_fault) faults |= FAULT_PEDAL;
    if (!APP_DRIVE_COMMISSIONED) faults |= FAULT_COMMISSION;

    /* 佩戴联锁。bring-up 模式下传感器还没装，
     * 所以这一位只能由调试覆盖命令（CMD_DEBUG_INTERLOCK）造出来。 */
#if !APP_BRINGUP_MODE
    if ((s->seat_pressed == 0u) || (s->belt_closed == 0u) || (s->helmet_ok == 0u))
    {
        faults |= FAULT_INTERLOCK;
    }
#endif

    /* 倾覆：|ay| 超过 sin(45°) 并持续 ROLL_TIME_MS */
    if (s->imu_present != 0u)
    {
        /* Provisional body axes: +Z up. Validate mounting and dynamic filtering. */
        ay = (int32_t)s->accel_mg[1];
        az = (int32_t)s->accel_mg[2];
        if ((az < 0) || ((ay * ay) > (az * az)))
        {
            if (s_roll_timing == 0u)
            {
                s_roll_timing = 1u;
                s_roll_start = xTaskGetTickCount();
            }
            else if ((xTaskGetTickCount() - s_roll_start) >=
                     pdMS_TO_TICKS(ROLL_TIME_MS))
            {
                s_roll_latched = 1u;
            }
        }
        else
        {
            s_roll_timing = 0u;
        }
    }
    if (s_roll_latched != 0u)
    {
        faults |= FAULT_ROLLOVER;
    }

    /* 碰撞：|a|^2 明显超过 1 g，不开根号，直接比平方 */
    if (s->imu_present != 0u)
    {
        ax = (int32_t)s->accel_mg[0];
        ay = (int32_t)s->accel_mg[1];
        az = (int32_t)s->accel_mg[2];
        mag2 = (uint32_t)(ax * ax) + (uint32_t)(ay * ay) + (uint32_t)(az * az);
        if (mag2 > 9000000u)            /* (3000 mg) 的平方 */
        {
            s_crash_latched = 1u;
        }
    }
    if (s_crash_latched != 0u)
    {
        faults |= FAULT_CRASH;
    }

    /* 超速。属于 SOFT：车还在动，所以先斜坡降下来。 */
    limit_x100 = (uint32_t)lim->effective * 100u;
    if ((limit_x100 > 0u) &&
        (s->speed_kmh_x100 > (uint16_t)((limit_x100 * OVERSPEED_FACTOR_X100) / 100u)))
    {
        faults |= FAULT_OVERSPEED;
    }

    /* 欠压 */
#if APP_CHECK_VBUS
    if (s->sample_valid && (s->bus_mv < UNDERVOLT_MV)) s_uv_latched = 1u;
    if (s_uv_latched) faults |= FAULT_UNDERVOLT;
#endif

    /* 过流，和急停一样做闩锁 */
#if APP_CHECK_OVERCURRENT
    if (s->current_ma > OVERCURRENT_MA)
    {
        s_oc_latched = 1u;
    }
    if (s_oc_latched != 0u)
    {
        faults |= FAULT_OVERCURRENT;
    }
#endif

    return faults;
}

/* 状态转移表（文档 7.5）
 *
 * 输入只有 (当前状态, 故障掩码, 油门, 停留时长)，
 * 是个纯函数，所以不用读任务里剩下的代码也能单独推演。 */
static app_state_t next_state(app_state_t st, uint32_t hard, uint32_t soft,
                              uint16_t throttle, uint32_t state_ms,
                              uint8_t latched)
{
    app_state_t next = st;

    switch (st)
    {
    case APP_STATE_SELF_TEST:
        if (state_ms >= SELFTEST_MS)
        {
            next = (hard != 0u) ? APP_STATE_FAULT_LOCK : APP_STATE_STANDBY;
        }
        break;

    case APP_STATE_STANDBY:
        if (hard != 0u)
        {
            next = APP_STATE_FAULT_LOCK;
        }
        else if ((soft == 0u) && (throttle < PEDAL_IDLE_THRESHOLD))
        {
            /* 必须先松开油门，才允许使能驱动 */
            next = APP_STATE_READY;
        }
        break;

    case APP_STATE_READY:
        if (hard != 0u)
        {
            next = APP_STATE_FAULT_LOCK;
        }
        else if (soft != 0u)
        {
            next = APP_STATE_DECEL;
        }
        else if ((throttle > PEDAL_RUN_THRESHOLD) &&
                 HAL_GPIO_ReadPin(ARM_OK_GPIO_Port, ARM_OK_Pin) == GPIO_PIN_SET)
        {
            next = APP_STATE_RUN;
        }
        break;

    case APP_STATE_RUN:
        if (hard != 0u)
        {
            next = APP_STATE_FAULT_LOCK;
        }
        else if (soft != 0u)
        {
            next = APP_STATE_DECEL;
        }
        break;

    case APP_STATE_DECEL:
        /* 3 秒的期限从进入 DECEL 那一刻起算，
         * 而不是从斜坡降到零那一刻起算：
         * 降不下来本身就是要升级的故障。 */
        if (hard != 0u)
        {
            next = APP_STATE_FAULT_LOCK;
        }
        else if (state_ms >= DECEL_TIMEOUT_MS)
        {
            next = APP_STATE_FAULT_LOCK;
        }
        else if ((soft == 0u) && (Motor_GetOutput() == 0))
        {
            next = APP_STATE_READY;         /* condition cleared, stopped */
        }
        break;

    case APP_STATE_FAULT_LOCK:
    default:
        /* 只有远程停车和欠压可以不掉电就解除；
         * 其余闩锁（急停、倾覆、碰撞、过流、DECEL 超时）都会一直让条件成立，
         * 所以这条边只可能被那两个触发。 */
        if ((hard == 0u) && (latched == 0u))
        {
            next = APP_STATE_STANDBY;
        }
        break;
    }

    return next;
}

/* 任务主体 */
void SafetyTask(void *arg)
{
    TickType_t    last_wake;    /* vTaskDelayUntil 的基准，保证周期严格 10 ms */
    TickType_t    state_since;  /* 进入当前状态的时刻，算 DECEL 超时用 */
    sensor_data_t s;            /* 本拍从 g_q_sensor 取到的传感器快照 */
    limit_cfg_t   lim;          /* 本拍从 g_mtx_cfg 取到的限速快照 */
    uint32_t      local;        /* 本任务自己评估出来的故障（急停/倾覆/碰撞/超速…） */
    uint32_t      remote;       /* 远端命令产生的故障，来自 g_eg_fault 事件组 */
    uint32_t      all;          /* local | remote，本拍的总故障字 */
    uint32_t      hard;         /* all 里属于"立刻切断"的那一类 */
    uint32_t      soft;         /* all 里属于"先降速再切"的那一类 */
    uint32_t      state_ms;     /* 在当前状态已经停留了多久 */
    uint16_t      throttle;     /* 本拍采用的油门值，千分比 */
    uint8_t       link_lost;    /* 本拍的失联判断，和 s_link_lost 比较得出跳变 */
    uint8_t       enable;       /* 本拍该不该给电调使能 */
    app_state_t   st;           /* 当前状态 */
    uint8_t       arm_seen = 0u;
    uint8_t       arm_idle_seen = 0u;
    app_state_t   next;         /* 状态机算出的下一状态 */

    (void)arg;

    last_wake   = xTaskGetTickCount();
    state_since = last_wake;

    for (;;)
    {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(PERIOD_SAFETY_MS));

        (void)xQueuePeek(g_q_sensor, &s, 0);
        App_GetLimitSnapshot(&lim);

        /* 油门：调试覆盖优先，规则和 ControlTask 保持一致。 */
        throttle = (g_dbg_throttle >= 0) ? (uint16_t)g_dbg_throttle
                                         : s.throttle_permille;

        local  = evaluate_local_faults(&s, &lim);
        if (g_dbg_interlock_fail != 0u)
        {
            local |= FAULT_INTERLOCK;
        }
        remote = (uint32_t)xEventGroupGetBits(g_eg_fault) & FAULT_MASK_REMOTE;
        all    = local | remote;
        g_local_faults = local;
        /* Only produce the external watchdog heartbeat if both essential tasks progress. */
        if (s.sample_valid &&
            ((xTaskGetTickCount() - s.sample_tick) <= pdMS_TO_TICKS(SENSOR_STALE_MS)) &&
            ((xTaskGetTickCount() - g_control_tick) <= pdMS_TO_TICKS(CONTROL_STALE_MS)))
        {
            s_hb_level ^= 1u;
            HAL_GPIO_WritePin(HB_GPIO_Port, HB_Pin,
                             s_hb_level ? GPIO_PIN_SET : GPIO_PIN_RESET);
        }

        /* 链路监护。丢了手机不能把孩子晾在赛道中间，
         * 所以这里不改状态：只把限速压到 LIMIT_FALLBACK，等链路回来。
         * 但标志位仍会通过 FAULT_LINK_LOST 传给 HMI 和黑匣子。 */
        link_lost = 0u;
        if (g_link_seen != 0u)
        {
            if ((xTaskGetTickCount() - g_last_link_tick) >
                pdMS_TO_TICKS(LINK_TIMEOUT_MS))
            {
                link_lost = 1u;
                all |= FAULT_LINK_LOST;
            }
        }
        if (link_lost != s_link_lost)
        {
            s_link_lost = link_lost;
            App_SetLinkFallback((link_lost != 0u) ? LIMIT_FALLBACK : 0u);
        }

        /* 故障注入：模拟一个不再调度的控制器。
         * 中断还开着，但这里永不主动让出，
         * 而本任务又是最高优先级，所以别的任务再也不会运行。
         * 看门狗会在约 200 ms 内把它变成一次复位。 */
        if ((all & FAULT_INJECT) != 0u)
        {
            for (;;)
            {
                __NOP();
            }
        }

        hard = all & FAULT_MASK_HARD;
        soft = all & FAULT_MASK_SOFT;

        /* Hardware ARM can close only in READY. Require a released pedal AFTER
         * ARM feedback, then a new press; pressing ARM with pedal down cannot run. */
        if (HAL_GPIO_ReadPin(ARM_OK_GPIO_Port, ARM_OK_Pin) == GPIO_PIN_RESET)
        {
            arm_seen = 0u;
            arm_idle_seen = 0u;
        }
        else if (!arm_seen)
        {
            arm_seen = 1u;
            arm_idle_seen = 0u;
        }
        else if (throttle < PEDAL_IDLE_THRESHOLD)
        {
            arm_idle_seen = 1u;
        }
        st       = g_app_state;
        state_ms = (uint32_t)(xTaskGetTickCount() - state_since);
        next     = next_state(st, hard, soft, throttle, state_ms, s_decel_latched);
        if ((hard == 0u) && (next == APP_STATE_RUN) && !arm_idle_seen)
            next = APP_STATE_READY;
        if ((hard == 0u) && (st == APP_STATE_RUN) && !arm_seen)
            next = APP_STATE_STANDBY;

        if ((st == APP_STATE_DECEL) && (next == APP_STATE_FAULT_LOCK))
        {
            /* DECEL 超时一律永久升级 */
            s_decel_latched = 1u;
        }

        if (next != st)
        {
            g_app_state = next;
            state_since = xTaskGetTickCount();
            App_LogEvent(LOG_EVT_STATE,
                         (uint16_t)(((uint16_t)st << 8) | (uint16_t)next));
        }

        /* 驱动使能。DECEL 期间保持开着：
         * 电调必须活着，才有机会把转速降下来。 */
        enable = App_State_MotorEnabled(next);
        if (!APP_DRIVE_COMMISSIONED || hard) enable = 0u;
        g_drive_allowed = enable;
        Motor_Enable(enable);
        if (enable == 0u)
        {
            Motor_Stop();
        }

        /* 只在本任务里喂狗。本任务一旦卡住，复位就会跟上来。 */
        if (s.sample_valid &&
            ((xTaskGetTickCount() - s.sample_tick) <= pdMS_TO_TICKS(SENSOR_STALE_MS)) &&
            ((xTaskGetTickCount() - g_control_tick) <= pdMS_TO_TICKS(CONTROL_STALE_MS)))
            Bsp_Iwdg_Feed();

        /* 把故障的跳变推给黑匣子 */
        if (all != s_published_faults)
        {
            if (all != 0u)
            {
                App_LogEvent(LOG_EVT_FAULT_SET, (uint16_t)all);
            }
            else
            {
                App_LogEvent(LOG_EVT_FAULT_CLEAR, (uint16_t)s_published_faults);
            }
            s_published_faults = all;
        }
    }
}
