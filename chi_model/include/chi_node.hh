#pragma once

#include "chi_types.hh"

namespace chi {

enum class NodeType : uint8_t {
    RN_F = 0,   // Request Node - Fully coherent
    HN_F = 1,   // Home Node - Fully coherent
    SN_F = 2,   // Slave Node - Fully coherent
};

class ChiNode {
public:
    ChiNode(NodeID id, NodeType type) : nodeID_(id), nodeType_(type) {}
    virtual ~ChiNode() = default;

    NodeID getNodeID() const { return nodeID_; }
    NodeType getNodeType() const { return nodeType_; }

    virtual void process() = 0;

protected:
    NodeID nodeID_;
    NodeType nodeType_;
};

} // namespace chi
