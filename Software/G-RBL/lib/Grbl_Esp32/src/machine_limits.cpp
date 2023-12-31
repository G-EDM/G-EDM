/*
  Limits.cpp - code pertaining to limit-switches and performing the homing cycle
  Part of Grbl

  Copyright (c) 2012-2016 Sungeun K. Jeon for Gnea Research LLC
  Copyright (c) 2009-2011 Simen Svale Skogsrud

	2018 -	Bart Dring This file was modifed for use on the ESP32
					CPU. Do not use this with Grbl for atMega328P
  2018-12-29 - Wolfgang Lienbacher renamed file from limits.h to grbl_limits.h
          fixing ambiguation issues with limit.h in the esp32 Arduino Framework
          when compiling with VS-Code/PlatformIO.

  Grbl is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Grbl is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with Grbl.  If not, see <http://www.gnu.org/licenses/>.

    2023 - Roland Lautensack (G-EDM) This file was edited and may no longer be compatible with the default grbl

*/
#include "Grbl.h"
#include <Arduino.h> // remove arduino later

uint8_t n_homing_locate_cycle = NHomingLocateCycle;

xQueueHandle limit_sw_queue;  // used by limit switch debouncing

// Homing axis search distance multiplier. Computed by this value times the cycle travel.
#ifndef HOMING_AXIS_SEARCH_SCALAR
#    define HOMING_AXIS_SEARCH_SCALAR 1.1  // Must be > 1 to ensure limit switch will be engaged.
#endif
#ifndef HOMING_AXIS_LOCATE_SCALAR
#    define HOMING_AXIS_LOCATE_SCALAR 5.0  // Must be > 1 to ensure limit switch is cleared.
#endif

void IRAM_ATTR isr_limit_switches() {
    // Ignore limit switches if already in an alarm state or in-process of executing an alarm.
    // When in the alarm state, Grbl should have been reset or will force a reset, so any pending
    // moves in the planner and serial buffers are all cleared and newly sent blocks will be
    // locked out until a homing cycle or a kill lock command. Allows the user to disable the hard
    // limit setting if their limits are constantly triggering after a reset and move their axes.
    if (sys.state != State::Alarm && sys.state != State::Homing) {
        if (sys_rt_exec_alarm == ExecAlarm::None) {
#ifdef ENABLE_SOFTWARE_DEBOUNCE
            // we will start a task that will recheck the switches after a small delay
            int evt;
            xQueueSendFromISR(limit_sw_queue, &evt, NULL);
#else
#    ifdef HARD_LIMIT_FORCE_STATE_CHECK
            // Check limit pin state.
            if (limits_get_state()) {
                grbl_msg_sendf(CLIENT_ALL, MsgLevel::Debug, "Hard limits");
                mc_reset();                                // Initiate system kill.
                sys_rt_exec_alarm = ExecAlarm::HardLimit;  // Indicate hard limit critical event
            }
#    else
            grbl_msg_sendf(CLIENT_ALL, MsgLevel::Debug, "Hard limits");
            mc_reset();                                // Initiate system kill.
            sys_rt_exec_alarm = ExecAlarm::HardLimit;  // Indicate hard limit critical event
#    endif
#endif
        }
    }
}

// Homes the specified cycle axes, sets the machine position, and performs a pull-off motion after
// completing. Homing is a special motion case, which involves rapid uncontrolled stops to locate
// the trigger point of the limit switches. The rapid stops are handled by a system level axis lock
// mask, which prevents the stepper algorithm from executing step pulses. Homing motions typically
// circumvent the processes for executing motions in normal operation.
// NOTE: Only the abort realtime command can interrupt this process.
// TODO: Move limit pin-specific calls to a general function for portability.
void limits_go_home(uint8_t cycle_mask) {
    if (sys.abort) {
        return;  // Block if system reset has been issued.
    }

    planner.set_ignore_breakpoints( true );
    // Initialize plan data struct for homing motion. Spindle and .. are disabled.

    // Put motors on axes listed in cycle_mask in homing mode and
    // replace cycle_mask with the list of motors that are ready for homing.
    // Motors with non standard homing can home during motors_set_homing_mode(...)
    cycle_mask = motor_manager.motors_set_homing_mode(cycle_mask, true);  // tell motors homing is about to start

    // see if any motors are left
    if (cycle_mask == 0) {
        planner.set_ignore_breakpoints( false );
        return;
    }
      
    plan_line_data_t  plan_data;
    plan_line_data_t* pl_data = &plan_data;
    memset(pl_data, 0, sizeof(plan_line_data_t));
    pl_data->motion                = {};
    pl_data->motion.systemMotion   = 1;
    pl_data->motion.noFeedOverride = 1;
    pl_data->step_delay            = DEFAULT_STEP_DELAY_HOMING_SEEK;


    // Initialize variables used for homing computations.
    uint8_t n_cycle = (2 * n_homing_locate_cycle + 1);
    uint8_t step_pin[MAX_N_AXIS];
    float   max_travel = 0.0;
    auto n_axis        = N_AXIS;

    for (uint8_t idx = 0; idx < n_axis; idx++) {
        // Initialize step pin masks
        step_pin[idx] = bit(idx);
        if (bit_istrue(cycle_mask, bit(idx))) {
            // Set target based on max_travel setting. Ensure homing switches engaged with search scalar.
            max_travel = MAX(max_travel, (HOMING_AXIS_SEARCH_SCALAR)*axis_settings[idx]->max_travel->get());
        }
    }

    // Set search mode with approach at seek rate to quickly engage the specified cycle_mask limit switches.
    bool     approach    = true;
    float    homing_rate = homing_seek_rate->get();
    uint8_t  n_active_axis;
    bool limit_state;
    AxisMask axislock;



    do {

        float target[MAX_N_AXIS];
        float* restore_target = system_get_mpos();
        memcpy( target, restore_target, sizeof(restore_target[0]) * N_AXIS );

        // Initialize and declare variables needed for homing routine.
        axislock      = 0;
        n_active_axis = 0;
        for (uint8_t idx = 0; idx < n_axis; idx++) {
            // Set target location for active axes and setup computation for homing rate.
            if (bit_istrue(cycle_mask, bit(idx))) {
                n_active_axis++;
                sys_position[idx] = 0;
                // Set target direction based on cycle mask and homing cycle approach state.
                // NOTE: This happens to compile smaller than any other implementation tried.
                auto mask = homing_dir_mask->get();
                if (bit_istrue(mask, bit(idx))) {
                    if (approach) {
                        target[idx] = -max_travel;
                    } else {
                        target[idx] = max_travel;
                    }
                } else {
                    if (approach) {
                        target[idx] = max_travel;
                    } else {
                        target[idx] = -max_travel;
                    }
                }
                // Apply axislock to the step port pins active in this cycle.
                axislock |= step_pin[idx];
            }
        }
        homing_rate *= sqrt(n_active_axis);  // [sqrt(number of active axis)] Adjust so individual axes all move at homing rate.
        sys.homing_axis_lock = axislock;
        // Perform homing cycle. Planner buffer should be empty, as required to initiate the homing cycle.
        pl_data->feed_rate = homing_rate;   // Set current homing rate.

        pl_data->use_limit_switches = approach ? true : false;

        planner.plan_history_line(target, pl_data);

        sys.step_control                  = {};
        sys.step_control.executeSysMotion = true;  // Set to execute homing motion and clear existing flags.
        //st_wake_up();  
                                    // Initiate motion

        if (approach) {
            // Check limit state. Lock out cycle axes when they change.
            limit_state = limits_get_state();
            for (uint8_t idx = 0; idx < n_axis; idx++) {
                if (axislock & step_pin[idx]) {
                    if (limit_state) {
                        axislock &= ~(step_pin[idx]);
                    }
                }
            }
            sys.homing_axis_lock = axislock;
        }


        //st_reset();                        // Immediately force kill steppers and reset step segment buffer.
        delay_ms(homing_debounce->get());  // Delay to allow transient dynamics to dissipate.
        // Reverse direction and reset homing rate for locate cycle(s).
        approach = !approach;
        // After first cycle, homing enters locating phase. Shorten search to pull-off distance.
        pl_data->step_delay = approach ? DEFAULT_STEP_DELAY_HOMING_FEED : DEFAULT_STEP_DELAY_HOMING_SEEK;

        if (approach) {
            max_travel  = homing_pulloff->get() * HOMING_AXIS_LOCATE_SCALAR;
            homing_rate = homing_feed_rate->get();
        } else {
            max_travel  = homing_pulloff->get();
            homing_rate = homing_seek_rate->get();
        }
    } while (n_cycle-- > 0);
    // The active cycle axes should now be homed and machine limits have been located. By
    // default, Grbl defines machine space as all negative, as do most CNCs. Since limit switches
    // can be on either side of an axes, check and set axes machine zero appropriately. Also,
    // set up pull-off maneuver from axes limit switches that have been homed. This provides
    // some initial clearance off the switches and should also help prevent them from falsely
    // triggering when hard limits are enabled or when more than one axes shares a limit pin.
    int32_t set_axis_position;
    // Set machine positions for homed limit switches. Don't update non-homed axes.
    auto mask    = homing_dir_mask->get();
    auto pulloff = homing_pulloff->get();
    for (uint8_t idx = 0; idx < n_axis; idx++) {
        auto steps = axis_settings[idx]->steps_per_mm->get();
        if (cycle_mask & bit(idx)) {
            float travel = axis_settings[idx]->max_travel->get();
            float mpos   = axis_settings[idx]->home_mpos->get();

            if (bit_istrue(homing_dir_mask->get(), bit(idx))) {
                sys_position[idx] = (mpos + pulloff) * steps;
            } else {
                sys_position[idx] = (mpos - pulloff) * steps;
            }
        }
    }
    sys.step_control = {};                      // Return step control to normal operation.
    motor_manager.motors_set_homing_mode(cycle_mask, false);  // tell motors homing is done
    planner.set_ignore_breakpoints( false );
}


uint8_t limit_pin = STEPPERS_LIMIT_ALL_PIN;

void limits_init() {
    int mode   = INPUT_PULLUP;
#ifdef DISABLE_LIMIT_PIN_PULL_UP
    mode = INPUT;
#endif

    pinMode(limit_pin, mode);
    if (hard_limits->get()) {
        attachInterrupt(limit_pin, isr_limit_switches, CHANGE);
    } else {
        detachInterrupt(limit_pin);
    }


    // setup task used for debouncing
    if (limit_sw_queue == NULL) {
        limit_sw_queue = xQueueCreate(10, sizeof(int));
        xTaskCreate(limitCheckTask,
                    "limitCheckTask",
                    2048,
                    NULL,
                    5,  // priority
                    NULL);
    }
}

// Disables hard limits.
void limits_disable() {
    detachInterrupt(limit_pin);
}

// Returns limit state as a bit-wise uint8 variable. Each bit indicates an axis limit, where
// triggered is 1 and not triggered is 0. Invert mask is applied. Axes are defined by their
// number in bit position, i.e. Z_AXIS is bit(2), and Y_AXIS is bit(1).
bool limits_get_state() {

    // only one limit pin for all
    int state = digitalRead( limit_pin );

    if( limit_invert->get() ){
        return state?false:true;
    } else{
        return state?true:false;
    }

}

// Performs a soft limit check. Called from mcline() only. Assumes the machine has been homed,
// the workspace volume is in all negative space, and the system is in normal operation.
// NOTE: Used by jogging to limit travel within soft-limit volume.
void limits_soft_check(float* target) {
    if (limitsCheckTravel(target)) {
        sys.soft_limit = true;
        mc_reset();                                // Issue system reset and ensure spindle and .. are shutdown.
        sys_rt_exec_alarm = ExecAlarm::SoftLimit;  // Indicate soft limit critical event
        protocol_execute_realtime();               // Execute to enter critical event loop and system abort
        return;
    }
}

// this is the task
void limitCheckTask(void* pvParameters) {
    while (true) {
        int evt;
        xQueueReceive(limit_sw_queue, &evt, portMAX_DELAY);  // block until receive queue
        vTaskDelay(DEBOUNCE_PERIOD / portTICK_PERIOD_MS);    // delay a while
        if ( limits_get_state() ) {
            mc_reset();                                // Initiate system kill.
            sys_rt_exec_alarm = ExecAlarm::HardLimit;  // Indicate hard limit critical event
        }
        static UBaseType_t uxHighWaterMark = 0;
    }
}

float limitsMaxPosition(uint8_t axis) {
    float mpos = axis_settings[axis]->home_mpos->get();
    return bitnum_istrue(homing_dir_mask->get(), axis) ? mpos + axis_settings[axis]->max_travel->get() : mpos;
}

float limitsMinPosition(uint8_t axis) {
    float mpos = axis_settings[axis]->home_mpos->get();
    return bitnum_istrue(homing_dir_mask->get(), axis) ? mpos : mpos - axis_settings[axis]->max_travel->get();
}

// Checks and reports if target array exceeds machine travel limits.
// Return true if exceeding limits
// Set $<axis>/MaxTravel=0 to selectively remove an axis from soft limit checks
bool __attribute__((weak)) limitsCheckTravel(float* target) {
    uint8_t idx;
    auto n_axis = N_AXIS;
    for (idx = 0; idx < n_axis; idx++) {
        float max_mpos, min_mpos;

        if ((target[idx] < limitsMinPosition(idx) || target[idx] > limitsMaxPosition(idx)) && axis_settings[idx]->max_travel->get() > 0) {
            return true;
        }
    }
    return false;
}

bool __attribute__((weak)) user_defined_homing(uint8_t cycle_mask) {
    return false;
}

void limitsCheckSoft(float* target) {
    if (soft_limits->get()) {
        // NOTE: Block jog state. Jogging is a special case and soft limits are handled independently.
        if (sys.state != State::Jog && sys.state != State::Homing) {
            limits_soft_check(target);
        }
    }
}
