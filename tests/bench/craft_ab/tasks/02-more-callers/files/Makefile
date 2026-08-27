CFLAGS = -std=c89 -pedantic -Wall -Wextra

report: report.c kv.c kv.h
	$(CC) $(CFLAGS) -o report report.c kv.c

clean:
	rm -f report

.PHONY: clean
