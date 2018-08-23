#pragma once

#include <infra/infra.h>
#include <util/util.h>

#include <string>
#include <exception>
#include <tuple>

#include <boost/filesystem.hpp>

#include <proto/faasm.pb.h>
#include <Runtime/Runtime.h>

using namespace IR;
using namespace Runtime;

namespace worker {
    const std::string ENTRYPOINT_FUNC = "run";

    const int MAX_NAME_LENGTH = 32;

    // Input memory
    // Need to make sure this starts high enough to avoid
    // other bits of the default memory
    const int INPUT_START = 1024 * 20;
    const int MAX_INPUT_BYTES = 1024;

    // Output memory
    const int OUTPUT_START = INPUT_START + MAX_INPUT_BYTES;
    const int MAX_OUTPUT_BYTES = 1024;

    // Chaining memory
    const int MAX_CHAINS = 50;
    const int CHAIN_NAMES_START = OUTPUT_START + MAX_OUTPUT_BYTES;
    const int MAX_CHAIN_NAME_BYTES = MAX_NAME_LENGTH * MAX_CHAINS;

    const int CHAIN_DATA_START = CHAIN_NAMES_START + MAX_CHAIN_NAME_BYTES;
    
    /** Defines everything to do with a Wasm module */
    class WasmModule {
    public:
        WasmModule();

        /** Executes the function and stores the result */
        int execute(message::FunctionCall &call);

        /** Cleans up */
        void clean();

        std::vector<message::FunctionCall> chainedCalls;

    private:
        Module *module;
        ModuleInstance *moduleInstance;

        Context *context;
        FunctionInstance *functionInstance;

        ValueTuple functionResults;

        void load(message::FunctionCall &call);

        std::vector<Value> buildInvokeArgs();

        void addDataSegment(int offset);
        void addDataSegment(int offset, std::vector<U8> &initialData);

        void setOutputData(message::FunctionCall &call);
        void setUpChainingData(const message::FunctionCall &call);
    };

    /** Worker wrapper */
    class Worker {
    public:
        Worker();

        void start();
    };

    /** Abstraction around cgroups */
    class CGroup {
    public:
        CGroup(const std::string &name);

        void limitCpu();
        void addCurrentPid();

    private:
        std::string name;
        std::string mode;
        std::vector<std::string> controllers;

        boost::filesystem::path getPathToController(const std::string &controller);
        boost::filesystem::path getPathToFile(const std::string &controller, const std::string &file);
        void mkdirForController(const std::string &controller);
    };

    /** Exceptions */
    class WasmException : public std::exception {
    };
}