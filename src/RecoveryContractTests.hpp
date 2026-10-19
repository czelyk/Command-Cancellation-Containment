// Local research tests; not an upstream fix or a flight-qualified component.
#include "Svc/CmdDispatcher/CommandDispatcherImpl.hpp"

namespace Svc {

// This executable does not link CmdDispatcher's normal unit-test fixture.
// Use its declared test-friend name solely to step the real queued component.
class CommandDispatcherTester {
  public:
    static void pump(CommandDispatcherImpl& component) {
        ASSERT_EQ(component.doDispatch(), Fw::QueuedComponentBase::MSG_DISPATCH_OK);
    }
};

class DeferredEffectSink final : public Fw::PassiveComponentBase {
  public:
    DeferredEffectSink() : Fw::PassiveComponentBase("DeferredEffectSink") {
        this->input.init();
        this->input.setPortNum(0);
        this->input.addCallComp(this, &DeferredEffectSink::receive);
    }

    Fw::InputCmdPort input;
    U32 queued = 0;
    U32 starts = 0;
    U32 rejected = 0;
    U32 active = 0;
    U32 lastSequence = 0;
    bool revoked = false;

    void startPending(bool enforce) {
        ASSERT_GT(this->queued, 0U);
        this->queued--;
        if (enforce && this->revoked) {
            this->rejected++;
        } else {
            this->starts++;
            this->active++;
        }
    }
    void finish() {
        ASSERT_GT(this->active, 0U);
        this->active--;
    }
    bool contained() const { return this->revoked && this->active == 0; }

  private:
    static void receive(Fw::PassiveComponentBase* comp, FwIndexType portNum,
                        FwOpcodeType opcode, U32 seq, Fw::CmdArgBuffer& args) {
        auto* self = static_cast<DeferredEffectSink*>(comp);
        EXPECT_EQ(opcode, static_cast<FwOpcodeType>(0xDEADBEEF));
        EXPECT_EQ(args.getSize(), static_cast<FwSizeType>(4));
        EXPECT_LT(self->queued, 8U);
        self->queued++;
        self->lastSequence = seq;
    }
};

class RecoveryBoundaryTester : public WasmSequencerTester {
  public:
    RecoveryBoundaryTester() : WasmSequencerTester(), dispatcher("RecoveryDispatcher") {
        this->dispatcher.init(20, 0);
        this->dispatcher.configure();
        this->connectRecoveryDispatcher(*this->dispatcher.get_seqCmdBuff_InputPort(0));
        this->dispatcher.set_seqCmdStatus_OutputPort(0, this->recoveryResponsePort());
        this->dispatcher.set_compCmdSend_OutputPort(0, &this->sink.input);
        this->dispatcher.get_compCmdReg_InputPort(0)->invoke(static_cast<FwOpcodeType>(0xDEADBEEF));
        this->clearHistory();
    }
    ~RecoveryBoundaryTester() override { this->dispatcher.deinit(); }

  protected:
    void runToPending(const Fw::String& file, U32 request) {
        this->sendCmd_RUN(0, request, file, Svc::BlockState::BLOCK);
        this->dispatchUntilInterpreterState(InterpreterState::RUNNING_AWAITING_RESPONSE_WAITING);
    }
    void cancelToIdle(U32 request) {
        this->sendCmd_CANCEL(0, request);
        this->dispatchUntilControllerState(ControllerState::IDLE);
        ASSERT_EQ(this->controllerState(), ControllerState::IDLE);
        ASSERT_EQ(this->getPendingHostFunctionKind(), WasmSequencer_HostFunction::NONE);
    }
    void dispatchDownstream() {
        CommandDispatcherTester::pump(this->dispatcher);
    }
    void respondFromSink(U32 sequence) {
        this->dispatcher.get_compCmdStat_InputPort(0)->invoke(
            static_cast<FwOpcodeType>(0xDEADBEEF), sequence, Fw::CmdResponse::OK);
        this->dispatchDownstream();
    }
    DeferredEffectSink sink;
    CommandDispatcherImpl dispatcher;
};

TEST_F(RecoveryBoundaryTester, DispatcherQueuedCommandStartsAfterSequencerCancellation) {
    COMMENT("A cancelled guest has no authority to retract the real dispatcher's queued command.");
    StagedAsset file(*this, "cmd.wasm");
    this->runToPending(file.file(), 500);
    this->cancelToIdle(501);
    ASSERT_EQ(this->sink.queued, 0U);
    this->dispatchDownstream();
    this->sink.startPending(false);
    ASSERT_EQ(this->sink.starts, 1U);
    this->sink.finish();
}

TEST_F(RecoveryBoundaryTester, ReceiverQueuedCommandStartsAfterSequencerCancellation) {
    COMMENT("Retraction at a sequencer cannot remove a command in a downstream handler queue.");
    StagedAsset file(*this, "cmd.wasm");
    this->runToPending(file.file(), 510);
    this->dispatchDownstream();
    this->cancelToIdle(511);
    this->sink.startPending(false);
    ASSERT_EQ(this->sink.starts, 1U);
    this->sink.finish();
}

TEST_F(RecoveryBoundaryTester, FinalGateRejectsPreviouslyQueuedCommand) {
    COMMENT("The downstream effect stub enforces revocation at initiation.");
    StagedAsset file(*this, "cmd.wasm");
    this->runToPending(file.file(), 520);
    this->dispatchDownstream();
    this->cancelToIdle(521);
    this->sink.revoked = true;
    this->sink.startPending(true);
    ASSERT_EQ(this->sink.starts, 0U);
    ASSERT_EQ(this->sink.rejected, 1U);
    ASSERT_TRUE(this->sink.contained());
}

TEST_F(RecoveryBoundaryTester, InFlightEffectPreventsQuiescentAcknowledgement) {
    COMMENT("Revocation does not undo an effect that has already started.");
    StagedAsset file(*this, "cmd.wasm");
    this->runToPending(file.file(), 530);
    this->dispatchDownstream();
    this->sink.startPending(false);
    this->cancelToIdle(531);
    this->sink.revoked = true;
    ASSERT_FALSE(this->sink.contained());
    this->sink.finish();
    ASSERT_TRUE(this->sink.contained());
}

TEST_F(RecoveryBoundaryTester, RepeatedCancelledRunsAccumulateResidualCommands) {
    COMMENT("Independent sequence generations can leave multiple downstream commands pending.");
    StagedAsset file(*this, "cmd.wasm");
    for (U32 i = 0; i < 4; ++i) {
        this->runToPending(file.file(), 540 + 2 * i);
        this->cancelToIdle(541 + 2 * i);
    }
    for (U32 i = 0; i < 4; ++i) {
        this->dispatchDownstream();
        this->sink.startPending(false);
        this->sink.finish();
    }
    ASSERT_EQ(this->sink.starts, 4U);
}

TEST_F(RecoveryBoundaryTester, OldTrackedReplyAliasesAfterFullCounterCycle) {
    COMMENT("No counter manipulation: retain one old dispatcher entry across 65536 subsequent runs.");
    StagedAsset file(*this, "cmd.wasm");
    this->runToPending(file.file(), 600);
    const U32 oldContext = this->currentCmdUid();
    this->dispatchDownstream();
    const U32 oldSequence = this->sink.lastSequence;
    // The stub holds this operation outside its ready queue, retaining the real
    // dispatcher's tracking entry until the experiment supplies its completion.
    this->sink.queued--;
    this->cancelToIdle(601);

    for (U32 i = 0; i < 65535; ++i) {
        this->clearHistory();
        this->runToPending(file.file(), 602);
        this->dispatchDownstream();
        const U32 currentSequence = this->sink.lastSequence;
        this->sink.startPending(false);
        this->sink.finish();
        this->respondFromSink(currentSequence);
        this->dispatchUntilControllerState(ControllerState::READY);
    }

    this->clearHistory();
    this->runToPending(file.file(), 603);
    const U32 currentContext = this->currentCmdUid();
    ASSERT_EQ(oldContext, currentContext);
    this->dispatchDownstream();
    const U32 newSequence = this->sink.lastSequence;
    ASSERT_NE(oldSequence, newSequence);
    const U32 startsBeforeReply = this->sink.starts;
    this->respondFromSink(oldSequence);
    this->dispatchUntilControllerState(ControllerState::READY);
    // The current guest completed without its own queued command starting.
    ASSERT_EQ(this->sink.starts, startsBeforeReply);
    ASSERT_EQ(this->sink.queued, 1U);
    ASSERT_EVENTS_CmdResponseFromOldSequence_SIZE(0);
    printf("ALIAS_WITNESS total_runs=65537 old_dispatcher_seq=%u new_dispatcher_seq=%u context=%u current_effect_started=0 guest_ready=1\n",
           oldSequence, newSequence, currentContext);
}

}  // namespace Svc
