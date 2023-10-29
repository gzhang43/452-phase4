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

int  kernSleep(USLOSS_Sysargs* arg) {
	int seconds = (int)(long)arg->arg1;
	return 0;
}

int  kernDiskRead(USLOSS_Sysargs* arg) {
	return 0;
}

int  kernDiskWrite(USLOSS_Sysargs* arg) {
	return 0;
}

int  kernDiskSize(USLOSS_Sysargs* arg) {
	return 0;
}

int  kernTermRead(USLOSS_Sysargs* arg) {
	char* buffer = (char*)arg->arg1;
	int bufSize = (int)(long)arg->arg2;
	int unit = (int)(long)arg->arg3;
	return 0;
}

int  kernTermWrite(USLOSS_Sysargs* arg) {
	char* buffer = (char*)arg->arg1;
	int bufSize = (int)(long)arg->arg2;
	int unit = (int)(long)arg->arg3;
	return 0;
}