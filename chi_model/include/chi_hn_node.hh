#pragma once

#include "chi_node.hh"
#include "chi_transaction.hh"
#include "chi_channel.hh"

#include <atomic>

namespace chi {

class HnNode : public ChiNode {
public:
    HnNode(NodeID id);

    BidirectionalChannel<ChiTransaction>& getRNChannel() { return rnChannel_; }
    const BidirectionalChannel<ChiTransaction>& getRNChannel() const { return rnChannel_; }

    BidirectionalChannel<ChiTransaction>& getSNChannel() { return snChannel_; }
    const BidirectionalChannel<ChiTransaction>& getSNChannel() const { return snChannel_; }

    void process() override;
    void stop();

    virtual ChiTransaction transformRequest(const ChiTransaction& req);
    virtual ChiTransaction transformResponse(const ChiTransaction& resp);

private:
    void processRequest(const ChiTransaction& req);
    void processResponse(const ChiTransaction& resp);

    BidirectionalChannel<ChiTransaction> rnChannel_;
    BidirectionalChannel<ChiTransaction> snChannel_;
    std::atomic<bool> running_{true};
};

} // namespace chi
