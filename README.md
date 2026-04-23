# PES-VCS — Version Control System

**Name:** Pavithra B  
**SRN:** PES1UG25AM807  
**Section:** C  

---

## Building & Running

```bash
sudo apt update && sudo apt install -y gcc build-essential libssl-dev

make all              # builds pes, test_objects, test_tree
./test_objects        # Phase 1 tests
./test_tree           # Phase 2 tests

./pes init
echo "hello" > file1.txt
./pes add file1.txt
./pes status
./pes commit -m "Initial commit"
./pes log

make test-integration  # full end-to-end test
```

---

## Phase 5 & 6 — Analysis Questions

### Q5.1 — Implementing `pes checkout <branch>`

To implement `pes checkout <branch>`:

**Files that change in `.pes/`:**
- `.pes/HEAD` must be rewritten to `ref: refs/heads/<branch>`.
- The working directory must be updated to match the target branch's tree.

**Steps:**
1. Read the current HEAD and target branch's commit hash from `.pes/refs/heads/<branch>`.
2. Read the target commit object → get its `tree` hash.
3. Recursively walk the target tree, and for each blob entry, write the file content to the working directory.
4. Delete any files tracked in the current HEAD's tree that do not exist in the target tree.
5. Update `.pes/HEAD` to point at the new branch.

**What makes it complex:**
- You must handle files that exist in the current branch but not the new one (delete them).
- You must detect and refuse to overwrite unsaved local changes.
- Subdirectories must be created/removed as needed.
- The operation must be atomic enough that a crash mid-checkout doesn't corrupt the working tree.

---

### Q5.2 — Detecting a "Dirty Working Directory" Conflict

To detect uncommitted changes without any other tools:

1. Read the current HEAD commit → get its tree object → expand recursively into a flat map of `path → blob_hash`.
2. Read the index → get a flat map of `path → blob_hash` for staged files.
3. For every file that differs between the two branches being switched:
   - Check if it appears in the **index** with a different hash than HEAD's tree → it has staged changes.
   - Check if the file's on-disk `mtime` or `size` differs from the index entry → it may have unstaged changes (re-hash to confirm).
4. If any such file is dirty in either sense, **refuse checkout** and report the conflicting file.

This uses only the index (for staged state) and the object store (for the committed state) — no diff tools required.

---

### Q5.3 — Detached HEAD

**What happens when you commit in detached HEAD state:**  
HEAD contains a raw commit hash instead of `ref: refs/heads/main`. New commits are created and HEAD advances (the hash in HEAD is updated), but no branch pointer moves. These commits are not reachable from any branch.

**How to recover those commits:**  
- If you know the commit hash (e.g. from terminal history), run:  
  `git branch recovery-branch <hash>` (or the PES equivalent: write that hash to a new file in `.pes/refs/heads/recovery-branch`).  
- Otherwise, Git's reflog (`git reflog`) stores a log of every HEAD movement. In PES-VCS without a reflog, you would need to scan all objects in `.pes/objects/` and find commit objects that are not reachable from any branch — this is the inverse of garbage collection.

---

### Q6.1 — Garbage Collection Algorithm

**Algorithm (mark-and-sweep):**

1. **Mark phase** — start from all branch tips in `.pes/refs/heads/`:
   - For each branch, walk the commit chain (following `parent` pointers).
   - For each commit, mark its `tree` hash reachable; recursively mark all blobs and sub-trees reachable.
   - Use a **hash set** (e.g., a hash table keyed by the 64-char hex string) to track all reachable hashes.

2. **Sweep phase** — enumerate every file in `.pes/objects/XX/YYY...`:
   - Reconstruct the full hash from the directory name.
   - If the hash is **not** in the reachable set, delete the file.

**Data structure:** A hash set (unordered set of strings) is ideal — O(1) average insert and lookup.

**Estimate for 100,000 commits, 50 branches:**  
- Each commit references ~1 root tree; each tree averages ~10 blobs/sub-trees.
- Rough object count per commit: 1 commit + 1–3 trees + 5–20 blobs ≈ ~10–25 objects.
- Total reachable objects: ~100,000 × 15 = **~1.5 million objects** to visit.
- With deduplication (shared blobs), the actual stored objects may be far fewer, but you still visit each unique object once.

---

### Q6.2 — Race Condition Between GC and Commit

**The race condition:**

1. A `commit` operation begins and writes a new blob object to `.pes/objects/XX/YYY`.
2. GC runs its **mark phase** — at this moment, the new blob is not yet referenced by any commit or tree, so it is **not** marked reachable.
3. The `commit` operation builds a tree pointing to that blob and writes the commit object.
4. GC runs its **sweep phase** — it sees the blob is unmarked and **deletes it**.
5. The new commit now references a blob that no longer exists → repository corruption.

**How Git avoids this:**  
- Git's GC has a **grace period** — any object newer than 2 weeks is never deleted, giving in-flight operations time to complete.
- Git also uses **lock files**: `gc.pid` and pack-file locks prevent GC from running while another git process holds a lock.
- Objects are written to disk before the commit object that references them, so a careful GC that checks object timestamps provides a safety window.
- In practice, Git's `git gc` is never run concurrently with write operations on the same repo.

---

## File Summary

| File | Role |
|------|------|
| `pes.h` | Core data structures and constants |
| `object.c` | Content-addressable object store |
| `tree.h / tree.c` | Tree serialization and construction |
| `index.h / index.c` | Staging area implementation |
| `commit.h / commit.c` | Commit creation and history walking |
| `pes.c` | CLI entry point and command dispatch |
| `test_objects.c` | Phase 1 tests |
| `test_tree.c` | Phase 2 tests |
| `test_sequence.sh` | End-to-end integration test |
| `Makefile` | Build system |
