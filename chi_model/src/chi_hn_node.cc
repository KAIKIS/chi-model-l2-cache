#include "chi_hn_node.hh"

namespace chi {

HnNode::HnNode(NodeID id)
    : ChiNode(id, NodeType::HN_F) {
}

void HnNode::process() {
    while (running_.load(std::memory_order_relaxed)) {
        auto reqOpt = rnChannel_.tryPopIn();
        if (reqOpt.has_value()) {
            processRequest(reqOpt.value());
        }

        auto respOpt = snChannel_.tryPopIn();
        if (respOpt.has_value()) {
            processResponse(respOpt.value());
        }
    }
}

void HnNode::stop() {
    running_.store(false, std::memory_order_relaxed);
}

ChiTransaction HnNode::transformRequest(const ChiTransaction& req) {
    ChiTransaction snReq = req;
    switch (req.opcode) {
        case Opcode::ReadShared:
        case Opcode::ReadUnique:
            snReq.opcode = Opcode::ReadNoSnp;
            break;
        case Opcode::WriteBackFull:
            snReq.opcode = Opcode::WriteNoSnp;
            break;
        default:
            break;
    }
    return snReq;
}

ChiTransaction HnNode::transformResponse(const ChiTransaction& resp) {
    return resp;
}

void HnNode::processRequest(const ChiTransaction& req) {
    if (req.opcode == Opcode::CleanUnique) {
        ChiTransaction comp;
        comp.txnID = req.txnID;
        comp.opcode = Opcode::Comp;
        comp.addr = req.addr;
        comp.srcNodeID = req.srcNodeID;
        comp.respStatus = RespStatus::OK;
        rnChannel_.pushOut(std::move(comp));
        return;
    }

    ChiTransaction snReq = transformRequest(req);
    snChannel_.pushOut(std::move(snReq));
}

void HnNode::processResponse(const ChiTransaction& resp) {
    ChiTransaction rnResp = transformResponse(resp);
    rnChannel_.pushOut(std::move(rnResp));
}

} // namespace chi
