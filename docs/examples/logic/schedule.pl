% schedule.pl -- constraint logic programming over integers (library(clpfd)).
% Three gate stages on one machine: each has a duration, none may overlap,
% and the sweep must finish before ci starts. State the constraints, then ask
% for the earliest finish. You do not write the search; you write the rules.
% Run (from this directory):
%   swipl -q -g main -t halt schedule.pl < /dev/null

:- use_module(library(clpfd)).

schedule(Starts, End) :-
    Starts = [Sweep, Redfirst, Ci],
    Starts ins 0..200,
    Durations = [5, 3, 60],              % minutes, as measured on the drive host
    Sweep + 5 #=< Ci,                    % the sweep finishes before ci starts
    no_overlap(Starts, Durations),
    End #= max(max(Sweep + 5, Redfirst + 3), Ci + 60),
    labeling([min(End)], [End|Starts]).

no_overlap([], []).
no_overlap([S|Ss], [D|Ds]) :- apart(S, D, Ss, Ds), no_overlap(Ss, Ds).
apart(_, _, [], []).
apart(S, D, [S2|Ss], [D2|Ds]) :-
    (S + D #=< S2) #\/ (S2 + D2 #=< S),
    apart(S, D, Ss, Ds).

main :-
    schedule([Sweep, Redfirst, Ci], End),
    format("sweep at ~d, redfirst at ~d, ci at ~d: all done at minute ~d~n",
           [Sweep, Redfirst, Ci, End]).
