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

class CFAsyncClient {

public:
    struct GetParams {
        std::string bucket;
        std::string key;
        bool headOnly = false;
        bool useCache = false;
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
        bool useCache = false;
    };

    struct PutOutcome {
        bool err = false;
        Aws::S3::S3Errors errType = Aws::S3::S3Errors::UNKNOWN;
        std::string errMsg;
    };

    CFAsyncClient(
        const std::string& r2EndPoint,
        const std::string& r2AccessKey,
        const std::string& r2SecretKey,
        const std::string& cfApiToken,
        size_t concurrency,
        bool cacheEnabled = false,
        size_t cacheCapacity = 0
    );
    ~CFAsyncClient();
    
    asio::awaitable<GetOutcome> getR2Object(const std::string& bucket, const std::string& key, const bool useCache = false);
    asio::awaitable<GetOutcome> headR2Object(const std::string& bucket, const std::string& key);
    asio::awaitable<PutOutcome> putR2Object(
        const std::string& bucket, 
        const std::string& key,
        const std::string& contentType,
        std::vector<uint8_t>&& data,
        const bool useCache = false
    );
    asio::awaitable<std::vector<GetOutcome>> getManyR2Objects(std::vector<GetParams>&& requests);
    asio::awaitable<std::vector<PutOutcome>> putManyR2Objects(std::vector<PutParams>&& requests);
    asio::awaitable<void> purgeCache(const std::vector<std::string>&& urls);

private:
    std::shared_ptr<Aws::S3::S3Client> _s3Cli;
    asio::thread_pool _threadPool;
    std::string _cfApiToken;

    bool _cacheEnabled;
    size_t _cacheCapacity, _cacheSize;
    std::list<std::string> _cacheEvictQueue;
    std::unordered_map<std::string, std::pair<std::list<std::string>::iterator, GetOutcome>> _cache;

};
