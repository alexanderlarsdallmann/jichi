% occurs.pl -- the same three questions asked of SWI-Prolog itself.
% Run (from this directory):
%   swipl -q -g main -t halt occurs.pl < /dev/null
main :-
    ( f(X, b) = f(a, Y) -> format("f(X,b) = f(a,Y): yes, X = ~w, Y = ~w~n", [X, Y]) ; true ),
    ( g(Z, Z) = g(a, b) -> true ; format("g(Z,Z) = g(a,b): no~n") ),
    ( unify_with_occurs_check(W, f(W)) -> true
    ; format("unify_with_occurs_check(W, f(W)): no~n") ),
    ( V = f(V) -> format("V = f(V) without the check: yes -- a cyclic term (SWI-Prolog supports them)~n") ; true ).
