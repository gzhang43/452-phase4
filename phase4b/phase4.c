/*
Assignment: Phase 4
Group: Grace Zhang and Ellie Martin
Course: CSC 452 (Operating Systems)
Instructors: Russell Lewis and Ben Dicken
Due Date: 11/15/23

Description: Code for Phase 4 of our operating systems kernel that implements
syscalls for TermRead(), TermWrite(), Sleep(), DiskRead(), DiskWrite(), and 
DiskSize(). These syscalls allow a process to sleep for a specified number of
seconds and to read and write characters from each of the four terminals.

To compile with testcases, run the Makefile.
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <usloss.h>
#include "phase1.h"
#include "phase2.h"
#include "phase3.h"
#include "phase4.h"
#include "phase3_usermode.h"
#include "phase4_usermode.h"

#define DEBUG_MODE 0

void sleepHelper(USLOSS_Sysargs* arg);
void termReadHelper(USLOSS_Sysargs* arg);
void termWriteHelper(USLOSS_Sysargs* arg);
void diskReadKern(USLOSS_Sysargs* arg);
void diskWriteKern(USLOSS_Sysargs* arg);
void diskSizeKern(USLOSS_Sysargs* arg);

int kernDiskSize(int unit, int* sector, int* track, int* disk);
int kernDiskRead(void* diskBuffer, int unit, int track, int first,
    int sectors, int* status);
int kernDiskWrite(void* diskBuffer, int unit, int track, int first,
    int sectors, int* status);

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

typedef struct UserDiskRequest {
    int requestType;
    void* buffer;
    int startTrack;
    int firstBlock;
    int blocks;
    int* statusOut;
    int mboxId;
    struct UserDiskRequest* nextReq;
} UserDiskRequest;

struct DiskState {
    USLOSS_DeviceRequest req;
    int numTracks;
    int currMboxId;
    int currTrack;
    int inUse;
    int status;
    struct UserDiskRequest* reqListFront;
    struct UserDiskRequest* reqListBack;
} disks[2];

struct PCB processTable4[MAXPROC+1];
struct PCB* wakeupPQ;

int globalTime; // number of clock ticks since start of program

int termWriteMbox[4];
struct PCB* writeProcesses[4];
int termInUse[4];
int termReadMboxIds[4]; // mailboxes to hold lines read
char termBuffers[4][MAXLINE+1]; // single line buffer for each terminal

/*
Initializes data structures and variables required for Phase 4, such as the
system call vector, terminal mailboxes, and the terminal control registers. 
*/
void phase4_init(void) {
    systemCallVec[1] = termReadHelper;
    systemCallVec[2] = termWriteHelper;
    systemCallVec[12] = sleepHelper;
    systemCallVec[13] = diskReadKern;
    systemCallVec[14] = diskWriteKern;
    systemCallVec[15] = diskSizeKern;

    globalTime = 0;

    int cr_val = 0x2; // recv int enable
    cr_val |= 0x4; // xmit int enable

    // Mask off read and write interrupts for each terminal
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

    disks[0].currTrack = -1;
    disks[1].currTrack = -1;
    disks[0].inUse = 0;
    disks[1].inUse = 0;
}

/*
Starts the daemon process for each device being used (one clock and four
terminals).
*/
void phase4_start_service_processes(void) {
    fork1("sleepDaemon", sleepDaemon, NULL, USLOSS_MIN_STACK, 1);
    
    fork1("term0Daemon", termDaemon, "0", USLOSS_MIN_STACK, 1);
    fork1("term1Daemon", termDaemon, "1", USLOSS_MIN_STACK, 1);
    fork1("term2Daemon", termDaemon, "2", USLOSS_MIN_STACK, 1);
    fork1("term3Daemon", termDaemon, "3", USLOSS_MIN_STACK, 1);
    
    fork1("disk0Daemon", diskDaemon, "0", USLOSS_MIN_STACK, 1);
    fork1("disk1Daemon", diskDaemon, "1", USLOSS_MIN_STACK, 1);
}

/*
Function that adds a process's PCB to a priority queue, where the priority
is the wakeup time of the process. The queue is implemented as a linked list,
and the function iterates through the list to find the spot to insert a 
process at.

Parameters:
    process - the struct PCB of the process to add to the queue

Returns: None
*/
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

void addToDiskPQ(UserDiskRequest* req, UserDiskRequest** queue) {
    int track = req->startTrack;
    if (*queue == NULL || track < (*queue)->startTrack) {
        req->nextReq = *queue;
        *queue = req;
        return;
    }
    UserDiskRequest* curr = *queue;
    while (curr->nextReq != NULL && track > curr->nextReq->startTrack) {
        curr = curr->nextReq;
    }
    UserDiskRequest* temp = curr->nextReq;
    curr->nextReq = req;
    req->nextReq = temp;
}

/*
Syscall that pauses the current process for the specified number of seconds.
The process is added to a priority queue and blocked, and is unblocked and
removed from the queue when the wakeup time has been reached

Parameters:
    seconds - the number of seconds to pause the process for

Returns: -1 if illegal values were given as input, and 0 otherwise.
*/
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

/*
Helper function for Sleep.

Parameters:
    arg.arg1: the number of seconds to pause the current process for

Outputs:
    arg.arg4: -1 if illegal values were given as input; 0 otherwise
*/
void sleepHelper(USLOSS_Sysargs* arg) {
    int seconds = (int)(long)arg->arg1;
    int ret = kernSleep(seconds);
    arg->arg4 = (void*)(long)ret;
    return;
}

/*
Daemon process that waits on the clock device for interrupts to be sent, and
removes and wakes up processes from the priority queue upon each interrupt if
necessary. Also increments globalTime upon each interrupt. 

Parameters:
    arg - required for fork

Returns: Does not return.
*/
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

/*
Syscall process

Parameters:
    arg1 - buffer pointer
    arg2 - number of blocks to read
    arg3 - starting track number
    arg4 - starting block number
    arg5 - which disk to access

Returns: does not return
*/
void diskReadKern(USLOSS_Sysargs* arg) {
    void* buffer = arg->arg1;
    int blocks = (int)(long)arg->arg2;
    int track = (int)(long)arg->arg3;
    int firstBlock = (int)(long)arg->arg4;
    int unit = (int)(long)arg->arg5;
    int* status;

    if (unit < 0 || unit > 1 || firstBlock < 0 || firstBlock > 15 ||
            track < 0) {
        arg->arg4 = (void*)(long)-1;
        return;
    }
    
    UserDiskRequest userRequest;
    userRequest.requestType = USLOSS_DISK_READ;
    userRequest.buffer = buffer;
    userRequest.startTrack = track;
    userRequest.firstBlock = firstBlock;
    userRequest.blocks = blocks;
    userRequest.mboxId = MboxCreate(1, 0);
    disks[unit].currMboxId = userRequest.mboxId;
    
    bool emptyLists = disks[unit].reqListFront == NULL && 
        disks[unit].reqListBack == NULL;

    UserDiskRequest* list;
    if (disks[unit].currTrack < track) {
        addToDiskPQ(&userRequest, &disks[unit].reqListFront);
    }
    else {
        addToDiskPQ(&userRequest, &disks[unit].reqListBack);
    }

    if (emptyLists && disks[unit].inUse == 0) {
        USLOSS_DeviceRequest req;
        req.opr = USLOSS_DISK_SEEK;
        req.reg1 = (void*)(long)track;

        disks[unit].inUse = 1;
        disks[unit].currTrack = track;
        disks[unit].req = req;
        USLOSS_DeviceOutput(USLOSS_DISK_DEV, unit, &req);
    }
    MboxRecv(userRequest.mboxId, NULL, 0);
    disks[unit].inUse = 0;
    MboxRelease(userRequest.mboxId);
    arg->arg1 = (void*)(long)0;
    arg->arg4 = (void*)(long)0;
    if (disks[unit].status == USLOSS_DEV_ERROR) {
        arg->arg1 = (void*)(long)disks[unit].status;
    }
}

/*
Syscall process that queues and begins a disk write process.

Parameters:
    arg1 - buffer pointer
    arg2 - number of blocks to read
    arg3 - starting track number
    arg4 - starting block number
    arg5 - which disk to access

Returns: does not return
*/
void diskWriteKern(USLOSS_Sysargs* arg) {
    void* buffer = arg->arg1;
    int blocks = (int)(long)arg->arg2;
    int track = (int)(long)arg->arg3;
    int firstBlock = (int)(long)arg->arg4;
    int unit = (int)(long)arg->arg5;
    int* status;

    if (unit < 0 || unit > 1 || firstBlock < 0 || firstBlock > 15 ||
            track < 0) {
        arg->arg4 = (void*)(long)-1;
        return;
    }

    UserDiskRequest userRequest;
    userRequest.requestType = USLOSS_DISK_WRITE;
    userRequest.buffer = buffer;
    userRequest.startTrack = track;
    userRequest.firstBlock = firstBlock;
    userRequest.blocks = blocks;
    userRequest.mboxId = MboxCreate(1, 0);
    disks[unit].currMboxId = userRequest.mboxId;

    bool emptyLists = disks[unit].reqListFront == NULL && 
        disks[unit].reqListBack == NULL;

    UserDiskRequest* list;
    if (disks[unit].currTrack < track) {
        addToDiskPQ(&userRequest, &disks[unit].reqListFront);
    }
    else {
        addToDiskPQ(&userRequest, &disks[unit].reqListBack);
    }

    if (emptyLists && disks[unit].inUse == 0) {
        USLOSS_DeviceRequest req;
        req.opr = USLOSS_DISK_SEEK;
        req.reg1 = (void*)(long)track;

        disks[unit].inUse = 1;
        disks[unit].currTrack = track;
        disks[unit].req = req;
        USLOSS_DeviceOutput(USLOSS_DISK_DEV, unit, &req);
    }
    MboxRecv(userRequest.mboxId, NULL, 0);
    disks[unit].inUse = 0;
    MboxRelease(userRequest.mboxId);
    arg->arg1 = (void*)(long)0;
    arg->arg4 = (void*)(long)0;
    if (disks[unit].status == USLOSS_DEV_ERROR) {
        arg->arg1 = (void*)(long)disks[unit].status;
    }
}

/*
Syscall process for determining the size of a given disk. It sends
a USLOSS_DISK_TRACKS request to get the number of tracks on the disk

Parameters:
    arg1 - the disk unit to use

Returns: does not return
*/
void diskSizeKern(USLOSS_Sysargs* arg) {
    int unit = (int)(long)arg->arg1;

    disks[unit].currMboxId = MboxCreate(1, 0);
    disks[unit].req.opr = USLOSS_DISK_TRACKS;
    disks[unit].req.reg1 = &disks[unit].numTracks;
    disks[unit].inUse = 1;

    USLOSS_DeviceOutput(USLOSS_DISK_DEV, unit, &disks[unit].req);
    MboxRecv(disks[unit].currMboxId, NULL, 0);

    disks[unit].inUse = 0;
    arg->arg1 = (void*)(long)512;
    arg->arg2 = (void*)(long)16;
    arg->arg3 = (void*)(long)disks[unit].numTracks;
    arg->arg4 = (void*)(long)0;
    
    MboxRelease(disks[unit].currMboxId);
}

/*
Daemon process that is forked for each disk device.

Parameters:
    arg - the unitNo of the disk device

Returns: Does not return.
*/
int diskDaemon(char* arg) {
    int unit = atoi(arg);
    int status;
    int prevRW = 0;
    UserDiskRequest* currUserReq;

    while (1) {
        waitDevice(USLOSS_DISK_DEV, unit, &status);
        if (prevRW == 1) {
            MboxCondSend(currUserReq->mboxId, NULL, 0);
            prevRW = 0;
        }
        if (status == USLOSS_DEV_READY && disks[unit].req.opr == 
                USLOSS_DISK_TRACKS) {
            MboxCondSend(disks[unit].currMboxId, NULL, 0); 
        }
        else if (status == USLOSS_DEV_READY && (disks[unit].reqListFront != NULL ||
                disks[unit].reqListBack != NULL)) {
            // Swap queues if reached end of queue
            if (disks[unit].reqListFront == NULL && disks[unit].reqListBack != NULL) {
                disks[unit].reqListFront = disks[unit].reqListBack;
                disks[unit].reqListBack = NULL;
            }
            currUserReq = disks[unit].reqListFront;
            disks[unit].reqListFront = disks[unit].reqListFront->nextReq;
            
            char* buffer = currUserReq->buffer;
            int currBlock = currUserReq->firstBlock;
            int numBlocks = currUserReq->blocks;
            int currTrack = currUserReq->startTrack;

            if (currTrack != disks[unit].currTrack) {
                USLOSS_DeviceRequest req;
                req.opr = USLOSS_DISK_SEEK;
                req.reg1 = (void*)(long)currTrack;
                USLOSS_DeviceOutput(USLOSS_DISK_DEV, unit, &req);
                waitDevice(USLOSS_DISK_DEV, unit, &status);
                if (status != USLOSS_DEV_READY) {
                    USLOSS_Console("Error with disk operation.\n");
                }
                disks[unit].currTrack = currTrack;
            }
            int count = 0;
            while (count < numBlocks) {
                if (DEBUG_MODE == 1) {
                    USLOSS_Console("Iteration %d\n", count);
                }
                if (currBlock > 15) {
                    USLOSS_DeviceRequest req;
                    req.opr = USLOSS_DISK_SEEK;
                    req.reg1 = (void*)(long)currTrack + 1;
                    currBlock = 0;

                    USLOSS_DeviceOutput(USLOSS_DISK_DEV, unit, &req);
                    waitDevice(USLOSS_DISK_DEV, unit, &status);
                    if (status != USLOSS_DEV_READY) {
                        USLOSS_Console("Error with disk operation.\n");
                    }
                    currTrack++;
                    disks[unit].currTrack = currTrack;
                }
                USLOSS_DeviceRequest req;
                req.opr = currUserReq->requestType;
                req.reg1 = (void*)(long)currBlock;
                req.reg2 = (void*)buffer;
                
                USLOSS_DeviceOutput(USLOSS_DISK_DEV, unit, &req);
                if (count != numBlocks - 1) {
                    waitDevice(USLOSS_DISK_DEV, unit, &status);
                    if (status != USLOSS_DEV_READY) {
                        USLOSS_Console("Error with disk operation.\n");
                    }
                }
                else {
                    prevRW = 1;
                }
                buffer += 512;
                currBlock++;
                count++;
            }
        } 
        else if (status == USLOSS_DEV_ERROR) {
            disks[unit].status = USLOSS_DEV_ERROR;
            if (prevRW == 1) {
                MboxCondSend(currUserReq->mboxId, NULL, 0);
            }
            else {
                MboxCondSend(disks[unit].currMboxId, NULL, 0); 
            }
        }
    }
}

/*
Syscall that performs a read of one of the terminals by returning the first
line of the terminal's buffer. A buffer can hold up to 10 lines, and is
implemented using a mailbox, and a line either ends with a newline or is
exactly MAXLINE characters long.

Parameters:
    buffer - the buffer pointer (to use for output)
    bufferSize - the length of the buffer
    unitID - which terminal to read from
    numCharsRead - out pointer for the number of characters read

Returns: -1 if illegal values were given as input and 0 otherwise.
*/
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

/*
Helper function for TermRead.

Parameters:
    arg.arg1 - buffer pointer
    arg.arg2 - length of the buffer
    arg.arg3 - the unit number of the terminal to read
    
Outputs:
    arg.arg2: the number of characters read
    arg.arg4: -1 if illegal values were given as input and 0 otherwise
*/
void termReadHelper(USLOSS_Sysargs* arg) {
    char* buffer = (char*)(long)arg->arg1;
    int bufferSize = (int)(long)arg->arg2;
    int unit = (int)(long)arg->arg3;
    int numCharsRead;
    int ret = kernTermRead(buffer, bufferSize, unit, &numCharsRead);
    arg->arg2 = (void*)(long)numCharsRead;
    arg->arg4 = (void*)(long)ret;
}

/*
Helper function for termWrite, called by the termWrite syscall

Parameters:
    arg->arg1 - buffer pointer
    arg->arg2 - length of the buffer
    arg->arg3 - the unit number of the terminal to read

Outputs:
    arg->arg2: the number of characters read
    arg->arg4: -1 if illegal values were given as input and 0 otherwise
*/
void termWriteHelper(USLOSS_Sysargs* arg) {
    char* buffer = (char*)(long)arg->arg1;
    int bufferSize = (int)(long)arg->arg2;
    int unit = (int)(long)arg->arg3;
    int numCharsRead;
    int ret = kernTermWrite(buffer, bufferSize, unit, &numCharsRead);
    arg->arg2 = (void*)(long)numCharsRead;
    arg->arg4 = (void*)(long)ret;
}

/*
Syscall function that will write a char buffer to a given terminal.
If the terminal specified is in use, then the curent process is added to
a queue and blocked.
If the terminal is not in use, begins the write process by repeatedly writing
a single character, blocking on a mailbox each time until the daemon sends
a message to continue.

Parameters:
    buffer - the buffer pointer
    bufferSize - the length of the buffer
    unitID - which terminal to read from
    numCharsRead - out pointer for the number of characters read

Returns: -1 if illegal values were given as input and 0 otherwise.
*/
int kernTermWrite(char* buffer, int bufferSize, int unitID, int* numCharsRead) {
    if (unitID < 0 || unitID > 4 || bufferSize <= 0) {
        return -1;
    }
    int pid = getpid();
    struct PCB* process = &processTable4[pid % MAXPROC];
    process->pid = pid;
    process->mboxId = MboxCreate(1, 0);

    //add process to queue if terminal is in use
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

    termInUse[unitID] = 1; //indicates this terminal as being in use
    int i = 0;
    while (i < bufferSize) {
        if (i+1 > MAXLINE) {
            break;
        }
        MboxRecv(termWriteMbox[unitID], NULL, 0);
        int crVal = 0x1; // turns on the send char bit
        crVal |= 0x2; // recv int enable
        crVal |= 0x4; // xmit int enable
        crVal |= (buffer[i] << 8); // the character to send
        
        if (DEBUG_MODE == 1) {
            USLOSS_Console("writing %c\n", buffer[i]);
        }
        // writes to terminal
        USLOSS_DeviceOutput(USLOSS_TERM_DEV, unitID, (void*)(long)crVal); 
        i++;
    }
    *numCharsRead = i;
    MboxRelease(process->mboxId);
    termInUse[unitID] = 0;
    return 0;
}

/*
Daemon process for all terminal syscall functions. It reads the recv and xmit
bit of the status register, and will send messages to unblock processes that
are waiting to read or write.

Parameters:
    arg - which terminal to read and/or write to

Returns: Does not return.
*/
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
        // Check if able to write char to terminal
        if (USLOSS_TERM_STAT_XMIT(status) == USLOSS_DEV_READY) {
            // either wake up process from queue or unblocks currently
            // writing process
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
