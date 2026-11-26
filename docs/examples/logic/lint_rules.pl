% lint_rules.pl -- a jichi-style lint as rules over facts, and the trap in it.
% The rule: every smoke driver must be registered in run.sh, unless allowlisted.
% `\+` is negation as failure: "not provable from the facts given". That is the
% CLOSED-WORLD assumption: whatever the facts do not say is taken as false.
% Run (from this directory):
%   swipl -q -g main -t halt lint_rules.pl < /dev/null

:- dynamic driver/1, registered/1, allowlisted/1.

violation(D) :- driver(D), \+ registered(D), \+ allowlisted(D).

load_facts(Drivers, Registered) :-
    retractall(driver(_)), retractall(registered(_)), retractall(allowlisted(_)),
    forall(member(D, Drivers), assertz(driver(D))),
    forall(member(R, Registered), assertz(registered(R))),
    assertz(allowlisted(helper_lib)).

report(Label) :-
    findall(D, violation(D), Vs), aggregate_all(count, driver(_), N),
    format("~w: ~d driver(s) read, violations ~w~n", [Label, N, Vs]).

main :-
    load_facts([append_lint, docs_lint, helper_lib], [append_lint]),
    report("normal run          "),
    % The extraction of registered/1 broke and read nothing: loud, every driver fails.
    load_facts([append_lint, docs_lint, helper_lib], []),
    report("registered/1 empty  "),
    % The extraction of driver/1 broke and read nothing: SILENT, the lint is green.
    load_facts([], [append_lint]),
    report("driver/1 empty      "),
    format("A green answer over zero facts proves nothing: floor the count you read.~n").
