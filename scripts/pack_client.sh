#!/bin/bash

source $(dirname $0)/pack_common.sh

function usage()
{
    echo "Options for subcommand 'pack_client':"
    echo "  -h"
    echo "  -p|--update-package-template <minos-package-template-file-path>"
    echo "  -b|--custom-boost-lib"
    echo "  -g|--custom-gcc"
    exit 0
}

pwd="$( cd "$( dirname "$0"  )" && pwd )"
shell_dir="$( cd $pwd/.. && pwd )"
cd $shell_dir

if [ ! -f src/include/pegasus/git_commit.h ]
then
    echo "ERROR: src/include/pegasus/git_commit.h not found"
    exit -1
fi

if [ ! -f DSN_ROOT/bin/pegasus_server/pegasus_server ]
then
    echo "ERROR: DSN_ROOT/bin/pegasus_server/pegasus_server not found"
    exit -1
fi

if [ ! -f src/builder/CMAKE_OPTIONS ]
then
    echo "ERROR: src/builder/CMAKE_OPTIONS not found"
    exit -1
fi

if grep -q Debug src/builder/CMAKE_OPTIONS
then
    build_type=debug
else
    build_type=release
fi
version=`grep "VERSION" src/include/pegasus/version.h | cut -d "\"" -f 2`
commit_id=`grep "GIT_COMMIT" src/include/pegasus/git_commit.h | cut -d "\"" -f 2`
platform=`lsb_release -a 2>/dev/null | grep "Distributor ID" | awk '{print $3}' | tr '[A-Z]' '[a-z]'`
echo "Packaging pegasus client $version ($commit_id) $platform $build_type ..."

pack_version=client-$version-${commit_id:0:7}-${platform}-${build_type}
pack=pegasus-$pack_version

if [ -f ${pack}.tar.gz ]
then
    rm -f ${pack}.tar.gz
fi

if [ -d ${pack} ]
then
    rm -rf ${pack}
fi

pack_template=""
if [ -n "$MINOS_CONFIG_FILE" ]; then
    pack_template=`dirname $MINOS_CONFIG_FILE`/xiaomi-config/package/pegasus.yaml
fi

custom_boost_lib="false"
custom_gcc="false"

while [[ $# > 0 ]]; do
    option_key="$1"
    case $option_key in
        -p|--update-package-template)
            pack_template="$2"
            shift
            ;;
        -b|--custom-boost-lib)
            custom_boost_lib="true"
            ;;
        -g|--custom-gcc)
            custom_gcc="true"
            ;;
        -h|--help)
            usage
            ;;
    esac
    shift
done

mkdir -p ${pack}/lib
cp -v ./DSN_ROOT/lib/libpegasus_client_static.a ${pack}/lib
cp -v ./DSN_ROOT/lib/libpegasus_client_shared.so ${pack}/lib
cp -v -r ./src/include ${pack}
cp -v -r ./src/sample ${pack}

echo "Pegasus Client $version ($commit_id) $platform $build_type" >${pack}/VERSION

tar cfz ${pack}.tar.gz ${pack}

if [ -f $pack_template ]; then
    echo "Modifying $pack_template ..."
    sed -i "/^version:/c version: \"$pack_version\"" $pack_template
    sed -i "/^build:/c build: \"\.\/run.sh pack\"" $pack_template
    sed -i "/^source:/c source: \"$PEGASUS_ROOT\"" $pack_template
fi

echo "Done"
