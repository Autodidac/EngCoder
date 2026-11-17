# Training reusable conversation skills

Research and experiment direction, 2026-11-17. This is a training design, not a claim that the current internal model has passed it.

The Foundation should learn operations over context: update a plan, retain an unchanged fact, resolve a reference, distinguish a possibility from a commitment, and ask for missing information. Days, places and object names are interchangeable values within those operations. They are not interchangeable when meaning depends on them. A proposed Thursday must not replace a confirmed Tuesday without an actual correction.

## Small, falsifiable experiments

Use original fictional examples, with explicit authored-synthetic provenance. Balance correction, negation, hypothetical changes, exceptions and two-step updates. Reserve whole value combinations and paraphrase templates for evaluation before training. Include pairs that differ by one meaningful clause and require different answers. Training every possible day pair is not the objective.

Examples of distinct rules:

- “Tuesday was the plan. Change it to Thursday.” → Thursday.
- “Thursday is possible, but keep Tuesday.” → Tuesday.
- “Move the meeting to Thursday; the delivery remains Tuesday.” → Update only the meeting.
- “Change Tuesday to Thursday, then change Thursday to Friday.” → Friday.
- “Thursday would be better.” → Express the preference without claiming another person accepted it or a calendar was edited.

Measure held-out semantic correctness, contradiction rate, unsupported action claims and conversational readability. Report results by rule and by unseen template/value combination. Exact answer matching alone is insufficient for natural dialogue; a deterministic scorer must reject negated or contradictory mentions rather than accept any occurrence of the right day. Inspect fresh free-form responses as well.

Compare the current checkpoint and candidate at the same parameter count and training-token budget. Keep a smaller and larger data budget for a learning curve. Reject improvements confined to seen templates. Keep active weights unchanged until normal Foundation readiness, privacy, identity and regression gates pass. Do not turn evaluator answers into training examples during the same experiment.

This compact suite is a transfer test and a curriculum component, not sufficient English pretraining by itself. The existing 110-example random-initialization experiment updated weights but failed generation readiness. More repeats of that small batch cannot supply missing language coverage. C++ learning remains in its own addon, not this conversation suite.

## Research informing the design

- [TinyStories (2023)](https://arxiv.org/abs/2305.07759) demonstrates coherent English in small models using constrained synthetic stories. It motivates simple, coherent language coverage; it does not establish that a few dozen chat pairs suffice.
- [Systematic Generalization in Language Models Scales with Information Entropy (ACL 2025)](https://aclanthology.org/2025.findings-acl.90/) studies how the distribution of components affects systematic generalization. Our inference is to balance values and compositions, not multiply near-duplicate greetings.
- [It's All Training (September 2026)](https://arxiv.org/abs/2609.37891) reports improved data efficiency with curated synthetic data, including small models. Those experiments do not predict EngCoder's required token budget, nor authorize importing the entire corpus without reviewing its rights and footprint.
- [Structural priors for data-efficient language learning (September 2026)](https://arxiv.org/abs/2609.11505) finds that lower language-model loss does not consistently imply improved linguistic performance. This supports retaining fresh-response gates rather than promoting on loss alone.
- [An Information-Theoretic Criterion for Efficient Data Synthesis (May 2026)](https://arxiv.org/abs/2605.16379) argues for external verification signals instead of closed self-generated loops. Our implementation direction is independently checked examples and held-out tasks, not treating the generator's confidence as evidence.

No paper establishes a universal “less data means more intelligence” rule. The engineering target is more measured transfer per training token and per byte of model storage.
