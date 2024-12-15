#include "HAL9000.h"
#include "ex_timer.h"
#include "iomu.h"
#include "thread_internal.h"

typedef struct _GLOBAL_TIMER_LIST
{
	// protect the global timer list
	LOCK TimerListLock;

	// the list’s head
	LIST_ENTRY TimerListHead;
}GLOBAL_TIMER_LIST;

static GLOBAL_TIMER_LIST m_globalTimerList;

void
ExTimerSystemPreinit(
	void
)
{
	memzero(&m_globalTimerList, sizeof(GLOBAL_TIMER_LIST));

	InitializeListHead(&m_globalTimerList.TimerListHead);
	LockInit(&m_globalTimerList.TimerListLock);
}

INT64
ExTimerCompareListElems(
	IN      PLIST_ENTRY Timer1,
	IN      PLIST_ENTRY Timer2,
	IN_OPT  PVOID       Context
)
{
	ASSERT(Context == NULL);
	ASSERT(Timer1 != NULL);
	ASSERT(Timer2 != NULL);

	return ExTimerCompareTimers(CONTAINING_RECORD(Timer1, EX_TIMER, TimerListElem), CONTAINING_RECORD(Timer2, EX_TIMER, TimerListElem));
}

INT64
ExTimerCompareTimers(
	IN      PEX_TIMER     FirstElem,
	IN      PEX_TIMER     SecondElem
)
{
	ASSERT(FirstElem != NULL);
	ASSERT(SecondElem != NULL);
	return FirstElem->TriggerTimeUs - SecondElem->TriggerTimeUs;
}

STATUS
ExTimerCheck(
	IN      PLIST_ENTRY     TimerListEntry,
	IN_OPT  PVOID           FunctionContext
)
{
	ASSERT(FunctionContext == NULL);

	if (TimerListEntry == NULL) {
		return STATUS_INVALID_PARAMETER1;
	}

	PEX_TIMER Timer = CONTAINING_RECORD(TimerListEntry, EX_TIMER, TimerListElem);
	if (Timer->TimerStarted && IomuGetSystemTimeUs() >= Timer->TriggerTimeUs) {
		ExEventSignal(&Timer->TimerEvent);
		return STATUS_SUCCESS;
	}
	return STATUS_TIMER_NOT_EXPIRED_YET;
}

void
ExTimerCheckAll(
	void
)
{
	INTR_STATE dummyState;

	LockAcquire(&m_globalTimerList.TimerListLock, &dummyState);
	ForEachElementExecute(&m_globalTimerList.TimerListHead, ExTimerCheck, NULL, 0);
	LockRelease(&m_globalTimerList.TimerListLock, dummyState);
}

STATUS
ExTimerInit(
    OUT     PEX_TIMER       Timer,
    IN      EX_TIMER_TYPE   Type,
    IN      QWORD           Time
    )
{
    STATUS status;
	INTR_STATE dummyState;

    if (NULL == Timer)
    {
        return STATUS_INVALID_PARAMETER1;
    }

    if (Type > ExTimerTypeMax)
    {
        return STATUS_INVALID_PARAMETER2;
    }

    status = STATUS_SUCCESS;

    memzero(Timer, sizeof(EX_TIMER));

    Timer->Type = Type;
    if (Timer->Type != ExTimerTypeAbsolute)
    {
        // relative time

        // if the time trigger time has already passed the timer will
        // be signaled after the first scheduler tick
        Timer->TriggerTimeUs = IomuGetSystemTimeUs() + Time;
        Timer->ReloadTimeUs = Time;
    }
    else
    {
        // absolute
        Timer->TriggerTimeUs = Time;
    }

	LockAcquire(&m_globalTimerList.TimerListLock, &dummyState);
	InsertOrderedList(&m_globalTimerList.TimerListHead, &Timer->TimerListElem, ExTimerCompareListElems, NULL);
	LockRelease(&m_globalTimerList.TimerListLock, dummyState);

	ExEventInit(&Timer->TimerEvent, ExEventTypeNotification, 0);

	return status;
}

void
ExTimerStart(
    IN      PEX_TIMER       Timer
    )
{
    ASSERT(Timer != NULL);

    if (Timer->TimerUninited)
    {
        return;
    }

    Timer->TimerStarted = TRUE;
}

void
ExTimerStop(
    IN      PEX_TIMER       Timer
    )
{
    ASSERT(Timer != NULL);

    if (Timer->TimerUninited)
    {
        return;
    }

    Timer->TimerStarted = FALSE;

	ExEventSignal(&Timer->TimerEvent);
}

void
ExTimerWait(
    INOUT   PEX_TIMER       Timer
    )
{
    ASSERT(Timer != NULL);

    if (Timer->TimerUninited)
    {
        return;
    }

    ExEventWaitForSignal(&Timer->TimerEvent);
}

void
ExTimerUninit(
    INOUT   PEX_TIMER       Timer
    )
{
	INTR_STATE dummyState;

	ASSERT(Timer != NULL);

	ExTimerStop(Timer);

	Timer->TimerUninited = TRUE;

	LockAcquire(&m_globalTimerList.TimerListLock, &dummyState);
	RemoveEntryList(&Timer->TimerListElem);
	LockRelease(&m_globalTimerList.TimerListLock, dummyState);
}