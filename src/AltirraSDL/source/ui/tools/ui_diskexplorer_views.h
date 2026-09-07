//	AltirraSDL - Disk Explorer format viewers

#ifndef f_ATUI_DISKEXPLORER_VIEWS_H
#define f_ATUI_DISKEXPLORER_VIEWS_H

#include <cstddef>
#include <vector>
#include <vd2/system/VDString.h>

struct ATUIXEXSegmentInfo {
	unsigned start = 0;
	unsigned end = 0;
	size_t dataOffset = 0;
};

bool ATUIParseXEX(const unsigned char *data, size_t len,
	std::vector<ATUIXEXSegmentInfo>& segments, VDStringA& error);

bool ATUIDecodeAtariBasic(const unsigned char *data, size_t len, VDStringA& text, VDStringA& error);
bool ATUIDecodeSynAssembler(const unsigned char *data, size_t len, VDStringA& text, VDStringA& error);
bool ATUIDisassemble6502(const unsigned char *data, size_t len, unsigned baseAddress, VDStringA& text, VDStringA& error);

#endif
