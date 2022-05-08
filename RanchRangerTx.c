/*
 * Copyright (c) 2017-2018, Texas Instruments Incorporated
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * *  Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * *  Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * *  Neither the name of Texas Instruments Incorporated nor the names of
 *    its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

 /* Standard C Libraries */
#include <stdlib.h>
#include <stdio.h>

/* XDCtools Header files */
#include <xdc/std.h>
#include <xdc/runtime/Assert.h>
#include <xdc/runtime/Error.h>
#include <xdc/runtime/System.h>

/* BIOS Header files */
#include <ti/sysbios/BIOS.h>
#include <ti/sysbios/knl/Task.h>
#include <ti/sysbios/knl/Semaphore.h>
#include <ti/sysbios/knl/Clock.h>

/* TI-RTOS Header files */
#include <ti/drivers/PIN.h>

#include <ti/devices/cc13x0/inc/hw_fcfg1.h>

/* Board Header files */
#include "Board.h"

/* Sensor Controller Studio generated header */
#include "scif.h"

/* EasyLink API Header files */
#include "easylink/EasyLink.h"

/* Application header files */
#include "smartrf_settings/smartrf_settings.h"

#define RANCHRANGERTX_TASK_STACK_SIZE    1024
#define RANCHRANGERTX_TASK_PRIORITY      2

/* https://e2e.ti.com/support/wireless-connectivity/sub-1-ghz-group/sub-1-ghz/f/sub-1-ghz-forum/1045315/cc1310-waking-up-the-cpu-using-the-sensor-controller */
Semaphore_Struct scsSemaphore;
Semaphore_Handle scsSemaphoreHandle;

Task_Struct txTask;    /* not static so you can see in ROV */
Task_Params txTaskParams;
uint8_t txTaskStack[RANCHRANGERTX_TASK_STACK_SIZE];

/* Pin driver handle */
PIN_Handle ledPinHandle;
PIN_Handle dipswitchPinHandle;
//PIN_Handle buttonWakeUpHandle;
PIN_State ledPinState;
PIN_State dipswitchPinState;
//PIN_State buttonWakeUpState;

/*
 * Application LED pin configuration table:
 *   - All LEDs board LEDs are off.
 */
PIN_Config ledPinTable[] = {
    Board_PIN_LED1 | PIN_GPIO_OUTPUT_EN | PIN_GPIO_LOW | PIN_PUSHPULL | PIN_DRVSTR_MAX,
    Board_PIN_LED2 | PIN_GPIO_OUTPUT_EN | PIN_GPIO_LOW | PIN_PUSHPULL | PIN_DRVSTR_MAX,
    PIN_TERMINATE
};

PIN_Config dipswitchPinTable[] = {
    Board_DIO1      | PIN_INPUT_EN | PIN_PULLDOWN,
    Board_DIO12     | PIN_INPUT_EN | PIN_PULLDOWN,
    Board_DIO15     | PIN_INPUT_EN | PIN_PULLDOWN,
    Board_DIO16_TDO | PIN_INPUT_EN | PIN_PULLDOWN,
    Board_DIO17_TDI | PIN_INPUT_EN | PIN_PULLDOWN,
    Board_DIO21     | PIN_INPUT_EN | PIN_PULLDOWN,
    PIN_TERMINATE
};
//
//PIN_Config buttonWakeUpTable[] = {
//    Board_PIN_BUTTON0 | PIN_INPUT_EN | PIN_PULLUP | PIN_IRQ_NEGEDGE,
//    PIN_TERMINATE
//};

typedef struct __attribute__((__packed__))
{
    uint16_t seqNumber;
    uint64_t targetID;
    uint8_t dipSwitchState;
} RanchRangerPayload;

// SCIF driver callback: Task control interface ready (non-blocking task control operation completed)
void scCtrlReadyCallback(void)
{
}

void scTaskAlertCallback(void)
{
    // Clear the ALERT interrupt source
    scifClearAlertIntSource();

    // Acknowledge the ALERT event
    scifAckAlertEvents();

    // Wake up the OS task
    Semaphore_post(scsSemaphoreHandle);
} // taskAlertCallback

void buttonCb(PIN_Handle handle, PIN_Id pinId)
{
    // Clear the ALERT interrupt source
//    scifClearAlertIntSource();

    // Acknowledge the ALERT event
//    scifAckAlertEvents();

    // Wake up the OS task
    Semaphore_post(scsSemaphoreHandle);
} // taskAlertCallback

static void ranchRangerTxFnx(UArg arg0, UArg arg1)
{
    // Initialize the SCIF operating system abstraction layer
    scifOsalInit();
    scifOsalRegisterCtrlReadyCallback(scCtrlReadyCallback);
    scifOsalRegisterTaskAlertCallback(scTaskAlertCallback);

    // Initialize the SCIF driver
    scifInit(&scifDriverSetup);

    // Start the "Target Hit Sensor" Sensor Controller task
    scifStartTasksNbl(1 << SCIF_TARGET_HIT_SENSOR_TASK_ID);

    EasyLink_TxPacket txPacket =  { {0}, 0, 0, {0} };

    // Initialize the EasyLink parameters to their default values
    EasyLink_Params easyLink_params;
    EasyLink_Params_init(&easyLink_params);

    /*
     * Initialize EasyLink with the settings found in easylink_config.h
     * Modify EASYLINK_PARAM_CONFIG in easylink_config.h to change the default
     * PHY
     */
    if(EasyLink_init(&easyLink_params) != EasyLink_Status_Success)
    {
        System_abort("EasyLink_init failed");
    }

    // Set up our transfer packet
    txPacket.absTime = 0; // Send immediately
    txPacket.len = sizeof(RanchRangerPayload);
    /*
     * Address filtering is enabled by default on the Rx device with the
     * an address of 0xAA. This device must set the dstAddr accordingly.
     */
    txPacket.dstAddr[0] = 0xAA;
    RanchRangerPayload * payload = (RanchRangerPayload*)&txPacket.payload;
    payload->seqNumber = 0;
    // We need a unique identifier per target.  Use the mac address.
    payload->targetID = *((uint64_t *)(FCFG1_BASE + FCFG1_O_MAC_15_4_0)) & 0xFFFFFFFFFFFF;

    /*
     * If you wish to use a frequency other than the default, use
     * the following API:
     * EasyLink_setFrequency(868000000);
     */

    while(1) {
        Semaphore_pend(scsSemaphoreHandle, BIOS_WAIT_FOREVER);

        payload->seqNumber++;
        payload->dipSwitchState = 0;
        payload->dipSwitchState |= PIN_getInputValue(Board_DIO1);
        payload->dipSwitchState |= PIN_getInputValue(Board_DIO12) << 1;
        payload->dipSwitchState |= PIN_getInputValue(Board_DIO15) << 2;
        payload->dipSwitchState |= PIN_getInputValue(Board_DIO16_TDO) << 3;
        payload->dipSwitchState |= PIN_getInputValue(Board_DIO17_TDI) << 4;
        payload->dipSwitchState |= PIN_getInputValue(Board_DIO21) << 5;

//        /* Add a Tx delay for > 500ms, so that the abort kicks in and brakes the burst */
//        uint32_t absTime;
//        if(EasyLink_getAbsTime(&absTime) != EasyLink_Status_Success)
//        {
//            // Problem getting absolute time
//        }
//        if(txBurstSize++ >= 10)
//        {
//          /* Set Tx absolute time to current time + 1s */
//          txPacket.absTime = absTime + EasyLink_ms_To_RadioTime(1000);
//          txBurstSize = 0;
//        }
//        /* Else set the next packet in burst to Tx in 100ms */
//        else
//        {
//          /* Set Tx absolute time to current time + 100ms */
//          txPacket.absTime = absTime + EasyLink_ms_To_RadioTime(100);
//        }

        EasyLink_Status result = EasyLink_transmit(&txPacket);

        if (result == EasyLink_Status_Success)
        {
            /* Toggle LED1 to indicate TX */
            PIN_setOutputValue(ledPinHandle, Board_PIN_LED1,!PIN_getOutputValue(Board_PIN_LED1));
        }
        else
        {
            /* Toggle LED1 and LED2 to indicate error */
            PIN_setOutputValue(ledPinHandle, Board_PIN_LED1,!PIN_getOutputValue(Board_PIN_LED1));
            PIN_setOutputValue(ledPinHandle, Board_PIN_LED2,!PIN_getOutputValue(Board_PIN_LED2));
        }
    }
}

int main(void)
{
    /* Call driver init functions. */
    Board_initGeneral();

    /* Initialize Sensor Controller semaphore */
    Semaphore_construct(&scsSemaphore, 0, NULL); // NULL = Use default params
    scsSemaphoreHandle = Semaphore_handle(&scsSemaphore);

    /* Open LED pins */
    ledPinHandle = PIN_open(&ledPinState, ledPinTable);
    Assert_isTrue(ledPinHandle != NULL, NULL);

    /* Open DIP switch pins */
    dipswitchPinHandle = PIN_open(&dipswitchPinState, dipswitchPinTable);
    Assert_isTrue(dipswitchPinHandle != NULL, NULL);

//    /* Setup button pins with ISR */
//    buttonWakeUpHandle = PIN_open(&buttonWakeUpState, buttonWakeUpTable);
//    PIN_registerIntCb(buttonWakeUpHandle, buttonCb);

    /* Clear LED pins */
    PIN_setOutputValue(ledPinHandle, Board_PIN_LED1, 0);
    PIN_setOutputValue(ledPinHandle, Board_PIN_LED2, 0);

    Task_Params_init(&txTaskParams);
    txTaskParams.stackSize = RANCHRANGERTX_TASK_STACK_SIZE;
    txTaskParams.priority = RANCHRANGERTX_TASK_PRIORITY;
    txTaskParams.stack = &txTaskStack;
    txTaskParams.arg0 = (UInt)1000000;

    Task_construct(&txTask, ranchRangerTxFnx, &txTaskParams, NULL);

    /* Start BIOS */
    BIOS_start();

    return (0);
}
