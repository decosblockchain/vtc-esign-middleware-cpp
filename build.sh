#!/bin/sh
docker build -t vtc-esign-middleware-base build/base/
docker build -t vtc-esign-proxy build/proxy/
docker build -t vtc-esign-middleware .
