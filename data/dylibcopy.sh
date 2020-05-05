#! /bin/sh

set -e

bundle="$1"

if [ ! -d "$bundle" ]; then
  echo "$bundle is not an application bundle"
  exit 1
fi

frameworks="${bundle}/Contents/Frameworks"

mkdir -p "$frameworks"
rm -f ${frameworks}/*.processed

absreadlink()
{
  if [ -L "$1" ]; then
    local target=`readlink "$1"`
    if [ "${target:0:1}" != "/" ]; then
      local dir=`dirname "$1"`
      target="$dir/$target"
    fi
    if [ -L "$target" ]; then
      absreadlink "$target"
    else
      echo "$target"
    fi
  else
    echo "$1"
  fi
}

process_dylib()
{
  local file="$1"
  local dylib="$2"

  local resolved=`absreadlink "$dylib"`
  local name="${resolved##*/}"

  if [ ! -f "${frameworks}/$name" ] && [ ! -f "${frameworks}/$name.processed" ]; then
    touch "${frameworks}/$name.processed"
    if [ -f "$dylib" ]; then
      echo "Found dependency $name"
      cp "$dylib" "${frameworks}/$name"
    else
      echo "Dependency $name not found"
      exit 1
    fi

    install_name_tool -id "$name" "${frameworks}/$name"

    # dylibs themselves have dependencies. Process them too
    process_file "${frameworks}/$name"
  fi

  install_name_tool -change "$dylib" "@executable_path/../Frameworks/$name" "$file"
}

process_dylibs()
{
  local file="$1"
  while [ ! -z "$2" ]; do
    process_dylib "$file" "$2"
    shift
  done
}

process_file()
{
  local file="$1"
  process_dylibs "$file" `otool -L "$file" | grep 'dylib' | sed 's/^[[:blank:]]*//' | sed 's/ .*//' | grep -v '^/usr/\|^/System/' | grep -v ':$'`
}

for file in "${bundle}/Contents/MacOS/"*; do
  process_file "$file"
done

rm -f ${frameworks}/*.processed

echo Dependencies copied
