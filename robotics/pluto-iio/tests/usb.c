/* SPDX-License-Identifier: BSD-2-Clause */
#include "native_usb.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
int main(void) {
	struct usbperiph_config c; unsigned mask=0, reset=0;
	uint8_t setup[8]={0x41,1,0,0,0,0,0,0};
	assert(rx_usb_config(&c,0,0)==-EINVAL);
	assert(rx_usb_config(&c,0x1234,0x5678)==0);
	assert(c.config_length==46);assert(c.device[8]==0x34&&c.device[9]==0x12);
	assert(c.config[20]==0x81&&c.config[27]==0x01&&c.config[34]==0x82&&c.config[41]==0x02);
	assert(rx_usb_setup(setup,&mask,&reset)==0&&mask==1);
	setup[2]=1;assert(rx_usb_setup(setup,&mask,&reset)==0&&mask==3);
	setup[2]=2;assert(rx_usb_setup(setup,&mask,&reset)==-EINVAL&&mask==3);
	setup[2]=0;setup[1]=0;assert(rx_usb_setup(setup,&mask,&reset)==0&&mask==0&&reset==3);
	setup[0]=0xc1;assert(rx_usb_setup(setup,&mask,&reset)==-EINVAL);
	setup[0]=0x41;setup[6]=1;assert(rx_usb_setup(setup,&mask,&reset)==-EINVAL);
	puts("usb: PASS");return 0;
}
