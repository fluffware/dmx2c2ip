#! /bin/sh
debian/rules clean
debian/rules build
fakeroot debian/rules binary
