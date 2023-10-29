//phase4

#include <stdio.h>
#include <usloss.h>
#include "phase1.h"
#include "phase2.h"
#include "phase3.h"
#include "phase4.h"
#include "phase3_usermode.h"
#include "phase4_usermode.h"

void phase4_init(void) {

}

void phase4_start_service_processes(void) {

}

int  kernSleep(int seconds) {
	return 0;
}

int  kernDiskRead(void* diskBuffer, int unit, int track, int first, int sectors, int* status) {
	return 0;
}

int  kernDiskWrite(void* diskBuffer, int unit, int track, int first, int sectors, int* status) {
	return 0;
}

int  kernDiskSize(int unit, int* sector, int* track, int* disk) {
	return 0;
}

int  kernTermRead(char* buffer, int bufferSize, int unitID, int* numCharsRead) {
	return 0;
}

int  kernTermWrite(char* buffer, int bufferSize, int unitID, int* numCharsRead) {
	return 0;
}