"""Host regression of the actual safety decision functions (no HAL/RTOS hardware)."""
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[2]
BASE=Path(__file__).parent
source=(ROOT/'童骑卫-FW/Core/Src/app_safety.c').read_text(encoding='utf-8')
source=source[source.index('#define FAULT_MASK_HARD'):source.index('/* 任务主体 */')]
config=(ROOT/'童骑卫-FW/Core/Inc/app_config.h').read_text(encoding='utf-8')
types=(ROOT/'童骑卫-FW/Core/Inc/app_types.h').read_text(encoding='utf-8')
state=(ROOT/'童骑卫-FW/Core/Inc/app_state.h').read_text(encoding='utf-8')
state='\n'.join(l for l in state.splitlines() if not l.startswith('#include'))
prefix='''#include <stdint.h>
#include <stdio.h>
#include <assert.h>
typedef uint32_t TickType_t;
static uint32_t mock_tick = 1000u;
static int estop = 1, arm = 0;
static int motor = 0;
volatile uint32_t g_control_tick = 1000u;
#define pdMS_TO_TICKS(x) (x)
#define ESTOP_GPIO_Port 0
#define ESTOP_Pin 1
#define ARM_OK_GPIO_Port 0
#define ARM_OK_Pin 2
#define GPIO_PIN_RESET 0
#define GPIO_PIN_SET 1
uint32_t xTaskGetTickCount(void) { return mock_tick; }
int HAL_GPIO_ReadPin(int p, int n) { (void)p; return n == 1 ? estop : arm; }
int Motor_GetOutput(void) { return motor; }
'''
tests='''
int main(void) {
 sensor_data_t s = {0};
 limit_cfg_t lim = {12,10,12,0,10};
 uint32_t f;
 s.sample_valid=1; s.sample_tick=mock_tick; s.imu_present=1;
 s.tof_present=1;s.nfc_present=1;s.speed_valid=1;
 s.seat_pressed=1;s.belt_closed=1;s.helmet_ok=1;
 s.bus_mv=25900;s.accel_mg[2]=1000;
 f=evaluate_local_faults(&s,&lim);
 assert(f == FAULT_COMMISSION);
 s.pedal_fault=1; assert(evaluate_local_faults(&s,&lim)&FAULT_PEDAL);s.pedal_fault=0;
 s.sample_tick=mock_tick-101;assert(evaluate_local_faults(&s,&lim)&FAULT_SENSOR);s.sample_tick=mock_tick;
 s.tof_present=0;assert(evaluate_local_faults(&s,&lim)&FAULT_SENSOR);s.tof_present=1;
 s.speed_valid=0;assert(evaluate_local_faults(&s,&lim)&FAULT_SENSOR);s.speed_valid=1;
 g_control_tick=mock_tick-51;assert(evaluate_local_faults(&s,&lim)&FAULT_SENSOR);g_control_tick=mock_tick;
 arm=0;assert(next_state(APP_STATE_READY,0,0,500,0,0)==APP_STATE_READY);
 arm=1;assert(next_state(APP_STATE_READY,0,0,500,0,0)==APP_STATE_RUN);
 assert(next_state(APP_STATE_RUN,FAULT_PEDAL,0,500,0,0)==APP_STATE_FAULT_LOCK);
 assert(next_state(APP_STATE_STANDBY,0,0,500,0,0)==APP_STATE_STANDBY);
 estop=0;assert(evaluate_local_faults(&s,&lim)&FAULT_ESTOP);
 estop=1;assert(evaluate_local_faults(&s,&lim)&FAULT_ESTOP);
 s.bus_mv=21000;assert(evaluate_local_faults(&s,&lim)&FAULT_UNDERVOLT);
 s.bus_mv=25900;assert(evaluate_local_faults(&s,&lim)&FAULT_UNDERVOLT);
 puts("Safety decision regression: 14 assertions passed.");
 return 0;
}
'''
path=BASE/'safety_host.c';path.write_text(prefix+config+'\n'+types+'\n'+state+'\n'+source+tests,encoding='utf-8')
gcc='C:/Program Files (x86)/Dev-Cpp/MinGW64/bin/gcc.exe'
subprocess.run([gcc,'-std=c99','-w',str(path),'-o',str(BASE/'safety_host.exe')],check=True)
subprocess.run([str(BASE/'safety_host.exe')],check=True)
