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

#include <atomic>
#include <climits>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "client/replication_ddl_client.h"
#include "gtest/gtest.h"
#include "include/pegasus/client.h"
#include "pegasus/error.h"
#include "shell/command_helper.h"
#include "task/async_calls.h"
#include "test/function_test/utils/test_util.h"
#include "test/function_test/utils/utils.h"
#include "test_util/test_util.h"
#include "utils/error_code.h"
#include "utils/fmt_logging.h"
#include "utils/test_macros.h"

namespace pegasus {

class copy_data_test : public test_util
{
protected:
    void SetUp() override
    {
        SET_UP_BASE(test_util);
        ASSERT_NO_FATAL_FAILURE(create_table_and_get_client());
        ASSERT_NO_FATAL_FAILURE(fill_data());
    }

    void TearDown() override
    {
        test_util::TearDown();
        ASSERT_EQ(dsn::ERR_OK, ddl_client_->drop_app(source_app_name, 0));
        ASSERT_EQ(dsn::ERR_OK, ddl_client_->drop_app(destination_app_name, 0));
    }

    void verify_data()
    {
        pegasus_client::scan_options options;
        std::vector<pegasus_client::pegasus_scanner *> scanners;
        ASSERT_EQ(PERR_OK, destination_client_->get_unordered_scanners(INT_MAX, options, scanners));

        std::string hash_key;
        std::string sort_key;
        std::string value;
        std::map<std::string, std::map<std::string, std::string>> actual_data;
        for (auto *scanner : scanners) {
            ASSERT_NE(nullptr, scanner);
            int ret = PERR_OK;
            while (PERR_OK == (ret = (scanner->next(hash_key, sort_key, value)))) {
                check_and_put(actual_data, hash_key, sort_key, value);
            }
            ASSERT_EQ(PERR_SCAN_COMPLETE, ret);
            delete scanner;
        }

        ASSERT_NO_FATAL_FAILURE(compare(expect_data_, actual_data));
    }

    void create_table_and_get_client()
    {
        ASSERT_EQ(dsn::ERR_OK,
                  ddl_client_->create_app(source_app_name, "pegasus", default_partitions, 3, {}));
        ASSERT_EQ(
            dsn::ERR_OK,
            ddl_client_->create_app(destination_app_name, "pegasus", default_partitions, 3, {}));
        source_client_ =
            pegasus_client_factory::get_client(kClusterName.c_str(), source_app_name.c_str());
        ASSERT_NE(nullptr, source_client_);
        destination_client_ =
            pegasus_client_factory::get_client(kClusterName.c_str(), destination_app_name.c_str());
        ASSERT_NE(nullptr, destination_client_);
    }

    void fill_data()
    {
        fill_random();

        std::string hash_key;
        std::string sort_key;
        std::string value;
        while (expect_data_[empty_hash_key].size() < 1000) {
            sort_key = random_string();
            value = random_string();
            ASSERT_EQ(PERR_OK, source_client_->set(empty_hash_key, sort_key, value))
                << "hash_key=" << hash_key << ", sort_key=" << sort_key;
            expect_data_[empty_hash_key][sort_key] = value;
        }

        while (expect_data_.size() < 500) {
            hash_key = random_string();
            while (expect_data_[hash_key].size() < 10) {
                sort_key = random_string();
                value = random_string();
                ASSERT_EQ(PERR_OK, source_client_->set(hash_key, sort_key, value))
                    << "hash_key=" << hash_key << ", sort_key=" << sort_key;
                expect_data_[hash_key][sort_key] = value;
            }
        }
    }

    const std::string empty_hash_key;
    const std::string source_app_name = "copy_data_source_table";
    const std::string destination_app_name = "copy_data_destination_table";

    const int max_batch_count = 500;
    const int timeout_ms = 5000;
    const int max_multi_set_concurrency = 20;
    const int32_t default_partitions = 4;

    std::map<std::string, std::map<std::string, std::string>> expect_data_;

    pegasus_client *source_client_{nullptr};
    pegasus_client *destination_client_{nullptr};
};

TEST_F(copy_data_test, EMPTY_HASH_KEY_COPY)
{
    LOG_INFO("TESTING_COPY_DATA, EMPTY HASH_KEY COPY ....");

    pegasus_client::scan_options options;
    options.return_expire_ts = true;
    std::vector<pegasus::pegasus_client::pegasus_scanner *> raw_scanners;
    ASSERT_EQ(PERR_OK, source_client_->get_unordered_scanners(INT_MAX, options, raw_scanners));

    LOG_INFO("open source app scanner succeed, partition_count = {}", raw_scanners.size());

    std::vector<pegasus::pegasus_client::pegasus_scanner_wrapper> scanners;
    for (auto *raw_scanner : raw_scanners) {
        ASSERT_NE(nullptr, raw_scanner);
        scanners.push_back(raw_scanner->get_smart_wrapper());
    }
    raw_scanners.clear();

    int split_count = scanners.size();
    LOG_INFO("prepare scanners succeed, split_count = {}", split_count);

    std::atomic_bool error_occurred(false);
    std::vector<std::unique_ptr<scan_data_context>> contexts;

    for (int i = 0; i < split_count; i++) {
        auto *context = new scan_data_context(SCAN_AND_MULTI_SET,
                                              i,
                                              max_batch_count,
                                              timeout_ms,
                                              scanners[i],
                                              destination_client_,
                                              nullptr,
                                              &error_occurred,
                                              max_multi_set_concurrency);
        contexts.emplace_back(context);
        dsn::tasking::enqueue(LPC_SCAN_DATA, nullptr, std::bind(scan_multi_data_next, context));
    }

    // wait thread complete
    ASSERT_IN_TIME(
        [&] {
            int completed_split_count = 0;
            for (int i = 0; i < split_count; i++) {
                if (contexts[i]->split_completed.load()) {
                    completed_split_count++;
                }
            }
            ASSERT_EQ(completed_split_count, split_count);
        },
        120);

    ASSERT_FALSE(error_occurred.load()) << "error occurred, processing terminated or timeout!";
    ASSERT_NO_FATAL_FAILURE(verify_data());
}

} // namespace pegasus
