/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <gtest/gtest.h>

#include "artillery/if/gen-cpp2/artillery_server_block_constants.h"
#include "contextprop/cpp/serde/SerDeHelper.h"
#include "mcrouter/lib/network/ThriftTransport.h"

namespace facebook::memcache {
namespace {

using contextprop::ArtilleryTraceIDs;
using contextprop::SerDeHelper;

std::shared_ptr<ArtilleryTraceIDs> tracedRequestHeader(
    const McGetRequest& request) {
  apache::thrift::RpcOptions rpcOptions;
  ThriftTransportUtil{}.traceRequest(request, rpcOptions);
  return SerDeHelper::decodeAndDeserialize<ArtilleryTraceIDs>(
      rpcOptions.getWriteHeaders(),
      contextprop::ContextpropConstants_constants::artillery_trace_ids_header_);
}

TEST(ThriftTransportTest, TraceRequestSendsParentBlock) {
  McGetRequest request{"key"};
  request.setTraceContext(
      *SerDeHelper::encodeUInt64(11) + *SerDeHelper::encodeUInt64(22));
  request.setTraceParentBlockId(*SerDeHelper::encodeUInt64(33));

  auto traceIDs = tracedRequestHeader(request);

  ASSERT_NE(nullptr, traceIDs);
  EXPECT_EQ(11, *traceIDs->trace_id());
  EXPECT_EQ(22, *traceIDs->edge_id());
  EXPECT_EQ(33, traceIDs->parent_block_id().value_or(0));
}

TEST(ThriftTransportTest, TraceRequestWithoutParentBlockSendsNone) {
  McGetRequest request{"key"};
  request.setTraceContext(
      *SerDeHelper::encodeUInt64(11) + *SerDeHelper::encodeUInt64(22));

  auto traceIDs = tracedRequestHeader(request);

  ASSERT_NE(nullptr, traceIDs);
  EXPECT_EQ(22, *traceIDs->edge_id());
  EXPECT_FALSE(traceIDs->parent_block_id().has_value());
}

TEST(ThriftTransportTest, TraceRequestDropsMalformedParentBlock) {
  McGetRequest request{"key"};
  request.setTraceContext(
      *SerDeHelper::encodeUInt64(11) + *SerDeHelper::encodeUInt64(22));
  request.setTraceParentBlockId("not-a-block-id");

  auto traceIDs = tracedRequestHeader(request);

  ASSERT_NE(nullptr, traceIDs);
  EXPECT_EQ(22, *traceIDs->edge_id());
  EXPECT_FALSE(traceIDs->parent_block_id().has_value());
}

TEST(ThriftTransportTest, CopiedRequestKeepsParentBlock) {
  McGetRequest request{"key"};
  request.setTraceParentBlockId(*SerDeHelper::encodeUInt64(33));

  McGetRequest copy{"other"};
  copy.copyRequestContextFrom(request);

  EXPECT_EQ(request.traceParentBlockId(), copy.traceParentBlockId());
}

TEST(ThriftTransportTest, TraceResponseCopiesServerBlockReport) {
  McGetRequest request{"key"};
  request.setTraceContext("trace-context");

  using CompleteResponse = apache::thrift::RpcResponseComplete<McGetReply>;
  folly::Try<CompleteResponse> response{CompleteResponse{
      .response = folly::Try<McGetReply>{McGetReply{carbon::Result::FOUND}},
      .responseContext = {},
  }};
  const std::string serializedReport{"serialized-server-block-report"};
  response->responseContext.headers.emplace(
      facebook::artillery2::artillery_server_block_constants::
          artillery_server_block_header_,
      serializedReport);

  ThriftTransportUtil{}.traceResponse(request, response);

  EXPECT_EQ(serializedReport, response->response->artilleryServerBlockReport());
}

} // namespace
} // namespace facebook::memcache
