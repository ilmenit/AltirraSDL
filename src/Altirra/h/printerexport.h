//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2026 Avery Lee
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

#ifndef f_AT_PRINTEREXPORT_H
#define f_AT_PRINTEREXPORT_H

class ATPrinterGraphicalOutput;

void ATPrinterExportAsPDF(const wchar_t *path, ATPrinterGraphicalOutput& output, float pageWidthMM, float pageHeightMM);

// Export printer graphics as SVG.
//
// If useBlending=false, the SVG vectors are exported in layers with the lightest colors first
// to approximate blending. If useBlending=true, darken blending mode is used to improve the
// approximation.
void ATPrinterExportAsSVG(const wchar_t *path, ATPrinterGraphicalOutput& output, bool useBlending);

#endif
