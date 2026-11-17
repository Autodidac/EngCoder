## Epoch workspace enumeration port and visible EngCoder repair trial (2026-11-17)

- Inspected Epoch's workspace boundary implementation at `Engine/dep/EpochSelfCoding/integration/src/ai/native/ai.backend_workspace_workspace.cpp` read-only. EngCoder now uses the same containment principle for tree and search. Enumerated entries are checked for links, evaluated under their lexical workspace names and resolved before admission. The EngCoder regression verifies normal source results, ignored build directories and exclusion of file and directory aliases. Windows Release built and passed 82/82 CTest checks. This path check does not provide OS process isolation or certify behavior under hostile concurrent filesystem replacement.
- An independent regression in a separate EngCoder checkout reproduced strict JSON failing when a 240-byte Learning Ban preview cut a UTF-8 character. The Codex preview sent four runs to the existing Qwen 3.5 9B. Two stopped at the output/context limit. Two proposed incorrect edits and were denied in the UI. Receipts record both denials and terminal failures with no checkpointed edit. The final proposal used 213 prompt tokens after cache reuse in 304 ms and generated 319 tokens at 25.89 tokens/s. Sampled peaks across the mission were 7.92 GiB of model working set, 17.81 MiB of server working set and 6,928 MiB of whole-device GPU memory at up to 77% utilization. The model was then stopped.
- A separately authored correction in the candidate preserves complete UTF-8 characters and passed 82/82 tests, including two- and three-byte splits, an exactly aligned code point and long ASCII. Working-source approval is pending for this independent repair. The checked two-file patch and the mission receipt are under `out/self-iteration-2026-11-17/utf8-ban-preview`. Patch SHA-256 is `2879e0d26baf4d53ce62f3e337939a1e2d373f0f70a8729f8d135ee5b010e56e`.

## Remove-only training invalidation and live preview refresh (2026-11-17)

- Remove now serializes with weight activation and persists a new revision before rewriting verified evidence. The worker checks that revision at curriculum admission, across checkpoints and before activation. Cold-start source filtering and compilation share the lock so neither can label a stale copy with a new revision. A ledger write failure preserves the old evidence and can conservatively invalidate queued training.
- Windows Release built and passed all 81 CTest checks. The optional PyTorch environment passed eight CPU admission tests and seven CPU lineage tests, including rejection of a stale candidate after Remove without a Ban. The isolated Release HTTP smoke cleared one synthetic evidence record, confirmed the revision persisted, and verified that a repeated Remove preserved the revision without creating a denylist.
- The running Codex preview uses a copy of the new Release server outside the build directory. The same preview URL returned HTTP 200 and continues using existing review state with model autoload and training disabled.
- Previously active weights remain in place after removal. Candidates derived from a generation with an older policy are refused. The application still needs a guided rebuild or recovery path for that case.

## Visible preview restored and transactional evidence removal (2026-11-17)

- The prior preview process was absent. Started the last verified server as PID 21852 on loopback port 55462 with the existing review state and current shared frontend. Health returned `ok: true`; Codex browser inspection showed the prior chats and learning controls. Model autoload and training remain disabled; this is an idle preview, not an ongoing AI coding run.
- Evidence removal now stages a new episode vector, flushes/closes its file, atomically replaces disk data and only then updates memory/indexes. Forced staging failure preserves both records, deduplication and eligibility counters; reopening retains the same records and retry succeeds.
- Release test target built and CTest passed 81/81. The full build could not relink the running `EngCoderServer.exe` (LNK1104); the preview was deliberately left available. The newly repaired store is tested but is not yet running in that preview. Full server relink/restart remains required. Remove-only policy invalidation is still open.

## Native/worker lock and checkpoint race verification (2026-11-17)

- Added a test-only compiled C++ lock probe and registered OS lock and interoperability tests with CTest. Python-held ownership blocks native acquisition; native-held ownership blocks Python acquisition; both acquire after release. Full Windows Release build and CTest passed 80/80. POSIX remains unexecuted.
- Six CPU policy-lineage tests passed using real tiny artifacts. The newest test performs one real optimizer step, saves a checkpoint, changes the ban policy, and observes a training error before the next step. A retry with unchanged example data does not restore that stale checkpoint: it yields to a foreground-inference marker at step zero. The active pointer remains byte-identical. No CUDA or user model state was used.
- Ban-policy serialization and checkpoint invalidation now have direct evidence. Remaining revocation work includes Remove-only policy revisions, recovery UI for corrupt policies, and clear active-weight rollback/rebuild status. Previously learned content is not erased merely by banning it.

## Candidate policy lineage enforcement (2026-11-17)

- Compiled-policy preflight now passes its exact snapshot into training, closing the preflight-to-start gap before model allocation. Training validates inherited weights, ignores repair generations with stale/unrecorded policy, incorporates policy into checkpoint identity, and checks for policy changes between training steps.
- New generation manifests retain the starting policy. Immediate promotion, deferred promotion and rollback verify it under the shared policy lock before changing the active pointer. Random initialization is exempt because it has no learned evidence; historical trained artifacts without policy metadata are not silently grandfathered in.
- Five tiny real CPU-artifact tests passed: stale candidate rejection, matching-policy promotion then stale rollback rejection, legacy metadata rejection, post-preflight invalidation before model load, and immediate-promotion rejection. All rejected activation tests preserve active-pointer bytes. Seven compiler/tensor admission tests also pass. Test readiness flags are fixtures, not evidence of learned competence; no user weights or GPU were used.
- Still required: direct native/Python contention certification and a training/checkpoint race test. A policy mismatch is conservative and requires rebuilding from an eligible ancestor; this does not unlearn previously active weights. Remove without Ban is not yet a policy revision. No new native build is claimed after this Python-only follow-up.

## Shared learning-policy serialization groundwork (2026-11-17)

- Native Ban/Unban and the worker's three generation activation paths now acquire `learning/policy.lock`: exclusive byte zero on Windows, `flock` on POSIX. Locks are OS-owned, bounded to five seconds, released through RAII/finally and never removed from the filesystem. Tensor training does not hold this lock.
- Windows Release build and 78/78 CTest passed. Two standard-library worker lock tests passed, including real child-process contention/termination and exception cleanup. Seven CPU tensor-admission tests still pass. The POSIX implementation was not built on this Windows host; native/Python contention still needs a direct interoperability test.
- This is groundwork, not complete revocation: policy fingerprints must still be attached to candidate/checkpoint lineage and checked under this shared lock. No trained weights were activated by this pass.

## Compiled-policy preflight revalidation (2026-11-17)

- Tensor admission now requires the compiler's learning-policy snapshot and checks the current denylist digest both before and after loading sequences. Creating, changing or deleting the policy since compilation rejects the stale compiled dataset; malformed policies remain fail-closed.
- Seven real compiler/tensor-loader CPU tests passed using the existing optional PyTorch runtime, without loading a model or allocating CUDA. The new regression changes the policy after compilation and during sequence loading. No dependency installation occurred.
- This is preflight protection, not in-flight revocation: native policy mutation and worker promotion do not yet share a serialization boundary. Candidate/checkpoint lineage invalidation remains required. No new native build or weight promotion is claimed for this Python-only follow-up.

## Transactional learning-ban persistence (2026-11-17)

- Ban/unban now stage and verify the complete file, then use the existing platform-specific atomic replacement helper. Replacement failure never deletes the prior denylist. In-memory policy changes happen only after persistence succeeds; ban evidence removal follows successful policy persistence.
- A forced staging failure test verifies failed ban preserves evidence, failed unban preserves the active ban, disk bytes remain unchanged, a reopened service retains the prior ban, and retry succeeds once the obstruction is removed.
- Windows Release build succeeded; full CTest passed 77/77. No new Debug, visual QA, model training or release publication is claimed.
- Follow-up: malformed JSON, non-array policies, invalid entries and malformed hashes now fail learning-service initialization with an explicit path/error, leaving the original file intact. Five corrupt fixtures and a valid empty policy passed; rebuilt Release and full CTest passed 78/78. A recovery UI and in-flight candidate/checkpoint revocation remain open. Atomic ban persistence does not retroactively remove learned weights.

## Corpus coverage and raw-generation diagnostic (2026-11-17)

- Added a read-only, standard-library corpus audit with two passing tests for exact counts, non-mutation and malformed-row diagnostics. Audited bootstrap, seed and both anchor files without modifying shipped data or model state.
- Bundled bootstrap: 4,247 records, 190,369 assistant UTF-8 bytes, 1,663 distinct answers. Seed: 190 records, 24,924 assistant bytes, 42 distinct answers. Repeated author score dictionaries are not compiler/test receipts.
- Raw bootstrap anchors overlap 49 exact training conversations. Recompiled using the production guard's normalized Foundation scope: 462 selected representatives, 28,696 supervised byte tokens, 277 anchor-overlap exclusions, zero exact message-array overlap in the output. The first direct audit used the unnormalized `english-foundation` compiler label and selected nothing; the corrected run uses `internal_scope_for`, as production does. This audit does not prove semantic independence.
- Actual g000004 weights generated malformed text on three seen examples, without retrieval. Teacher-forced token accuracy was 38.6%, 66.7%, and 58.4%; cached/full-prefix maximum logit differences were below 0.000002 on eight probed target bytes. No optimization or promotion occurred during this diagnostic. Active weights remain unchanged.
- Local evidence: `out/self-iteration-2026-11-17/compositional-curriculum/` contains `generation-diagnostic.json`, `reproducible-bundled-audit.json`, and `bundled-compiled-manifest.json`. This tooling/documentation pass does not claim another native build, visual test or release.

## Reviewed curriculum contract and measured rerun (2026-11-17)

- Compiler and tensor loader now share explicit independently attributed review receipts. Self-review, changed material, malformed review hashes, rejected/private/error content and held-out-to-gradient reclassification are rejected. Training now preflights the compiled hash and exact loaded sequence count before model allocation. Six CPU tests exercise real compile-to-loader behavior; eight denylist tests also pass. The integrated Windows Release build and full CTest suite passed 76/76. Review attribution is not reviewer authentication.
- The corrected experiment preflight loaded 237 training sequences and 146 evaluation sequences. It excluded one legacy human-only item under the existing diversity threshold and two legacy anchors under volatile-content policy. All 128 new training examples and all 128 new heldouts loaded in their separate roles. Longest training sequence was 372 byte tokens within the actual 1,024-token context; zero sequences were truncated and zero heldout sequences entered gradients.
- With a 512-step caller cap, the remaining cumulative target bounded execution to 328 steps / 82 optimizer updates, reaching 1,024 total steps. g000004 held-out loss fell from 2.553535 to 1.386442 on the corrected evaluation set. This is not directly comparable to the previous 20-anchor run. Generation readiness and all four fresh natural-dialogue probes still failed. Active generation remained g000000; promotion was explicitly deferred. Monolithic weights remained 118,449,136 bytes.
- Exact receipts: `out/self-iteration-2026-11-17/compositional-curriculum/admitted-preflight.json`, `admitted-metrics.json`, `admitted-training.log`, and `admitted-responses.json`. A small CPU decoder-consistency probe matched single-token cached logits against full-prefix logits across six positions (maximum absolute difference 5.96e-8). That narrow check is not language competence or comprehensive decoder certification.
- The next learning work is broader reusable English coverage plus fresh-response evaluation, not more near-duplicate corrections or claiming successful conversation from lower loss. No trained weights were promoted and no release was published.

## Admission integration and failed transfer experiment (2026-11-17)

- Integrated the four-file denylist admission repair. Existing-dependency Windows Release build succeeded; CTest passed 76/76. The guard now labels its startup root as a default and reports the effective command-specific root, avoiding misleading isolated-run diagnostics. Active-job revocation and policy/weight-lineage coordination remain open.
- Independently reviewed synthetic training records were staged separately from original unaccepted candidates, retaining human_accepted=false and the review receipt hash. Compiler admission selected all 128 (16 per rule, 4,622 supervised byte-token targets). Combined with the earlier batch, the compiler selected 238 records and reserved 148 anchors.
- A bounded isolated CUDA run executed 256 steps / 64 optimizer updates with promotion explicitly deferred. It revealed an admission mismatch: the numerical loader discarded the independently reviewed examples and the ineligible heldout records, consuming only the earlier batch and 20 anchors. Therefore this is NOT a valid test of the new compositional curriculum. Loss changed from 2.086356 to 2.053140; g000003 failed readiness and four raw natural-dialogue probes. Active generation remained g000000.
- Exact receipts are `out/self-iteration-2026-11-17/compositional-curriculum/experiment-metrics.json`, `experiment-training.log`, and `experiment-responses.json`. The model's existing retention compacted its old repair parent, so the response probe reports the candidate alone, not a matched before/after comparison. No retrieval shortcuts or rubric answers entered inference context. The teacher models stayed unloaded; sampled whole-device VRAM was about 3,013 MiB during training and returned to about 1,927 MiB afterward.
- The compiler/loader provenance mismatch and evaluation-purpose handling must be corrected and preflighted before another experiment. Do not claim generalization from this run or change approval fields to impersonate a human/reference verifier.

## Compositional curriculum and admission audit (2026-11-17)

- Researched small-model synthetic curriculum and systematic generalization; source links and experiment criteria are in `docs/compositional-training.md`. Built 128 original fictional practice candidates and 128 template/composition-held-out checks under `out/self-iteration-2026-11-17/compositional-curriculum`. Independent review corrected preference versus confirmed-plan targets, balanced value roles and removed repeated acknowledgement filler. Five structural tests passed; 4,238 assistant UTF-8 bytes per split is a byte measurement, not a universal token count.
- The real compiler admitted zero unreviewed examples, preserving eligibility controls. Its signature audit found 128 distinct training signatures with no cross-rule collisions or held-out overlaps; this does not prove abstraction or model learning. No model loaded, gradient training or promotion occurred in this pass. Twelve independently authored natural-dialogue heldouts and semantic rubrics are separate from gradient material; the reviewer rechecked repaired targets and recorded hashes in review.md.
- Found the internal-training path bypassing denylist-aware episode ingestion through curriculum compilation. An isolated four-file admission repair passes eight CPU tests plus seven bootstrap tests and an apply-check against current source. Patch: `out/self-iteration-2026-11-17/denylist-admission.patch`. This repair is not integrated and does not solve revocation during active training or promotion; those require policy-version and candidate-lineage coordination.

## Approved maintenance integration (2026-11-17)

- The user explicitly approved integration of the reviewed candidate. Applied the exact 40-file patch (`e1318fa5ef181b262ed94b3e8654f5f95c2e8b2c3509974fca69054061b3772b`) after rechecking the baseline. All integrated files match the candidate with line endings normalized. Earlier pending-integration entries below describe historical checkpoints.
- Built the working source successfully in Windows x64 Release at `out/build/dependency-refresh`, serially with MSBuild node reuse disabled. Integrated CTest passed 75/75; all 23 Web checks passed. No new Debug or cross-platform build is claimed. The existing vcpkg bootstrap downloaded its required CMake/7-Zip helpers; application dependencies were already installed.
- Updated the mission record and retained all 29 missions. Source integration does not approve weight promotion or release publication. No model was loaded and no training job ran during integration. The earlier conversation candidate remains rejected and not suitable for conversation.

## Interface repair preview (2026-11-17)

- Repaired overlapping Remove/Ban actions, wrapped feedback text, immediate saved-state controls, and cancellation/error handling that previously could clear saved feedback incorrectly.
- Repaired narrow Lab header/tab overlap with one sticky header container and wrapping tabs; training fields reflow, Refresh shares normal button styling, and disabled training controls remain disabled.
- Native terminal training state now takes precedence over stale progress. Model assignment cards say Configured rather than falsely claiming physical residency.
- Visually checked the served candidate in Codex at normal width and 360 x 800, including scrolling Lab and opening Session Settings. All 23 Web checks passed. Independent review found and repaired successful Remove/Ban/Unban operations being misreported when only their subsequent refresh failed; the server mutation result now remains authoritative and a separate refresh warning is shown. Full Windows Release CTest passed 75/75 with normal permissions; restricted execution failed loopback/filesystem-dependent fixtures. No real model was loaded by these tests.
- The visible host-only preview used about 12.6 MiB RAM when sampled. The previously launched 9B worker was stopped for the user's concurrent workload. These repairs remain in the isolated candidate; working-source promotion and publication have not occurred.
- The consolidated review covers 40 changed source/test files against `446d7ed`; exact patch SHA-256 is `e1318fa5ef181b262ed94b3e8654f5f95c2e8b2c3509974fca69054061b3772b`. Candidate hashes and the 75/75 native plus 23/23 Web receipts were rechecked in `out/self-iteration-2026-11-16/review/current-checkpoint-verification.json`. The accompanying `goal-acceptance-audit.md` distinguishes verified candidate work from pending integration and incomplete conversation training.

## 0.7.5 live Codex C++ repair and Run info (2026-11-17)

- The Windows Release host served the updated shared UI inside Codex at 1,280 × 720. The disposable fixture built successfully and failed its row-bounds test before the edit. EngCoder's existing Qwen 3.5 9B read `selectable_row.cpp`, then proposed replacing `index <= row_count` with `index < row_count`. The operator approved the single edit in chat. It created checkpoint `cp_6012065325dc_7_dc976effc4229fe5`. The test source and CMake configuration retained their original hashes.
- The agent's Release build and CTest each returned exit 0. The row test covers empty input, equality at the upper boundary, the last valid index and the maximum `std::size_t`. Run info showed the approval, checkpoint, passing build and test receipts, and aggregate success after reload and reopening the inspector. The candidate reply offered no chat training feedback.
- The task took 43.8 s from acceptance to the durable terminal event, including review. The proposal turn evaluated 143 prompt tokens in 306 ms after prefix reuse and generated 123 tokens at 21.87 tokens/s. The provider used a peak sampled 5.69 GiB working set, with 14,964 MiB and 40% utilization at the whole-device GPU peaks. The final isolated EngCoder Windows Release build passed 70/70 tests independently. The prior 19/19 Web result covers the same frontend. Working-source integration remains open.

## 0.7.5 isolated Run info checkpoint and validation receipts (2026-11-17)

- In a bounded task on `projects/web/js/api.js`, EngCoder's existing Qwen 3.5 9B proposed the checkpoint label. Its first draft dropped ordinary tool-completion text and was declined. The revised exact patch preserved that text and was approved in the disposable candidate. The native task recorded checkpoint `cp_5faf0684af84_a_ae33261816c8f950`, Release build exit 0, CTest exit 0, and aggregate success.
- The approved run took 77.9 s including review and checks. On the successful proposal turn the provider evaluated 165 prompt tokens in 327 ms after prefix reuse and decoded 121 tokens at 22.69 tokens/s. Peak sampled working sets were 5.54 GiB for the provider and 13.47 MiB for EngCoder. Whole-device GPU memory peaked at 14,778 MiB and utilization at 34% during the sampled run. The preview reused the existing GGUF and its temporary 24-layer setting.
- Integration keeps approval, a completed checkpointed edit, individual build and test results, and the final task outcome separate. The summary retains check receipts across timeline truncation and reload, groups updates to the same check, and leaves an interrupted check at Started until a result is recorded. Ordinary model completion appears as Model finished. All 19 Web smoke checks and application JavaScript syntax checks passed. The new receipts were verified with native events and a Web regression fixture, then visually in the live run above.

## 0.7.5 candidate approval and native installer cancellation receipts (2026-11-17)

- The final Windows Release server ran three self-iteration approval cases with a scripted loopback provider. Deny and Stop left the CRLF fixture byte-identical. Approving after an intervening edit preserved the new contents and reported the changed preimage. All three emitted exactly one terminal event and started no build or test. Wrong-call and post-terminal approvals returned HTTP 409 in all three cases.
- A compiled local stand-in for the native model process held startup open. The Release server returned HTTP 409 for clearing the cache during installation. Unload cancelled each of two explicit install attempts in 710 ms and 722 ms, measured to observing the `cancelled` state. The fixture recorded exactly two child launches. It never reported ready or transferred weights. Actual readiness-boundary timing and cancellation during a real download remain open.
- Both independent receipts identify the tested `EngCoderServer.exe` by SHA-256 `ee08efdf658fb00d2ef62325811396066c8974630bdf3073cffbfb0e3c00f549`.

## 0.7.5 isolated native-model cache coordination (2026-11-17)

- A local fixture reproduced transient cache clearing and profile removal going through while backend installation held its worker open. The isolated repair coordinates install admission, model preparation, profile changes, and cache removal. The regression uses two competing callers, verifies busy results throughout installation, and checks successful retry after the fixture installer exits. It also checks confirmed profile and cached-file removal. It writes synthetic files and downloads no weights.
- EngCoder's existing Qwen 3.5 9B inspected the candidate manager source and proposed the two-line install admission guard. The approved candidate edit created checkpoint `cp_5e63370b9200_17_c4670789387be784` after 139.1 s including review. The provider evaluated 429 prompt tokens in 373 ms after prefix reuse and decoded 549 tokens at 21.23 tokens/s. The preview watchdog ended the model-owned validation before it produced a result. The operator integrated guards for the competing operations and ran the checks independently.
- Independent review also found an unload race during install handoff and a startup recovery path that could retry just after an unload. The candidate waits for the install worker to be registered before requesting Stop and checks for cancellation again after the readiness probe. The final Windows x64 Release build passed 70/70 CTest cases and all 19 Web smoke checks passed. The exact live cancellation timing and verification on Linux and macOS remain open, as does working-source integration.

## 0.7.5 in-Codex restart and saved-chat recovery (2026-11-17)

- EngCoder's existing Qwen 3.5 9B proposed the desktop profile repair after its first draft retained the old overrides. The approved patch moved the profile under the configured data directory while preserving the ordinary Windows profile path. It created checkpoint `cp_5d63ab82226c_a_f5939525e6a00763`; the Windows Release build and 69/69 CTest cases passed. The task took 89.0 s. On the successful proposal, the provider evaluated 589 prompt tokens in 597 ms after cache reuse and decoded 197 tokens at 27.43 tokens/s. With 24 offloaded layers, the peak provider and host working sets were 5.59 GiB and 12.84 MiB. Whole-device GPU memory peaked at 7,053 MiB and utilization at 66%.
- Standalone desktop launch created its Edge profile in the disposable data directory. The interaction moved to Codex's browser preview at the user's request. There, a real Release backend with a scripted provider showed the pending exact edit. After restart, the approval card cleared, the failure persisted in Run info after reloading, the cursor advanced from 14 to 15, the fixture source stayed byte-identical, and the provider received one request total. Full standalone desktop restart interaction remains open.
- The chat reopen fixture reproduced a missing observer when starting in another chat and selecting the saved task's chat. The 9B model proposed the selection guard and reconnect call in separate drafts. The operator integrated both in the click handler. The fixture passed with one event observer, no task resubmission, no repeated render on same-chat clicks, and a cleared stale approval after the terminal event. All 19 Web smoke checks passed. Product changes remain in the isolated review candidate.

## 0.7.5 isolated task restart recovery (2026-11-17)

- The native candidate persists a queue receipt before acknowledging submission. An exclusive task host for each data directory serializes event sequences and marks unfinished queued or started tasks as interrupted on startup. Existing terminal events and source/checkpoint bytes are preserved. Recovery never replays approvals or model work. The new ownership check applies between builds that implement it.
- The existing Qwen 3.5 9B proposed the standalone server startup check. The isolated approval created checkpoint `cp_5cdffdf21158_5_1d6a5fba51d978be`, and its build and CTest passed. The model's completion summary claimed a runtime error that was absent from the patch. The operator added the error propagation during integration. The native task took 173.7 s including review and validation. The provider evaluated 1,642 prompt tokens in 997 ms and decoded 112 tokens at 28.99 tokens/s. With 24 offloaded layers, peak working sets were 5.54 GiB for the provider and 12.07 MiB for EngCoder. Whole-device GPU memory peaked at 6,678 MiB and utilization at 74%.
- The final Windows x64 Release build passed 69/69 CTest cases and all 19 Web smoke checks. A separate test used a real Windows Release server and a scripted loopback provider. A competing host was refused, killing the first host left an approval and a queued task, and the replacement persisted one restart failure per task. The old approval returned HTTP 409, the fixture source stayed byte-identical, and a second restart added zero events. The browser fixture covers replaying a saved terminal cursor and avoiding duplicate output when replaying saved text. Working-source integration still awaits approval.

## 0.7.5 isolated bounded workspace reads (2026-11-17)

- The workspace tool regression reproduced unbounded candidate reads and ignored line ranges. The isolated candidate now defaults to 40 complete lines and supports one-based `start_line` and `max_lines` with a ceiling of 80 lines and 3,072 source bytes. Pages preserve the original line endings and return `next_line` for continuation. A line larger than the byte ceiling returns a diagnostic with the next line number. Ordinary reads without range arguments keep their existing response.
- Two live 9B proposals for the argument guard used invalid C++ signatures. The operator left both unapplied and implemented the guard and read paging in the isolated checkout. The Windows x64 Release build and 68/68 CTest cases passed, including the registered-tool paging test.
- The fresh Release server ran the existing Qwen 3.5 9B with a read-only diagnostic on the 80,288-byte `agent_runtime.cpp`. It returned the exact 1,739 bytes for lines 1–40 and `next_line` 41. The provider accepted the next turn and decoded 96 tokens at 11.21 tokens/s after evaluating 474 prompt tokens in 657 ms following prefix reuse. The run reached the expected “No checkpointed candidate edit was completed” terminal event after 14.3 s. Peak working sets were 5.70 GiB for the provider and 11.26 MiB for EngCoder. Whole-device GPU memory peaked at 13,658 MiB and utilization at 62% with 12 layers offloaded. The working source awaits integration approval.

## 0.7.5 isolated patch approval preflight (2026-11-17)

- The native candidate fixture reproduced prompts for empty, absent, overlapping, and unchanged patches. The candidate now checks the same matching rules before review and execution. It reports the reason to the model and the Run info timeline. Valid patches still require per-edit approval, and edits to the file while approval is pending still invalidate that approval.
- A first Qwen 3.5 9B attempt read the whole runtime source and exceeded its 8,192-token context. With exact source excerpts and explicit session instructions, the next attempt authored the preflight call in the isolated checkout. It evaluated 595 prompt tokens in 1,395 ms after cache reuse and decoded 190 tokens at 11.52 tokens/s. The sampled provider and EngCoder working-set peaks were 6.05 GiB and 12.22 MiB. Whole-device GPU use reached 13,226 MiB of 16,303 MiB with a 71% utilization peak while running with 12 offloaded layers.
- The local approval API recorded the exact tool approval and checkpoint `cp_5ba4063c4960_a_615488a92da158e7`. The live candidate build passed. Its test run exposed fixture state that was reused between two simulated repair tasks. The live task ended after 128.4 s including approval and validation. The operator reset the fixture state and formatted the model edit. The independent Windows x64 Release rebuild and 68/68 CTest cases passed, as did 19 Web checks. The working-source integration bundle is staged for review.

## 0.7.5 isolated Windows patch follow-up (2026-11-16)

- A new native test reproduced four failures through the registered `workspace.patch` tool: LF text could fail to match CRLF, a multiline replacement could introduce mixed endings, overlapping matches could be accepted, and a match could split a CRLF pair.
- The existing Qwen 3.5 9B model proposed an unchanged replacement, then a patch with undefined variables and an unmatched preimage. Both were left unapplied. One intervening task hit the provider's upstream-slot guard. The retry ran after the provider reported release. With 12 GPU layers, the last proposal evaluated 442 prompt tokens in 648.69 ms after prefix reuse and decoded 443 tokens at 11.32 tokens/s. Peak working sets were 5.65 GiB for the provider and 12.82 MiB for EngCoder. Whole-device GPU memory reached 13,627 MiB of 16,303 MiB and utilization reached 69%. The preview opened, but browser input timed out and the local task API supplied the proposal receipts.
- The operator finished the repair in the isolated candidate. Uniform CRLF files accept LF in patch arguments and retain their line endings. Mixed-ending files still match byte for byte. Checkpoints restore the original bytes, and the existing raw approval comparison remains in place. The Windows x64 Release build and 68/68 CTest cases passed with the suite's loopback and temporary-profile access. The working checkout awaits approval before integration.

## Conversation transfer curriculum follow-up (2026-11-17)

- Compiled 110 conversation records with 7,604 supervised byte tokens under a 16,384-token budget, reserving the existing 20 holdouts. Added 12 authored fictional examples covering clarification, changed context, references, constraints, uncertainty, and distinguishing intended actions from completed actions; no additional greeting variants or C++ examples were added. Receipt: `out/self-iteration-2026-11-16/conversation-curriculum/transfer-manifest.json`; selected JSONL SHA-256 `de9ff0d5acca2b698cfb174aaa55e3dff7f7ae7c0c16fc476e5ff6eba2d8fd45`.
- Located the existing Python 3.12.10 / PyTorch 2.10.0+cu128 runtime under `C:/Users/iammi/Documents/ComfyUI_Fresh/ComfyUI_windows_portable_nvidia_cu128/ComfyUI_windows_portable/python_embeded`. No package install or model download was needed. The first full-backbone run crashed at its first readiness evaluation because `LoadedInternalModel` lacked the required dtype argument. The isolated candidate supplies the model's actual dtype; its focused regression passes for float32 and float64.
- The repaired CUDA experiment completed 440 training steps / 110 optimizer updates in 265.125 seconds, consuming 119,492 supervised byte tokens through repeated exposure to the compact batch. Held-out loss fell from 5.568701 to 2.086356. All generation-readiness canaries failed; fresh prompts also produced repetitive word fragments. The 20-anchor experiment does not satisfy the 320-anchor promotion requirement. Generation `g000001` was rejected, and the active pointer remained `g000000`. This is actual full-weight learning, not evidence of conversational competence. Exact results: `transfer-metrics.json` and `transfer-response-receipt.json` in the same curriculum directory; retrieval shortcuts were disabled for raw responses.
- Model parameter count stayed 29,605,248. Each measured generation contained 118,449,136 bytes in its monolithic float32 weights and 168,012,864 bytes across all weight files including separate addon exports. Training did not grow these files. All seven non-Foundation addon exports, including both C++ addons, retained their initial SHA-256. Checkpoints and retained generations are additional storage, not extra deployed model capacity.
- Isolated training repairs also initialize a cold Foundation's backbone through the normal worker boundary, keep post-target repair steps within the caller's cap, freeze shared normalization during addon updates, and reject incompatible old scoped checkpoints before loading their tensors. Twelve bootstrap/scope tests and one dtype test passed. Windows x64 Release build and CTest 71/71 passed. Direct embedded-Python worker launch initially failed to resolve sibling imports; the candidate now resolves its own backend directory, and `doctor` confirms internal training ready.
- The real native `/api/engcoder/training/start` endpoint resumed the rejected candidate for exactly one requested step, reached cumulative step 441, rejected generation `g000002`, and left the active pointer unchanged. This verifies the native-to-worker path and repaired step bound, not a successful conversational model. Receipt: `native-training-result.json`. Its mixed progress/final JSON output exposed a separate native status-parser defect: final metrics were retained as raw output rather than structured top-level fields. The isolated candidate now parses progress and terminal results separately; four focused cases (26 assertions) and the full Release suite (75/75) pass. The real GPU job was not repeated after this parser repair.
- Training direction: reusable English conversation belongs to KeyMaker Foundation; character-specific role-play and C++ skills remain separate addons. Supply changing character roles and scene facts as context rather than duplicating them in Foundation weights. Judge future checkpoints on unseen conversations and model bytes, not the number of memorized greeting responses.

## 0.7.5 isolated task repair and conversation training (2026-11-16)

- Independent native cases reproduced missing terminal events for cancellation before model preparation and for failed preparation. An existing Qwen 3.5 9B GGUF ran in place on loopback and proposed the two exact C++ edits. The operator denied malformed proposals and approved the final edits in the isolated EngCoder checkout. The candidate passed the Windows x64 Release build and 67/67 CTest cases. Run info showed both passing receipts and the final task completed in 50.9 s. The working source still awaits approval for integration.
- A whole-file read overflowed the 8,192-token model context. An otherwise correct LF patch could not match the CRLF preimage. Scoped input excerpts, LF in the disposable candidate, and direct generation enabled the successful run. The final patch turn evaluated 129 uncached prompt tokens in 105.50 ms and decoded 120 tokens at 84.31 tokens/s. Over the sampled session, peak working sets were 6.18 GiB for the provider and 15.47 MiB for EngCoder. Whole-device GPU memory peaked at 15,255 MiB of 16,303 MiB and utilization peaked at 96%. Those GPU samples include other desktop workloads.
- In a normal conversation in the isolated UI, thumbs-up persisted one eligible `keymaker-foundation` evidence row. The expanded conversation compiler input contained 102 authored rows and that verified chat row. It kept 98 unique behavior clusters with 6,931 supervised byte tokens and reserved 20 independently written holdouts. C++ and role-play were outside this batch.
- A separate one-step RTX 5080 CUDA smoke used the earlier 28-example subset with eight holdouts. It executed one optimizer update and evaluated all eight. The candidate failed the Foundation anchor gate and was rejected. The isolated active pointer still selected its initial generation. No trained generation was promoted, and production weights and feedback stayed unchanged.

## 0.7.5 self-iteration checkpoint (2026-11-16)

- Run info shows baseline and candidate build and test receipts, attempt numbers, and failing exit codes while the full timeline stays collapsed. Verified in the served Windows Release UI and in the Web fixture.
- The disposable C++23 row-selection candidate compiled. Independent CTest probes reproduced two failures and one pass. The 0.5B Qwen Coder run reached its token limit without an edit. The 4B Qwen Instruct run read the candidate and proposed a patch that still admitted the one-past-end index and removed a brace. The operator denied it through the visible UI. The task recorded that no checkpointed edit completed. The candidate source, tests, and CMake inputs retained their starting hashes.
- For the 4B run, llama.cpp measured 1,321 initial prompt tokens in 168 ms. The patch turn decoded 325 tokens at 165.3 tokens/s. Its provider used 5.89 CPU seconds and peaked at 2,866 MiB of working set across the sampled run. The RTX 5080 peaked at 6,312 MiB and 92% utilization across the device, with 2,136 MiB in use before launch. The active EngCoder process peaked at 13 MiB of working set. Samples do not isolate per-process VRAM.
- Separate EngCoder source tests reproduce missing durable terminal events after early cancellation and provider preparation failure. The native repair remains a candidate mission. The test models were stopped and the two temporary GGUF files were removed.

## 0.7.5 dependency and output layout follow-up (2026-11-16)

- Clean Windows x64 builds against vcpkg 2026.10.16 passed 67/67 CTest cases in Debug and Release. The registry resolved curl 8.23.0, cpp-httplib 0.59.0, SDL 3.4.18, ImGui 1.92.9, and Catch2 3.16.0. Linux and macOS builds were not run.
- All 19 Web checks passed. The responsive fixture covers lost pointer ownership and capture, keyboard resize, and compact fallback. A browser smoke against the isolated Release server verified the divider at 1,440 × 900, keyboard resizing from 380 to 404 CSS pixels, width persistence after reload, and the overlay at 620 × 800.
- Reviewed [EpochGui 0.90.3](https://github.com/Autodidac/EpochGui/tree/336d3791bc4a4325e8160352c7279d4d0fd605bb). The shared EngCoder frontend applies its bounded splitter and cancelled-pointer behavior with native browser controls.

## 0.7.5 chat lifecycle follow-up (2026-11-16)

- Windows x64 Debug and Release builds each passed 67/67 CTest cases. All 19 Web smoke tests and JavaScript syntax checks passed.
- The chat lifecycle fixture covers a Stop click before task registration, a reply completing before Stop arrives, retry after a failed cancel request, keeping the task ID after disconnection, malformed event polling, and refreshed approvals that receive stale acknowledgments. Real-window verification and task recovery after a native runtime restart remain open.

## 0.7.5 provider and approval follow-up (2026-11-16)

- Windows x64 Debug and Release builds each passed 67/67 CTest cases. Loopback fixtures cover Chat Completions and Responses output limits, a streamed error forwarded by an OpenAI-compatible endpoint, incomplete streamed tool calls, valid tool-only replies, provider cancellation, and recovery from an evicted-model fixture. Architect's fallback test covers both an unavailable responder and an explicit cutoff.
- All 18 Web smoke tests and JavaScript syntax checks passed. The approval regression checks node identity through activity updates, replacement for a new approval, and cleanup when finished. Fresh visual QA and GPU-model reload remain open. All 29 open Missions are still present.
- An isolated Windows Release smoke used the 15M-parameter [TinyStories GGUF](https://huggingface.co/ggml-org/models-moved/blob/main/tinyllamas/stories15M-q4_0.gguf) on CPU. A one-token cutoff reached both the buffered HTTP 422 response and the SSE error event. Longer live prompts also reached their cap, so ordinary successful completion was verified with fixtures. No production model was loaded.

## 0.7.5 development baseline (2026-11-15)

- Windows x64 Debug and Release builds passed all 66 native tests each. All 18 Web smoke tests and all application JavaScript syntax checks passed.
- Compared the October 14 source export against its October 9 base and carried its existing-GGUF profile linking and per-model load override into the November source. The native tests cover profile persistence, split shards, invalid paths, and preservation of GPU settings. The Web tests cover the link controls, candidate approval behavior, and the 50% default GPU setting. These checks use fixtures rather than a live shared model.
- The Missions viewer now includes all 29 open entries. Four live routing acceptance checks were previously outside the sections consumed by the generator.
- The model-library follow-up passed 66 native tests in both Windows configurations and all 18 Web smoke tests. Fixture checks cover invalid shard numbers, saving the profile only after valid selection, the single reload request, and refreshing the model card after a rejected load. The rebuilt Release server rejected the invalid shard through both Link and Reload and preserved the saved profile. Validation with a running GPU model remains open.

## Candidate UI follow-up (2026-11-10)

- Web regression tests cover candidate replies with persisted flags or legacy activity provenance, ordinary retries while the candidate switch is armed, approval event ordering, and removal of the approval card at terminal completion. This follow-up changes the shared frontend and documentation only.

## 0.7.4 Windows release (2026-11-10)

- Windows x64 desktop, server, and CLI built in Debug and Release. CTest passed 65/65 in each configuration, including stale approval, create-only writes, repair escalation, and learning recall. All 16 Web smoke tests and 10 application JavaScript syntax checks passed.
- The served desktop interface ran a staged, fresh unsigned reservation repair against independent MSVC and CTest checks. The Qwen 3.5 9B model wrote the initial edit. CTest identified a remaining boundary failure and the native runtime handed the unchanged candidate and diagnostics to Qwen 3.8 27B. The stronger model's first revision still failed. Its next approved revision passed all three cases.
- The visible Edge preview showed the model handoff in Run info and one compiler-and-tests memory in EngCoder Lab. The tests and CMake inputs stayed byte-identical. The two local models used one provider slot sequentially. The run stored a scoped capacity rule as retrieval memory, with no weight update or source ingestion.

## C++ boundary lab (2026-11-10)

- Windows Debug and Release CLI and test builds passed, with 64/64 CTest cases in both configurations. The CLI ran the live reference lookup, C++23 compilation, repair verification, deduplicated training, and serialized inference.
- Four verified option sets remained after deduplicating 64 compiler-checked training cases. The 56-weight candidate occupied 228 bytes, below its 256-byte cap. It selected 16/16 correct repairs on the fresh split versus 4/16 for the zero-weight tie-break baseline. Exhaustive verification also passed 16/16. The fresh split changes syntax, values, context nouns and option positions within the four known families.
- On the identical 16 prompts with an A–D decoder grammar, local Qwen 3.5 9B scored 12/16 and Qwen 3.8 27B scored 16/16. Both returned all 16 choices. With plain prompts, the 27B also scored 16/16 and the 9B scored 10/16 with six responses stopped at the output limit.
- The CLI selected the strict upper bound for a new zero-based index example and rejected a wrong-size artifact. A verified local clamp choice made no teacher call. The same clamp with zero weights failed two probes, skipped a missing advertised model, then accepted a live 27B choice after independent verification.
- Scope: four known repair families, authored choices, and three fixed public C++ draft references. No unrestricted code generation, conversational English, or active Foundation or addon weight promotion was exercised.

## 0.7.3 Candidate self-iteration (2026-11-10)

- Windows VS2022 x64 Debug and Release builds passed. CTest passed 62/62 in each configuration. The new native test covers approved repair, denied edits, an out-of-candidate cache, and exhausting the two-repair limit.
- All 16 Web smoke tests and 10 JavaScript syntax checks passed. All 25 mission entries synchronized.
- A loopback smoke test drove the Release server with a scripted streaming provider. An approved exact edit caused a real MSVC compilation failure. The next model request received the diagnostic, a second approved edit fixed it, and the real rebuild and CTest passed. The served UI exposed the candidate control.
- Live KeyMaker inference, interactive desktop acceptance and Linux builds remain unverified for this release.
- The learning-boundary candidate applied eight exact approved patches through EngCoder self-iteration and passed 63 Release CTest cases and 16 Web smoke tests. A fresh loopback server using the candidate binary rejected completed candidate feedback, missing provenance and a mismatched ordinary reply. It accepted a matching ordinary reply as the sole verified evidence record. The patch provider was scripted.

## 0.7.1 Windows release verification (2026-10-09)

- **Passed:** Visual Studio 2022 x64 Release build and all 61 CTest cases.
- **Passed:** Visual Studio 2022 x64 Debug build and all 61 CTest cases.
- **Passed:** all 16 Web smoke tests and syntax checks for all 10 Web JavaScript files.
- **Passed:** isolated Release `EngCoderServer.exe` startup on loopback port 14329, `/health` reporting EngCoder 0.7.1, `/ui/` returning HTTP 200, and `/api/engcoder/qwen-output` returning the retained-output snapshot contract.
- **Fixed during release audit:** the new-profile Web GPU input still declared `value="95"` while every other 0.7.1 default and its label used 50%. It now starts at 50%, and the release-default smoke test protects both the label and input value. Saved profiles remain authoritative and are not rewritten.
- **Not claimed:** live GGUF generation, physical GPU-layer allocation, live LM Studio 9B/27B routing, remote-LAN operation, or interactive Edge responsiveness during a 79 GB cold model load. Those require available models/hardware and hands-on acceptance; the source retains those limitations below.

## 0.7.1 Async UI 1 checks (2026-10-09)

- `release_071_async_sidebar_live_smoke.cjs`: tests immediate drawer visibility while a model load is pending, non-modal Lab access, deferred/coalesced panel painting, and bounded event-loop yielding with terminal persistence.
- All Web smoke tests and JavaScript syntax checks must pass. Existing idle retention, MathML, retry and role-routing tests must remain green.
- **Windows acceptance pending:** start native model loading (cold, large GGUF), immediately toggle Output / Qwen Live / Run info, send/stream text with available loaded model or `/v1` client, confirm partial output appears and the sidebar remains clickable; repeat in served Edge client. Verify Stop and chat refresh while generating.

## 0.7.1 Qwen Live 3 idle-retention hotfix (2026-10-09)

- The right-panel Qwen Live view retains its last generated output after completion and through empty/failed request starts. A real provider fragment from a new request replaces the retained output. The status shows `Qwen preparing · last output retained` until replacement begins, then `Idle · last output retained` on completion.
- `projects/web/tests/qwen_live_idle_persistence_smoke.cjs` checks start/finish/next-delta and snapshot-recovery transitions; existing Web tests must remain green.
- Native `test_qwen_output_journal.cpp` now covers the same transitions. C++23 isolated journal checks can be run under GCC/Clang; the full Catch2/MSVC suite and a live MCP-hosted inference test remain outstanding.

## EngCoder 0.7.1 source-package verification (2026-10-09)

- **Passed in the supplied source package:** all 16 Web smoke tests (`node projects/web/tests/*smoke.cjs`, including the new release/default/scale contract check). The 25-mission Web regression passes.
- **Reviewed:** C++ model default (50%), native GUI default/reset (50%), Web form/default/reset (50%), 0–100% scale and 60%-layer ceiling unchanged. The 48-layer hint maps the new 50% default to `-ngl 14`; prior custom 99% still maps to 29.
- **Native regression added:** `0.7.1 keeps saved Qwen GPU tuning when the default changes` verifies a revision-9 saved 99% model survives two manager startups and its persisted manifest does not change. Also updated old migration tests for the new default.
- **Fixed regression drift:** An older Web smoke test hardcoded `0.6.9`; it now checks semantic version format, leaving version specificity to the 0.7.1 test.
- **Fixed:** GPU layer display fallback to 50% for malformed/NaN profile data; legitimate values and mapping remain unchanged.
- **Fixed:** Release version metadata previously disagreed (`0.6.8` vs `0.6.9`). Native CMake, vcpkg, browser and training worker now declare `0.7.1`.
- **Superseded by the Windows release verification above:** the package author did not have the full native toolchain; this repository pass subsequently completed both MSVC configurations and CTest. Actual Qwen GPU-layer allocation, remote clients, and live inference remain unverified.

## Qwen live provider output hotfix (2026-10-08)

- Web regression: `node projects/web/tests/qwen_live_provider_smoke.cjs` exercises event capture, separate Qwen live tab, source-route wiring and exclusion of unrelated output. Existing Web regression suite must remain passing.
- Native regression: `test_qwen_output_journal.cpp` checks streamed content, cursor semantics, bounded event retention and chunk splitting. Standalone C++23 concurrency test is run in the artifact environment.
- Missing here: linked Windows/MSVC CTest, actual Qwen Next Flash inference through `/v1`, and remote authenticated served-browser end-to-end checks. No claim of raw tokenizer IDs or observation of requests bypassing EngCoder.

## 0.6.9 Catch2 routing/handoff hotfix (2026-10-08)

- User's Windows CTest report: 52/54 passed. Failures were `agent runtime persists streamed text and terminal completion` and `handoffs reject unsafe fields and extra-long content`.
- Root causes found in source: the earlier fast-path gate could omit the `session-core-only` audit event, and the handoff validator used case-sensitive matching for the capitalized `Ignore previous` regression case.
- The code now emits a no-model `session-core-only` decision even when the normal controller path is deliberately skipped, and rejects unsafe handoff content without regard to ASCII letter case.
- Added coverage for mixed-case injections, per-item and total-length limits, and acceptable handoff notes.
- Offline verification: all 12 Web regression scripts, JS syntax and mission source synchronization. Native validator predicate compiles in strict C++23 mode in an isolated harness.
- **Not verified here:** full MSVC/CTest suite. Re-run `auto.bat` on Windows and confirm 54/54 before considering this hotfix certified.

## 0.6.9 advisory handoff gate hotfix (2026-10-08)

- **PASS:** GCC 14.2 and Clang C++23 standalone classifier checks: eight scenarios each (basic C++23 display, complex Vulkan refactor, architecture review, arithmetic/math bypass, greetings and simple definitions).
- **PASS:** all 12 Web JS smoke tests (including Run timing, raw output, right-panel Retry, math assets, served mode, and 25-mission sync).
- **ADDED, NOT EXECUTED HERE:** Catch2 `test_handoff_contract.cpp` validates JSON contract, reasoning-only/empty content, code leakage, tool calls, truncation and malformed input; `test_agent_runtime.cpp` verifies direct basic-code routing, one-time preflight, three-tier handoff and original-prompt forwarding.
- **BLOCKED:** native CMake configuration requires nlohmann_json development package, not installed in this container. Therefore no native Windows build, MSVC Catch2 suite, or live 9B/27B/Qwen validation is claimed. Run `auto.bat` and tests on target Windows system.
- **UNCHANGED:** Qwen GPU offload/profile, model host lifecycle, session storage, theme, raw-output-only pane, Run timing UI and canonical 25-mission backlog.

## 0.6.9 math HTTP-route hotfix

**Root cause:** the math renderer existed and the HTML referenced it, but the C++ native HTTP server omitted `math.js` from its explicit `/ui/js/*` allowlist. The request returned 404, and optional invocation silently left raw LaTeX in chat. Browser caching could also keep an old script after updating.

- `node projects/web/tests/served_static_assets_smoke.cjs`: all HTML script references must have native server routes; explicitly checks `math.js` and serving `Cache-Control: no-store`.
- `node projects/web/tests/release_070_math_smoke.cjs`: inline/display parser, fractions, boxed answer, literal fenced code, and DOM safety.
- Run the existing Web smoke suite. Offline checks do not certify live Windows/Edge behavior or the complete native MSVC build.
- **Windows acceptance:** start updated `auto.bat`, inspect `http://127.0.0.1:14321/ui/js/math.js` (HTTP 200), reload chat, then verify `\(1\)`, `\[5X+3=8\equiv1\pmod{7}\]`, and `\[\boxed{1}\]` render as math in the conversation. Copy and raw Output must still show original text.

## 0.6.9 subagent maintenance — source-only verification

- Updated native three-tier routing with fallback for **enabled but unassigned** specialist addons; added C++ integration case verifying small checklist, middle review, then large final answer.
- Existing JS regression checks and source/mission sync must pass before packaging; Windows/MSVC full build and real LM Studio 9B/27B + native Qwen run are **not** certified by Linux tests.
- Configured server URL is environment-driven and not the Web Session Settings endpoint.

## 0.6.9 screenshot routing hotfix — validation targets

- C++23 classifier regression: modular arithmetic, percentage arithmetic, mph/speed, exact fractions, and simple text.
- Web Run info: present Architect fallback reason, while Output remains generated text only.
- Full Windows 49+ CTest suite, LM Studio controller compatibility and live model availability **require Windows verification**.

## 0.6.9 hotfix — Architect numerical routing

- Reproduced the target CTest mismatch using the exact existing intent expectations: `What is 12% of 180?` was incorrectly classified as `simple` rather than `complex`.
- Updated the classifier and expanded the first native regression case for `%`, `-`, written math, round-trip speed, and nonmathematical simple definitions.
- **Offline passed:** the two unmodified native test case bodies in `test_task_intent.cpp`, using a minimal Catch2-compatible standalone macro runner under GCC and Clang with C++23 and `-Wall -Wextra -Wpedantic -Werror` (the real Catch2 package is unavailable here).
- **Offline passed:** eight Web UI smoke scripts and `scripts/sync_missions.py --check` with 24 missions preserved.
- **Pending Windows acceptance:** full project MSVC build and all 49 CTest cases using `auto.bat`. Never infer those outcomes from the offline runners.

## 0.6.9 model-role and served-mode regression

- `node projects/web/tests/release_069_model_roles_smoke.cjs` checks actual provider-reported final model vs. small/middle consultations, source protection from native subagents, and retry of both external foundations and small responders after settings change.
- All Web UI smoke tests must pass; right-panel Output and Run info remain separate; themes and saved session data remain served by the same native runtime.
- Windows acceptance: run `auto.bat`, all CTest cases, 3 loaded models with distinct assignments, native/served UI theme parity, regenerate an LM Studio response, and check per-response Run info after refresh. Resource preflight and OS process confinement are separate outstanding missions.

## 0.6.8 maintenance — platform warning and responsive Chats

- Regression tests: `node projects/web/tests/responsive_chats_smoke.cjs` covers manual open at 1100px and narrow widths, unrelated layout calls, outside-click persistence, shrink-to-close, and aria state.
- Windows/MSVC acceptance: `auto.bat` must build `engcoder_runtime` without C4996; Linux source branch uses `std::getenv`. Full CTest and Edge window-resize interaction require Windows verification.

# Validation — EngCoder 0.6.8

This release keeps the existing EngCoder/BotFace interfaces and agent/training architecture while repairing the native Qwen3.8 Flash execution boundary.

## Runtime findings encoded in this release

- The known-success baseline is the pinned llama-server loading `unsloth/Qwen3.8-Flash-Next-GGUF:UD-Q2_K_XL` through `-hf`; Architect/System 1 is not involved at the point where the failing process crashes.
- Legacy expert-cache binaries are therefore treated as compatibility baselines, not as proof that the Q2 cache path is valid.
- A Qwen-specific compatibility implementation documents Q2 layouts whose routed `ffn_down_exps` physical width is padded to the quantization block boundary and whose cache path requires matching activation padding. The exact published case is not proof that Unsloth UD-Q2_K_XL has the same file layout, so EngCoder treats this patch as a known prerequisite/candidate compatibility fix rather than the sole proven crash cause.
- `expert_cache_mode=auto` requires the patched runtime before a cache probe is even attempted, then persists the outcome per model/runtime recipe. Old runtimes automatically use the no-cache baseline; patched runtimes become trusted only after reaching `/health` with the cache enabled.
- Runtime recovery isolates optional acceleration before sacrificing context: cache-off at the same context, then a same-context minimal llama.cpp launch, then bounded context fallbacks.

## Offline release checks

- C++23 syntax compilation with `-Wall -Wextra -Wpedantic -Werror` for modified config/runtime-model/Qwen/native-model/process sources using the repository's local dependency stubs where external headers are unavailable.
- SDL/ImGui fallback syntax compilation with local UI stubs.
- `node --check` for every Web JavaScript file and a DOM-reference audit for shared Web/Desktop controls.
- `bash -n` for shell scripts, Python bytecode compilation, and JSON parsing.
- Runtime patch audit: both Qwen3.8 Q2 compatibility hunks are present; Windows/Linux installers run `git apply --check` before applying the patch to the pinned commit and record the runtime feature manifest.
- Package hygiene: no model weights, generated binaries, build trees, downloaded dependencies, vcpkg installed tree, or bytecode caches are included.

## Target-machine acceptance

The artifact environment cannot execute the Windows RTX 5080/CUDA runtime or perform the complete networked vcpkg/backend build. `auto.bat` remains the full EngCoder build/CTest acceptance step. For Q2 expert-cache acceleration, use **EngCoder Lab -> Model Library -> Upgrade/Install Qwen runtime** once; the model weights already in the Hugging Face cache are reused. Until that patched runtime is installed, Recommended/Automatic intentionally run the stable no-cache Qwen baseline rather than enabling the incompatible cache path.

## 0.6.6 routing/cleanup checks

- Native C++ routing/model/scheduler/server sources: strict C++23 syntax with `-Wall -Wextra -Wpedantic -Werror` using offline dependency stubs.
- New regression test sources: syntax checked with Catch2 stubs.
- Web JavaScript: `node --check` for all application scripts.
- Runtime Default semantics: loaded native model resolves to `qwenflash://...`; provider-assigned model becomes AgentRuntime's inference model.
- Managed-model availability: missing `engcoder://` controller/foundation/addon components no longer become phantom external models.
- Scheduler shutdown: active stop sources are cancelled before jthread teardown.
- Web polling: adaptive idle backoff caps at 400 ms; active-event polling returns to 100 ms.
- Model probing: one architecture snapshot supplies the registry; no duplicate `/api/engcoder/models` scan is issued by the shared BotFace frontend.
- Browser-state migration: stale unavailable managed-model selections are removed instead of recreated as custom options.
- Persistence/polling: ordinary settings changes do not serialize chat history, hidden Lab pages do not poll, and visible Lab polling targets only the active telemetry tab.
- Cross-platform entrypoints: `auto`, `build`, `check`, `run`, and `clean` wrappers are present for both Windows (`.bat`) and Unix (`.sh`); shell syntax and Windows CRLF wrapper formatting are validated.

## 0.6.5 approval / desktop lifecycle checks

- Approval continuation: HTTP tasks with `wait_for_approval` keep the exact reviewed tool call pending; approval is bound to task + call ID, and denial returns a tool-denied observation instead of terminating the run.
- Tool preference: repository/base-directory inspection is directed to `workspace.tree/read/search`; `where`/`which` executable discovery is classified read-only.
- Desktop lifecycle: the shared Windows Desktop/Web page registers an ephemeral per-launch token, reports page close, and the native owner stops the embedded server after a reload-safe grace period.
- Edge ownership: the app-mode Edge process tree is placed in a kill-on-close Job Object where supported, with a direct process fallback, so closing EngCoder does not leave its dedicated browser/runtime process behind.
- HTTP endpoint default: EngCoder now uses loopback port `14321`; the old 8787 probing range is removed from product defaults/build launchers.
- Static validation in the artifact environment: `node --check` passes for every Web JavaScript file, Unix wrappers pass `bash -n`, requested source invariants were audited, and no remaining Catch2 `CHECK`/`REQUIRE` logical-chain assertions were found. Windows/MSVC remains the authoritative final build/CTest target.

## Runtime backend UI/provisioning validation
- `node --check projects/web/js/state.js`
- `node --check projects/web/js/ui.js`
- `bash -n runtime/qwen-flash/install-runtime.sh`
- `git apply --numstat runtime/qwen-flash/patches/0001-qwen38-q2-padded-down.patch` parses successfully.
- Windows runtime progress is emitted to `<state-root>/runtime-install-qwen-flash.json` and surfaced through `/api/engcoder/model-library`.

### Composer plus-menu / quick-bar validation
- `node --check projects/web/js/*.js` passes after the composer integration.
- `projects/web/index.html` parses successfully with the standard HTML parser.
- The original send/stop/microphone controls and composer grid remain in place; additions are menu/quick-bar surfaces around the existing row.
- Folder import filters unsupported/binary files and limits context to 96 files, 512 KiB per file, 1.5 MiB total so chat persistence stays below typical localStorage quotas.
- The quick routing presets only manipulate existing `forcedAddon`, `autoRouting`, and `enableAddons` controls; they do not grant filesystem authority or bypass the native workspace sandbox.

## Settings/runtime/learning safety checks

- Web JavaScript parses with `node --check` after the runtime settings, lifecycle-menu, per-field defaults, learning remove/ban, and denylist UI changes.
- Model profile defaults are 36% GPU layer offload in config, runtime-model decoding, Web UI, native GUI, and seeded built-in profile. Untouched legacy 70%/90% recommended profiles migrate to 36% while Manual/custom profiles are preserved.
- `Reload / Apply settings` saves the visible runtime profile before unload/load; ordinary setting edits never reload a live model automatically.
- Model cards distinguish saved GPU target from the loaded effective profile and `-ngl` reported by the active Qwen runtime.
- `Clear temp cache` only removes transient non-GGUF files under EngCoder's configured model-cache tree. Downloaded model weights have a separate confirmed delete route, and Local GGUF/external paths are never deleted by EngCoder.
- Learning removal rewrites `verified-evidence.jsonl`; hard bans persist in `learning/denylist.json`, remove matching existing evidence, block prompt- or response-equivalent future ingestion through `LearningService::record`, and can be explicitly unbanned.
- The artifact environment cannot execute the authoritative Windows/MSVC build; rerun `auto.bat` on the target machine for final GUI/test acceptance.

## 0.6.6 source implementation and verification (2026-10-07)

- **Passed:** `node --check` across every `projects/web/js/*.js` script.
- **Passed:** an isolated Node VM regression for native raw text and reasoning delta capture, 96 KiB tail bound, event cursor sequence, required UI model selectors, and conversation-persistence hook presence.
- **Passed:** independent C++23 GCC `-Wall -Wextra -Wpedantic -Werror` compile of `src/controller/task_intent.cpp` and runnable smoke test of conservative simple/coding/complex classification plus training/persistent replay of 20 numeric controller examples.
- **Passed:** Python source compilation and Bash `-n` for installer/shell scripts.
- **Not run:** full CMake build and Catch2 suite. The artifact container does not provide the project's `nlohmann_json` CMake package or complete declared native dependencies; `cmake` stops at `find_package(nlohmann_json)`.
- **Not run:** Windows MSVC/Visual Studio, Qwen GGUF first load/reload, LM Studio companion model, live Web/Edge UI, model draft compatibility, real tokenizer identifiers, OS-isolated sandbox behavior. All require a target-machine run and logs.
- **Compatibility note:** older sections above are retained as historical release-validation records; 0.6.6 continues the revised 95%-default GPU slider where new 100% corresponds to old 60%, not the obsolete old 36% default.

Track remaining work, especially security and MTP compatibility certification, in `docs/FUTURE_MISSIONS.md`.

## 0.6.6 maintenance: warning, profile persistence and composer missions

- `repair_legacy_self_draft` result is now explicitly consumed; the previous `[[nodiscard]]` discard at `native_model_manager.cpp` no longer exists.
- The built-in model profile no longer triggers an unconditional disk write on every EngCoder startup.
- Native write uses a checked unique temporary file plus replace, and a corrupt built-in profile is preserved instead of being replaced blindly. Added Catch2 regression tests for no-op manifest timestamp and corrupt input preservation. Native tests are **pending** MSVC/CMake dependencies.
- `projects/web/js/mission_data.js` is generated from the one canonical markdown register. Run `python scripts/sync_missions.py --check` to detect drift. The bottom Missions control and filtered modal are included in both Web and shared Desktop UI via native static file routes.

### 0.6.6 maintenance artifact acceptance (C4834 + composer Missions)

- **Passed:** `python scripts/sync_missions.py --check` — 24 open P0–P2 objectives match the generated Web data.
- **Passed:** `node projects/web/tests/mission_smoke.cjs` — footer button, mission count, dialog open/close, priority filter, search and safe text nodes.
- **Passed:** `node --check` for every `projects/web/js/*.js` source.
- **Passed:** C++23 `-Wall -Wextra -Wpedantic -Werror` standalone POSIX `core::replace_file` test — overwrite succeeds; a failed replacement leaves the existing manifest intact.
- **Passed:** previous standalone C++23 shadow-router smoke (simple/complex/coding, numeric learning and persistence) and `bash -n` across shell files.
- **Source verified:** built-in profile repair result is consumed (`C4834` origin removed); unchanged profiles no longer rewrite manifest on startup; native save checks errors and replaces via platform-specific helper.
- **Not run here:** authoritative Windows MSVC solution build, dependency-linked full CTest, Edge layout/4K visual QA, actual model runtime and GPU reload. Verify on the target machine before deployment.

## 0.6.6 Windows CTest hotfix — MTP validation

- Corrected an unreachable validation branch: a self-referential legacy speculative repository was rejected with the generic missing-GGUF error before the specific same-repository rejection could execute.
- Retained the stricter runtime policy: no remote draft repositories, missing local draft GGUF, unsupported draft type, or non-GGUF file can pass validation while MTP is enabled.
- Expanded the existing Catch2 unsafe-scheduling test with positive and negative MTP cases so validation precedence is covered.
- Passed: 8 isolated strict C++23 (`-Wall -Wextra -Wpedantic -Werror`) runtime-MTP branch checks, compiled directly from the exact production validation block. These exercise validation ordering, but do not replace the full Catch2 suite.
- Passed: JavaScript (`node --check`), Bash (`bash -n`), and Python syntax checks for the packaged source.
- Target-machine acceptance: rebuild and rerun `auto.bat`; all 45 CTest entries in the user-reported maintenance build must pass before launch. The Linux artifact environment cannot certify the Windows/MSVC CTest suite.

## 0.6.7 source checks

- `node --check` for every application/test `.js/.cjs` file; `node projects/web/tests/mission_smoke.cjs` and `release_067_ui_smoke.cjs`.
- Standalone C++23 `task_intent.cpp` regression smoke for greetings/complex/coding and shadow journaling.
- New Catch2 native `automatic small responder handles greetings without addon routing or tool authority` case is registered; full dependency-linked MSVC/CTest must be run on Windows.
- `scripts/sync_missions.py --check` verifies all 24 unfinished missions are still exposed in the footer.
- No actual Qwen/LM Studio live inference, Windows/CUDA runtime or MTP GGUF compatibility certification available in this build environment.

### Executed in this source release environment

- Passed `node --check` on 11 Web scripts and tests.
- Passed `mission_smoke.cjs` and `release_067_ui_smoke.cjs`.
- Passed mission-source parity check (24 still-open missions).
- Passed Python AST (5 files), JSON parser (5 files), and `bash -n` checks.
- Passed a standalone g++ C++23 intent and shadow-journal harness with `-Wall -Wextra -Wpedantic -Werror`.
- Native linked Catch2/CTest suite and Windows live Qwen/LM Studio runtime are **not run** in this environment; these are explicit acceptance blockers, not reported successes.

## 0.6.7 generated-only panel acceptance

- `node projects/web/tests/generated_output_only_smoke.cjs` validates streaming fragment capture, exclusion of controller/profile events, legacy payload migration, cursor progression, and in-place rendering contract.
- Live browser/Qwen validation still required on Windows.

## 0.6.8 offline and hardware qualification

- Tested offline: Web regression smoke for generated-output-only panel, Missions viewer (24 outstanding), attachment visibility and independent response routing; syntax validation; isolated C++23 workspace checkpoint capture/restore and traversal-denial scenario; Python trainer denylist ingestion smoke.
- Test source added but not executed under Catch2 in this environment: hierarchy specialist → reviewer → Qwen, review-off/failure cases, small responder failed-before-stream vs failed-after-stream, wrong-target checkpoint denial.
- Required target acceptance: MSVC Release/Debug build + full CTest green, native Qwen GPU reload, LM Studio specialist/reviewer model selection, cancellation, actual remote bearer authentication, training-ban race evaluation, and runtime process isolation. No assertion of all-missions-complete.

## Math renderer and native-model guide maintenance (2026-10-08)

**Verified in current offline environment:** `node --check` of the new MathML parser and updated renderer; MathML subset smoke tests for inline/display math, fraction, boxed result, code isolation and safe text; all prior Web UI smoke tests; mission UI synchronized to 25 open missions; source archive integrity and patch applicability (see release log).

**Not verified:** Browser image inspection/visual layout in the local headless Chromium environment (startup timed out); Windows Edge high-DPI rendering; full MSVC build/CTest; other GGUF backend compatibility; any three-concurrent-native-model scenario. The latter remains a documented engineering mission. Existing Qwen native runtime source and GPU configuration were intentionally not changed by this maintenance.

## 0.6.9 timing quick reference (2026-10-08)

- `node projects/web/tests/release_069_run_quick_metrics_smoke.cjs`: verifies Run quick-reference position, completion/live timing, TPS estimate labeling, and persisted original clock anchors.
- Model TPS is an estimate until the backend exposes tokenizer-based inference stats; llama.cpp CLI benchmarks cannot be equated to end-to-end agent task speed.
- A live Windows/Edge test with Qwen and LM Studio remains required.

- Qwen Live 2 idle persistence: `node projects/web/tests/qwen_live_idle_persistence_smoke.cjs`; native journal test cases also check `QwenOutputSnapshot.latest` after finish and ring pruning.
