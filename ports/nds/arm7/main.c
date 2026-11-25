/*---------------------------------------------------------------------------------

	default ARM7 core

		Copyright (C) 2005 - 2010
		Michael Noland (joat)
		Jason Rogers (dovoto)
		Dave Murphy (WinterMute)

	This software is provided 'as-is', without any express or implied
	warranty.  In no event will the authors be held liable for any
	damages arising from the use of this software.

	Permission is granted to anyone to use this software for any
	purpose, including commercial applications, and to alter it and
	redistribute it freely, subject to the following restrictions:

	1.	The origin of this software must not be misrepresented; you
		must not claim that you wrote the original software. If you use
		this software in a product, an acknowledgment in the product
		documentation would be appreciated but is not required.

	2.	Altered source versions must be plainly marked as such, and
		must not be misrepresented as being the original software.

	3.	This notice may not be removed or altered from any source
		distribution.

---------------------------------------------------------------------------------*/

/*
 * ALTERED SOURCE VERSION: DTMF-Signer minimal ARM7 core.
 *
 * Compared with the default calico core, this ARM7 does NOT start the
 * wireless manager (wlmgr), the block-device driver for the SD card / NAND
 * (blk), the RTC, the touch screen server or maxmod. Their code is not even
 * linked, so the ARM9 has no way to ask for network or storage access. Only
 * power management, the keypad's extra keys (the lid), the sound driver and
 * the microphone driver run; the microphone and its amplifier are off
 * except while the app listens. The console is never let sleep: closing
 * the lid makes the ARM9 wipe and turn it off instead.
 *
 * wirelessOff() also powers the wireless hardware down at start (the
 * launcher may leave it on): the DS (Mitsumi) chip and, when started in
 * DSi mode, the Atheros chip and the wireless LED.
 */
#include <nds.h>

static void wirelessOff(void)
{
	pmPowerOff(POWCNT_WL_MITSUMI);
	if (systemIsTwlMode()) {
		REG_GPIO_WL &= ~GPIO_WL_ACTIVE;  // hold the Atheros chip in reset
		i2cLock();
		u8 led = i2cReadRegister8(I2cDev_MCU, McuReg_WifiLed);
		i2cWriteRegister8(I2cDev_MCU, McuReg_WifiLed, led & ~1);  // bit 0: LED on
		i2cUnlock();
	}
}

int main(void)
{
	// Configure and enable VBlank interrupt
	lcdSetIrqMask(DISPSTAT_IE_ALL, DISPSTAT_IE_VBLANK);
	irqEnable(IRQ_VBLANK);

	// Initialize power management
	pmInit();

	// The extended keypad (X/Y and the hinge): the ARM9 sees the lid close
	pmSetSleepAllowed(false);
	keypadStartExtServer();

	// No networking: the wireless hardware stays off
	wirelessOff();

	// The touch screen controller, which the microphone is read through in
	// DS mode (no touch server: the touch screen is not used)
	touchInit();

	// Sound driver (handles the DSi audio codec)
	soundStartServer(MAIN_THREAD_PRIO - 0x10);

	// Microphone driver: idle (microphone and amplifier off) except while
	// the app listens for tones (arm9/mic.c)
	micStartServer(MAIN_THREAD_PRIO - 0x18);

	// Keep the ARM7 mostly idle
	while (pmMainLoop()) {
		threadWaitForVBlank();
	}

	return 0;
}
