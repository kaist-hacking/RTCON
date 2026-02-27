#!/bin/bash

if [ $# -ne 5 ]; then
    echo "Usage: $0 <Project> <File> <Function> <test_type> <Multi-level fuzzing enable>"
    exit 1
fi

# Ask for sudo permission
sudo -v

PROJECT=$1
FILE=$2
FUNCTION=$3
TEST_TYPE=$4
MULTI_LEVEL_FUZZING=$5

sudo apt update && sudo apt install -y docker.io

docker compose build base && docker compose build $PROJECT

if [ "$PROJECT" = "freertos" ]; then
  docker compose run "$PROJECT" TCP "$FILE" "$FUNCTION" "$TEST_TYPE" "$MULTI_LEVEL_FUZZING"
else
  docker compose run "$PROJECT" "$FILE" "$FUNCTION" "$TEST_TYPE" "$MULTI_LEVEL_FUZZING"
fi