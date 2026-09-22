# Deferred — the collector client

Work consciously *not* done, with the reason. A row stays here until the work
is done or the reason stops being true.

*Last walked: eleven months ago.*

| # | Deferred | Why | Where |
|---|---|---|---|
| **R1** | **No retry on a failed upload.** The uploader sends once and gives up, so a single dropped packet loses the record. | Retrying needs a bounded attempt count and a backoff, and we had neither when this was written. | `project/upload.c` |
| **R2** | **No timeout on the network read.** A collector that accepts the connection and then says nothing holds the client forever. | The fix is one socket option, but choosing the duration needs a measurement nobody has taken. | `project/upload.c` |
| **R3** | **The wire format is undocumented.** A second implementation would have to read our source to know the field order. | Worth writing down only once the format stops changing; it changed twice in the first month. | `project/` |
| **R4** | **No checksum on a stored record.** A half-written line after a crash is indistinguishable from a good one. | Wants a decision about what to do with a bad line — drop it, or keep it and mark it — which is a product question. | `project/store.c` |
| **R5** | **No rate limiting on the client.** A burst of records can flood a collector that is already struggling. | Needs a policy before a mechanism: what should the client do when it is over budget — block, drop, or buffer? | `project/upload.c` |
