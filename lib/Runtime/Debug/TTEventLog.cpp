//-------------------------------------------------------------------------------------------------------
// Copyright (C) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------
#include "RuntimeDebugPch.h"

#if ENABLE_TTD

namespace TTD
{
    TTDExceptionFramePopper::TTDExceptionFramePopper()
        : m_log(nullptr)
    {
        ;
    }

    TTDExceptionFramePopper::~TTDExceptionFramePopper()
    {
#if ENABLE_TTD_DEBUGGING
        //we didn't clear this so an exception was thrown and we are propagating
        if(this->m_log != nullptr)
        {
            //if it doesn't have an exception frame then this is the frame where the exception was thrown so record our info
            this->m_log->PopCallEventException(!this->m_log->HasImmediateExceptionFrame());
        }
#endif
    }

    void TTDExceptionFramePopper::PushInfo(EventLog* log)
    {
        this->m_log = log; //set the log info so if the pop isn't called the destructor will record propagation
    }

    void TTDExceptionFramePopper::PopInfo()
    {
        this->m_log = nullptr; //normal pop (no exception) just clear so destructor nops
    }

    TTDRecordExternalFunctionCallActionPopper::TTDRecordExternalFunctionCallActionPopper(EventLog* log, Js::ScriptContext* scriptContext)
        : m_log(log), m_scriptContext(scriptContext), m_timer(), m_callAction(nullptr)
    {
        ;
    }

    TTDRecordExternalFunctionCallActionPopper::~TTDRecordExternalFunctionCallActionPopper()
    {
        if(this->m_callAction != nullptr)
        {
            double endTime = this->m_timer.Now();

            //
            //TODO: we will want to be a bit more detailed on this later
            //
            bool hasScriptException = this->m_scriptContext->HasRecordedException();
            bool hasTerminalException = false;

            this->m_log->RecordExternalCallEndEvent(this->m_callAction->GetEventTime(), this->m_callAction->GetRootNestingDepth(), hasScriptException, hasTerminalException, endTime, this->m_scriptContext->GetLibrary()->GetUndefined());

            this->m_scriptContext->TTDRootNestingCount--;
        }
    }

    void TTDRecordExternalFunctionCallActionPopper::NormalReturn(bool checkException, Js::Var returnValue)
    {
        AssertMsg(this->m_callAction != nullptr, "Should never be null on normal return!");

        double endTime = this->m_timer.Now();

        //
        //TODO: we will want to be a bit more detailed on this later
        //
        bool hasScriptException = checkException ? this->m_scriptContext->HasRecordedException() : false;
        bool hasTerminalException = false;

        this->m_log->RecordExternalCallEndEvent(this->m_callAction->GetEventTime(), this->m_callAction->GetRootNestingDepth(), hasScriptException, hasTerminalException, endTime, returnValue);

        this->m_callAction = nullptr;
        this->m_scriptContext->TTDRootNestingCount--;
    }

    void TTDRecordExternalFunctionCallActionPopper::SetCallAction(ExternalCallEventBeginLogEntry* action)
    {
        this->m_callAction = action;
    }

    double TTDRecordExternalFunctionCallActionPopper::GetStartTime()
    {
        return this->m_startTime;
    }

    TTDRecordJsRTFunctionCallActionPopper::TTDRecordJsRTFunctionCallActionPopper(EventLog* log, Js::ScriptContext* scriptContext)
        : m_log(log), m_scriptContext(scriptContext), m_timer(), m_callAction(nullptr)
    {
        ;
    }

    TTDRecordJsRTFunctionCallActionPopper::~TTDRecordJsRTFunctionCallActionPopper()
    {
        if(this->m_callAction != nullptr)
        {
            double endTime = this->m_timer.Now();

            //
            //TODO: we will want to be a bit more detailed on this later
            //
            bool hasScriptException = true; 
            bool hasTerminalException = false;

            this->m_log->RecordJsRTCallFunctionEnd(this->m_scriptContext, this->m_callAction->GetEventTime(), hasScriptException, hasTerminalException, this->m_callAction->GetCallDepth(), endTime);
            this->m_log->IncrementElapsedSnapshotTime(endTime - this->m_startTime);
        }
    }

    void TTDRecordJsRTFunctionCallActionPopper::NormalReturn()
    {
        AssertMsg(this->m_callAction != nullptr, "Should never be null on normal return!");

        double endTime = this->m_timer.Now();

        //
        //TODO: we will want to be a bit more detailed on this later
        //
        bool hasScriptException = false;
        bool hasTerminalException = false;

        this->m_log->RecordJsRTCallFunctionEnd(this->m_scriptContext, this->m_callAction->GetEventTime(), hasScriptException, hasTerminalException, this->m_callAction->GetCallDepth(), endTime);
        this->m_log->IncrementElapsedSnapshotTime(endTime - this->m_startTime);

        this->m_callAction = nullptr;
    }

    void TTDRecordJsRTFunctionCallActionPopper::SetCallAction(JsRTCallFunctionBeginAction* action)
    {
        this->m_callAction = action;
    }

    double TTDRecordJsRTFunctionCallActionPopper::GetStartTime()
    {
        this->m_startTime = this->m_timer.Now();

        return this->m_startTime;
    }

    /////////////

    void TTEventList::AddArrayLink()
    {
        TTEventListLink* newHeadBlock = this->m_alloc->SlabAllocateStruct<TTEventListLink>();
        newHeadBlock->BlockData = this->m_alloc->SlabAllocateFixedSizeArray<EventLogEntry*, TTD_EVENTLOG_LIST_BLOCK_SIZE>();

        newHeadBlock->CurrPos = 0;
        newHeadBlock->StartPos = 0;

        newHeadBlock->Next = nullptr;
        newHeadBlock->Previous = this->m_headBlock;

        if(this->m_headBlock != nullptr)
        {
            this->m_headBlock->Next = newHeadBlock;
        }

        this->m_headBlock = newHeadBlock;
    }

    void TTEventList::RemoveArrayLink(TTEventListLink* block)
    {
        AssertMsg(block->Previous == nullptr, "Not first event block in log!!!");
        AssertMsg(block->StartPos == block->CurrPos, "Haven't cleared all the events in this link");

        if(block->Next == nullptr)
        {
            this->m_headBlock = nullptr; //was only 1 block to we are now all null
        }
        else
        {
            block->Next->Previous = nullptr;
        }

        this->m_alloc->UnlinkAllocation(block->BlockData);
        this->m_alloc->UnlinkAllocation(block);
    }

    TTEventList::TTEventList(UnlinkableSlabAllocator* alloc)
        : m_alloc(alloc), m_headBlock(nullptr)
    {
        ;
    }

    void TTEventList::UnloadEventList()
    {
        if(this->m_headBlock == nullptr)
        {
            return;
        }

        TTEventListLink* firstBlock = this->m_headBlock;
        while(firstBlock->Previous != nullptr)
        {
            firstBlock = firstBlock->Previous;
        }

        TTEventListLink* curr = firstBlock;
        while(curr != nullptr)
        {
            for(uint32 i = curr->StartPos; i < curr->CurrPos; ++i)
            {
                curr->BlockData[i]->UnloadEventMemory(*(this->m_alloc));

                this->m_alloc->UnlinkAllocation(curr->BlockData[i]);
            }
            curr->StartPos = curr->CurrPos;

            TTEventListLink* next = curr->Next;
            this->RemoveArrayLink(curr);
            curr = next;
        }

        this->m_headBlock = nullptr;
    }

    void TTEventList::AddEntry(EventLogEntry* data)
    {
        if((this->m_headBlock == nullptr) || (this->m_headBlock->CurrPos == TTD_EVENTLOG_LIST_BLOCK_SIZE))
        {
            this->AddArrayLink();
        }

        this->m_headBlock->BlockData[this->m_headBlock->CurrPos] = data;
        this->m_headBlock->CurrPos++;
    }

    void TTEventList::DeleteFirstEntry(TTEventListLink* block, EventLogEntry* data)
    {
        AssertMsg(block->Previous == nullptr, "Not first event block in log!!!");
        AssertMsg(block->BlockData[block->StartPos] == data, "Not the data at the start of the list!!!");

        data->UnloadEventMemory(*(this->m_alloc));
        this->m_alloc->UnlinkAllocation(data);

        block->StartPos++;
        if(block->StartPos == block->CurrPos)
        {
            this->RemoveArrayLink(block);
        }
    }

    bool TTEventList::IsEmpty() const
    {
        return this->m_headBlock == nullptr;
    }

    uint32 TTEventList::Count() const
    {
        uint32 count = 0;

        for(TTEventListLink* curr = this->m_headBlock; curr != nullptr; curr = curr->Previous)
        {
            count += (curr->CurrPos - curr->StartPos);
        }

        return (uint32)count;
    }

    TTEventList::Iterator::Iterator()
        : m_currLink(nullptr), m_currIdx(0)
    {
        ;
    }

    TTEventList::Iterator::Iterator(TTEventListLink* head, uint32 pos)
        : m_currLink(head), m_currIdx(pos)
    {
        ;
    }

    const EventLogEntry* TTEventList::Iterator::Current() const
    {
        AssertMsg(this->IsValid(), "Iterator is invalid!!!");

        return this->m_currLink->BlockData[this->m_currIdx];
    }

    EventLogEntry* TTEventList::Iterator::Current()
    {
        AssertMsg(this->IsValid(), "Iterator is invalid!!!");

        return this->m_currLink->BlockData[this->m_currIdx];
    }

    bool TTEventList::Iterator::IsValid() const
    {
        return (this->m_currLink != nullptr && this->m_currLink->StartPos <= this->m_currIdx && this->m_currIdx < this->m_currLink->CurrPos);
    }

    void TTEventList::Iterator::MoveNext()
    {
        if(this->m_currIdx < (this->m_currLink->CurrPos - 1))
        {
            this->m_currIdx++;
        }
        else
        {
            this->m_currLink = this->m_currLink->Next;
            this->m_currIdx = (this->m_currLink != nullptr) ? this->m_currLink->StartPos : 0;
        }
    }

    void TTEventList::Iterator::MovePrevious()
    {
        if(this->m_currIdx > this->m_currLink->StartPos)
        {
            this->m_currIdx--;
        }
        else
        {
            this->m_currLink = this->m_currLink->Previous;
            this->m_currIdx = (this->m_currLink != nullptr) ? (this->m_currLink->CurrPos - 1) : 0;
        }
    }

    TTEventList::Iterator TTEventList::GetIteratorAtFirst() const
    {
        if(this->m_headBlock == nullptr)
        {
            return Iterator(nullptr, 0);
        }
        else
        {
            TTEventListLink* firstBlock = this->m_headBlock;
            while(firstBlock->Previous != nullptr)
            {
                firstBlock = firstBlock->Previous;
            }

            return Iterator(firstBlock, firstBlock->StartPos);
        }
    }

    TTEventList::Iterator TTEventList::GetIteratorAtLast() const
    {
        if(this->m_headBlock == nullptr)
        {
            return Iterator(nullptr, 0);
        }
        else
        {
            return Iterator(this->m_headBlock, this->m_headBlock->CurrPos - 1);
        }
    }

    //////

    const SingleCallCounter& EventLog::GetTopCallCounter() const
    {
        AssertMsg(this->m_callStack.Count() != 0, "Empty stack!");

        return this->m_callStack.Item(this->m_callStack.Count() - 1);
    }

    SingleCallCounter& EventLog::GetTopCallCounter()
    {
        AssertMsg(this->m_callStack.Count() != 0, "Empty stack!");

        return this->m_callStack.Item(this->m_callStack.Count() - 1);
    }

    const SingleCallCounter& EventLog::GetTopCallCallerCounter() const
    {
        AssertMsg(this->m_callStack.Count() >= 2, "Empty stack!");

        return this->m_callStack.Item(this->m_callStack.Count() - 2);
    }

    int64 EventLog::GetCurrentEventTimeAndAdvance()
    {
        return this->m_eventTimeCtr++;
    }

    void EventLog::AdvanceTimeAndPositionForReplay()
    {
        this->m_eventTimeCtr++;
        this->m_currentReplayEventIterator.MoveNext();

        AssertMsg(!this->m_currentReplayEventIterator.IsValid() || this->m_eventTimeCtr <= this->m_currentReplayEventIterator.Current()->GetEventTime(), "Something is out of sync.");
    }

    void EventLog::InsertEventAtHead(EventLogEntry* evnt)
    {
        this->m_eventList.AddEntry(evnt);
    }

    void EventLog::UpdateComputedMode()
    {
        AssertMsg(this->m_modeStack.Count() >= 0, "Should never be empty!!!");

        TTDMode cm = TTDMode::Invalid;
        for(int32 i = 0; i < this->m_modeStack.Count(); ++i)
        {
            TTDMode m = this->m_modeStack.Item(i);
            switch(m)
            {
            case TTDMode::Pending:
            case TTDMode::Detached:
            case TTDMode::RecordEnabled:
            case TTDMode::DebuggingEnabled:
                AssertMsg(i == 0, "One of these should always be first on the stack.");
                cm = m;
                break;
            case TTDMode::ExcludedExecution:
                AssertMsg(i != 0, "A base mode should always be first on the stack.");
                cm |= m;
                break;
            default:
                AssertMsg(false, "This mode is unknown or should never appear.");
                break;
            }
        }

        this->m_currentMode = cm;

        if(this->m_ttdContext != nullptr)
        {
            this->m_ttdContext->SetMode_TTD(this->m_currentMode);
        }
    }

    void EventLog::UnloadRetainedData()
    {
        if(this->m_lastInflateMap != nullptr)
        {
            HeapDelete(this->m_lastInflateMap);
            this->m_lastInflateMap = nullptr;
        }

        if(this->m_propertyRecordPinSet != nullptr)
        {
            this->m_propertyRecordPinSet->GetAllocator()->RootRelease(this->m_propertyRecordPinSet);
            this->m_propertyRecordPinSet = nullptr;
        }
    }

    void EventLog::DoSnapshotExtract_Helper(bool firstSnap, SnapShot** snap, TTD_LOG_TAG* logTag, TTD_IDENTITY_TAG* identityTag)
    {
        AssertMsg(this->m_ttdContext != nullptr, "We aren't actually tracking anything!!!");

        JsUtil::List<Js::Var, HeapAllocator> roots(&HeapAllocator::Instance);
        JsUtil::List<Js::ScriptContext*, HeapAllocator> ctxs(&HeapAllocator::Instance);

        ctxs.Add(this->m_ttdContext);
        this->m_ttdContext->ExtractSnapshotRoots_TTD(roots);

        this->m_snapExtractor.BeginSnapshot(this->m_threadContext, roots, ctxs);
        this->m_snapExtractor.DoMarkWalk(roots, ctxs, this->m_threadContext, firstSnap);

        ///////////////////////////
        //Phase 2: Evacuate marked objects
        //Allows for parallel execute and evacuate (in conjunction with later refactoring)

        this->m_snapExtractor.EvacuateMarkedIntoSnapshot(this->m_threadContext, ctxs);

        ///////////////////////////
        //Phase 3: Complete and return snapshot

        *snap = this->m_snapExtractor.CompleteSnapshot();

        ///////////////////////////
        //Get the tag information

        this->m_threadContext->TTDInfo->GetTagsForSnapshot(logTag, identityTag);
    }

    EventLog::EventLog(ThreadContext* threadContext, LPCWSTR logDir)
        : m_threadContext(threadContext), m_eventSlabAllocator(TTD_SLAB_BLOCK_ALLOCATION_SIZE_MID), m_miscSlabAllocator(TTD_SLAB_BLOCK_ALLOCATION_SIZE_SMALL),
        m_eventTimeCtr(0), m_runningFunctionTimeCtr(0), m_topLevelCallbackEventTime(-1), m_hostCallbackId(-1),
        m_eventList(&this->m_eventSlabAllocator), m_currentReplayEventIterator(),
        m_callStack(&HeapAllocator::Instance, 32), 
#if ENABLE_TTD_DEBUGGING
        m_isReturnFrame(false), m_isExceptionFrame(false), m_lastFrame(),
#endif
        m_modeStack(&HeapAllocator::Instance), m_currentMode(TTDMode::Pending),
        m_ttdContext(nullptr),
        m_snapExtractor(), m_elapsedExecutionTimeSinceSnapshot(0.0),
        m_lastInflateSnapshotTime(-1), m_lastInflateMap(nullptr), m_propertyRecordPinSet(nullptr), m_propertyRecordList(&this->m_miscSlabAllocator)
#if ENABLE_TTD_DEBUGGING_TEMP_WORKAROUND
        , BPIsSet(false)
        , BPRootEventTime(-1)
        , BPFunctionTime(0)
        , BPLoopTime(0)
        , BPLine(0)
        , BPColumn(0)
        , BPSourceContextId(0)
        , BPBreakAtNextStmtInto(false)
        , BPBreakAtNextStmtDepth(-1)
#endif
    {
        JsSupport::CopyStringToHeapAllocator(logDir, this->m_logInfoRootDir);

        this->m_modeStack.Add(TTDMode::Pending);

        this->m_propertyRecordPinSet = RecyclerNew(threadContext->GetRecycler(), ReferencePinSet, threadContext->GetRecycler());
        this->m_threadContext->GetRecycler()->RootAddRef(this->m_propertyRecordPinSet);
    }

    EventLog::~EventLog()
    {
        this->m_eventList.UnloadEventList();

        this->UnloadRetainedData();

        JsSupport::DeleteStringFromHeapAllocator(this->m_logInfoRootDir);
    }

    void EventLog::InitForTTDRecord()
    {
        //Prepr the logging stream so it is ready for us to write into
        this->m_threadContext->TTDWriteInitializeFunction(this->m_logInfoRootDir.Contents);

        //pin all the current properties so they don't move/disappear on us
        for(Js::PropertyId pid = TotalNumberOfBuiltInProperties + 1; pid < this->m_threadContext->GetMaxPropertyId(); ++pid)
        {
            const Js::PropertyRecord* pRecord = this->m_threadContext->GetPropertyName(pid);
            this->AddPropertyRecord(pRecord);
        }
    }

    void EventLog::InitForTTDReplay()
    {
        this->ParseLogInto();

        Js::PropertyId maxPid = TotalNumberOfBuiltInProperties + 1;
        JsUtil::BaseDictionary<Js::PropertyId, NSSnapType::SnapPropertyRecord*, HeapAllocator> pidMap(&HeapAllocator::Instance);

        for(auto iter = this->m_propertyRecordList.GetIterator(); iter.IsValid(); iter.MoveNext())
        {
            maxPid = max(maxPid, iter.Current()->PropertyId);
            pidMap.AddNew(iter.Current()->PropertyId, iter.Current());
        }

        for(Js::PropertyId cpid = TotalNumberOfBuiltInProperties + 1; cpid <= maxPid; ++cpid)
        {
            NSSnapType::SnapPropertyRecord* spRecord = pidMap.LookupWithKey(cpid, nullptr);
            AssertMsg(spRecord != nullptr, "We have a gap in the sequence of propertyIds. Not sure how that happens.");

            const Js::PropertyRecord* newPropertyRecord = NSSnapType::InflatePropertyRecord(spRecord, this->m_threadContext);

            if(!this->m_propertyRecordPinSet->ContainsKey(const_cast<Js::PropertyRecord*>(newPropertyRecord)))
            {
                this->m_propertyRecordPinSet->AddNew(const_cast<Js::PropertyRecord*>(newPropertyRecord));
            }
        }
    }

    void EventLog::StartTimeTravelOnScript(Js::ScriptContext* ctx)
    {
        AssertMsg(this->m_ttdContext == nullptr, "Should only add 1 time!");

        ctx->SetMode_TTD(this->m_currentMode);
        this->m_ttdContext = ctx;

        ctx->InitializeRecordingActionsAsNeeded_TTD();
    }

    void EventLog::StopTimeTravelOnScript(Js::ScriptContext* ctx)
    {
        AssertMsg(this->m_ttdContext == ctx, "Should be enabled before we disable!");

        ctx->SetMode_TTD(TTDMode::Detached);
        this->m_ttdContext = nullptr;
    }

    void EventLog::SetGlobalMode(TTDMode m)
    {
        AssertMsg(m == TTDMode::Pending || m == TTDMode::Detached || m == TTDMode::RecordEnabled || m == TTDMode::DebuggingEnabled, "These are the only valid global modes");

        this->m_modeStack.SetItem(0, m);
        this->UpdateComputedMode();
    }

    void EventLog::PushMode(TTDMode m)
    {
        AssertMsg(m == TTDMode::ExcludedExecution, "These are the only valid mode modifiers to push");

        this->m_modeStack.Add(m);
        this->UpdateComputedMode();
    }

    void EventLog::PopMode(TTDMode m)
    {
        AssertMsg(m == TTDMode::ExcludedExecution, "These are the only valid mode modifiers to push");
        AssertMsg(this->m_modeStack.Last() == m, "Push/Pop is not matched so something went wrong.");

        this->m_modeStack.RemoveAtEnd();
        this->UpdateComputedMode();
    }

    void EventLog::SetIntoDebuggingMode()
    {
        this->m_modeStack.SetItem(0, TTDMode::DebuggingEnabled);
        this->UpdateComputedMode();

        this->m_ttdContext->InitializeDebuggingActionsAsNeeded_TTD();
    }

    bool EventLog::ShouldPerformRecordAction() const
    {
        bool modeIsRecord = (this->m_currentMode & TTDMode::RecordEnabled) == TTDMode::RecordEnabled;
        bool inRecordableCode = (this->m_currentMode & TTDMode::ExcludedExecution) == TTDMode::Invalid;

        return modeIsRecord & inRecordableCode;
    }

    bool EventLog::ShouldPerformDebugAction() const
    {
        bool modeIsDebug = (this->m_currentMode & TTDMode::DebuggingEnabled) == TTDMode::DebuggingEnabled;
        bool inDebugableCode = (this->m_currentMode & TTDMode::ExcludedExecution) == TTDMode::Invalid;

        return modeIsDebug & inDebugableCode;
    }

    bool EventLog::ShouldTagForJsRT() const
    {
        bool modeIsPending = (this->m_currentMode & TTDMode::Pending) == TTDMode::Pending;
        bool modeIsRecord = (this->m_currentMode & TTDMode::RecordEnabled) == TTDMode::RecordEnabled;
        bool inDebugableCode = (this->m_currentMode & TTDMode::ExcludedExecution) == TTDMode::Invalid;

        return ((modeIsPending | modeIsRecord) & inDebugableCode);
    }

    bool EventLog::ShouldTagForExternalCall() const
    {
        bool modeIsPending = (this->m_currentMode & TTDMode::Pending) == TTDMode::Pending;
        bool modeIsRecord = (this->m_currentMode & TTDMode::RecordEnabled) == TTDMode::RecordEnabled;
        bool modeIsDebug = (this->m_currentMode & TTDMode::DebuggingEnabled) == TTDMode::DebuggingEnabled;
        bool inDebugableCode = (this->m_currentMode & TTDMode::ExcludedExecution) == TTDMode::Invalid;

        return ((modeIsPending | modeIsRecord | modeIsDebug) & inDebugableCode);
    }

    bool EventLog::IsTTDActive() const
    {
        return (this->m_currentMode & TTDMode::TTDActive) != TTDMode::Invalid;
    }

    bool EventLog::IsTTDDetached() const
    {
        return (this->m_currentMode & TTDMode::Detached) != TTDMode::Invalid;
    }

    void EventLog::AddPropertyRecord(const Js::PropertyRecord* record)
    {
        this->m_propertyRecordPinSet->AddNew(const_cast<Js::PropertyRecord*>(record));
    }

    void EventLog::RecordDateTimeEvent(double time)
    {
        AssertMsg(this->ShouldPerformRecordAction(), "Mode is inconsistent!");

        DoubleEventLogEntry* devent = this->m_eventSlabAllocator.SlabNew<DoubleEventLogEntry>(this->GetCurrentEventTimeAndAdvance(), time);
        this->InsertEventAtHead(devent);
    }

    void EventLog::RecordDateStringEvent(Js::JavascriptString* stringValue)
    {
        AssertMsg(this->ShouldPerformRecordAction(), "Mode is inconsistent!");

        TTString copyStr;
        this->m_eventSlabAllocator.CopyStringIntoWLength(stringValue->GetSz(), stringValue->GetLength(), copyStr);

        StringValueEventLogEntry* sevent = this->m_eventSlabAllocator.SlabNew<StringValueEventLogEntry>(this->GetCurrentEventTimeAndAdvance(), copyStr);
        this->InsertEventAtHead(sevent);
    }

    void EventLog::ReplayDateTimeEvent(double* result)
    {
        AssertMsg(this->ShouldPerformDebugAction(), "Mode is inconsistent!");

        if(!this->m_currentReplayEventIterator.IsValid())
        {
            this->AbortReplayReturnToHost();
        }

        AssertMsg(this->m_currentReplayEventIterator.Current()->GetEventTime() == this->m_eventTimeCtr, "Out of Sync!!!");

        DoubleEventLogEntry* devent = DoubleEventLogEntry::As(this->m_currentReplayEventIterator.Current());
        *result = devent->GetDoubleValue();

        this->AdvanceTimeAndPositionForReplay();
    }

    void EventLog::ReplayDateStringEvent(Js::ScriptContext* ctx, Js::JavascriptString** result)
    {
        AssertMsg(this->ShouldPerformDebugAction(), "Mode is inconsistent!");

        if(!this->m_currentReplayEventIterator.IsValid())
        {
            this->AbortReplayReturnToHost();
        }

        AssertMsg(this->m_currentReplayEventIterator.Current()->GetEventTime() == this->m_eventTimeCtr, "Out of Sync!!!");

        StringValueEventLogEntry* sevent = StringValueEventLogEntry::As(this->m_currentReplayEventIterator.Current());
        const TTString& str = sevent->GetStringValue();
        *result = Js::JavascriptString::NewCopyBuffer(str.Contents, str.Length, ctx);

        this->AdvanceTimeAndPositionForReplay();
    }

    void EventLog::RecordExternalEntropyRandomEvent(uint64 seed0, uint64 seed1)
    {
        AssertMsg(this->ShouldPerformRecordAction(), "Shouldn't be logging during replay!");

        RandomSeedEventLogEntry* revent = this->m_eventSlabAllocator.SlabNew<RandomSeedEventLogEntry>(this->GetCurrentEventTimeAndAdvance(), seed0, seed1);
        this->InsertEventAtHead(revent);
    }

    void EventLog::ReplayExternalEntropyRandomEvent(uint64* seed0, uint64* seed1)
    {
        AssertMsg(this->ShouldPerformDebugAction(), "Mode is inconsistent!");

        if(!this->m_currentReplayEventIterator.IsValid())
        {
            this->AbortReplayReturnToHost();
        }

        AssertMsg(this->m_currentReplayEventIterator.Current()->GetEventTime() == this->m_eventTimeCtr, "Out of Sync!!!");

        RandomSeedEventLogEntry* revent = RandomSeedEventLogEntry::As(this->m_currentReplayEventIterator.Current());
        *seed0 = revent->GetSeed0();
        *seed1 = revent->GetSeed1();

        this->AdvanceTimeAndPositionForReplay();
    }

    void EventLog::RecordPropertyEnumEvent(BOOL returnCode, Js::PropertyId pid, Js::PropertyAttributes attributes, Js::JavascriptString* propertyName)
    {
        AssertMsg(this->ShouldPerformRecordAction(), "Shouldn't be logging during replay!");

        TTString optName;
        InitializeAsNullPtrTTString(optName);

#if ENABLE_TTD_INTERNAL_DIAGNOSTICS
        if(returnCode)
        {
            this->m_eventSlabAllocator.CopyStringIntoWLength(propertyName->GetSz(), propertyName->GetLength(), optName);
        }
#else
        if(pid == Js::Constants::NoProperty)
        {
            this->m_slabAllocator.CopyStringIntoWLength(propertyName->GetSz(), propertyName->GetLength(), optName);
        }
#endif

        PropertyEnumStepEventLogEntry* eevent = this->m_eventSlabAllocator.SlabNew<PropertyEnumStepEventLogEntry>(this->GetCurrentEventTimeAndAdvance(), returnCode, pid, attributes, optName);
        this->InsertEventAtHead(eevent);
    }

    void EventLog::ReplayPropertyEnumEvent(BOOL* returnCode, int32* newIndex, const Js::DynamicObject* obj, Js::PropertyId* pid, Js::PropertyAttributes* attributes, Js::JavascriptString** propertyName)
    {
        AssertMsg(this->ShouldPerformDebugAction(), "Mode is inconsistent!");

        if(!this->m_currentReplayEventIterator.IsValid())
        {
            this->AbortReplayReturnToHost();
        }

        AssertMsg(this->m_currentReplayEventIterator.Current()->GetEventTime() == this->m_eventTimeCtr, "Out of Sync!!!");

        PropertyEnumStepEventLogEntry* eevent = PropertyEnumStepEventLogEntry::As(this->m_currentReplayEventIterator.Current());
        *returnCode = eevent->GetReturnCode();
        *pid = eevent->GetPropertyId();
        *attributes = eevent->GetAttributes();

        if(*returnCode)
        {
            AssertMsg(*pid != Js::Constants::NoProperty, "This is so weird we need to figure out what this means.");
            Js::PropertyString* propertyString = obj->GetScriptContext()->GetPropertyString(*pid);
            *propertyName = propertyString;

            const Js::PropertyRecord* pRecord = obj->GetScriptContext()->GetPropertyName(*pid);
            *newIndex = obj->GetDynamicType()->GetTypeHandler()->GetPropertyIndex(pRecord);
        }
        else
        {
            *propertyName = nullptr;

            *newIndex = obj->GetDynamicType()->GetTypeHandler()->GetPropertyCount();
        }

        this->AdvanceTimeAndPositionForReplay();
    }

    void EventLog::RecordSymbolCreationEvent(Js::PropertyId pid)
    {
        AssertMsg(this->ShouldPerformRecordAction(), "Shouldn't be logging during replay!");

        SymbolCreationEventLogEntry* sevent = this->m_eventSlabAllocator.SlabNew<SymbolCreationEventLogEntry>(this->GetCurrentEventTimeAndAdvance(), pid);
        this->InsertEventAtHead(sevent);
    }

    void EventLog::ReplaySymbolCreationEvent(Js::PropertyId* pid)
    {
        AssertMsg(this->ShouldPerformDebugAction(), "Mode is inconsistent!");

        if(!this->m_currentReplayEventIterator.IsValid())
        {
            this->AbortReplayReturnToHost();
        }

        AssertMsg(this->m_currentReplayEventIterator.Current()->GetEventTime() == this->m_eventTimeCtr, "Out of Sync!!!");

        SymbolCreationEventLogEntry* sevent = SymbolCreationEventLogEntry::As(this->m_currentReplayEventIterator.Current());
        *pid = sevent->GetPropertyId();

        this->AdvanceTimeAndPositionForReplay();
    }

    ExternalCallEventBeginLogEntry* EventLog::RecordExternalCallBeginEvent(Js::JavascriptFunction* func, int32 rootDepth, double beginTime)
    {
        AssertMsg(this->ShouldPerformRecordAction(), "Shouldn't be logging during replay!");

        ExternalCallEventBeginLogEntry* eevent = this->m_eventSlabAllocator.SlabNew<ExternalCallEventBeginLogEntry>(this->GetCurrentEventTimeAndAdvance(), rootDepth, beginTime);

#if ENABLE_TTD_INTERNAL_DIAGNOSTICS
        TTString fname;
        Js::JavascriptString* displayName = func->GetDisplayName();
        this->m_eventSlabAllocator.CopyStringIntoWLength(displayName->GetSz(), displayName->GetLength(), fname);

        eevent->SetFunctionName(fname);
#endif

        this->InsertEventAtHead(eevent);

        return eevent;
    }

    void EventLog::RecordExternalCallEndEvent(int64 matchingBeginTime, int32 rootNestingDepth, bool hasScriptException, bool hasTerminatingException, double endTime, Js::Var value)
    {
        AssertMsg(this->ShouldPerformRecordAction(), "Shouldn't be logging during replay!");

        NSLogValue::ArgRetValue retVal;
        NSLogValue::ExtractArgRetValueFromVar(value, retVal);

        ExternalCallEventEndLogEntry* eevent = this->m_eventSlabAllocator.SlabNew<ExternalCallEventEndLogEntry>(this->GetCurrentEventTimeAndAdvance(), matchingBeginTime, rootNestingDepth, hasScriptException, hasTerminatingException, endTime, retVal);

        this->InsertEventAtHead(eevent);
    }

    ExternalCallEventBeginLogEntry* EventLog::RecordPromiseRegisterBeginEvent(int32 rootDepth, double beginTime)
    {
        AssertMsg(this->ShouldPerformRecordAction(), "Shouldn't be logging during replay!");

        ExternalCallEventBeginLogEntry* eevent = this->m_eventSlabAllocator.SlabNew<ExternalCallEventBeginLogEntry>(this->GetCurrentEventTimeAndAdvance(), rootDepth, beginTime);

#if ENABLE_TTD_INTERNAL_DIAGNOSTICS
        TTString pname;
        this->m_eventSlabAllocator.CopyNullTermStringInto(L"Register Promise Function", pname);

        eevent->SetFunctionName(pname);
#endif

        this->InsertEventAtHead(eevent);

        return eevent;
    }

    void EventLog::RecordPromiseRegisterEndEvent(int64 matchingBeginTime, int32 rootDepth, double endTime, Js::Var value)
    {
        AssertMsg(this->ShouldPerformRecordAction(), "Shouldn't be logging during replay!");

        NSLogValue::ArgRetValue retVal;
        NSLogValue::ExtractArgRetValueFromVar(value, retVal);

        ExternalCallEventEndLogEntry* eevent = this->m_eventSlabAllocator.SlabNew<ExternalCallEventEndLogEntry>(this->GetCurrentEventTimeAndAdvance(), matchingBeginTime, rootDepth, false, false, endTime, retVal);

        this->InsertEventAtHead(eevent);
    }

    void EventLog::ReplayExternalCallEvent(Js::ScriptContext* ctx, Js::Var* result)
    {
        AssertMsg(this->ShouldPerformDebugAction(), "Mode is inconsistent!");

        if(!this->m_currentReplayEventIterator.IsValid())
        {
            this->AbortReplayReturnToHost();
        }

        AssertMsg(this->m_currentReplayEventIterator.Current()->GetEventTime() == this->m_eventTimeCtr, "Out of Sync!!!");

        //advance the begin event item off the event list
        ExternalCallEventBeginLogEntry* eeventBegin = ExternalCallEventBeginLogEntry::As(this->m_currentReplayEventIterator.Current());
        this->AdvanceTimeAndPositionForReplay();

        //replay anything that happens when we are out of the call
        if(this->m_currentReplayEventIterator.Current()->GetEventKind() == EventLogEntry::EventKind::JsRTActionTag)
        {
            this->ReplayActionLoopStep();
        }

        if(this->m_currentReplayEventIterator.Current() == nullptr)
        {
            this->AbortReplayReturnToHost();
        }

        AssertMsg(this->m_currentReplayEventIterator.Current()->GetEventTime() == this->m_eventTimeCtr, "Out of Sync!!!");

        //advance the end event item off the event list and get the return value
        ExternalCallEventEndLogEntry* eeventEnd = ExternalCallEventEndLogEntry::As(this->m_currentReplayEventIterator.Current());
        this->AdvanceTimeAndPositionForReplay();

        AssertMsg(eeventBegin->GetRootNestingDepth() == eeventEnd->GetRootNestingDepth(), "These should always match!!!");

        *result = NSLogValue::InflateArgRetValueIntoVar(eeventEnd->GetReturnValue(), ctx);
    }

    void EventLog::PushCallEvent(Js::FunctionBody* fbody, bool isInFinally)
    {
        AssertMsg(this->IsTTDActive(), "Should check this first.");

        if(this->ShouldPerformRecordAction() | this->ShouldPerformDebugAction())
        {
#if ENABLE_TTD_DEBUGGING
            //Clear any previous last return frame info
            this->ClearReturnFrame();
#endif

            this->m_runningFunctionTimeCtr++;

            SingleCallCounter cfinfo;
            cfinfo.Function = fbody;

#if ENABLE_TTD_INTERNAL_DIAGNOSTICS
            cfinfo.Name = fbody->GetExternalDisplayName();
#endif

            cfinfo.EventTime = this->m_eventTimeCtr; //don't need to advance just note what the event time was when this is called
            cfinfo.FunctionTime = this->m_runningFunctionTimeCtr;
            cfinfo.LoopTime = 0;

#if ENABLE_TTD_DEBUGGING
            cfinfo.CurrentStatementIndex = -1;
            cfinfo.CurrentStatementLoopTime = 0;

            cfinfo.LastStatementIndex = -1;
            cfinfo.LastStatementLoopTime = 0;

            cfinfo.CurrentStatementBytecodeMin = UINT32_MAX;
            cfinfo.CurrentStatementBytecodeMax = UINT32_MAX;
#endif

            this->m_callStack.Add(cfinfo);
        }
    }

    void EventLog::PopCallEvent(Js::FunctionBody* fbody, Js::Var result)
    {
        AssertMsg(this->IsTTDActive(), "Should check this first.");

        if(this->ShouldPerformRecordAction() | this->ShouldPerformDebugAction())
        {
#if ENABLE_TTD_DEBUGGING
            if(!this->HasImmediateExceptionFrame())
            {
                this->SetReturnAndExceptionFramesFromCurrent(true, false);
            }
#endif

            this->m_runningFunctionTimeCtr++;
            this->m_callStack.RemoveAtEnd();
        }
    }

    void EventLog::PopCallEventException(bool isFirstException)
    {
        AssertMsg(this->IsTTDActive(), "Should check this first.");

        if(this->ShouldPerformRecordAction() | this->ShouldPerformDebugAction())
        {
#if ENABLE_TTD_DEBUGGING
            if(isFirstException)
            {
                this->SetReturnAndExceptionFramesFromCurrent(false, true);
            }
#endif

            this->m_runningFunctionTimeCtr++;
            this->m_callStack.RemoveAtEnd();
        }
    }

#if ENABLE_TTD_DEBUGGING
    bool EventLog::HasImmediateReturnFrame() const
    {
        return this->m_isReturnFrame;
    }

    bool EventLog::HasImmediateExceptionFrame() const
    {
        return this->m_isExceptionFrame;
    }

    const SingleCallCounter& EventLog::GetImmediateReturnFrame() const
    {
        AssertMsg(this->IsTTDActive(), "Should check this first.");
        AssertMsg(this->m_isReturnFrame, "This data is invalid if we haven't recorded a return!!!");

        return this->m_lastFrame;
    }

    const SingleCallCounter& EventLog::GetImmediateExceptionFrame() const
    {
        AssertMsg(this->IsTTDActive(), "Should check this first.");
        AssertMsg(this->m_isExceptionFrame, "This data is invalid if we haven't recorded an exception!!!");

        return this->m_lastFrame;
    }

    void EventLog::ClearReturnFrame()
    {
        this->m_isReturnFrame = false;
    }

    void EventLog::ClearExceptionFrame()
    {
        this->m_isExceptionFrame = false;
    }

    void EventLog::SetReturnAndExceptionFramesFromCurrent(bool setReturn, bool setException)
    {
        AssertMsg(this->IsTTDActive(), "Should check this first.");
        AssertMsg(this->m_callStack.Count() != 0, "We must have pushed something in order to have an exception or return!!!");
        AssertMsg((setReturn | setException) & (!setReturn | !setException), "We can only have a return or exception -- exactly one not both!!!");

        this->m_isReturnFrame = setReturn;
        this->m_isExceptionFrame = setException;

        this->m_lastFrame = this->m_callStack.Last();
    }
#endif

    void EventLog::UpdateLoopCountInfo()
    {
        AssertMsg(this->IsTTDActive(), "Should check this first.");

        if(this->ShouldPerformRecordAction() | this->ShouldPerformDebugAction())
        {
            SingleCallCounter& cfinfo = this->m_callStack.Last();
            cfinfo.LoopTime++;
        }
    }

#if ENABLE_TTD_DEBUGGING
    bool EventLog::UpdateCurrentStatementInfo(uint bytecodeOffset)
    {
        SingleCallCounter& cfinfo = this->GetTopCallCounter();
        if((cfinfo.CurrentStatementBytecodeMin <= bytecodeOffset) & (bytecodeOffset <= cfinfo.CurrentStatementBytecodeMax))
        {
            return false;
        }
        else
        {
            Js::FunctionBody* fb = cfinfo.Function;

            int32 cIndex = fb->GetEnclosingStatementIndexFromByteCode(bytecodeOffset, true);
            AssertMsg(cIndex != -1, "Should always have a mapping.");

            //we moved to a new statement
            Js::FunctionBody::StatementMap* pstmt = fb->GetStatementMaps()->Item(cIndex);
            bool newstmt = (cIndex != cfinfo.CurrentStatementIndex && pstmt->byteCodeSpan.begin <= (int)bytecodeOffset && (int)bytecodeOffset <= pstmt->byteCodeSpan.end);
            if(newstmt)
            {
                cfinfo.LastStatementIndex = cfinfo.CurrentStatementIndex;
                cfinfo.LastStatementLoopTime = cfinfo.CurrentStatementLoopTime;

                cfinfo.CurrentStatementIndex = cIndex;
                cfinfo.CurrentStatementLoopTime = cfinfo.LoopTime;

                cfinfo.CurrentStatementBytecodeMin = (uint32)pstmt->byteCodeSpan.begin;
                cfinfo.CurrentStatementBytecodeMax = (uint32)pstmt->byteCodeSpan.end;
            }

            return newstmt;
        }
    }

    void EventLog::GetTimeAndPositionForDebugger(int64* rootEventTime, uint64* ftime, uint64* ltime, uint32* line, uint32* column, uint32* sourceId) const
    {
        AssertMsg(this->ShouldPerformDebugAction(), "This should only be executed if we are debugging.");

        const SingleCallCounter& cfinfo = this->GetTopCallCounter();

        *rootEventTime = this->m_topLevelCallbackEventTime;
        *ftime = cfinfo.FunctionTime;
        *ltime = cfinfo.LoopTime;

        ULONG srcLine = 0;
        LONG srcColumn = -1;
        uint32 startOffset = cfinfo.Function->GetStatementStartOffset(cfinfo.CurrentStatementIndex);
        cfinfo.Function->GetSourceLineFromStartOffset_TTD(startOffset, &srcLine, &srcColumn);

        *line = (uint32)srcLine;
        *column = (uint32)srcColumn;
        *sourceId = cfinfo.Function->GetSourceContextId();
    }

    bool EventLog::GetPreviousTimeAndPositionForDebugger(int64* rootEventTime, uint64* ftime, uint64* ltime, uint32* line, uint32* column, uint32* sourceId) const
    {
        AssertMsg(this->ShouldPerformDebugAction(), "This should only be executed if we are debugging.");

        const SingleCallCounter& cfinfo = this->GetTopCallCounter();

        //this always works -- even if we are at the start of the function
        *rootEventTime = this->m_topLevelCallbackEventTime;

        //check if we are at the first statement in the callback event
        if(this->m_callStack.Count() == 1 && cfinfo.LastStatementIndex == -1)
        {
            return true;
        }

        //if we are at the first statement in the function then we want the parents current
        Js::FunctionBody* fbody = nullptr;
        int32 statementIndex = -1;
        if(cfinfo.LastStatementIndex == -1)
        {
            const SingleCallCounter& cfinfoCaller = this->GetTopCallCallerCounter();
            *ftime = cfinfoCaller.FunctionTime;
            *ltime = cfinfoCaller.CurrentStatementLoopTime;

            fbody = cfinfoCaller.Function;
            statementIndex = cfinfoCaller.CurrentStatementIndex;
        }
        else
        {
            *ftime = cfinfo.FunctionTime;
            *ltime = cfinfo.LastStatementLoopTime;

            fbody = cfinfo.Function;
            statementIndex = cfinfo.LastStatementIndex;
        }

        ULONG srcLine = 0;
        LONG srcColumn = -1;
        uint32 startOffset = fbody->GetStatementStartOffset(statementIndex);
        fbody->GetSourceLineFromStartOffset_TTD(startOffset, &srcLine, &srcColumn);

        *line = (uint32)srcLine;
        *column = (uint32)srcColumn;
        *sourceId = fbody->GetSourceContextId();

        return false;
    }

    bool EventLog::GetExceptionTimeAndPositionForDebugger(int64* rootEventTime, uint64* ftime, uint64* ltime, uint32* line, uint32* column, uint32* sourceId) const
    {
        if(!this->m_isExceptionFrame)
        {
            *rootEventTime = -1;
            *ftime = 0;
            *ltime = 0;

            *line = 0;
            *column = 0;
            *sourceId = 0;

            return false;
        }
        else
        {
            *rootEventTime = this->m_topLevelCallbackEventTime;
            *ftime = this->m_lastFrame.FunctionTime;
            *ltime = this->m_lastFrame.CurrentStatementLoopTime;

            ULONG srcLine = 0;
            LONG srcColumn = -1;
            uint32 startOffset = this->m_lastFrame.Function->GetStatementStartOffset(this->m_lastFrame.CurrentStatementIndex);
            this->m_lastFrame.Function->GetSourceLineFromStartOffset_TTD(startOffset, &srcLine, &srcColumn);

            *line = (uint32)srcLine;
            *column = (uint32)srcColumn;
            *sourceId = this->m_lastFrame.Function->GetSourceContextId();

            return true;
        }
    }

    bool EventLog::GetImmediateReturnTimeAndPositionForDebugger(int64* rootEventTime, uint64* ftime, uint64* ltime, uint32* line, uint32* column, uint32* sourceId) const
    {
        if(!this->m_isReturnFrame)
        {
            *rootEventTime = -1;
            *ftime = 0;
            *ltime = 0;

            *line = 0;
            *column = 0;
            *sourceId = 0;

            return false;
        }
        else
        {
            *rootEventTime = this->m_topLevelCallbackEventTime;
            *ftime = this->m_lastFrame.FunctionTime;
            *ltime = this->m_lastFrame.CurrentStatementLoopTime;

            ULONG srcLine = 0;
            LONG srcColumn = -1;
            uint32 startOffset = this->m_lastFrame.Function->GetStatementStartOffset(this->m_lastFrame.CurrentStatementIndex);
            this->m_lastFrame.Function->GetSourceLineFromStartOffset_TTD(startOffset, &srcLine, &srcColumn);

            *line = (uint32)srcLine;
            *column = (uint32)srcColumn;
            *sourceId = this->m_lastFrame.Function->GetSourceContextId();

            return true;
        }
    }

    int64 EventLog::GetCurrentHostCallbackId() const
    {
        return this->m_hostCallbackId;
    }

    int64 EventLog::GetCurrentTopLevelEventTime() const
    {
        return this->m_topLevelCallbackEventTime;
    }

    JsRTCallbackAction* EventLog::GetEventForHostCallbackId(bool wantRegisterOp, int64 hostIdOfInterest) const
    {
        if(hostIdOfInterest == -1)
        {
            return nullptr;
        }

        for(auto iter = this->m_currentReplayEventIterator; iter.IsValid(); iter.MovePrevious())
        {
            if(iter.Current()->GetEventKind() == EventLogEntry::EventKind::JsRTActionTag && JsRTActionLogEntry::As(iter.Current())->GetActionTypeTag() == JsRTActionType::CallbackOp)
            {
                JsRTCallbackAction* callbackAction = JsRTCallbackAction::As(JsRTActionLogEntry::As(iter.Current()));
                if(callbackAction->GetAssociatedHostCallbackId() == hostIdOfInterest && callbackAction->IsCreateOp() == wantRegisterOp)
                {
                    return callbackAction;
                }
            }
        }

        return nullptr;
    }

    int64 EventLog::GetKthEventTime(uint32 k) const
    {
        uint32 topLevelCount = 0;
        for(auto iter = this->m_eventList.GetIteratorAtFirst(); iter.IsValid(); iter.MoveNext())
        {
            if(iter.Current()->GetEventKind() == EventLogEntry::EventKind::JsRTActionTag && JsRTActionLogEntry::As(iter.Current())->IsRootCallBegin())
            {
                JsRTCallFunctionBeginAction* rootCallAction = JsRTCallFunctionBeginAction::As(JsRTActionLogEntry::As(iter.Current()));
                topLevelCount++;

                if(topLevelCount == k)
                {
                    return rootCallAction->GetEventTime();
                }
            }
        }

        AssertMsg(false, "Bad event index!!!");
        return -1;
    }

#if ENABLE_TTD_DEBUGGING_TEMP_WORKAROUND
    void EventLog::ClearBreakpointOnNextStatement()
    {
        this->BPBreakAtNextStmtInto = false;
        this->BPBreakAtNextStmtDepth = -1;
    }

    void EventLog::SetBreakpointOnNextStatement(bool into)
    {
        this->BPBreakAtNextStmtInto = into;
        this->BPBreakAtNextStmtDepth = this->m_callStack.Count();
    }

    void EventLog::BPPrintBaseVariable(Js::ScriptContext* ctx, Js::Var var, bool expandObjects)
    {
        Js::TypeId tid = Js::JavascriptOperators::GetTypeId(var);
        switch(tid)
        {
        case Js::TypeIds_Undefined:
            wprintf(L"undefined");
            break;
        case Js::TypeIds_Null:
            wprintf(L"null");
            break;
        case Js::TypeIds_Boolean:
            wprintf(Js::JavascriptBoolean::FromVar(var)->GetValue() ? L"true" : L"false");
            break;
        case Js::TypeIds_Integer:
            wprintf(L"%I32i", Js::TaggedInt::ToInt32(var));
            break;
        case Js::TypeIds_Number:
        {
            if(Js::NumberUtilities::IsNan(Js::JavascriptNumber::GetValue(var)))
            {
                wprintf(L"#Nan");
            }
            else if(!Js::NumberUtilities::IsFinite(Js::JavascriptNumber::GetValue(var)))
            {
                wprintf(L"Infinite");
            }
            else
            {
                if(floor(Js::JavascriptNumber::GetValue(var)) == Js::JavascriptNumber::GetValue(var))
                {
                    wprintf(L"%I64i", (int64)Js::JavascriptNumber::GetValue(var));
                }
                else
                {
                    wprintf(L"%.22f", Js::JavascriptNumber::GetValue(var));
                }
            }
            break;
        }
        case Js::TypeIds_Int64Number:
            wprintf(L"%I64i", Js::JavascriptInt64Number::FromVar(var)->GetValue());
            break;
        case Js::TypeIds_UInt64Number:
            wprintf(L"%I64u", Js::JavascriptUInt64Number::FromVar(var)->GetValue());
            break;
        case Js::TypeIds_String:
            wprintf(L"\"%ls\"", Js::JavascriptString::FromVar(var)->GetSz());
            break;
        case Js::TypeIds_Symbol:
        case Js::TypeIds_Enumerator:
        case Js::TypeIds_VariantDate:
        case Js::TypeIds_SIMDFloat32x4:
        case Js::TypeIds_SIMDFloat64x2:
        case Js::TypeIds_SIMDInt32x4:
            wprintf(L"Printing not supported for variable!");
            break;
        default:
        {
#if ENABLE_TTD_IDENTITY_TRACING
            if(Js::StaticType::Is(tid))
            {
                wprintf(L"static object w/o identity: {");
            }
            else
            {
                wprintf(L"object w/ identity %I64i: {", Js::DynamicObject::FromVar(var)->TTDObjectIdentityTag);
            }
#else
            wprintf(L"untagged object: {");
#endif

            Js::RecyclableObject* obj = Js::RecyclableObject::FromVar(var);
            int32 pcount = obj->GetPropertyCount();
            bool first = true;
            for(int32 i = 0; i < pcount; ++i)
            {
                Js::PropertyId propertyId = obj->GetPropertyId((Js::PropertyIndex)i);
                if(Js::IsInternalPropertyId(propertyId))
                {
                    continue;
                }

                if(!first)
                {
                    wprintf(L", ");
                }
                first = false;

                wprintf(L"%ls: ", ctx->GetPropertyName(propertyId)->GetBuffer());

                Js::Var pval = nullptr;
                Js::JavascriptOperators::GetProperty(obj, propertyId, &pval, ctx, nullptr);
                this->BPPrintBaseVariable(ctx, pval, false);
            }

            wprintf(L"}");
            break;
        }
        }
    }

    void EventLog::BPPrintVariable(Js::ScriptContext* ctx, LPCWSTR name)
    {
        Js::PropertyId propertyId = ctx->GetOrAddPropertyIdTracked(JsUtil::CharacterBuffer<WCHAR>(name, (charcount_t)wcslen(name)));
        Js::Var var = Js::JavascriptOperators::GetProperty(ctx->GetGlobalObject(), propertyId, ctx, nullptr);

        if(var == nullptr)
        {
            wprintf(L"Name was not found in the global scope.\n");
            return;
        }

        wprintf(L"  -> ");
        this->BPPrintBaseVariable(ctx, var, true);
        wprintf(L"\n");
    }

    void EventLog::BPCheckAndAction(Js::ScriptContext* ctx)
    {
        AssertMsg(this->ShouldPerformDebugAction(), "This should only be executed if we are debugging.");

        const SingleCallCounter& cfinfo = this->GetTopCallCounter();

        bool bpHit = false;

        if(this->BPBreakAtNextStmtDepth != -1)
        {
            if(this->BPBreakAtNextStmtInto)
            {
                bpHit = true;
            }
            else
            {
                bpHit = this->m_callStack.Count() <= this->BPBreakAtNextStmtDepth;
            }
        }

        if(!bpHit)
        {
            ULONG srcLine = 0;
            LONG srcColumn = -1;
            uint32 startOffset = cfinfo.Function->GetStatementStartOffset(cfinfo.CurrentStatementIndex);
            cfinfo.Function->GetSourceLineFromStartOffset_TTD(startOffset, &srcLine, &srcColumn);

            bool lineMatch = (this->BPLine == (uint32)srcLine);
            bool columnMatch = (this->BPColumn == (uint32)srcColumn);
            bool srcMatch = (this->BPSourceContextId == cfinfo.Function->GetSourceContextId());

            bool etimeMatch = (this->BPRootEventTime == this->m_topLevelCallbackEventTime);
            bool ftimeMatch = (this->BPFunctionTime == cfinfo.FunctionTime);
            bool ltimeMatch = (this->BPLoopTime == cfinfo.LoopTime);

            bpHit = (lineMatch & columnMatch & srcMatch & etimeMatch & ftimeMatch & ltimeMatch);
        }

        int64 optAbortTime = 0;
        wchar_t* optAbortMsg = nullptr;
        bool continueExecution = true;

        if(bpHit)
        {
            //if we hit a breakpoint then disable future hits -- unless we re-enable in this handler
            this->BPIsSet = false; 
            this->BPRootEventTime = -1;
            this->ClearBreakpointOnNextStatement();

            //print the call stack
            int callStackPrint = min(this->m_callStack.Count(), 5);
            if(this->m_callStack.Count() != callStackPrint)
            {
                wprintf(L"...\n");
            }

            for(int32 i = this->m_callStack.Count() - callStackPrint; i < this->m_callStack.Count() - 1; ++i)
            {
                wprintf(L"%ls\n", this->m_callStack.Item(i).Function->GetDisplayName());
            }

            //print the current line information
            ULONG srcLine = 0;
            LONG srcColumn = -1;
            LPCUTF8 srcBegin = nullptr;
            LPCUTF8 srcEnd = nullptr;
            uint32 startOffset = cfinfo.Function->GetStatementStartOffset(cfinfo.CurrentStatementIndex);
            cfinfo.Function->GetSourceLineFromStartOffset_TTD(startOffset, &srcBegin, &srcEnd, &srcLine, &srcColumn);

            wprintf(L"----\n");
            wprintf(L"%ls @ ", this->m_callStack.Last().Function->GetDisplayName());
            if(Js::Configuration::Global.flags.TTDCmdsFromFile == nullptr)
            {
                wprintf(L"line: %u, column: %i, etime: %I64i, ftime: %I64u, ltime: %I64u\n\n", srcLine, srcColumn, this->m_topLevelCallbackEventTime, cfinfo.FunctionTime, cfinfo.LoopTime);
            }
            else
            {
                wprintf(L"line: %u, column: %i, ftime: %I64u, ltime: %I64u\n\n", srcLine, srcColumn, cfinfo.FunctionTime, cfinfo.LoopTime);
            }

            while(srcBegin != srcEnd)
            {
                wprintf(L"%C", (wchar)*srcBegin);
                srcBegin++;
            }
            wprintf(L"\n\n");

            continueExecution = this->BPDbgCallback(&optAbortTime, &optAbortMsg);
        }

        if(!continueExecution)
        {
            throw TTDebuggerAbortException::CreateTopLevelAbortRequest(optAbortTime, optAbortMsg);
        }
    }
#endif
#endif

    void EventLog::ResetCallStackForTopLevelCall(int64 topLevelCallbackEventTime, int64 hostCallbackId)
    {
        AssertMsg(this->m_callStack.Count() == 0, "We should be at the top-level entry!!!");

        this->m_runningFunctionTimeCtr = 0;
        this->m_topLevelCallbackEventTime = topLevelCallbackEventTime;
        this->m_hostCallbackId = hostCallbackId;

#if ENABLE_TTD_DEBUGGING
        this->ClearReturnFrame();
        this->ClearExceptionFrame();
#endif
    }

    double EventLog::GetElapsedSnapshotTime()
    {
        return this->m_elapsedExecutionTimeSinceSnapshot;
    }

    void EventLog::IncrementElapsedSnapshotTime(double addtlTime)
    {
        this->m_elapsedExecutionTimeSinceSnapshot += addtlTime;
    }

    void EventLog::AbortReplayReturnToHost()
    {
        throw TTDebuggerAbortException::CreateAbortEndOfLog(L"End of log reached -- returning to top-level.");
    }

    bool EventLog::HasDoneFirstSnapshot() const
    {
        return !this->m_eventList.IsEmpty();
    }

    void EventLog::DoSnapshotExtract(bool firstSnap)
    {
        AssertMsg(this->m_ttdContext != nullptr, "We aren't actually tracking anything!!!");

        SnapShot* snap = nullptr;
        TTD_LOG_TAG logTag = TTD_INVALID_LOG_TAG;
        TTD_IDENTITY_TAG idTag = TTD_INVALID_IDENTITY_TAG;

        this->DoSnapshotExtract_Helper(firstSnap, &snap, &logTag, &idTag);

        ///////////////////////////
        //Create the event object and addi it to the log

        uint64 etime = this->GetCurrentEventTimeAndAdvance();

        SnapshotEventLogEntry* sevent = this->m_eventSlabAllocator.SlabNew<SnapshotEventLogEntry>(etime, snap, etime, logTag, idTag);
        this->InsertEventAtHead(sevent);

        this->m_elapsedExecutionTimeSinceSnapshot = 0.0;
    }

    void EventLog::DoRtrSnapIfNeeded()
    {
        AssertMsg(this->m_ttdContext != nullptr, "We aren't actually tracking anything!!!");
        AssertMsg(this->m_currentReplayEventIterator.IsValid() && this->m_currentReplayEventIterator.Current()->GetEventKind() == EventLogEntry::EventKind::JsRTActionTag, "Something in wrong with the event position.");
        AssertMsg(JsRTActionLogEntry::As(this->m_currentReplayEventIterator.Current())->IsRootCallBegin(), "Something in wrong with the event position.");

        JsRTCallFunctionBeginAction* rootCall = JsRTCallFunctionBeginAction::As(JsRTActionLogEntry::As(this->m_currentReplayEventIterator.Current()));

        if(!rootCall->HasReadyToRunSnapshotInfo())
        {
            SnapShot* snap = nullptr;
            TTD_LOG_TAG logTag = TTD_INVALID_LOG_TAG;
            TTD_IDENTITY_TAG idTag = TTD_INVALID_IDENTITY_TAG;
            this->DoSnapshotExtract_Helper(false, &snap, &logTag, &idTag);

            rootCall->SetReadyToRunSnapshotInfo(snap, logTag, idTag);
        }
    }

    int64 EventLog::FindSnapTimeForEventTime(int64 targetTime, bool* newCtxsNeeded)
    {
        *newCtxsNeeded = false;
        int64 snapTime = -1;

        for(auto iter = this->m_eventList.GetIteratorAtLast(); iter.IsValid(); iter.MovePrevious())
        {
            if(iter.Current()->GetEventTime() <= targetTime)
            {
                if(iter.Current()->GetEventKind() == EventLogEntry::EventKind::SnapshotTag)
                {
                    snapTime = iter.Current()->GetEventTime();
                    break;
                }

                if(iter.Current()->GetEventKind() == EventLogEntry::EventKind::JsRTActionTag && JsRTActionLogEntry::As(iter.Current())->IsRootCallBegin())
                {
                    JsRTCallFunctionBeginAction* rootEntry = JsRTCallFunctionBeginAction::As(JsRTActionLogEntry::As(iter.Current()));

                    if(rootEntry->HasReadyToRunSnapshotInfo())
                    {
                        snapTime = iter.Current()->GetEventTime();
                        break;
                    }
                }
            }
        }

        //if this->m_lastInflateMap then this is the first time we have inflated (otherwise we always nullify and recreate as a pair)
        if(this->m_lastInflateMap != nullptr)
        {
            *newCtxsNeeded = (snapTime != this->m_lastInflateSnapshotTime);
        }

        return snapTime;
    }

    void EventLog::UpdateInflateMapForFreshScriptContexts()
    {
        this->m_ttdContext = nullptr;

        if(this->m_lastInflateMap != nullptr)
        {
            HeapDelete(this->m_lastInflateMap);
            this->m_lastInflateMap = nullptr;
        }
    }

    void EventLog::DoSnapshotInflate(int64 etime)
    {
        //collect anything that is dead
        this->m_threadContext->GetRecycler()->CollectNow<CollectNowForceInThread>();

        const SnapShot* snap = nullptr;
        int64 restoreEventTime = -1;
        TTD_LOG_TAG restoreLogTagCtr = TTD_INVALID_LOG_TAG;
        TTD_IDENTITY_TAG restoreIdentityTagCtr = TTD_INVALID_IDENTITY_TAG;

        for(auto iter = this->m_eventList.GetIteratorAtLast(); iter.IsValid(); iter.MovePrevious())
        {
            if(iter.Current()->GetEventTime() == etime)
            {
                if(iter.Current()->GetEventKind() == EventLogEntry::EventKind::SnapshotTag)
                {
                    SnapshotEventLogEntry* snpEntry = SnapshotEventLogEntry::As(iter.Current());
                    snpEntry->EnsureSnapshotDeserialized(this->m_logInfoRootDir.Contents, this->m_threadContext);

                    restoreEventTime = snpEntry->GetRestoreEventTime();
                    restoreLogTagCtr = snpEntry->GetRestoreLogTag();
                    restoreIdentityTagCtr = snpEntry->GetRestoreIdentityTag();

                    snap = snpEntry->GetSnapshot();
                }
                else
                {
                    JsRTCallFunctionBeginAction* rootEntry = JsRTCallFunctionBeginAction::As(JsRTActionLogEntry::As(iter.Current()));

                    SnapShot* ncSnap = nullptr;
                    rootEntry->GetReadyToRunSnapshotInfo(&ncSnap, &restoreLogTagCtr, &restoreIdentityTagCtr);
                    snap = ncSnap;

                    restoreEventTime = rootEntry->GetEventTime();
                }

                break;
            }
        }
        AssertMsg(snap != nullptr, "Log should start with a snapshot!!!");

        //
        //TODO: we currently assume a single context here which we load into the existing ctx
        //
        const UnorderedArrayList<NSSnapValues::SnapContext, TTD_ARRAY_LIST_SIZE_SMALL>& snpCtxs = snap->GetContextList();
        AssertMsg(this->m_ttdContext != nullptr, "We are assuming a single context");
        const NSSnapValues::SnapContext* sCtx = snpCtxs.GetIterator().Current();

        if(this->m_lastInflateMap != nullptr)
        {
            this->m_lastInflateMap->PrepForReInflate(snap->ContextCount(), snap->HandlerCount(), snap->TypeCount(), snap->PrimitiveCount() + snap->ObjectCount(), snap->BodyCount(), snap->EnvCount(), snap->SlotArrayCount());

            NSSnapValues::InflateScriptContext(sCtx, this->m_ttdContext, this->m_lastInflateMap);
        }
        else
        {
            this->m_lastInflateMap = HeapNew(InflateMap);
            this->m_lastInflateMap->PrepForInitialInflate(this->m_threadContext, snap->ContextCount(), snap->HandlerCount(), snap->TypeCount(), snap->PrimitiveCount() + snap->ObjectCount(), snap->BodyCount(), snap->EnvCount(), snap->SlotArrayCount());
            this->m_lastInflateSnapshotTime = etime;

            NSSnapValues::InflateScriptContext(sCtx, this->m_ttdContext, this->m_lastInflateMap);

            //We don't want to have a bunch of snapshots in memory (that will get big fast) so unload all but the current one
            for(auto iter = this->m_eventList.GetIteratorAtLast(); iter.IsValid(); iter.MovePrevious())
            {
                if(iter.Current()->GetEventTime() != etime)
                {
                    iter.Current()->UnloadSnapshot();
                }
            }
        }

        //reset the tagged object maps before we do the inflate
        this->m_threadContext->TTDInfo->ResetTagsForRestore_TTD(restoreLogTagCtr, restoreIdentityTagCtr);
        this->m_eventTimeCtr = restoreEventTime;

        snap->Inflate(this->m_lastInflateMap, sCtx);
        this->m_lastInflateMap->CleanupAfterInflate();

        if(!this->m_eventList.IsEmpty())
        {
            this->m_currentReplayEventIterator = this->m_eventList.GetIteratorAtLast();
            while(this->m_currentReplayEventIterator.Current()->GetEventTime() != this->m_eventTimeCtr)
            {
                this->m_currentReplayEventIterator.MovePrevious();
            }

            //we want to advance to the event immediately after the snapshot as well so do that
            if(this->m_currentReplayEventIterator.Current()->GetEventKind() == EventLogEntry::EventKind::SnapshotTag)
            {
                this->m_eventTimeCtr++;
                this->m_currentReplayEventIterator.MoveNext();
            }

            //clear this out -- it shouldn't matter for most JsRT actions (alloc etc.) and should be reset by any call actions
            this->ResetCallStackForTopLevelCall(-1, -1);
        }
    }

    void EventLog::ReplaySingleEntry()
    {
        AssertMsg(this->ShouldPerformDebugAction(), "Mode is inconsistent!");

        if(!this->m_currentReplayEventIterator.IsValid())
        {
            this->AbortReplayReturnToHost();
        }

        switch(this->m_currentReplayEventIterator.Current()->GetEventKind())
        {
            case EventLogEntry::EventKind::SnapshotTag:
                this->AdvanceTimeAndPositionForReplay(); //nothing to replay so we just move along
                break;
            case EventLogEntry::EventKind::JsRTActionTag:
                this->ReplayActionLoopStep(); 
                break;
            default:
                AssertMsg(false, "Either this is an invalid tag to replay directly (should be driven internally) or it is not known!!!");
        }
    }

    void EventLog::ReplayToTime(int64 eventTime)
    {
        AssertMsg(this->m_currentReplayEventIterator.IsValid() && this->m_currentReplayEventIterator.Current()->GetEventTime() <= eventTime, "This isn't going to work.");

        //Note use of == in test as we want a specific root event not just sometime later
        while(this->m_currentReplayEventIterator.Current()->GetEventTime() != eventTime)
        {
            this->ReplaySingleEntry();

            AssertMsg(this->m_currentReplayEventIterator.IsValid() && m_currentReplayEventIterator.Current()->GetEventTime() <= eventTime, "Something is not lined up correctly.");
        }
    }

    void EventLog::ReplayFullTrace()
    {
        while(this->m_currentReplayEventIterator.IsValid())
        {
            this->ReplaySingleEntry();
        }

        //we are at end of trace so abort to top level
        this->AbortReplayReturnToHost();
    }

    void EventLog::RecordJsRTAllocateInt(Js::ScriptContext* ctx, int32 ival)
    {
        uint64 etime = this->GetCurrentEventTimeAndAdvance();
        TTD_LOG_TAG ctxTag = TTD_EXTRACT_CTX_LOG_TAG(ctx);

        JsRTNumberAllocateAction* allocEvent = this->m_eventSlabAllocator.SlabNew<JsRTNumberAllocateAction>(etime, ctxTag, true, ival, 0.0);

        this->InsertEventAtHead(allocEvent);
    }

    void EventLog::RecordJsRTAllocateDouble(Js::ScriptContext* ctx, double dval)
    {
        uint64 etime = this->GetCurrentEventTimeAndAdvance();
        TTD_LOG_TAG ctxTag = TTD_EXTRACT_CTX_LOG_TAG(ctx);

        JsRTNumberAllocateAction* allocEvent = this->m_eventSlabAllocator.SlabNew<JsRTNumberAllocateAction>(etime, ctxTag, false, 0, dval);

        this->InsertEventAtHead(allocEvent);
    }

    void EventLog::RecordJsRTAllocateString(Js::ScriptContext* ctx, LPCWSTR stringValue, uint32 stringLength)
    {
        uint64 etime = this->GetCurrentEventTimeAndAdvance();
        TTD_LOG_TAG ctxTag = TTD_EXTRACT_CTX_LOG_TAG(ctx);

        TTString str;
        this->m_eventSlabAllocator.CopyStringIntoWLength(stringValue, stringLength, str);
        JsRTStringAllocateAction* allocEvent = this->m_eventSlabAllocator.SlabNew<JsRTStringAllocateAction>(etime, ctxTag, str);

        this->InsertEventAtHead(allocEvent);
    }

    void EventLog::RecordJsRTAllocateSymbol(Js::ScriptContext* ctx, Js::Var symbolDescription)
    {
        uint64 etime = this->GetCurrentEventTimeAndAdvance();
        TTD_LOG_TAG ctxTag = TTD_EXTRACT_CTX_LOG_TAG(ctx);

        NSLogValue::ArgRetValue symDescriptor;
        NSLogValue::ExtractArgRetValueFromVar(symbolDescription, symDescriptor);

        JsRTSymbolAllocateAction* allocEvent = this->m_eventSlabAllocator.SlabNew<JsRTSymbolAllocateAction>(etime, ctxTag, symDescriptor);

        this->InsertEventAtHead(allocEvent);
    }

    void EventLog::RecordJsRTVarConversion(Js::ScriptContext* ctx, Js::Var var, bool toBool, bool toNumber, bool toString, bool toObject)
    {
        uint64 etime = this->GetCurrentEventTimeAndAdvance();
        TTD_LOG_TAG ctxTag = TTD_EXTRACT_CTX_LOG_TAG(ctx);

        NSLogValue::ArgRetValue vval;
        NSLogValue::ExtractArgRetValueFromVar(var, vval);

        JsRTVarConvertAction* convertEvent = this->m_eventSlabAllocator.SlabNew<JsRTVarConvertAction>(etime, ctxTag, toBool, toNumber, toString, toObject, vval);

        this->InsertEventAtHead(convertEvent);
    }

    void EventLog::RecordJsRTAllocateBasicObject(Js::ScriptContext* ctx, bool isRegularObject)
    {
        uint64 etime = this->GetCurrentEventTimeAndAdvance();
        TTD_LOG_TAG ctxTag = TTD_EXTRACT_CTX_LOG_TAG(ctx);

        JsRTObjectAllocateAction* allocEvent = this->m_eventSlabAllocator.SlabNew<JsRTObjectAllocateAction>(etime, ctxTag, isRegularObject);

        this->InsertEventAtHead(allocEvent);
    }

    void EventLog::RecordJsRTAllocateBasicClearArray(Js::ScriptContext* ctx, Js::TypeId arrayType, uint32 length)
    {
        uint64 etime = this->GetCurrentEventTimeAndAdvance();
        TTD_LOG_TAG ctxTag = TTD_EXTRACT_CTX_LOG_TAG(ctx);

        JsRTArrayAllocateAction* allocEvent = this->m_eventSlabAllocator.SlabNew<JsRTArrayAllocateAction>(etime, ctxTag, arrayType, length);

        this->InsertEventAtHead(allocEvent);
    }

    void EventLog::RecordJsRTAllocateArrayBuffer(Js::ScriptContext* ctx, byte* buff, uint32 size)
    {
        uint64 etime = this->GetCurrentEventTimeAndAdvance();
        TTD_LOG_TAG ctxTag = TTD_EXTRACT_CTX_LOG_TAG(ctx);

        byte* abuff = this->m_eventSlabAllocator.SlabAllocateArray<byte>(size);
        js_memcpy_s(abuff, size, buff, size);

        JsRTArrayBufferAllocateAction* allocEvent = this->m_eventSlabAllocator.SlabNew<JsRTArrayBufferAllocateAction>(etime, ctxTag, size, buff);

        this->InsertEventAtHead(allocEvent);
    }

    void EventLog::RecordJsRTAllocateFunction(Js::ScriptContext* ctx, bool isNamed, Js::Var optName)
    {
        uint64 etime = this->GetCurrentEventTimeAndAdvance();
        TTD_LOG_TAG ctxTag = TTD_EXTRACT_CTX_LOG_TAG(ctx);

        NSLogValue::ArgRetValue name;
        NSLogValue::InitializeArgRetValueAsInvalid(name);

        if(isNamed)
        {
            NSLogValue::ExtractArgRetValueFromVar(optName, name);
        }

        JsRTFunctionAllocateAction* allocEvent = this->m_eventSlabAllocator.SlabNew<JsRTFunctionAllocateAction>(etime, ctxTag, isNamed, name);

        this->InsertEventAtHead(allocEvent);
    }

    void EventLog::RecordJsRTGetAndClearException(Js::ScriptContext* ctx)
    {
        uint64 etime = this->GetCurrentEventTimeAndAdvance();
        TTD_LOG_TAG ctxTag = TTD_EXTRACT_CTX_LOG_TAG(ctx);

        JsRTGetAndClearExceptionAction* exceptionEvent = this->m_eventSlabAllocator.SlabNew<JsRTGetAndClearExceptionAction>(etime, ctxTag);

        this->InsertEventAtHead(exceptionEvent);
    }

    void EventLog::RecordJsRTGetProperty(Js::ScriptContext* ctx, Js::PropertyId pid, Js::Var var)
    {
        uint64 etime = this->GetCurrentEventTimeAndAdvance();
        TTD_LOG_TAG ctxTag = TTD_EXTRACT_CTX_LOG_TAG(ctx);

        NSLogValue::ArgRetValue val;
        NSLogValue::ExtractArgRetValueFromVar(var, val);

        JsRTGetPropertyAction* getEvent = this->m_eventSlabAllocator.SlabNew<JsRTGetPropertyAction>(etime, ctxTag, pid, val);

        this->InsertEventAtHead(getEvent);
    }

    void EventLog::RecordJsRTGetIndex(Js::ScriptContext* ctx, Js::Var index, Js::Var var)
    {
        uint64 etime = this->GetCurrentEventTimeAndAdvance();
        TTD_LOG_TAG ctxTag = TTD_EXTRACT_CTX_LOG_TAG(ctx);

        NSLogValue::ArgRetValue aindex;
        NSLogValue::ExtractArgRetValueFromVar(index, aindex);

        NSLogValue::ArgRetValue aval;
        NSLogValue::ExtractArgRetValueFromVar(var, aval);

        JsRTGetIndexAction* getEvent = this->m_eventSlabAllocator.SlabNew<JsRTGetIndexAction>(etime, ctxTag, aindex, aval);

        this->InsertEventAtHead(getEvent);
    }

    void EventLog::RecordJsRTGetOwnPropertyInfo(Js::ScriptContext* ctx, Js::PropertyId pid, Js::Var var)
    {
        uint64 etime = this->GetCurrentEventTimeAndAdvance();
        TTD_LOG_TAG ctxTag = TTD_EXTRACT_CTX_LOG_TAG(ctx);

        NSLogValue::ArgRetValue val;
        NSLogValue::ExtractArgRetValueFromVar(var, val);

        JsRTGetOwnPropertyInfoAction* getInfoEvent = this->m_eventSlabAllocator.SlabNew<JsRTGetOwnPropertyInfoAction>(etime, ctxTag, pid, val);

        this->InsertEventAtHead(getInfoEvent);
    }

    void EventLog::RecordJsRTGetOwnPropertiesInfo(Js::ScriptContext* ctx, bool isGetNames, Js::Var var)
    {
        uint64 etime = this->GetCurrentEventTimeAndAdvance();
        TTD_LOG_TAG ctxTag = TTD_EXTRACT_CTX_LOG_TAG(ctx);

        NSLogValue::ArgRetValue val;
        NSLogValue::ExtractArgRetValueFromVar(var, val);

        JsRTGetOwnPropertiesInfoAction* getInfoEvent = this->m_eventSlabAllocator.SlabNew<JsRTGetOwnPropertiesInfoAction>(etime, ctxTag, isGetNames, val);

        this->InsertEventAtHead(getInfoEvent);
    }

    void EventLog::RecordJsRTDefineProperty(Js::ScriptContext* ctx, Js::Var var, Js::PropertyId pid, Js::Var propertyDescriptor)
    {
        uint64 etime = this->GetCurrentEventTimeAndAdvance();
        TTD_LOG_TAG ctxTag = TTD_EXTRACT_CTX_LOG_TAG(ctx);

        NSLogValue::ArgRetValue avar;
        NSLogValue::ExtractArgRetValueFromVar(var, avar);

        NSLogValue::ArgRetValue pdval;
        NSLogValue::ExtractArgRetValueFromVar(propertyDescriptor, pdval);

        JsRTDefinePropertyAction* defineEvent = this->m_eventSlabAllocator.SlabNew<JsRTDefinePropertyAction>(etime, ctxTag, avar, pid, pdval);

        this->InsertEventAtHead(defineEvent);
    }

    void EventLog::RecordJsRTDeleteProperty(Js::ScriptContext* ctx, Js::Var var, Js::PropertyId pid, bool useStrictRules)
    {
        uint64 etime = this->GetCurrentEventTimeAndAdvance();
        TTD_LOG_TAG ctxTag = TTD_EXTRACT_CTX_LOG_TAG(ctx);

        NSLogValue::ArgRetValue avar;
        NSLogValue::ExtractArgRetValueFromVar(var, avar);

        JsRTDeletePropertyAction* deleteEvent = this->m_eventSlabAllocator.SlabNew<JsRTDeletePropertyAction>(etime, ctxTag, avar, pid, useStrictRules);

        this->InsertEventAtHead(deleteEvent);
    }

    void EventLog::RecordJsRTSetPrototype(Js::ScriptContext* ctx, Js::Var var, Js::Var proto)
    {
        uint64 etime = this->GetCurrentEventTimeAndAdvance();
        TTD_LOG_TAG ctxTag = TTD_EXTRACT_CTX_LOG_TAG(ctx);

        NSLogValue::ArgRetValue avar;
        NSLogValue::ExtractArgRetValueFromVar(var, avar);

        NSLogValue::ArgRetValue aproto;
        NSLogValue::ExtractArgRetValueFromVar(proto, aproto);

        JsRTSetPrototypeAction* setEvent = this->m_eventSlabAllocator.SlabNew<JsRTSetPrototypeAction>(etime, ctxTag, avar, aproto);

        this->InsertEventAtHead(setEvent);
    }

    void EventLog::RecordJsRTSetProperty(Js::ScriptContext* ctx, Js::Var var, Js::PropertyId pid, Js::Var val, bool useStrictRules)
    {
        uint64 etime = this->GetCurrentEventTimeAndAdvance();
        TTD_LOG_TAG ctxTag = TTD_EXTRACT_CTX_LOG_TAG(ctx);

        NSLogValue::ArgRetValue avar;
        NSLogValue::ExtractArgRetValueFromVar(var, avar);

        NSLogValue::ArgRetValue aval;
        NSLogValue::ExtractArgRetValueFromVar(val, aval);

        JsRTSetPropertyAction* setEvent = this->m_eventSlabAllocator.SlabNew<JsRTSetPropertyAction>(etime, ctxTag, avar, pid, aval, useStrictRules);

        this->InsertEventAtHead(setEvent);
    }

    void EventLog::RecordJsRTSetIndex(Js::ScriptContext* ctx, Js::Var var, Js::Var index, Js::Var val)
    {
        uint64 etime = this->GetCurrentEventTimeAndAdvance();
        TTD_LOG_TAG ctxTag = TTD_EXTRACT_CTX_LOG_TAG(ctx);

        NSLogValue::ArgRetValue avar;
        NSLogValue::ExtractArgRetValueFromVar(var, avar);

        NSLogValue::ArgRetValue aindex;
        NSLogValue::ExtractArgRetValueFromVar(index, aindex);

        NSLogValue::ArgRetValue aval;
        NSLogValue::ExtractArgRetValueFromVar(val, aval);

        JsRTSetIndexAction* setEvent = this->m_eventSlabAllocator.SlabNew<JsRTSetIndexAction>(etime, ctxTag, avar, aindex, aval);

        this->InsertEventAtHead(setEvent);
    }

    void EventLog::RecordJsRTGetTypedArrayInfo(Js::ScriptContext* ctx, bool returnsArrayBuff, Js::Var var)
    {
        uint64 etime = this->GetCurrentEventTimeAndAdvance();
        TTD_LOG_TAG ctxTag = TTD_EXTRACT_CTX_LOG_TAG(ctx);

        NSLogValue::ArgRetValue avar;
        NSLogValue::ExtractArgRetValueFromVar(var, avar);

        JsRTGetTypedArrayInfoAction* infoEvent = this->m_eventSlabAllocator.SlabNew<JsRTGetTypedArrayInfoAction>(etime, ctxTag, returnsArrayBuff, avar);

        this->InsertEventAtHead(infoEvent);
    }

    void EventLog::RecordJsRTConstructCall(Js::ScriptContext* ctx, Js::JavascriptFunction* func, uint32 argCount, Js::Var* args)
    {
        uint64 etime = this->GetCurrentEventTimeAndAdvance();
        TTD_LOG_TAG ctxTag = TTD_EXTRACT_CTX_LOG_TAG(ctx);
        TTD_LOG_TAG fTag = ctx->GetThreadContext()->TTDInfo->LookupTagForObject(func);

        NSLogValue::ArgRetValue* argArray = (argCount != 0) ? this->m_eventSlabAllocator.SlabAllocateArray<NSLogValue::ArgRetValue>(argCount) : 0;
        for(uint32 i = 0; i < argCount; ++i)
        {
            Js::Var arg = args[i];
            NSLogValue::ExtractArgRetValueFromVar(arg, argArray[i]);
        }
        Js::Var* execArgs = (argCount != 0) ? this->m_eventSlabAllocator.SlabAllocateArray<Js::Var>(argCount) : nullptr;

        JsRTConstructCallAction* constructEvent = this->m_eventSlabAllocator.SlabNew<JsRTConstructCallAction>(etime, ctxTag, fTag, argCount, argArray, execArgs);

        this->InsertEventAtHead(constructEvent);
    }

    void EventLog::RecordJsRTCallbackOperation(Js::ScriptContext* ctx, bool isCancel, bool isRepeating, Js::JavascriptFunction* func, int64 createdCallbackId)
    {
        uint64 etime = this->GetCurrentEventTimeAndAdvance();
        TTD_LOG_TAG ctxTag = TTD_EXTRACT_CTX_LOG_TAG(ctx);
        TTD_LOG_TAG fTag = (func != nullptr) ? ctx->GetThreadContext()->TTDInfo->LookupTagForObject(func) : TTD_INVALID_LOG_TAG;

        JsRTCallbackAction* createAction = this->m_eventSlabAllocator.SlabNew<JsRTCallbackAction>(etime, ctxTag, isCancel, isRepeating, this->m_hostCallbackId, fTag, createdCallbackId);

        this->InsertEventAtHead(createAction);
    }

    void EventLog::RecordJsRTCodeParse(Js::ScriptContext* ctx, bool isExpression, Js::JavascriptFunction* func, LPCWSTR srcCode, LPCWSTR sourceUri)
    {
        uint64 etime = this->GetCurrentEventTimeAndAdvance();
        TTD_LOG_TAG ctxTag = TTD_EXTRACT_CTX_LOG_TAG(ctx);

        Js::FunctionBody* fb = JsSupport::ForceAndGetFunctionBody(func->GetFunctionBody());

        TTString optSrcUri;
        this->m_eventSlabAllocator.CopyNullTermStringInto(fb->GetSourceContextInfo()->url, optSrcUri);

        DWORD_PTR optDocumentID = fb->GetSourceContextId();

        TTString sourceCode;
        this->m_eventSlabAllocator.CopyNullTermStringInto(srcCode, sourceCode);

        TTString dir;
        this->m_eventSlabAllocator.CopyStringIntoWLength(this->m_logInfoRootDir.Contents, this->m_logInfoRootDir.Length, dir);

        TTString ssUri;
        this->m_eventSlabAllocator.CopyNullTermStringInto(sourceUri, ssUri);

        JsRTCodeParseAction* parseEvent = this->m_eventSlabAllocator.SlabNew<JsRTCodeParseAction>(etime, ctxTag, isExpression, sourceCode, optDocumentID, optSrcUri, dir, ssUri);

        this->InsertEventAtHead(parseEvent);
    }

    JsRTCallFunctionBeginAction* EventLog::RecordJsRTCallFunctionBegin(Js::ScriptContext* ctx, int32 rootDepth, int64 hostCallbackId, double beginTime, Js::JavascriptFunction* func, uint32 argCount, Js::Var* args)
    {
        uint64 etime = this->GetCurrentEventTimeAndAdvance();
        TTD_LOG_TAG ctxTag = TTD_EXTRACT_CTX_LOG_TAG(ctx);
        TTD_LOG_TAG fTag = ctx->GetThreadContext()->TTDInfo->LookupTagForObject(func);

        NSLogValue::ArgRetValue* argArray = (argCount != 0) ? this->m_eventSlabAllocator.SlabAllocateArray<NSLogValue::ArgRetValue>(argCount) : 0;
        for(uint32 i = 0; i < argCount; ++i)
        {
            Js::Var arg = args[i];
            NSLogValue::ExtractArgRetValueFromVar(arg, argArray[i]);
        }
        Js::Var* execArgs = (argCount != 0) ? this->m_eventSlabAllocator.SlabAllocateArray<Js::Var>(argCount) : nullptr;

        JsRTCallFunctionBeginAction* callEvent = this->m_eventSlabAllocator.SlabNew<JsRTCallFunctionBeginAction>(etime, ctxTag, rootDepth, hostCallbackId, beginTime, fTag, argCount, argArray, execArgs);

#if ENABLE_TTD_INTERNAL_DIAGNOSTICS
        TTString fname;
        Js::JavascriptString* dname = func->GetDisplayName();
        this->m_eventSlabAllocator.CopyStringIntoWLength(dname->GetSz(), dname->GetLength(), fname);
        callEvent->SetFunctionName(fname);
#endif

        this->InsertEventAtHead(callEvent);

        return callEvent;
    }

    void EventLog::RecordJsRTCallFunctionEnd(Js::ScriptContext* ctx, int64 matchingBeginTime, bool hasScriptException, bool hasTerminatingException, int32 callbackDepth, double endTime)
    {
        AssertMsg(this->ShouldPerformRecordAction(), "Shouldn't be logging during replay!");

        uint64 etime = this->GetCurrentEventTimeAndAdvance();
        TTD_LOG_TAG ctxTag = TTD_EXTRACT_CTX_LOG_TAG(ctx);

        JsRTCallFunctionEndAction* callEvent = this->m_eventSlabAllocator.SlabNew<JsRTCallFunctionEndAction>(etime, ctxTag, matchingBeginTime, hasScriptException, hasTerminatingException, callbackDepth, endTime);

        this->InsertEventAtHead(callEvent);
    }

    void EventLog::ReplayActionLoopStep()
    {
        AssertMsg(this->ShouldPerformDebugAction(), "Mode is inconsistent!");
        AssertMsg(this->m_currentReplayEventIterator.IsValid() && this->m_currentReplayEventIterator.Current()->GetEventKind() == EventLogEntry::EventKind::JsRTActionTag, "Should check this first!");

        bool nextActionValid = false;
        bool nextActionRootCall = false;
        do
        {
            JsRTActionLogEntry* action = JsRTActionLogEntry::As(this->m_currentReplayEventIterator.Current());
            this->AdvanceTimeAndPositionForReplay();

            Js::ScriptContext* ctx = action->GetScriptContextForAction(this->m_threadContext);
            if(action->IsExecutedInScriptWrapper())
            {
                BEGIN_ENTER_SCRIPT(ctx, true, true, true);
                {
                    action->ExecuteAction(this->m_threadContext);
                }
                END_ENTER_SCRIPT;
            }
            else
            {
                action->ExecuteAction(this->m_threadContext);
            }

            nextActionValid = (this->m_currentReplayEventIterator.IsValid() && this->m_currentReplayEventIterator.Current()->GetEventKind() == EventLogEntry::EventKind::JsRTActionTag);
            nextActionRootCall = (nextActionValid && JsRTActionLogEntry::As(this->m_currentReplayEventIterator.Current())->IsRootCallBegin());

        } while(nextActionValid & !nextActionRootCall);
    }

    void EventLog::EmitLog()
    {
#if TTD_WRITE_JSON_OUTPUT || TTD_WRITE_BINARY_OUTPUT

        HANDLE logHandle = this->m_threadContext->TTDStreamFunctions.pfGetLogStream(this->m_logInfoRootDir.Contents, false, true);
        JSONWriter writer(logHandle, this->m_threadContext->TTDStreamFunctions.pfWriteBytesToStream, this->m_threadContext->TTDStreamFunctions.pfFlushAndCloseStream);

        writer.WriteRecordStart();
        writer.AdjustIndent(1);

        TTString archString;
#if defined(_M_IX86)
        this->m_miscSlabAllocator(L"x86", archString);
#elif defined(_M_X64)
        this->m_miscSlabAllocator.CopyNullTermStringInto(L"x64", archString);
#elif defined(_M_ARM)
        this->m_miscSlabAllocator(L"arm64", archString);
#else
        this->m_miscSlabAllocator(L"unknown", archString);
#endif

        writer.WriteString(NSTokens::Key::arch, archString);

#if ENABLE_TTD_INTERNAL_DIAGNOSTICS
        bool diagEnabled = true;
#else
        bool diagEnabled = false;
#endif

        writer.WriteBool(NSTokens::Key::diagEnabled, diagEnabled, NSTokens::Separator::CommaSeparator);

        uint64 usedSpace = 0;
        uint64 reservedSpace = 0;
        this->m_eventSlabAllocator.ComputeMemoryUsed(&usedSpace, &reservedSpace);

        writer.WriteUInt64(NSTokens::Key::usedMemory, usedSpace, NSTokens::Separator::CommaSeparator);
        writer.WriteUInt64(NSTokens::Key::reservedMemory, reservedSpace, NSTokens::Separator::CommaSeparator);

        uint32 ecount = this->m_eventList.Count();
        writer.WriteLengthValue(ecount, NSTokens::Separator::CommaAndBigSpaceSeparator);

        bool firstEvent = true;
        writer.WriteSequenceStart_DefaultKey(NSTokens::Separator::CommaSeparator);
        writer.AdjustIndent(1);
        for(auto iter = this->m_eventList.GetIteratorAtFirst(); iter.IsValid(); iter.MoveNext())
        {
            bool isJsRTEndCall = (iter.Current()->GetEventKind() == EventLogEntry::EventKind::JsRTActionTag && JsRTActionLogEntry::As(iter.Current())->GetActionTypeTag() == JsRTActionType::CallExistingFunctionEnd);
            bool isExternalEndCall = (iter.Current()->GetEventKind() == EventLogEntry::EventKind::ExternalCallEndTag);
            if(isJsRTEndCall | isExternalEndCall)
            {
                writer.AdjustIndent(-1);
            }

            iter.Current()->EmitEvent(this->m_logInfoRootDir.Contents, &writer, this->m_threadContext, !firstEvent ? NSTokens::Separator::CommaAndBigSpaceSeparator : NSTokens::Separator::BigSpaceSeparator);
            firstEvent = false;

            bool isJsRTBeginCall = (iter.Current()->GetEventKind() == EventLogEntry::EventKind::JsRTActionTag && JsRTActionLogEntry::As(iter.Current())->GetActionTypeTag() == JsRTActionType::CallExistingFunctionBegin);
            bool isExternalBeginCall = (iter.Current()->GetEventKind() == EventLogEntry::EventKind::ExternalCallBeginTag);
            if(isJsRTBeginCall | isExternalBeginCall)
            {
                writer.AdjustIndent(1);
            }
        }
        writer.AdjustIndent(-1);
        writer.WriteSequenceEnd(NSTokens::Separator::BigSpaceSeparator);

        //we haven't moved the properties to their serialized form them take care of it 
        AssertMsg(this->m_propertyRecordList.Count() == 0, "We only compute this when we are ready to emit.");

        for(auto iter = this->m_propertyRecordPinSet->GetIterator(); iter.IsValid(); iter.MoveNext())
        {
            Js::PropertyRecord* pRecord = static_cast<Js::PropertyRecord*>(iter.CurrentValue());
            NSSnapType::SnapPropertyRecord* sRecord = this->m_propertyRecordList.NextOpenEntry();

            sRecord->PropertyId = pRecord->GetPropertyId();
            sRecord->IsNumeric = pRecord->IsNumeric();
            sRecord->IsBound = pRecord->IsBound();
            sRecord->IsSymbol = pRecord->IsSymbol();

            this->m_miscSlabAllocator.CopyStringIntoWLength(pRecord->GetBuffer(), pRecord->GetLength(), sRecord->PropertyName);
        }

        //emit the properties
        writer.WriteLengthValue(this->m_propertyRecordList.Count(), NSTokens::Separator::CommaSeparator);

        writer.WriteSequenceStart_DefaultKey(NSTokens::Separator::CommaSeparator);
        writer.AdjustIndent(1);
        bool firstProperty = true;
        for(auto iter = this->m_propertyRecordList.GetIterator(); iter.IsValid(); iter.MoveNext())
        {
            NSTokens::Separator sep = (!firstProperty) ? NSTokens::Separator::CommaAndBigSpaceSeparator : NSTokens::Separator::BigSpaceSeparator;
            NSSnapType::EmitSnapPropertyRecord(iter.Current(), &writer, sep);

            firstProperty = false;
        }
        writer.AdjustIndent(-1);
        writer.WriteSequenceEnd(NSTokens::Separator::BigSpaceSeparator);

        writer.AdjustIndent(-1);
        writer.WriteRecordEnd(NSTokens::Separator::BigSpaceSeparator);

        writer.FlushAndClose();

#endif
    }

    void EventLog::ParseLogInto()
    {
        HANDLE logHandle = this->m_threadContext->TTDStreamFunctions.pfGetLogStream(this->m_logInfoRootDir.Contents, true, false);
        JSONReader reader(logHandle, this->m_threadContext->TTDStreamFunctions.pfReadBytesFromStream, this->m_threadContext->TTDStreamFunctions.pfFlushAndCloseStream);

        reader.ReadRecordStart();

        TTString archString;
        reader.ReadString(NSTokens::Key::arch, this->m_miscSlabAllocator, archString);

#if defined(_M_IX86)
        AssertMsg(wcscmp(L"x86", archString.Contents) == 0, "Mismatch in arch between record and replay!!!");
#elif defined(_M_X64)
        AssertMsg(wcscmp(L"x64", archString.Contents) == 0, "Mismatch in arch between record and replay!!!");
#elif defined(_M_ARM)
        AssertMsg(wcscmp(L"arm64", archString.Contents) == 0, "Mismatch in arch between record and replay!!!");
#else
        AssertMsg(false, "Unknown arch!!!");
#endif

        bool diagEnabled = reader.ReadBool(NSTokens::Key::diagEnabled, true);

#if ENABLE_TTD_INTERNAL_DIAGNOSTICS
        AssertMsg(diagEnabled, "Diag was enabled in record so it shoud be in replay as well!!!");
#else
        AssertMsg(!diagEnabled, "Diag was *not* enabled in record so it shoud *not* be in replay either!!!");
#endif

        reader.ReadUInt64(NSTokens::Key::usedMemory, true);
        reader.ReadUInt64(NSTokens::Key::reservedMemory, true);

        uint32 ecount = reader.ReadLengthValue(true);
        reader.ReadSequenceStart_WDefaultKey(true);
        for(uint32 i = 0; i < ecount; ++i)
        {
            EventLogEntry* curr = EventLogEntry::Parse(i != 0, this->m_threadContext, &reader, this->m_eventSlabAllocator);

            this->m_eventList.AddEntry(curr);
        }
        reader.ReadSequenceEnd();

        //parse the properties
        uint32 propertyCount = reader.ReadLengthValue(true);
        reader.ReadSequenceStart_WDefaultKey(true);
        for(uint32 i = 0; i < propertyCount; ++i)
        {
            NSSnapType::SnapPropertyRecord* sRecord = this->m_propertyRecordList.NextOpenEntry();
            NSSnapType::ParseSnapPropertyRecord(sRecord, i != 0, &reader, this->m_miscSlabAllocator);
        }
        reader.ReadSequenceEnd();

        reader.ReadRecordEnd();
    }
}

#endif