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
int diskDaemon(char* arg);
int termDaemon(char* arg);

typedef struct PCB {
    int pid;
    int mboxId;
    int wakeupTime;
    struct PCB* nextInQueue;
    struct PCB* prevInQueue;
    struct PCB* nextWriteProcess;
} PCB;

struct PCB processTable4[MAXPROC+1];
struct PCB* wakeupPQ;

int globalTime;
int term0Mbox;
int term1Mbox;
int term2Mbox;
int term3Mbox;

int termWriteMbox[4];
struct PCB* writeProcesses[4];
int termInUse[4];

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

    termInUse[0] = 0;
    termInUse[1] = 0;
    termInUse[2] = 0;
    termInUse[3] = 0;

    termWriteMbox[0] = MboxCreate(1, 0);
    termWriteMbox[1] = MboxCreate(1, 0);
    termWriteMbox[2] = MboxCreate(1, 0);
    termWriteMbox[3] = MboxCreate(1, 0);

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
    
    // fork1("disk0Daemon", diskDaemon, "0", USLOSS_MIN_STACK, 1);
    // fork1("disk1Daemon", diskDaemon, "1", USLOSS_MIN_STACK, 1);
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

int diskDaemon(char* arg) {
    int unit = atoi(arg);
    int status;
    while (1) {
        waitDevice(USLOSS_DISK_DEV, unit, &status);
    }
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
    if (unitID < 0 || unitID > 4 || bufferSize <= 0) {
        return -1;
    }
    char temp[MAXLINE+1];
    MboxRecv(termReadMboxIds[unitID], temp, MAXLINE+1);
    strncpy(buffer, temp, bufferSize);
    if (bufferSize <= MAXLINE) {
        buffer[bufferSize] = '\0';
    }
    *numCharsRead = strlen(buffer);
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
    if (unitID < 0 || unitID > 4 || bufferSize <= 0) {
        return -1;
    }
    int pid = getpid();
    struct PCB* process = &processTable4[pid % MAXPROC];
    process->pid = pid;
    process->mboxId = MboxCreate(1, 0);
    if (writeProcesses[unitID] == NULL && termInUse[unitID] == 1) {
        writeProcesses[unitID] = process;
        MboxRecv(process->mboxId, NULL, 0); // block with mailbox
    }
    else if (termInUse[unitID] == 1) {
        PCB* queue = writeProcesses[unitID];
        while (queue->nextWriteProcess != NULL) {
            queue = queue->nextWriteProcess;
        }
        queue->nextWriteProcess = process;
        MboxRecv(process->mboxId, NULL, 0); // block with mailbox
    }

    termInUse[unitID] = 1;
    int i = 0;
    while (i < bufferSize) {
        if (i+1 > MAXLINE) {
            break;
        }
        MboxRecv(termWriteMbox[unitID], NULL, 0);
        int crVal = 0x1; // this turns on the �send char� bit (USLOSS spec page 9)
        crVal |= 0x2; // recv int enable
        crVal |= 0x4; // xmit int enable
        crVal |= (buffer[i] << 8); // the character to send
        //USLOSS_Console("writing %c\n", buffer[i]);
        USLOSS_DeviceOutput(USLOSS_TERM_DEV, unitID, (void*)(long)crVal);
        i++;
    }
    *numCharsRead = i;
    MboxRelease(process->mboxId);
    termInUse[unitID] = 0;
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
        if (USLOSS_TERM_STAT_XMIT(status) == USLOSS_DEV_READY) {
            if (termInUse[unit] == 0 && writeProcesses[unit] != NULL) {
                PCB* process = writeProcesses[unit];
                writeProcesses[unit] = writeProcesses[unit]->nextWriteProcess;
                MboxCondSend(process->mboxId, NULL, 0);
            }
            else {
                MboxCondSend(termWriteMbox[unit], NULL, 0);
            }
        }
    }
}
