/*
 * Name: hvprog.h
 * Project: AVR-Doper
 * Author: Christian Starkjohann <cs@obdev.at>
 * Creation Date: 2006-07-07
 * Tabsize: 4
 * Copyright: (c) 2006 by Christian Starkjohann, all rights reserved.
 * License: GNU GPL v2 (see License.txt) or proprietary (CommercialLicense.txt)
 * Revision: $Id$
 */

/*
General Description:
This module implements the STK500v2 primitives for High Voltage serial and
parallel programming. Functions accept parameters directly from the input data
stream and prepare results for the output data stream, where appropriate.
*/

#ifndef __hvprog_h_included__
#define __hvprog_h_included__

#include "stk500protocol.h"


void    hvspEnterProgmode(stkEnterProgHvsp_t *param);
void    hvspLeaveProgmode(stkLeaveProgHvsp_t *param);
uchar   hvspChipErase(stkChipEraseHvsp_t *param);
uchar   hvspProgramMemory(stkProgramFlashHvsp_t *param, uchar isEeprom);
uint    hvspReadMemory(stkReadFlashHvsp_t *param, stkReadFlashHvspResult_t *result, uchar isEeprom);
uchar   hvspProgramFuse(stkProgramFuseHvsp_t *param);
uchar   hvspProgramLock(stkProgramFuseHvsp_t *param);
uchar   hvspReadFuse(stkReadFuseHvsp_t *param);
uchar   hvspReadLock(void);
uchar   hvspReadSignature(stkReadFuseHvsp_t *param);
uchar   hvspReadOsccal(void);

void    ppEnterProgmode(stkEnterProgPp_t *param);
void    ppLeaveProgmode(stkLeaveProgPp_t *param);
uchar   ppChipErase(stkChipErasePp_t *param);
#define ppProgramMemory(param, isEeprom)        hvspProgramMemory(param, isEeprom)
#define ppReadMemory(param, result, isEeprom)   hvspReadMemory(param, result, isEeprom)
uchar   ppProgramFuse(stkProgramFusePp_t *param);
uchar   ppProgramLock(stkProgramFusePp_t *param);
#define ppReadFuse(param)                       hvspReadFuse(param)
#define ppReadLock()                            hvspReadLock()
#define ppReadSignature(param)                  hvspReadSignature(param)
#define ppReadOsccal()                          hvspReadOsccal()

#endif /* __hvprog_h_included__ */
