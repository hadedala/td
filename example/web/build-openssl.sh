#!/bin/sh

emconfigure 2> /dev/null || { echo 'emconfigure not found. Install emsdk and add emconfigure and emmake to PATH environment variable. See instruction at https://kripken.github.io/emscripten-site/docs/getting_started/downloads.html. Do not forget to add `emconfigure` and `emmake` to the PATH environment variable via `emsdk/emsdk_env.sh` script.'; exit 1; }

OPENSSL=OpenSSL_1_1_0j
if [ ! -f $OPENSSL.tar.gz ]; then
  echo "Downloading OpenSSL sources..."
  wget https://github.com/openssl/openssl/archive/$OPENSSL.tar.gz
fi
rm -rf ./$OPENSSL
echo "Unpacking OpenSSL sources..."
tar xzf $OPENSSL.tar.gz || exit 1
cd openssl-$OPENSSL

emconfigure ./Configure linux-generic32 no-shared no-dso no-engine no-unit-test no-ui || exit 1
sed -i.bak 's/CROSS_COMPILE=.*/CROSS_COMPILE=/g' Makefile || exit 1
sed -i 's/-ldl //g' Makefile || exit 1
echo "Building OpenSSL..."
emmake make depend || exit 1
emmake make -j 4 || exit 1

rm -rf ../build/crypto || exit 1
mkdir -p ../build/crypto/lib || exit 1
cp libcrypto.a libssl.a ../build/crypto/lib/ || exit 1
cp -r include ../build/crypto/ || exit 1
cd ..
