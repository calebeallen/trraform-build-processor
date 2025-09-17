#pragma once

#include <memory>
#include <vector>
#include <cstdint>
#include <string>

#include <aws/s3/S3Client.h>
#include <aws/core/Aws.h>
#include <aws/core/auth/AWSCredentials.h>
#include <boost/asio/awaitable.hpp>
#include <nlohmann/json.hpp>

namespace asio = boost::asio;

class CFAsyncClient {

private:
    std::shared_ptr<Aws::S3::S3Client> _s3Cli;
    Aws::SDKOptions _s3CliOpts;
    std::string _apiToken;

public:
    CFAsyncClient(const std::string&, const std::string&, const std::string&, const std::string&);
    ~CFAsyncClient();
    
    asio::awaitable<Aws::S3::Model::GetObjectOutcome> getR2Object(const std::string&, const std::string&) const;
    asio::awaitable<std::vector<Aws::S3::Model::GetObjectOutcome>> getManyR2Objects(const std::string&, const std::vector<std::string>&) const;
    asio::awaitable<Aws::S3::Model::PutObjectOutcome> putR2Object(const std::string&, const std::string&, const std::string&, const std::vector<uint8_t>&) const;
    asio::awaitable<std::vector<Aws::S3::Model::PutObjectOutcome>> putManyR2Objects(const std::string&, const std::vector<std::string>&, const std::string&, const std::vector<std::vector<uint8_t>>&) const;
    void purgeCache(const std::vector<std::string>&) const;

};
