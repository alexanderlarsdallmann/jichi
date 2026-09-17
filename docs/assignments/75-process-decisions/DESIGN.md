# Design (given) -- note-taking API
A small REST service over a single table notes(id, body).
- POST /notes creates a note and returns its id (R1).
- GET /notes/{id} fetches a note by id (R2).
- GET /notes lists all notes (R3).
- DELETE /notes/{id} deletes a note by id (R4).
