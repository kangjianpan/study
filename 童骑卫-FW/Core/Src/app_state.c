#include "app_state.h"
#include "app_config.h"

/* 全局状态机的当前状态。
 * 初值是 SELF_TEST，不是 STANDBY —— 上电后先等 SELFTEST_MS(200 ms)
 * 让传感器和电调稳定，期间任何故障都会直接跳到 FAULT_LOCK。 */
volatile app_state_t g_app_state = APP_STATE_SELF_TEST;

/* 状态名查表，下标必须和 app_state_t 的枚举顺序严格一致。
 * 改枚举时漏改这里，串口上就会打印出错误的 (或者越界的) 名字。
 * 只供调试打印用，不参与任何控制逻辑。 */
static const char *const s_names[APP_STATE_COUNT] =
{
    "SELF_TEST",
    "STANDBY",
    "READY",
    "RUN",
    "DECEL",
    "FAULT_LOCK"
};

const char *App_State_Name(app_state_t st)
{
    if ((unsigned)st >= (unsigned)APP_STATE_COUNT)
    {
        return "?";
    }

    return s_names[(unsigned)st];
}

uint8_t App_State_MotorEnabled(app_state_t st)
{
    if (st == APP_STATE_READY)
    {
        /* 默认不使能；只有实测确认过"电调收到中立脉宽就是零扭矩"才打开，
         * 见 APP_ENABLE_IN_READY。 */
        return (APP_ENABLE_IN_READY != 0) ? 1u : 0u;
    }

    if ((st == APP_STATE_RUN) || (st == APP_STATE_DECEL))
    {
        return 1u;
    }

    return 0u;
}
