#include <catch2/catch.hpp>

#include "faasm_fixtures.h"
#include "utils.h"

#include <faabric/util/environment.h>

namespace tests {
// TEST_CASE_METHOD(MultiRuntimeFunctionExecTestFixture,
//                  "Test stream batch function chaining by pointer",
//                  "[batchfaaslet]")
// {

//     SECTION("WAMR")
//     {
//         faasmConf.wasmVm = "wamr";
//     }

//     std::string user = "demo";
//     std::string func = "hello";
//     for (int i = 0; i < 3; i++) {
//         auto newReq = faabric::util::batchExecFactory();
//         newReq->set_user(user);
//         newReq->set_function(func);
//         newReq->set_appid(100000+i);
//         auto* message = newReq->add_messages();
//         message->set_appid(100000+i);
//         message->set_id(200000+i);
//         message->set_user(user);
//         message->set_function(func);
//         message->set_parallelismid(1);
//         plannerCli.callFunctions(newReq);
//     }

//     // TODO - sometimes, the memory problem happens
// }

TEST_CASE_METHOD(MultiRuntimeFunctionExecTestFixture,
                 "Test stream batch function input",
                 "[testnow]")
{

    SECTION("WAMR")
    {
        faasmConf.wasmVm = "wamr";
    }

    std::string user = "stream";
    std::string func = "function_parstate_source";
    for (int i = 0; i < 3; i++) {
        auto newReq = faabric::util::batchExecFactory();
        newReq->set_user(user);
        newReq->set_function(func);
        newReq->set_appid(100000+i);
        auto* message = newReq->add_messages();
        message->set_appid(100000+i);
        message->set_id(200000+i);
        message->set_user(user);
        message->set_function(func);
        message->set_parallelismid(1);
        // std::string input = "hello world" + std::to_string(i);
        plannerCli.callFunctions(newReq);
    }
}
}
