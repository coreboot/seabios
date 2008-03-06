/*
 * smm.h
 * 
 * Copyright (C) 2002  MandrakeSoft S.A.
 * Copyright (C) 2008  Nguyen Anh Quynh <aquynh@gmail.com>
 * 
 * This file may be distributed under the terms of the GNU GPLv3 license.
 */

#ifndef __SMM_H
#define __SMM_H

#include "pci.h"

void smm_init(PCIDevice *d);

#endif	/* __SMM_H */
