#!/bin/sh
set -eu

echo '[generating compile_commands.json]'
bear -- ./build.sh media
bear --append -- ./build.sh mooch
bear --append -- ./build.sh moochrss
