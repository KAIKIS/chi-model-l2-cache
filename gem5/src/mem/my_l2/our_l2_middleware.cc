#include "mem/my_l2/our_l2_middleware.hh"

#include "base/cprintf.hh"
#include "base/trace.hh"
#include "mem/ruby/protocol/CHI/CHIRequestType.hh"
#include "mem/ruby/protocol/CHI/CHIResponseType.hh"
#include "mem/ruby/protocol/CHI/CHIDataType.hh"
#include "mem/ruby/system/RubySystem.hh"
#include "sim/sim_exit.hh"

namespace gem5 {
namespace ruby {

using namespace CHI;

OurL2Middleware::OurL2Middleware(const OurL2MiddlewareParams &p)
    : CHIGenericController(p),
      cacheStorage_(p.l2_num_sets, p.l2_assoc),
      requestCount(0)
{
    engine_ = std::make_unique<chi::ChiProtocolEngine>(&cacheStorage_, "L2");
    std::cout << "[OurL2] Middleware initialized with ChiProtocolEngine"
              << " machineID=" << m_machineID << std::endl;

    // Print L2 stats when simulation exits (even on panic).
    registerExitCallback([this]() { engine_->printStats(); });
}

OurL2Middleware::~OurL2Middleware() {}

void
OurL2Middleware::wakeup()
{
    // Delegate to parent: CHIGenericController::wakeup() processes all four
    // input ports (rspIn, datIn, snpIn, reqIn) via virtual recv* methods, and
    // reschedules only when work is pending. Consumer callbacks on input
    // buffers trigger wakeups when new messages arrive.
    CHIGenericController::wakeup();

    // Process deferred trigger actions (send retries)
    bool pending = processTriggerQueue();

    if (pending) {
        scheduleEvent(Cycles(1));
    }
}

bool
OurL2Middleware::trySendRequest(std::shared_ptr<CHIRequestMsg> msg)
{
    if (sendRequestMsg(msg)) return true;
    triggerQueue_.push({TriggerAction::RetrySendRequest, msg, nullptr, nullptr});
    return false;
}

bool
OurL2Middleware::trySendResponse(std::shared_ptr<CHIResponseMsg> msg)
{
    if (sendResponseMsg(msg)) return true;
    triggerQueue_.push({TriggerAction::RetrySendResponse, nullptr, msg, nullptr});
    return false;
}

bool
OurL2Middleware::trySendData(std::shared_ptr<CHIDataMsg> msg)
{
    if (sendDataMsg(msg)) return true;
    triggerQueue_.push({TriggerAction::RetrySendData, nullptr, nullptr, msg});
    return false;
}

bool
OurL2Middleware::trySendSnoop(std::shared_ptr<CHIRequestMsg> msg)
{
    if (sendSnoopMsg(msg)) return true;
    triggerQueue_.push({TriggerAction::RetrySendSnoop, msg, nullptr, nullptr});
    return false;
}

bool
OurL2Middleware::processTriggerQueue()
{
    bool processed = false;
    while (!triggerQueue_.empty()) {
        TriggerAction& ta = triggerQueue_.front();
        bool ok = false;
        switch (ta.type) {
          case TriggerAction::RetrySendRequest:
            ok = sendRequestMsg(ta.reqMsg);
            break;
          case TriggerAction::RetrySendResponse:
            ok = sendResponseMsg(ta.rspMsg);
            break;
          case TriggerAction::RetrySendData:
            ok = sendDataMsg(ta.datMsg);
            break;
          case TriggerAction::RetrySendSnoop:
            ok = sendSnoopMsg(ta.reqMsg);
            break;
        }
        if (ok) {
            triggerQueue_.pop();
            processed = true;
        } else {
            // Output buffer still full — keep in queue, stop processing
            return true;
        }
    }
    return processed;
}

// ---- Opcode mapping ----

chi::Opcode OurL2Middleware::gem5ToOpcode(CHIRequestType type)
{
    switch (type) {
      case CHIRequestType_ReadShared:
      case CHIRequestType_StashOnceShared:
      case CHIRequestType_StashOnceUnique:
        return chi::Opcode::ReadShared;
      case CHIRequestType_ReadUnique:
      case CHIRequestType_MakeReadUnique:
        return chi::Opcode::ReadUnique;
      case CHIRequestType_CleanUnique:
        return chi::Opcode::CleanUnique;
      case CHIRequestType_WriteBackFull:
      case CHIRequestType_WriteUniquePtl:
      case CHIRequestType_WriteUniqueZero:
      case CHIRequestType_WriteCleanFull:
        return chi::Opcode::WriteBackFull;
      case CHIRequestType_ReadNotSharedDirty:
        return chi::Opcode::ReadNotSharedDirty;
      case CHIRequestType_WriteUniqueFull:
        return chi::Opcode::WriteUniqueFull;
      case CHIRequestType_WriteEvictFull:
        return chi::Opcode::WriteEvictFull;
      default:
        panic("[OurL2] Unknown request type: %s",
              CHIRequestType_to_string(type));
    }
}

// ---- recvRequestMsg: translate + delegate to Engine ----

bool OurL2Middleware::recvRequestMsg(const CHIRequestMsg *msg)
{
    requestCount++;
    CHIRequestType inType = msg->gettype();
    Addr addr = msg->getaddr();

    cprintf("%s: [OurL2] Req #%d %s addr=%#x req=%s txnId=%d\n",
            name(), requestCount,
            CHIRequestType_to_string(inType).c_str(), addr,
            msg->getrequestor(), msg->gettxnId());

    // Evict: L1 evicts a clean line. Invalidate our copy and
    // send Comp_I to acknowledge completion.
    if (inType == CHIRequestType_Evict) {
        engine_->invalidate(addr);
        sendComp(msg->getrequestor(), addr, CHIResponseType_Comp_I,
                 msg->gettxnId());
        return true;
    }

    chi::ChiTransaction txn;
    txn.opcode    = gem5ToOpcode(inType);
    txn.addr      = addr;
    txn.size      = msg->getaccSize();
    txn.srcNodeID = static_cast<chi::NodeID>(msg->getrequestor().getNum());
    txn.txnID     = msg->gettxnId();

    auto actions = engine_->recvRequest(txn);
    executeActions(actions);
    return true;
}

// ---- recvDataMsg: delegate to Engine ----

bool OurL2Middleware::recvDataMsg(const CHIDataMsg *msg)
{
    Addr addr = msg->getaddr();
    chi::TxnID txnId = msg->gettxnId();

    cprintf("%s: [OurL2] Data %s addr=%#x txnId=%d\n",
            name(), CHIDataType_to_string(msg->gettype()).c_str(),
            addr, txnId);

    const uint8_t* data = msg->getdataBlk().getData(0, cacheLineSize);
    auto actions = engine_->recvData(txnId, addr, data);
    executeActions(actions);
    return true;
}

// ---- recvResponseMsg: delegate to Engine ----

bool OurL2Middleware::recvResponseMsg(const CHIResponseMsg *msg)
{
    CHIResponseType type = msg->gettype();
    chi::TxnID txnId = msg->gettxnId();
    Addr addr = msg->getaddr();

    cprintf("%s: [OurL2] Resp %s addr=%#x txnId=%d\n",
            name(), CHIResponseType_to_string(type).c_str(),
            addr, txnId);

    // CompAck from L1: consume silently (Engine doesn't track these)
    if (type == CHIResponseType_CompAck) {
        return true;
    }

    chi::RespStatus status = chi::RespStatus::OK;
    auto actions = engine_->recvResponse(txnId, status);
    executeActions(actions);
    return true;
}

// ---- recvSnoopMsg: no-op in single-HN-F config ----

bool OurL2Middleware::recvSnoopMsg(const CHIRequestMsg *msg)
{
    return true;
}

// ---- executeActions: translate ProtocolAction -> gem5 sends ----

void OurL2Middleware::executeActions(
    const std::vector<chi::ProtocolAction>& actions)
{
    for (const auto& a : actions) {
        switch (a.type) {
          case chi::ProtocolAction::SendReadNoSnp:
            sendReadNoSnp(a.addr, a.txnId);
            break;
          case chi::ProtocolAction::SendWriteNoSnp:
            sendWriteNoSnp(a.addr, a.data, a.txnId);
            break;
          case chi::ProtocolAction::SendCompData: {
            MachineID dest(MachineType_Cache, a.destNode);
            sendCompData(dest, a.addr, a.data, a.respState, a.txnId);
            break;
          }
          case chi::ProtocolAction::SendComp: {
            MachineID dest(MachineType_Cache, a.destNode);
            // Engine only uses SendComp for CleanUnique -> Comp_UC
            sendComp(dest, a.addr, CHIResponseType_Comp_UC, a.txnId);
            break;
          }
          case chi::ProtocolAction::SendCompDBIDResp: {
            MachineID dest(MachineType_Cache, a.destNode);
            sendCompDBIDResp(dest, a.addr, a.txnId);
            break;
          }
          case chi::ProtocolAction::SendSnpCleanInvalid: {
            MachineID target(MachineType_Cache, a.destNode);
            sendSnpCleanInvalid(a.addr, target, a.txnId, a.retToSrc);
            break;
          }
          case chi::ProtocolAction::SendSnpUnique: {
            MachineID target(MachineType_Cache, a.destNode);
            sendSnpUnique(a.addr, target, a.txnId, a.retToSrc);
            break;
          }
          case chi::ProtocolAction::SendSnpNotSharedDirty: {
            MachineID target(MachineType_Cache, a.destNode);
            sendSnpNotSharedDirty(a.addr, target, a.txnId, a.retToSrc);
            break;
          }
        }
    }
}

// --- Helper: send CompData to L1 ---
// CHI protocol requires data to be sent in beats of dataChannelSize bytes.
// The L1 cache asserts bitMask.count() <= data_channel_size on each message.
void OurL2Middleware::sendCompData(
    const MachineID& dest, Addr addr,
    const uint8_t* data, chi::LineState state, chi::TxnID txnId)
{
    NetDest destNet(m_ruby_system);
    destNet.add(dest);

    CHIDataType dataType;
    switch (state) {
      case chi::LineState::SC: dataType = CHIDataType_CompData_SC; break;
      case chi::LineState::UC: dataType = CHIDataType_CompData_UC; break;
      case chi::LineState::UD: dataType = CHIDataType_CompData_UD_PD; break;
      default: dataType = CHIDataType_CompData_SC; break;
    }

    // Send data in dataMsgsPerLine beats
    for (int beat = 0; beat < dataMsgsPerLine; beat++) {
        int offset = beat * dataChannelSize;
        auto dat = std::make_shared<CHIDataMsg>(
            curTick(), cacheLineSize, m_ruby_system);
        dat->setaddr(addr);
        dat->settype(dataType);
        dat->setresponder(m_machineID);
        dat->setDestination(destNet);

        dat->m_dataBlk.setData(data, 0, cacheLineSize);
        WriteMask bitmask(cacheLineSize);
        bitmask.setMask(offset, dataChannelSize);
        dat->setbitMask(bitmask);

        dat->setusesTxnId(false);
        dat->settxnId(txnId);

        trySendData(dat);
    }
}

// --- Helper: send Comp to L1 ---
void OurL2Middleware::sendComp(
    const MachineID& dest, Addr addr,
    CHIResponseType compType, chi::TxnID txnId)
{
    auto rsp = std::make_shared<CHIResponseMsg>(
        curTick(), cacheLineSize, m_ruby_system);
    rsp->setaddr(addr);
    rsp->settype(compType);
    rsp->setresponder(m_machineID);
    NetDest destNet(m_ruby_system);
    destNet.add(dest);
    rsp->setDestination(destNet);
    rsp->setusesTxnId(false);
    rsp->settxnId(txnId);
    trySendResponse(rsp);
}

// --- Helper: send ReadNoSnp to memory (non-DMT) ---
void OurL2Middleware::sendReadNoSnp(Addr addr, chi::TxnID txnId)
{
    auto req = std::make_shared<CHIRequestMsg>(
        curTick(), cacheLineSize, m_ruby_system);
    req->setaddr(addr);
    req->settype(CHIRequestType_ReadNoSnp);
    req->setrequestor(m_machineID);
    req->setfwdRequestor(m_machineID);       // data comes back to us
    req->setdataToFwdRequestor(false);        // non-DMT
    req->setallowRetry(true);
    req->setDestination(allDownstreamDest());
    req->setusesTxnId(false);
    req->settxnId(txnId);
    req->setaccAddr(addr);
    req->setaccSize(cacheLineSize);
    trySendRequest(req);
}

// --- Helper: send WriteNoSnp to memory ---
void OurL2Middleware::sendWriteNoSnp(Addr addr, const uint8_t* data, chi::TxnID txnId)
{
    auto req = std::make_shared<CHIRequestMsg>(
        curTick(), cacheLineSize, m_ruby_system);
    req->setaddr(addr);
    req->settype(CHIRequestType_WriteNoSnp);
    req->setrequestor(m_machineID);
    req->setfwdRequestor(m_machineID);
    req->setdataToFwdRequestor(false);
    req->setallowRetry(true);
    req->setDestination(allDownstreamDest());
    req->setusesTxnId(false);
    req->settxnId(txnId);
    req->setaccAddr(addr);
    req->setaccSize(cacheLineSize);

    // WriteNoSnp also needs data - send via separate data message
    trySendRequest(req);

    // Send write data on data channel in dataMsgsPerLine beats
    for (int beat = 0; beat < dataMsgsPerLine; beat++) {
        int offset = beat * dataChannelSize;
        auto dat = std::make_shared<CHIDataMsg>(
            curTick(), cacheLineSize, m_ruby_system);
        dat->setaddr(addr);
        dat->settype(CHIDataType_NCBWrData);
        dat->setresponder(m_machineID);
        dat->setDestination(allDownstreamDest());
        dat->m_dataBlk.setData(data, 0, cacheLineSize);
        WriteMask bitmask(cacheLineSize);
        bitmask.setMask(offset, dataChannelSize);
        dat->setbitMask(bitmask);
        dat->setusesTxnId(false);
        dat->settxnId(txnId);
        trySendData(dat);
    }
}

// --- Helper: send CompDBIDResp to L1 ---
void OurL2Middleware::sendCompDBIDResp(
    const MachineID& dest, Addr addr, chi::TxnID txnId)
{
    auto rsp = std::make_shared<CHIResponseMsg>(
        curTick(), cacheLineSize, m_ruby_system);
    rsp->setaddr(addr);
    rsp->settype(CHIResponseType_CompDBIDResp);
    rsp->setresponder(m_machineID);
    NetDest destNet(m_ruby_system);
    destNet.add(dest);
    rsp->setDestination(destNet);
    rsp->setusesTxnId(false);
    rsp->settxnId(txnId);
    trySendResponse(rsp);
}

// --- Helper: send SnpCleanInvalid to RN-F ---
void OurL2Middleware::sendSnpCleanInvalid(
    Addr addr, const MachineID& target, chi::TxnID txnId, bool retToSrc)
{
    auto snp = std::make_shared<CHIRequestMsg>(
        curTick(), cacheLineSize, m_ruby_system);
    snp->setaddr(addr);
    snp->settype(CHIRequestType_SnpCleanInvalid);
    snp->setrequestor(m_machineID);
    NetDest dest(m_ruby_system);
    dest.add(target);
    snp->setDestination(dest);
    snp->setallowRetry(false);
    snp->setusesTxnId(false);
    snp->settxnId(txnId);
    snp->setretToSrc(retToSrc);
    snp->setaccAddr(addr);
    snp->setaccSize(cacheLineSize);
    trySendSnoop(snp);
}

// --- Helper: send SnpUnique to RN-F ---
void OurL2Middleware::sendSnpUnique(
    Addr addr, const MachineID& target, chi::TxnID txnId, bool retToSrc)
{
    auto snp = std::make_shared<CHIRequestMsg>(
        curTick(), cacheLineSize, m_ruby_system);
    snp->setaddr(addr);
    snp->settype(CHIRequestType_SnpUnique);
    snp->setrequestor(m_machineID);
    NetDest dest(m_ruby_system);
    dest.add(target);
    snp->setDestination(dest);
    snp->setallowRetry(false);
    snp->setusesTxnId(false);
    snp->settxnId(txnId);
    snp->setretToSrc(retToSrc);
    snp->setaccAddr(addr);
    snp->setaccSize(cacheLineSize);
    trySendSnoop(snp);
}

// --- Helper: send SnpNotSharedDirty to RN-F ---
void OurL2Middleware::sendSnpNotSharedDirty(
    Addr addr, const MachineID& target, chi::TxnID txnId, bool retToSrc)
{
    auto snp = std::make_shared<CHIRequestMsg>(
        curTick(), cacheLineSize, m_ruby_system);
    snp->setaddr(addr);
    snp->settype(CHIRequestType_SnpNotSharedDirtyFwd);
    snp->setrequestor(m_machineID);
    NetDest dest(m_ruby_system);
    dest.add(target);
    snp->setDestination(dest);
    snp->setallowRetry(false);
    snp->setusesTxnId(false);
    snp->settxnId(txnId);
    snp->setretToSrc(retToSrc);
    snp->setaccAddr(addr);
    snp->setaccSize(cacheLineSize);
    trySendSnoop(snp);
}

} // namespace ruby
} // namespace gem5
