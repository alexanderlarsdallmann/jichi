# bash completion for jichi.
# The command list is checked against `jichi --help` by
# tests/smoke/completions_lint.sh -- it drifted to 28 of 51 while this
# comment asked a human to keep it in sync. Add new subcommands here; the
# lint fails in both directions (missing, and named-but-not-a-subcommand).
_jichi()
{
    local cur prev cmds opts
    COMPREPLY=()
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD-1]}"

    cmds="agents assign assignments attempt attempts audit benchmark board brief-check \
 checkpoints \
commands complete config constraints context control daemon describe docs \
doctor \
dream embed export fim glossary grade hint improve index init learn \
ls lsp map mcp memory models output-styles packages prune rerank \
recover rewind rules runs serve setup skills status sysmsg telemetry test \
timeouts undo workflow"
    opts="-p --print --config --model --plan --auto --readonly -c --continue \
--session --all -v --verbose -q --quiet --silent --output --color --no-color \
--no-markdown --type-ahead --no-type-ahead --accessible --no-stdin \
--no-session --heartbeat \
--reindex \
--acp --route-fast \
--route-strong --no-route --verify --verify-retries --verify-timeout \
--verify-baseline --verify-kind --no-rollback --strict-scope --budget-tokens --deadline \
--max-tool-calls --edit-scope --journal --control --dry-run --list --global --force \
-V --version -h --help"

    case "$prev" in
        --config|--journal|--edit-scope)
            COMPREPLY=( $(compgen -f -- "$cur") ); return ;;
        --output)
            COMPREPLY=( $(compgen -W "text json jsonl" -- "$cur") ); return ;;
        --model|--route-fast|--route-strong|--session|--heartbeat)
            return ;;
    esac

    if [[ "$cur" == -* ]]; then
        COMPREPLY=( $(compgen -W "$opts" -- "$cur") )
    else
        COMPREPLY=( $(compgen -W "$cmds" -- "$cur") $(compgen -f -- "$cur") )
    fi
}
complete -o filenames -F _jichi jichi
