#!/bin/sh
set -eu

for arg in "$@"; do eval "$arg=1"; done
if [ -z "${release:-}" ]; then debug=1; fi
if [ -n "${debug:-}" ];   then echo '[debug mode]'; fi
if [ -n "${release:-}" ]; then echo '[release mode]'; fi

compile_common='-I../src/ -g -fdiagnostics-absolute-paths -Wall -Xclang -flto-visibility-public-std'
compile_debug_cpp="clang++ -g -O0 -DBUILD_DEBUG=1 ${compile_common}"
compile_release_cpp="clang++ -g -O3 -DBUILD_DEBUG=0 ${compile_common}"
compile_debug_c="clang -g -O0 -DBUILD_DEBUG=1 ${compile_common}"
compile_release_c="clang -g -O3 -DBUILD_DEBUG=0 ${compile_common}"
out='-o'

if [ -n "${mooch:-}" ] || [ -n "${moochrss:-}" ]; then
  link_mooch="$(pkg-config --cflags --libs libxml-2.0 libtorrent-rasterbar) -lcurl"
fi
if [ -n "${media:-}" ]; then
  link_media='-lavcodec -lavformat -lavutil -lswscale'
fi

if [ -n "${debug:-}" ];   then
  compile_cpp="$compile_debug_cpp"
  compile_c="$compile_debug_c"
elif [ -n "${release:-}" ]; then
  compile_cpp="$compile_release_cpp"
  compile_c="$compile_release_c"
fi

mkdir -p build
cd build
if [ -n "${mooch:-}" ];    then didbuild=1 && $compile_cpp ../src/mooch/main.cpp $link_mooch $out mooch; fi
if [ -n "${moochrss:-}" ]; then didbuild=1 && $compile_cpp ../src/moochrss/main.cpp $link_mooch $out moochrss; fi
if [ -n "${media:-}" ];    then didbuild=1 && $compile_c ../src/media/main2.c $link_media $out media; fi
cd ..

if [ -z "${didbuild:-}" ]; then
  echo '[WARNING] no valid build target specified; must use build target names as arguments to this script, like ./build.sh mooch.'
  exit 1
fi
