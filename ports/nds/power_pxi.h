/* DTMF-Signer for the Nintendo DS: the one request the ARM9 makes of the
 * ARM7 beyond calico's own, turning the console off after a wipe.
 *
 * libnds' systemShutDown() does not do it under calico (it only sets a bit
 * of the ARM9's own power register): the console is switched off from the
 * ARM7, through the DSi's MCU in DSi mode and the power management chip
 * otherwise, as NDS-Signer does. */
#ifndef NDS_POWER_PXI_H
#define NDS_POWER_PXI_H

#include <nds.h>

#define NDS_PXI_POWER PxiChannel_User0
#define NDS_POWER_OFF 1u

#endif
