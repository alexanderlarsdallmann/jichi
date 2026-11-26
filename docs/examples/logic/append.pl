% append.pl -- one relation, several directions ("modes").
% Run (from this directory):
%   swipl -q -g main -t halt append.pl < /dev/null

% app(Xs, Ys, Zs): Zs is Xs followed by Ys.
app([], Ys, Ys).
app([X|Xs], Ys, [X|Zs]) :- app(Xs, Ys, Zs).

main :-
    app([a,b], [c], Z),
    format("forward:   app([a,b],[c],Z)    gives Z = ~w~n", [Z]),
    app(X, [c], [a,b,c]),
    format("backward:  app(X,[c],[a,b,c])  gives X = ~w~n", [X]),
    format("splitting: app(P,S,[a,b,c]) gives every split:~n"),
    forall(app(P, S, [a,b,c]), format("  P = ~w, S = ~w~n", [P, S])),
    (   app([a], [b], [a,c]) -> true
    ;   format("checking:  app([a],[b],[a,c]) fails: it is simply false~n")
    ).
