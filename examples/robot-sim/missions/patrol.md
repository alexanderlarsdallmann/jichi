Patrol the area. Repeat: call read_sensors; if `obstacle` is 1, call stop_all
and report; otherwise drive_motor forward a short distance (left=1 right=1
duration=1). Stop when battery drops below 20 and report the final pose.

Keep each move small and re-check sensors between moves — you are the slow,
deliberate layer, not a servo loop.
