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
    }
    else {
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
   * If not registered, return 0. If registered remote don't have, delete the
   * Redis key and return 0.
   */
  static int32_t __faasm_read_function_state_wrapper(wasm_exec_env_t exec_env,
    char* buffer,
    int32_t bufferLen)
  {
    std::string user = ExecutorContext::get()->getMsg().user();
    std::string func = ExecutorContext::get()->getMsg().function();
    int32_t parallelismId = ExecutorContext::get()->getMsg().parallelismid();
    SPDLOG_DEBUG(
      "S - faasm_read_function_state - {}/{}-{}", user, func, parallelismId);
    // If the size is 0, it means the function state is not initialized.
    if (bufferLen == 0) {
      return 0;
    }
    // If the size is not 0, which means the function state is already created.
    auto fs = faabric::state::getGlobalState().getFS(
      user, func, parallelismId, bufferLen);
    fs->get(reinterpret_cast<uint8_t*>(buffer));
    return fs->size();
  }

  /**
   * Create a new memory region, read the function state for the given key into
   * it, then return a pointer to the new memory. if lock == 0, don't lock. if lock == 1, lock it.
   */
  static int32_t __faasm_read_function_state_size_wrapper(wasm_exec_env_t exec_env, int32_t lock)
  {
    std::string user = ExecutorContext::get()->getMsg().user();
    std::string func = ExecutorContext::get()->getMsg().function();
    int32_t parallelismId = ExecutorContext::get()->getMsg().parallelismid();
    SPDLOG_DEBUG("S - faasm_read_function_state_size - {}/{}-{}",
      user,
      func,
      parallelismId);
    // Get the Size of this Function State at first. If it is not created,
    // return nullptr.
    faabric::state::State& state = faabric::state::getGlobalState();
    // Lock first, then read size.
    bool dataLock = lock == 1;
    SPDLOG_DEBUG("S - faasm_read_function_state_size get the state size, and lock is {}",
      dataLock ? "true" : "false");
    int32_t stateSize =
      state.getFunctionStateSize(user, func, parallelismId, dataLock);
    SPDLOG_TRACE("S - faasm_read_function_state_size - size: {}", stateSize);
    return stateSize;
  }

  /**
   * Create a new memory region, read the function state for the given key into
   * it, then return a pointer to the new memory.
   */
   // TODO - Bug happens in function_parstate.cpp
  static int32_t __faasm_read_function_state_ptr_lock_wrapper(wasm_exec_env_t exec_env)
  {
    std::string user = ExecutorContext::get()->getMsg().user();
    std::string func = ExecutorContext::get()->getMsg().function();
    int32_t parallelismId = ExecutorContext::get()->getMsg().parallelismid();
    SPDLOG_DEBUG("S - faasm_read_funcState_ptr_lock - {}/{}-{}",
      user,
      func,
      parallelismId);
    // Get the Size of this Function State at first. If it is not created,
    // return nullptr.
    faabric::state::State& state = faabric::state::getGlobalState();
    // Lock first, then read size.
    int32_t stateSize =
      state.getFunctionStateSize(user, func, parallelismId, true);
    SPDLOG_DEBUG("S - faasm_read_funcState_ptr_lock get the state size {}", stateSize);
    // If size is 0, return nullptr
    if (stateSize == 0) {
      return 0;
    }
    // If the size is not 0, which means the function state is already created.
    auto fs = faabric::state::getGlobalState().getFS(
      user, func, parallelismId, stateSize);
    // Map shared memory
    WasmModule* module = getExecutingModule();
    uint32_t wasmPtr = module->mapSharedFuncStateMemory(fs, 0, stateSize);

    // Call get to make sure the value is pulled
    fs->get();
    return wasmPtr;
  }

  /**
   * Writes the given data buffer to the function state referenced by the given
   * key and Unlock the function state.
   */
  static void __faasm_write_function_state_unlock_wrapper(
    wasm_exec_env_t exec_env,
    char* buffer,
    int32_t bufferLen)
  {
    std::string user = ExecutorContext::get()->getMsg().user();
    std::string func = ExecutorContext::get()->getMsg().function();
    int32_t parallelismId = ExecutorContext::get()->getMsg().parallelismid();
    SPDLOG_DEBUG("S - faasm_write_function_state_unlock - {}/{}-{}",
      user,
      func,
      parallelismId);
    // Create and set the data
    auto fs = faabric::state::getGlobalState().getFS(
      user, func, parallelismId, bufferLen);
    fs->set(reinterpret_cast<uint8_t*>(buffer), bufferLen, true);
    // TODO - Remove the pointer
  }

  static int32_t __faasm_function_state_lock_wrapper(wasm_exec_env_t exec_env) {
    std::string user = ExecutorContext::get()->getMsg().user();
    std::string func = ExecutorContext::get()->getMsg().function();
    int32_t parallelismId = ExecutorContext::get()->getMsg().parallelismid();
    SPDLOG_DEBUG("S - faasm_function_state_lock_wrapper - {}/{}-{}",
      user,
      func,
      parallelismId);
    auto fs = faabric::state::getGlobalState().getFS(
      user, func, parallelismId);
    return fs->lockMasterWrite();
  }

  static void __faasm_function_state_unlock_wrapper(wasm_exec_env_t exec_env) {
    std::string user = ExecutorContext::get()->getMsg().user();
    std::string func = ExecutorContext::get()->getMsg().function();
    int32_t parallelismId = ExecutorContext::get()->getMsg().parallelismid();
    SPDLOG_DEBUG("S - faasm_function_state_unlock_wrapper - {}/{}-{}",
      user,
      func,
      parallelismId);
    auto fs = faabric::state::getGlobalState().getFS(
      user, func, parallelismId);
    fs->unlockMasterWrite();
  }

  static NativeSymbol ns[] = {
      REG_NATIVE_FUNC(__faasm_read_state, "($$i)i"),
      REG_NATIVE_FUNC(__faasm_read_state_ptr, "($i)i"),
      REG_NATIVE_FUNC(__faasm_write_state, "($$i)"),
      REG_NATIVE_FUNC(__faasm_push_state, "($)"),
      // REG_NATIVE_FUNC(__faasm_create_function_state, "($i$$)"),
      REG_NATIVE_FUNC(__faasm_write_function_state, "($i)"),
      REG_NATIVE_FUNC(__faasm_read_function_state, "($i)i"),
      REG_NATIVE_FUNC(__faasm_read_function_state_ptr_lock, "()i"),
      REG_NATIVE_FUNC(__faasm_write_function_state_unlock, "($i)"),
      REG_NATIVE_FUNC(__faasm_read_function_state_size, "(i)i"),
      REG_NATIVE_FUNC(__faasm_function_state_lock, "()i"),
      REG_NATIVE_FUNC(__faasm_function_state_unlock, "()"),
  };

  uint32_t getFaasmStateApi(NativeSymbol** nativeSymbols)
  {
    *nativeSymbols = ns;
    return sizeof(ns) / sizeof(NativeSymbol);
  }
}
