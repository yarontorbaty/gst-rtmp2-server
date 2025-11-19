# Prompt: Commit, Push, Merge PR, Then Continue Debugging

Copy the block below into a fresh Cursor/chat session when you want the agent to finish the GitHub workflow and then continue investigating the remaining streaming issue.

---

You are assisting on the `gst-rtmp2-server` repo. Continue on branch `fix/parser-buffer-compaction`.  
Primary goals:
1. Commit the parser/FLV queue fixes that are already coded.
2. Push the branch, open/refresh the PR, and merge it into `main`.
3. After the merge, start a new chat thread to investigate the remaining “SRT pipeline stalls / timestamps” issue with the user.

**Context to remember**
- Relevant files that changed: `gst/rtmp2chunk_v2.c`, `gst/rtmp2chunk_v2.h`, `gst/rtmp2client.c`, `gst/rtmp2flv.c`, `gst/rtmp2flv.h`, `gst/gstrtmp2serversrc.c`, `PARSER_FIX_SUMMARY.md`.
- Leave all `.flv` capture artifacts and the helper rebuild scripts untracked; do not delete or commit them.
- Tests require the user’s environment (FFmpeg ↔︎ gst-launch). Provide instructions instead of running them yourself if the shell becomes unreliable.

**Step-by-step**
1. `git status -sb` and verify only the files listed above are staged for commit. If any `.flv` or helper scripts show up as tracked, unstage/remove them.
2. Stage the code + summary: `git add gst/rtmp2chunk_v2.* gst/rtmp2client.c gst/rtmp2flv.* gst/gstrtmp2serversrc.c PARSER_FIX_SUMMARY.md`.
3. Craft a commit message that captures:
   - fast-buffer compaction fix stays as-is,
   - new parser diagnostics,
   - FLV `pending_tags` queue now mutex-protected,
   - consumer side (serversrc) now locks queue before iterating,
   - reference to GitHub issue #6.
4. `git commit`.
5. `git push -u origin fix/parser-buffer-compaction`.
6. Use GitHub CLI:
   - `gh pr status` (if no PR, `gh pr create --fill`),
   - update the PR body to summarize root cause, fix, and manual test instructions (video-only + audio+video results),
   - `gh pr merge --squash --delete-branch` (confirm prompts).
7. After merge succeeds, capture the PR URL + merge hash for the handoff note.
8. Start/continue a chat with the user explaining the next investigation: SRT pipeline stalls due to timestamp/backpressure suspicion, propose plan (buffer inspection, queue depth logging, etc.), and wait for their guidance.

Deliverables:
- Terminal transcript snippets showing commit/push/merge.
- PR link + merge commit hash.
- Chat handoff summary describing what’s left (SRT timestamp/buffer issue) and a proposed plan of attack.

---

