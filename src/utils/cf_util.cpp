#include "cf_util.hpp"
#include <string>
#include <aws/s3/S3Client.h>
#include <aws/core/Aws.h>
#include <aws/core/auth/AWSCredentialsProvider.h>
#include <aws/core/client/ClientConfiguration.h>

Aws::SDKOptions awsSdkOpt;
Aws::S3::S3Client* r2Cli = nullptr;

void initR2Cli(){

    Aws::InitAPI(awsSdkOpt);

    Aws::Auth::AWSCredentials creds(std::getenv("CF_R2_ACCESS_KEY"), std::getenv("CF_R2_SECRET_KEY"));
    Aws::Client::ClientConfiguration cfg;
    cfg.endpointOverride = "https://1534f5e1cce37d41a018df4c9716751e.r2.cloudflarestorage.com";
    cfg.region = "auto";
    cfg.scheme = Aws::Http::Scheme::HTTPS;
    r2Cli = new Aws::S3::S3Client(creds, cfg, Aws::Client::AWSAuthV4Signer::PayloadSigningPolicy::Never, false);

}

void closeR2Cli(){

    delete r2Cli;
    r2Cli = nullptr;
    Aws::ShutdownAPI(awsSdkOpt);

}