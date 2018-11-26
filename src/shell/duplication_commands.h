// Copyright (c) 2017, Xiaomi, Inc.  All rights reserved.
// This source code is licensed under the Apache License Version 2.0, which
// can be found in the LICENSE file in the root directory of this source tree.

#pragma once

#include <fmt/ostream.h>
#include <dsn/utility/errors.h>
#include <dsn/dist/replication/duplication_common.h>

#include "command_executor.h"

inline bool add_dup(command_executor *e, shell_context *sc, arguments args)
{
    if (args.argc <= 2)
        return false;

    std::string app_name = args.argv[1];
    std::string remote_address = args.argv[2];

    static struct option long_options[] = {{"freezed", no_argument, 0, 'f'}, {0, 0, 0, 0}};
    bool freezed = false;
    while (true) {
        int option_index;
        int c = getopt_long(args.argc, args.argv, "f", long_options, &option_index);
        if (c == -1)
            break;
        switch (c) {
        case 'f':
            freezed = true;
            break;
        default:
            return false;
        }
    }

    auto err_resp = sc->ddl_client->add_dup(app_name, remote_address, freezed);
    dsn::error_s err = err_resp.get_error();
    if (err.is_ok()) {
        err = dsn::error_s::make(err_resp.get_value().err);
    }
    if (!err.is_ok()) {
        fmt::print(
            "adding duplication for app [{}] failed, error={}\n", app_name, err.description());
    } else {
        const auto &resp = err_resp.get_value();
        fmt::print("Success for adding duplication [app: {}, remote address: {}, appid: {}, dupid: "
                   "{}, freezed: {}]\n",
                   app_name,
                   remote_address,
                   resp.appid,
                   resp.dupid,
                   freezed);
    }
    return true;
}

inline bool string2dupid(const std::string &str, dsn::replication::dupid_t *dup_id)
{
    bool ok = dsn::buf2int32(str, *dup_id);
    if (!ok) {
        fmt::print(stderr, "parsing {} as positive int failed: {}\n", str);
        return false;
    }
    return true;
}

inline bool query_dup(command_executor *e, shell_context *sc, arguments args)
{
    if (args.argc <= 1)
        return false;

    std::string app_name = args.argv[1];
    auto err_resp = sc->ddl_client->query_dup(app_name);
    dsn::error_s err = err_resp.get_error();
    if (err.is_ok()) {
        err = dsn::error_s::make(err_resp.get_value().err);
    }
    if (!err.is_ok()) {
        fmt::print(
            "querying duplications of app [{}] failed, error={}\n", app_name, err.description());
    } else {
        const auto &resp = err_resp.get_value();
        fmt::print("duplications of app [{}] are listed as below:\n", app_name);
        fmt::print("|{: ^16}|{: ^12}|{: ^24}|{: ^25}|\n",
                   "dup_id",
                   "status",
                   "remote cluster",
                   "create time");
        char create_time[25];
        for (auto info : resp.entry_list) {
            dsn::utils::time_ms_to_date_time(info.create_ts, create_time, sizeof(create_time));
            fmt::print("|{: ^16}|{: ^12}|{: ^24}|{: ^25}|\n",
                       info.dupid,
                       duplication_status_to_string(info.status),
                       info.remote_address,
                       create_time);
        }
    }
    return true;
}

inline bool query_dup_detail(command_executor *e, shell_context *sc, arguments args)
{
    if (args.argc <= 2)
        return false;

    std::string app_name = args.argv[1];

    dsn::replication::dupid_t dup_id;
    if (!string2dupid(args.argv[2], &dup_id)) {
        return false;
    }

    auto err_resp = sc->ddl_client->query_dup(app_name);
    if (!err_resp.is_ok()) {
        fmt::print("querying duplication of [app({}) dupid({})] failed, error={}\n",
                   app_name,
                   dup_id,
                   err_resp.get_error().description());
    } else {
        fmt::print("duplication [{}] of app [{}]:\n", dup_id, app_name);
        const auto &resp = err_resp.get_value();
        for (auto info : resp.entry_list) {
            if (info.dupid == dup_id) {
                fmt::print("{}\n", duplication_entry_to_string(info));
            }
        }
    }
    return true;
}

namespace internal {

inline bool change_dup_status(command_executor *e,
                              shell_context *sc,
                              const arguments &args,
                              duplication_status::type status)
{
    if (args.argc <= 2) {
        return false;
    }

    std::string app_name = args.argv[1];

    dsn::replication::dupid_t dup_id;
    if (!string2dupid(args.argv[2], &dup_id)) {
        return false;
    }

    std::string operation;
    switch (status) {
    case duplication_status::DS_START:
        operation = "starting duplication";
        break;
    case duplication_status::DS_PAUSE:
        operation = "pausing duplication";
        break;
    case duplication_status::DS_REMOVED:
        operation = "removing duplication";
        break;
    default:
        dfatal("unexpected duplication status %d", status);
    }

    auto err_resp = sc->ddl_client->change_dup_status(app_name, dup_id, status);
    dsn::error_s err = err_resp.get_error();
    if (err.is_ok()) {
        err = dsn::error_s::make(err_resp.get_value().err);
    }
    if (err.is_ok()) {
        fmt::print("{}({}) for app [{}] succeed\n", operation, dup_id, app_name);
    } else {
        fmt::print("{}({}) for app [{}] failed, error={}\n",
                   operation,
                   dup_id,
                   app_name,
                   err.description());
    }
    return true;
}

} // namespace internal

inline bool remove_dup(command_executor *e, shell_context *sc, arguments args)
{
    return internal::change_dup_status(e, sc, args, duplication_status::DS_REMOVED);
}

inline bool start_dup(command_executor *e, shell_context *sc, arguments args)
{
    return internal::change_dup_status(e, sc, args, duplication_status::DS_START);
}

inline bool pause_dup(command_executor *e, shell_context *sc, arguments args)
{
    return internal::change_dup_status(e, sc, args, duplication_status::DS_PAUSE);
}