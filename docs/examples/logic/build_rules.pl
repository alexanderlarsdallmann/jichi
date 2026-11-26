% build_rules.pl -- what `make` decides, written as logic.
% A target must be rebuilt if it is missing, if a prerequisite is newer than
% it, or if a prerequisite must itself be rebuilt. `make` answers exactly this
% question by working BACKWARD from the goal, which is how Prolog runs rules.
% Run (from this directory):
%   swipl -q -g main -t halt build_rules.pl < /dev/null

% depends(Target, Prerequisite)
depends(jichi,      'main.o').
depends(jichi,      'agent.o').
depends('main.o',   'main.c').
depends('main.o',   'agent.h').
depends('agent.o',  'agent.c').
depends('agent.o',  'agent.h').

% mtime(File, Time): the files that exist, with a modification time.
mtime('main.c',  10).
mtime('agent.c', 11).
mtime('agent.h', 30).          % the header was just edited
mtime('main.o',  20).
mtime('agent.o', 21).
mtime(jichi,     22).

stale(T) :- \+ mtime(T, _), depends(T, _).                    % missing target
stale(T) :- depends(T, P), mtime(T, Tt), mtime(P, Tp), Tp > Tt.  % newer prerequisite
stale(T) :- depends(T, P), stale(P).                          % stale prerequisite

main :-
    setof(T, stale(T), Ts),
    format("after editing agent.h, rebuild: ~w~n", [Ts]),
    (   stale('main.c') -> true
    ;   format("main.c is a source file: nothing to rebuild~n")
    ).
