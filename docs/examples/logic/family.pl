% family.pl -- facts, a recursive rule, and backtracking.
% Run (from this directory):
%   swipl -q -g main -t halt family.pl < /dev/null

parent(ada, ben).
parent(ada, cy).
parent(ben, dora).
parent(cy, eli).

% X is an ancestor of Y if X is a parent of Y, or a parent of an ancestor of Y.
ancestor(X, Y) :- parent(X, Y).
ancestor(X, Y) :- parent(X, Z), ancestor(Z, Y).

main :-
    format("Who are ada's descendants?~n"),
    forall(ancestor(ada, D), format("  ~w~n", [D])),
    format("Who are eli's ancestors?~n"),
    forall(ancestor(A, eli), format("  ~w~n", [A])),
    findall(X-Y, ancestor(X, Y), Pairs), length(Pairs, N),
    format("All ancestor pairs: ~d~n", [N]),
    (   ancestor(dora, _) -> format("dora has descendants~n")
    ;   format("dora has none: the query FAILS, which is an answer, not an error~n")
    ).
