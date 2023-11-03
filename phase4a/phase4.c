/*
Assignment: Phase 4a
Group: Grace Zhang and Ellie Martin
Course: CSC 452 (Operating Systems)
Instructors: Russell Lewis and Ben Dicken
Due Date: 11/8/23
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <usloss.h>
#include "phase1.h"
#include "phase2.h"
#include "phase3.h"
#include "phase4.h"
#include "phase3_usermode.h"
#include "phase4_usermode.h"

void sleepHelper(USLOSS_Sysargs* arg);
void termReadHelper(USLOSS_Sysargs* arg);
void termWriteHelper(USLOSS_Sysargs* arg);
int sleepDaemon(char* arg);
int termDaemon(char* arg);

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
int term0Mbox;
int term1Mbox;
int term2Mbox;
int term3Mbox;

// Mailboxes to hold lines read
int termReadMboxIds[4];
char termBuffers[4][MAXLINE+1];

void phase4_init(void) {
    systemCallVec[1] = termReadHelper;
    systemCallVec[2] = termWriteHelper;
    systemCallVec[12] = sleepHelper;

    globalTime = 0;

    int cr_val = 0x2; // recv int enable
    cr_val |= 0x4; // xmit int enable

    USLOSS_DeviceOutput(USLOSS_TERM_DEV, 0, (void*)(long)cr_val);
    USLOSS_DeviceOutput(USLOSS_TERM_DEV, 1, (void*)(long)cr_val);
    USLOSS_DeviceOutput(USLOSS_TERM_DEV, 2, (void*)(long)cr_val);
    USLOSS_DeviceOutput(USLOSS_TERM_DEV, 3, (void*)(long)cr_val);

    term0Mbox = MboxCreate(1, 0);
    term1Mbox = MboxCreate(1, 0);
    term2Mbox = MboxCreate(1, 0);
    term3Mbox = MboxCreate(1, 0);

    termReadMboxIds[0] = MboxCreate(10, MAXLINE+1);
    termReadMboxIds[1] = MboxCreate(10, MAXLINE+1);
    termReadMboxIds[2] = MboxCreate(10, MAXLINE+1);
    termReadMboxIds[3] = MboxCreate(10, MAXLINE+1);
}

void phase4_start_service_processes(void) {
    fork1("sleepDaemon", sleepDaemon, NULL, USLOSS_MIN_STACK, 1);
    fork1("term0Daemon", termDaemon, "0", USLOSS_MIN_STACK, 1);
    fork1("term1Daemon", termDaemon, "1", USLOSS_MIN_STACK, 1);
    fork1("term2Daemon", termDaemon, "2", USLOSS_MIN_STACK, 1);
    fork1("term3Daemon", termDaemon, "3", USLOSS_MIN_STACK, 1);
}

void addToPQ(struct PCB* process) {
    int wakeupTime = process->wakeupTime;
    if (wakeupPQ == NULL || wakeupTime <= wakeupPQ->wakeupTime) {
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
    process->wakeupTime = globalTime + (seconds * 10);
    process->mboxId = MboxCreate(1, 0);

    // Add process to wakeup priority queue and block
    addToPQ(process);
    MboxRecv(process->mboxId, NULL, 0); // block with mailbox

    MboxRelease(process->mboxId);
    return 0; 
}

int sleepDaemon(char* arg) {
    int status;
    while(1) {
        waitDevice(USLOSS_CLOCK_DEV, 0, &status);
        globalTime++;
        while (wakeupPQ != NULL && globalTime >= wakeupPQ->wakeupTime) {
            PCB* process = wakeupPQ;
            wakeupPQ = wakeupPQ->nextInQueue;
            MboxCondSend(process->mboxId, NULL, 0);
        }
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

void termReadHelper(USLOSS_Sysargs* arg) {
    char* buffer = (char*)(long)arg->arg1;
    int bufferSize = (int)(long)arg->arg2;
    int unit = (int)(long)arg->arg3;
    int numCharsRead;
    int ret = kernTermRead(buffer, bufferSize, unit, &numCharsRead);
    arg->arg2 = (void*)(long)numCharsRead;
    arg->arg4 = (void*)(long)ret;
}

int kernTermRead(char* buffer, int bufferSize, int unitID, int* numCharsRead) {
    return 0;
}

void termWriteHelper(USLOSS_Sysargs* arg) {
    char* buffer = (char*)(long)arg->arg1;
    int bufferSize = (int)(long)arg->arg2;
    int unit = (int)(long)arg->arg3;
    int numCharsRead;
    int ret = kernTermWrite(buffer, bufferSize, unit, &numCharsRead);
    arg->arg2 = (void*)(long)numCharsRead;
    arg->arg4 = (void*)(long)ret;
}

int kernTermWrite(char* buffer, int bufferSize, int unitID, int* numCharsRead) {
    return 0;
}

int termDaemon(char* arg) {
    int unit = atoi(arg);
    int status;
    while (1) {
        waitDevice(USLOSS_TERM_DEV, unit, &status);

        // Check if character has been received and stored in status reg
        if (USLOSS_TERM_STAT_RECV(status) == USLOSS_DEV_BUSY) {
            char c = USLOSS_TERM_STAT_CHAR(status); 
            if (strlen(termBuffers[unit]) < MAXLINE) {
                strncat(termBuffers[unit], &c, 1);
            }
            if (strlen(termBuffers[unit]) == MAXLINE || c == '\n') {
                MboxCondSend(termReadMboxIds[unit], termBuffers[unit], MAXLINE);
                memset(termBuffers[unit], 0, sizeof termBuffers[unit]);
            }
        }
        // TODO: Check if xmit is ready
    }
}
