#pragma once
#include "internal.h"

class SectionLocker {
	char *Function;
	int Line;
	void *Object;
public:
	CRITICAL_SECTION *section;
	
	SectionLocker(CRITICAL_SECTION &critsection,char *function,int line,void* object);
	//SectionLocker(CRITICAL_SECTION &section);
	~SectionLocker();
};

class SectionLogger {
	DWORD start;
	char *SectionName;
	public:
	SectionLogger(char *section);
	~SectionLogger();
};

// (the historical "#define lock(x) lockObj(...)" macro was removed - it
// clashed with the explicit 4-argument SectionLocker constructor calls.)