
#include <memory>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/use_awaitable.hpp>   
#include <boost/asio/async_result.hpp>    
#include <boost/asio/associated_executor.hpp>
#include <boost/asio/experimental/channel.hpp>
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/detached.hpp>
#include <cpr/cpr.h>
#include <aws/core/Aws.h>
#include <aws/s3/S3Client.h>
#include <nlohmann/json.hpp>

#include "utils/cf_async_client.hpp"
#include "config/config.hpp"

asio::awaitable<Aws::S3::Model::GetObjectOutcome> CFAsyncClient::getR2Object(const std::string& bucket, const std::string& key) const {

    co_return co_await asio::async_initiate<decltype(asio::use_awaitable), void(Aws::S3::Model::GetObjectOutcome)>(
        [this, bucket, key](auto&& handler) mutable {
            Aws::S3::Model::GetObjectRequest req;
            req.SetBucket(bucket);
            req.SetKey(key);

            auto exe = asio::get_associated_executor(handler);

            _s3Cli->GetObjectAsync(
                req,
                [handler = std::move(handler), exe](
                    const Aws::S3::S3Client*,
                    const Aws::S3::Model::GetObjectRequest&,
                    const Aws::S3::Model::GetObjectOutcome& out,
                    const std::shared_ptr<const Aws::Client::AsyncCallerContext>&
                ) mutable {
                    asio::post(
                        exe, 
                        [handler = std::move(handler), out]() mutable { 
                            handler(out); 
                    });
                }
            );
        },
        asio::use_awaitable
    );

}

asio::awaitable<std::vector<Aws::S3::Model::GetObjectOutcome>> CFAsyncClient::getManyR2Objects(const std::string& bucket, const std::vector<std::string>& keys) const {

    typedef struct{
        size_t idx;
        Aws::S3::Model::GetObjectOutcome out;
    } GetRes;

    auto exe = co_await asio::this_coro::executor;
    auto channel = std::make_shared<asio::experimental::channel<void(boost::system::error_code, GetRes)>>(exe, keys.size());

    // parallelize request
    for (size_t i = 0; i < keys.size(); ++i) {
        asio::co_spawn(
            exe,
            [this, bucket, keys, i, channel]() -> asio::awaitable<void> {
                auto out = co_await getR2Object(bucket, keys[i]);
                co_await channel->async_send({}, GetRes{i,std::move(out)}, asio::use_awaitable);
            },
            asio::detached
        );
    }

    // wait for all results
    std::vector<Aws::S3::Model::GetObjectOutcome> all(keys.size());

    for (size_t i = 0; i < keys.size(); ++i) {
        auto res = co_await channel->async_receive(asio::use_awaitable);
        all[res.idx] = std::move(res.out);
    }

    channel->close();
    co_return all;
}

asio::awaitable<Aws::S3::Model::PutObjectOutcome> CFAsyncClient::putR2Object(const std::string& bucket, const std::string& key, const std::string& contentType, const std::vector<uint8_t>& data) const {

    co_return co_await asio::async_initiate<decltype(asio::use_awaitable), void(Aws::S3::Model::PutObjectOutcome)>(
        [this, bucket, key, &data](auto&& handler) mutable {
            Aws::S3::Model::PutObjectRequest req;
            req.SetBucket(bucket.c_str());
            req.SetKey(key.c_str());

            // Build body
            auto body = Aws::MakeShared<Aws::StringStream>("PutR2ObjectBody");
            body->write(reinterpret_cast<const char*>(data.data()),
                        static_cast<std::streamsize>(data.size()));
            body->seekg(0);
            req.SetBody(body);
            req.SetContentLength(static_cast<long long>(data.size()));
            req.SetContentType(contentType);
            auto exe = asio::get_associated_executor(handler);

            _s3Cli->PutObjectAsync(
                req,
                [handler = std::move(handler), exe](
                    const Aws::S3::S3Client*,
                    const Aws::S3::Model::PutObjectRequest&,
                    const Outcome& out,
                    const std::shared_ptr<const Aws::Client::AsyncCallerContext>&
                ) mutable {
                    asio::post(
                        exe, 
                        [h2 = std::move(handler), out]() mutable { 
                            h2(out); 
                    });
                }
            );
        },
        asio::use_awaitable
    );

}

void CFAsyncClient::purgeCache(const std::vector<std::string>& urls) const {

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
