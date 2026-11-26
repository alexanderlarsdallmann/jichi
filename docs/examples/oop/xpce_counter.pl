% xpce_counter.pl -- object-oriented programming in XPCE, driven from Prolog.
% XPCE is a class-based object system written in C (packages/xpce in the
% SWI-Prolog source); Prolog defines classes in it and sends it messages.
% No window is opened: these classes are not graphical, so it runs headless.
% Run (from this directory; needs SWI-Prolog with XPCE, e.g. Ubuntu's swi-prolog-x):
%   swipl -q -g main -t halt xpce_counter.pl < /dev/null
:- use_module(library(pce)).

% A class: one instance variable, one send method (:->), one get method (:<-).
:- pce_begin_class(counter, object, "Counts up").
variable(value, int := 0, both, "Current value").
increment(C) :->
    "Add one"::
    get(C, value, V), V1 is V + 1, send(C, value, V1).
doubled(C, D:int) :<-
    "Twice the value"::
    get(C, value, V), D is V * 2.
:- pce_end_class.

% A subclass that overrides increment and calls the parent's version.
:- pce_begin_class(bounded_counter, counter, "Counts up to a limit").
variable(limit, int, both, "Highest allowed value").
initialise(C, Limit:int) :->
    send_super(C, initialise), send(C, limit, Limit).
increment(C) :->
    get(C, value, V), get(C, limit, L),
    (   V < L -> send_super(C, increment) ; true ).   % at the limit: stay
:- pce_end_class.

% Delegation: a meter has no increment method of its own; it delegates to its
% counter slot, so an increment sent to the meter is resolved on the counter.
:- pce_begin_class(meter, object, "A labelled counter").
variable(label,   name,    both, "What it counts").
variable(counter, counter, get,  "The counter it delegates to").
delegate_to(counter).
initialise(M, Label:name) :->
    send(M, label, Label), send(M, slot, counter, new(counter)).
:- pce_end_class.

main :-
    new(C, counter), send(C, increment), send(C, increment),
    get(C, value, V), get(C, doubled, D),
    format("counter: value ~w, doubled ~w~n", [V, D]),
    new(B, bounded_counter(2)),
    forall(between(1, 5, _), send(B, increment)),
    get(B, value, BV),
    format("bounded_counter(2) after 5 increments: ~w (the override stopped it)~n", [BV]),
    new(M, meter(requests)), send(M, increment), send(M, increment), send(M, increment),
    get(M, counter, MC), get(MC, value, MV),
    format("meter: 3 increments sent to the meter reached its counter: ~w~n", [MV]),
    % In XPCE's C kernel a missing method is a status (PCE_ERR_NO_BEHAVIOUR);
    % the Prolog interface raises it as an exception, so catch it to see it.
    catch(send(C, no_such_method), E, true),
    (   nonvar(E), E = error(pce(Kind, [_Object, Arrow, Selector]), _)
    ->  format("an unknown message raises an exception: pce(~q, [<the object>, ~w, ~q])~n",
               [Kind, Arrow, Selector])
    ;   format("unexpected: no exception for an unknown message~n")
    ),
    get(class(bounded_counter), super_class, S), get(S, name, SN),
    get(class(counter), sub_classes, Subs), chain_list(Subs, SubL),
    maplist([K, N]>>get(K, name, N), SubL, SubNames),
    format("classes are objects too: bounded_counter's super_class is ~w; counter's sub_classes are ~w~n",
           [SN, SubNames]).
