/****************************************************************************
 * arch/risc-v/src/common/riscv_backtrace.c
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/arch.h>
#include "sched/sched.h"

#include "riscv_internal.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: getfp
 *
 * Description:
 *  getfp() returns current frame pointer
 *
 ****************************************************************************/

static inline uintptr_t getfp(void)
{
  register uintptr_t fp;

  __asm__
  (
    "\tadd  %0, x0, fp\n"
    : "=r"(fp)
  );

  return fp;
}

/****************************************************************************
 * Name: backtrace
 *
 * Description:
 *  backtrace() parsing the return address through frame pointer
 *
 ****************************************************************************/

static int backtrace(FAR uintptr_t *base, FAR uintptr_t *limit,
                     FAR uintptr_t *fp, FAR uintptr_t *ra,
                     FAR void **buffer, int size)
{
  int i = 0;

  if (ra)
    {
      buffer[i++] = ra;
    }

  for (; i < size; fp = (FAR uintptr_t *)*(fp - 2), i++)
    {
      if (fp > limit || fp < base)
        {
          break;
        }

      ra = (FAR uintptr_t *)*(fp - 1);
      if (ra == NULL)
        {
          break;
        }

      buffer[i] = ra;
    }

  return i;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_backtrace
 *
 * Description:
 *  up_backtrace()  returns  a backtrace for the TCB, in the array
 *  pointed to by buffer.  A backtrace is the series of currently active
 *  function calls for the program.  Each item in the array pointed to by
 *  buffer is of type void *, and is the return address from the
 *  corresponding stack frame.  The size argument specifies the maximum
 *  number of addresses that can be stored in buffer.   If  the backtrace is
 *  larger than size, then the addresses corresponding to the size most
 *  recent function calls are returned; to obtain the complete backtrace,
 *  make sure that buffer and size are large enough.
 *
 * Input Parameters:
 *   tcb    - Address of the task's TCB
 *   buffer - Return address from the corresponding stack frame
 *   size   - Maximum number of addresses that can be stored in buffer
 *
 * Returned Value:
 *   up_backtrace() returns the number of addresses returned in buffer
 *
 ****************************************************************************/

int up_backtrace(FAR struct tcb_s *tcb, FAR void **buffer, int size)
{
  FAR struct tcb_s *rtcb = running_task();
  irqstate_t flags;
  int ret;

  if (size <= 0 || !buffer)
    {
      return 0;
    }

  if (tcb == NULL || tcb == rtcb)
    {
      if (up_interrupt_context())
        {
#if CONFIG_ARCH_INTERRUPTSTACK > 15
          ret = backtrace((FAR void *)&g_intstackalloc,
                          (FAR void *)((uint32_t)&g_intstackalloc +
                                       CONFIG_ARCH_INTERRUPTSTACK),
                          (FAR void *)getfp(), NULL, buffer, size);
#else
          ret = backtrace(rtcb->stack_base_ptr,
                          rtcb->stack_base_ptr + rtcb->adj_stack_size,
                          (FAR void *)getfp(), NULL, buffer, size);
#endif
          if (ret < size)
            {
              ret += backtrace(rtcb->stack_base_ptr,
                               rtcb->stack_base_ptr +
                               rtcb->adj_stack_size,
                               (FAR void *)CURRENT_REGS[REG_FP],
                               (FAR void *)CURRENT_REGS[REG_EPC],
                               &buffer[ret], size - ret);
            }
        }
      else
        {
          ret = backtrace(rtcb->stack_base_ptr,
                          rtcb->stack_base_ptr + rtcb->adj_stack_size,
                          (FAR void *)getfp(), NULL, buffer, size);
        }
    }
  else
    {
      flags = enter_critical_section();

      ret = backtrace(tcb->stack_base_ptr,
                      tcb->stack_base_ptr + tcb->adj_stack_size,
                      (FAR void *)tcb->xcp.regs[REG_FP],
                      (FAR void *)tcb->xcp.regs[REG_EPC],
                      buffer, size);

      leave_critical_section(flags);
    }

  return ret;
}
