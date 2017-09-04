/*
 * ES9118C.h  --  ES9118C Soc Audio driver
 *
 * Copyright 2005 Openedhand Ltd.
 *
 * Author: Richard Purdie <richard@openedhand.com>
 *
 * Based on ES9118C.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _ES9118C_H
#define _ES9118C_H

#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/of_device.h>
#include <sound/soc.h>
#include <linux/mutex.h>


/* ES9118C register space */

#define ES9118C_SYSTEM_SETTING    		0x00
#define ES9118C_INPUT_CONFIG   			0x01
#define ES9118C_AUTOMUTE_TIME   		0x04
#define ES9118C_AUTOMUTE_LEVEL   		0x05
#define ES9118C_DEEMPHASIS    			0x06
#define ES9118C_GENERAL_SET   			0x07
#define ES9118C_GPIO_CONFIG      		0x08
#define ES9118C_W_MODE_CONTROL    		0x09
#define ES9118C_V_MODE_CONTROL    		0x0A
#define ES9118C_CHANNEL_MAP    			0x0B
#define ES9118C_DPLL   				    0x0C
#define ES9118C_THD_COMPENSATION   		0x0D
#define ES9118C_SOFT_START	   			0x0E
#define ES9118C_VOLUME1	   			    0x0F
#define ES9118C_VOLUME2	   			    0x10
#define ES9118C_MASTERTRIM0	   			0x11
#define ES9118C_MASTERTRIM1	   			0x12
#define ES9118C_MASTERTRIM2	  			0x13
#define ES9118C_MASTERTRIM3	   			0x14
#define ES9118C_INPUT_SELECT	   		0x15
#define ES9118C_2_HARMONIC_COMPENSATION_0	    	0x16
#define ES9118C_2_HARMONIC_COMPENSATION_1	    	0x17
#define ES9118C_3_HARMONIC_COMPENSATION_0	    	0x18
#define ES9118C_3_HARMONIC_COMPENSATION_1	    	0x19

/* below for audio debugging */
#ifdef CONFIG_MT_ENG_BUILD
#define DEBUG_AUDDRV_ES9118
#endif

#ifdef DEBUG_AUDDRV_ES9118
#define pr_aud_es9118(format, args...) pr_debug(format, ##args)
#else
#define pr_aud_es9118(format, args...)
#endif


////////****function declear******//////////
int es9118_close(void);
int es9118_single_write(void);
void es9118_hifi_mode_init(void);

int get_headset_status(void);
void set_headset_status(int status);

int get_9118_status(void);
void set_9118_status(int status);

int AudDrv_Es9118_MODE_Select(int mode);

int AudDrv_GPIO_OSC_Select(int mode); 

int AudDrv_GPIO_Power_Select(int bEnable);
int AudDrv_HeadSet_Switch(int bEnable);

////////****function declear******//////////
#endif
