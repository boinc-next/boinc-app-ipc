// This file is part of boinc-app-ipc.
// https://boinc-next.github.io
// Copyright (C) 2016 BOINC Next project
//
// boinc-app-ipc is free software; you can redistribute it and/or modify it
// under the terms of the GNU Lesser General Public License
// as published by the Free Software Foundation,
// either version 3 of the License, or (at your option) any later version.
//
// boinc-app-ipc is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
// See the GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with boinc-app-ipc.  If not, see <http://www.gnu.org/licenses/>.

#ifndef _APP_IPC_
#define _APP_IPC_

#include "result.hpp"
#include "xmlpp_util.hpp"

#include <glibmm.h>
#include <json/json.h>
#include <libxml++/libxml++.h>
#include <pstreams/pstream.h>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <condition_variable>
#include <set>
#include <thread>

using ManagerError = int;

template <typename T> using TResult = Result<T, ManagerError>;

using Dummy = std::tuple<>;
using CResult = TResult<Dummy>;
using OK = types::Ok<Dummy>;

#define CB(type, v) [&](const type& v)
#define DUMMY_CB [&](const Dummy&)
#define ERROR_CB(v) [&](const ManagerError& v)

enum class MessageDirection {
    In,  // To app
    Out, // From app
};

enum class MessageQueue {
    ProcessControlRequest,
    ProcessControlReply,
    GraphicsRequest,
    GraphicsReply,
    Heartbeat,
    TrickleUp,
    TrickleDown,
    AppStatus,
};

const std::map<MessageQueue, MessageDirection> queue_directions = {
    {MessageQueue::ProcessControlRequest, MessageDirection::In},
    {MessageQueue::ProcessControlReply, MessageDirection::Out},
    {MessageQueue::GraphicsRequest, MessageDirection::In},
    {MessageQueue::GraphicsReply, MessageDirection::Out},
    {MessageQueue::Heartbeat, MessageDirection::In},
    {MessageQueue::TrickleUp, MessageDirection::Out},
    {MessageQueue::TrickleDown, MessageDirection::In},
    {MessageQueue::AppStatus, MessageDirection::Out}};

std::set<MessageQueue> get_queues(MessageDirection dir) {
    std::set<MessageQueue> v;
    for (auto kv : queue_directions) {
        if (kv.second == dir) {
            v.insert(kv.first);
        }
    }
    return v;
}

constexpr const char* queue_get_string(MessageQueue q) {
    switch (q) {
    case MessageQueue::ProcessControlRequest:
        return "process_control_request";
    case MessageQueue::ProcessControlReply:
        return "process_control_reply";
    case MessageQueue::GraphicsRequest:
        return "graphics_request";
    case MessageQueue::GraphicsReply:
        return "graphics_reply";
    case MessageQueue::Heartbeat:
        return "heartbeat";
    case MessageQueue::TrickleUp:
        return "trickle_up";
    case MessageQueue::TrickleDown:
        return "trickle_down";
    case MessageQueue::AppStatus:
        return "app_status";
    default:
        throw std::logic_error("");
    }
}

MessageQueue get_queue_id(const Glib::ustring& v) {
    if (v == "process_control_request") return MessageQueue::ProcessControlRequest;
    if (v == "process_control_reply") return MessageQueue::ProcessControlReply;
    if (v == "graphics_request") return MessageQueue::GraphicsRequest;
    if (v == "graphics_reply") return MessageQueue::GraphicsReply;
    if (v == "heartbeat") return MessageQueue::Heartbeat;
    if (v == "trickle_up") return MessageQueue::TrickleUp;
    if (v == "trickle_down") return MessageQueue::TrickleDown;
    if (v == "app_status") return MessageQueue::AppStatus;

    throw std::invalid_argument("Invalid queue ID");
}

using MsgData = std::map<MessageQueue, Glib::ustring>;

static Glib::ustring replace(const Glib::ustring& orig, const Glib::ustring& pattern,
                             const Glib::ustring& replacement) {
    return Glib::Regex::create(pattern)->replace(orig, 0, replacement,
                                                 static_cast<Glib::RegexMatchFlags>(0));
}

Json::Value shmem_to_json(MessageQueue, const std::string& v) {
    auto xmlData = replace(Glib::ustring::compose("%1%2%3", "<root>", v, "</root>"), "\n", "");

    Json::Value out(Json::objectValue);

    try {
        xmlpp::util::EasyDocument doc;
        doc.parse(xmlData);
        auto& root_node = doc();

        xmlpp::util::CallbackMap cb_map;
        cb_map["current_cpu_time"] = [&](const auto& node) {
            out["current_cpu_time"] = xmlpp::util::get_number<double>(node);
        };
        cb_map["checkpoint_cpu_time"] = [&](const auto& node) {
            out["checkpoint_cpu_time"] = xmlpp::util::get_number<double>(node);
        };
        cb_map["fraction_done"] = [&](const auto& node) {
            out["fraction_done"] = xmlpp::util::get_number<double>(node);
        };
        xmlpp::util::map_node(root_node, cb_map);
    } catch (...) {}

    return out;
}

Glib::ustring json_to_shmem(const Json::Value& v) {
    xmlpp::util::EasyDocument doc("ROOT");
    auto& root_node = doc();
    for (const auto& name : v.getMemberNames()) {
        try {
            auto& node = v[name];
            xmlpp::util::write_kv(root_node, name, Glib::ustring::format(name));
        } catch (...) {}
    }
    auto xml_data = doc.write_to_string();

    for (const auto& v : {"<?xml version=\"1.0\" encoding=\"UTF-8\"?>", "<ROOT>", "</ROOT>"}) {
        replace(xml_data, v, "");
    }

    return xml_data;
}

enum class RunMode {
    View,   // View mode - only peek memory buffers
    Edit,   // Exclusive mode - clear buffers on receive, push into in msg channels
    Manage, // Management mode - create and manage memory map
};

CResult check_tool_response(const Json::Value& v) {
    if (v.isMember("error")) {
        return Err(1);
    }
    if (v.isMember("ok")) {
        if (v["ok"].asBool()) {
            return Err(1);
        }

        return OK({});
    }

    return Err(1);
}

TResult<std::string> get_tool_payload(std::iostream& s, Json::Reader& r) {
    std::string str;
    getline(s, str);

    Json::Value v;
    r.parse(str, v);

    TRY(check_tool_response(v));

    return Ok<std::string>(v["payload"].asString());
}

struct ChannelMsgs {
    std::deque<Glib::ustring> msgs;
    std::deque<Glib::ustring> urgent_msgs;
};

class ConnectionManager {
  private:
    std::atomic<bool> want_exit;
    std::atomic<bool> enable_send;

    std::unique_ptr<Json::Writer> json_writer;
    std::unique_ptr<Json::Reader> json_reader;

    MsgData recv_cache;
    std::unique_ptr<redi::pstream> io;
    std::unique_ptr<std::thread> io_loop;
    std::unique_ptr<std::thread> output_loop;
    std::unique_ptr<std::thread> input_loader_loop;

    std::map<MessageQueue, std::unique_ptr<std::thread>> input_push_loops;

    std::map<MessageQueue, ChannelMsgs> channel_msgs;

    std::map<MessageQueue, std::mutex> channel_mutexes;
    std::map<MessageQueue, std::condition_variable> channel_cvs;

  public:
    std::function<void(Json::Value)> out_cb;
    std::function<Json::Value()> in_func;
    int refresh_interval;

    TResult<std::string> receive_payload(MessageQueue q, bool peek = true) {
        Json::Value v;
        v["channel"] = queue_get_string(q);
        v["action"] = peek ? "peek" : "receive";
        *io << this->json_writer->write(v);
        return Ok<std::string>(TRY(get_tool_payload(*io, *json_reader)));
    }

    CResult send_payload(MessageQueue q, const std::string& data, bool force = false) {
        Json::Value v;
        v["channel"] = queue_get_string(q);
        v["action"] = force ? "force" : "send";
        v["payload"] = data;
        *io << this->json_writer->write(v);
        TRY(get_tool_payload(*io, *json_reader));

        return OK({});
    }

    void receive_cycle() {
        for (auto& q : get_queues(MessageDirection::Out)) {
            auto res = this->receive_payload(q, false); // FIXME: allow flushing MSG_CHANNEL
            if (res.isErr()) continue;

            auto pl = res.unwrap();
            if (pl == this->recv_cache[q]) continue;

            if (!pl.empty()) this->out_cb(shmem_to_json(q, pl));
            this->recv_cache[q] = pl;
        }
    }

    void send_cycle() {
        for (auto& kv : this->channel_msgs) {
            auto queue_id = kv.first;
            auto& chan_data = kv.second;

            auto& chan_mutex = this->channel_mutexes[queue_id];
            auto& msgs = chan_data.msgs;
            auto& urgent_msgs = chan_data.urgent_msgs;

            std::lock_guard<std::mutex> lg(chan_mutex);
            if (!urgent_msgs.empty()) {
                send_payload(queue_id, urgent_msgs.front(), true).then(DUMMY_CB {
                    urgent_msgs.pop_front();
                });
            } else {
                if (msgs.empty()) continue;
                send_payload(queue_id, msgs.front()).then(DUMMY_CB { msgs.pop_front(); });
            }
        }
    }

    void io_cycle() {
        receive_cycle();
        if (enable_send) {
            send_cycle();
        }
    }

    ConnectionManager(RunMode mode, const Glib::ustring& tool_path, const Glib::ustring& mmap_path,
                      std::function<Json::Value()> _in_func,
                      std::function<void(Json::Value)> _out_cb, int scan_interval = 250000,
                      std::function<void(MsgData, Json::Value)> poll_cb = nullptr,
                      std::function<void(MsgData, Json::Value)> push_cb = nullptr) {
        this->in_func = _in_func;
        this->out_cb = _out_cb;
        this->json_writer = std::make_unique<Json::FastWriter>();
        this->json_reader = std::make_unique<Json::Reader>();
        this->enable_send = (mode == RunMode::View ? false : true);
        this->io = std::make_unique<redi::pstream>(tool_path + " " + mmap_path);

        io_loop = std::make_unique<std::thread>([this] {
            while (!this->want_exit) {
                io_cycle();
            }
        });
    }
    ~ConnectionManager() {
        want_exit = true;
        io_loop->join();
    }
};
#endif
