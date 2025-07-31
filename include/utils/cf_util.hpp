#pragma once
#include <memory>
#include <aws/s3/S3Client.h>
#include <aws/core/Aws.h>

extern Aws::SDKOptions awsSdkOpt;
extern Aws::S3::S3Client* r2Cli;

void initR2Cli();
void closeR2Cli();
