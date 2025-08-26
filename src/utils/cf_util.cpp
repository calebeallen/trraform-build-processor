
#include <string>

#include <aws/s3/S3Client.h>
#include <aws/core/Aws.h>
#include <aws/core/auth/AWSCredentialsProvider.h>
#include <aws/core/client/ClientConfiguration.h>
#include <nlohmann/json.hpp>
#include <cpr/cpr.h>

#include "cf_util.hpp"
#include "constants.hpp"

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

void purgeCacheCDN(const std::vector<std::string>& urls) {

    nlohmann::json payload;
    nlohmann::json filesArray = nlohmann::json::array();

    for (const auto& url : urls) {
        filesArray.push_back({
            {"url", url},
            {"headers", {
                {"Origin", VARS::ORIGIN}
            }}
        });
    }
    payload["files"] = filesArray;

    std::string apiUrl = "https://api.cloudflare.com/client/v4/zones/" + VARS::CF_ZONE_ID + "/purge_cache";
    cpr::Response r = cpr::Post(
        cpr::Url{apiUrl},
        cpr::Header{
            {"Authorization", "Bearer " + ENV::CF_API_TOKEN},
            {"Content-Type", "application/json"}
        },
        cpr::Body{payload.dump()}
    );

    if (r.status_code != 200) {
        std::cerr << "API request failed with status code " + std::to_string(r.status_code) + ": " + r.text;
        return;
    }

    try {
        nlohmann::json purgeRes = nlohmann::json::parse(r.text);

        if (!purgeRes.value("success", false)) {
            std::string errorMsg = "Cloudflare API reported a failure.";
            if (purgeRes.contains("errors")) {
                errorMsg += " Errors: " + purgeRes["errors"].dump();
            }
            std::cerr << errorMsg;
        }
    } catch (const nlohmann::json::parse_error& e) {
        std::cerr << "Failed to parse JSON response: " + std::string(e.what());
    }

}