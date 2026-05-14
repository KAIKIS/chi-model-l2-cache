#ifndef __MEM_MY_L2_OUR_L2_MIDDLEWARE_HH__
#define __MEM_MY_L2_OUR_L2_MIDDLEWARE_HH__

#include <memory>
#include <queue>
#include <vector>

#include "mem/ruby/protocol/chi/generic/CHIGenericController.hh"
#include "params/OurL2Middleware.hh"

#include "chi_protocol_engine.hh"
#include "chi_opcode.hh"

namespace gem5
{
namespace ruby
{

class OurL2Middleware : public CHIGenericController
{
  public:
    PARAMS(OurL2Middleware);
    OurL2Middleware(const Params &p);
    ~OurL2Middleware();

  protected:
    bool recvRequestMsg(const CHI::CHIRequestMsg *msg) override;
    bool recvSnoopMsg(const CHI::CHIRequestMsg *msg) override;
    bool recvResponseMsg(const CHI::CHIResponseMsg *msg) override;
    bool recvDataMsg(const CHI::CHIDataMsg *msg) override;

  public:
    void wakeup() override;

  private:
    chi::L2Cache cacheStorage_;
    std::unique_ptr<chi::ChiProtocolEngine> engine_;

    chi::Opcode gem5ToOpcode(CHI::CHIRequestType type);

    void executeActions(const std::vector<chi::ProtocolAction>& actions);

    // Trigger queue: deferred send retries when output buffers are full
    struct TriggerAction {
        enum Type { RetrySendRequest, RetrySendResponse, RetrySendData, RetrySendSnoop };
        Type type;
        std::shared_ptr<CHI::CHIRequestMsg>  reqMsg;
        std::shared_ptr<CHI::CHIResponseMsg> rspMsg;
        std::shared_ptr<CHI::CHIDataMsg>     datMsg;
    };
    std::queue<TriggerAction> triggerQueue_;
    bool processTriggerQueue();

    // Try to send; on failure enqueue a retry trigger.
    // Returns true if sent, false if queued for retry.
    bool trySendRequest(std::shared_ptr<CHI::CHIRequestMsg> msg);
    bool trySendResponse(std::shared_ptr<CHI::CHIResponseMsg> msg);
    bool trySendData(std::shared_ptr<CHI::CHIDataMsg> msg);
    bool trySendSnoop(std::shared_ptr<CHI::CHIRequestMsg> msg);

    // Send helpers (thin wrappers, no protocol logic)
    void sendReadNoSnp(Addr addr, chi::TxnID txnId);
    void sendWriteNoSnp(Addr addr, const uint8_t* data, chi::TxnID txnId);
    void sendComp(const MachineID& dest, Addr addr,
                  CHI::CHIResponseType compType, chi::TxnID txnId);
    void sendCompData(const MachineID& dest, Addr addr,
                      const uint8_t* data, chi::LineState state,
                      chi::TxnID txnId);
    void sendCompDBIDResp(const MachineID& dest, Addr addr, chi::TxnID txnId);
    void sendSnpCleanInvalid(Addr addr, const MachineID& target,
                             chi::TxnID txnId, bool retToSrc);
    void sendSnpUnique(Addr addr, const MachineID& target,
                       chi::TxnID txnId, bool retToSrc);
    void sendSnpNotSharedDirty(Addr addr, const MachineID& target,
                               chi::TxnID txnId, bool retToSrc);
    void sendSnpOnce(Addr addr, const MachineID& target,
                     chi::TxnID txnId, bool retToSrc);
    void sendSnpShared(Addr addr, const MachineID& target,
                       chi::TxnID txnId, bool retToSrc);

    uint64_t requestCount;
};

} // namespace ruby
} // namespace gem5

#endif // __MEM_MY_L2_OUR_L2_MIDDLEWARE_HH__
