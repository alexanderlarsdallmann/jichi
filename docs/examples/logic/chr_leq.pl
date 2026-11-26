% chr_leq.pl -- Constraint Handling Rules: the classic "less than or equal".
% Each rule rewrites the CONSTRAINT STORE when its head matches:
%   Head <=> Body        simplification: remove the head, add the body
%   Head ==> Body        propagation: keep the head, add the body
%   Keep \ Remove <=> B  simpagation: keep part, remove part
% Run (from this directory):
%   swipl -q -g main -t halt chr_leq.pl < /dev/null
:- use_module(library(chr)).
:- chr_constraint leq/2.

reflexivity  @ leq(X, X) <=> true.
antisymmetry @ leq(X, Y), leq(Y, X) <=> X = Y.
idempotence  @ leq(X, Y) \ leq(X, Y) <=> true.
transitivity @ leq(X, Y), leq(Y, Z) ==> leq(X, Z).

main :-
    leq(A, B), leq(B, C), leq(C, A),
    (   A == B, B == C
    ->  format("leq(A,B), leq(B,C), leq(C,A): the rules proved A = B = C~n")
    ;   format("unexpected: A, B and C are not unified~n")
    ).
