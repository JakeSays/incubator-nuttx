#!/bin/bash
#############################################################################
# tools/checkrelease.sh
#
# Licensed to the Apache Software Foundation (ASF) under one or more
# contributor license agreements.  See the NOTICE file distributed with
# this work for additional information regarding copyright ownership.  The
# ASF licenses this file to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance with the
# License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
# License for the specific language governing permissions and limitations
# under the License.
#
#############################################################################

VERBOSE=0
RETURN_CODE=0
BASE_URL="https://dist.apache.org/repos/dist/dev/incubator/nuttx"
DIST_DIR="dist.apache.org"
TMP="/tmp"
TEMPDIR="$TMP/nuttx-checkrelease"
ORIGINAL_DIR="$(pwd)"
trap "{ rm -rf $TEMPDIR; }" EXIT

function validate_url() {
  if [[ `wget -S --spider $1  2>&1 | grep 'HTTP/1.1 200 OK'` ]]; then echo "true"; fi
}

function download_release() {
  rm -rf "$TEMPDIR"
  if [[ -n "$URL" ]]; then
    mkdir "$TEMPDIR"
    if [[ $(validate_url "$URL") ]]; then
      echo "Downloading release files from $URL"
      wget --quiet -r --no-parent -P "$TEMPDIR" --cut-dirs 100 "$URL"
      cd "$TEMPDIR"
      mv $DIST_DIR/apache-nuttx-* .
    else
      echo "The URL given doesn't return HTTP 200 OK return code— exiting. ($URL)"
      exit 1
    fi
  else
    if [[ -n "$DIRECTORY" ]]; then
      cp -r "$DIRECTORY" "$TEMPDIR"
      cd "$TEMPDIR"
    else
      echo "One of --dir or --url is required!"
      exit 1
    fi
  fi
}

function check_sha512() {
    RELEASE_FILE=$1
    echo "Checking $RELEASE_FILE sha512..."
    output="$(sha512sum -c $RELEASE_FILE.sha512 2>&1)"
    return_value=$?
    if [ $return_value -eq 0 ]; then
      echo " OK: $RELEASE_FILE sha512 hash matches."
    else
      RETURN_CODE=1
      echo " - $RELEASE_FILE sha512 hash does not match:"
      echo "$output"
    fi
    echo
}

function check_gpg() {
    RELEASE_FILE=$1
    echo "Checking $RELEASE_FILE GPG signature:"
    gpg --verify $RELEASE_FILE.asc $RELEASE_FILE
    return_value=$?
    if [ $return_value -eq 0 ]; then
      echo " OK: $RELEASE_FILE gpg signature matches."
    else
      RETURN_CODE=1
      echo " - Error checking $RELEASE_FILE gpg signature:"
      echo
    fi
    echo
}

function check_required_files() {
    RELEASE_DIR=$1
    echo "Checking $RELEASE_FILE for required files:"
    if [ ! -f "$RELEASE_DIR/LICENSE" ]; then
      echo " - LICENSE file not present."
      RETURN_CODE=1
    fi
    if [ ! -f "$RELEASE_DIR/NOTICE" ]; then
      echo " - NOTICE file not present."
      RETURN_CODE=1
    fi
    if [ ! -f "$RELEASE_DIR/README.txt" ]; then
      echo " - README.txt file not present."
      RETURN_CODE=1
    fi
    if [ ! -f "$RELEASE_DIR/DISCLAIMER-WIP" ]; then
      echo " - DISCLAIMER-WIP file not present."
      RETURN_CODE=1
    fi
    if [ 0 -eq $RETURN_CODE ]; then
      echo " OK: all required files exist in $RELEASE_DIR."
    fi
    echo
}

function check_nuttx() {
    RELEASE_FILE="$(ls *.tar.gz|head -1)"
    RELEASE_DIR="nuttx"
    check_sha512 "$RELEASE_FILE"
    check_gpg "$RELEASE_FILE"
    tar xf "$RELEASE_FILE"
    check_required_files "$RELEASE_DIR"
    mv "$RELEASE_FILE" "$TMP"
}

function check_nuttx_apps() {
    RELEASE_FILE="$(ls *.tar.gz|head -2| tail -1)"
    RELEASE_DIR="apps"
    check_sha512 "$RELEASE_FILE"
    check_gpg "$RELEASE_FILE"
    tar xf "$RELEASE_FILE"
    check_required_files "$RELEASE_DIR"
    mv "$RELEASE_FILE" "$TMP"
}

function check_sim_nsh_build() {
    RELEASE_DIR="nuttx"
    echo "Trying to build $RELEASE_DIR sim:nsh..."
    cd "$RELEASE_DIR"
    if [[ $VERBOSE -eq 0 ]]; then
      output=$(make distclean ; ./tools/configure.sh sim:nsh ; make) 2>&1
    else
      make distclean
      ./tools/configure.sh sim:nsh
      make
    fi
    return_value=$?
    if [ $return_value -eq 0 ]; then
      echo " OK: we were able to build sim:nsh"
    else
      RETURN_CODE=1
      echo " - Error building sim:nsh:"
      echo
      echo $output
    fi

    echo
}
function usage() {
    echo "Usage: $0 [--verbose] [--url <URL-of-release-dir>] [--release <name-of-release] [--dir <path-to-directory>] [--tempdir <path-to-directory>]"
    echo "   Given release full URL, release name, or a local directory, downloads or copies"
    echo "   all files in that directory (which for a release should include nuttx and nuttx-apps, sha512, "
    echo "   asc, and tar.gz files), checks the release SHA512 and GPG signatures, checks the unpacked "
    echo "   directories for required files, and tries to build NuttX for sim:nsh."
    echo
    echo "   If tempdir is specified, it will be removed and recreated; if it is not specified, /tmp/nuttx-checkrelease"
    echo "   is used."
    echo
    echo "   If --url or --release are given, nuttx and nuttx-apps tar.gz files are left in /tmp for the user to "
    echo "   build for their platform."
    echo
    echo "   -V and --verbose are equivalent."
    echo
    echo "Examples:"
    echo
    echo "  $0 --release 9.1.0-RC1"
    echo "  $0 --url https://dist.apache.org/repos/dist/dev/incubator/nuttx/9.1.0-RC1/"
    echo "  $0 --dir ./some-dir-that-has-nuttx-and-apps"
    echo
}

UNKNOWN=()
URL=""
DIRECTORY=""

while [[ $# -gt 0 ]]
do
  key="$1"

  case $key in
      -U|--url)
      shift
      URL="$1"
      ;;
      -R|--release)
      shift
      RELEASE="$1"
      URL="$BASE_URL/$RELEASE/"
      ;;
      -D|--dir)
      shift
      DIRECTORY="$(readlink -f $1)"
      ;;
      -T|--tempdir)
      shift
      TEMPDIR="$(readlink -f $1)"
      ;;
      -h|--help)
      usage
      exit 0
      ;;
      -V|--verbose)
      VERBOSE=1
      ;;
      *)    # unknown option
        usage
        exit 1
      ;;
  esac
  shift
done

if [[ (-z "$URL") && (-z "$DIRECTORY") ]]; then
  usage
  exit 1
fi

download_release
if [[ -n "$URL" ]]; then
  # download and check
  check_nuttx
  check_nuttx_apps
else
  # check directories without downloading
  check_required_files "nuttx"
  check_required_files "apps"
fi
check_sim_nsh_build
exit $RETURN_CODE
