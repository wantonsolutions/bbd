#!/bin/bash
JEMALLOC_DIR=jemalloc

usage="Example: $1 [args]\n
-je, --jemalloc \t install jemalloc
-nt, --nedtries \t install nedtries
-f, --force \t force clean and setup everything\n
-h, --help \t this usage information message\n"

# parse cli
for i in "$@"
do
case $i in

    -je|--jemalloc)
    JEMALLOC="${i#*=}"
    ;;

    -nt|--nedtries)
    NEDTRIES="${i#*=}"
    ;;

    -f| --force)
    FORCE=1
    ;;

    -h|--help)
    echo -e $usage
    exit
    ;;

    *)                      # unknown option
    echo "Unknown Option: $i"
    echo -e $usage
    exit
    ;;
esac
done

if [[ $JEMALLOC ]]; then

    # clean
    if [[ $FORCE ]];
    then
        rm -rf ${JEMALLOC_DIR}
    fi

    # Initialize & setup jemalloc
    if [ ! -d ${JEMALLOC_DIR} ]; then
        git submodule update --init ${JEMALLOC_DIR}
    fi

    # build
    pushd ${JEMALLOC_DIR}
    if [[ $FORCE ]]; then   rm -rf build;   fi
    autoconf
    ./configure --with-jemalloc-prefix=je_ --with-malloc-conf --config-cache --disable-cxx
    make -j$(nproc)
    #install the library
    echo "Installing jemalloc may require sudo"
    sudo make install


    # save paths
    JE_BUILD_DIR=`pwd`
    # echo "-L${JE_BUILD_DIR}/lib -Wl,-rpath,${JE_BUILD_DIR}/lib -ljemalloc" > je_libs
    # echo "${JE_BUILD_DIR}/lib/libjemalloc_pic.a" > je_static_libs
    # echo "-I${JE_BUILD_DIR}/include" > je_includes
    popd
fi

if [[ $NEDTRIES ]]; then

    # clean
    if [[ $FORCE ]];
    then
        rm -rf ${NEDTRIES_DIR}
    fi

    # Initialize & setup nedtries
    if [ ! -d ${NEDTRIES_DIR} ]; then
        git submodule update --init ${NEDTRIES_DIR}
    fi

fi

