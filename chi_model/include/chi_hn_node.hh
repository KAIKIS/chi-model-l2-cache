#pragma once

#include "chi_node.hh"
#include "chi_transaction.hh"
#include "chi_channel.hh"

namespace chi {

class HnNode : public ChiNode {
public:
    HnNode(NodeID id);

    Channel<ChiTransaction>& getRNChannel() { return rnChannel_; }
    const Channel<ChiTransaction>& getRNChannel() const { return rnChannel_; }

    Channel<ChiTransaction>& getSNChannel() { return snChannel_; }
    const Channel<ChiTransaction>& getSNChannel() const { return snChannel_; }

    void process() override;
    void stop();

protected:
    virtual ChiTransaction transformRequest(const ChiTransaction& req);
    virtual ChiTransaction transformResponse(const ChiTransaction& resp);

private:
    void processRequest(const ChiTransaction& req);
    void processResponse(const ChiTransaction& resp);

    Channel<ChiTransaction> rnChannel_;
    Channel<ChiTransaction> snChannel_;
    bool running_ = true;
};

} // namespace chi
