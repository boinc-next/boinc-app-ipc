// This file is part of boinc-ipc-manager.
// https://boinc-next.github.io
// Copyright (C) 2016 BOINC Next project
//
// boinc-ipc-manager is free software; you can redistribute it and/or modify it
// under the terms of the GNU Lesser General Public License
// as published by the Free Software Foundation,
// either version 3 of the License, or (at your option) any later version.
//
// boinc-ipc-manager is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
// See the GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with boinc-ipc-manager.  If not, see <http://www.gnu.org/licenses/>.

#include "manager.hpp"

#include <iostream>

using namespace std;
using namespace Glib;

const auto MMAPPED_FILE_NAME = "boinc_mmap_file";

void print_shmem_var(const ustring& title, const MsgData& data, MessageQueue t, ostream& out,
                     bool& put_newline) {
    try {
        auto msg = ustring::compose("%1: %2", title, data.at(t));
        if (not put_newline) {
            out << endl;
            put_newline = true;
        };
        out << msg << endl;
    } catch (...) {
    }
}

void print_shmem_msgs(MsgData data, ostream& out) {
    bool newline = false;
    auto func = [&](const ustring& k, MsgData v, MessageQueue q, ostream& out) {
        print_shmem_var(k, v, q, out, newline);
    };
    func("process_control_request", data, MessageQueue::ProcessControlRequest, out);
    func("process_control_reply", data, MessageQueue::ProcessControlReply, out);
    func("graphics_request", data, MessageQueue::GraphicsRequest, out);
    func("graphics_reply", data, MessageQueue::GraphicsReply, out);
    func("trickle_up", data, MessageQueue::TrickleUp, out);
    func("heartbeat", data, MessageQueue::Heartbeat, out);
    func("trickle_down", data, MessageQueue::TrickleDown, out);
    func("app_status", data, MessageQueue::AppStatus, out);
}

#ifndef ADD_ARG
#define ADD_ARG(k, desc) add_arg(k##_arg, k, #k, desc)
#endif

class Options {
  private:
    OptionContext context;
    OptionGroup main_group;

    OptionEntry mode_arg;
    OptionEntry tool_path_arg;
    OptionEntry mmap_path_arg;
    OptionEntry debug_arg;

    template <typename T>
    inline void add_arg(OptionEntry& e, T& storage, const ustring& k, const ustring& desc) {
        e.set_long_name(k);
        e.set_arg_description(desc);
        main_group.add_entry(e, storage);
    }
    void init() { debug = false; }

  public:
    ustring mode;
    ustring tool_path;
    ustring mmap_path;
    bool debug;

    Options(int& argc, char**& argv)
        : context("IPC manager for BOINC applications"), main_group("", "") {
        init();

        context.set_main_group(main_group);
        context.set_strict_posix(true);
        context.set_ignore_unknown_options(true);

        ADD_ARG(mode, "select run mode: view, edit or manage");
        ADD_ARG(tool_path, "path to interactive console");
        ADD_ARG(mmap_path, "path to mmap");
        ADD_ARG(debug, "print debug output");

        context.parse(argc, argv);
    }
};

RunMode parse_run_mode_string(const ustring& mode_string) {
    if (mode_string == "edit")
        return RunMode::Edit;
    if (mode_string == "manage")
        return RunMode::Manage;

    return RunMode::View;
}

int normalize_options(Options& options) {
    if (options.tool_path.empty()) {
        options.tool_path = "/usr/bin/boinc-shmem-tool";
    }

    if (options.mmap_path.empty()) {
        cerr << "Please specify mmap path" << endl;
        return EBADF;
    }

    return 0;
}

int main(int argc, char* argv[]) {
    Options options(argc, argv);
    auto normalize_err = normalize_options(options);
    if (normalize_err) {
        std::cerr << "Invalid options" << std::endl;
        return normalize_err;
    }

    auto mmap_path = ustring::compose("%1/%2", options.mmap_path, MMAPPED_FILE_NAME);

    Json::FastWriter writer;
    Json::Reader reader;

    auto in_func = [&] {
        string str;
        getline(cin, str);
        Json::Value v;
        reader.parse(str, v);
        return v;
    };
    auto out_func = [&](Json::Value v) {
        cout << writer.write(v);
        cout.flush();
    };
    function<void(MsgData, Json::Value)> debug_cb;
    if (options.debug) {
        debug_cb = [](MsgData data, Json::Value v) {
            cout << ".";
            cout.flush();
            print_shmem_msgs(data, cout);
        };
    }

    unique_ptr<ConnectionManager> manager;
    try {
        manager =
            make_unique<ConnectionManager>(parse_run_mode_string(options.mode), options.tool_path,
                                           mmap_path, in_func, out_func, 250000, debug_cb);
    } catch (const system_error& e) {
        if (e.code().value() == EBADF) {
            cerr << ustring::compose("Failed to open file descriptor: %1", e.what()) << endl;
            return EBADF;
        } else {
            throw;
        }
    }

    for (;;) {
        pause();
    }
}
