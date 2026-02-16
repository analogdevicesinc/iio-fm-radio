#!/bin/bash

version=$1
architecture=$(dpkg --print-architecture)

source_code=$(basename "$PWD")

sudo apt update 
sudo apt install -y build-essential make devscripts debhelper

sed -i "s/@VERSION@/$version-1/" packaging/debian/changelog
sed -i "s/@DATE@/$(date -R)/" packaging/debian/changelog
sed -i "s/@ARCHITECTURE@/$architecture/" packaging/debian/control

cp -r packaging/debian .

rm -rf packaging

pushd ..
tar czf ${source_code}_${version}.orig.tar.gz \
    --exclude='.git' \
    --exclude='debian' \
    $source_code
popd

debuild
