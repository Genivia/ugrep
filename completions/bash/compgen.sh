#!/bin/sh

UGREP=../../src/ugrep

cat > ug <<'END'
# bash completion for ug and ugrep
# recommended: add the following lines to ~/.inputrc
# set colored-stats on
# set colored-completion-prefix on

if ! declare -F _init_completion >/dev/null 2>&1; then
    return
fi

_comp_cmd_ugrep()
{
    local IFS=$' \t\n'
    local cur prev words cword
    _init_completion -s || return

    if [[ $cword -eq 1 && "${words[1]}" == "" ]]; then
        _comp_cmd_ugrep_usage
        return
    fi

    local i
    for i in "${!words[@]}"; do
        if [ "${words[$i]}" = "--" ]; then
            if [ $cword -gt $i ]; then
                _filedir
                return
            fi
            words=( "${words[@]:0:$i}" )
            break
        fi
    done

    case $prev in
    -D)
        # complete devices parameter
        COMPREPLY=( $(compgen -W "read skip" -- $cur) )
        compopt +o nospace
        return
        ;;
    -d)
        # complete directories parameter
        COMPREPLY=( $(compgen -W "read recurse skip" -- $cur) )
        compopt +o nospace
        return
        ;;
    -t)
        # complete a file type by generating the option values
        _comp_cmd_ugrep_file_type
        return
        ;;
    -Z)
        # suggest fuzzy parameters
        COMPREPLY=( $(compgen -W "1 +1 -1 ~1 +-1 +~1 +-~1 -~1 best1 best+1 best-1 best~1 best+-1 best+~1 best+-~1 best-~1" -- $cur) )
        return
        ;;
    esac

    case "${words[$cword]}" in
    --binary-files=*)
        # complete binary-files parameter
        COMPREPLY=( $(compgen -W "binary hex text with-hex without-match" -- $cur) )
        compopt +o nospace
        return
        ;;
    --color=* | --colour=*)
        # complete color and pretty parameter
        COMPREPLY=( $(compgen -W "always auto never" -- $cur) )
        compopt +o nospace
        return
        ;;
    --colors=* | --colours=* | --file-magic=*)
        # add an opening quote to quote the long option argument when recommended
        COMPREPLY=( "'" )
        return
        ;;
    --devices=*)
        # complete devices parameter
        COMPREPLY=( $(compgen -W "read skip" -- $cur) )
        compopt +o nospace
        return
        ;;
    --directories=*)
        # complete directories parameter
        COMPREPLY=( $(compgen -W "read recurse skip" -- $cur) )
        compopt +o nospace
        return
        ;;
    --encoding=*)
        # complete encoding format by generating the option values
        _comp_cmd_ugrep_encoding
        return
        ;;
    --hexdump=*)
        # suggest hexdump parameters
        COMPREPLY=( $(compgen -W "1a 2a 4ah 6ah 8ah 1aC1 2aC1 4ahC1 6ahC1 8ahC1" -- $cur) )
        return
        ;;
    --hyperlink=)
        # suggest hyperlinking line and column numbers
        COMPREPLY=( "+" )
        compopt +o nospace
        return
        ;;
    --sort=*)
        # complete sort key
        COMPREPLY=( $(compgen -W "best changed created name size used rbest rchanged rcreated rname rsize rused" -- $cur) )
        compopt +o nospace
        return
        ;;
    --file-type=*)
        # complete a file type by generating the option values
        _comp_cmd_ugrep_file_type
        return
        ;;
    --fuzzy=*)
        # suggest fuzzy parameters
        COMPREPLY=( $(compgen -W "1 +1 -1 ~1 +-1 +~1 +-~1 -~1 best1 best+1 best-1 best~1 best+-1 best+~1 best+-~1 best-~1" -- $cur) )
        return
        ;;
    esac

    case $cur in
    -A | -B | -C)
        # suggest a context
        COMPREPLY=( $(compgen -W "-A3 -B3 -C3" -- $cur) )
        return
        ;;
    -D*)
        # complete devices parameter
        COMPREPLY=( $(compgen -W "-Dread -Dskip" -- $cur) )
        compopt +o nospace
        return
        ;;
    -d*)
        # complete directories parameter
        COMPREPLY=( $(compgen -W "-dread -drecurse -dskip" -- $cur) )
        compopt +o nospace
        return
        ;;
    -e | -g | -M | -N | --and | --andnot | --not)
        # add an opening quote to quote the option argument when recommended
        COMPREPLY=( "${cur} '" )
        return
        ;;
    --colors | --colours)
        # add an opening quote to quote color arguments (recommended)
        COMPREPLY=( "${cur}='" )
        return
        ;;
    -K)
        # suggest a line range
        COMPREPLY=( "-K1,10" )
        return
        ;;
    -m*)
        # suggest a match count range
        COMPREPLY=( $(compgen -W "-m1 -m1, -m1,10" -- $cur) )
        return
        ;;
    -t*)
        # complete a file type by generating the option values
        _comp_cmd_ugrep_file_type_t
        return
        ;;
    -Z*)
        # suggest fuzzy parameters
        COMPREPLY=( $(compgen -W "-Z -Z1 -Z+1 -Z-1 -Z~1 -Z+-1 -Z+~1 -Z+-~1 -Z-~1 -Zbest1 -Zbest+1 -Zbest-1 -Zbest~1 -Zbest+-1 -Zbest+~1 -Zbest+-~1 -Zbest-~1" -- $cur) )
        return
        ;;
    --*)
        # complete long options by generating them
        COMPREPLY=( $(compgen -W "$(_parse_help _comp_cmd_ugrep_help)" -- $cur) )
        if [[ ! "${COMPREPLY[@]}" =~ "=" ]]; then
            # add space after long options that do not end with a =
            compopt +o nospace
        fi
        return
        ;;
    -?*)
        # add space after short option(s)
        COMPREPLY=( $cur )
        compopt +o nospace
        return
        ;;
    -)
        _comp_cmd_ugrep_usage
        return
        ;;
    esac

    _filedir
} &&
    complete -o nospace -F _comp_cmd_ugrep ug ug+ ugrep ugrep+

_comp_cmd_ugrep_usage()
{
    local -a usage=()
    local line i=0
    case $COMP_TYPE in
    33|63|64)
        # generate list of options, concat the first sentence to them
        usage[0]="Usage:"
        while read -r line; do
            # truncate to screen width
            (( ++i ))
            usage[$i]=${line:0:$COLUMNS}
        done < <(_comp_cmd_ugrep_help)
        ;;
    37)
        # generate list of options
        while read -r line; do
            # keep initial option only, remove the rest
            usage[$i]=${line%%[[, ]*}
            (( ++i ))
        done < <(_comp_cmd_ugrep_help)
        ;;
    esac
    COMPREPLY=( "${usage[@]}" )
    compopt -o nosort
}

END

# generate --help
printf "_comp_cmd_ugrep_help()\n{\ncat <<'END'\n" >> ug
$UGREP --help 2>&1 \
    | sed -E -e '/^[ ]{4}-/,/^[ ]{12}.*\./!d' -e 's/^([ ]{12}[^.]*\.)( .*)?$/\1/' -e 's/^[ ]{4}//' -e $'s/[`\']//g' \
    | sed -e :a -e '$!N;s/\n[ ]\{7\}//;ta' -e 'P;D' \
    >> ug
printf "END\n}\n\n" >> ug

# generate -t (--file-type=) TYPE arguments
printf '_comp_cmd_ugrep_file_type()\n{\n    COMPREPLY=( $(compgen -W "' >> ug
$UGREP -tlist 2>&1 | sed -E -e 's/[ ]*([0-9A-Za-z+]+).*/\1/' -e '/FILE/d' -e '/^ /d' | tr '\n' ' ' | sed 's/ $//' >> ug
printf '" -- $cur) )\n    compopt +o nospace\n}\n\n' >> ug

# generate -t (--file-type=) -tTYPE arguments
printf '_comp_cmd_ugrep_file_type_t()\n{\n    COMPREPLY=( $(compgen -W "' >> ug
$UGREP -tlist 2>&1 | sed -E -e 's/[ ]*([0-9A-Za-z+]+).*/\1/' -e '/FILE/d' -e '/^ /d' | tr '\n' ' ' | sed -e 's/ $//' -e 's/^/-t/' -e 's/ / -t/g' >> ug
printf '" -- $cur) )\n    compopt +o nospace\n}\n\n' >> ug

# generate --encoding=ENCODING arguments
printf '_comp_cmd_ugrep_encoding()\n{\n    COMPREPLY=( $(compgen -W "' >> ug
$UGREP --encoding=list 2>&1 | sed -e 's/^.[a-z].*are//' -e '/help/d' -e "s/ '//g" -e "s/',\?/ /g" | tr '\n' ' ' | sed 's/ $//' >> ug
printf '" -- $cur) )\n    compopt +o nospace\n}\n\n' >> ug

cat ug
