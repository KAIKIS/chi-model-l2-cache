#pragma once

#include "chi_hn_node.hh"

namespace chi {

// 直通模式 L2 Cache，继承 HnNode
// Phase 1：无缓存状态，所有请求直接转发
class SimpleL2Cache : public HnNode {
public:
    SimpleL2Cache(NodeID id);

    // 直通模式：不覆盖 transformRequest/transformResponse
    // 使用 HnNode 的默认 opcode 转换逻辑
};

} // namespace chi
