/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#pragma once

#include <gtest/gtest.h>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "dsn.layer2_types.h"
#include "rpc/rpc_host_port.h"
#include "utils/ports.h"

// TODO(yingchun): it's too tricky, but I don't know how does it happen, we can fix it later.
#define TRICKY_CODE_TO_AVOID_LINK_ERROR                                                            \
    do {                                                                                           \
        ddl_client_->create_app("", "pegasus", 0, 0, {});                                          \
        pegasus_client_factory::get_client("", "");                                                \
    } while (false)

namespace dsn {
namespace replication {
class replication_ddl_client;
} // namespace replication
} // namespace dsn

namespace pegasus {
class pegasus_client;

class test_util : public ::testing::Test
{
public:
    ~test_util() override = default;

protected:
    test_util(std::map<std::string, std::string> create_envs, std::string cluster_name);
    explicit test_util(const std::map<std::string, std::string> &create_envs);
    test_util();

    static void SetUpTestSuite();

    void SetUp() override;

    static void run_cmd_from_project_root(const std::string &cmd);

    static int get_alive_replica_server_count();

    // Get the leader replica count of the 'replica_server_index' (based on 1) replica server
    // on the 'table_name'.
    static int get_leader_count(const std::string &table_name, int replica_server_index);

    void wait_table_healthy(const std::string &table_name) const;

    // Write some data to table 'table_name_' according to the parameters.
    void
    write_data(const std::string &hashkey_prefix, const std::string &value_prefix, int count) const;
    void write_data(int count) const;

    // Verify the data can be read from the table according to the parameters.
    void verify_data(const std::string &table_name,
                     const std::string &hashkey_prefix,
                     const std::string &value_prefix,
                     int count) const;
    void verify_data(int count) const;
    void verify_data(const std::string &table_name, int count) const;

    // Delete some data from the table according to the parameters.
    void
    delete_data(const std::string &table_name, const std::string &hashkey_prefix, int count) const;

    // Verify the data can NOT be read from the table according to the parameters.
    void check_not_found(const std::string &table_name,
                         const std::string &hashkey_prefix,
                         int count) const;

    void update_table_env(const std::vector<std::string> &keys,
                          const std::vector<std::string> &values) const;

    // REQUIRED: `buffer_` has been filled with random chars.
    [[nodiscard]] std::string random_string() const
    {
        const auto pos = random() % buffer_.size();
        const auto length = random() % buffer_.size() + 1;
        if (pos + length < buffer_.size()) {
            return {buffer_.data() + pos, length};
        }

        return std::string(buffer_.data() + pos, buffer_.size() - pos) +
               std::string(buffer_.data(), length + pos - buffer_.size());
    }

    void fill_random()
    {
        srandom(static_cast<unsigned int>(time(nullptr)));
        for (auto &c : buffer_) {
            c = kCharSet[random() % kCharSet.size()];
        }
    }

    enum class OperateDataType
    {
        kSet,
        kGet,
        kDelete,
        kCheckNotFound
    };
    std::map<test_util::OperateDataType, std::string> kOpNames;
    void operate_data(OperateDataType type,
                      const std::string &table_name,
                      const std::string &hashkey_prefix,
                      const std::optional<std::string> &value_prefix,
                      int count) const;

    static constexpr std::string_view kCharSet =
        "_0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";

    const std::string kClusterName;
    const std::string kHashkeyPrefix;
    const std::string kSortkey;
    const std::string kValuePrefix;
    const std::map<std::string, std::string> kCreateEnvs;
    std::string table_name_;
    int32_t table_id_{0};
    int32_t partition_count_{8};
    std::vector<dsn::partition_configuration> pcs_;
    pegasus_client *client_ = nullptr;
    std::vector<dsn::host_port> meta_list_;
    std::shared_ptr<dsn::replication::replication_ddl_client> ddl_client_;
    std::array<char, 256> buffer_{};

private:
    DISALLOW_COPY_AND_ASSIGN(test_util);
    DISALLOW_MOVE_AND_ASSIGN(test_util);
};

} // namespace pegasus
