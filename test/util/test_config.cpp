#include <catch/catch.hpp>

#include <util/config.h>
#include <util/environment.h>

using namespace util;

namespace tests {
    TEST_CASE("Test default system config initialisation", "[util]") {
        SystemConfig conf;
        conf.reset();

        REQUIRE(conf.threadsPerWorker == 5);

        // CI has to override some stuff
        if(conf.hostType == "ci") {
            REQUIRE(conf.redisStateHost == "localhost");
            REQUIRE(conf.redisQueueHost == "localhost");
            REQUIRE(conf.cgroupMode == "off");
        } else {
            REQUIRE(conf.hostType == "default");
            REQUIRE(conf.redisStateHost == "localhost");
            REQUIRE(conf.redisQueueHost == "localhost");
            REQUIRE(conf.cgroupMode == "on");
        }

        REQUIRE(conf.globalMessageBus == "redis");
        REQUIRE(conf.functionStorage == "local");
        REQUIRE(conf.serialisation == "json");
        REQUIRE(conf.bucketName == "");
        REQUIRE(conf.queueName == "faasm-messages");
        REQUIRE(conf.netNsMode == "off");
        REQUIRE(conf.awsLogLevel == "off");
        REQUIRE(conf.unsafeMode == "off");

        REQUIRE(conf.redisPort == "6379");

        REQUIRE(conf.maxNodes == 4);
        REQUIRE(conf.noScheduler == 0);
        REQUIRE(conf.prewarm == 1);
        REQUIRE(conf.maxQueueRatio == 3);
        REQUIRE(conf.maxWorkersPerFunction == 10);

        REQUIRE(conf.globalMessageTimeout == 60000);
        REQUIRE(conf.boundTimeout == 30000);
        REQUIRE(conf.unboundTimeout == 60000);

        REQUIRE(conf.stateStaleThreshold == 60000);
        REQUIRE(conf.stateClearThreshold == 300000);
        REQUIRE(conf.statePushInterval == 500);
        REQUIRE(conf.fullAsync == 0);
        REQUIRE(conf.fullSync == 0);
    }

    TEST_CASE("Test overriding system config initialisation", "[util]") {
        std::string originalHostType = getSystemConfig().hostType;

        setEnvVar("THREADS_PER_WORKER", "50");

        setEnvVar("HOST_TYPE", "magic");
        setEnvVar("GLOBAL_MESSAGE_BUS", "blah");
        setEnvVar("FUNCTION_STORAGE", "foobar");
        setEnvVar("SERIALISATION", "proto");
        setEnvVar("BUCKET_NAME", "foo-bucket");
        setEnvVar("QUEUE_NAME", "dummy-queue");
        setEnvVar("CGROUP_MODE", "off");
        setEnvVar("NETNS_MODE", "on");
        setEnvVar("AWS_LOG_LEVEL", "debug");
        setEnvVar("UNSAFE_MODE", "on");

        setEnvVar("REDIS_STATE_HOST", "not-localhost");
        setEnvVar("REDIS_QUEUE_HOST", "other-host");
        setEnvVar("REDIS_PORT", "1234");

        setEnvVar("MAX_NODES", "15");
        setEnvVar("NO_SCHEDULER", "1");
        setEnvVar("PREWARM", "5");
        setEnvVar("MAX_QUEUE_RATIO", "8888");
        setEnvVar("MAX_WORKERS_PER_FUNCTION", "7777");

        setEnvVar("GLOBAL_MESSAGE_TIMEOUT", "9876");
        setEnvVar("BOUND_TIMEOUT", "6666");
        setEnvVar("UNBOUND_TIMEOUT", "5555");

        setEnvVar("STATE_STALE_THRESHOLD", "4444");
        setEnvVar("STATE_CLEAR_THRESHOLD", "3333");
        setEnvVar("STATE_PUSH_INTERVAL", "2222");
        setEnvVar("FULL_ASYNC", "1");
        setEnvVar("FULL_SYNC", "12");

        // Create new conf for test
        SystemConfig conf;
        REQUIRE(conf.threadsPerWorker == 50);

        REQUIRE(conf.hostType == "magic");
        REQUIRE(conf.globalMessageBus == "blah");
        REQUIRE(conf.functionStorage == "foobar");
        REQUIRE(conf.serialisation == "proto");
        REQUIRE(conf.bucketName == "foo-bucket");
        REQUIRE(conf.queueName == "dummy-queue");
        REQUIRE(conf.cgroupMode == "off");
        REQUIRE(conf.netNsMode == "on");
        REQUIRE(conf.awsLogLevel == "debug");
        REQUIRE(conf.unsafeMode == "on");

        REQUIRE(conf.redisStateHost == "not-localhost");
        REQUIRE(conf.redisQueueHost == "other-host");
        REQUIRE(conf.redisPort == "1234");

        REQUIRE(conf.maxNodes == 15);
        REQUIRE(conf.noScheduler == 1);
        REQUIRE(conf.prewarm == 5);
        REQUIRE(conf.maxQueueRatio == 8888);
        REQUIRE(conf.maxWorkersPerFunction == 7777);

        REQUIRE(conf.globalMessageTimeout == 9876);
        REQUIRE(conf.boundTimeout == 6666);
        REQUIRE(conf.unboundTimeout == 5555);

        REQUIRE(conf.stateStaleThreshold == 4444);
        REQUIRE(conf.stateClearThreshold == 3333);
        REQUIRE(conf.statePushInterval == 2222);
        REQUIRE(conf.fullAsync == 1);
        REQUIRE(conf.fullSync == 12);

        // Be careful with host type
        setEnvVar("HOST_TYPE", originalHostType);

        unsetEnvVar("THREADS_PER_WORKER");

        unsetEnvVar("GLOBAL_MESSAGE_BUS");
        unsetEnvVar("FUNCTION_STORAGE");
        unsetEnvVar("SERIALISATION");
        unsetEnvVar("BUCKET_NAME");
        unsetEnvVar("QUEUE_NAME");
        unsetEnvVar("CGROUP_MODE");
        unsetEnvVar("NETNS_MODE");
        unsetEnvVar("AWS_LOG_LEVEL");
        unsetEnvVar("UNSAFE_MODE");

        unsetEnvVar("REDIS_STATE_HOST");
        unsetEnvVar("REDIS_QUEUE_HOST");
        unsetEnvVar("REDIS_PORT");

        unsetEnvVar("MAX_NODES");
        unsetEnvVar("NO_SCHEDULER");
        unsetEnvVar("PREWARM");
        unsetEnvVar("MAX_QUEUE_RATIO");
        unsetEnvVar("MAX_WORKERS_PER_FUNCTION");

        unsetEnvVar("GLOBAL_MESSAGE_TIMEOUT");
        unsetEnvVar("BOUND_TIMEOUT");
        unsetEnvVar("UNBOUND_TIMEOUT");

        unsetEnvVar("STATE_STALE_THRESHOLD");
        unsetEnvVar("STATE_CLEAR_THRESHOLD");
        unsetEnvVar("STATE_PUSH_INTERVAL");
        unsetEnvVar("FULL_ASYNC");
        unsetEnvVar("FULL_SYNC");
    }

    TEST_CASE("Check we can't have both full sync and full async on at the same time", "[conf]") {
        setEnvVar("FULL_ASYNC", "1");
        setEnvVar("FULL_SYNC", "1");

        REQUIRE_THROWS(SystemConfig());

        unsetEnvVar("FULL_ASYNC");
        unsetEnvVar("FULL_SYNC");
    }
}