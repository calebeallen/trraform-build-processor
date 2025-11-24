#pragma once

#include <memory>
#include <vector>
#include <cstdint>
#include <string>
#include <optional>

#include <aws/s3/S3Client.h>
#include <aws/core/Aws.h>
#include <aws/core/auth/AWSCredentials.h>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/thread_pool.hpp>

#include <nlohmann/json.hpp>

namespace asio = boost::asio;

struct GetParams {
    std::string bucket;
    std::string key;
    bool headOnly = false;
};

struct GetOutcome {
    bool err = false;
    Aws::S3::S3Errors errType = Aws::S3::S3Errors::UNKNOWN;
    std::string errMsg;
    std::unordered_map<std::string, std::string> metadata;
    std::vector<uint8_t> body;
};

struct PutParams {
    std::string bucket;
    std::string key;
    std::string contentType;
    std::vector<uint8_t> data;
};

struct PutOutcome {
    bool err = false;
    Aws::S3::S3Errors errType = Aws::S3::S3Errors::UNKNOWN;
    std::string errMsg;
};

class CFAsyncClient {

private:
    std::shared_ptr<Aws::S3::S3Client> _s3Cli;
    asio::thread_pool _threadPool;

public:
    CFAsyncClient(
        const std::string& r2EndPoint,
        const std::string& r2AccessKey,
        const std::string& r2SecretKey,
        int concurrency
    );
    ~CFAsyncClient();
    
    asio::awaitable<GetOutcome> getR2Object(const std::string& bucket, const std::string& key);
    asio::awaitable<GetOutcome> headR2Object(const std::string& bucket, const std::string& key);
    asio::awaitable<PutOutcome> putR2Object(
        const std::string& bucket, 
        const std::string& key,
        const std::string& contentType,
        std::vector<uint8_t> data
    );
    asio::awaitable<std::vector<GetOutcome>> getManyR2Objects(std::vector<GetParams> requests);
    asio::awaitable<std::vector<PutOutcome>> putManyR2Objects(std::vector<PutParams> requests);

};
