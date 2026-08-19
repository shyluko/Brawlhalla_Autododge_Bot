# Offset Status

Offsets are relative to the object named in each row. This document separates
live-confirmed values from candidates and older values so that an observation is
not accidentally promoted to fact.

## Confirmed

| Area | Field or signature | Value |
|---|---|---|
| Capture hook | PhysObject position-read AOB | `F2 0F 10 8B 40 01 00 00` |
| PhysObject | `pos_x` | `+0x140` |
| PhysObject | `pos_y` | `+0x138` |
| PhysObject | `is_attacking` | `+0x050` |
| PhysObject | `attack_id` / PowerID | `+0x06C` |
| PhysObject | linked Entity pointer | `+0x0C0` |
| PhysObject | weapon pointer | `+0x0C8` |
| Entity | facing | `+0x110` |
| Entity | damage | `+0x5D8` |
| Entity | legend pointer | `+0x3E8`, then legend ID at `+0x48` |

## Camera Chains

Each list dereferences every value except the last offset, which addresses the
final `double`.

| Value | Module root | Chain |
|---|---|---|
| Zoom X | `Adobe AIR.dll + 0x1218FE0` | `840, 4A0, 18, 230, 690, 30, 20` |
| Zoom Y | `Adobe AIR.dll + 0x1218FE0` | `830, 200, 10, 110, 28, 30, 38` |
| Camera X | `Adobe AIR.dll + 0x1218FE0` | `800, C00, 10, 28, 2C0, E0, 40` |
| Camera Y | `Adobe AIR.dll + 0x1219038` | `288, 3C8, 118, 100, 690, 30, 48` |

## Conditional

| Object | Field | Value | Rule |
|---|---|---|---|
| Entity | local-player marker | `+0x250` | Accept value `9` only when exactly one valid fighter has it. Offline and ambiguous cases use movement calibration. |

## Post-Patch Candidates

| Object | Field | Current candidate | Legacy fallback |
|---|---|---|---|
| Entity | airborne | `+0x12C` | `+0x124` |
| Entity | CanDodge / dash-related flag | `+0x1A4` | `+0x190` |

`F3` switches only these two fields. Facing remains fixed at the confirmed
`+0x110` value.

## Unconfirmed or Disabled by Default

| Object | Field | Candidate |
|---|---|---|
| Entity | attacking mirror | `+0x0E4` |
| Entity | stunned | `+0x144` |
| Entity | edge/ledge state | `+0x154` |
| Entity | remaining recovery options | `+0x220` |
| Entity | dead | `+0x25C` |
| Entity | stocks | `+0x2A4` |
| Entity | knocked back | `+0x084` |

These values must be revalidated after patches. Features that would fail closed
when these values are wrong remain disabled by default.
