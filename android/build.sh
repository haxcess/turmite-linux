#!/bin/sh
set -eu
cd "$(dirname "$0")"
command -v java >/dev/null 2>&1 || { echo 'Install JDK 17 or 21 and set JAVA_HOME.' >&2; exit 1; }
command -v gradle >/dev/null 2>&1 || { echo 'Install Gradle 8.11.1 and add its bin directory to PATH.' >&2; exit 1; }
gradle --version | grep -q '^Gradle 8\.11\.1$' || { echo 'This build requires Gradle 8.11.1.' >&2; exit 1; }
if [ "$#" -eq 0 ]; then set -- assembleDebug; fi
exec gradle "$@"
