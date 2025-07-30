#pragma once
#include <memory>
#include <aws/s3/S3Client.h>

extern std::shared_ptr<Aws::S3::S3Client> r2Cli;

void initS3Client(const std::string& accessKey, const std::string& secretKey, const std::string& endpoint);