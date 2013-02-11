/*
 * Name: hvprog.c
 * Project: AVR-Doper
 * Author: Christian Starkjohann <cs@obdev.at>
 * Creation Date: 2006-07-07
 * Tabsize: 4
 * Copyright: (c) 2006 by Christian Starkjohann, all rights reserved.
 * License: GNU GPL v2 (see License.txt) or proprietary (CommercialLicense.txt)
 * Revision: $Id$
 */

#include "hardware.h"

#if ENABLE_HVPROG
#include <avr/io.h>
#include <avr/wdt.h>
#include "timer.h"
#include "utils.h"
#include "hvprog.h"


/* This module can handle high voltage serial and parallel programming. Serial
 * programming can be understood as a serial interface to parallel programming:
 * SDI represents programming data and SII a serial input for the control
 * input lines /OE, /WR, BS1, XA0, XA1, PAGEL, and BS2.
 * In order to handle both with similar code, we define the function
 * hvSetControlAndData() which takes care of the underlying mechanism.
 */

/* control lines for high voltage serial (and parallel) programming */
/*
    7       6       5       4    |  3       2       1       0
    n/a     XA1     XA0     BS1  |  /WR     /OE     BS2     PAGEL
*/
#define HVCTL_PAGEL (1 << 0)
#define HVCTL_BS2   (1 << 1)
#define HVCTL_nOE   (1 << 2)
#define HVCTL_nWR   (1 << 3)
#define HVCTL_BS1   (1 << 4)
#define HVCTL_XA0   (1 << 5)
#define HVCTL_XA1   (1 << 6)

/* actions: */
#define HV_ADDR     0
#define HV_DATA     HVCTL_XA0
#define HV_CMD      HVCTL_XA1
#define HV_NONE     (HVCTL_XA1 | HVCTL_XA0)
#define HV_PAGEL    (HVCTL_XA1 | HVCTL_XA0 | HVCTL_PAGEL)

/* bytes: */
#define HV_LOW      0
#define HV_HIGH     HVCTL_BS1
#define HV_EXT      HVCTL_BS2
#define HV_EXT2     (HVCTL_BS1 | HVCTL_BS2)

/* modes: */
#define HV_READ     HVCTL_nWR
#define HV_WRITE    HVCTL_nOE
#define HV_NORW     (HVCTL_nWR | HVCTL_nOE)

#define HVCTL(action, byte, mode)   ((action) | (byte) | (mode))


/* high voltage parallel and serial programming commands */
#define HVCMD_CHIP_ERASE    0x80
#define HVCMD_WRITE_FUSE    0x40
#define HVCMD_WRITE_LOCK    0x20
#define HVCMD_WRITE_FLASH   0x10
#define HVCMD_WRITE_EEPROM  0x11
#define HVCMD_READ_SIGCAL   0x08
#define HVCMD_READ_FUSELCK  0x04
#define HVCMD_READ_FLASH    0x02
#define HVCMD_READ_EEPROM   0x03
#define HVCMD_NOP           0x00

/* ------------------------------------------------------------------------- */

static uchar    progModeIsPp;   /* use parallel programming primitives */
static uchar    hvPollTimeout;

/*
Implementing High Voltage Parallel Programming:
4 functions have to be implemented for HVPP:
ppEnterProgmode()
    This function brings the port lines into the state required for programming.
ppLeaveProgmode()
    This function turns off all signals to the target device.
ppExecute()
    This function reads data, sets data and sets control lines according to
    the parameters passed to the function.
ppPoll()
    This function polls for "programming ready".
You may have to add global variables to communicate additional parameters
such as e.g. write pulse width. See the hvsp* function implementation for
more information.
*/

/* ------------------------------------------------------------------------- */

#define nop2()    asm volatile("rjmp .+0"::)

static uchar hvspExecute(uchar ctlLines, uchar data)
{
uchar   cnt, r = 0, port;

    port = PORT_OUT(HWPIN_HVSP_SII) & ~((1<<PORT_BIT(HWPIN_HVSP_SII)) | (1<<PORT_BIT(HWPIN_HVSP_SDI)));
    PORT_OUT(HWPIN_HVSP_SII) = port;
    PORT_PIN_SET(HWPIN_HVSP_SCI);
    cnt = 8;
    nop2();
    PORT_PIN_CLR(HWPIN_HVSP_SCI);
    do{
        r <<= 1;
        if(PORT_PIN_VALUE(HWPIN_HVSP_SDO))
            r |= 1;
        if(data & 0x80)
            port |= 1 << PORT_BIT(HWPIN_HVSP_SDI);
        if(ctlLines & 0x80)
            port |= 1 << PORT_BIT(HWPIN_HVSP_SII);
        PORT_OUT(HWPIN_HVSP_SII) = port;
        PORT_PIN_SET(HWPIN_HVSP_SCI);
        port &= ~((1<<PORT_BIT(HWPIN_HVSP_SII)) | (1<<PORT_BIT(HWPIN_HVSP_SDI)));
        ctlLines <<= 1;
        data <<= 1;
        nop2();
        PORT_PIN_CLR(HWPIN_HVSP_SCI);
    }while(--cnt);
    PORT_OUT(HWPIN_HVSP_SII) = port;
    /* clock out two zeros */
    PORT_PIN_SET(HWPIN_HVSP_SCI);
    nop2();
    nop2();
    PORT_PIN_CLR(HWPIN_HVSP_SCI);
    nop2();
    nop2();
    PORT_PIN_SET(HWPIN_HVSP_SCI);
    nop2();
    nop2();
    PORT_PIN_CLR(HWPIN_HVSP_SCI);
    return r;
}

/* This function applies 'data' to the data lines, 'ctlLines' to the control
 * lines and returns the status of the data lines BEFORE any control lines
 * were changed. These somewhat strange semantics are required for
 * compatibility with HV serial programming.
 */
static uchar hvSetControlAndData(uchar ctlLines, uchar data)
{
    /* ### insert if(progModeIsPp){}else{} here */
    return hvspExecute(ctlLines, data);
}

/* ------------------------------------------------------------------------- */

#if 1
void    hvspEnterProgmode(stkEnterProgHvsp_t *param)
{
    progModeIsPp = 0;
    PORT_PIN_SET(HWPIN_LED);
    TCCR2 &= ~(1 << COM20);             /* clear toggle on compare match mode */
    PORT_PIN_SET(HWPIN_HVSP_RESET);
    PORT_PIN_CLR(HWPIN_HVSP_SUPPLY);
    PORT_DDR(HWPIN_HVSP_SDO) |= (1 << PORT_BIT(HWPIN_HVSP_SDO));
    TIMER_US_DELAY(40);
    PORT_PIN_CLR(HWPIN_HVSP_RESET);
    PORT_DDR_CLR(HWPIN_HVSP_HVRESET);   /* use internal pull-up to source current */
    PORT_PIN_SET(HWPIN_HVSP_HVRESET);
    TIMER_US_DELAY(15);
    PORT_DDR(HWPIN_HVSP_SDO) &= ~(1 << PORT_BIT(HWPIN_HVSP_SDO));   /* prevent contention */
    TIMER_US_DELAY(300);
}
#else
/* This is the new mechanism to enter prog mode which is closer to the method
 * described in the ATTiny45 data sheet, but it seems to fail on some of the
 * ATTiny45. We therefore stick with the old method, but leave this code
 * for reference.
 */
void    hvspEnterProgmode(stkEnterProgHvsp_t *param)
{
uchar i;

    progModeIsPp = 0;
    PORT_PIN_SET(HWPIN_LED);
    TCCR2 &= ~(1 << COM20);             /* clear toggle on compare match mode */
    PORT_PIN_SET(HWPIN_HVSP_RESET);
    PORT_PIN_CLR(HWPIN_HVSP_SUPPLY);
    for(i = 0; i < 16; i++){    /* ATTiny[248]4 data sheet says: toggle SCI at least 6 times */
        TIMER_US_DELAY(20);
        PORT_OUT(HWPIN_HVSP_SCI) ^= 1 << PORT_BIT(HWPIN_HVSP_SCI);
    }
    PORT_DDR_SET(HWPIN_HVSP_SDO);
    TIMER_US_DELAY(45);
    PORT_PIN_CLR(HWPIN_HVSP_RESET);
    PORT_DDR_CLR(HWPIN_HVSP_HVRESET);   /* use internal pull-up to source current */
    PORT_PIN_SET(HWPIN_HVSP_HVRESET);
    TIMER_US_DELAY(20);
    PORT_DDR_CLR(HWPIN_HVSP_SDO);       /* prevent contention */
    TIMER_US_DELAY(300);
}
#endif

void    hvspLeaveProgmode(stkLeaveProgHvsp_t *param)
{
    PORT_PIN_CLR(HWPIN_HVSP_HVRESET);
    PORT_DDR_SET(HWPIN_HVSP_HVRESET);   /* output low level */
    PORT_PIN_SET(HWPIN_HVSP_RESET);
    PORT_OUT(HWPIN_HVSP_SII) &= ~((1<<PORT_BIT(HWPIN_HVSP_SII)) | (1<<PORT_BIT(HWPIN_HVSP_SDI)));
    PORT_PIN_SET(HWPIN_HVSP_SUPPLY);
    PORT_PIN_CLR(HWPIN_HVSP_RESET);
    PORT_PIN_CLR(HWPIN_LED);
}

/* ------------------------------------------------------------------------- */

void    ppEnterProgmode(stkEnterProgPp_t *param)
{
    progModeIsPp = 1;
    /* ### not implemented yet */
}

void    ppLeaveProgmode(stkLeaveProgPp_t *param)
{
    /* ### not implemented yet */
}

/* ------------------------------------------------------------------------- */

static uchar    hvspPoll(void)
{
uchar   rval = STK_STATUS_CMD_OK;

    timerSetupTimeout(hvPollTimeout);
    while(!PORT_PIN_VALUE(HWPIN_HVSP_SDO)){
        if(timerTimeoutOccurred()){
            rval = STK_STATUS_CMD_TOUT;
            break;
        }
    }
    return rval;
}

static uchar    hvPoll(void)
{
    /* ### insert if(progModeIsPp){}else{} here */
    return hvspPoll();
}

/* ------------------------------------------------------------------------- */

static uchar   hvChipErase(uchar eraseTime)
{
uchar rval = STK_STATUS_CMD_OK;

    hvSetControlAndData(HVCTL(HV_CMD, HV_LOW, HV_NORW), HVCMD_CHIP_ERASE);
    hvSetControlAndData(HVCTL(HV_NONE, HV_LOW, HV_WRITE), 0);
    hvSetControlAndData(HVCTL(HV_NONE, HV_LOW, HV_NORW), 0);
    if(hvPollTimeout){
        rval = hvPoll();
    }else{
        timerMsDelay(eraseTime);
    }
    hvSetControlAndData(HVCTL(HV_CMD, HV_LOW, HV_NORW), HVCMD_NOP);
    return rval;
}

uchar   hvspChipErase(stkChipEraseHvsp_t *param)
{
    hvPollTimeout = param->pollTimeout;
    return hvChipErase(param->eraseTime);
}

uchar   ppChipErase(stkChipErasePp_t *param)
{
    /* ### set pulse width to global variable */
    hvPollTimeout = param->pollTimeout;
    return hvChipErase(10);
}

/* ------------------------------------------------------------------------- */

#define MODEMASK_PAGEMODE   1
#define MODEMASK_LAST_PAGE  0x40
#define MODEMASK_FLASH_PAGE 0x80

/* len == 0 means 256 bytes */
static uchar    hvProgramMemory(uchar *data, uchar len, uchar mode, uchar isEeprom)
{
uchar   x, pageMask = 0xff, rval = STK_STATUS_CMD_OK;

    x = -(mode >> 1) & 7;
    while(x--)  /* pageMask >>= x is less efficient */
        pageMask >>= 1;
    if(!isEeprom)
        pageMask >>= 1;
    hvSetControlAndData(HVCTL(HV_CMD, HV_LOW, HV_NORW), isEeprom ? HVCMD_WRITE_EEPROM : HVCMD_WRITE_FLASH);
    do{
        wdt_reset();
        hvSetControlAndData(HVCTL(HV_ADDR, HV_LOW, HV_NORW), stkAddress.bytes[0]);
        if(mode & MODEMASK_PAGEMODE){
            hvSetControlAndData(HVCTL(HV_DATA, HV_LOW, HV_NORW), *data++);
            if(isEeprom){
                hvSetControlAndData(HVCTL(HV_PAGEL, HV_LOW, HV_NORW), 0);
                hvSetControlAndData(HVCTL(HV_NONE, HV_LOW, HV_NORW), 0);
            }else{
                hvSetControlAndData(HVCTL(HV_DATA, HV_HIGH, HV_NORW), *data++);
                hvSetControlAndData(HVCTL(HV_PAGEL, HV_HIGH, HV_NORW), 0);
                hvSetControlAndData(HVCTL(HV_NONE, HV_HIGH, HV_NORW), 0);
            }
            x = stkAddress.bytes[0] + 1;    /* enforce byte wide operation */
            if((x & pageMask) == 0 && (mode & MODEMASK_FLASH_PAGE)){
                hvSetControlAndData(HVCTL(HV_ADDR, HV_HIGH, HV_NORW), stkAddress.bytes[1]);
                hvSetControlAndData(HVCTL(HV_NONE, HV_LOW, HV_WRITE), 0);
                hvSetControlAndData(HVCTL(HV_NONE, HV_LOW, HV_NORW), 0);
                rval = hvPoll();
            }
        }else{
            hvSetControlAndData(HVCTL(HV_ADDR, HV_HIGH, HV_NORW), stkAddress.bytes[1]);
            hvSetControlAndData(HVCTL(HV_DATA, HV_LOW, HV_NORW), *data++);
            if(isEeprom){
                hvSetControlAndData(HVCTL(HV_PAGEL, HV_LOW, HV_NORW), 0);
                hvSetControlAndData(HVCTL(HV_NONE, HV_LOW, HV_WRITE), 0);
                hvSetControlAndData(HVCTL(HV_NONE, HV_LOW, HV_NORW), 0);
            }else{
                hvSetControlAndData(HVCTL(HV_NONE, HV_LOW, HV_WRITE), 0);
                hvSetControlAndData(HVCTL(HV_NONE, HV_LOW, HV_NORW), 0);
                if((rval = hvPoll()) != STK_STATUS_CMD_OK)
                    break;
                hvSetControlAndData(HVCTL(HV_DATA, HV_HIGH, HV_NORW), *data++);
                hvSetControlAndData(HVCTL(HV_NONE, HV_HIGH, HV_WRITE), 0);
                hvSetControlAndData(HVCTL(HV_NONE, HV_HIGH, HV_NORW), 0);
            }
            if((rval = hvPoll()) != STK_STATUS_CMD_OK)
                break;
        }
        stkIncrementAddress();
        if(!isEeprom && !--len) /* should not happen, but we would hang indefinitely */
            break;
    }while(--len);
    if(!(mode & MODEMASK_PAGEMODE) || (mode & MODEMASK_LAST_PAGE))
        hvSetControlAndData(HVCTL(HV_CMD, HV_LOW, HV_NORW), HVCMD_NOP);
    return rval;
}

uchar   hvspProgramMemory(stkProgramFlashHvsp_t *param, uchar isEeprom)
{
    hvPollTimeout = param->pollTimeout;
    return hvProgramMemory(param->data, param->numBytes[1], param->mode, isEeprom);
}

/* ------------------------------------------------------------------------- */

static void hvReadMemory(uchar *data, uint len, uchar isEeprom)
{
    hvSetControlAndData(HVCTL(HV_CMD, HV_LOW, HV_NORW), isEeprom ? HVCMD_READ_EEPROM : HVCMD_READ_FLASH);
    while(len-- > 0){
        wdt_reset();
        hvSetControlAndData(HVCTL(HV_ADDR, HV_LOW, HV_NORW), stkAddress.bytes[0]);
        hvSetControlAndData(HVCTL(HV_ADDR, HV_HIGH, HV_NORW), stkAddress.bytes[1]);
        hvSetControlAndData(HVCTL(HV_NONE, HV_LOW, HV_READ), 0);
        *data++ = hvSetControlAndData(HVCTL(HV_NONE, HV_LOW, HV_NORW), 0);
        if(!isEeprom){
            hvSetControlAndData(HVCTL(HV_NONE, HV_HIGH, HV_READ), 0);
            *data++ = hvSetControlAndData(HVCTL(HV_NONE, HV_HIGH, HV_NORW), 0);
            len--;
        }
        stkIncrementAddress();
    }
    *data++ = STK_STATUS_CMD_OK; /* status2 */
}

uint    hvspReadMemory(stkReadFlashHvsp_t *param, stkReadFlashHvspResult_t *result, uchar isEeprom)
{
utilWord_t  numBytes;

    numBytes.bytes[1] = param->numBytes[0];
    numBytes.bytes[0] = param->numBytes[1];
    result->status1 = STK_STATUS_CMD_OK;
    hvReadMemory(result->data, numBytes.word, isEeprom);
    return numBytes.word + 2;
}

/* ------------------------------------------------------------------------- */

static uchar    hvProgramFuse(uchar value, uchar cmd, uchar highLow)
{
uchar   rval;

    hvSetControlAndData(HVCTL(HV_CMD, HV_LOW, HV_NORW), cmd);
    hvSetControlAndData(HVCTL(HV_DATA, HV_LOW, HV_NORW), value);
    hvSetControlAndData(HVCTL(HV_NONE, highLow, HV_WRITE), 0);
    hvSetControlAndData(HVCTL(HV_NONE, highLow, HV_NORW), 0);
    rval = hvPoll();
    hvSetControlAndData(HVCTL(HV_CMD, HV_LOW, HV_NORW), HVCMD_NOP);
    return rval;
}

uchar   hvspProgramFuse(stkProgramFuseHvsp_t *param)
{
uchar   highLow;

    hvPollTimeout = param->pollTimeout;
    if(param->fuseAddress == 0){
        highLow = HV_LOW;
    }else if(param->fuseAddress == 1){
        highLow = HV_HIGH;
    }else{
        highLow = HV_EXT;
    }
    return hvProgramFuse(param->fuseByte, HVCMD_WRITE_FUSE, highLow);
}

uchar   hvspProgramLock(stkProgramFuseHvsp_t *param)
{
    hvPollTimeout = param->pollTimeout;
    return hvProgramFuse(param->fuseByte, HVCMD_WRITE_LOCK, HV_LOW);
}

uchar   ppProgramFuse(stkProgramFusePp_t *param)
{
uchar   highLow;

    /* ### set pulse width to global variable */
    hvPollTimeout = param->pollTimeout;
    if(param->address == 0){
        highLow = HV_LOW;
    }else if(param->address == 1){
        highLow = HV_HIGH;
    }else if(param->address == 2){
        highLow = HV_EXT;
    }else{
        highLow = HV_EXT2;
    }
    return hvProgramFuse(param->data, HVCMD_WRITE_FUSE, highLow);
}

uchar   ppProgramLock(stkProgramFusePp_t *param)
{
    /* ### set pulse width to global variable */
    hvPollTimeout = param->pollTimeout;
    return hvProgramFuse(param->data, HVCMD_WRITE_LOCK, HV_LOW);
}

/* ------------------------------------------------------------------------- */

static uchar    hvReadFuse(uchar highLow)
{
    hvSetControlAndData(HVCTL(HV_CMD, HV_LOW, HV_NORW), HVCMD_READ_FUSELCK);
    hvSetControlAndData(HVCTL(HV_NONE, highLow, HV_READ), 0);
    return hvSetControlAndData(HVCTL(HV_NONE, highLow, HV_NORW), 0);
}

uchar   hvspReadFuse(stkReadFuseHvsp_t *param)
{
uchar   highLow;

    if(param->fuseAddress == 0){
        highLow = HV_LOW;
    }else if(param->fuseAddress == 1){
        highLow = HV_EXT2;
    }else if(param->fuseAddress == 2){
        highLow = HV_EXT;
    }else{
        return STK_STATUS_CMD_FAILED;   /* ### not implemented yet -- which data sheet documents this? */
    }
    return hvReadFuse(highLow);
}

uchar   hvspReadLock(void)
{
    return hvReadFuse(HV_HIGH);
}

/* ------------------------------------------------------------------------- */

static uchar    hvReadSignature(uchar addr, uchar highLow)
{
    hvSetControlAndData(HVCTL(HV_CMD, HV_LOW, HV_NORW), HVCMD_READ_SIGCAL);
    hvSetControlAndData(HVCTL(HV_ADDR, HV_LOW, HV_NORW), addr);
    hvSetControlAndData(HVCTL(HV_NONE, highLow, HV_READ), 0);
    return hvSetControlAndData(HVCTL(HV_NONE, highLow, HV_NORW), 0);
}

uchar   hvspReadSignature(stkReadFuseHvsp_t *param)
{
    return hvReadSignature(param->fuseAddress, HV_LOW);
}

uchar   hvspReadOsccal(void)
{
    return hvReadSignature(0, HV_HIGH);
}

/* ------------------------------------------------------------------------- */

#endif /* ENABLE_HVPROG */
