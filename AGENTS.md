# EventEdge engineering guidance

- Read the project documentation before making architectural changes.
- Stay within the requested EVE milestone; do not implement later milestones early.
- Prefer simple, explicit C++ over unnecessary abstraction.
- Use RAII and modern ownership practices; avoid raw owning pointers.
- Write tests for meaningful behavior and keep concurrency correctness explicit.
- Add dependencies only with a clear reason.
- Run relevant formatting, build, and test validation before handing work off.
- Do not fabricate benchmark results.
- Do not commit changes unless explicitly asked.
