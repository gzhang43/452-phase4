//phase4

#include <stdio.h>
#include <usloss.h>
#include "phase1.h"
#include "phase2.h"
#include "phase3.h"
#include "phase4.h"
#include "phase3_usermode.h"
#include "phase4_usermode.h"

void sleepHelper(USLOSS_Sysargs* arg);

typedef struct PCB {
    int pid;
    int mboxId;
    int wakeupTime;
    struct PCB* nextInQueue;
    struct PCB* prevInQueue;
} PCB;

struct PCB processTable4[MAXPROC+1];
struct PCB* wakeupPQ;

int globalTime;

void phase4_init(void) {
    systemCallVec[12] = sleepHelper;
    
    fork1("sleepDaemon", NULL, NULL, USLOSS_MIN_STACK, 7);
}

void phase4_start_service_processes(void) {

}

void addToPQ(struct PCB* process) {
    int wakeupTime = process->wakeupTime;
    if (wakeupPQ == NULL || wakeupTime < wakeupPQ->wakeupTime) {
        process->nextInQueue = wakeupPQ;
        wakeupPQ = process;
        return;
    }
    // Find spot in list to insert process depending on wakeup time
    struct PCB* curr = wakeupPQ;
    while (curr->nextInQueue != NULL && 
            wakeupTime > curr->nextInQueue->wakeupTime) {
        curr = curr->nextInQueue;
    }
    struct PCB* temp = curr->nextInQueue;
    curr->nextInQueue = process;
    process->nextInQueue = temp; 
}

void sleepHelper(USLOSS_Sysargs* arg) {
    int seconds = (int)(long)arg->arg1;
    int ret = kernSleep(seconds);
    arg->arg4 = (void*)(long)ret;
    return;
}

int kernSleep(int seconds) {
    int pid = getpid();
    struct PCB* process = &processTable4[pid % MAXPROC];
    process->pid = pid;
    process->mboxId = MboxCreate(1, 0);

    // Add process to wakeup priority queue and block
    addToPQ(process);
    MboxRecv(process->mboxId, NULL, 0); // block with mailbox

    MboxRelease(process->mboxId);
    return 0; 
}

void sleepDaemon() {
    int status;
    while(1) {
        waitDevice(USLOSS_CLOCK_DEV, 0, &status);
        // TODO: Wakeup all necessary processes
    }
}

int kernDiskRead(void* diskBuffer, int unit, int track, int first, 
        int sectors, int* status) {
    return 0;
}

int kernDiskWrite(void* diskBuffer, int unit, int track, int first, 
        int sectors, int* status) {
    return 0;
}

int kernDiskSize(int unit, int* sector, int* track, int* disk) {
    return 0;
}

int kernTermRead(char* buffer, int bufferSize, int unitID, int* numCharsRead) {
    return 0;
}

int kernTermWrite(char* buffer, int bufferSize, int unitID,
        int* numCharsRead) {
    return 0;
}
