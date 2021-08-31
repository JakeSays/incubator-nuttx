/****************************************************************************
 * boards/arm/sama5/jupiter_nano/src/sam_autoleds.c
 *
 *  Licensed to the Apache Software Foundation (ASF) under one or more
 *  contributor license agreements.  See the NOTICE file distributed with
 *  this work for additional information regarding copyright ownership.  The
 *  ASF licenses this file to you under the Apache License, Version 2.0 (the
 *  "License"); you may not use this file except in compliance with the
 *  License.  You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 *  WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 *  License for the specific language governing permissions and limitations
 *  under the License.
 *
 ****************************************************************************/

/* There is a blue status LED on board the Jupiter Nano. It is driven
 * driven by pin PA6. The LED is connected to ground so bringing the LED high
 * will illuminate the LED.
 *
 *   ------------------------------ ------------------- ---------------------
 *   SAMA5D2 PIO                    SIGNAL              USAGE
 *   ------------------------------ ------------------- ---------------------
 *   PA6                            STATUS_LED          Blue LED
 *   ------------------------------ ------------------- ---------------------
 *
 *
 * This LED is not used by the board port unless CONFIG_ARCH_LEDS is
 * defined.  In that case, the usage by the board port is defined in
 * include/board.h and src/sam_leds.c. The LEDs are used to encode OS-related
 * events as follows.  Note that only the blue LED is used. 
 *
 *   SYMBOL              Meaning                 Blue LED
 *   ------------------- ----------------------- ---------
 *   LED_STARTED         NuttX has been started  OFF
 *   LED_HEAPALLOCATE    Heap has been allocated OFF
 *   LED_IRQSENABLED     Interrupts enabled      OFF
 *   LED_STACKCREATED    Idle stack created      ON
 *   LED_INIRQ           In an interrupt         N/C
 *   LED_SIGNAL          In a signal handler     N/C
 *   LED_ASSERTION       An assertion failed     N/C
 *   LED_PANIC           The system has crashed  FLASH
 *
 * Thus if the Blue LED is statically on, NuttX has successfully  booted
 * and is, apparently, running normally.  If LED is flashing at
 * approximately 2Hz, then a fatal error has been detected and the system
 * has halted.
 */

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <debug.h>

#include <nuttx/board.h>
#include <arch/board/board.h>

#include "sam_pio.h"
#include "jupiter-nano.h"

#ifdef CONFIG_ARCH_LEDS

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: board_autoled_initialize
 ****************************************************************************/

void board_autoled_initialize(void)
{
  /* Configure LED PIOs for output */

  sam_configpio(PIO_LED_BLUE);
}

/****************************************************************************
 * Name: board_autoled_on
 ****************************************************************************/

void board_autoled_on(int led)
{
  switch (led)
    {
      default:
      case 0:  /* LED_STARTED, LED_HEAPALLOCATE, LED_IRQSENABLED */
        break; /* Leave blue LED off */

      case 1:   /* LED_STACKCREATED */
      case 3:   /* LED_PANIC */
        {
          /* blue LED is ON (high illuminates) */

          sam_piowrite(PIO_LED_BLUE, true);
        }
        break;

      case 2:  /* LED_INIRQ, LED_SIGNAL, LED_ASSERTION */
        break; /* No change */
    }
}

/****************************************************************************
 * Name: board_autoled_off
 ****************************************************************************/

void board_autoled_off(int led)
{
  switch (led)
    {
      default:
      case 0:  /* LED_STARTED, LED_HEAPALLOCATE, LED_IRQSENABLED,  */
      case 1:  /* LED_STACKCREATED */
        break; /* Will not happen */

      case 2:  /* LED_INIRQ, LED_SIGNAL, LED_ASSERTION */
        break; /* No change */

      case 3:  /* LED_PANIC */
        {
          /* LED is OFF (high illuminates) */

          sam_piowrite(PIO_LED_BLUE, false);
        }
        break;
    }
}

#endif /* CONFIG_ARCH_LEDS */
