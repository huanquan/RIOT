#/bin/bash
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
"${DIR}/bin/native/ndn_sync.elf" "$2" -i "$1" -o
