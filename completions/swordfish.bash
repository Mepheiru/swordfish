# Bash completion for swordfish

_swordfish_completions()
{
    local cur prev opts
    COMPREPLY=()
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD-1]}"

    # All available options
    opts="
        -S -k -K -x -y -p -t -v -r -u
        --sort --exclude --help
        -9 -KILL -TERM
    "

    # If current word starts with '-', complete options
    if [[ "${cur}" == -* ]]; then
        COMPREPLY=( $(compgen -W "${opts}" -- "${cur}") )
        return 0
    fi

    # Special handling for arguments
    case "${prev}" in
        -u)
            # Complete with usernames
            COMPREPLY=( $(compgen -W "$(cut -d: -f1 /etc/passwd)" -- "${cur}") )
            return 0
            ;;
        --sort)
            COMPREPLY=( $(compgen -W "cpu ram age" -- "${cur}") )
            return 0
            ;;
        --exclude)
            # Suggest process names for exclusion
            COMPREPLY=( $(compgen -W "$(ps -eo comm= | sort -u)" -- "${cur}") )
            return 0
            ;;
    esac

    # Default: complete with process names dynamically
    # Lowercase current word
    local cur_lc=$(echo "$cur" | tr '[:upper:]' '[:lower:]')

    # Lowercase process names
    local procs=$(ps -eo comm= | tr '[:upper:]' '[:lower:]' | sort -u)

    COMPREPLY=( $(compgen -W "${procs}" -- "${cur_lc}") )
    return 0

}

# Register completion for swordfish command
complete -F _swordfish_completions swordfish
