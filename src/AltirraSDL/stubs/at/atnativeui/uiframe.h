//	Altirra SDL3 build - stub for <at/atnativeui/uiframe.h>
//	Provides only the declarations needed by debugger.cpp and other
//	cross-platform source files, without pulling in <windows.h>.

#ifndef AT_UIFRAME_H
#define AT_UIFRAME_H

#include <new>
#include <vd2/system/vdtypes.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/VDString.h>

class ATContainerWindow;
class ATContainerDockingPane;
class ATFrameWindow;
class ATUIPane;

enum {
	kATContainerDockCenter,
	kATContainerDockLeft,
	kATContainerDockRight,
	kATContainerDockTop,
	kATContainerDockBottom
};

typedef bool (*ATPaneCreator)(ATUIPane **);
typedef bool (*ATPaneClassCreator)(uint32 id, ATUIPane **);

template<class T, class U>
bool ATUIPaneClassFactory(uint32 id, U **pp) {
	T *p = new(std::nothrow) T(id);
	if (!p)
		return false;

	*pp = static_cast<U *>(p);
	p->AddRef();
	return true;
}

void ATRegisterUIPaneType(uint32 id, ATPaneCreator creator);
void ATRegisterUIPaneClass(uint32 id, ATPaneClassCreator creator);

void ATActivateUIPane(uint32 id, bool giveFocus, bool visible = true, uint32 relid = 0, int reldock = 0);

#endif
