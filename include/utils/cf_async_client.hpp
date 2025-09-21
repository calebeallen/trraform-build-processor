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
#include <nlohmann/json.hpp>

namespace asio = boost::asio;

struct GetParams {
    const std::string bucket;
    const std::string key;
    const bool headOnly = false;
};

struct GetOutcome {
    bool err = false;
    Aws::S3::S3Errors errType = Aws::S3::S3Errors::UNKNOWN;
    std::string errMsg;
    std::unordered_map<std::string, std::string> metadata;
    std::vector<std::uint8_t> body;
};

struct PutParams {
    const std::string bucket;
    const std::string key;
    const std::string contentType;
    const std::vector<std::uint8_t> data;
};

struct PutOutcome {
    bool err = false;
    Aws::S3::S3Errors errType = Aws::S3::S3Errors::UNKNOWN;
    std::string errMsg;
};

class CFAsyncClient {

private:
    std::shared_ptr<Aws::S3::S3Client> _s3Cli;
    Aws::SDKOptions _s3CliOpts;
    std::string _apiToken;

public:
    CFAsyncClient(const std::string&, const std::string&, const std::string&, const std::string&);
    ~CFAsyncClient();
    
    asio::awaitable<GetOutcome> getR2Object(const GetParams&) const;
    asio::awaitable<GetOutcome> headR2Object(const GetParams&) const;
    asio::awaitable<std::vector<GetOutcome>> getManyR2Objects(const std::vector<GetParams>&) const;
    asio::awaitable<PutOutcome> putR2Object(const PutParams&) const;
    asio::awaitable<std::vector<PutOutcome>> putManyR2Objects(const std::vector<PutParams>&) const;
    void purgeCache(const std::vector<std::string>&) const;

};
