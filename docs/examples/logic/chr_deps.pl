% chr_deps.pl -- forward chaining over a dependency graph: as facts ARRIVE, the
% rules derive their consequences at once (Prolog's build_rules.pl worked
% backward from a goal instead). Duplicates are removed by simpagation, and a
% target that depends on itself is reported the moment the cycle closes.
% Run (from this directory):
%   swipl -q -g main -t halt chr_deps.pl < /dev/null
:- use_module(library(chr)).
:- chr_constraint dep/2, cycle/1.

dedup      @ dep(A, B) \ dep(A, B) <=> true.
cycle      @ dep(A, A) ==> cycle(A).
transitive @ dep(A, B), dep(B, C) ==> dep(A, C).
cycle(A) \ cycle(A) <=> true.

show(Label) :-
    findall(A-B, current_chr_constraint(dep(A, B)), Ds), length(Ds, N),
    findall(C, current_chr_constraint(cycle(C)), Cs),
    format("~w: ~d dependency edge(s) known, cycles ~w~n", [Label, N, Cs]).

main :-
    dep(jichi, 'main.o'), dep('main.o', 'agent.h'),
    show("two edges added    "),
    dep('agent.h', gen_header),
    show("a third added      "),
    dep(gen_header, jichi),
    show("an edge that closes a loop").
