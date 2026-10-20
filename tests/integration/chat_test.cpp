/** @file
 * @brief Verify direct/group privacy, discovery, disconnect cleanup, and client buffer limits.
 */
#include "chat/client/chat_client.hpp"
#include "chat/server/ping_server.hpp"
#include "test_peer.hpp"

#include <boost/asio/socket_base.hpp>
#include <future>
#include <gtest/gtest.h>
#include <thread>

namespace {
using chat::protocol::Envelope;
using chat::test::encode;
using chat::test::Peer;
using chat::test::ping;
using namespace std::chrono_literals;

class ChatTest : public testing::Test {
  protected:
    ChatTest() : server(context, {boost::asio::ip::address_v4::loopback(), 0}) {
        server.start();
        for (auto& worker : workers) {
            worker = std::thread([this] { context.run(); });
        }
        connect(alice, "alice");
        connect(bob, "bob");
        connect(charlie, "charlie");
        sync_all();
    }
    ~ChatTest() override {
        server.stop();
        for (auto& worker : workers) {
            worker.join();
        }
    }
    Envelope request() { return ping(0, "chat-" + std::to_string(++number)); }
    Envelope transact(Peer& peer, const Envelope& message) {
        peer.send(encode(message));
        const auto result = peer.receive(1);
        if (result.messages.size() != 1) {
            throw std::runtime_error("missing response");
        }
        EXPECT_EQ(result.messages[0].request_id(), message.request_id());
        return result.messages[0];
    }
    void connect(Peer& peer, const std::string& name) {
        peer.socket.connect({boost::asio::ip::address_v4::loopback(), server.port()});
        auto message = request();
        message.mutable_login_request()->set_username(name);
        if (!transact(peer, message).login_response().accepted()) {
            throw std::runtime_error("login failed");
        }
    }
    void sync(Peer& peer) {
        const auto response = transact(peer, ping(321, "barrier-" + std::to_string(++number)));
        EXPECT_TRUE(response.has_pong());
    }
    void sync_all() {
        sync(alice);
        sync(bob);
        sync(charlie);
    }
    void quiet(Peer& peer) {
        const auto result = peer.receive(1, 100ms);
        EXPECT_TRUE(result.timed_out);
        EXPECT_TRUE(result.messages.empty());
    }
    Envelope direct_request(const std::string& recipient = "bob",
                            const std::string& text = "hello") {
        auto message = request();
        message.mutable_direct_message()->set_recipient(recipient);
        message.mutable_direct_message()->set_text(text);
        return message;
    }
    Envelope group_request(const std::string& id, const std::string& text = "hello group") {
        auto message = request();
        message.mutable_group_message()->set_group_id(id);
        message.mutable_group_message()->set_text(text);
        return message;
    }
    Envelope delivery(Peer& peer) {
        const auto result = peer.receive(1);
        if (result.messages.size() != 1) {
            throw std::runtime_error("missing delivery");
        }
        EXPECT_TRUE(result.messages[0].request_id().empty());
        return result.messages[0];
    }
    std::string create(const std::string& name = "Study group") {
        auto message = request();
        message.mutable_create_group_request()->set_name(name);
        const auto response = transact(alice, message);
        if (!response.has_ack()) {
            throw std::runtime_error("group creation failed");
        }
        sync_all();
        return response.ack().resource_id();
    }
    void join(Peer& peer, const std::string& id) {
        auto message = request();
        message.mutable_join_group_request()->set_group_id(id);
        EXPECT_TRUE(transact(peer, message).has_ack());
        sync_all();
    }
    const chat::protocol::GroupInfo& info(Peer& peer, const std::string& id) {
        if (peer.groups) {
            for (const auto& group : peer.groups->groups()) {
                if (group.group_id() == id) {
                    return group;
                }
            }
        }
        throw std::runtime_error("group absent from snapshot");
    }
    boost::asio::io_context context;
    chat::server::PingServer server;
    std::array<std::thread, 2> workers;
    Peer alice, bob, charlie;
    unsigned int number = 0;
};

/// @brief Check matching sender/recipient deliveries and a bounded third-client silence.
TEST_F(ChatTest, DirectMessageReachesBothParticipantsAndNeverTheThirdClient) {
    const auto ack = transact(alice, direct_request());
    ASSERT_TRUE(ack.has_ack());
    const auto sent = delivery(alice).direct_message();
    const auto received = delivery(bob).direct_message();
    EXPECT_EQ(sent.SerializeAsString(), received.SerializeAsString());
    EXPECT_EQ(received.sender(), "alice");
    EXPECT_EQ(received.recipient(), "bob");
    EXPECT_EQ(received.text(), "hello");
    EXPECT_EQ(received.room_id(), ack.ack().resource_id());
    EXPECT_EQ(received.message_id(), ack.ack().message_id());
    EXPECT_FALSE(received.message_id().empty());
    EXPECT_EQ(received.room_sequence(), 1U);
    quiet(charlie);
}

/// @brief Keep symmetric pair rooms separate from other conversations.
TEST_F(ChatTest, ReverseDirectMessagesReuseThePairRoomAndOtherPairsAreSeparate) {
    const auto first = transact(alice, direct_request()).ack().resource_id();
    delivery(alice);
    delivery(bob);
    const auto reverse = transact(bob, direct_request("alice", "reply")).ack().resource_id();
    delivery(alice);
    delivery(bob);
    EXPECT_EQ(first, reverse);
    const auto other = transact(alice, direct_request("charlie", "other")).ack().resource_id();
    delivery(alice);
    delivery(charlie);
    EXPECT_NE(first, other);
    quiet(bob);
}

/// @brief Avoid duplicate delivery when sender and recipient are the same connection.
TEST_F(ChatTest, SelfDirectMessageProducesOneEcho) {
    EXPECT_TRUE(transact(alice, direct_request("alice")).has_ack());
    EXPECT_EQ(delivery(alice).direct_message().recipient(), "alice");
    quiet(alice);
    quiet(bob);
    quiet(charlie);
}

/// @brief Reject an offline target without unintended message delivery.
TEST_F(ChatTest, MissingRecipientReturnsCorrelatedErrorWithoutDelivery) {
    EXPECT_EQ(transact(alice, direct_request("missing")).error().code(),
              chat::protocol::ERROR_CODE_USER_NOT_FOUND);
    quiet(alice);
    quiet(bob);
    quiet(charlie);
}

/// @brief Reject each client-supplied identity, room, ID, sequence, or timestamp field.
TEST_F(ChatTest, EveryForgedDirectMetadataFieldIsRejected) {
    for (int field = 0; field < 5; ++field) {
        auto message = direct_request();
        auto* dm = message.mutable_direct_message();
        switch (field) {
        case 0:
            dm->set_sender("bob");
            break;
        case 1:
            dm->set_room_id("forged");
            break;
        case 2:
            dm->set_message_id("forged");
            break;
        case 3:
            dm->set_room_sequence(7);
            break;
        case 4:
            dm->set_server_timestamp_unix_ms(7);
            break;
        }
        EXPECT_EQ(transact(alice, message).error().code(),
                  chat::protocol::ERROR_CODE_INVALID_REQUEST);
    }
    quiet(bob);
    quiet(charlie);
}

/// @brief Exercise direct-message text size and recipient name validation.
TEST_F(ChatTest, DirectTextAndRecipientBoundsAreEnforced) {
    for (const auto& text : {std::string{}, std::string(4097, 'x')}) {
        EXPECT_EQ(transact(alice, direct_request("bob", text)).error().code(),
                  chat::protocol::ERROR_CODE_INVALID_REQUEST);
    }
    EXPECT_EQ(transact(alice, direct_request("")).error().code(),
              chat::protocol::ERROR_CODE_INVALID_REQUEST);
    EXPECT_TRUE(transact(alice, direct_request("bob", std::string(4096, 'x'))).has_ack());
    EXPECT_EQ(delivery(alice).direct_message().text().size(), 4096U);
    EXPECT_EQ(delivery(bob).direct_message().text().size(), 4096U);
    quiet(charlie);
}

/// @brief Expose group discovery while initially adding only its creator.
TEST_F(ChatTest, GroupStartsWithOnlyItsCreatorAndIsVisibleToNonmembers) {
    const auto id = create();
    for (Peer* peer : {&alice, &bob, &charlie}) {
        EXPECT_EQ(info(*peer, id).name(), "Study group");
        ASSERT_EQ(info(*peer, id).members_size(), 1);
        EXPECT_EQ(info(*peer, id).members(0), "alice");
    }
}

/// @brief Require a voluntary join before member-only group delivery.
TEST_F(ChatTest, ExplicitSelfJoinUpdatesAllSnapshotsAndRoutesOnlyToMembers) {
    const auto id = create();
    join(bob, id);
    for (Peer* peer : {&alice, &bob, &charlie}) {
        ASSERT_EQ(info(*peer, id).members_size(), 2);
        EXPECT_EQ(info(*peer, id).members(0), "alice");
        EXPECT_EQ(info(*peer, id).members(1), "bob");
    }
    EXPECT_TRUE(transact(bob, group_request(id)).has_ack());
    const auto a = delivery(alice).group_message();
    const auto b = delivery(bob).group_message();
    EXPECT_EQ(a.SerializeAsString(), b.SerializeAsString());
    EXPECT_EQ(a.sender(), "bob");
    EXPECT_EQ(a.group_id(), id);
    quiet(charlie);
}

/// @brief Deny a forged sender when the requesting connection is outside the group.
TEST_F(ChatTest, NonmemberCannotSendEvenWhenClaimingAMemberIdentity) {
    const auto id = create();
    EXPECT_EQ(transact(bob, group_request(id)).error().code(),
              chat::protocol::ERROR_CODE_NOT_GROUP_MEMBER);
    auto forged = group_request(id);
    forged.mutable_group_message()->set_sender("alice");
    EXPECT_EQ(transact(bob, forged).error().code(), chat::protocol::ERROR_CODE_INVALID_REQUEST);
    quiet(alice);
    quiet(charlie);
}

/// @brief Reject client-supplied group sender and future delivery metadata.
TEST_F(ChatTest, EveryForgedGroupMetadataFieldIsRejected) {
    const auto id = create();
    for (int field = 0; field < 4; ++field) {
        auto message = group_request(id);
        auto* group = message.mutable_group_message();
        switch (field) {
        case 0:
            group->set_sender("bob");
            break;
        case 1:
            group->set_message_id("forged");
            break;
        case 2:
            group->set_room_sequence(7);
            break;
        case 3:
            group->set_server_timestamp_unix_ms(7);
            break;
        }
        EXPECT_EQ(transact(alice, message).error().code(),
                  chat::protocol::ERROR_CODE_INVALID_REQUEST);
    }
    quiet(bob);
    quiet(charlie);
}

/// @brief Return distinct errors for duplicate membership and absent groups.
TEST_F(ChatTest, DuplicateJoinAndUnknownGroupsAreRejected) {
    const auto id = create();
    auto message = request();
    message.mutable_join_group_request()->set_group_id(id);
    EXPECT_EQ(transact(alice, message).error().code(), chat::protocol::ERROR_CODE_ALREADY_MEMBER);
    message.mutable_join_group_request()->set_group_id("missing");
    EXPECT_EQ(transact(bob, message).error().code(), chat::protocol::ERROR_CODE_GROUP_NOT_FOUND);
    EXPECT_EQ(transact(bob, group_request("missing")).error().code(),
              chat::protocol::ERROR_CODE_GROUP_NOT_FOUND);
}

/// @brief Separate group identity from its reusable display name.
TEST_F(ChatTest, DuplicateDisplayNamesHaveDistinctGroupIdsAndIndependentMembership) {
    const auto first = create("same");
    const auto second = create("same");
    EXPECT_NE(first, second);
    join(bob, first);
    EXPECT_EQ(info(charlie, first).members_size(), 2);
    EXPECT_EQ(info(charlie, second).members_size(), 1);
    EXPECT_TRUE(transact(alice, group_request(second)).has_ack());
    delivery(alice);
    quiet(bob);
    quiet(charlie);
}

/// @brief Validate group-name controls/length and message text boundaries.
TEST_F(ChatTest, InvalidGroupNamesAndTextAreRejected) {
    for (const auto& name :
         {std::string{}, std::string("   "), std::string("bad\nname"), std::string(65, 'x')}) {
        auto message = request();
        message.mutable_create_group_request()->set_name(name);
        EXPECT_EQ(transact(alice, message).error().code(),
                  chat::protocol::ERROR_CODE_INVALID_REQUEST);
    }
    const auto id = create(std::string(64, 'x'));
    for (const auto& text : {std::string{}, std::string(4097, 'x')}) {
        EXPECT_EQ(transact(alice, group_request(id, text)).error().code(),
                  chat::protocol::ERROR_CODE_INVALID_REQUEST);
    }
    EXPECT_TRUE(transact(alice, group_request(id, std::string(4096, 'x'))).has_ack());
    EXPECT_EQ(delivery(alice).group_message().text().size(), 4096U);
    quiet(bob);
}

/// @brief Broadcast active names only to successfully registered connections.
TEST_F(ChatTest, AllClientsSeePresenceIncludingThemselvesAndRejectedClaimsStayHidden) {
    for (Peer* peer : {&alice, &bob, &charlie}) {
        ASSERT_TRUE(peer->users);
        EXPECT_EQ(peer->users->usernames_size(), 3);
        EXPECT_EQ(peer->users->usernames(0), "alice");
        EXPECT_EQ(peer->users->usernames(2), "charlie");
    }
    Peer rejected(server.port());
    auto message = request();
    message.mutable_login_request()->set_username("alice");
    EXPECT_FALSE(transact(rejected, message).login_response().accepted());
    sync_all();
    EXPECT_EQ(alice.users->usernames_size(), 3);
    quiet(rejected);
    EXPECT_FALSE(rejected.users);
    EXPECT_FALSE(rejected.groups);
}

/// @brief Remove connection-scoped membership before the same name is reused.
TEST_F(ChatTest, DisconnectRefreshesPresenceAndMembershipAndReclaimedNameMustRejoin) {
    const auto id = create();
    join(bob, id);
    bob.socket.shutdown(chat::test::tcp::socket::shutdown_send);
    ASSERT_EQ(bob.receive(1).error, boost::asio::error::eof);
    sync(alice);
    sync(charlie);
    EXPECT_EQ(alice.users->usernames_size(), 2);
    EXPECT_EQ(info(charlie, id).members_size(), 1);
    Peer replacement;
    connect(replacement, "bob");
    sync(replacement);
    EXPECT_EQ(transact(replacement, group_request(id)).error().code(),
              chat::protocol::ERROR_CODE_NOT_GROUP_MEMBER);
    quiet(alice);
    quiet(charlie);
}

/// @brief Keep discoverable empty groups until the server restarts.
TEST_F(ChatTest, EmptyGroupsSurviveCreatorDisconnectAndRemainJoinable) {
    const auto id = create();
    alice.socket.shutdown(chat::test::tcp::socket::shutdown_send);
    ASSERT_EQ(alice.receive(1).error, boost::asio::error::eof);
    sync(bob);
    sync(charlie);
    EXPECT_EQ(info(charlie, id).members_size(), 0);
    auto message = request();
    message.mutable_join_group_request()->set_group_id(id);
    EXPECT_TRUE(transact(bob, message).has_ack());
    sync(bob);
    sync(charlie);
    EXPECT_EQ(info(charlie, id).members(0), "bob");
}

/// @brief Avoid inheriting pair rooms from a previous connection identity.
TEST_F(ChatTest, ReconnectedUsernameGetsANewDirectRoom) {
    const auto previous = transact(alice, direct_request()).ack().resource_id();
    delivery(alice);
    delivery(bob);
    bob.socket.shutdown(chat::test::tcp::socket::shutdown_send);
    ASSERT_EQ(bob.receive(1).error, boost::asio::error::eof);
    Peer replacement;
    connect(replacement, "bob");
    const auto current = transact(alice, direct_request()).ack().resource_id();
    delivery(alice);
    delivery(replacement);
    EXPECT_NE(previous, current);
    quiet(charlie);
}

/// @brief Preserve delivery order despite TCP segmentation/coalescing.
TEST_F(ChatTest, FragmentedChatRequestAndCoalescedRequestsKeepDeliveryOrder) {
    const auto first = direct_request("bob", "split");
    for (const auto byte : encode(first)) {
        alice.send({byte});
    }
    EXPECT_TRUE(alice.receive(1).messages[0].has_ack());
    delivery(alice);
    EXPECT_EQ(delivery(bob).direct_message().text(), "split");
    std::vector<std::uint8_t> batch;
    for (const std::string text : {"one", "two"}) {
        const auto bytes = encode(direct_request("bob", text));
        batch.insert(batch.end(), bytes.begin(), bytes.end());
    }
    alice.send(batch);
    const auto results = alice.receive(4);
    ASSERT_EQ(results.messages.size(), 4U);
    EXPECT_TRUE(results.messages[0].has_ack());
    EXPECT_TRUE(results.messages[2].has_ack());
    EXPECT_EQ(delivery(bob).direct_message().text(), "one");
    EXPECT_EQ(delivery(bob).direct_message().text(), "two");
}

/// @brief Exercise public chat operations, typed updates, soft timeout, and subsequent Ping.
TEST_F(ChatTest, HeadlessApiReceivesSnapshotsEchoesAndOtherClientsDeliveries) {
    chat::client::ChatClient client;
    client.connect("127.0.0.1", server.port());
    ASSERT_TRUE(client.login("dana").accepted);
    bool saw_users = false, saw_groups = false;
    for (int i = 0; i < 2; ++i) {
        const auto event = client.next_event();
        ASSERT_TRUE(event);
        if (const auto* users = std::get_if<chat::client::UserSnapshot>(&*event)) {
            saw_users = true;
            EXPECT_EQ(users->usernames.size(), 4U);
        } else {
            saw_groups = std::holds_alternative<chat::client::GroupSnapshot>(*event);
        }
    }
    EXPECT_TRUE(saw_users);
    EXPECT_TRUE(saw_groups);
    const auto dm = client.send_direct("alice", "api");
    EXPECT_TRUE(dm.accepted);
    EXPECT_EQ(delivery(alice).direct_message().text(), "api");
    const auto echo = client.next_event();
    ASSERT_TRUE(echo);
    EXPECT_TRUE(std::holds_alternative<chat::client::DirectDelivery>(*echo));
    EXPECT_FALSE(client.next_event(50ms));
    EXPECT_EQ(client.ping(7), 7U);
    const auto created = client.create_group("API group");
    ASSERT_TRUE(created.accepted);
    EXPECT_TRUE(client.next_event().has_value());
    auto message = request();
    message.mutable_join_group_request()->set_group_id(created.resource_id);
    EXPECT_TRUE(transact(alice, message).has_ack());
    const auto group_ack = client.send_group(created.resource_id, "api group");
    EXPECT_TRUE(group_ack.accepted);
    EXPECT_EQ(delivery(alice).group_message().text(), "api group");
    bool saw_group_delivery = false;
    for (int i = 0; i < 2; ++i) {
        const auto event = client.next_event();
        ASSERT_TRUE(event);
        saw_group_delivery |= std::holds_alternative<chat::client::GroupDelivery>(*event);
    }
    EXPECT_TRUE(saw_group_delivery);
    EXPECT_EQ(client.join_group("missing").error, chat::client::RequestError::group_not_found);
}

/// @brief Discover existing groups without silently joining them.
TEST_F(ChatTest, LateLoginReceivesExistingGroupsWithoutImplicitMembership) {
    const auto id = create();
    join(bob, id);
    Peer newcomer;
    connect(newcomer, "dana");
    sync(newcomer);
    ASSERT_TRUE(newcomer.users);
    EXPECT_EQ(newcomer.users->usernames_size(), 4);
    EXPECT_EQ(info(newcomer, id).members_size(), 2);
    EXPECT_EQ(transact(newcomer, group_request(id)).error().code(),
              chat::protocol::ERROR_CODE_NOT_GROUP_MEMBER);
    quiet(alice);
    quiet(bob);
    quiet(charlie);
}

/// @brief Reject the 129th group without publishing phantom state.
TEST_F(ChatTest, GroupCountIsBoundedAndLimitFailureDoesNotCreateAnotherGroup) {
    for (int index = 0; index < 128; ++index) {
        auto message = request();
        message.mutable_create_group_request()->set_name("bounded");
        ASSERT_TRUE(transact(alice, message).has_ack());
    }
    auto message = request();
    message.mutable_create_group_request()->set_name("overflow");
    EXPECT_EQ(transact(alice, message).error().code(), chat::protocol::ERROR_CODE_INVALID_REQUEST);
    sync_all();
    ASSERT_TRUE(charlie.groups);
    EXPECT_EQ(charlie.groups->groups_size(), 128);
}

/// @brief Preserve a partial frame through a soft receive timeout.
TEST_F(ChatTest, QuietHeadlessReceiveRetainsPartialFramesForTheNextRead) {
    boost::asio::io_context fake_context;
    chat::test::tcp::acceptor fake(fake_context, {boost::asio::ip::address_v4::loopback(), 0});
    chat::client::ChatClient client;
    client.connect("127.0.0.1", fake.local_endpoint().port());
    Peer fake_server;
    fake.accept(fake_server.socket);
    Envelope update;
    update.set_protocol_version(1);
    update.mutable_user_list_update()->add_usernames("alice");
    const auto bytes = encode(update);
    fake_server.send({bytes.begin(), bytes.begin() + 2});
    EXPECT_FALSE(client.next_event(50ms));
    fake_server.send({bytes.begin() + 2, bytes.end()});
    const auto event = client.next_event();
    ASSERT_TRUE(event);
    EXPECT_EQ(std::get<chat::client::UserSnapshot>(*event).usernames[0], "alice");
}

/// @brief Fail explicitly when unsolicited events exceed the count bound.
TEST_F(ChatTest, ClientEventQueueOverflowClosesConnectionInsteadOfDroppingEvents) {
    boost::asio::io_context fake_context;
    chat::test::tcp::acceptor fake(fake_context, {boost::asio::ip::address_v4::loopback(), 0});
    chat::client::ChatClient client;
    client.connect("127.0.0.1", fake.local_endpoint().port());
    Peer fake_server;
    fake.accept(fake_server.socket);
    auto response = std::async(std::launch::async, [&] {
        const auto request = fake_server.receive(1);
        if (request.messages.size() != 1) {
            return false;
        }
        Envelope update;
        update.set_protocol_version(1);
        update.mutable_user_list_update();
        const auto bytes = encode(update);
        std::vector<std::uint8_t> batch;
        for (int index = 0; index < 257; ++index) {
            batch.insert(batch.end(), bytes.begin(), bytes.end());
        }
        fake_server.send(batch);
        return true;
    });
    try {
        static_cast<void>(client.ping(7));
        FAIL() << "event queue should overflow";
    } catch (const std::runtime_error& error) {
        EXPECT_STREQ(error.what(), "client event queue limit exceeded");
    }
    EXPECT_TRUE(response.get());
    EXPECT_THROW(static_cast<void>(client.ping(8)), std::runtime_error);
}

/// @brief Remove a slow receiver while healthy clients receive cleanup snapshots.
TEST_F(ChatTest, SlowMemberQueueOverflowUpdatesRegistryWithoutInterruptingOtherClients) {
    const auto id = create();
    join(bob, id);
    bob.socket.set_option(boost::asio::socket_base::receive_buffer_size(1024));
    bool removed = false;
    for (int index = 0; index < 600; ++index) {
        ASSERT_TRUE(transact(alice, group_request(id, std::string(4096, 'x'))).has_ack());
        delivery(alice);
        if (alice.users && alice.users->usernames_size() == 2) {
            removed = true;
            break;
        }
    }
    EXPECT_TRUE(removed);
    sync(alice);
    sync(charlie);
    EXPECT_EQ(charlie.users->usernames_size(), 2);
    EXPECT_EQ(info(charlie, id).members_size(), 1);
    EXPECT_EQ(info(charlie, id).members(0), "alice");
    quiet(charlie);
}

/// @brief Retain event bytes following a complete correlated reply.
TEST_F(ChatTest, CorrelatedReplyCanBeFollowedByAPartialEventInTheSameRead) {
    boost::asio::io_context fake_context;
    chat::test::tcp::acceptor fake(fake_context, {boost::asio::ip::address_v4::loopback(), 0});
    chat::client::ChatClient client;
    client.connect("127.0.0.1", fake.local_endpoint().port());
    Peer fake_server;
    fake.accept(fake_server.socket);
    Envelope update;
    update.set_protocol_version(1);
    update.mutable_user_list_update()->add_usernames("alice");
    const auto bytes = encode(update);
    auto response = std::async(std::launch::async, [&] {
        const auto request = fake_server.receive(1);
        if (request.messages.size() != 1) {
            return false;
        }
        auto reply = request.messages[0];
        reply.mutable_pong()->set_nonce(7);
        auto batch = encode(reply);
        batch.insert(batch.end(), bytes.begin(), bytes.begin() + 2);
        fake_server.send(batch);
        return true;
    });
    EXPECT_EQ(client.ping(7), 7U);
    EXPECT_TRUE(response.get());
    fake_server.send({bytes.begin() + 2, bytes.end()});
    const auto event = client.next_event();
    ASSERT_TRUE(event);
    EXPECT_EQ(std::get<chat::client::UserSnapshot>(*event).usernames[0], "alice");
}

/// @brief Enforce the event byte budget independently of its frame count.
TEST_F(ChatTest, ClientEventByteLimitAppliesBeforeTheFrameCountLimit) {
    boost::asio::io_context fake_context;
    chat::test::tcp::acceptor fake(fake_context, {boost::asio::ip::address_v4::loopback(), 0});
    chat::client::ChatClient client;
    client.connect("127.0.0.1", fake.local_endpoint().port());
    Peer fake_server;
    fake.accept(fake_server.socket);
    Envelope update;
    update.set_protocol_version(1);
    for (int group = 0; group < 128; ++group) {
        auto* info = update.mutable_group_list_update()->add_groups();
        info->set_group_id("group-" + std::to_string(group));
        info->set_name("valid");
        for (int member = 0; member < 64; ++member) {
            info->add_members(std::string(30, 'a') + std::to_string(member));
        }
    }
    const auto bytes = encode(update);
    ASSERT_GT(bytes.size() * 8, 2U * chat::protocol::kMaxFrameSize);
    auto response = std::async(std::launch::async, [&] {
        const auto request = fake_server.receive(1);
        if (request.messages.size() != 1) {
            return false;
        }
        std::vector<std::uint8_t> batch;
        for (int index = 0; index < 8; ++index) {
            batch.insert(batch.end(), bytes.begin(), bytes.end());
        }
        fake_server.send(batch);
        return true;
    });
    try {
        static_cast<void>(client.ping(7));
        FAIL() << "byte limit should overflow";
    } catch (const std::runtime_error& error) {
        EXPECT_STREQ(error.what(), "client event queue limit exceeded");
    }
    EXPECT_TRUE(response.get());
}

} // namespace
