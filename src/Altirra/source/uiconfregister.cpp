//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2014 Avery Lee
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
//	You should have received a copy of the GNU General Public License
//	along with this program; if not, write to the Free Software
//	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#include <stdafx.h>
#include "devicemanager.h"

bool ATUIConfDevHardDisk(VDGUIHandle hParent, ATPropertySet& props);
bool ATUIConfDevBlackBox(VDGUIHandle hParent, ATPropertySet& props);
bool ATUIConfDevBlackBoxFloppy(VDGUIHandle hParent, ATPropertySet& props);
bool ATUIConfDevModem(VDGUIHandle hParent, ATPropertySet& props);
bool ATUIConfDevDragonCart(VDGUIHandle hParent, ATPropertySet& props);
bool ATUIConfDevPCLink(VDGUIHandle hParent, ATPropertySet& props);
bool ATUIConfDevHostFS(VDGUIHandle hParent, ATPropertySet& props);
bool ATUIConfDev1030(VDGUIHandle hParent, ATPropertySet& props);
bool ATUIConfDev1030Full(VDGUIHandle hParent, ATPropertySet& props);
bool ATUIConfDev835(VDGUIHandle hParent, ATPropertySet& props);
bool ATUIConfDev835Full(VDGUIHandle hParent, ATPropertySet& props);
bool ATUIConfDev1400XL(VDGUIHandle hParent, ATPropertySet& props);
bool ATUIConfDev850(VDGUIHandle hParent, ATPropertySet& props);
bool ATUIConfDevSX212(VDGUIHandle hParent, ATPropertySet& props);
bool ATUIConfDevVeronica(VDGUIHandle hParent, ATPropertySet& props);
bool ATUIConfDevSoundBoard(VDGUIHandle hParent, ATPropertySet& props);
bool ATUIConfDevPocketModem(VDGUIHandle hParent, ATPropertySet& props);
bool ATUIConfDevCorvus(VDGUIHandle hParent, ATPropertySet& props);
bool ATUIConfDevMyIDE2(VDGUIHandle hParent, ATPropertySet& props);
bool ATUIConfDevDongle(VDGUIHandle hParent, ATPropertySet& props);
bool ATUIConfDevKMKJZIDE(VDGUIHandle hParent, ATPropertySet& props);
bool ATUIConfDevKMKJZIDE2(VDGUIHandle hParent, ATPropertySet& props);
bool ATUIConfDevCovox(VDGUIHandle hParent, ATPropertySet& props);
bool ATUIConfDevDiskDriveFull(VDGUIHandle hParent, ATPropertySet& props);
bool ATUIConfDevATR8000(VDGUIHandle hParent, ATPropertySet& props);
bool ATUIConfDevPercomRFD(VDGUIHandle hParent, ATPropertySet& props);
bool ATUIConfDevPercomAT(VDGUIHandle hParent, ATPropertySet& props);
bool ATUIConfDevPercomATSPD(VDGUIHandle hParent, ATPropertySet& props);
bool ATUIConfDevVBXE(VDGUIHandle hParent, ATPropertySet& props);
bool ATUIConfDevAMDC(VDGUIHandle hParent, ATPropertySet& props);
bool ATUIConfDevXEP80(VDGUIHandle hParent, ATPropertySet& props);
bool ATUIConfDevHappy810(VDGUIHandle hParent, ATPropertySet& props);
bool ATUIConfDev815(VDGUIHandle hParent, ATPropertySet& props);
bool ATUIConfDevCustom(VDGUIHandle hParent, ATPropertySet& props);
bool ATUIConfDevHDVirtFAT32(VDGUIHandle hParent, ATPropertySet& props);
bool ATUIConfDevSIDE3(VDGUIHandle hParent, ATPropertySet& props);
bool ATUIConfDevParFileWriter(VDGUIHandle hParent, ATPropertySet& props);
bool ATUIConfDevComputerEyes(VDGUIHandle hParent, ATPropertySet& props);
bool ATUIConfDevVideoStillImage(VDGUIHandle hParent, ATPropertySet& props);
bool ATUIConfDevNetSerial(VDGUIHandle hParent, ATPropertySet& props);
bool ATUIConfDevPrinter(VDGUIHandle hParent, ATPropertySet& props);
bool ATUIConfDevPrinterHLE(VDGUIHandle hParent, ATPropertySet& props);
bool ATUIConfDev850Full(VDGUIHandle hParent, ATPropertySet& props);
bool ATUIConfDev1020(VDGUIHandle hParent, ATPropertySet& props);
bool ATUIConfDevMultiplexer(VDGUIHandle hParent, ATPropertySet& props);
bool ATUIConfDevPipeSerial(VDGUIHandle hParent, ATPropertySet& props);
bool ATUIConfDevKarinMaxiDrive(VDGUIHandle hParent, ATPropertySet& props);

void ATRegisterDeviceConfigurers(ATDeviceManager& dev) {
	dev.AddDeviceConfigurer("harddisk", ATUIConfDevHardDisk);
	dev.AddDeviceConfigurer("blackbox", ATUIConfDevBlackBox);
	dev.AddDeviceConfigurer("blackboxfloppy", ATUIConfDevBlackBoxFloppy);
	dev.AddDeviceConfigurer("modem", ATUIConfDevModem);
	dev.AddDeviceConfigurer("dragoncart", ATUIConfDevDragonCart);
	dev.AddDeviceConfigurer("pclink", ATUIConfDevPCLink);
	dev.AddDeviceConfigurer("hostfs", ATUIConfDevHostFS);
	dev.AddDeviceConfigurer("1030", ATUIConfDev1030);
	dev.AddDeviceConfigurer("1030full", ATUIConfDev1030Full);
	dev.AddDeviceConfigurer("835", ATUIConfDev835);
	dev.AddDeviceConfigurer("835full", ATUIConfDev835Full);
	dev.AddDeviceConfigurer("1400xl", ATUIConfDev1400XL);
	dev.AddDeviceConfigurer("850", ATUIConfDev850);
	dev.AddDeviceConfigurer("sx212", ATUIConfDevSX212);
	dev.AddDeviceConfigurer("veronica", ATUIConfDevVeronica);
	dev.AddDeviceConfigurer("soundboard", ATUIConfDevSoundBoard);
	dev.AddDeviceConfigurer("pocketmodem", ATUIConfDevPocketModem);
	dev.AddDeviceConfigurer("corvus", ATUIConfDevCorvus);
	dev.AddDeviceConfigurer("myide2", ATUIConfDevMyIDE2);
	dev.AddDeviceConfigurer("dongle", ATUIConfDevDongle);
	dev.AddDeviceConfigurer("kmkjzide", ATUIConfDevKMKJZIDE);
	dev.AddDeviceConfigurer("kmkjzide2", ATUIConfDevKMKJZIDE2);
	dev.AddDeviceConfigurer("covox", ATUIConfDevCovox);
	dev.AddDeviceConfigurer("pokeymax", ATUIConfDevCovox);
	dev.AddDeviceConfigurer("diskdriveatr8000", ATUIConfDevATR8000);
	dev.AddDeviceConfigurer("diskdrivepercom", ATUIConfDevPercomRFD);
	dev.AddDeviceConfigurer("diskdrivepercomat", ATUIConfDevPercomAT);
	dev.AddDeviceConfigurer("diskdrivepercomatspd", ATUIConfDevPercomATSPD);
	dev.AddDeviceConfigurer("diskdriveamdc", ATUIConfDevAMDC);
	dev.AddDeviceConfigurer("diskdrivehappy810", ATUIConfDevHappy810);
	dev.AddDeviceConfigurer("diskdrive815", ATUIConfDev815);
	dev.AddDeviceConfigurer("vbxe", ATUIConfDevVBXE);
	dev.AddDeviceConfigurer("xep80", ATUIConfDevXEP80);
	dev.AddDeviceConfigurer("custom", ATUIConfDevCustom);
	dev.AddDeviceConfigurer("hdvirtfat16", ATUIConfDevHDVirtFAT32);
	dev.AddDeviceConfigurer("hdvirtfat32", ATUIConfDevHDVirtFAT32);
	dev.AddDeviceConfigurer("hdvirtsdfs", ATUIConfDevHDVirtFAT32);
	dev.AddDeviceConfigurer("side3", ATUIConfDevSIDE3);
	dev.AddDeviceConfigurer("parfilewriter", ATUIConfDevParFileWriter);
	dev.AddDeviceConfigurer("computereyes", ATUIConfDevComputerEyes);
	dev.AddDeviceConfigurer("videostillimage", ATUIConfDevVideoStillImage);
	dev.AddDeviceConfigurer("netserial", ATUIConfDevNetSerial);
	dev.AddDeviceConfigurer("820", ATUIConfDevPrinter);
	dev.AddDeviceConfigurer("1025", ATUIConfDevPrinter);
	dev.AddDeviceConfigurer("1029", ATUIConfDevPrinter);
	dev.AddDeviceConfigurer("printer", ATUIConfDevPrinterHLE);
	dev.AddDeviceConfigurer("850full", ATUIConfDev850Full);
	dev.AddDeviceConfigurer("1020", ATUIConfDev1020);
	dev.AddDeviceConfigurer("multiplexer", ATUIConfDevMultiplexer);
	dev.AddDeviceConfigurer("pipeserial", ATUIConfDevPipeSerial);
	dev.AddDeviceConfigurer("karinmaxidrive", ATUIConfDevKarinMaxiDrive);

	static const char *const kDiskDriveFullTypes[]={
		"diskdrive810",
		"diskdrive810archiver",
		"diskdrive1050",
		"diskdriveusdoubler",
		"diskdrivespeedy1050",
		"diskdrivehappy1050",
		"diskdrivesuperarchiver",
		"diskdrivesuperarchiverbw",
		"diskdrivetoms1050",
		"diskdrive1050duplicator",
		"diskdrivetygrys1050",
		"diskdrive1050turbo",
		"diskdrive1050turboii",
		"diskdriveisplate",
		"diskdriveindusgt",
		"diskdrivexf551",
		"diskdrive810turbo",
		"diskdrivespeedyxf",
	};

	for(const char *s : kDiskDriveFullTypes) {
		dev.AddDeviceConfigurer(s, ATUIConfDevDiskDriveFull);
	}
}
