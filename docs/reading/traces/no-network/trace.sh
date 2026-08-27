# no-network/trace.sh -- the definition of this trace, sourced by ../capture.sh.
#
# A whole jichi run with no model in it. `map` walks the workspace, extracts
# top-level symbols, and prints the repository map -- the same text the
# repoMap feature puts in a real turn's context, here on its own.

# No model, so no reply table and no request artifacts. capture.sh still
# writes a config, pointed at a port nothing listens on: if this run dialled a
# model it would fail, so a clean exit is the evidence that it did not.
NEEDS_MODEL=0

# stdout here is a report for a person, not an event stream.
STDOUT_NAME=stdout.txt

# A miniature project, chosen to make the map show its edges: two languages, a
# header whose only definition is a struct, a subdirectory, a file with an
# extension the scanner does not know (README.md, which is therefore never
# opened), and names whose alphabetical order differs from the order they are
# created in.
seed_workspace() {
    mkdir -p src
    printf '#include "util.h"\n\nint helper(int x)\n{\n    return x + 1;\n}\n\nint main(void)\n{\n    return helper(1);\n}\n' > main.c
    printf 'struct point {\n    int x;\n    int y;\n};\n\nint helper(int x);\n' > util.h
    printf 'void b_one(void) {}\n\nvoid b_two(void) {}\n' > src/b.c
    printf 'def hello():\n    pass\n' > tool.py
    printf '# a fixture project\n\nNothing to see here.\n' > README.md
}

run_trace() {
    "$BIN" --config "$CONFIG" map
}
