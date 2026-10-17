# Shared Editing Manifesto

For the CollabText, Wave, and Markoff development teams.

We are building a foundation for collaborative human editing. It must preserve
the meaning of edits, identities, references, and undo through concurrency,
failure, and recovery. Convergence is necessary. Our standard also includes
what people see, what their actions mean, and what remains recoverable.

## Begin with the editing experience

Define correctness through concrete human actions. Two people edit the same
sentence. One deletes a paragraph while another changes it. Someone undoes a
paste after a collaborator corrects part of it. A reference survives a block
move. An action spanning text and structure is interrupted by a crash.

For each scenario, specify the visible result, reference behavior, undo
behavior, and recovery behavior before choosing the implementation. Record
product decisions explicitly. An algorithm can enforce a policy; it cannot
infer human intent from operations alone.

## Preserve identity and meaning

A block remains the same block when it moves. A reference continues addressing
its intended referent, or reports why that referent is unavailable. Undo has
specified effects on shared history. Recovery reconstructs identities and
relationships along with visible content.

Matching strings alone are insufficient evidence. Preserve the distinctions
between boundaries, attachments, structural items, and editing actions.

## Keep the common library coherent

CollabText owns sequence semantics: identity, ordering, visibility, causal
applicability, scoped positions, shared compensation, and the interpretation
of supported durable operation formats.

Consumers own document and action semantics: document identity, composition of
streams, application-specific editing policies, authorization, durable
transactions, delivery, and presentation.

Make the integration protocol explicit and practical. Every obligation needs
an owner, an observable outcome, and a recovery path. When consumers repeatedly
implement the same mechanism, use their experience to identify a reusable
component. Let demonstrated common behavior guide abstraction.

## Use different consumers to test the design

Wave and Markoff exercise different document models. Their differences reveal
which concepts are fundamental and which belong to an application.

Complete real editing paths in both consumers against the same library
candidate. Include remote delivery, references, undo, durable publication, and
reopen. Library tests, independent models, and installed package probes each
provide evidence; production integration supplies its own required evidence.

## Make failure part of the model

Specify what happens when allocation succeeds and publication fails, when an
append is uncertain, when input is deferred, and when retained history is
unavailable. Preserve the information needed to diagnose and recover.

Durable acceptance must follow a recoverable commit. Identity authority must
survive restart. Uncertainty requires reconciliation before further decisions.
Treat refusal and resource limits as deliberate behavior with a clear path for
the consumer and the user.

## Optimize without weakening the promises

Correctness guides implementation. Responsiveness and bounded resource use
also shape whether editing works in practice.

Measure realistic documents, long histories, reconnects, and undo workloads.
Preserve independent semantic models while improving expensive paths. Make
performance claims at the scope the evidence supports.

## Prefer completed experiences and understandable contracts

Choose work that closes a real editing path or resolves a demonstrated
integration difficulty. Evaluate each new capability by the experience it
enables and the obligations it introduces.

Keep a small authoritative account of the editing model, integration protocol,
and durable format promises. Preserve discovery history separately. A new
developer should be able to identify the governing contract and the evidence
required to change it.

Our vision becomes credible through repeatable editing experiences: people
can collaborate, refer to content, undo their actions, and recover their work
while the system preserves the relationships they depend on.
