"""Compile actual decision/driver sources against deterministic bus/RTOS fakes."""
from pathlib import Path
import os
import shutil
import subprocess

FW = Path(__file__).resolve().parents[1]
TEST = Path(__file__).resolve().parent
BUILD = TEST / 'build'
BUILD.mkdir(exist_ok=True)
gcc = os.environ.get('CC') or shutil.which('gcc')
if not gcc:
    raise SystemExit('gcc is required (set CC or add it to PATH).')

def run(name, sources, flags=()):
    out = BUILD / (name + ('.exe' if os.name == 'nt' else ''))
    subprocess.run([gcc, '-std=c99', '-Wall', '-Wextra', '-Werror',
        '-I' + str(FW/'Core/Inc'), '-I' + str(TEST), *flags,
        *map(str, sources), '-o', str(out)], check=True)
    subprocess.run([str(out)], check=True)

run('arm_wheel', [TEST/'test_arm_wheel.c', FW/'Core/Src/app_arm.c', FW/'Core/Src/drv_wheel.c'],
    ['-DHOST_TEST', '-DAPP_WHEEL_VALIDATED=1', '-DWHEEL_CIRCUMFERENCE_MM=1000', '-DWHEEL_PULSES_PER_REV=1'])
run('sensors', [TEST/'test_sensors.c', *[FW/'Core/Src'/n for n in ['drv_imu.c','drv_tof.c','drv_nfc.c']]],
    ['-DAPP_HELMET_UID_LEN=7', '-DAPP_HELMET_UID_BYTES={1,2,3,4,5,6,7}'])

# Compile the real, complete SafetyTask and execute several scheduler ticks.
fake = BUILD/'app_main.h'
fake.write_text('''#include <stdint.h>
#include "app_types.h"
#include "app_state.h"
typedef uint32_t TickType_t;
extern int g_q_sensor, g_eg_fault;
extern volatile uint32_t g_control_tick,g_local_faults,g_last_link_tick;
extern volatile uint8_t g_drive_allowed,g_arm_can_run,g_sensor_initializing,g_dbg_interlock_fail,g_link_seen;
extern volatile int16_t g_dbg_throttle;
#define pdMS_TO_TICKS(x) (x)
#define GPIO_PIN_SET 1
#define GPIO_PIN_RESET 0
#define ESTOP_Pin 1
#define ARM_OK_Pin 2
#define HB_Pin 3
#define ESTOP_GPIO_Port 0
#define ARM_OK_GPIO_Port 0
#define HB_GPIO_Port 0
#define __APP_PINS_H
#define __BSP_H
#define __DRV_MOTOR_H
uint32_t xTaskGetTickCount(void);
uint32_t HAL_GetTick(void);
int HAL_GPIO_ReadPin(int p,int n);
void HAL_GPIO_WritePin(int p,int n,int v);
void vTaskDelayUntil(uint32_t *last,uint32_t period);
int xQueuePeek(int q,void *out,int t);
uint32_t xEventGroupGetBits(int e);
void App_GetLimitSnapshot(limit_cfg_t *out);
void App_SetLinkFallback(uint8_t v);
void App_LogEvent(uint8_t c,uint16_t v);
void Bsp_Iwdg_Feed(void);
int16_t Motor_GetOutput(void);
void Motor_Enable(uint8_t v);
void Motor_Stop(void);
#define __NOP() ((void)0)
''', encoding='utf-8')
source = (FW/'Core/Src/app_safety.c').read_text(encoding='utf-8')
source = source.replace('#include "bsp.h"', '').replace('#include "drv_motor.h"', '')
source = source.replace('#include "app_pins.h"', '')
path = BUILD/'safety_actual.c'
path.write_text(source, encoding='utf-8')
run('safety', [TEST/'test_safety.c', path, FW/'Core/Src/app_arm.c', FW/'Core/Src/app_state.c'], ['-I'+str(BUILD)])
run('safety_commissioned', [TEST/'test_safety.c', path, FW/'Core/Src/app_arm.c', FW/'Core/Src/app_state.c'],
    ['-I'+str(BUILD), '-DTEST_COMMISSIONED', '-DAPP_DRIVE_COMMISSIONED=1',
     '-DAPP_WHEEL_VALIDATED=1', '-DAPP_ESC_TIMING_VALIDATED=1',
     '-DWHEEL_CIRCUMFERENCE_MM=1000', '-DWHEEL_PULSES_PER_REV=1', '-DAPP_HELMET_UID_LEN=7'])

# Check that opening the drive gate without hardware configuration fails compilation.
gate = BUILD/'commission.c'
gate.write_text('#include "app_config.h"\nint main(void){return 0;}\n', encoding='ascii')
bad = subprocess.run([gcc, '-I'+str(FW/'Core/Inc'), '-DAPP_DRIVE_COMMISSIONED=1', '-c', str(gate),
                      '-o', str(BUILD/'commission.o')], capture_output=True, text=True)
assert bad.returncode != 0 and 'Commission wheel geometry' in bad.stderr, bad.stderr
print('Unconfigured drive enable: rejected as expected')
