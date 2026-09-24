-- learning.sql -- the practice database of docs/SQLITE.md.
-- Build it with:  sqlite3 learning.db < learning.sql
CREATE TABLE lessons  (id INTEGER PRIMARY KEY, title TEXT NOT NULL, minutes INTEGER NOT NULL);
CREATE TABLE progress (lesson_id INTEGER NOT NULL REFERENCES lessons(id), done_on TEXT NOT NULL, score INTEGER);
INSERT INTO lessons (id, title, minutes) VALUES
  (1, 'Pointers and arrays', 45), (2, 'Hash tables', 60), (3, 'Recursion', 30),
  (4, 'Sorting', 50), (5, 'Big-O by measurement', 40);
INSERT INTO progress (lesson_id, done_on, score) VALUES
  (1, '2026-09-01', 72), (2, '2026-09-03', 58), (1, '2026-09-10', 91), (3, '2026-09-12', 85);
