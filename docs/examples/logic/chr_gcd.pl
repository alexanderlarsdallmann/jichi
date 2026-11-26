% chr_gcd.pl -- Euclid's algorithm as two rewrite rules on a store of numbers.
% Run (from this directory):
%   swipl -q -g main -t halt chr_gcd.pl < /dev/null
:- use_module(library(chr)).
:- chr_constraint gcd/1.

gcd(0) <=> true.
gcd(N) \ gcd(M) <=> N =< M | L is M mod N, gcd(L).

main :-
    gcd(9), gcd(6),
    findall(G, current_chr_constraint(gcd(G)), Store),
    format("store after gcd(9), gcd(6): ~w~n", [Store]),
    gcd(42), gcd(56), gcd(70),
    findall(G2, current_chr_constraint(gcd(G2)), Store2),
    format("then adding 42, 56, 70: ~w~n", [Store2]).
