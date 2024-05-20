#include <conf/FaasmConfig.h>
#include <faabric/executor/ExecutorContext.h>
#include <faabric/planner/PlannerClient.h>
#include <faabric/util/ExecGraph.h>
#include <faabric/util/batch.h>
#include <faabric/util/bytes.h>
#include <faabric/util/func.h>
#include <faabric/util/logging.h>
#include <wasm/WasmExecutionContext.h>
#include <wasm/WasmModule.h>
#include <wasm/chaining.h>

#define STREAM_BATCH -2

namespace wasm {
int awaitChainedCall(unsigned int messageId)
{
    int callTimeoutMs = conf::getFaasmConfig().chainedCallTimeout;
    auto* exec = faabric::executor::ExecutorContext::get()->getExecutor();

    int returnCode = 1;
    try {
        int appId = exec->getBoundMessage().appid();
        auto& plannerCli = faabric::planner::getPlannerClient();
        const faabric::Message result =
          plannerCli.getMessageResult(appId, messageId, callTimeoutMs);
        returnCode = result.returnvalue();
    } catch (faabric::executor::ChainedCallException& ex) {
        SPDLOG_ERROR(
          "Error getting chained call message: {}: {}", messageId, ex.what());
    } catch (faabric::redis::RedisNoResponseException& ex) {
        SPDLOG_ERROR("Timed out waiting for chained call: {}", messageId);
    } catch (std::exception& ex) {
        SPDLOG_ERROR("Non-timeout exception waiting for chained call: {}",
                     ex.what());
    }

    return returnCode;
}

int makeChainedCall(const std::string& functionName,
                    int wasmFuncPtr,
                    const char* pyFuncName,
                    const std::vector<uint8_t>& inputData,
                    int msgIdx)
{
    faabric::Message* originalCall;
    if (faabric::executor::ExecutorContext::get()->getMsgIdx() ==
        STREAM_BATCH) {
        SPDLOG_TRACE("S - chained_call STREAM_BATCH");
        std::shared_ptr<faabric::executor::ExecutorContext> context =
          faabric::executor::ExecutorContext::get();
        context->chainedFunctionName.push_back(functionName);
        context->chainedMsgId.push_back(msgIdx);
        context->chainedInput.push_back(inputData);
        return 0;
    } else {
        originalCall = &faabric::executor::ExecutorContext::get()->getMsg();
    }

    std::string user = originalCall->user();

    assert(!user.empty());
    assert(!functionName.empty());

    // Spawn a child batch exec (parent-child expressed by having the same app
    // id)
    std::shared_ptr<faabric::BatchExecuteRequest> req =
      faabric::util::batchExecFactory(originalCall->user(), functionName, 1);
    faabric::util::updateBatchExecAppId(req, originalCall->appid());

    // Propagate chaining-specific fields
    faabric::Message& msg = req->mutable_messages()->at(0);
    msg.set_inputdata(inputData.data(), inputData.size());
    msg.set_funcptr(wasmFuncPtr);
    msg.set_chainedid(originalCall->chainedid());

    // Propagate the command line if needed
    msg.set_cmdline(originalCall->cmdline());

    // Python properties
    msg.set_pythonuser(originalCall->pythonuser());
    msg.set_pythonfunction(originalCall->pythonfunction());
    if (pyFuncName != nullptr) {
        msg.set_pythonentry(pyFuncName);
    }
    msg.set_ispython(originalCall->ispython());

    if (originalCall->recordexecgraph()) {
        msg.set_recordexecgraph(true);
    }

    if (msg.funcptr() == 0) {
        SPDLOG_INFO("Chaining call {}/{} -> {}/{} (ids: {} -> {})",
                    originalCall->user(),
                    originalCall->function(),
                    msg.user(),
                    msg.function(),
                    originalCall->id(),
                    msg.id());
    } else {
        SPDLOG_INFO("Chaining nested call {}/{} (ids: {} -> {})",
                    msg.user(),
                    msg.function(),
                    originalCall->id(),
                    msg.id());
    }

    // Record the chained call in the executor before invoking the new
    // functions to avoid data races
    faabric::executor::ExecutorContext::get()->getExecutor()->addChainedMessage(
      req->messages(0));

    auto& plannerCli = faabric::planner::getPlannerClient();
    plannerCli.callFunctions(req);
    if (originalCall->recordexecgraph()) {
        faabric::util::logChainedFunction(*originalCall, msg);
    }

    return msg.id();
}

int makeChainedCallBatch()
{
    // This function is only designed for the STREAM_BATCH mode
    if (faabric::executor::ExecutorContext::get()->getMsgIdx() !=
        STREAM_BATCH){
        return 0;
    }
    SPDLOG_TRACE("S - chained_call_back_invoke");
    std::shared_ptr<faabric::executor::ExecutorContext> context =
      faabric::executor::ExecutorContext::get();
    std::vector<std::string> chainedFunctionName = context->chainedFunctionName;
    std::vector<int> chainedMsgId = context->chainedMsgId;
    std::vector<std::vector<uint8_t>> chainedInput = context->chainedInput;

    // Prepare the batch execute request
    // Since each executor can execute the same function with different
    // appid, msgid. They can also chained call different functions.
    // We have to prepare a map to store the request for each function.
    // For each function, we also have to prepare for each appid. Since
    // currently, planner only supports batchcall with the same AppId.
    std::map<std::string,
             std::map<int, std::shared_ptr<faabric::BatchExecuteRequest>>>
      reqsMap;
    // Prepare each message
    for (size_t i = 0; i < chainedMsgId.size(); i++) {
        std::string functionName = chainedFunctionName.at(i);
        int msgIdx = chainedMsgId.at(i);
        const std::vector<uint8_t>& inputData = chainedInput.at(i);

        // Get the original call
        faabric::Message* originalCall =
          &faabric::executor::ExecutorContext::get()->getBatch().mutable_messages()->at(msgIdx);
        std::string user = originalCall->user();
        int appId = originalCall->appid();

        assert(!user.empty());
        assert(!functionName.empty());

        // Spawn a child batch exec (parent-child expressed by having the same
        // app id)
        std::shared_ptr<faabric::BatchExecuteRequest> req;
        faabric::Message* msgPtr = nullptr;
        // If the reqsMap contains the function name and contains the appid
        if (reqsMap.contains(functionName) &&
            reqsMap[functionName].contains(appId)) {
            req = reqsMap[functionName][appId];
            *req->add_messages() =
              faabric::util::messageFactory(user, functionName);
            msgPtr = &req->mutable_messages()->at(req->messages_size() - 1);
            msgPtr->set_appid(appId);
        } else {
            req = faabric::util::batchExecFactory(
              originalCall->user(), functionName, 1);
            faabric::util::updateBatchExecAppId(req, originalCall->appid());
            reqsMap[functionName][appId] = req;
            msgPtr = &req->mutable_messages()->at(0);
        }

        faabric::Message &msg = *msgPtr;
        // Propagate chaining-specific fields
        msg.set_inputdata(inputData.data(), inputData.size());
        msg.set_funcptr(0);
        msg.set_chainedid(originalCall->chainedid());

        // Propagate the command line if needed
        msg.set_cmdline(originalCall->cmdline());

        // Python properties
        msg.set_pythonuser(originalCall->pythonuser());
        msg.set_pythonfunction(originalCall->pythonfunction());
        // We don's support PY function name now.
        msg.set_ispython(originalCall->ispython());

        if (originalCall->recordexecgraph()) {
            msg.set_recordexecgraph(true);
        }

        SPDLOG_INFO("Chaining call {}/{} -> {}/{} (ids: {} -> {})",
                    originalCall->user(),
                    originalCall->function(),
                    msg.user(),
                    msg.function(),
                    originalCall->id(),
                    msg.id());

        // Record the chained call in the executor before invoking the new
        // functions to avoid data races
        faabric::executor::ExecutorContext::get()
          ->getExecutor()
          ->addChainedMessage(req->messages(req->messages_size() - 1));

        if (originalCall->recordexecgraph()) {
            faabric::util::logChainedFunction(*originalCall, msg);
        }
    }

    auto& plannerCli = faabric::planner::getPlannerClient();
    // call all the requests in reqsMap
    for (auto& [functionName, reqs] : reqsMap) {
        for (auto& [appId, req] : reqs) {
            plannerCli.callFunctions(req);
        }
    }
    // clear the context
    context->chainedFunctionName.clear();
    context->chainedMsgId.clear();
    context->chainedInput.clear();

    return 0;
}

int awaitChainedCallOutput(unsigned int messageId, char* buffer, int bufferLen)
{
    int callTimeoutMs = conf::getFaasmConfig().chainedCallTimeout;
    auto* exec = faabric::executor::ExecutorContext::get()->getExecutor();

    faabric::Message result;
    try {
        auto msg = exec->getChainedMessage(messageId);
        auto& plannerCli = faabric::planner::getPlannerClient();
        result = plannerCli.getMessageResult(msg, callTimeoutMs);
    } catch (faabric::executor::ChainedCallException& e) {
        SPDLOG_ERROR(
          "Error awaiting for chained call {}: {}", messageId, e.what());
        return 1;
    }

    if (result.type() == faabric::Message_MessageType_EMPTY) {
        SPDLOG_ERROR("Cannot find output for {}", messageId);
    }

    std::string outputData = result.outputdata();
    strncpy(buffer, outputData.c_str(), outputData.size());

    if (bufferLen < outputData.size()) {
        SPDLOG_WARN("Undersized output buffer: {} for {} output",
                    bufferLen,
                    outputData.size());
    }

    return result.returnvalue();
}
}
