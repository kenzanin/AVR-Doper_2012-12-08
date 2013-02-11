/*
 * Name: serial.c
 * Project: AVR-Doper
 * Author: Christian Starkjohann <cs@obdev.at>
 * Creation Date: 2006-07-10
 * Tabsize: 4
 * Copyright: (c) 2006 by Christian Starkjohann, all rights reserved.
 * License: GNU GPL v2 (see License.txt) or proprietary (CommercialLicense.txt)
 * Revision: $Id$
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include "serial.h"
#include "hardware.h"

#if ENABLE_DEBUG_INTERFACE
ringBuffer_t    serialRingBuffer;
static uchar    UCSRBMirror;

/* ATMega 48/88/168 uses different vector name for Rx interrupt */
#ifndef USART_RXC_vect
#   define USART_RXC_vect    USART_RX_vect
#endif

ISR(USART_RXC_vect, ISR_NAKED)
{
    // be careful not to modify any registers or SREG flags
    asm volatile(
        "lds    r2, %0\n\t"
        "sts    %1, r2\n\t"
        "sei\n\t"   // run with global interrupts enabled, but USART RX disabled
        ";rjmp   __vector_Receiver [commented out because fallthrough]"
        :           // no output operands
        : "i" (&UCSRBMirror), "i" (&UCSRB)
    );
}
/* The former ISR is naked and has no reti or rjmp and thus falls through into
 * the following pseudo-ISR.
 */
ISR(__vector_Receiver)
{
    ringBufferWrite(&serialRingBuffer, UDR);
    UCSRB = UCSRBMirror | _BV(RXCIE);   /* re-enable USART RX before we return */
}

void  serialInit(void)
{
    UBRRL = F_CPU / (HW_DEBUG_BAUDRATE * 16L) - 1;
    UCSRB = UCSRBMirror = (1 << TXEN) | (1 << RXEN) | (1 << RXCIE); /* enable rx, tx and rx interrupt */
}

#endif
