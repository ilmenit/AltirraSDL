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
#include <windows.h>
#include <richedit.h>
#include <vd2/system/error.h>
#include <vd2/system/file.h>
#include <vd2/system/filesys.h>
#include <vd2/system/math.h>
#include <vd2/system/registry.h>
#include <vd2/Dita/services.h>
#include <vd2/Kasumi/pixmaputils.h>
#include <at/atcore/device.h>
#include <at/atcore/devicediskdrive.h>
#include <at/atcore/deviceparent.h>
#include <at/atcore/propertyset.h>
#include <at/atnativeui/dialog.h>
#include <at/atnativeui/theme.h>
#include "devicemanager.h"
#include "devxcmdcopypaste.h"
#include "diskinterface.h"
#include "oshelper.h"
#include "resource.h"
#include "simulator.h"
#include "uidevices.h"
#include "uiconfirm.h"

extern ATSimulator g_sim;

///////////////////////////////////////////////////////////////////////////

class ATUIDialogDeviceNew final : public VDResizableDialogFrameW32 {
public:
	ATUIDialogDeviceNew(const vdfunction<bool(const char *)>& filter);

	const char *GetDeviceTag() const { return mpDeviceTag; }

protected:
	bool OnLoaded() override;
	void OnDestroy() override;
	bool OnOK() override;
	void OnDataExchange(bool write) override;

	void OnItemSelectionChanged(VDUIProxyTreeViewControl *sender, int);
	void UpdateHelpText();
	void AppendRTF(VDStringA& rtf, const wchar_t *text);

	const vdfunction<bool(const char *)>& mFilter;
	VDUIProxyTreeViewControl mTreeView;
	VDUIProxyRichEditControl mHelpView;
	VDDelegate mDelItemSelChanged;
	const char *mpDeviceTag;
	const wchar_t *mpLastHelpText;

	struct TreeEntry {
		const char *mpTag;
		const wchar_t *mpText;
		const wchar_t *mpHelpText;
	};

	struct CategoryEntry {
		const wchar_t *mpText;
		const char *mpTag;
		std::initializer_list<TreeEntry> mDevices;
	};

	class TreeNode : public vdrefcounted<IVDUITreeViewVirtualItem> {
	public:
		TreeNode(const TreeEntry& node) : mNode(node) {}

		void *AsInterface(uint32 id) { return nullptr; }

		virtual void GetText(VDStringW& s) const {
			s = mNode.mpText;
		}

		const TreeEntry& mNode;
	};

	static const CategoryEntry kCategories[];
};

const ATUIDialogDeviceNew::CategoryEntry ATUIDialogDeviceNew::kCategories[]={
	{
		L"Parallel Bus Interface (PBI) devices",
		"pbi",
		{
			{
				"1090",
				L"1090 80 Column Video Card",
				L"PBI-based 80 column video display card for the 1090 XL expansion system."
			},
			{
				"blackbox",
				L"Black Box",
				L"The CSS Black Box attaches to the PBI port to provide SCSI hard disk, RAM disk, "
					L"parallel printer port, and RS-232 serial port functionality.\n"
					L"The built-in firmware is configured by pressing the Black Box's Menu button."
			},
			{
				"karinmaxidrive", L"Karin Maxi Drive",
				L"PBI disk interface created by Mariusz Geisler and the Polish company Karin, providing "
				L"access to up to two disk drives with double-density, 80 track, and double sided support.\n"
				L"This emulation also supports an updated hardware design created by Stryker, Zaxon, Jer, "
				L"trub, drac030, Candle, and Dely."
			},
			{
				"kmkjzide",
				L"KMK/J\u017B IDE v1",
				L"A PBI-based hard disk interface for parallel ATA hard disk devices.",
			},
			{
				"kmkjzide2",
				L"KMK/J\u017B IDE v2 (IDEPlus 2.0)",
				L"A PBI-based hard disk interface for parallel ATA hard disk devices. Version 2 "
					L"adds expanded firmware and built-in SpartaDOS X capability."
			},
			{
				"mio", L"MIO",
				L"The ICD Multi-I/O (MIO) is a PBI device that adds SCSI hard disk, RAM disk, "
					L"parallel printer port, and RS-232 serial port functionality.\n"
					L"The MIO firmware's built-in menu is accessed with Select+Reset."
			},
		},
	},
	{
		L"Cartridge devices",
		"cartridge",
		{
			{ "dragoncart", L"DragonCart",
				L"Adds an Ethernet port to the computer. No firmware is built-in; "
					L"networking software must be run separately."
			},
			{ "multiplexer", L"Multiplexer",
				L"Cartridge device made by Computer Software Services (CSS) that allows computers to share disk drives and printers over a local network.\n"
				L"At least two emulated computers are needed each with a Multiplexer, one running the MASTER.COM software to share its devices and the rest "
				L"running the MUX OS or equivalent to access those devices over the network.\n"
				L"This emulation simulates the parallel ribbon cable bus with a shared TCP/IP port."
			},
			{ "myide-d5xx", L"MyIDE (cartridge)",
				L"IDE adapter attached to cartridge port, using the $D5xx address range."
			},
			{ "myide2", L"MyIDE-II",
				L"Enhanced version of the MyIDE interface, providing a CompactFlash interface with "
					L"hot-swap support and banked flash memory with three access windows."
			},
			{ "rtime8", L"R-Time 8",
				L"A simple cartridge that provides a real-time clock. It was specifically designed "
					L"to work in the pass-through port of the SpartaDOS X cartridge. Separate software "
					L"is required to provide the Z: device handler."
			},
			{ "side", L"SIDE",
				L"Cartridge with 512K of flash and a CompactFlash port for storage."
			},
			{ "side2", L"SIDE 2",
				L"Enhanced version of SIDE with enhanced banking, CompactFlash change detection support, "
					L"and improved Ultimate1MB compatibility."
			},
			{ "side3", L"SIDE 3",
				L"Third-generation SIDE device with SD card storage, cartridge emulation, and DMA support."
			},
			{ "slightsid", L"SlightSID",
				L"A cartridge-shaped adapter for the C64's SID sound chip."
			},
			{ "veronica", L"Veronica",
				L"A cartridge-based 65C816 coprocessor with 128K of on-board memory.\n"
					L"Veronica does not have on-board firmware, so software must be externally loaded to use it."
			},
			{ "thepill", L"The Pill",
				L"A cartridge from Computer Software Services which has no ROM of its own, but has a switch to write protect the cartridge memory areas to allow RAM to simulate a cartridge. Requires at least 48K of memory."
			},
		}
	},
	{
		L"Internal devices",
		"internal",
		{
			{ "1450xldisk", L"1450XLD Disk Controller",
				L"Internal PBI-based disk controller in the 1450XLD."
			},
			{ "1450xldiskfull", L"1450XLD Disk Controller (full emulation)",
				L"Internal PBI-based disk controller in the 1450XLD, with full 8040 controller emulation."
			},
			{ "1450xltongdiskfull", L"1450XLD \"TONG\" Disk Controller (full emulation)",
				L"Internal PBI-based disk controller in the \"TONG\" integrated version of the 1450XLD, with full 8040 controller emulation."
			},
			{ "warpos", L"APE Warp+ OS 32-in-1",
				L"Internal upgrade allowing for soft-switching between 32 different OS ROMs in an XL/XE system."
			},
			{ "bit3", L"Bit 3 Full-View 80",
				L"Add-on 80 column video board for the 800 that replaces RAM slot 3."
			},
			{ "covox", L"Covox",
				L"Provides a simple DAC for 8-bit digital sound."
			},
			{ "pokeymax", L"PokeyMax",
				L"Provides a simple DAC for 8-bit digital sound (Covox-compatible)."
			},
			{ "rapidus", L"Rapidus Accelerator",
				L"Switchable 6502/65C816 based accelerator running at 20MHz with 15MB of memory and 512KB of flash."
			},
			{ "soundboard", L"SoundBoard",
				L"Provides multi-channel, wavetable sound capabilities with 512K of internal memory."
			},
			{ "myide-d1xx", L"MyIDE (internal)",
				L"IDE adapter attached to internal port, using the $D1xx address range."
			},
			{ "vbxe", L"VideoBoard XE (VBXE)",
				L"Adds an enhanced video display adapter with 512K of video RAM, 640x horizontal resolution, 8-bit overlays, attribute map, and blitter.\n"
				L"VBXE requires special software to use enhanced features. This software is not included with the emulator."
			},
			{ "xelcf", L"XEL-CF CompactFlash Adapter",
				L"CompactFlash adapter attached to internal port, using the $D1xx address range. Includes reset strobe."
			},
			{ "xelcf3", L"XEL-CF3 CompactFlash Adapter",
				L"CompactFlash adapter attached to internal port, using the $D1xx address range. Includes reset strobe and swap button."
			},
		}
	},
	{
		L"Controller port devices",
		"joyport",
		{
			{ "computereyes", L"ComputerEyes Video Acquisition System",
				L"Video capture device by Digital Vision, Inc. connecting to joystick ports 1 and 2."
			},
			{ "corvus", L"Corvus Disk Interface",
				L"External hard drive connected to controller ports 3 and 4 of a 400/800. Additional handler software "
					L"is required to access the disk (not included)."
			},
			{ "dongle", L"Joystick port dongle",
				L"Small device attached to joystick port used to lock software operation to physical posesssion "
					L"of the dongle device. This form implements a simple mapping function where up to three bits output "
					L"by the computer produce up to four bits of output from the dongle."
			},
			{ "mpp1000e", L"Microbits MPP-1000E Modem",
				L"A 300 baud modem connecting to joystick port 2."
			},
			{ "simcovox", L"SimCovox",
				L"A joystick based Covox device made by Jakub Husak, plugging into ports 1 and 2.\n\nhttps://github.com/jhusak/atari8_simcovox_arduino_mega328p"
			},
			{ "supersalt", L"SuperSALT Test Assembly",
				L"Test device that is used with the SuperSALT cartridges to test joystick ports and the SIO port."
			},
			{ "xep80", L"XEP80",
				L"External 80-column video output that attaches to the joystick port and drives a separate display. "
					L"Additional handler software is required (provided on Additions disk)."
			},
		}
	},
	{
		L"Hard disks",
		"harddisk",
		{
			{ "harddisk", L"Hard disk",
				L"Hard drive or solid state drive. This device can be added as a sub-device to any parent device "
					L"that has an IDE, CompactFlash, SCSI, or SD Card interface."
			},
			{ "hdtempwritefilter", L"Temporary write filter",
				L"Allows writes to a read-only hard disk image by caching the writes in memory. Written data persists "
					L"across an emulation warm/cold reset, but is lost when the device is removed or the emulator is closed."
			},
			{ "hdvirtfat16", L"Virtual FAT16 hard disk",
				L"Virtual hard drive image created from files in a host directory, reflecting the files in a "
					L"synthesized FAT16-formatted partition. This is limited to 256MB of file content."
			},
			{ "hdvirtfat32", L"Virtual FAT32 hard disk",
				L"Virtual hard drive image created from files in a host directory, reflecting the files in a "
					L"synthesized FAT32-formatted partition."
			},
			{ "hdvirtsdfs", L"Virtual SDFS hard disk",
				L"Virtual hard drive image created from files in a host directory, reflecting the files in a "
					L"synthesized SpartaDOS-formatted partition."
			}
		},
	},
	{
		L"Serial devices",
		"serial",
		{
			{ "parfilewriter", L"File writer",
				L"Writes all data output from a parallel or serial port to a file."
			},
			{ "loopback", L"Loopback",
				L"Simple plug that connects the transmit and receive lines together, looping back all transmitted data "
				L"to the receiver. Commonly used for testing."
			},
			{ "modem", L"Modem",
				L"Hayes compatible modem with connection simulated over TCP/IP."
			},
			{ "netserial", L"Networked serial port",
				L"Network to serial port bridge over TCP/IP."
			},
			{ "pipeserial", L"Named pipe serial port",
				L"Windows named pipe to serial port bridge. This bridges a serial port in the emulation to a named pipe, "
				L"allowing access from programs that read or write to a file path."
			},
			{ "serialsplitter", L"Serial splitter",
				L"Allows different connections for the input and output halves of a serial port."
			}
		}
	},
	{
		L"Parallel port devices",
		"parallel",
		{
			{ "825", L"825 80-Column Printer",
				L"80 column dot-matrix printer with a parallel port connection. Based on the Centronics 737 printer."
			},
			{ "parfilewriter", L"File writer",
				L"Writes all data output from a parallel or serial port to a file."
			},
			{ "par2ser", L"Parallel to serial adapter",
				L"Connects a parallel port output to a serial input."
			},
		}
	},
	{
		L"Disk drives (full emulation)",
		"sio",
		{
			{ "diskdrive810", L"810",
				L"Full 810 disk drive emulation, including 6507 CPU.\n"
					L"The 810 has a 40-track drive and only reads single density disks. It is the lowest common denominator among all "
					L"compatible disk drives and will not read any larger disk formats, such as enhanced or double density."
			},

			{ "diskdrive810archiver", L"810 Archiver",
				L"Full emulation for the 810 Archiver disk drive, a.k.a. 810 with \"The Chip\", including 6507 CPU."
			},

			{ "diskdrivehappy810", L"Happy 810",
				L"Full Happy 810 disk drive emulation, including 6507 CPU.\n"
					L"The Happy 810 adds track buffering and custom code execution capabilities to the 810 drive. It does not support "
					L"double density, however."
			},

			{ "diskdrive810turbo", L"810 Turbo",
				L"Full disk drive emulation for the 810 Turbo Double Density Conversion board, made by Neanderthal Computer Things (NCT). "
				L"This add-on to the standard 810 disk drive boosts the CPU speed from 500KHz to 1MHz, adds double-density support, "
				L"and adds 4K of memory.\n"
				L"Note that the standard 810 Turbo firmware does not support enhanced (medium) disks."
			},

			{ "diskdrive815", L"815",
				L"Full 815 disk drive emulation, including 6507 CPU.\n"
					L"The 815 is a dual disk drive supporting ONLY double density disks (256 bytes/sector). Currently only read-only "
					L"emulation is supported.\n"
					L"The firmware for the 815 is contained in two 2K chips that need to be combined into a single 4K image. The "
					L"chips are labeled A107 and A106 on the board and should be combined in that order."
			},

			{ "diskdrive1050", L"1050",
				L"Full 1050 disk drive emulation, including 6507 CPU.\n"
					L"The 1050 supports single density and enhanced density disks. True double density (256 bytes/sector) disks are not "
					L"supported."
			},

			{ "diskdrive1050duplicator", L"1050 Duplicator",
				L"Full 1050 Duplicator disk drive emulation, including 6507 CPU."
			},

			{ "diskdriveusdoubler", L"US Doubler",
				L"Full US Doubler disk drive emulation, including 6507 CPU.\n"
					L"The US Doubler is an enhanced 1050 drive that supports true double density and high speed operation."
			},

			{ "diskdrivespeedy1050", L"Speedy 1050",
				L"Full Speedy 1050 disk drive emulation, including 65C02 CPU.\n"
					L"The Speedy 1050 is an enhanced 1050 that supports double density, track buffering, and high speed operation.\n"
					L"NOTE: The Speedy 1050 may operate erratically in high-speed mode with NTSC computers due to marginal write timing. "
					L"Version 1.6 or later firmware is needed for reliable high-speed writes in NTSC."
			},

			{ "diskdrivespeedyxf", L"Speedy XF",
				L"Full Speedy XF disk drive emulation, including 65C02 CPU.\n"
					L"The Speedy XF is a modified XF551 drive that replaces the 8040 MCU with a 65C02 and adds 64K of ROM and 32K of RAM, "
					L"making it more similar to a Speedy 1050."
			},

			{ "diskdrivehappy1050", L"Happy 1050",
				L"Full Happy 1050 disk drive emulation, including 6502 CPU."
			},

			{ "diskdrivesuperarchiver", L"Super Archiver",
				L"Full Super Archiver disk drive emulation, including 6507 CPU."
			},

			{ "diskdrivesuperarchiverbw", L"Super Archiver w/BitWriter",
				L"Full emulation of a 1050 drive with the Super Archiver mod, including the BitWriter hardware which allows raw write capability."
			},

			{ "diskdrivetoms1050", L"TOMS 1050",
				L"Full TOMS 1050 disk drive emulation, including 6507 CPU."
			},

			{ "diskdrivetygrys1050", L"Tygrys 1050",
				L"Full Tygrys 1050 disk drive emulation, including 6507 CPU."
			},

			{ "diskdrive1050turbo", L"1050 Turbo",
				L"Full emulation of the 1050 Turbo disk drive modification by Bernhard Engl, including 6507 CPU."
			},

			{ "diskdrive1050turboii", L"1050 Turbo II",
				L"Full emulation of the 1050 Turbo II disk drive modification by Bernhard Engl, including 6507 CPU. This model is also known as Version 3.5 in some places."
			},

			{ "diskdriveisplate", L"I.S. Plate",
				L"Full emulation of the Innovated Software I.S. Plate modification for the 1050 disk drive (a.k.a. ISP or ISP Plate). The ISP "
				L"supports single, medium, and double density disks and track buffering."
			},

			{ "diskdriveindusgt", L"Indus GT",
				L"Full Indus GT disk drive emulation, including Z80 CPU and RamCharger. This will also work for TOMS Turbo Drive firmware.\n"
					L"The Indus GT supports single density, double density, and enhanced density formats. It does not, however, support double-sided disks."
			},

			{ "diskdrivexf551", L"XF551",
				L"Full XF551 disk drive emulation, including 8048 CPU.\n"
					L"The XF551 supports single density, double density, enhanced density, and double-sided double density formats."
			},

			{ "diskdriveatr8000", L"ATR8000",
				L"Full ATR8000 disk drive emulation, including Z80 CPU.\n"
					L"The ATR8000 supports up to four disk drives, either 5.25\" or 8\", as well as a built-in parallel "
					L"printer port and RS-232 serial port."
			},

			{ "diskdrivepercom", L"Percom RFD-40S1",
				L"Full Percom RFD-40S1 disk drive emulation, including 6809 CPU.\n"
					L"The RFD-40S1 supports up to four disk drives, which may be either single-sided or double-sided "
					L"and 40 or 80 track. Both single-density and double-density are supported by the hardware.\n"
					L"This will also work for the Astra 1001, which has compatible firmware with the RFD-40S1."
			},

			{ "diskdrivepercomat", L"Percom AT-88S1",
				L"Full Percom AT-88S1 disk drive emulation, including 6809 CPU.\n"
					L"The AT-88S1 supports up to four disk drives, which may be either single-sided or double-sided "
					L"and 40 or 80 track. Both single-density and double-density are supported by the hardware.\n"
					L"This device is the version without the printer interface; SPD firmware will not work on this device."
			},

			{ "diskdrivepercomatspd", L"Percom AT88-SPD",
				L"Full Percom AT88-SPD disk drive emulation, including 6809 CPU.\n"
					L"The AT88-SPD supports up to four disk drives, which may be either single-sided or double-sided "
					L"and 40 or 80 track. Both single-density and double-density are supported by the hardware.\n"
					L"This device is the version with the printer interface; non-SPD firmware will not work on this device."
			},

			{ "diskdriveamdc", L"Amdek AMDC-I/II",
				L"Full Amdek AMDC-I/II emulation, including 6809 CPU.\n"
					L"The AMDC-I/II has one or two internal 3\" disk drives, along with a port for two more external disk drives."
			},
		},
	},
	{
		L"Serial I/O bus devices",
		"sio",
		{
			{ "820", L"820 40-Column Printer",
				L"Basic 40 column dot-matrix printer, printing 3 7/8\" roll paper. Capable of 40 column conventional text and 29 character sideways text."
			},
			{ "820full", L"820 40-Column Printer (full emulation)",
				L"Basic 40 column dot-matrix printer, with full emulation of the 6507 microcontroller and dot matrix print rendering."
			},
			{ "835", L"835 Modem",
				L"A 300 baud modem that connects directly to the SIO port.\n"
			},
			{ "835full", L"835 Modem (full emulation)",
				L"A 300 baud modem that connects directly to the SIO port.\n"
					L"Full hardware emulation of 8048 microcontroller on the 835."
			},
			{ "850", L"850 Interface Module",
				L"A combination device with four RS-232 serial devices and a printer port. The serial ports "
					L"can be used to interface to a modem.\n"
					L"A R: handler is needed to use the serial ports of the 850. Depending on the emulation mode setting, "
					L"this can either be a simulated R: device or a real R: software handler. In Full emulation mode, "
					L"booting the computer without disk drives or with "
					L"the DOS 2.x AUTORUN.SYS will automatically load the R: handler from the 850; otherwise, "
					L"the tools in the Additions disk can be used to load the handler."
			},
			{ "850full", L"850 Interface Module (full emulation)",
				L"Full emulation of the 850 Interface Module's hardware and built-in controller. This requires 850 firmware "
					L"to function, and lacks acceleration support possible with the regular 850 emulation, but supports running "
					L"6502 code directly on the module."
			},
			{ "1020", L"1020 Color Printer",
				L"A four-color plotter that prints on roll paper with an 820-compatible printer protocol."
			},
			{ "1025", L"1025 80-Column Printer",
				L"80 column dot-matrix printer supporting double-width and condensed modes, adjustable line spacing, and international characters."
			},
			{ "1025full", L"1025 80-Column Printer (full emulation)",
				L"80 column dot-matrix printer supporting double-width and condensed modes, adjustable line spacing, and international characters."
			},
			{ "1029", L"1029 80-Column Printer",
				L"80 column dot-matrix printer supporting elongated text, adjustable line spacing, international characters, and bitmapped graphics."
			},
			{ "1029full", L"1029 80-Column Printer (full emulation)",
				L"80 column dot-matrix printer supporting elongated text, adjustable line spacing, international characters, and bitmapped graphics."
			},
			{ "1030", L"1030 Modem",
				L"A 300 baud modem that connects directly to the SIO port.\n"
					L"A T: handler is needed to use the 1030. Depending on the emulation mode setting, the T: "
					L"device can be simulated, or in Full mode the tools from the the Additions disk can be used to load "
					L"a software T: handler."
			},
			{ "1030full", L"1030 Modem (full emulation)",
				L"A 300 baud modem that connects directly to the SIO port.\n"
					L"Full hardware emulation of 8050 microcontroller on the 1030, including auto-booting of the "
					L"ModemLink firmware with no disk drive present."
			},
			{ "midimate", L"MidiMate",
				L"An SIO-based adapter for communicating with MIDI devices. This emulation links the MidiMate to the host "
					L"OS MIDI support. There is no connection to MIDI IN or to the SYNC inputs."
			},
			{ "pclink", L"PCLink",
				L"PC-based file server with SIO bus connection. Requires additional handler software to use, "
					L"included with recent versions of the SpartaDOS X Toolkit disk."
			},
			{ "pocketmodem", L"Pocket Modem",
				L"SIO-based modem capable of 110, 210, 300, and 500 baud operation. Requires an M: handler to use"
					L" (not included)."
			},
			{ "rverter", L"R-Verter",
				L"An adapter cable connecting a regular RS-232 serial device to the computer's SIO port.\n"
				L"An R: handler is needed to use the R-Verter. The R-Verter itself has no bootstrap capability, "
				L"so this handler must be loaded from disk or another source."
			},
			{ "sdrive", L"SDrive",
				L"A hardware disk emulator based on images stored on SD cards.\n"
				L"Currently, only raw sector access is implemented."
			},
			{ "sioserial", L"SIO serial adapter",
				L"An adapter between the SIO bus and a traditional serial port, similar to an SIO2PC or 10502PC adapter. "
				L"Allows connecting serial devices to the emulated SIO bus."
			},
			{ "sio2sd", L"SIO2SD",
				L"A hardware disk emulator based on images stored on SD cards.\n"
				L"Currently, only minimal configuration commands are implemented."
			},
			{ "sioclock", L"SIO Real-Time Clock",
				L"SIO-based real-time clock. This device implements APE, AspeQt, and SIO2USB RTC protocols."
			},
			{ "sx212", L"SX212 Modem",
				L"A 1200 baud modem that can be used either by RS-232 or SIO. This emulation is for the "
				L"SIO port connection.\n"
					L"An R: handler is needed to use the SX212. Depending on the emulation mode setting, the R: "
					L"device can be simulated, or in Full mode the tools from the the Additions disk can be used to load "
					L"a software R: handler."
			},
			{ "testsiopoll3", L"SIO Type 3 Poll Test Device",
				L"A fictitious SIO device for testing that implements the XL/XE operating system's boot-time handler "
					L"auto-load protocol (type 3 poll). The handler installs a T: device that simply eats "
					L"anything printed to it."
			},
			{ "testsiopoll4", L"SIO Type 4 Poll Test Device",
				L"A fictitious SIO device for testing that implements the XL/XE operating system's on demand handler "
					L"auto-load protocol (type 4 poll). The handler installs a T: device that simply eats "
					L"anything printed to it."
			},
			{ "testsiohs", L"SIO High Speed Test Device",
				L"A fictitious SIO device for testing that implements an ultra-high speed SIO device with an external clock. "
					L"The device ID is $31 and supports a pseudo-disk protocol."
			},
			{ "xm301", L"XM301 Modem",
				L"A 300 baud modem that connects directly to the SIO port, with auto-answer and audio support.\n"
					L"A T: handler is needed to use the XM301, which must be loaded from disk or included in software."
			},
		}
	},
	{
		L"High-level emulation (HLE) devices",
		"hle",
		{
			{ "hostfs", L"Host device (H:)",
				L"Adds an H: device to CIO to access files on the host computer. This can also be installed as D: for "
					L"programs that do disk access."
			},
			{ "printer", L"Printer (P:)",
				L"Reroutes text printed to P: to the Printer Output window in the emulator. 820 Printer emulation is also provided "
					L"for software that bypasses P:."
			},
			{ "browser", L"Browser (B:)",
				L"Adds a B: device that parses HTTP/HTTPS URLs written to it a line at a time and launches them in a web browser."
			},
		}
	},
	{
		L"Video source devices",
		"videosource",
		{
			{ "videogenerator", L"Video generator",
				L"Generates a static image frame for a composite video input."
			},
			{ "videostillimage", L"Video still image",
				L"Generates a still image frame for a composite video input from an image file."
			}
		}
	},
	{
		L"Add-on devices",
		"blackboxfloppy",
		{
			{ "blackboxfloppy", L"Black Box Floppy Board",
				L"Adds parallel bus based floppy drive support to the Black Box."
			}
		}
	},
	{
		L"Other devices",
		"other",
		{
			{ "custom", L"Custom device",
				L"Adds a custom device based on a device description file (*.atdevice). See Help for the custom device specification format."
			}
		}
	}
};

ATUIDialogDeviceNew::ATUIDialogDeviceNew(const vdfunction<bool(const char *)>& filter)
	: VDResizableDialogFrameW32(IDD_DEVICE_NEW)
	, mFilter(filter)
	, mpDeviceTag(nullptr)
	, mpLastHelpText(nullptr)
{
	mTreeView.OnItemSelectionChanged() += mDelItemSelChanged.Bind(this, &ATUIDialogDeviceNew::OnItemSelectionChanged);
}

bool ATUIDialogDeviceNew::OnLoaded() {
	AddProxy(&mTreeView, IDC_TREE);
	AddProxy(&mHelpView, IDC_HELP_INFO);

	mResizer.Add(IDC_HELP_INFO, mResizer.kML | mResizer.kSuppressFontChange);
	mResizer.Add(IDC_TREE, mResizer.kMC);
	mResizer.Add(IDOK, mResizer.kBR);
	mResizer.Add(IDCANCEL, mResizer.kBR);

	ATUIRestoreWindowPlacement(mhdlg, "Add new device", SW_SHOW, true);

	mHelpView.SetReadOnlyBackground();

	VDStringA s;
	
	s = "{\\rtf";

	const uint32 fg = ATUIGetThemeColors().mStaticFg;
	s.append_sprintf("{\\colortbl;\\red%u\\green%u\\blue%u;}"
		, (fg >> 16) & 0xFF
		, (fg >>  8) & 0xFF
		, fg & 0xFF);

	s += "{\\fonttbl{\\f0\\fcharset0 MS Shell Dlg;}} \\f0\\cf1\\sa90\\fs16 ";

	AppendRTF(s, L"Select a device to add.");
	s += "}";

	mHelpView.SetTextRTF(s.c_str());

	mTreeView.SetRedraw(false);

	for(const auto& category : kCategories) {
		if (mFilter && !mFilter(category.mpTag))
			continue;

		VDUIProxyTreeViewControl::NodeRef catNode = mTreeView.AddItem(mTreeView.kNodeRoot, mTreeView.kNodeLast, category.mpText);

		vdfastvector<const TreeEntry *> devs;

		for(const auto& device : category.mDevices)
			devs.push_back(&device);

		std::ranges::sort(
			devs,
			[](const TreeEntry *a, const TreeEntry *b) {
				return VDStringSpanW(a->mpText).comparei(b->mpText) < 0;
			}
		);

		for(const auto *device : devs)
			mTreeView.AddVirtualItem(catNode, mTreeView.kNodeLast, vdmakerefptr(new TreeNode(*device)));

		mTreeView.ExpandNode(catNode, true);
	}

	mTreeView.SetRedraw(true);
	SetFocusToControl(IDC_TREE);

	VDDialogFrameW32::OnLoaded();

	return true;
}

void ATUIDialogDeviceNew::OnDestroy() {
	ATUISaveWindowPlacement(mhdlg, "Add new device");
}

bool ATUIDialogDeviceNew::OnOK() {
	if (VDResizableDialogFrameW32::OnOK())
		return true;

	if (!strncmp(mpDeviceTag, "diskdrive", 9)) {
		if (!ATUIConfirmAddFullDrive())
			return true;
	}

	return false;
}

void ATUIDialogDeviceNew::OnDataExchange(bool write) {
	if (write) {
		TreeNode *node = static_cast<TreeNode *>(mTreeView.GetSelectedVirtualItem());

		if (!node) {
			FailValidation(IDC_TREE);
			return;
		}

		mpDeviceTag = node->mNode.mpTag;
	}
}

void ATUIDialogDeviceNew::OnItemSelectionChanged(VDUIProxyTreeViewControl *sender, int) {
	TreeNode *node = static_cast<TreeNode *>(mTreeView.GetSelectedVirtualItem());

	EnableControl(IDOK, node != nullptr);

	UpdateHelpText();
}

void ATUIDialogDeviceNew::UpdateHelpText() {
	TreeNode *node = static_cast<TreeNode *>(mTreeView.GetSelectedVirtualItem());
	if (!node) {
		if (mpLastHelpText) {
			mpLastHelpText = nullptr;

			mHelpView.SetTextRTF("{\\rtf}");
		}
		return;
	}

	const TreeEntry& te = node->mNode;

	if (mpLastHelpText == te.mpHelpText)
		return;

	mpLastHelpText = te.mpHelpText;

	VDStringA s;

	s = "{\\rtf";

	const uint32 fg = ATUIGetThemeColors().mStaticFg;
	s.append_sprintf("{\\colortbl;\\red%u\\green%u\\blue%u;}"
		, (fg >> 16) & 0xFF
		, (fg >>  8) & 0xFF
		, fg & 0xFF);

	s += "{\\fonttbl{\\f0\\fnil\\fcharset0 MS Shell Dlg;}}\\cf1\\f0\\sa90\\fs16{\\b ";
	AppendRTF(s, te.mpText);
	s += "}\\par ";
	AppendRTF(s, te.mpHelpText);
	s += "}";

	mHelpView.SetTextRTF(s.c_str());
}

void ATUIDialogDeviceNew::AppendRTF(VDStringA& rtf, const wchar_t *text) {
	while(const wchar_t c = *text++) {
		if (c == '\n')
			rtf += "\\par ";
		else if (c < 0x20 || c >= 0x7f)
			rtf.append_sprintf("\\u%d?", (sint16)c);	// yes, this needs to be negative at times
		else if (c == '{' || c == '}' || c == '\\')
			rtf.append_sprintf("\\'%02x", c);
		else
			rtf += (char)c;
	}
}

///////////////////////////////////////////////////////////////////////////

struct ATUIControllerDevices::DeviceNode final : public vdrefcounted<IVDUITreeViewVirtualItem> {
	DeviceNode(IATDevice *dev, const wchar_t *prefix) : mpDev(dev) {
		ATDeviceInfo info;
		dev->GetDeviceInfo(info);

		mName = prefix;
		mName.append(info.mpDef->mpName);
		mbHasSettings = info.mpDef->mpConfigTag != nullptr;
		mbCanRemove = !(info.mpDef->mFlags & kATDeviceDefFlag_Internal);
	}

	DeviceNode(DeviceNode *parentNode, IATDeviceParent *parent, sint32 busId) {
		mpParent = parentNode;
		mpDevParent = parent;
		mBusId = busId;

		mpDevBus = parent->GetDeviceBusById(busId);

		mName = mpDevBus->GetBusName();
		mbHasSettings = false;
		mbCanRemove = false;
	}

	DeviceNode(DeviceNode *parent, const wchar_t *label) {
		mName = label;
		mpParent = parent;
	}

	void *AsInterface(uint32 id) override {
		return nullptr;
	}

	void GetText(VDStringW& s) const override {
		s = mName;

		if (mpDev) {
			VDStringW buf;
			mpDev->GetSettingsBlurb(buf);

			if (!buf.empty()) {
				s += L" - ";
				s += buf;
			}
		}
	}

	vdrefptr<IATDevice> mpDev;
	IATDeviceParent *mpDevParent = nullptr;
	sint32 mBusId = -1;
	IATDeviceBus *mpDevBus = nullptr;
	VDStringW mName;
	bool mbHasSettings = false;
	bool mbCanRemove = false;
	VDUIProxyTreeViewControl::NodeRef mNode = {};
	VDUIProxyTreeViewControl::NodeRef mChildNode = {};
	DeviceNode *mpParent = nullptr;

	vdfastvector<VDUIProxyTreeViewControl::NodeRef> mErrorNodes;
};

///////////////////////////////////////////////////////////////////////////////

class ATUIControllerDevices::XCmdRemove final : public vdrefcounted<IATDeviceXCmd> {
public:
	XCmdRemove(ATUIControllerDevices& parent, DeviceNode *node) : mParent(parent), mpNode(node) {}

	bool IsPostRefreshNeeded() const override { return false; }
	bool IsSupported(IATDevice *dev, int busIndex) const override;
	ATDeviceXCmdInfo GetInfo() const override;
	void Invoke(ATDeviceManager& devmgr, IATDevice *dev, int busIndex) override;

private:
	ATUIControllerDevices& mParent;
	vdrefptr<DeviceNode> mpNode;
};

bool ATUIControllerDevices::XCmdRemove::IsSupported(IATDevice *dev, int busIndex) const
{
	return mpNode && mpNode->mbCanRemove;
}

ATDeviceXCmdInfo ATUIControllerDevices::XCmdRemove::GetInfo() const
{
	ATDeviceXCmdInfo info {};
	info.mDisplayName = L"Remove";
	info.mbRequiresElevation = false;

	return info;
}

void ATUIControllerDevices::XCmdRemove::Invoke(ATDeviceManager& devmgr, IATDevice *dev, int busIndex)
{
	mParent.Remove(mpNode);
}

///////////////////////////////////////////////////////////////////////////////

class ATUIControllerDevices::XCmdSettings final : public vdrefcounted<IATDeviceXCmd> {
public:
	XCmdSettings(ATUIControllerDevices& parent, DeviceNode *node) : mParent(parent), mpNode(node) {}

	bool IsPostRefreshNeeded() const override { return false; }
	bool IsSupported(IATDevice *dev, int busIndex) const override;
	ATDeviceXCmdInfo GetInfo() const override;
	void Invoke(ATDeviceManager& devmgr, IATDevice *dev, int busIndex) override;

private:
	ATUIControllerDevices& mParent;
	vdrefptr<DeviceNode> mpNode;
};

bool ATUIControllerDevices::XCmdSettings::IsSupported(IATDevice *dev, int busIndex) const
{
	return mpNode && mpNode->mbHasSettings;
}

ATDeviceXCmdInfo ATUIControllerDevices::XCmdSettings::GetInfo() const
{
	ATDeviceXCmdInfo info {};
	info.mDisplayName = L"Settings...";
	info.mbRequiresElevation = false;

	return info;
}

void ATUIControllerDevices::XCmdSettings::Invoke(ATDeviceManager& devmgr, IATDevice *dev, int busIndex)
{
	mParent.Settings(mpNode);
}

///////////////////////////////////////////////////////////////////////////////
ATUIControllerDevices::ATUIControllerDevices(VDDialogFrameW32& parent, ATDeviceManager& devMgr, VDUIProxyTreeViewControl& treeView, VDUIProxyButtonControl& settingsView, VDUIProxyButtonControl& removeView,
	VDUIProxyButtonControl& moreView)
	: mParent(parent)
	, mDevMgr(devMgr)
	, mTreeView(treeView)
	, mSettingsView(settingsView)
	, mRemoveView(removeView)
	, mMoreView(moreView)
{
	mTreeView.OnItemSelectionChanged() += mDelSelectionChanged.Bind(this, &ATUIControllerDevices::OnItemSelectionChanged);
	mTreeView.OnItemDoubleClicked() += mDelDoubleClicked.Bind(this, &ATUIControllerDevices::OnItemDoubleClicked);
	mTreeView.OnItemGetDisplayAttributes() += mDelGetDisplayAttributes.Bind(this, &ATUIControllerDevices::OnItemGetDisplayAttributes);
	mTreeView.SetOnContextMenu([this](const auto& event) -> bool { return OnContextMenu(event); });

	mDeviceStatusCallback = std::bind_front(&ATUIControllerDevices::OnDeviceStatusChanged, this);
	mDevMgr.AddDeviceStatusCallback(&mDeviceStatusCallback);
}

ATUIControllerDevices::~ATUIControllerDevices() {
	mDevMgr.RemoveDeviceStatusCallback(&mDeviceStatusCallback);
}

void ATUIControllerDevices::OnDataExchange(bool write) {
	if (write) {
	} else {
		mDeviceNodeLookup.clear();

		mTreeView.SetRedraw(false);
		mTreeView.Clear();

		auto p = mTreeView.AddItem(mTreeView.kNodeRoot, mTreeView.kNodeLast, L"Computer");

		for(IATDevice *dev : mDevMgr.GetDevices(true, true, false))
			CreateDeviceNode(p, dev, L"");

		mTreeView.ExpandNode(p, true);

		mTreeView.SetRedraw(true);
	}
}

void ATUIControllerDevices::OnDpiChanged() {
	UpdateIcons();
}

void ATUIControllerDevices::Add() {
	IATDeviceParent *devParent = nullptr;
	IATDeviceBus *devBus = nullptr;

	auto *p = static_cast<DeviceNode *>(mTreeView.GetSelectedVirtualItem());
	if (p) {
		for(;;) {
			if (!devBus)
				devBus = p->mpDevBus;

			if (!p->mpParent)
				break;

			p = p->mpParent;
		}

		devParent = vdpoly_cast<IATDeviceParent *>(p->mpDev);

		// ignore devices that have no actual buses, just to make shared device
		// base implementation easier
		if (devParent && devParent->GetDeviceBusIdByIndex(0) < 0)
			devParent = nullptr;
	}

	static const char *const kBaseCategories[]={
		"pbi",
		"cartridge",
		"internal",
		"joyport",
		"sio",
		"hle",
		"other",
	};

	vdfunction<bool(const char *)> filter;

	if (devBus) {
		filter = [devBus](const char *tag) -> bool {
			uint32 i=0;

			while(const char *s = devBus->GetSupportedType(i++)) {
				if (!strcmp(tag, s))
					return true;
			}

			return false;
		};
	} else if (devParent) {
		filter = [devParent](const char *tag) -> bool {
			uint32 busIndex = 0;

			for(;;) {
				sint32 busId = devParent->GetDeviceBusIdByIndex(busIndex++);
				if (busId < 0)
					break;

				IATDeviceBus *bus = devParent->GetDeviceBusById(busId);
				if (!bus) {
					VDFAIL("Device returned invalid bus ID");
					continue;
				}

				uint32 i = 0;

				while(const char *s = bus->GetSupportedType(i++)) {
					if (!strcmp(tag, s))
						return true;
				}
			}

			return false;
		};
	} else {
		filter = [](const char *tag) -> bool {
			for(const char *s : kBaseCategories) {
				if (!strcmp(s, tag))
					return true;
			}

			return false;
		};
	}

	ATUIDialogDeviceNew dlg(filter);
	if (dlg.ShowDialog(&mParent)) {
		const ATDeviceDefinition *def = mDevMgr.GetDeviceDefinition(dlg.GetDeviceTag());

		if (!def) {
			VDFAIL("Device dialog contains unregistered device type.");
			return;
		}

		ATPropertySet props;

		{
			VDRegistryAppKey key("Device config history", false);
			VDStringW propstr;
			key.getString(dlg.GetDeviceTag(), propstr);
			if (!propstr.empty())
				mDevMgr.DeserializeProps(props, propstr.c_str());
		}

		ATDeviceConfigureFn cfn = mDevMgr.GetDeviceConfigureFn(dlg.GetDeviceTag());

		if (!cfn || cfn((VDGUIHandle)mParent.GetWindowHandle(), props)) {
			const bool needsReboot = (def->mFlags & kATDeviceDefFlag_RebootOnPlug) != 0;

			if (needsReboot) {
				if (!ATUIConfirmReset((VDGUIHandle)mParent.GetWindowHandle(), "AddDevicesAndReboot", L"The emulated computer will be rebooted to add this device. Are you sure?", L"Adding device and rebooting"))
					return;
			}

			vdrefptr<IATDevice> dev;
			try {
				dev = mDevMgr.AddDevice(def, props, devParent != nullptr || devBus != nullptr);

				try {
					if (devBus)
						devBus->AddChildDevice(dev);
					else if (devParent) {
						for(uint32 busIndex = 0; ; ++busIndex) {
							sint32 busId = devParent->GetDeviceBusIdByIndex(busIndex);
							if (busId < 0)
								break;

							IATDeviceBus *bus = devParent->GetDeviceBusById(busId);
							if (!bus) {
								VDFAIL("Device returned invalid bus ID");
								continue;
							}

							bus->AddChildDevice(dev);
							if (dev->GetParent())
								break;
						}
					}

					SaveSettings(dlg.GetDeviceTag(), props);
				} catch(...) {
					mDevMgr.RemoveDevice(dev);
					throw;
				}
			} catch(const MyError& err) {
				mParent.ShowError2(err, L"Unable to create device");
				return;
			}

			if (needsReboot)
				ATUIConfirmResetComplete();

			OnDataExchange(false);

			VDUIProxyTreeViewControl::NodeRef itemToSelect = VDUIProxyTreeViewControl::kNodeRoot;

			mTreeView.EnumChildrenRecursive(VDUIProxyTreeViewControl::kNodeRoot,
				[p = dev.get(), &itemToSelect](IVDUITreeViewVirtualItem *item) {
					DeviceNode *node = static_cast<DeviceNode *>(item);

					if (node->mpDev == p)
						itemToSelect = node->mNode;
				}
			);

			if (itemToSelect != VDUIProxyTreeViewControl::kNodeRoot)
				mTreeView.SelectNode(itemToSelect);
		}
	}
}

void ATUIControllerDevices::Remove() {
	auto *p = static_cast<DeviceNode *>(mTreeView.GetSelectedVirtualItem());

	Remove(p);
}

void ATUIControllerDevices::Remove(DeviceNode *p) {
	if (!p || !p->mpDev || !p->mbCanRemove)
		return;

	IATDevice *dev = p->mpDev;
	vdrefptr<IATDevice> devholder(dev);

	vdfastvector<IATDevice *> childDevices;
	mDevMgr.MarkAndSweep(&dev, 1, childDevices);

	ATDeviceInfo info;
	dev->GetDeviceInfo(info);

	bool needsReboot = false;

	if (info.mpDef->mFlags & kATDeviceDefFlag_RebootOnPlug)
		needsReboot = true;

	if (childDevices.empty()) {
		if (needsReboot) {
			if (!ATUIConfirmReset((VDGUIHandle)mParent.GetWindowHandle(), "RemoveDevicesAndReboot", L"The emulated computer will be rebooted to remove this device. Are you sure?", L"Removing devices and rebooting"))
				return;
		}
	} else {
		for(IATDevice *dev : childDevices) {
			ATDeviceInfo info;
			dev->GetDeviceInfo(info);

			if (info.mpDef->mFlags & kATDeviceDefFlag_RebootOnPlug) {
				needsReboot = true;
				break;
			}
		}

		VDStringW msg;

		msg = L"These attached devices will also be removed:\n\n";

		for(auto it = childDevices.begin(), itEnd = childDevices.end();
			it != itEnd;
			++it)
		{
			ATDeviceInfo info;
			(*it)->GetDeviceInfo(info);

			msg.append_sprintf(L"    %ls\n", info.mpDef->mpName);
		}

		msg += L"\nProceed?";

		if (needsReboot) {
			if (!ATUIConfirmReset((VDGUIHandle)mParent.GetWindowHandle(), "RemoveDevicesAndReboot", msg.c_str(), L"Removing devices and rebooting"))
				return;
		} else {
			if (!mParent.Confirm2("RemoveDevices", msg.c_str(), L"Removing devices"))
				return;
		}
	}
	
	p->mpDev = nullptr;

	IATDeviceParent *parent = dev->GetParent();
	sint32 parentBusId = -1;

	if (parent) {
		parentBusId = dev->GetParentBusId();
		parent->GetDeviceBusById(parentBusId)->RemoveChildDevice(dev);
	}

	vdrefptr<IATDevice> parentDevice(vdpoly_cast<IATDevice *>(parent));

	mDevMgr.RemoveDevice(dev);

	while(!childDevices.empty()) {
		IATDevice *child = childDevices.back();
		childDevices.pop_back();

		mDevMgr.RemoveDevice(child);
	}

	if (needsReboot)
		ATUIConfirmResetComplete();

	OnDataExchange(false);

	VDUIProxyTreeViewControl::NodeRef itemToSelect = VDUIProxyTreeViewControl::kNodeRoot;
	if (parent) {
		mTreeView.EnumChildrenRecursive(VDUIProxyTreeViewControl::kNodeRoot,
			[parent, parentBusId, &itemToSelect](IVDUITreeViewVirtualItem *item) {
				DeviceNode *node = static_cast<DeviceNode *>(item);

				if (node->mpDevParent == parent && node->mBusId == parentBusId)
					itemToSelect = node->mNode;
			}
		);
	}

	if (itemToSelect == VDUIProxyTreeViewControl::kNodeRoot)
		itemToSelect = mTreeView.GetChildNode(itemToSelect);

	if (itemToSelect != VDUIProxyTreeViewControl::kNodeRoot)
		mTreeView.SelectNode(itemToSelect);
}

void ATUIControllerDevices::RemoveAll() {
	bool needsReboot = false;

	for(IATDevice *dev : mDevMgr.GetDevices(false, true, true)) {
		ATDeviceInfo info;
		dev->GetDeviceInfo(info);

		if (info.mpDef->mFlags & kATDeviceDefFlag_RebootOnPlug) {
			needsReboot = true;
			break;
		}
	}

	if (needsReboot) {
		if (!ATUIConfirmReset((VDGUIHandle)mParent.GetWindowHandle(), "RemoveAllDevicesAndReboot", L"This will remove all devices and reboot the emulated computer. Are you sure?", L"Removing devices and rebooting"))
			return;
	} else {
		if (!mParent.Confirm2("RemoveAllDevices", L"This will remove all devices. Are you sure?", L"Removing devices"))
			return;
	}

	mDevMgr.RemoveAllDevices(false);

	if (needsReboot)
		ATUIConfirmResetComplete();

	OnDataExchange(false);
}

void ATUIControllerDevices::Settings() {
	auto *p = static_cast<DeviceNode *>(mTreeView.GetSelectedVirtualItem());

	Settings(p);
}

void ATUIControllerDevices::Settings(DeviceNode *p) {
	if (!p || !p->mpDev || !p->mbHasSettings)
		return;

	IATDevice *dev = p->mpDev;
	if (!dev)
		return;

	ATDeviceInfo info;
	dev->GetDeviceInfo(info);

	if (!info.mpDef->mpConfigTag)
		return;

	const VDStringA configTag(info.mpDef->mpConfigTag);
	ATDeviceConfigureFn fn = mDevMgr.GetDeviceConfigureFn(configTag.c_str());
	if (!fn)
		return;

	ATPropertySet pset;
	dev->GetSettings(pset);
	
	if (fn((VDGUIHandle)mParent.GetWindowHandle(), pset)) {
		try {
			mDevMgr.ReconfigureDevice(*dev, pset);

			SaveSettings(configTag.c_str(), pset);
		} catch(const MyError& err) {
			err.post(mParent.GetWindowHandle(), "Error");
		}

		// We may have lost the previous device, so we need to reinit the tree
		// even on failure.
		OnDataExchange(false);
	}
}

void ATUIControllerDevices::More() {
	VDUIProxyTreeViewControl::ContextMenuEvent event {};
	event.mpItem = mTreeView.GetSelectedVirtualItem();

	if (event.mpItem) {
		DisplayMore(event, true);
	}
}

void ATUIControllerDevices::Copy() {
	IATDevice *dev = nullptr;
	sint32 busIndex = -1;
	if (GetXCmdContext(dev, busIndex)) {
		ExecuteXCmd(dev, busIndex, ATGetDeviceXCmdCopyWithChildren());
	}
}

void ATUIControllerDevices::Paste() {
	IATDevice *dev = nullptr;
	sint32 busIndex = -1;
	if (GetXCmdContext(dev, busIndex)) {
		ExecuteXCmd(dev, busIndex, ATGetDeviceXCmdPaste());
	}
}

void ATUIControllerDevices::CreateDeviceNode(VDUIProxyTreeViewControl::NodeRef parentNode, IATDevice *dev, const wchar_t *prefix) {
	auto nodeObject = vdmakerefptr(new DeviceNode(dev, prefix));

	mDeviceNodeLookup.insert_as(dev).first->second = nodeObject;

	auto devnode = mTreeView.AddVirtualItem(parentNode, mTreeView.kNodeLast, nodeObject);

	nodeObject->mNode = devnode;

	RefreshNodeErrors(*nodeObject);

	IATDeviceParent *devParent = vdpoly_cast<IATDeviceParent *>(dev);
	if (devParent) {
		for(uint32 busIndex = 0; ; ++busIndex) {
			sint32 busId = devParent->GetDeviceBusIdByIndex(busIndex);
			if (busId < 0)
				break;

			IATDeviceBus *bus = devParent->GetDeviceBusById(busId);
			if (!bus) {
				VDFAIL("Device returned invalid bus ID");
				continue;
			}

			auto busNode = vdmakerefptr(new DeviceNode(nodeObject, devParent, busId));
			auto busTreeNode = mTreeView.AddVirtualItem(devnode, mTreeView.kNodeLast, busNode);

			busNode->mNode = busTreeNode;

			vdfastvector<IATDevice *> childDevs;

			bus->GetChildDevices(childDevs);

			if (childDevs.empty()) {
				auto emptyNode = vdmakerefptr(new DeviceNode(busNode, L"(No attached devices)"));
				auto emptyTreeNode = mTreeView.AddVirtualItem(busTreeNode, mTreeView.kNodeLast, emptyNode);

				emptyNode->mNode = emptyTreeNode;
				nodeObject->mChildNode = emptyTreeNode;
			} else {
				uint32 index = 0;
				VDStringW childPrefix;

				for(IATDevice *child : childDevs) {
					VDASSERT(child->GetParent() == devParent);

					childPrefix.clear();
					bus->GetChildDevicePrefix(index++, childPrefix);
					CreateDeviceNode(busTreeNode, child, childPrefix.c_str());
				}
			}
		}
	}

	mTreeView.ExpandNode(devnode, true);
}

void ATUIControllerDevices::SaveSettings(const char *configTag, const ATPropertySet& props) {
	VDRegistryAppKey key("Device config history", true);
	if (props.IsEmpty()) {
		key.removeValue(configTag);
	} else {
		VDStringW s;
		mDevMgr.SerializeProps(props, s);
		key.setString(configTag, s.c_str());
	}
}

void ATUIControllerDevices::OnItemSelectionChanged(VDUIProxyTreeViewControl *sender, int idx) {
	const auto *node = static_cast<DeviceNode *>(mTreeView.GetSelectedVirtualItem());
	bool enabled = node && node->mbHasSettings;

	mSettingsView.SetEnabled(enabled);
	mRemoveView.SetEnabled(node && node->mbCanRemove);
	mMoreView.SetEnabled(node && node->mpDev);
}

void ATUIControllerDevices::OnItemDoubleClicked(VDUIProxyTreeViewControl *sender, bool *handled) {
	Settings();

	*handled = true;
}

void ATUIControllerDevices::OnItemGetDisplayAttributes(VDUIProxyTreeViewControl *sender, VDUIProxyTreeViewControl::GetDispAttrEvent *event) {
	const auto *node = static_cast<DeviceNode *>(event->mpItem);

	if (node && node->mpParent)
		event->mbIsMuted = true;
}

bool ATUIControllerDevices::OnContextMenu(const VDUIProxyTreeViewControl::ContextMenuEvent& event) {
	return DisplayMore(event, false);
}

void ATUIControllerDevices::OnDeviceStatusChanged(IATDevice& dev) {
	if (mDevicesToRefresh.insert(vdrefptr(&dev)).second) {
		if (!mbDeviceRefreshQueued) {
			mbDeviceRefreshQueued = true;

			mParent.PostCall([this] { RefreshDeviceStatuses(); });
		}
	}
}

void ATUIControllerDevices::RefreshDeviceStatuses() {
	if (mbDeviceRefreshQueued) {
		mbDeviceRefreshQueued = false;

		auto devices(std::move(mDevicesToRefresh));
		mDevicesToRefresh.clear();

		for(IATDevice *dev : devices) {
			auto it = mDeviceNodeLookup.find(dev);
			if (it != mDeviceNodeLookup.end()) {
				RefreshNodeErrors(*it->second);
			}
		}
	}
}

void ATUIControllerDevices::RefreshNodeErrors(DeviceNode& devnode) {
	IATDevice *dev = devnode.mpDev;
	auto devtreenode = devnode.mNode;

	while(!devnode.mErrorNodes.empty()) {
		mTreeView.DeleteItem(devnode.mErrorNodes.back());
		devnode.mErrorNodes.pop_back();
	}

	if (IATDeviceFirmware *fw = vdpoly_cast<IATDeviceFirmware *>(dev)) {
		ATDeviceFirmwareStatus status = fw->GetFirmwareStatus();

		if (status != ATDeviceFirmwareStatus::OK) {
			auto node = mTreeView.AddItem(devtreenode, mTreeView.kNodeLast, status == ATDeviceFirmwareStatus::Invalid ? L"Current device firmware failed validation checks and may not work" : L"Missing firmware for device");
			mTreeView.SetNodeImage(node, 1);
			devnode.mErrorNodes.push_back(node);
		}
	}

	if (IATDeviceDiskDrive *dd = vdpoly_cast<IATDeviceDiskDrive *>(dev)) {
		uint32 index = 0;

		for(;;) {
			const auto& diBinding = dd->GetDiskInterfaceClient(index);

			if (!diBinding.mpClient)
				break;

			ATDiskInterface& di = g_sim.GetDiskInterface(diBinding.mInterfaceIndex);

			IATDiskImage *image = di.GetDiskImage();
			if (image && !diBinding.mpClient->IsImageSupported(*image)) {
				const ATDiskGeometryInfo& geometry = image->GetGeometry();

				VDStringW msg;
				msg.sprintf(L"Disk drive model cannot read disk format in D%u: (%u tracks of %u sectors%s%s%s)"
					, (unsigned)diBinding.mInterfaceIndex + 1
					, geometry.mTrackCount
					, geometry.mSectorsPerTrack
					, geometry.mSideCount > 1 ? L", double-sided" : L""
					, geometry.mbMFM ? L", MFM" : L""
					, geometry.mbHighDensity ? L", HD" : L""
				);

				auto node = mTreeView.AddItem(devtreenode, mTreeView.kNodeLast, msg.c_str());
				mTreeView.SetNodeImage(node, 1);
				devnode.mErrorNodes.push_back(node);
				break;
			}

			++index;
		}
	}

	VDStringW err;
	for(uint32 i=0; dev->GetErrorStatus(i, err); ++i) {
		auto node = mTreeView.AddItem(devtreenode, mTreeView.kNodeLast, err.c_str());
		mTreeView.SetNodeImage(node, 1);
		devnode.mErrorNodes.push_back(node);
	}

	mTreeView.ExpandNode(devtreenode, true);
}

bool ATUIControllerDevices::DisplayMore(const VDUIProxyTreeViewControl::ContextMenuEvent& event, bool fromButton) {
	vdvector<VDDialogFrameW32::PopupMenuItem> items;

	DeviceNode *node = static_cast<DeviceNode *>(event.mpItem);
	vdfastvector<IATDeviceXCmd *> xcmds;
	vdfastvector<int> xcmdOrder;
	IATDevice *dev = nullptr;
	sint32 busIndex = -1;
	bool validNode = GetXCmdContext(event.mNode, node, dev, busIndex);

	vdrefptr<IATDeviceXCmd> removeXcmd;
	vdrefptr<IATDeviceXCmd> settingsXcmd;
	if (validNode) {
		xcmds = mDevMgr.GetExtendedCommandsForDevice(dev, busIndex);

		if (node) {
			if (node->mbCanRemove) {
				removeXcmd = new XCmdRemove(*this, node);
				xcmds.emplace_back(removeXcmd);
			}

			if (node->mbHasSettings) {
				settingsXcmd = new XCmdSettings(*this, node);
				xcmds.emplace_back(settingsXcmd);
			}
		}

		auto n = xcmds.size();

		xcmdOrder.resize(n);
		for(size_t i=0; i<n; ++i)
			xcmdOrder[i] = i;

		vdvector<ATDeviceXCmdInfo> xcmdInfos;
		xcmdInfos.reserve(n);

		for(IATDeviceXCmd *xcmd : xcmds)
			xcmdInfos.emplace_back(xcmd->GetInfo());

		std::sort(xcmdOrder.begin(), xcmdOrder.end(),
			[&xcmdInfos](int i, int j) {
				return xcmdInfos[i].mDisplayName.comparei(xcmdInfos[j].mDisplayName) < 0;
			}
		);

		for(int index : xcmdOrder) {
			const auto& xcmdInfo = xcmdInfos[index];

			items.emplace_back();
			auto& menuItem = items.back();

			menuItem.mDisplayName = xcmdInfo.mDisplayName;
			menuItem.mbElevationRequired = xcmdInfo.mbRequiresElevation;
		}
	}

	if (items.empty()) {
		items.emplace_back();

		auto& noItem = items.back();
		noItem.mDisplayName = L"No commands available";
		noItem.mbDisabled = true;
	}

	int index;
	
	if (fromButton)
		index = mParent.ActivateMenuButton(mMoreView.GetWindowId(), vdspan(items.begin(), items.end()));
	else
		index = mParent.ActivatePopupMenu(event.mScreenPos.x, event.mScreenPos.y, vdspan(items.begin(), items.end()));

	if ((unsigned)index < xcmdOrder.size() && !items[index].mbDisabled) {
		ExecuteXCmd(dev, busIndex, *xcmds[xcmdOrder[index]]);
		return true;
	}

	return false;
}

bool ATUIControllerDevices::GetXCmdContext(IATDevice *&dev, sint32& busIndex) {
	return GetXCmdContext(mTreeView.GetSelectedNode(), static_cast<DeviceNode *>(mTreeView.GetSelectedVirtualItem()), dev, busIndex);
}

bool ATUIControllerDevices::GetXCmdContext(VDUIProxyTreeViewControl::NodeRef selectedTreeNode, DeviceNode *selectedDeviceNode, IATDevice *&dev, sint32& busId) {
	dev = nullptr;
	busId = -1;

	if (selectedTreeNode && selectedTreeNode == mTreeView.GetRootNode()) {
		// computer node
		return true;
	}
	
	if (!selectedDeviceNode)
		return false;

	if (selectedDeviceNode->mpDev) {
		// device node
		dev = selectedDeviceNode->mpDev;

		return true;
	} else if (selectedDeviceNode->mpDevParent) {
		// bus node
		dev = selectedDeviceNode->mpParent->mpDev;
		busId = selectedDeviceNode->mBusId;

		return true;
	}

	return false;
}

void ATUIControllerDevices::ExecuteXCmd(IATDevice *dev, sint32 busIndex, IATDeviceXCmd& xcmd) {
	try {
		xcmd.Invoke(mDevMgr, dev, busIndex);
	} catch(const MyError& e) {
		mParent.ShowError(e);
	}

	if (xcmd.IsPostRefreshNeeded())
		OnDataExchange(false);
}

void ATUIControllerDevices::UpdateIcons() {
	mTreeView.InitImageList(1, 0, 0);

	VDPixmapBuffer pxbuf;
	if (ATLoadImageResource(IDB_WARNING, pxbuf))
		mTreeView.AddImage(pxbuf);
}
