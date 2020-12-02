//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/GroupCallManager.h"

#include "td/telegram/ContactsManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/misc.h"
#include "td/telegram/net/NetQuery.h"
#include "td/telegram/Td.h"
#include "td/telegram/UpdatesManager.h"

#include "td/utils/JsonBuilder.h"
#include "td/utils/Random.h"

namespace td {

class CreateGroupCallQuery : public Td::ResultHandler {
  Promise<InputGroupCallId> promise_;
  ChannelId channel_id_;

 public:
  explicit CreateGroupCallQuery(Promise<InputGroupCallId> &&promise) : promise_(std::move(promise)) {
  }

  void send(ChannelId channel_id) {
    channel_id_ = channel_id;

    auto input_channel = td->contacts_manager_->get_input_channel(channel_id);
    CHECK(input_channel != nullptr);

    send_query(G()->net_query_creator().create(
        telegram_api::phone_createGroupCall(std::move(input_channel), Random::secure_int32())));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::phone_createGroupCall>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for CreateGroupCallQuery: " << to_string(ptr);

    auto group_call_ids = td->updates_manager_->get_update_new_group_call_ids(ptr.get());
    if (group_call_ids.size() != 1) {
      LOG(ERROR) << "Receive wrong CreateGroupCallQuery response " << to_string(ptr);
      return on_error(id, Status::Error(500, "Receive wrong response"));
    }

    td->updates_manager_->on_get_updates(std::move(ptr));

    // TODO set promise after updates are processed
    promise_.set_value(std::move(group_call_ids[0]));
  }

  void on_error(uint64 id, Status status) override {
    td->contacts_manager_->on_get_channel_error(channel_id_, status, "CreateGroupCallQuery");
    promise_.set_error(std::move(status));
  }
};

class GetGroupCallQuery : public Td::ResultHandler {
  Promise<tl_object_ptr<telegram_api::phone_groupCall>> promise_;
  InputGroupCallId group_call_id_;

 public:
  explicit GetGroupCallQuery(Promise<tl_object_ptr<telegram_api::phone_groupCall>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(InputGroupCallId group_call_id) {
    group_call_id_ = group_call_id;

    send_query(G()->net_query_creator().create(telegram_api::phone_getGroupCall(group_call_id.get_input_group_call())));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::phone_getGroupCall>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetGroupCallQuery: " << to_string(ptr);

    promise_.set_value(std::move(ptr));
  }

  void on_error(uint64 id, Status status) override {
    promise_.set_error(std::move(status));
  }
};

class JoinGroupCallQuery : public Td::ResultHandler {
  Promise<Unit> promise_;
  InputGroupCallId group_call_id_;
  uint64 generation_ = 0;

 public:
  explicit JoinGroupCallQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  NetQueryRef send(InputGroupCallId group_call_id, const string &payload, bool is_muted, uint64 generation) {
    group_call_id_ = group_call_id;
    generation_ = generation;

    int32 flags = 0;
    if (is_muted) {
      flags |= telegram_api::phone_joinGroupCall::MUTED_MASK;
    }
    auto query = G()->net_query_creator().create(
        telegram_api::phone_joinGroupCall(flags, false /*ignored*/, group_call_id.get_input_group_call(),
                                          make_tl_object<telegram_api::dataJSON>(payload)));
    auto join_query_ref = query.get_weak();
    send_query(std::move(query));
    return join_query_ref;
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::phone_joinGroupCall>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    td->group_call_manager_->process_join_group_call_response(group_call_id_, generation_, result_ptr.move_as_ok(),
                                                              std::move(promise_));
  }

  void on_error(uint64 id, Status status) override {
    promise_.set_error(std::move(status));
  }
};

class ToggleGroupCallSettingsQuery : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit ToggleGroupCallSettingsQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(int32 flags, InputGroupCallId group_call_id, bool join_muted) {
    send_query(G()->net_query_creator().create(
        telegram_api::phone_toggleGroupCallSettings(flags, group_call_id.get_input_group_call(), join_muted)));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::phone_toggleGroupCallSettings>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for ToggleGroupCallSettingsQuery: " << to_string(ptr);
    td->updates_manager_->on_get_updates(std::move(ptr));

    // TODO set promise after updates are processed
    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    promise_.set_error(std::move(status));
  }
};

class InviteToGroupCallQuery : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit InviteToGroupCallQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(InputGroupCallId group_call_id, vector<tl_object_ptr<telegram_api::InputUser>> input_users) {
    send_query(G()->net_query_creator().create(
        telegram_api::phone_inviteToGroupCall(group_call_id.get_input_group_call(), std::move(input_users))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::phone_inviteToGroupCall>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for InviteToGroupCallQuery: " << to_string(ptr);
    td->updates_manager_->on_get_updates(std::move(ptr));

    // TODO set promise after updates are processed
    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    promise_.set_error(std::move(status));
  }
};

class EditGroupCallMemberQuery : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit EditGroupCallMemberQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(InputGroupCallId group_call_id, UserId user_id, bool is_muted) {
    auto input_user = td->contacts_manager_->get_input_user(user_id);
    CHECK(input_user != nullptr);

    int32 flags = 0;
    if (is_muted) {
      flags |= telegram_api::phone_editGroupCallMember::MUTED_MASK;
    }

    send_query(G()->net_query_creator().create(telegram_api::phone_editGroupCallMember(
        flags, false /*ignored*/, group_call_id.get_input_group_call(), std::move(input_user))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::phone_editGroupCallMember>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for EditGroupCallMemberQuery: " << to_string(ptr);
    td->updates_manager_->on_get_updates(std::move(ptr));

    // TODO set promise after updates are processed
    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    promise_.set_error(std::move(status));
  }
};

class CheckGroupCallQuery : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit CheckGroupCallQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(InputGroupCallId group_call_id, int32 source) {
    send_query(G()->net_query_creator().create(
        telegram_api::phone_checkGroupCall(group_call_id.get_input_group_call(), source)));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::phone_checkGroupCall>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    bool success = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for CheckGroupCallQuery: " << success;

    if (success) {
      promise_.set_value(Unit());
    } else {
      promise_.set_error(Status::Error(200, "Group call left"));
    }
  }

  void on_error(uint64 id, Status status) override {
    promise_.set_error(std::move(status));
  }
};

class LeaveGroupCallQuery : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit LeaveGroupCallQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(InputGroupCallId group_call_id, int32 source) {
    send_query(G()->net_query_creator().create(
        telegram_api::phone_leaveGroupCall(group_call_id.get_input_group_call(), source)));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::phone_leaveGroupCall>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for LeaveGroupCallQuery: " << to_string(ptr);
    td->updates_manager_->on_get_updates(std::move(ptr));

    // TODO set promise after updates are processed
    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    promise_.set_error(std::move(status));
  }
};

class DiscardGroupCallQuery : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit DiscardGroupCallQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(InputGroupCallId group_call_id) {
    send_query(
        G()->net_query_creator().create(telegram_api::phone_discardGroupCall(group_call_id.get_input_group_call())));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::phone_discardGroupCall>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for DiscardGroupCallQuery: " << to_string(ptr);
    td->updates_manager_->on_get_updates(std::move(ptr));

    // TODO set promise after updates are processed
    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    promise_.set_error(std::move(status));
  }
};

struct GroupCallManager::GroupCall {
  bool is_active = false;
  bool mute_new_members = false;
  bool allowed_change_mute_new_members = false;
  int32 member_count = 0;
  int32 version = -1;
  int32 duration = 0;
};

struct GroupCallManager::PendingJoinRequest {
  NetQueryRef query_ref;
  uint64 generation = 0;
  Promise<td_api::object_ptr<td_api::groupCallJoinResponse>> promise;
};

GroupCallManager::GroupCallManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
}

GroupCallManager::~GroupCallManager() = default;

void GroupCallManager::tear_down() {
  parent_.reset();
}

void GroupCallManager::create_voice_chat(ChannelId channel_id, Promise<InputGroupCallId> &&promise) {
  td_->create_handler<CreateGroupCallQuery>(std::move(promise))->send(channel_id);
}

const GroupCallManager::GroupCall *GroupCallManager::get_group_call(InputGroupCallId group_call_id) const {
  auto it = group_calls_.find(group_call_id);
  if (it == group_calls_.end()) {
    return nullptr;
  } else {
    return it->second.get();
  }
}

GroupCallManager::GroupCall *GroupCallManager::get_group_call(InputGroupCallId group_call_id) {
  auto it = group_calls_.find(group_call_id);
  if (it == group_calls_.end()) {
    return nullptr;
  } else {
    return it->second.get();
  }
}

void GroupCallManager::get_group_call(InputGroupCallId group_call_id,
                                      Promise<td_api::object_ptr<td_api::groupCall>> &&promise) {
  auto group_call = get_group_call(group_call_id);
  if (group_call != nullptr) {
    return promise.set_value(get_group_call_object(group_call_id, group_call));
  }

  reload_group_call(group_call_id, std::move(promise));
}

void GroupCallManager::reload_group_call(InputGroupCallId group_call_id,
                                         Promise<td_api::object_ptr<td_api::groupCall>> &&promise) {
  auto &queries = load_group_call_queries_[group_call_id];
  queries.push_back(std::move(promise));
  if (queries.size() == 1) {
    auto query_promise = PromiseCreator::lambda(
        [actor_id = actor_id(this), group_call_id](Result<tl_object_ptr<telegram_api::phone_groupCall>> &&result) {
          send_closure(actor_id, &GroupCallManager::finish_get_group_call, group_call_id, std::move(result));
        });
    td_->create_handler<GetGroupCallQuery>(std::move(query_promise))->send(group_call_id);
  }
}

void GroupCallManager::finish_get_group_call(InputGroupCallId group_call_id,
                                             Result<tl_object_ptr<telegram_api::phone_groupCall>> &&result) {
  auto it = load_group_call_queries_.find(group_call_id);
  CHECK(it != load_group_call_queries_.end());
  CHECK(!it->second.empty());
  auto promises = std::move(it->second);
  load_group_call_queries_.erase(it);

  if (result.is_ok()) {
    td_->contacts_manager_->on_get_users(std::move(result.ok_ref()->users_), "finish_get_group_call");

    auto call_id = update_group_call(result.ok()->call_);
    if (call_id != group_call_id) {
      LOG(ERROR) << "Expected " << group_call_id << ", but received " << to_string(result.ok());
      result = Status::Error(500, "Receive another group call");
    }
  }

  if (result.is_error()) {
    for (auto &promise : promises) {
      promise.set_error(result.error().clone());
    }
    return;
  }

  auto group_call = get_group_call(group_call_id);
  CHECK(group_call != nullptr);
  for (auto &promise : promises) {
    promise.set_value(get_group_call_object(group_call_id, group_call));
  }
}

void GroupCallManager::join_group_call(InputGroupCallId group_call_id,
                                       td_api::object_ptr<td_api::groupCallPayload> &&payload, int32 source,
                                       bool is_muted,
                                       Promise<td_api::object_ptr<td_api::groupCallJoinResponse>> &&promise) {
  if (pending_join_requests_.count(group_call_id)) {
    auto it = pending_join_requests_.find(group_call_id);
    CHECK(it != pending_join_requests_.end());
    CHECK(it->second != nullptr);
    if (!it->second->query_ref.empty()) {
      cancel_query(it->second->query_ref);
    }
    it->second->promise.set_error(Status::Error(200, "Cancelled by another joinGroupCall request"));
    pending_join_requests_.erase(it);
  }

  if (payload == nullptr) {
    return promise.set_error(Status::Error(400, "Payload must be non-empty"));
  }
  if (!clean_input_string(payload->ufrag_)) {
    return promise.set_error(Status::Error(400, "Payload ufrag must be encoded in UTF-8"));
  }
  if (!clean_input_string(payload->pwd_)) {
    return promise.set_error(Status::Error(400, "Payload pwd must be encoded in UTF-8"));
  }
  for (auto &fingerprint : payload->fingerprints_) {
    if (fingerprint == nullptr) {
      return promise.set_error(Status::Error(400, "Payload fingerprint must be non-empty"));
    }
    if (!clean_input_string(fingerprint->hash_)) {
      return promise.set_error(Status::Error(400, "Fingerprint hash must be encoded in UTF-8"));
    }
    if (!clean_input_string(fingerprint->setup_)) {
      return promise.set_error(Status::Error(400, "Fingerprint setup must be encoded in UTF-8"));
    }
    if (!clean_input_string(fingerprint->fingerprint_)) {
      return promise.set_error(Status::Error(400, "Fingerprint must be encoded in UTF-8"));
    }
  }

  auto json_payload = json_encode<string>(json_object([&payload, source](auto &o) {
    o("ufrag", payload->ufrag_);
    o("pwd", payload->pwd_);
    o("fingerprints", json_array(payload->fingerprints_,
                                 [](const td_api::object_ptr<td_api::groupCallPayloadFingerprint> &fingerprint) {
                                   return json_object([&fingerprint](auto &o) {
                                     o("hash", fingerprint->hash_);
                                     o("setup", fingerprint->setup_);
                                     o("fingerprint", fingerprint->fingerprint_);
                                   });
                                 }));
    o("ssrc", source);
  }));

  auto generation = ++join_group_request_generation_;
  auto &request = pending_join_requests_[group_call_id];
  request = make_unique<PendingJoinRequest>();
  request->generation = generation;
  request->promise = std::move(promise);

  auto query_promise =
      PromiseCreator::lambda([actor_id = actor_id(this), generation, group_call_id](Result<Unit> &&result) {
        CHECK(result.is_error());
        send_closure(actor_id, &GroupCallManager::finish_join_group_call, group_call_id, generation,
                     result.move_as_error());
      });
  request->query_ref = td_->create_handler<JoinGroupCallQuery>(std::move(query_promise))
                           ->send(group_call_id, json_payload, is_muted, generation);
}

void GroupCallManager::process_join_group_call_response(InputGroupCallId group_call_id, uint64 generation,
                                                        tl_object_ptr<telegram_api::Updates> &&updates,
                                                        Promise<Unit> &&promise) {
  auto it = pending_join_requests_.find(group_call_id);
  if (it == pending_join_requests_.end() || it->second->generation != generation) {
    LOG(INFO) << "Ignore JoinGroupCallQuery response with " << group_call_id << " and generation " << generation;
    return;
  }

  LOG(INFO) << "Receive result for JoinGroupCallQuery: " << to_string(updates);
  td_->updates_manager_->on_get_updates(std::move(updates));

  promise.set_error(Status::Error(500, "Wrong join response received"));
}

Result<td_api::object_ptr<td_api::groupCallJoinResponse>> GroupCallManager::get_group_call_join_response_object(
    string json_response) {
  auto r_value = json_decode(json_response);
  if (r_value.is_error()) {
    return Status::Error("Can't parse JSON object");
  }

  auto value = r_value.move_as_ok();
  if (value.type() != JsonValue::Type::Object) {
    return Status::Error("Expected an Object");
  }

  auto &value_object = value.get_object();
  TRY_RESULT(transport, get_json_object_field(value_object, "transport", JsonValue::Type::Object, false));
  CHECK(transport.type() == JsonValue::Type::Object);
  auto &transport_object = transport.get_object();

  TRY_RESULT(candidates, get_json_object_field(transport_object, "candidates", JsonValue::Type::Array, false));
  TRY_RESULT(fingerprints, get_json_object_field(transport_object, "fingerprints", JsonValue::Type::Array, false));
  TRY_RESULT(ufrag, get_json_object_string_field(transport_object, "ufrag", false));
  TRY_RESULT(pwd, get_json_object_string_field(transport_object, "pwd", false));
  // skip "xmlns", "rtcp-mux"

  vector<td_api::object_ptr<td_api::groupCallPayloadFingerprint>> fingerprints_object;
  for (auto &fingerprint : fingerprints.get_array()) {
    if (fingerprint.type() != JsonValue::Type::Object) {
      return Status::Error("Expected JSON object as fingerprint");
    }
    auto &fingerprint_object = fingerprint.get_object();
    TRY_RESULT(hash, get_json_object_string_field(fingerprint_object, "hash", false));
    TRY_RESULT(setup, get_json_object_string_field(fingerprint_object, "setup", false));
    TRY_RESULT(fingerprint_value, get_json_object_string_field(fingerprint_object, "fingerprint", false));
    fingerprints_object.push_back(
        td_api::make_object<td_api::groupCallPayloadFingerprint>(hash, setup, fingerprint_value));
  }

  vector<td_api::object_ptr<td_api::groupCallJoinResponseCandidate>> candidates_object;
  for (auto &candidate : candidates.get_array()) {
    if (candidate.type() != JsonValue::Type::Object) {
      return Status::Error("Expected JSON object as candidate");
    }
    auto &candidate_object = candidate.get_object();
    TRY_RESULT(port, get_json_object_string_field(candidate_object, "port", false));
    TRY_RESULT(protocol, get_json_object_string_field(candidate_object, "protocol", false));
    TRY_RESULT(network, get_json_object_string_field(candidate_object, "network", false));
    TRY_RESULT(generation, get_json_object_string_field(candidate_object, "generation", false));
    TRY_RESULT(id, get_json_object_string_field(candidate_object, "id", false));
    TRY_RESULT(component, get_json_object_string_field(candidate_object, "component", false));
    TRY_RESULT(foundation, get_json_object_string_field(candidate_object, "foundation", false));
    TRY_RESULT(priority, get_json_object_string_field(candidate_object, "priority", false));
    TRY_RESULT(ip, get_json_object_string_field(candidate_object, "ip", false));
    TRY_RESULT(type, get_json_object_string_field(candidate_object, "type", false));
    TRY_RESULT(tcp_type, get_json_object_string_field(candidate_object, "tcptype"));
    TRY_RESULT(rel_addr, get_json_object_string_field(candidate_object, "rel-addr"));
    TRY_RESULT(rel_port, get_json_object_string_field(candidate_object, "rel-port"));
    candidates_object.push_back(td_api::make_object<td_api::groupCallJoinResponseCandidate>(
        port, protocol, network, generation, id, component, foundation, priority, ip, type, tcp_type, rel_addr,
        rel_port));
  }

  auto payload = td_api::make_object<td_api::groupCallPayload>(ufrag, pwd, std::move(fingerprints_object));
  return td_api::make_object<td_api::groupCallJoinResponse>(std::move(payload), std::move(candidates_object));
}

void GroupCallManager::on_join_group_call_response(InputGroupCallId group_call_id, string json_response) {
  auto it = pending_join_requests_.find(group_call_id);
  if (it == pending_join_requests_.end()) {
    return;
  }
  CHECK(it->second != nullptr);

  auto result = get_group_call_join_response_object(std::move(json_response));
  if (result.is_error()) {
    LOG(ERROR) << "Failed to parse join response JSON object: " << result.error().message();
    it->second->promise.set_error(Status::Error(500, "Receive invalid join group call response payload"));
  } else {
    it->second->promise.set_value(result.move_as_ok());
  }
  pending_join_requests_.erase(it);
}

void GroupCallManager::finish_join_group_call(InputGroupCallId group_call_id, uint64 generation, Status error) {
  CHECK(error.is_error());
  auto it = pending_join_requests_.find(group_call_id);
  if (it == pending_join_requests_.end() || (generation != 0 && it->second->generation != generation)) {
    return;
  }
  it->second->promise.set_error(std::move(error));
  pending_join_requests_.erase(it);
}

void GroupCallManager::toggle_group_call_mute_new_members(InputGroupCallId group_call_id, bool mute_new_members,
                                                          Promise<Unit> &&promise) {
  int32 flags = telegram_api::phone_toggleGroupCallSettings::JOIN_MUTED_MASK;
  td_->create_handler<ToggleGroupCallSettingsQuery>(std::move(promise))->send(flags, group_call_id, mute_new_members);
}

void GroupCallManager::invite_group_call_members(InputGroupCallId group_call_id, vector<UserId> &&user_ids,
                                                 Promise<Unit> &&promise) {
  vector<tl_object_ptr<telegram_api::InputUser>> input_users;
  auto my_user_id = td_->contacts_manager_->get_my_id();
  for (auto user_id : user_ids) {
    auto input_user = td_->contacts_manager_->get_input_user(user_id);
    if (input_user == nullptr) {
      return promise.set_error(Status::Error(400, "User not found"));
    }

    if (user_id == my_user_id) {
      // can't invite self
      continue;
    }
    input_users.push_back(std::move(input_user));
  }

  if (input_users.empty()) {
    return promise.set_value(Unit());
  }

  td_->create_handler<InviteToGroupCallQuery>(std::move(promise))->send(group_call_id, std::move(input_users));
}

void GroupCallManager::toggle_group_call_member_is_muted(InputGroupCallId group_call_id, UserId user_id, bool is_muted,
                                                         Promise<Unit> &&promise) {
  if (!td_->contacts_manager_->have_input_user(user_id)) {
    return promise.set_error(Status::Error(400, "Have no access to the user"));
  }
  td_->create_handler<EditGroupCallMemberQuery>(std::move(promise))->send(group_call_id, user_id, is_muted);
}

void GroupCallManager::check_group_call_source(InputGroupCallId group_call_id, int32 source, Promise<Unit> &&promise) {
  td_->create_handler<CheckGroupCallQuery>(std::move(promise))->send(group_call_id, source);
}

void GroupCallManager::leave_group_call(InputGroupCallId group_call_id, int32 source, Promise<Unit> &&promise) {
  td_->create_handler<LeaveGroupCallQuery>(std::move(promise))->send(group_call_id, source);
}

void GroupCallManager::discard_group_call(InputGroupCallId group_call_id, Promise<Unit> &&promise) {
  td_->create_handler<DiscardGroupCallQuery>(std::move(promise))->send(group_call_id);
}

void GroupCallManager::on_update_group_call(tl_object_ptr<telegram_api::GroupCall> group_call_ptr) {
  auto call_id = update_group_call(group_call_ptr);
  if (call_id.is_valid()) {
    LOG(INFO) << "Update " << call_id;
  } else {
    LOG(ERROR) << "Receive invalid " << to_string(group_call_ptr);
  }
}

InputGroupCallId GroupCallManager::update_group_call(const tl_object_ptr<telegram_api::GroupCall> &group_call_ptr) {
  CHECK(group_call_ptr != nullptr);

  InputGroupCallId call_id;
  GroupCall call;
  switch (group_call_ptr->get_id()) {
    case telegram_api::groupCall::ID: {
      auto group_call = static_cast<const telegram_api::groupCall *>(group_call_ptr.get());
      call_id = InputGroupCallId(group_call->id_, group_call->access_hash_);
      call.is_active = true;
      call.mute_new_members = group_call->join_muted_;
      call.allowed_change_mute_new_members = group_call->can_change_join_muted_;
      call.member_count = group_call->participants_count_;
      call.version = group_call->version_;
      if (group_call->params_ != nullptr) {
        on_join_group_call_response(call_id, std::move(group_call->params_->data_));
      }
      break;
    }
    case telegram_api::groupCallDiscarded::ID: {
      auto group_call = static_cast<const telegram_api::groupCallDiscarded *>(group_call_ptr.get());
      call_id = InputGroupCallId(group_call->id_, group_call->access_hash_);
      call.duration = group_call->duration_;
      finish_join_group_call(call_id, 0, Status::Error(400, "Group call ended"));
      break;
    }
    default:
      UNREACHABLE();
  }
  if (!call_id.is_valid() || call.member_count < 0) {
    return {};
  }

  auto &group_call = group_calls_[call_id];
  bool need_update = false;
  if (group_call == nullptr) {
    group_call = make_unique<GroupCall>();
    *group_call = std::move(call);
    need_update = true;
  } else {
    if (!group_call->is_active) {
      // never update ended calls
    } else if (!call.is_active) {
      // always update to an ended call
      *group_call = std::move(call);
      need_update = true;
    } else {
      auto mute_flags_changed = call.mute_new_members != group_call->mute_new_members ||
                                call.allowed_change_mute_new_members != group_call->allowed_change_mute_new_members;
      if (call.version > group_call->version) {
        need_update = call.member_count != group_call->member_count || mute_flags_changed;
        *group_call = std::move(call);
      } else if (call.version == group_call->version) {
        if (mute_flags_changed) {
          group_call->mute_new_members = call.mute_new_members;
          group_call->allowed_change_mute_new_members = call.allowed_change_mute_new_members;
          need_update = true;
        }
      }
    }
  }

  if (need_update) {
    send_closure(G()->td(), &Td::send_update, get_update_group_call_object(call_id, group_call.get()));
  }

  return call_id;
}

tl_object_ptr<td_api::groupCall> GroupCallManager::get_group_call_object(InputGroupCallId group_call_id,
                                                                         const GroupCall *group_call) {
  CHECK(group_call != nullptr);
  return td_api::make_object<td_api::groupCall>(group_call_id.get_group_call_id(), group_call->is_active,
                                                group_call->member_count, group_call->mute_new_members,
                                                group_call->allowed_change_mute_new_members, group_call->duration);
}

tl_object_ptr<td_api::updateGroupCall> GroupCallManager::get_update_group_call_object(InputGroupCallId group_call_id,
                                                                                      const GroupCall *group_call) {
  return td_api::make_object<td_api::updateGroupCall>(get_group_call_object(group_call_id, group_call));
}

}  // namespace td
