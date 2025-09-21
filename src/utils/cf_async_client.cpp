
#include <memory>
#include <vector>
#include <unordered_map>

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
#include <aws/s3/model/HeadObjectRequest.h>
#include <aws/s3/model/HeadObjectResult.h>
#include <aws/s3/model/GetObjectRequest.h>
#include <aws/s3/model/PutObjectRequest.h>
#include <aws/s3/model/GetObjectResult.h>
#include <aws/s3/model/PutObjectResult.h>
#include <aws/s3/S3Client.h>
#include <nlohmann/json.hpp>

#include "utils/cf_async_client.hpp"
#include "config/config.hpp"

CFAsyncClient::CFAsyncClient(
    const std::string& r2EndPoint,
    const std::string& r2AccessKey,
    const std::string& r2SecretKey,
    const std::string& apiToken
) {
    _apiToken = apiToken;

    Aws::InitAPI(_s3CliOpts);

    Aws::Client::ClientConfiguration config;
    config.region = "auto";
    config.endpointOverride = r2EndPoint;
    config.scheme = Aws::Http::Scheme::HTTPS;

    Aws::Auth::AWSCredentials creds(r2AccessKey, r2SecretKey);

    _s3Cli = std::make_shared<Aws::S3::S3Client>(
        creds,
        config,
        Aws::Client::AWSAuthV4Signer::PayloadSigningPolicy::Never,
        false
    );
}

CFAsyncClient::~CFAsyncClient() {
    Aws::ShutdownAPI(_s3CliOpts);
}

asio::awaitable<GetOutcome> CFAsyncClient::getR2Object(const GetParams& params) const {

    GetOutcome obj;

    co_await asio::async_initiate<decltype(asio::use_awaitable), void()>(
        [this, &params, &obj](auto&& handler) mutable {
            Aws::S3::Model::GetObjectRequest req;
            req.SetBucket(params.bucket);
            req.SetKey(params.key);

            // make handler copyable
            using HandlerT = std::decay_t<decltype(handler)>;
            std::shared_ptr<HandlerT> hptr = std::make_shared<HandlerT>(std::move(handler));
 
            _s3Cli->GetObjectAsync(req,
                [hptr, &obj](
                    const Aws::S3::S3Client*,
                    const Aws::S3::Model::GetObjectRequest&,
                    const Aws::S3::Model::GetObjectOutcome& awsOut,
                    const std::shared_ptr<const Aws::Client::AsyncCallerContext>&
                ) mutable {
                    if (awsOut.IsSuccess()) {
                        const auto& res = awsOut.GetResult();
                        for (const auto& [k, v] : res.GetMetadata())
                            obj.metadata.emplace(k, v);

                        auto& body = res.GetBody();  
                        obj.body.reserve(res.GetContentLength());
                        obj.body.assign(std::istreambuf_iterator<char>(body), std::istreambuf_iterator<char>());        
                    } else {
                        obj.err = true;
                        const auto& err = awsOut.GetError();
                        obj.errType = err.GetErrorType();
                        obj.errMsg = "R2 GetObject error: " + err.GetMessage();
                    }
                    
                    asio::post(
                        asio::get_associated_executor(*hptr), 
                        [hptr]() mutable { (*hptr)();}
                    );
                }
            );
        },
        asio::use_awaitable
    );

    co_return obj;

}


asio::awaitable<GetOutcome> CFAsyncClient::headR2Object(const GetParams& params) const {

    GetOutcome obj;

    co_await asio::async_initiate<decltype(asio::use_awaitable), void()>(
        [this, &params, &obj](auto&& handler) mutable {
            Aws::S3::Model::HeadObjectRequest req;
            req.SetBucket(params.bucket);
            req.SetKey(params.key);

            // make handler copyable
            using HandlerT = std::decay_t<decltype(handler)>;
            std::shared_ptr<HandlerT> hptr = std::make_shared<HandlerT>(std::move(handler));

            _s3Cli->HeadObjectAsync(req,
                [hptr, &obj](
                    const Aws::S3::S3Client*,
                    const Aws::S3::Model::HeadObjectRequest&,
                    const Aws::S3::Model::HeadObjectOutcome& awsOut,
                    const std::shared_ptr<const Aws::Client::AsyncCallerContext>&
                ) mutable {
                    if (awsOut.IsSuccess()) {
                        const auto& res = awsOut.GetResult();
                        for (const auto& [k, v] : res.GetMetadata())
                            obj.metadata.emplace(k, v);

                    } else {
                        obj.err = true;
                        const auto& err = awsOut.GetError();
                        obj.errType = err.GetErrorType();
                        obj.errMsg = "R2 GetObject error: " + err.GetMessage();
                    }
                    
                    asio::post(
                        asio::get_associated_executor(*hptr), 
                        [hptr]() mutable { (*hptr)();}
                    );
                }
            );
        },
        asio::use_awaitable
    );

    co_return obj;
}



asio::awaitable<PutOutcome> CFAsyncClient::putR2Object(const PutParams& params) const {

    PutOutcome obj;

    co_await asio::async_initiate<decltype(asio::use_awaitable), void()>(
        [this, &params, &obj](auto&& handler) mutable {
            Aws::S3::Model::PutObjectRequest req;
            req.SetBucket(params.bucket);
            req.SetKey(params.key);

            // Build body
            auto body = Aws::MakeShared<Aws::StringStream>("PutR2ObjectBody");
            body->write(
                reinterpret_cast<const char*>(params.data.data()), 
                static_cast<std::streamsize>(params.data.size())
            );
            body->seekg(0);
            req.SetBody(body);
            req.SetContentLength(static_cast<long long>(params.data.size()));
            req.SetContentType(params.contentType);

            auto exe = asio::get_associated_executor(handler);

            // handler is non-copyable, make shared ptr so that lambda capture can be copied
            using HandlerT = std::decay_t<decltype(handler)>;
            std::shared_ptr<HandlerT> hptr = std::make_shared<HandlerT>(std::move(handler));

            _s3Cli->PutObjectAsync(
                req,
                [hptr, &obj](
                    const Aws::S3::S3Client*,
                    const Aws::S3::Model::PutObjectRequest&,
                    const Aws::S3::Model::PutObjectOutcome& out,
                    const std::shared_ptr<const Aws::Client::AsyncCallerContext>&
                ) mutable {
                    
                    if (!out.IsSuccess()) {
                        const auto& err = out.GetError();
                        obj.err = true;
                        obj.errType = err.GetErrorType();
                        obj.errMsg = "R2 PutObject error: " + err.GetMessage();
                    }

                    asio::post(
                        asio::get_associated_executor(*hptr), 
                        [hptr]() mutable {(*hptr)();}
                    );
                }
            );
        },
        asio::use_awaitable
    );

    co_return obj;
}

asio::awaitable<std::vector<GetOutcome>> CFAsyncClient::getManyR2Objects(const std::vector<GetParams>& requests) const {

    auto exe = co_await asio::this_coro::executor;
    asio::experimental::channel<void(boost::system::error_code, int)> channel(exe, requests.size());
    std::vector<GetOutcome> results(requests.size());

    // parallelize request
    for (size_t i = 0; i < requests.size(); ++i)
        asio::co_spawn(
            exe,
            [this, i, &requests, &channel, &results]() -> asio::awaitable<void> {
                results[i] = co_await getR2Object(requests[i]);
                co_await channel.async_send({}, 0, asio::use_awaitable);
            },
            asio::detached
        );

    // wait for all results
    for (size_t i = 0; i < requests.size(); ++i) 
        co_await channel.async_receive(asio::use_awaitable);

    channel.close();
    co_return results;
}

asio::awaitable<std::vector<PutOutcome>> CFAsyncClient::putManyR2Objects(const std::vector<PutParams>& requests) const {

    auto exe = co_await asio::this_coro::executor;
    asio::experimental::channel<void(boost::system::error_code, int)> channel(exe, requests.size());
    std::vector<PutOutcome> results(requests.size());

    // parallelize request
    for (size_t i = 0; i < requests.size(); ++i)
        asio::co_spawn(
            exe,
            [this, i, &requests, &channel, &results]() -> asio::awaitable<void> {
                results[i] = co_await putR2Object(requests[i]);
                co_await channel.async_send({}, 0, asio::use_awaitable);
            },
            asio::detached
        );

    // wait for all results
    for (size_t i = 0; i < requests.size(); ++i) 
        co_await channel.async_receive(asio::use_awaitable);

    channel.close();
    co_return results;
    
};

// void CFAsyncClient::purgeCache(const std::vector<std::string>& urls) const {

//     nlohmann::json payload;
//     nlohmann::json filesArray = nlohmann::json::array();

//     for (const auto& url : urls) {
//         filesArray.push_back({
//             {"url", url},
//             {"headers", {
//                 {"Origin", VARS::ORIGIN}
//             }}
//         });
//     }
//     payload["files"] = filesArray;

//     std::string apiUrl = "https://api.cloudflare.com/client/v4/zones/" + VARS::CF_ZONE_ID + "/purge_cache";
//     cpr::Response r = cpr::Post(
//         cpr::Url{apiUrl},
//         cpr::Header{
//             {"Authorization", "Bearer " + _apiToken},
//             {"Content-Type", "application/json"}
//         },
//         cpr::Body{payload.dump()}
//     );

//     if (r.status_code != 200) {
//         std::cerr << "API request failed with status code " + std::to_string(r.status_code) + ": " + r.text;
//         return;
//     }

//     try {
//         nlohmann::json purgeRes = nlohmann::json::parse(r.text);

//         if (!purgeRes.value("success", false)) {
//             std::string errorMsg = "Cloudflare API reported a failure.";
//             if (purgeRes.contains("errors")) {
//                 errorMsg += " Errors: " + purgeRes["errors"].dump();
//             }
//             std::cerr << errorMsg;
//         }
//     } catch (const nlohmann::json::parse_error& e) {
//         std::cerr << "Failed to parse JSON response: " + std::string(e.what());
//     }

// }
