#!/bin/bash
# install-deps.sh - Install all dependencies for Candor-Research

sudo apt-get update
sudo apt-get install -y \
  build-essential \
  libzmq3-dev \
  libreadline-dev \
  libeigen3-dev \
  libopencv-dev \
  libssl-dev \
  libcurl4-openssl-dev

echo "All dependencies installed successfully!"