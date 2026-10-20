//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2023 Avery Lee
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.
//
//	This program is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//	GNU General Public License for more details.
//
//	You should have received a copy of the GNU General Public License along
//	with this program. If not, see <http://www.gnu.org/licenses/>.

#include <stdafx.h>
#include <at/atcore/savestate.h>
#include "vbxestate.h"

template<ATExchanger T>
void ATSaveStateVbxe::Exchange(T& ex) {
	ex.Transfer("memory", &mpMemory);

	ex.Transfer("arch_video_control", &mVideoControl);
	ex.Transfer("arch_feature_control", &mFeatureControl);
	ex.Transfer("arch_feature_control_present", &mbFeatureControlPresent);
	ex.Transfer("arch_xdl_adr_x", &mXdlAddr);
	ex.Transfer("arch_csel", &mCsel);
	ex.Transfer("arch_psel", &mPsel);
	ex.Transfer("arch_colmask", &mColMask);
	ex.Transfer("arch_bl_adr_x", &mBlitListAddr);
	ex.TransferArray("arch_p_x", mPri);
	ex.Transfer("arch_irq_control", &mIrqControl);
	ex.Transfer("arch_memac_b_control", &mMemacBControl);
	ex.Transfer("arch_memac_control", &mMemacControl);
	ex.Transfer("arch_memac_bank_sel", &mMemacBankSel);
	ex.TransferArray("arch_pal", mPalette);

	ex.Transfer("arch_coldetect", &mArchColDetect);
	ex.Transfer("arch_blt_collision_code", &mArchBltCollisionCode);
	ex.Transfer("arch_blitter_busy", &mArchBlitterBusy);
	ex.Transfer("arch_irq_status", &mArchIrqStatus);

	ex.Transfer("int_xdl_addr", &mXdlAddrActive);
	ex.Transfer("int_xdl_repeat", &mXdlRepeat);
	ex.Transfer("int_pf_palette", &mIntPfPalette);
	ex.Transfer("int_ov_palette", &mIntOvPalette);
	ex.Transfer("int_ov_mode", &mIntOvMode);
	ex.Transfer("int_ov_width", &mIntOvWidth);
	ex.Transfer("int_ov_addr", &mIntOvAddr);
	ex.Transfer("int_ov_step", &mIntOvStep);
	ex.Transfer("int_ov_hscroll", &mIntOvHscroll);
	ex.Transfer("int_ov_vscroll", &mIntOvVscroll);
	ex.Transfer("int_ov_chbase", &mIntOvChbase);
	ex.Transfer("int_ov_priority", &mIntOvPriority);
	ex.Transfer("int_ov_text_row", &mIntOvTextRow);
	ex.Transfer("int_map_enabled", &mbIntMapEnabled);
	ex.Transfer("int_map_addr", &mIntMapAddr);
	ex.Transfer("int_map_step", &mIntMapStep);
	ex.Transfer("int_map_hscroll", &mIntMapHscroll);
	ex.Transfer("int_map_vscroll", &mIntMapVscroll);
	ex.Transfer("int_map_width", &mIntMapWidth);
	ex.Transfer("int_map_height", &mIntMapHeight);
	ex.Transfer("int_map_row", &mIntMapRow);

	ex.Transfer("int_blit_list_addr", &mIntBlitListAddr);
	ex.TransferArray("int_blit_state", mIntBlitState);
	ex.Transfer("int_blit_cycles_pending", &mIntBlitCyclesPending);
	ex.Transfer("int_blit_height_left", &mIntBlitHeightLeft);
	ex.Transfer("int_blit_yzoom_counter", &mIntBlitYZoomCounter);
	ex.Transfer("int_blit_stop_delay", &mIntBlitStopDelay);
	ex.Transfer("int_blit_collision_latched", &mbIntBlitCollisionLatched);
	ex.Transfer("int_blit_containers_pending", &mbIntBlitContainersPending);
	ex.Transfer("int_blit_raycast", &mbIntBlitRaycast);
	ex.TransferArray("int_blit_raycast_coords", mIntBlitRaycastCoords);
	ex.TransferArray("int_blit_raycast_texture_bits", mIntBlitRaycastTextureBits);

	ex.TransferArray("int_gtia_pal", mGtiaPal);
	ex.Transfer("int_gtia_prior", &mGtiaPrior);
	ex.Transfer("int_gtia_changes", &mGtiaRegisterChanges);

	if constexpr (T::IsReader) {
		if ((mFeatureControl & ~0xBB)
			|| (mbIntBlitRaycast
				&& (mIntBlitRaycastTextureBits[0] < 3
					|| mIntBlitRaycastTextureBits[0] > 7
					|| mIntBlitRaycastTextureBits[1] < 3
					|| mIntBlitRaycastTextureBits[1] > 7)))
			throw ATInvalidSaveStateException();

		// validate that register changes have ascending position and full triples
		size_t rcn = mGtiaRegisterChanges.size();

		if (rcn % 3)
			throw ATInvalidSaveStateException();

		for(size_t i=3; i<rcn; ++i) {
			if (mGtiaRegisterChanges[i] < mGtiaRegisterChanges[i-3])
				throw ATInvalidSaveStateException();
		}
	}
}

template void ATSaveStateVbxe::Exchange(ATSerializer&);
template void ATSaveStateVbxe::Exchange(ATDeserializer&);
