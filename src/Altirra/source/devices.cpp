//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2015 Avery Lee
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
#include <at/atcore/device.h>
#include <at/atdevices/devices.h>
#include "devicemanager.h"

void ATRegisterDeviceConfigurers(ATDeviceManager& dm);


extern const ATDeviceDefinition g_ATDeviceDefModem;
extern const ATDeviceDefinition g_ATDeviceDefBlackBox;
extern const ATDeviceDefinition g_ATDeviceDefBlackBoxFloppy;
extern const ATDeviceDefinition g_ATDeviceDefMIO;
extern const ATDeviceDefinition g_ATDeviceDefHardDisks;
extern const ATDeviceDefinition g_ATDeviceDefIDEPhysDisk;
extern const ATDeviceDefinition g_ATDeviceDefIDERawImage;
extern const ATDeviceDefinition g_ATDeviceDefIDEVHDImage;
extern const ATDeviceDefinition g_ATDeviceDefRTime8;
extern const ATDeviceDefinition g_ATDeviceDefCovox;
extern const ATDeviceDefinition g_ATDeviceDefPokeyMax;
extern const ATDeviceDefinition g_ATDeviceDefXEP80;
extern const ATDeviceDefinition g_ATDeviceDefSlightSID;
extern const ATDeviceDefinition g_ATDeviceDefDragonCart;
extern const ATDeviceDefinition g_ATDeviceDefSIOClock;
extern const ATDeviceDefinition g_ATDeviceDefTestSIOPoll3;
extern const ATDeviceDefinition g_ATDeviceDefTestSIOPoll4;
extern const ATDeviceDefinition g_ATDeviceDefTestSIOHighSpeed;
extern const ATDeviceDefinition g_ATDeviceDefPCLink;
extern const ATDeviceDefinition g_ATDeviceDefHostDevice;
extern const ATDeviceDefinition g_ATDeviceDefPrinter;
extern const ATDeviceDefinition g_ATDeviceDef850;
extern const ATDeviceDefinition g_ATDeviceDef835Modem;
extern const ATDeviceDefinition g_ATDeviceDef1030Modem;
extern const ATDeviceDefinition g_ATDeviceDefXM301Modem;
extern const ATDeviceDefinition g_ATDeviceDefSX212;
extern const ATDeviceDefinition g_ATDeviceDefMidiMate;
extern const ATDeviceDefinition g_ATDeviceDefSDrive;
extern const ATDeviceDefinition g_ATDeviceDefSIO2SD;
extern const ATDeviceDefinition g_ATDeviceDefVeronica;
extern const ATDeviceDefinition g_ATDeviceDefRVerter;
extern const ATDeviceDefinition g_ATDeviceDefSoundBoard;
extern const ATDeviceDefinition g_ATDeviceDefPocketModem;
extern const ATDeviceDefinition g_ATDeviceDefKMKJZIDE;
extern const ATDeviceDefinition g_ATDeviceDefKMKJZIDE2;
extern const ATDeviceDefinition g_ATDeviceDefMyIDED1xx;
extern const ATDeviceDefinition g_ATDeviceDefMyIDED5xx;
extern const ATDeviceDefinition g_ATDeviceDefMyIDE2;
extern const ATDeviceDefinition g_ATDeviceDefSIDE;
extern const ATDeviceDefinition g_ATDeviceDefSIDE2;
extern const ATDeviceDefinition g_ATDeviceDefSIDE3;
extern const ATDeviceDefinition g_ATDeviceDefDongle;
extern const ATDeviceDefinition g_ATDeviceDefPBIDisk;
extern const ATDeviceDefinition g_ATDeviceDefDiskDrive810;
extern const ATDeviceDefinition g_ATDeviceDefDiskDrive810Archiver;
extern const ATDeviceDefinition g_ATDeviceDefDiskDriveHappy810;
extern const ATDeviceDefinition g_ATDeviceDefDiskDrive1050;
extern const ATDeviceDefinition g_ATDeviceDefDiskDriveUSDoubler;
extern const ATDeviceDefinition g_ATDeviceDefDiskDriveSpeedy1050;
extern const ATDeviceDefinition g_ATDeviceDefDiskDriveSpeedyXF;
extern const ATDeviceDefinition g_ATDeviceDefDiskDriveHappy1050;
extern const ATDeviceDefinition g_ATDeviceDefDiskDriveSuperArchiver;
extern const ATDeviceDefinition g_ATDeviceDefDiskDriveSuperArchiverBW;
extern const ATDeviceDefinition g_ATDeviceDefDiskDriveTOMS1050;
extern const ATDeviceDefinition g_ATDeviceDefDiskDriveTygrys1050;
extern const ATDeviceDefinition g_ATDeviceDefDiskDrive1050Duplicator;
extern const ATDeviceDefinition g_ATDeviceDefDiskDrive1050Turbo;
extern const ATDeviceDefinition g_ATDeviceDefDiskDrive1050TurboII;
extern const ATDeviceDefinition g_ATDeviceDefDiskDriveISPlate;
extern const ATDeviceDefinition g_ATDeviceDefDiskDriveIndusGT;
extern const ATDeviceDefinition g_ATDeviceDefDiskDriveXF551;
extern const ATDeviceDefinition g_ATDeviceDefDiskDriveATR8000;
extern const ATDeviceDefinition g_ATDeviceDefDiskDrivePercomRFD;
extern const ATDeviceDefinition g_ATDeviceDefDiskDrivePercomAT;
extern const ATDeviceDefinition g_ATDeviceDefDiskDrivePercomATSPD;
extern const ATDeviceDefinition g_ATDeviceDefDiskDrive810Turbo;
extern const ATDeviceDefinition g_ATDeviceDefDiskDriveAMDC;
extern const ATDeviceDefinition g_ATDeviceDefDiskDrive815;
extern const ATDeviceDefinition g_ATDeviceDefBrowser;
extern const ATDeviceDefinition g_ATDeviceDefVBXE;
extern const ATDeviceDefinition g_ATDeviceDefXELCF;
extern const ATDeviceDefinition g_ATDeviceDefXELCF3;
extern const ATDeviceDefinition g_ATDeviceDefRapidus;
extern const ATDeviceDefinition g_ATDeviceDefWarpOS;
extern const ATDeviceDefinition g_ATDeviceDefCustom;
extern const ATDeviceDefinition g_ATDeviceDefBlockDevVFAT16;
extern const ATDeviceDefinition g_ATDeviceDefBlockDevVFAT32;
extern const ATDeviceDefinition g_ATDeviceDefBlockDevVSDFS;
extern const ATDeviceDefinition g_ATDeviceDefBlockDevTemporaryWriteFilter;
extern const ATDeviceDefinition g_ATDeviceDef1090;
extern const ATDeviceDefinition g_ATDeviceDefBit3FullView;
extern const ATDeviceDefinition g_ATDeviceDefComputerEyes;
extern const ATDeviceDefinition g_ATDeviceDefVideoGenerator;
extern const ATDeviceDefinition g_ATDeviceDefVideoStillImage;
extern const ATDeviceDefinition g_ATDeviceDefSimCovox;
extern const ATDeviceDefinition g_ATDeviceDef1400XL;
extern const ATDeviceDefinition g_ATDeviceDef1450XLDiskController;
extern const ATDeviceDefinition g_ATDeviceDef1450XLDiskControllerFull;
extern const ATDeviceDefinition g_ATDeviceDef1450XLTONGDiskControllerFull;
extern const ATDeviceDefinition g_ATDeviceDef1030Full;
extern const ATDeviceDefinition g_ATDeviceDef835Full;
extern const ATDeviceDefinition g_ATDeviceDefSIOSerialAdapter;
extern const ATDeviceDefinition g_ATDeviceDefMPP1000E;
extern const ATDeviceDefinition g_ATDeviceDefPrinter820;
extern const ATDeviceDefinition g_ATDeviceDefPrinter820Full;
extern const ATDeviceDefinition g_ATDeviceDefPrinter1025;
extern const ATDeviceDefinition g_ATDeviceDefPrinter1025Full;
extern const ATDeviceDefinition g_ATDeviceDefPrinter1029;
extern const ATDeviceDefinition g_ATDeviceDefPrinter1029Full;
extern const ATDeviceDefinition g_ATDeviceDefPrinter825;
extern const ATDeviceDefinition g_ATDeviceDefPrinter1020;
extern const ATDeviceDefinition g_ATDeviceDef850Full;
extern const ATDeviceDefinition g_ATDeviceDefThePill;
extern const ATDeviceDefinition g_ATDeviceDefMultiplexer;
extern const ATDeviceDefinition g_ATDeviceDefPipeSerial;
extern const ATDeviceDefinition g_ATDeviceDefKarinMaxiDrive;

void ATRegisterDevices(ATDeviceManager& dm) {
	static constexpr const ATDeviceDefinition *kDeviceDefs[]={
		&g_ATDeviceDefModem,
		&g_ATDeviceDefBlackBox,
		&g_ATDeviceDefBlackBoxFloppy,
		&g_ATDeviceDefMIO,
		&g_ATDeviceDefHardDisks,
		&g_ATDeviceDefIDEPhysDisk,
		&g_ATDeviceDefIDERawImage,
		&g_ATDeviceDefIDEVHDImage,
		&g_ATDeviceDefRTime8,
		&g_ATDeviceDefCovox,
		&g_ATDeviceDefPokeyMax,
		&g_ATDeviceDefXEP80,
		&g_ATDeviceDefSlightSID,
		&g_ATDeviceDefDragonCart,
		&g_ATDeviceDefSIOClock,
		&g_ATDeviceDefTestSIOPoll3,
		&g_ATDeviceDefTestSIOPoll4,
		&g_ATDeviceDefTestSIOHighSpeed,
		&g_ATDeviceDefPCLink,
		&g_ATDeviceDefHostDevice,
		&g_ATDeviceDefPrinter,
		&g_ATDeviceDef850,
		&g_ATDeviceDef850Full,
		&g_ATDeviceDef835Modem,
		&g_ATDeviceDef1030Modem,
		&g_ATDeviceDefXM301Modem,
		&g_ATDeviceDefSX212,
		&g_ATDeviceDefMidiMate,
		&g_ATDeviceDefSDrive,
		&g_ATDeviceDefSIO2SD,
		&g_ATDeviceDefVeronica,
		&g_ATDeviceDefRVerter,
		&g_ATDeviceDefSoundBoard,
		&g_ATDeviceDefPocketModem,
		&g_ATDeviceDefKMKJZIDE,
		&g_ATDeviceDefKMKJZIDE2,
		&g_ATDeviceDefMyIDED1xx,
		&g_ATDeviceDefMyIDED5xx,
		&g_ATDeviceDefMyIDE2,
		&g_ATDeviceDefSIDE,
		&g_ATDeviceDefSIDE2,
		&g_ATDeviceDefSIDE3,
		&g_ATDeviceDefDongle,
		&g_ATDeviceDefPBIDisk,
		&g_ATDeviceDefDiskDrive810,
		&g_ATDeviceDefDiskDrive810Archiver,
		&g_ATDeviceDefDiskDriveHappy810,
		&g_ATDeviceDefDiskDrive1050,
		&g_ATDeviceDefDiskDriveUSDoubler,
		&g_ATDeviceDefDiskDriveSpeedy1050,
		&g_ATDeviceDefDiskDriveSpeedyXF,
		&g_ATDeviceDefDiskDriveHappy1050,
		&g_ATDeviceDefDiskDriveSuperArchiver,
		&g_ATDeviceDefDiskDriveSuperArchiverBW,
		&g_ATDeviceDefDiskDriveTOMS1050,
		&g_ATDeviceDefDiskDriveTygrys1050,
		&g_ATDeviceDefDiskDrive1050Duplicator,
		&g_ATDeviceDefDiskDrive1050Turbo,
		&g_ATDeviceDefDiskDrive1050TurboII,
		&g_ATDeviceDefDiskDriveISPlate,
		&g_ATDeviceDefDiskDriveIndusGT,
		&g_ATDeviceDefDiskDriveXF551,
		&g_ATDeviceDefDiskDriveATR8000,
		&g_ATDeviceDefDiskDrivePercomRFD,
		&g_ATDeviceDefDiskDrivePercomAT,
		&g_ATDeviceDefDiskDrivePercomATSPD,
		&g_ATDeviceDefDiskDrive810Turbo,
		&g_ATDeviceDefDiskDriveAMDC,
		&g_ATDeviceDefDiskDrive815,
		&g_ATDeviceDefBrowser,
		&g_ATDeviceDefVBXE,
		&g_ATDeviceDefXELCF,
		&g_ATDeviceDefXELCF3,
		&g_ATDeviceDefRapidus,
		&g_ATDeviceDefWarpOS,
		&g_ATDeviceDefCustom,
		&g_ATDeviceDefBlockDevVFAT16,
		&g_ATDeviceDefBlockDevVFAT32,
		&g_ATDeviceDefBlockDevVSDFS,
		&g_ATDeviceDefBlockDevTemporaryWriteFilter,
		&g_ATDeviceDef1090,
		&g_ATDeviceDefBit3FullView,
		&g_ATDeviceDefComputerEyes,
		&g_ATDeviceDefVideoGenerator,
		&g_ATDeviceDefVideoStillImage,
		&g_ATDeviceDefSimCovox,
		&g_ATDeviceDef1400XL,
		&g_ATDeviceDef1450XLDiskController,
		&g_ATDeviceDef1450XLDiskControllerFull,
		&g_ATDeviceDef1450XLTONGDiskControllerFull,
		&g_ATDeviceDef1030Full,
		&g_ATDeviceDef835Full,
		&g_ATDeviceDefSIOSerialAdapter,
		&g_ATDeviceDefMPP1000E,
		&g_ATDeviceDefPrinter820,
		&g_ATDeviceDefPrinter820Full,
		&g_ATDeviceDefPrinter1025,
		&g_ATDeviceDefPrinter1025Full,
		&g_ATDeviceDefPrinter1029,
		&g_ATDeviceDefPrinter1029Full,
		&g_ATDeviceDefPrinter825,
		&g_ATDeviceDefPrinter1020,
		&g_ATDeviceDefThePill,
		&g_ATDeviceDefMultiplexer,
		&g_ATDeviceDefPipeSerial,
		&g_ATDeviceDefKarinMaxiDrive,
	};

	for(const ATDeviceDefinition *def : kDeviceDefs)
		dm.AddDeviceDefinition(def);

	for(const ATDeviceDefinition *def : kATDeviceLibraryDefs)
		dm.AddDeviceDefinition(def);

	ATRegisterDeviceConfigurers(dm);
}

void ATRegisterDeviceXCmds(ATDeviceManager& dm) {
	void ATDeviceInitXCmdMountVHD(ATDeviceManager& dm);
	void ATDeviceInitXCmdExploreDisk(ATDeviceManager& dm);
	void ATDeviceInitXCmdRescanDynamicDisk(ATDeviceManager& dm);
	void ATDeviceInitXCmdCopyPaste(ATDeviceManager& dm);

	ATDeviceInitXCmdMountVHD(dm);
	ATDeviceInitXCmdExploreDisk(dm);
	ATDeviceInitXCmdRescanDynamicDisk(dm);
	ATDeviceInitXCmdCopyPaste(dm);
}
