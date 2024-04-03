#include <faabric/executor/ExecutorContext.h>
#include <faabric/planner/PlannerClient.h>
#include <faabric/proto/faabric.pb.h>
#include <faabric/util/logging.h>
#include <wamr/WAMRWasmModule.h>
#include <wamr/native.h>
#include <wasm/WasmExecutionContext.h>
#include <wasm/WasmModule.h>

#include <wasm_export.h>

using namespace faabric::executor;

namespace wasm {
/**
 * Read state for the given key into the buffer provided.
 *
 * Returns size of the state if buffer length is zero.
 */
static int32_t __faasm_read_state_wrapper(wasm_exec_env_t exec_env,
                                          char* key,
                                          char* buffer,
                                          int32_t bufferLen)
{
    SPDLOG_DEBUG("S - faasm_read_state {} <buffer> {}", key, bufferLen);

    std::string user = ExecutorContext::get()->getMsg().user();

    if (bufferLen == 0) {
        // If buffer len is zero, just need the state size
        faabric::state::State& state = faabric::state::getGlobalState();
        return (int32_t)state.getStateSize(user, key);
    } else {
        // Write state to buffer
        auto kv = faabric::state::getGlobalState().getKV(user, key, bufferLen);
        kv->get(reinterpret_cast<uint8_t*>(buffer));

        return kv->size();
    }

    return 0;
}

/**
 * Create a new memory region, read the state for the given key into it,
 * then return a pointer to the new memory.
 */
static int32_t __faasm_read_state_ptr_wrapper(wasm_exec_env_t exec_env,
                                              char* key,
                                              int32_t bufferLen)
{
    std::string user = ExecutorContext::get()->getMsg().user();
    auto kv = faabric::state::getGlobalState().getKV(user, key, bufferLen);

    SPDLOG_DEBUG("S - faasm_read_state_ptr - {} {}", kv->key, bufferLen);

    // Map shared memory
    WasmModule* module = getExecutingModule();
    uint32_t wasmPtr = module->mapSharedStateMemory(kv, 0, bufferLen);

    // Call get to make sure the value is pulled
    kv->get();

    return wasmPtr;
}

/**
 * Writes the given data buffer to the state referenced by the given key.
 */
static void __faasm_write_state_wrapper(wasm_exec_env_t exec_env,
                                        char* key,
                                        char* buffer,
                                        int32_t bufferLen)
{
    std::string user = ExecutorContext::get()->getMsg().user();
    auto kv = faabric::state::getGlobalState().getKV(user, key, bufferLen);

    SPDLOG_DEBUG("S - faasm_write_state - {} <data> {}", kv->key, bufferLen);

    kv->set(reinterpret_cast<uint8_t*>(buffer));
}

/**
 * Pushes the state for the given key
 */
static void __faasm_push_state_wrapper(wasm_exec_env_t exec_env, char* key)
{
    SPDLOG_DEBUG("S - faasm_push_state - {}", key);

    std::string user = ExecutorContext::get()->getMsg().user();
    auto kv = faabric::state::getGlobalState().getKV(user, key, 0);
    kv->pushFull();
}

/**
 * Register the parititoned stateful function and write the initial data to
 * state server.
 */
static void __faasm_create_function_state_wrapper(wasm_exec_env_t exec_env,
                                                  char* buffer,
                                                  long bufferLen,
                                                  char* inputKey,
                                                  char* stateKey)
{
    std::string user = ExecutorContext::get()->getMsg().user();
    std::string func = ExecutorContext::get()->getMsg().function();
    int32_t parallelismId = ExecutorContext::get()->getMsg().parallelismid();
    SPDLOG_DEBUG("S - faasm_create_func_state - {}/{}-{} input {}/ state{}",
                 user,
                 func,
                 parallelismId,
                 inputKey,
                 stateKey);
    // Create and set the data
    auto fs = faabric::state::getGlobalState().getFS(
      user, func, parallelismId, bufferLen);
    fs->set(reinterpret_cast<uint8_t*>(buffer), bufferLen);
    // Register the function state to the state
    if (stateKey != nullptr) {
        if (inputKey == nullptr) {
            throw std::runtime_error("inputKey isn't set, but stateKey is set");
        }
        fs->setPartitionKey(std::string(stateKey));
    }
    // Register the function state to the planner
    auto regReq = std::make_shared<faabric::FunctionStateRegister>();
    regReq->set_user(user);
    regReq->set_func(func);
    regReq->set_isparition(false);
    if (inputKey != nullptr && stateKey != nullptr) {
        regReq->set_isparition(true);
        regReq->set_pinputkey(inputKey);
        regReq->set_pstatekey(stateKey);
    }
    auto& plannerCli = faabric::planner::getPlannerClient();
    plannerCli.registerFunctionState(regReq);
}

/**
 * Writes the given data buffer to the function state referenced by the given
 * key.
 */
static void __faasm_write_function_state_wrapper(wasm_exec_env_t exec_env,
                                                 char* buffer,
                                                 int32_t bufferLen)
{
    std::string user = ExecutorContext::get()->getMsg().user();
    std::string func = ExecutorContext::get()->getMsg().function();
    int32_t parallelismId = ExecutorContext::get()->getMsg().parallelismid();
    SPDLOG_DEBUG(
      "S - faasm_write_function_state - {}/{}-{}", user, func, parallelismId);
    // Create and set the data
    auto fs = faabric::state::getGlobalState().getFS(
      user, func, parallelismId, bufferLen);
    fs->set(reinterpret_cast<uint8_t*>(buffer), bufferLen);
}

/**
 * Read state for the given key into the buffer provided.
 * RULES:
 * Reading Size :
 * If local storage has master copy, return its size.
 * Otherwise get the master from remote and return it from remote.
 * If not registered, return 0. If registered remote don't have, delete the Redis
 * key and return 0.
 */
static int32_t __faasm_read_function_state_wrapper(wasm_exec_env_t exec_env,
                                                   char* buffer,
                                                   int32_t bufferLen,
                                                   char* inputKeys)
{
    // If buffer is nullptr, flag is true
    std::string user = ExecutorContext::get()->getMsg().user();
    std::string func = ExecutorContext::get()->getMsg().function();
    int32_t parallelismId = ExecutorContext::get()->getMsg().parallelismid();
    SPDLOG_DEBUG(
      "S - faasm_read_function_state - {}/{}-{}", user, func, parallelismId);
    if (inputKeys != nullptr) {
        SPDLOG_DEBUG("S - faasm_read_function_state - inputKeys: {}",
                     inputKeys);
    }
    if (bufferLen == 0) {
        // If buffer len is zero, just need to get the state size
        SPDLOG_DEBUG("S - faasm_read_function_state get the state size");
        faabric::state::State& state = faabric::state::getGlobalState();
        return (int32_t)state.getFunctionStateSize(user, func, parallelismId);
    } else {
        // TODO - only read the input keys for partitioned stateful.
        auto fs = faabric::state::getGlobalState().getFS(
          user, func, parallelismId, bufferLen);
        fs->get(reinterpret_cast<uint8_t*>(buffer));
        return fs->size();
    }

    return 0;
}

static NativeSymbol ns[] = {
    REG_NATIVE_FUNC(__faasm_read_state, "($$i)i"),
    REG_NATIVE_FUNC(__faasm_read_state_ptr, "($i)i"),
    REG_NATIVE_FUNC(__faasm_write_state, "($$i)"),
    REG_NATIVE_FUNC(__faasm_push_state, "($)"),
    REG_NATIVE_FUNC(__faasm_create_function_state, "($i$$)"),
    REG_NATIVE_FUNC(__faasm_write_function_state, "($i)"),
    REG_NATIVE_FUNC(__faasm_read_function_state, "($i$)i"),
};

uint32_t getFaasmStateApi(NativeSymbol** nativeSymbols)
{
    *nativeSymbols = ns;
    return sizeof(ns) / sizeof(NativeSymbol);
}
}
