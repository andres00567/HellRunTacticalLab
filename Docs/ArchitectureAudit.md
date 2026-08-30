# Tactical AI / GOAP Simulation Debugger Architecture Audit

This audit follows `tactical_ai_goap_simulation_debugger_handoff.md`. The lab is an editor debugger for real gameplay AI; it is not a second planner, perception system, or navigation runtime.

## Current ownership map

| Concern | Authoritative runtime | Current lab bridge | Provenance status |
|---|---|---|---|
| GOAP planning | `HellRunGOAP`: `FGOAPPlanner`, `UGOAPBrainComponent`, `UGOAPWorldStateSubsystem` | `FTacticalLabIntegrations::BuildPlan` host callback | Real planner, but the bridge currently truncates goal scores, fact values, plan failure, and replan reason |
| Tactical candidate scoring | `FHellRunTacticalEvaluator` | Direct, shared by editor simulation and batch simulation | Real lab policy evaluation with per-component scores and rejection strings |
| Movement pathing | Custom voxel navigation (`AHellRunVoxelNavVolume`) | Live EQS and Find Cover use `BuildQueryPath`; ordinary simulated candidates still use a coarse baked 2D fallback | Split authority; must converge on a provider interface |
| Perception | Gameplay perception/controller configuration | Canvas previously drew one global cone clipped by baked 2D obstacle edges | Not authoritative. This milestone separates configured FOV and resolved 2D visibility and exposes per-profile settings; gameplay trace bridging remains required |
| Cover | Gameplay EQS plus custom voxel generator/test | Native tactical EQS query and `TacticalLabEQSTest_VoxelPath`; simulation scores baked candidates | Query path is real voxel navigation; scoring/runtime selection still needs unified provenance |
| Squads | Gameplay squad coordination and movement grants | Fixture fields (`SquadId`, `SquadRole`, `bMovementGranted`) | Fixture approximation, explicitly labeled in decision provenance |
| Director | `UHellRunAIDirectorSubsystem::GetDirectorSnapshot()` | Typed `FTacticalLabIntegrations::CaptureDirectorDebug` host adapter copied into each recorded PIE frame | Authoritative bounded aggregate snapshot; controller-only draw primitives are not yet normalized |
| Timeline | `FHellRunTacticalLabLifetime` events/decisions/candidates | Toolkit timeline and goal graph | Real simulation history, but selection/scrubbing is not synchronized yet |

## Root cause: inconsistent FOV

The previous canvas used `UTacticalLabScenarioAsset::VisionConeRange` and `VisionConeHalfAngleDegrees` for every entity. Sixteen rays were intersected against `Scenario.Obstacles`. Therefore apparently different cones did not represent different AI perception settings: the same theoretical cone was clipped differently by nearby baked 2D lines.

That rendering omitted gameplay perception config, trace channel, trace height, ignored actors/components, and blocker identity. The current milestone adds per-profile range/angle/ray count and separate **Configured** and **Resolved** overlays. A selected agent shows resolved ray hit points. This makes the remaining approximation visible instead of presenting it as gameplay truth.

## Root cause: cover/path rejection

`GenerateCandidateRoutes` used a bounded 180 cm 2D grid around baked obstacle segments. When it failed, it silently replaced the failure with a direct start-to-destination line. `EvaluateRoute` then rejected that line for intersecting a baked movement blocker. The UI rendered accepted routes but not the rejected route or blocker, so the report looked arbitrary.

The unsafe fallback is no longer treated as navigable. It is retained only as an attempted diagnostic segment with an explicit generation failure. Rejected routes render red and known blocking obstacle IDs render bright red. Candidate records carry `FailedSegmentIndex` and `BlockingObstacleId` for inspector/timeline synchronization.

## Milestone-one execution semantics

- **Play / Pause** advances the actual `FHellRunTacticalLab` instance on an active timer.
- **Movement Enabled** advances accepted routes without terminating the interactive session after one maneuver.
- **Analysis Frozen** advances time and decision evaluation while preserving agent transforms.
- **Step** retains deterministic decision/movement stepping.
- **Run** and batch runs retain terminal deterministic lifetime semantics.

## Remaining required work

1. Goal scores and plan failure now cross `FTacticalLabPlanResult`; next carry complete typed input facts, fact provenance, action state, and replan reason from `UGOAPBrainComponent` when a live gameplay brain is available.
2. Add a navigation-provider callback so normal simulation candidate generation uses the same custom voxel path authority as live EQS.
3. Bridge configured and resolved perception to actual gameplay perception/trace configuration, including channel, height, ignored set, and named blocker.
4. Synchronize selected agent, candidate, decision, timeline time, and canvas highlights.
5. Add squad and director adapters only after the single-agent decision loop is trustworthy.

## Handoff milestone audit (2026-08-15)

### Implemented and backed by current data

- Selecting an agent now synchronizes the tactical canvas, inspector, GOAP graph,
  and timeline focus. Timeline rows can also select their associated agent.
- The inspector presents simulated transform/velocity, global target truth,
  deterministic planner inputs, configured intent, competing goal scores and
  reasons, the returned gameplay GOAP plan, tactical candidate scores, rejection
  reasons, failed route segment, and baked blocker identity.
- Play/Pause, Stop, Step, Reset, movement-enabled, analysis-frozen, speed, batch,
  export, Run EQS, and Find Cover are wired commands. Stop preserves current state;
  Reset restores the authored snapshot.
- The live timeline is vertically scrollable, selected-agent focused, bounded,
  and filters playback to meaningful transitions. Candidate details remain in the
  lifetime candidate table instead of producing overlapping per-candidate rows.
- Configured FOV renders its complete range arc independently from resolved LOS.
  Resolved visibility renders independent clipped rays; it no longer joins
  unrelated ray endpoints into false triangles.
- Native tactical EQS snaps the querier to a current custom voxel-navigation node,
  uses the typed-edge reachability test, reports item/reachability counts, and
  labels cover candidates and returned voxel path points.
- Find Cover no longer falls through to generic generated retreat points. Its
  destination score rewards progress toward the configured threat, while the
  EQS trace still validates cover at crouched capsule half-height.

### Explicitly incomplete; no placeholder data is presented as authoritative

- `UGOAPBrainComponent` does not yet emit per-action runtime status, action
  precondition transitions, or replan reasons through `FTacticalLabPlanResult`.
  The inspector says this directly instead of fabricating an executing action.
- Agent belief is currently the deterministic set of facts submitted to the GOAP
  planning bridge, not a live gameplay blackboard snapshot.
- Resolved visibility remains the baked 2D LOS fixture. Gameplay trace channel,
  ignored components, trace height, and hit component identity still require a
  perception adapter.
- Director enable/disable, manual squad spawning, planner-search node expansion,
  and timeline state snapshots are later handoff phases and are not exposed as
  working controls.

## PIE authority milestone (2026-08-15)

- **Attach PIE** records bounded live snapshots of controlled pawns at 5 Hz. Each
  snapshot contains runtime transform/velocity/facing, the active navigation path,
  configured sight, resolved world-trace blocker identity, and the complete
  `UGOAPBrainComponent::GetDebugSnapshot()` payload when a live brain exists.
- The canvas can rewind/advance recorded frames or return to Live. Player-group
  follow is independently toggleable so following never removes manual pan/zoom.
- Selecting a PIE pawn drives a live inspector with active goal/action/status,
  remaining plan, replan reason, goal utility, typed facts/provenance, navigation
  path point count, and named perception blockers.
- Live routes render independently of perception configuration. The recorder copies
  the active `UPathFollowingComponent` path, so bots without a sight sense no longer
  lose an otherwise valid recorded route at the presentation layer.
- Ranged enemies additionally publish their last accepted tactical route through
  the host-neutral agent debug adapter. This preserves hybrid/voxel route points,
  provider, admission reason, and exposure after PathFollowing releases its path.
- Each frame also carries a host-neutral projection of the authoritative Director
  snapshot: pacing phase/intensity, cadence, population, enemy states, targeting,
  attack gates, recycling pressure, and planner-batch metrics. Historical frames
  therefore display historical Director aggregates rather than querying live state.
- The baked 2D obstacle fixture no longer produces a resolved ray fan. Outside
  PIE it renders configured FOV only; in PIE resolved rays are `ECC_Visibility`
  world traces from the pawn eye transform.
- **Find Cover Now** in attached PIE uses the selected pawn's exact runtime XYZ,
  the current PIE voxel volume, the nearest player pawn as threat context, and
  the character movement component's crouched capsule half-height. A dedicated
  cover-occlusion test ignores every pawn capsule, preventing the target or a
  bystander from being falsely classified as cover.
- Cover generation remains two-stage: wall-adjacent ground nodes must face away
  from the threat by context dot product, then real world geometry must occlude
  the threat and the typed voxel path must be reachable.

### Still required for a complete gameplay debugger

- Capture authoritative perception stimuli/forget events in addition to the
  diagnostic sight ray fan. A geometric fan is useful for collision diagnosis,
  but it is not a replacement for AI Perception's observed-target history.
- Add a scrubber tied to recorded PIE time and route/GOAP transitions; the current
  controls step snapshots one frame at a time.
- Add explicit squad and Director transition-event streams. Director aggregate
  snapshots are bridged, but its draw-only per-agent steering/EQS primitives are
  not yet a durable event contract. The player-group centroid remains only a
  camera-follow target, not fabricated squad reasoning.

## Authoritative GOAP runtime event contract (2026-08-30)

### Implemented in production GOAP

- `HellRunGOAP` now defines a typed `FGOAPRuntimeEvent` contract and a native,
  synchronous per-brain event delegate. Events carry a process-monotonic sequence,
  world time, component-lifetime agent GUID, readable identity, domain, typed
  goal/action/plan/fact fields, reasons, provenance, expiry, and state revisions.
- Brain transitions emit logic, replan-request, plan-queue, plan success/failure,
  goal-selection, action lifecycle, and agent-fact lifecycle events at their
  authoritative transition points. Immediate tasks emit one start and one terminal
  event; timeout is distinct from failure and abort.
- Shared world/squad fact set, clear, and expiry transitions flow through the
  world-state subsystem to each affected brain with typed old/new records. Weak
  brain registration and the native event source do not retain destroyed actors.
- Failed planning results remain in the debug snapshot and are broadcast through
  the legacy plan delegate as well as the runtime stream. The original replan
  request reason is no longer replaced by a later planner failure, and aggregate
  failed-search metrics are preserved.

### Live recorder and preview integration

- The editor module now owns a persistent PIE session recorder. It subscribes to
  the process-wide native GOAP stream before a PIE world exists, samples spatial
  frames at 5 Hz, discovers later-spawned brains, bounds frame/event history, and
  retains the completed session after PIE ends. Opening or closing a Lab window
  no longer controls instrumentation.
- Spatial frames sample at a bounded 10 Hz. Surface updates may check at 20 Hz,
  while the expensive inspector remains capped at 5 Hz and the event timeline is
  reconstructed only when the event count changes. This separates visual cadence
  from event-list cost without polling gameplay at render frequency.
- Enabling Attach PIE invokes the existing editor-world bake/minimap pipeline once
  before consuming the live session. Baking remains a toolkit operation rather
  than work performed by the persistent recorder tick.
- `FTacticalLabEditorToolkit` is now a read-only session consumer. The tactical
  surface defaults to the live recording, agent selection uses the session GUID,
  the inspector reads the displayed historical frame, and the GOAP timeline reads
  authoritative transition events rather than comparing snapshots.
- Selecting a recorded event selects its agent and displays the nearest spatial
  frame at or before the event time. Manual frame stepping remains stable when old
  bounded frames are evicted, and Return to Live resumes newest-frame following.

### Explicitly incomplete and unverified

- Resolved diagnostic sight rays are no longer generated by the recorder; the
  preview shows configured sight while authoritative AI Perception stimuli remain
  a future source adapter.
- The existing goal graph consumes offline lifetime data and is not yet backed by
  a historical live-session graph model.
- Director aggregate state is recorded, but per-agent steering vectors, EQS
  candidates, squad links, and maneuver debug geometry still exist only in the
  gameplay debug-draw path and are not presented as authoritative Lab data.
- These source changes have not been compiled, run through automation, or observed
  in PIE in this workspace as part of this milestone.
