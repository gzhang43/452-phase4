//phase4

#include <stdio.h>
#include <usloss.h>
#include "phase1.h"
#include "phase2.h"
#include "phase3.h"
#include "phase4.h"
#include "phase3_usermode.h"
#include "phase4_usermode.h"

typedef struct PCB {
	int pid;
} PCB;

void sleepHelper(USLOSS_Sysargs* arg);

void phase4_init(void) {
	systemCallVec[12] = sleepHelper;
}

void phase4_start_service_processes(void) {

}

void sleepHelper(USLOSS_Sysargs* arg) {
	int seconds = (int)(long)arg->arg1;
	int ret = kernSleep(seconds);
	arg->arg4 = (void*)(long)ret;
	return;
}

int  kernSleep(int seconds) {
	int clockCycles = seconds * 10;
	USLOSS_Console("%d seconds\n", seconds);
	int i = 0;
	int status;
	while (i < clockCycles) {
		USLOSS_Console("WAITING\n");
		waitDevice(0, NULL, &status);
		i++;
	}
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