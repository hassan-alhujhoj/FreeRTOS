/*
 * Main source file for ENCE464 Heli project
 *
 *  Created on: 27/07/2020
 *      Authors: tch118, ...
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include "inc/hw_memmap.h"
#include "inc/hw_types.h"
#include "inc/hw_ints.h"

#include "driverlib/pin_map.h"
#include "driverlib/debug.h"
#include "driverlib/gpio.h"
#include "driverlib/sysctl.h"
#include "driverlib/interrupt.h"
#include "driverlib/uart.h"
#include "utils/ustdlib.h"
#include "stdlib.h"

//#include "inc/tm4c123gh6pm.h"
#include "driverlib/pwm.h"

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "OrbitOLED/OrbitOLEDInterface.h"
#include "buttons4.h"

#include "OLEDDisplay.h"
#include "constants.h"
#include "buffer.h"
#include "myFreeRTOS.h"
#include "myMotors.h"
#include "myYaw.h"
#include "altitude.h"
#include "controllers.h"
#include "myButtons.h"


//******************************************************************
// Global Variables
//******************************************************************
static uint8_t targetAlt;
static int16_t targetYaw;


//******************************************************************
// Functions
//******************************************************************
void displayOLED(void* pvParameters) {


    char text_buffer[16];
    while(1) {
        // Display Height
        sprintf(text_buffer, "Altitude: %d%%", getAlt());
        writeDisplay(text_buffer, LINE_1);

        // Display yaw
        sprintf(text_buffer, "Yaw: %d", getYaw());
        writeDisplay(text_buffer, LINE_2);

        sprintf(text_buffer, "Target Alt: %d%%", targetAlt);
        writeDisplay(text_buffer, LINE_3);

        sprintf(text_buffer, "Target Yaw: %d", targetYaw);
        writeDisplay(text_buffer, LINE_4);

        taskDelayMS(1000/DISPLAY_RATE_HZ);
    }
}


void pollButton(void* pvParameters) {
    targetAlt = 0;
    targetYaw = 0;
    xButtPollSemaphore = xSemaphoreCreateBinary();

    xSemaphoreTake(xButtPollSemaphore, portMAX_DELAY);
    while (1) {
        updateButtons();
        if (checkButton (UP) == PUSHED) {
            if (targetAlt != 100) {
                targetAlt += 10;
            }

        } else if (checkButton (DOWN) == PUSHED) {
            if (!targetAlt == 0){
                targetAlt -= 10;
            }

        } else if (checkButton (LEFT) == PUSHED) {
            if (targetYaw == 0) {
                targetYaw = 345;
            } else {
                targetYaw -= 15;
            }

        } else if (checkButton (RIGHT) == PUSHED) {
            if (targetYaw == 345) {
                targetYaw = 0;
            } else {
                targetYaw += 15;
            }
        }

        taskDelayMS(1000/BUTTON_POLL_RATE_HZ);
    }
}


void controller(void* pvParameters) {
    xControlSemaphore = xSemaphoreCreateBinary();

    xSemaphoreTake(xControlSemaphore, portMAX_DELAY);
    targetYaw = getReference();
    targetAlt = 10;
    while(1) {

        piMainUpdate(targetAlt);
        piTailUpdate(targetYaw);

        taskDelayMS(1000/CONTROLLER_RATE_HZ);
    }
}


//********************************************************
// initialiseUSB_UART - 8 bits, 1 stop bit, no parity
//********************************************************
void
initialiseUSB_UART (void)
{
    //
    // Enable GPIO port A which is used for UART0 pins.
    //
    SysCtlPeripheralEnable(UART_USB_PERIPH_UART);
    SysCtlPeripheralEnable(UART_USB_PERIPH_GPIO);
    //
    // Select the alternate (UART) function for these pins.
    //
    GPIOPinTypeUART(UART_USB_GPIO_BASE, UART_USB_GPIO_PINS);
    GPIOPinConfigure (GPIO_PA0_U0RX);
    GPIOPinConfigure (GPIO_PA1_U0TX);

    UARTConfigSetExpClk(UART_USB_BASE, SysCtlClockGet(), BAUD_RATE,
            UART_CONFIG_WLEN_8 | UART_CONFIG_STOP_ONE |
            UART_CONFIG_PAR_NONE);
    UARTFIFOEnable(UART_USB_BASE);
    UARTEnable(UART_USB_BASE);
}


//**********************************************************************
// Transmit a string via UART0
//**********************************************************************
void
UARTSend (char *pucBuffer)
{
    // Loop while there are more characters to send.
    while(*pucBuffer)
    {
        // Write the next character to the UART Tx FIFO.
        UARTCharPut(UART_USB_BASE, *pucBuffer);
        pucBuffer++;
    }
}


// Function to update UART communications
void sendData(void* pvParameters) {
    char statusStr[16 + 1];

    while(1) {
        // Form and send a status message to the console
        sprintf (statusStr, "Alt %d [%d] \r\n", getAlt(), targetAlt); // * usprintf
        UARTSend (statusStr);
        sprintf (statusStr, "Yaw %d [%d] \r\n", getYaw(), targetYaw); // * usprintf
        UARTSend (statusStr);
        sprintf (statusStr, "Main %d Tail %d \r\n", getPWM(), getPWM() ); // * usprintf
        UARTSend (statusStr);

/*        if (heli_state == landing) {
            usprintf (statusStr, "Mode landing \r\n");
        } else if (heli_state == landed) {
            usprintf (statusStr, "Mode landed \r\n");
        } else if (heli_state == take_off) {
            usprintf (statusStr, "Mode take off \r\n");
        } else {
            usprintf (statusStr, "Mode in flight \r\n");
        }
        UARTSend (statusStr);*/
        taskDelayMS(1000/2);
    }
}


void createTasks(void) {
    createTask(pollButton, "Button Poll", 200, (void *) NULL, 3, NULL);
    createTask(displayOLED, "display", 200, (void *) NULL, 3, NULL);
    createTask(controller, "controller", 56, (void *) NULL, 2, NULL);
    createTask(processAlt, "Altitude Calc", 128, (void *) NULL, 4, NULL);
    createTask(sendData, "UART", 200, (void *) NULL, 5, NULL);
    createTask(takeOff, "Take off sequence", 56, (void *) NULL, 3, NULL);
}


// Initialize the program
void initialize(void) {
    // Set clock to 80MHz
    SysCtlClockSet (SYSCTL_SYSDIV_2_5 | SYSCTL_USE_PLL | SYSCTL_OSC_MAIN | SYSCTL_XTAL_16MHZ);

    initButtons();
    initModeSwitch();
    initADC();
    initDisplay();
    initBuffer();
    initMotors();
    initYaw();
    createTasks();
    initialiseUSB_UART();

    //// BUTTONS...
    GPIOPinTypeGPIOInput (LEFT_BUT_PORT_BASE, LEFT_BUT_PIN);
    GPIOPadConfigSet (LEFT_BUT_PORT_BASE, LEFT_BUT_PIN, GPIO_STRENGTH_2MA,
       GPIO_PIN_TYPE_STD_WPU);


    //---Unlock PF0 for the right button:
    GPIO_PORTF_LOCK_R = GPIO_LOCK_KEY;
    GPIO_PORTF_CR_R |= GPIO_PIN_0; //PF0 unlocked
    GPIO_PORTF_LOCK_R = GPIO_LOCK_M;
    GPIOPinTypeGPIOInput (RIGHT_BUT_PORT_BASE, RIGHT_BUT_PIN);
    GPIOPadConfigSet (RIGHT_BUT_PORT_BASE, RIGHT_BUT_PIN, GPIO_STRENGTH_2MA,
       GPIO_PIN_TYPE_STD_WPU);
    ////


    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOC);                // For Reference signal
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOC));

    GPIOPinWrite(LED_GPIO_BASE, LED_RED_PIN, 0x00);               // off by default
    GPIOPinTypeGPIOOutput(GPIO_PORTF_BASE, GPIO_PIN_3);         // PF_1 as output
    GPIOPadConfigSet(GPIO_PORTF_BASE, GPIO_PIN_3, GPIO_STRENGTH_4MA, GPIO_PIN_TYPE_STD);    // doesn't need too much drive strength as the RGB LEDs on the TM4C123 launchpad are switched via N-type transistors
    GPIOPinWrite(LED_GPIO_BASE, LED_GREEN_PIN, 0x00);

    IntMasterEnable();
}


void main(void) {
    initialize();
    startFreeRTOS();
}
