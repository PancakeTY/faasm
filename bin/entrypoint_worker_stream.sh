#!/bin/bash

set -e

# Record the current directory
THIS_DIR=$(dirname $(readlink -f $0))

# Change to the Faasm code directory
cd /usr/local/code/faasm 

# Check for updates in the repository and rebuild if necessary
echo "Checking for updates in the Faasm repository..."
git fetch origin state
LOCAL_COMMIT=$(git rev-parse HEAD)
REMOTE_COMMIT=$(git rev-parse origin/state)

if [ "$LOCAL_COMMIT" != "$REMOTE_COMMIT" ]; then
    echo "New changes detected. Updating the repository and rebuilding the pool_runner..."
    git pull origin state
    
    echo "Updating submodules..."
    git submodule update --init --recursive

    source venv/bin/activate
    inv dev.cc pool_runner
else
    echo "The repository is already up to date."
fi

# Run codegen
cd $THIS_DIR
$THIS_DIR/entrypoint_codegen.sh

# Start hoststats
nohup hoststats start > /var/log/hoststats.log 2>&1 &

# Set up isolation
pushd /usr/local/code/faasm >> /dev/null

echo "Setting up cgroup"
./bin/cgroup.sh

echo "Setting up namespaces"
./bin/netns.sh ${MAX_NET_NAMESPACES}

popd >> /dev/null

# Continue with normal command
exec "$@"
