// Copyright (c) by respective owners including Yahoo!, Microsoft, and
// individual contributors. All rights reserved. Released under a BSD (revised)
// license as described in the file LICENSE.
#pragma once

#include "vw/common/future_compat.h"
#include "vw/core/vw_fwd.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace VW
{
using weight = float;
namespace details
{
constexpr uint64_t QUADRATIC_CONSTANT = 27942141;
constexpr uint64_t CUBIC_CONSTANT = 21791;
constexpr uint64_t CUBIC_CONSTANT2 = 37663;
constexpr uint64_t AFFIX_CONSTANT = 13903957;

// Index of the constant feature is always zero
constexpr uint64_t CONSTANT = 0;

constexpr float PROBABILITY_TOLERANCE = 1e-5f;

constexpr const char* DEFAULT_NAMESPACE_STR = " ";
constexpr const char* WILDCARD_NAMESPACE_STR = ":";

constexpr VW::namespace_index DEFAULT_NAMESPACE = 32;   // ' ' (space)
constexpr VW::namespace_index WILDCARD_NAMESPACE = 58;  // ':'
constexpr VW::namespace_index WAP_LDF_NAMESPACE = 126;
constexpr VW::namespace_index HISTORY_NAMESPACE = 127;
constexpr VW::namespace_index CONSTANT_NAMESPACE = 128;
constexpr VW::namespace_index NN_OUTPUT_NAMESPACE = 129;
constexpr VW::namespace_index AUTOLINK_NAMESPACE = 130;
constexpr VW::namespace_index NEIGHBOR_NAMESPACE =
    131;  // this is \x83 -- to do quadratic, say "-q a`printf "\x83"` on the command line
constexpr VW::namespace_index AFFIX_NAMESPACE = 132;                     // this is \x84
constexpr VW::namespace_index SPELLING_NAMESPACE = 133;                  // this is \x85
constexpr VW::namespace_index CONDITIONING_NAMESPACE = 134;              // this is \x86
constexpr VW::namespace_index DICTIONARY_NAMESPACE = 135;                // this is \x87
constexpr VW::namespace_index NODE_ID_NAMESPACE = 136;                   // this is \x88
constexpr VW::namespace_index BASELINE_ENABLED_MESSAGE_NAMESPACE = 137;  // this is \x89
constexpr VW::namespace_index CCB_SLOT_NAMESPACE = 139;
constexpr VW::namespace_index CCB_ID_NAMESPACE = 140;
constexpr VW::namespace_index IGL_FEEDBACK_NAMESPACE = 141;

constexpr std::array<VW::namespace_index, 17> SPECIAL_NAMESPACES {{
    DEFAULT_NAMESPACE,
    WILDCARD_NAMESPACE, WAP_LDF_NAMESPACE,
    HISTORY_NAMESPACE, CONSTANT_NAMESPACE,
    NN_OUTPUT_NAMESPACE, AUTOLINK_NAMESPACE,
    NEIGHBOR_NAMESPACE, AFFIX_NAMESPACE,
    SPELLING_NAMESPACE, CONDITIONING_NAMESPACE,
    DICTIONARY_NAMESPACE, NODE_ID_NAMESPACE,
    BASELINE_ENABLED_MESSAGE_NAMESPACE,
    CCB_SLOT_NAMESPACE, CCB_ID_NAMESPACE,
    IGL_FEEDBACK_NAMESPACE
}};

constexpr const char* CCB_LABEL = "ccb";
constexpr const char* SLATES_LABEL = "slates";
constexpr const char* SHARED_TYPE = "shared";
constexpr const char* ACTION_TYPE = "action";
constexpr const char* SLOT_TYPE = "slot";
constexpr const char* CA_LABEL = "ca";
constexpr const char* PDF = "pdf";
constexpr const char* CHOSEN_ACTION = "chosen_action";
// GRAPH_FEEDBACK_TYPE is an experimental type for an experimental reduction
constexpr const char* GRAPH_FEEDBACK_TYPE = "graph";

static constexpr uint32_t SHARED_EX_INDEX = 0;
static constexpr uint32_t TOP_ACTION_INDEX = 0;
static constexpr const int DEFAULT_FLOAT_PRECISION = 6;
static constexpr const int DEFAULT_FLOAT_FORMATTING_DECIMAL_PRECISION = 2;
static constexpr const int AS_MANY_AS_NEEDED_FLOAT_FORMATTING_DECIMAL_PRECISION = -1;

}  // namespace details
}  // namespace VW

using weight VW_DEPRECATED("weight renamed to VW::weight") = VW::weight;
