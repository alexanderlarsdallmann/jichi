#compdef jichi
# zsh completion for jichi.
# The command list is checked against `jichi --help` by
# tests/smoke/completions_lint.sh -- it drifted to 28 of 51 while this
# comment asked a human to keep it in sync. Add new subcommands here; the
# lint fails in both directions (missing, and named-but-not-a-subcommand).
_jichi() {
    local -a cmds opts
    cmds=(agents assign assignments attempt audit benchmark board brief-check
          attempts checkpoints commands complete config constraints context control
          daemon
          describe docs doctor dream embed export fim glossary grade
          hint improve index init learn ls lsp map mcp memory models
          output-styles packages prune rerank recover rewind rules runs serve
          setup skills status sysmsg telemetry test timeouts undo
          workflow)
    opts=(-p --print --config --model --plan --auto --readonly -c --continue
          --session --all -v --verbose -q --quiet --silent --output --color
          --no-color --no-markdown --type-ahead --no-type-ahead --accessible
          --no-stdin
          --no-session
          --heartbeat --reindex --acp
          --route-fast --route-strong --no-route --verify --verify-retries
          --verify-timeout --verify-baseline --verify-kind --no-rollback --strict-scope
          --budget-tokens --deadline --max-tool-calls --edit-scope --journal --control
          --dry-run --list --global --force -V --version -h --help)
    if [[ $words[CURRENT] == -* ]]; then
        compadd -- $opts
    else
        compadd -- $cmds
        _files
    fi
}
_jichi "$@"
