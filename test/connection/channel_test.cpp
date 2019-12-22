#include <thread>

#include <gtest/gtest.h>

#include "common/test_utils.h"
#include "connection/channel.h"

using namespace std;
using namespace slog;

class ChannelTest : public ::testing::Test {
protected:

  void SetUp() override {
    auto context = std::make_shared<zmq::context_t>(1);
    channel_ = std::make_unique<Channel>(context, "test_channel");
  }

  std::unique_ptr<Channel> channel_;
};

TEST_F(ChannelTest, ListenToChannel) {
  std::thread th([this](){
    std::unique_ptr<ChannelListener> listener(channel_->GetListener());
    MMessage message;
    listener->PollMessage(message);
    proto::Request req;
    ASSERT_TRUE(message.GetProto(req));
    ASSERT_EQ("test", req.echo_req().data());
  });

  MMessage message;
  message.Add(MakeEchoRequest("test"));
  message.SetIdentity("zZz");
  channel_->SendToListener(message);
  th.join();
}

TEST_F(ChannelTest, SendToChannel) {
  std::thread th([this](){
    std::unique_ptr<ChannelListener> listener(channel_->GetListener());
    MMessage message;
    message.SetIdentity("zZz");
    message.Add(MakeEchoResponse("test"));
    listener->SendMessage(message);
  });

  // Not necessary to poll but still do to test GetPollItem
  auto item = channel_->GetPollItem();
  zmq::poll(&item, 1);

  MMessage message;
  channel_->ReceiveFromListener(message);
  proto::Response res;
  ASSERT_TRUE(message.GetProto(res));
  ASSERT_EQ("test", res.echo_res().data());

  th.join();
}
