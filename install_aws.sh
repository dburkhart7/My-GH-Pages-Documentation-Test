#!/bin/bash
# https://docs.aws.amazon.com/sdk-for-cpp/v1/developer-guide/setup-linux.htm

# install apt deps
sudo apt-get install -y \
  libcurl4-openssl-dev \
  libssl-dev \
  uuid-dev \
  zlib1g-dev \
  libpulse-dev

# Install AWS SDK from source
cd ~
git clone --recurse-submodules https://github.com/aws/aws-sdk-cpp
mkdir ~/sdk_build
cd ~/sdk_build/

# only build s3
cmake ../aws-sdk-cpp/ \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/usr/local \
  -DCMAKE_INSTALL_PREFIX=/usr/local \
  -DBUILD_ONLY="s3"
make -j$(nproc)
sudo make install